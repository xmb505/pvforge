#include "VideoExporter.h"

#include "audio/AudioEngine.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kMaxBufferedFrames = 3;

QString errorTail(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= 1200)
        return trimmed;
    return trimmed.right(1200);
}

bool anyAudible(const QByteArray& pcm)
{
    const auto* samples = reinterpret_cast<const qint16*>(pcm.constData());
    const int count = int(pcm.size() / 2);
    for (int i = 0; i < count; ++i)
        if (samples[i] != 0)
            return true;
    return false;
}

} // namespace

VideoExporter::VideoExporter(QObject* parent)
    : QObject(parent)
{
}

VideoExporter::~VideoExporter()
{
    const bool wasRunning = m_running;
    m_running = false;
    abandonFrame();
    if (m_timer)
        m_timer->stop();
    stopProcess();
    if (wasRunning)
        QFile::remove(m_partialPath);
    delete m_temp;
}

QString VideoExporter::ffmpegExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

bool VideoExporter::vaapiAvailable()
{
    static const bool available = [] {
        if (!QFileInfo(QStringLiteral("/dev/dri/renderD128")).isReadable())
            return false;
        const QString ffmpeg = ffmpegExecutable();
        if (ffmpeg.isEmpty())
            return false;
        QProcess probe;
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"),
                             QStringLiteral("-encoders")});
        if (!probe.waitForFinished(3000) || probe.exitCode() != 0)
            return false;
        return probe.readAllStandardOutput().contains("h264_vaapi");
    }();
    return available;
}

