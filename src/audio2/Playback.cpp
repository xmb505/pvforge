#include "Audio2.h"

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

QByteArray toInt16Pcm(const QByteArray& pcm)
{
    const auto* source = reinterpret_cast<const float*>(pcm.constData());
    const int count = pcm.size() / int(sizeof(float));
    QByteArray converted;
    converted.resize(count * int(sizeof(qint16)));
    auto* target = reinterpret_cast<qint16*>(converted.data());
    for (int i = 0; i < count; ++i)
    {
        target[i] = qint16(std::lround(
            std::clamp(source[i], -1.0f, 1.0f) * 32767.0f));
    }
    return converted;
}

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

    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (fps <= 0)
        return fail(QStringLiteral("帧率无效"));
    if (endFrame <= startFrame)
        return fail(QStringLiteral("播放范围为空"));

    MixOptions options;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    options.fps = fps;
    options.sampleRate = kSampleRate;
    options.channels = kChannels;

    QStringList warnings;
    m_pcm = mixRange(m_project, m_assets, options, &warnings);
    if (m_pcm.isEmpty())
        return fail(QStringLiteral("所选范围内没有可播放的音频"));

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return fail(QStringLiteral("没有可用的音频输出设备"));

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Float);
    QByteArray playbackPcm = m_pcm;
    if (!device.isFormatSupported(format))
    {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            return fail(QStringLiteral("音频输出设备不支持 48 kHz 双声道格式"));
        playbackPcm = toInt16Pcm(m_pcm);
    }

    if (m_sink)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }

    m_sink = new QAudioSink(device, format, this);
    connect(m_sink, &QAudioSink::stateChanged, this,
            [this](QAudio::State state) {
                if (state == QAudio::StoppedState || state == QAudio::IdleState)
                    m_playing = false;
            });

    m_buffer.close();
    m_buffer.setData(playbackPcm);
    m_buffer.open(QIODevice::ReadOnly);
    m_sink->start(&m_buffer);
    if (m_sink->error() != QAudio::NoError)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        m_buffer.close();
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
    m_buffer.close();
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

} // namespace pvf::audio2
