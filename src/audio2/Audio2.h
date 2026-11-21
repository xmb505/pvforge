#pragma once

#include "common/Types.h"

#include <QAudioFormat>
#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QAudioDevice;
class QAudioSink;
class QIODevice;

namespace pvf::model {
struct Project;
}

namespace pvf::assets {
class AssetStore;
}

namespace pvf::audio2 {

class AudioStream;

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

// Same mixing, but content files come from a pre-resolved snapshot instead of
// the AssetStore.  Used by the background stream so the audio thread never
// reads the live project or asset metadata (issue #143).  Hashes missing from
// `contentPaths` are skipped with a warning.
QByteArray mixRangeWithPaths(
    const model::Project& project,
    const QHash<AssetHash, QString>& contentPaths, const MixOptions& options,
    QStringList* warnings = nullptr);

// One scrub blip as 48 kHz stereo interleaved float PCM (no WAV header).
// A right move mixes the traversed half-open frame range `[fromFrame, toFrame)`
// (at most kMaxScrubFrames); a left move mixes `[toFrame, fromFrame)` and
// reverses it by stereo sample frame.  Both ends get a 2 ms linear de-click
// ramp.  Returns an empty array when the range is empty or has no audio.
QByteArray buildScrubSlice(const model::Project& project,
                           const assets::AssetStore& assets,
                           int fromFrame, int toFrame, int fps,
                           int sampleRate = 48000, int channels = 2);

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
    // Audition the frame range without touching the main playback sink.
    // Returns false while `play()` is active, when there is no output device,
    // or when the range has no audible audio.
    bool scrub(int fromFrame, int toFrame, int fps);
    bool isPlaying() const;
    int startFrame() const;
    int endFrame() const;
    // Project frame derived from the samples the output device has played
    // since the last play()/restart.  -1 when no audio clock is available.
    // PlaybackController uses it to keep the picture and soundtrack together.
    int positionFrame() const;

#ifdef PVFORGE_TEST_HOOKS
    void setPlayingForTest(bool playing) { m_playing = playing; }
#endif

private:
    void stopScrub();
    bool ensureScrubFormat(const QAudioDevice& device);

    const model::Project& m_project;
    const assets::AssetStore& m_assets;
    QAudioSink* m_sink = nullptr;
    AudioStream* m_stream = nullptr;
    // Independent, reusable sink for frame scrubbing.  It never shares state
    // with m_sink, so a failed scrub cannot stall full playback.
    QAudioSink* m_scrubSink = nullptr;
    QByteArray m_scrubDeviceId;
    QBuffer m_scrubBuffer;
    QByteArray m_scrubPcm;
    QAudioFormat m_scrubFormat;
    bool m_playing = false;
    int m_startFrame = 0;
    int m_endFrame = 0;
    int m_fps = 30;
};

} // namespace pvf::audio2
