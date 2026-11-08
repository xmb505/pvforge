#include "VideoExporter.h"

#include "audio/AudioEngine.h"

#include <QFile>
#include <QFileInfo>
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
constexpr qint64 kMaxPipeBacklog = 16 * 1024 * 1024;

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
    stopProcess();
    if (wasRunning)
        QFile::remove(m_partialPath);
    delete m_temp;
}

QString VideoExporter::ffmpegExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

bool VideoExporter::start(const Project& project, const ExportOptions& options,
                          FrameRenderer renderFrame, QString* error)
{
    auto bail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (m_running)
        return bail(QStringLiteral("已经在导出中"));
    if (!renderFrame)
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
    m_render = std::move(renderFrame);
    m_nextFrame = 0;
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
    arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("medium")
              << QStringLiteral("-crf") << QString::number(m_options.crf)
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
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

    if (m_nextFrame >= m_options.frameCount)
    {
        m_timer->stop();
        m_process->closeWriteChannel();
        return;
    }
    // Give ffmpeg time to drain the pipe instead of buffering the whole video
    // in memory when encoding is slower than rendering.
    if (m_process->bytesToWrite() > kMaxPipeBacklog)
        return;

    QImage image = m_render(m_nextFrame);
    if (image.isNull())
    {
        fail(QStringLiteral("渲染第 %1 帧失败").arg(m_nextFrame));
        return;
    }
    if (image.size() != m_options.size)
        image = image.scaled(m_options.size, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // Little-endian ARGB32 is BGRA in memory, which matches ffmpeg's bgra.
    const int rowBytes = m_options.size.width() * 4;
    const qint64 frameBytes = qint64(rowBytes) * m_options.size.height();
    qint64 written = 0;
    if (image.bytesPerLine() == rowBytes)
    {
        written = m_process->write(
            reinterpret_cast<const char*>(image.constBits()), frameBytes);
    }
    else
    {
        QByteArray rows;
        rows.reserve(int(frameBytes));
        for (int y = 0; y < image.height(); ++y)
            rows.append(reinterpret_cast<const char*>(image.constScanLine(y)),
                        rowBytes);
        written = m_process->write(rows);
    }
    if (written != frameBytes)
    {
        fail(QStringLiteral("写入 ffmpeg 失败"));
        return;
    }

    ++m_nextFrame;
    emit progress(m_nextFrame, m_options.frameCount);
}

void VideoExporter::cancel()
{
    if (!m_running)
        return;
    m_running = false;
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