bool VideoExporter::start(const Project& project, const ExportOptions& options,
                          FrameProvider provider, QString* error)
{
    auto bail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (m_running)
        return bail(QStringLiteral("已经在导出中"));
    if (!provider)
        return bail(QStringLiteral("没有可用的渲染回调"));
    if (options.outputPath.trimmed().isEmpty())
        return bail(QStringLiteral("请选择输出文件"));
    if (options.frameCount <= 0)
        return bail(QStringLiteral("时间轴是空的，没有可导出的帧"));
    if (options.fps <= 0)
        return bail(QStringLiteral("帧率无效"));

    const QString ffmpeg = ffmpegExecutable();
    if (ffmpeg.isEmpty())
        return bail(QStringLiteral("找不到 ffmpeg 命令，无法导出 MP4"));

    m_options = options;
    m_options.outputPath = QFileInfo(options.outputPath).absoluteFilePath();
    // H.264 with 4:2:0 needs even dimensions.
    m_options.size = QSize(std::max(2, options.size.width() & ~1),
                           std::max(2, options.size.height() & ~1));
    m_options.crf = std::clamp(options.crf, 0, 51);
    m_options.frameTimeoutMs = std::max(1, options.frameTimeoutMs);
    if (m_options.encoder == ExportEncoder::H264Vaapi && !vaapiAvailable())
        m_options.encoder = ExportEncoder::H264Software;
    m_provider = std::move(provider);
    ++m_generation;
    m_nextFrame = 0;
    m_requestedFrame = -1;
    m_waiting = false;
    m_previewClock.invalidate();
    m_stderr.clear();

    delete m_temp;
    m_temp = new QTemporaryDir;
    if (!m_temp->isValid())
        return bail(QStringLiteral("无法创建临时文件目录"));

    QString audioPath;
    if (options.includeAudio)
    {
        const double duration = options.frameCount / double(m_options.fps);
        const QString candidate = m_temp->filePath(QStringLiteral("audio.pcm"));
        QFile file(candidate);
        if (file.open(QIODevice::WriteOnly))
        {
            // Mix in ten-second chunks so a long project does not need the
            // whole 48 kHz stereo track in memory at once.
            constexpr qint64 kChunkSamples = 10 * kSampleRate;
            const qint64 totalSamples =
                qint64(std::ceil(duration * kSampleRate));
            bool audible = false;
            bool written = true;
            for (qint64 start = 0; start < totalSamples; start += kChunkSamples)
            {
                const qint64 end = std::min(totalSamples, start + kChunkSamples);
                QByteArray pcm = AudioEngine::mixRange(
                    project, start / double(kSampleRate),
                    end / double(kSampleRate));
                if (pcm.isEmpty())
                {
                    written = false;
                    break;
                }
                // mixRange writes ceil(duration * rate) frames; trim or pad to
                // the exact sample count so chunk boundaries never drift.
                const int want = int((end - start) * kChannels * 2);
                if (pcm.size() < want)
                    pcm.append(want - pcm.size(), '\0');
                else if (pcm.size() > want)
                    pcm.truncate(want);
                if (!audible && anyAudible(pcm))
                    audible = true;
                if (file.write(pcm) != pcm.size())
                {
                    written = false;
                    break;
                }
            }
            file.close();
            if (written && audible)
                audioPath = candidate;
            else
                QFile::remove(candidate);
        }
    }

    m_partialPath = m_options.outputPath + QStringLiteral(".part.mp4");
    QFile::remove(m_partialPath);

    QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pixel_format"), QStringLiteral("bgra"),
        QStringLiteral("-video_size"),
        QStringLiteral("%1x%2").arg(m_options.size.width()).arg(m_options.size.height()),
        QStringLiteral("-framerate"), QString::number(m_options.fps),
        QStringLiteral("-i"), QStringLiteral("-")};
    if (!audioPath.isEmpty())
    {
        arguments << QStringLiteral("-f") << QStringLiteral("s16le")
                  << QStringLiteral("-ar") << QString::number(kSampleRate)
                  << QStringLiteral("-ac") << QString::number(kChannels)
                  << QStringLiteral("-i") << audioPath;
    }
    arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (!audioPath.isEmpty())
        arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    if (m_options.encoder == ExportEncoder::H264Vaapi)
    {
        // VAAPI takes raw BGRA, converts to NV12 on the GPU and encodes with
        // constant QP; CRF presets map to a comparable QP range.
        arguments << QStringLiteral("-vaapi_device")
                  << QStringLiteral("/dev/dri/renderD128")
                  << QStringLiteral("-vf") << QStringLiteral("format=nv12,hwupload")
                  << QStringLiteral("-c:v") << QStringLiteral("h264_vaapi")
                  << QStringLiteral("-rc_mode") << QStringLiteral("CQP")
                  << QStringLiteral("-qp")
                  << QString::number(std::clamp(m_options.crf + 3, 0, 51))
                  << QStringLiteral("-bf") << QStringLiteral("0");
    }
    else
    {
        arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
                  << QStringLiteral("-preset") << QStringLiteral("medium")
                  << QStringLiteral("-crf") << QString::number(m_options.crf)
                  << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    if (!audioPath.isEmpty())
        arguments << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("192k");
    arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << QStringLiteral("-f") << QStringLiteral("mp4") << m_partialPath;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        if (!m_process)
            return;
        m_stderr += QString::fromLocal8Bit(m_process->readAllStandardError());
        if (m_stderr.size() > 8000)
            m_stderr = m_stderr.right(4000);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                if (!m_running)
                    return;
                if (m_nextFrame < m_options.frameCount)
                {
                    fail(QStringLiteral("ffmpeg 提前退出：%1")
                             .arg(errorTail(m_stderr)));
                    return;
                }
                if (status != QProcess::NormalExit || code != 0)
                {
                    fail(QStringLiteral("ffmpeg 导出失败（退出码 %1）：%2")
                             .arg(code)
                             .arg(errorTail(m_stderr)));
                    return;
                }
                QFile::remove(m_options.outputPath);
                if (!QFile::rename(m_partialPath, m_options.outputPath))
                {
                    fail(QStringLiteral("无法写入输出文件"));
                    return;
                }
                finish(true);
            });

    m_process->start(ffmpeg, arguments);
    if (!m_process->waitForStarted(3000)
        || m_process->state() != QProcess::Running)
    {
        m_process->deleteLater();
        m_process = nullptr;
        delete m_temp;
        m_temp = nullptr;
        return bail(QStringLiteral("ffmpeg 未能启动"));
    }

    m_running = true;
    if (!m_timer)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(0);
        connect(m_timer, &QTimer::timeout, this, &VideoExporter::tick);
    }
    m_timer->start();
    return true;
}

void VideoExporter::tick()
{
    if (!m_running || !m_process || !m_timer)
        return;
    if (m_process->state() != QProcess::Running)
        return;   // the finished handler reports the result

    // A provider callback is outstanding; the requestFrame() completion
    // restarts the tick timer.
    if (m_waiting)
        return;

    if (m_nextFrame >= m_options.frameCount)
    {
        m_timer->stop();
        m_process->closeWriteChannel();
        return;
    }
    // Give ffmpeg time to drain the pipe instead of buffering the whole video
    // in memory when encoding is slower than rendering. Three frames keep the
    // progress and time estimate close to the actual encoder position.
    const qint64 bytesPerFrame =
        qint64(m_options.size.width()) * m_options.size.height() * 4;
    const qint64 backlogLimit =
        std::max<qint64>(1 << 20, bytesPerFrame * kMaxBufferedFrames);
    if (m_process->bytesToWrite() > backlogLimit)
        return;

    requestFrame();
}

