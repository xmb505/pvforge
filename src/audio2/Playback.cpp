#include "Audio2.h"

#include "AudioStream.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pvf::audio2 {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

} // namespace

Playback::Playback(const model::Project& project,
                   const assets::AssetStore& assets, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_assets(assets)
{
}

Playback::~Playback()
{
    stop();
}

bool Playback::play(int startFrame, int endFrame, int fps, QString* error)
{
    stop();
    m_startFrame = startFrame;
    m_endFrame = endFrame;
    m_fps = std::max(1, fps);

    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (fps <= 0)
        return fail(QStringLiteral("帧率无效"));
    if (endFrame <= startFrame)
        return fail(QStringLiteral("播放范围为空"));

    // Snapshot on the UI thread: the mixer copies the project and resolves the
    // content files once, so the audio thread never races document edits and
    // never mixes more than the current chunk (issue #143).
    QStringList captureWarnings;
    AudioSnapshot snapshot = captureAudioSnapshot(m_project, m_assets, startFrame,
                                                  endFrame, m_fps,
                                                  &captureWarnings);
    if (snapshot.contentPaths.isEmpty())
    {
        return fail(captureWarnings.isEmpty()
                        ? QStringLiteral("所选范围内没有可播放的音频")
                        : captureWarnings.join(QStringLiteral("；")));
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return fail(QStringLiteral("没有可用的音频输出设备"));

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format))
    {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            return fail(QStringLiteral("音频输出设备不支持 48 kHz 双声道格式"));
    }

    if (m_sink)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }

    m_stream = new AudioStream(std::move(snapshot), format.sampleFormat(), this);
    m_stream->open(QIODevice::ReadOnly);

    m_sink = new QAudioSink(device, format, this);
    connect(m_sink, &QAudioSink::stateChanged, this,
            [this](QAudio::State state) {
                if (state == QAudio::StoppedState || state == QAudio::IdleState)
                    m_playing = false;
            });

    m_sink->start(m_stream);
    if (m_sink->error() != QAudio::NoError)
    {
        const QStringList warnings = m_stream->warnings();
        Q_UNUSED(warnings);
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        m_stream->cancel();
        delete m_stream;
        m_stream = nullptr;
        return fail(QStringLiteral("音频输出启动失败"));
    }

    m_playing = true;
    return true;
}

void Playback::stop()
{
    if (m_sink)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_stream)
    {
        m_stream->cancel();
        delete m_stream;
        m_stream = nullptr;
    }
    m_playing = false;
}

bool Playback::isPlaying() const
{
    return m_playing;
}

int Playback::startFrame() const
{
    return m_startFrame;
}

int Playback::endFrame() const
{
    return m_endFrame;
}

int Playback::positionFrame() const
{
    if (!m_sink || !m_playing)
        return -1;
    // processedUSecs counts audio the device actually consumed, so the clock
    // stays honest even when the sink buffered ahead.
    const qint64 playedUs = m_sink->processedUSecs();
    if (playedUs < 0)
        return -1;
    return m_startFrame
           + int((playedUs * qint64(m_fps)) / 1000000LL);
}

} // namespace pvf::audio2
