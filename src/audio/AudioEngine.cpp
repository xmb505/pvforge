#include "AudioEngine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
}

AudioEngine::AudioEngine(Project* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
}

QByteArray AudioEngine::mixRange(const Project& project, double startSeconds,
                                 double endSeconds)
{
    if (endSeconds <= startSeconds)
        return QByteArray();

    const qint64 outputFrames =
        qint64(std::ceil((endSeconds - startSeconds) * kSampleRate));
    if (outputFrames <= 0 || outputFrames > (qint64(1) << 31))
        return QByteArray();

    QVector<qint32> mix(int(outputFrames * kChannels), 0);
    const double fps = std::max(1, project.fps);

    for (const Entity& entity : project.entities())
    {
        if (entity.type != EntityType::Audio || entity.hidden || !entity.audio)
            continue;
        const AudioClip& clip = *entity.audio;
        const double clipStart =
            entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble() / fps;
        const double trimStart =
            entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            entity.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        const double clipDuration =
            std::max(0.0, clip.duration - trimStart - trimEnd);
        const double clipEnd = clipStart + clipDuration;
        const double volume =
            std::max(0.0, entity.data.value(QStringLiteral("volume"), 1.0).toDouble());
        const double fadeIn =
            std::max(0.0, entity.data.value(QStringLiteral("fadeIn"), 0.0).toDouble());
        const double fadeOut =
            std::max(0.0, entity.data.value(QStringLiteral("fadeOut"), 0.0).toDouble());

        const double overlapStart = std::max(startSeconds, clipStart);
        const double overlapEnd = std::min(endSeconds, clipEnd);
        if (overlapEnd <= overlapStart)
            continue;

        const qint64 firstFrame =
            qint64(std::ceil((overlapStart - startSeconds) * kSampleRate));
        const qint64 lastFrame =
            qint64(std::ceil((overlapEnd - startSeconds) * kSampleRate));
        for (qint64 frame = firstFrame; frame < lastFrame; ++frame)
        {
            if (frame < 0 || frame >= outputFrames)
                continue;
            const double t = startSeconds + frame / double(kSampleRate);
            const double fileTime = trimStart + (t - clipStart);
            const qint64 sourceFrame = qint64(std::llround(fileTime * kSampleRate));
            if (sourceFrame < 0 || sourceFrame >= clip.samples.size() / kChannels)
                continue;

            double gain = volume;
            if (fadeIn > 0.0)
                gain *= std::clamp((t - clipStart) / fadeIn, 0.0, 1.0);
            if (fadeOut > 0.0)
                gain *= std::clamp((clipEnd - t) / fadeOut, 0.0, 1.0);

            const int sourceIndex = int(sourceFrame) * kChannels;
            const int outputIndex = int(frame) * kChannels;
            mix[outputIndex] += qint32(clip.samples[sourceIndex] * gain);
            mix[outputIndex + 1] += qint32(clip.samples[sourceIndex + 1] * gain);
        }
    }

    QByteArray pcm;
    pcm.resize(int(outputFrames * kChannels * 2));
    auto* output = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < mix.size(); ++i)
        output[i] = qint16(std::clamp(mix[i], -32768, 32767));
    return pcm;
}

bool AudioEngine::play(int startFrame)
{
    stop();
    if (!m_project)
        return false;

    const double fps = std::max(1, m_project->fps);
    m_startSeconds = std::max(0, startFrame) / fps;
    m_endSeconds = m_project->timelineLength() / fps;
    if (m_endSeconds <= m_startSeconds + 1.0 / kSampleRate)
        return false;

    m_pcm = mixRange(*m_project, m_startSeconds, m_endSeconds);
    if (m_pcm.isEmpty())
        return false;

    // If everything is silent, don't take over the animation clock.
    bool audible = false;
    const auto* samples = reinterpret_cast<const qint16*>(m_pcm.constData());
    for (int i = 0; i < m_pcm.size() / 2; ++i)
    {
        if (samples[i] != 0)
        {
            audible = true;
            break;
        }
    }
    if (!audible)
        return false;

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(format))
        return false;

    if (m_sink)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    m_sink = new QAudioSink(device, format, this);

    m_buffer.close();
    m_buffer.setData(m_pcm);
    m_buffer.open(QIODevice::ReadOnly);
    m_sink->start(&m_buffer);
    m_running = true;
    return true;
}

void AudioEngine::stop()
{
    if (m_sink)
        m_sink->stop();
    m_buffer.close();
    m_running = false;
}

double AudioEngine::positionSeconds() const
{
    if (!m_running || !m_sink)
        return m_startSeconds;
    return std::min(m_endSeconds,
                    m_startSeconds + m_sink->processedUSecs() / 1000000.0);
}

} // namespace pvf
