#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QAudioSink;

namespace pvf::model {
struct Project;
}

namespace pvf::assets {
class AssetStore;
}

namespace pvf::audio2 {

struct MixOptions
{
    int startFrame = 0;
    int endFrame = 0; // half-open
    int fps = 30;
    int sampleRate = 48000;
    int channels = 2;
};

// 48 kHz stereo interleaved float PCM; empty when no audible audio.
QByteArray mixRange(const model::Project& project,
                    const assets::AssetStore& assets, const MixOptions& options,
                    QStringList* warnings = nullptr);

class Playback : public QObject
{
    Q_OBJECT
public:
    explicit Playback(const model::Project& project,
                      const assets::AssetStore& assets,
                      QObject* parent = nullptr);
    ~Playback() override;

    bool play(int startFrame, int endFrame, int fps, QString* error = nullptr);
    void stop();
    bool isPlaying() const;
    int startFrame() const;
    int endFrame() const;

private:
    const model::Project& m_project;
    const assets::AssetStore& m_assets;
    QAudioSink* m_sink = nullptr;
    QBuffer m_buffer;
    QByteArray m_pcm;
    bool m_playing = false;
    int m_startFrame = 0;
    int m_endFrame = 0;
};

} // namespace pvf::audio2