void VideoExporter::requestFrame()
{
    const int frame = m_nextFrame;
    m_waiting = true;
    m_requestedFrame = frame;

    if (!m_frameTimer)
    {
        m_frameTimer = new QTimer(this);
        m_frameTimer->setSingleShot(true);
        connect(m_frameTimer, &QTimer::timeout, this,
                &VideoExporter::onFrameTimeout);
    }
    m_frameTimer->start(m_options.frameTimeoutMs);

    // Stop polling while the provider works; onFrame() restarts the timer so
    // a slow provider never spins the event loop.
    m_timer->stop();

    // Queue the completion even for a synchronous provider. This keeps the
    // exporter state machine single-threaded and prevents a callback that
    // arrives from another thread from touching QProcess directly. The
    // QPointer guard makes a provider that answers after the exporter was
    // destroyed a no-op instead of a dangling call.
    const quint64 token = m_generation;
    const QPointer<VideoExporter> guard(this);
    m_provider(frame, [guard, token, frame](const QImage& image) {
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, token, frame, image] {
                if (guard)
                    guard->onFrame(token, frame, image);
            },
            Qt::QueuedConnection);
    });
}

void VideoExporter::onFrame(quint64 token, int frame, const QImage& image)
{
    // Ignore callbacks from a cancelled, failed or restarted export, and
    // duplicate answers to a request that was already handled.
    if (!m_running || token != m_generation || !m_waiting
        || frame != m_requestedFrame)
    {
        return;
    }
    m_waiting = false;
    m_requestedFrame = -1;
    if (m_frameTimer)
        m_frameTimer->stop();

    if (image.isNull())
    {
        fail(QStringLiteral("渲染第 %1 帧失败").arg(frame));
        return;
    }

    QImage output = image;
    if (output.size() != m_options.size)
        output = output.scaled(m_options.size, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    if (output.format() != QImage::Format_ARGB32_Premultiplied)
        output = output.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // Little-endian ARGB32 is BGRA in memory, which matches ffmpeg's bgra.
    const int rowBytes = m_options.size.width() * 4;
    const qint64 frameBytes = qint64(rowBytes) * m_options.size.height();
    qint64 written = 0;
    if (output.bytesPerLine() == rowBytes)
    {
        written = m_process->write(
            reinterpret_cast<const char*>(output.constBits()), frameBytes);
    }
    else
    {
        QByteArray rows;
        rows.reserve(int(frameBytes));
        for (int y = 0; y < output.height(); ++y)
            rows.append(
                reinterpret_cast<const char*>(output.constScanLine(y)),
                rowBytes);
        written = m_process->write(rows);
    }
    if (written != frameBytes)
    {
        fail(QStringLiteral("写入 ffmpeg 失败"));
        return;
    }

    ++m_nextFrame;
    if (!m_previewClock.isValid() || m_previewClock.elapsed() >= 80)
    {
        m_previewClock.start();
        emit previewFrame(output);
    }
    emit progress(m_nextFrame, m_options.frameCount);

    if (m_running && m_timer)
        m_timer->start();
}

void VideoExporter::onFrameTimeout()
{
    if (!m_running || !m_waiting)
        return;
    fail(QStringLiteral("第 %1 帧渲染超时（%2 毫秒）")
             .arg(m_requestedFrame)
             .arg(m_options.frameTimeoutMs));
}

void VideoExporter::abandonFrame()
{
    m_waiting = false;
    m_requestedFrame = -1;
    if (m_frameTimer)
        m_frameTimer->stop();
    // Invalidate every callback captured for this export.
    ++m_generation;
}

void VideoExporter::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    abandonFrame();
    if (m_timer)
        m_timer->stop();
    stopProcess();
    QFile::remove(m_partialPath);
    delete m_temp;
    m_temp = nullptr;
    emit finished(false, QString());
}

void VideoExporter::fail(const QString& error)
{
    if (!m_running)
        return;
    m_running = false;
    abandonFrame();
    if (m_timer)
        m_timer->stop();
    stopProcess();
    QFile::remove(m_partialPath);
    delete m_temp;
    m_temp = nullptr;
    emit finished(false, error);
}

void VideoExporter::finish(bool success)
{
    m_running = false;
    abandonFrame();
    if (m_timer)
        m_timer->stop();
    if (m_process)
    {
        m_process->closeWriteChannel();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (!success)
        QFile::remove(m_partialPath);
    delete m_temp;
    m_temp = nullptr;
    emit finished(success, success ? QString() : errorTail(m_stderr));
}

void VideoExporter::stopProcess()
{
    if (!m_process)
        return;
    if (m_process->state() != QProcess::NotRunning)
    {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_process->deleteLater();
    m_process = nullptr;
}

} // namespace pvf
