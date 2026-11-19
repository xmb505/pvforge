#pragma once

#include "Audio2.h"
#include "assets/AssetStore.h"
#include "model/Project.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QHash>
#include <QIODevice>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>
#include <thread>

namespace pvf::audio2 {

// Everything the background mixer reads, captured on the UI thread (issue
// #143): a deep copy of the project plus the resolved content file for every
// audio asset.  The mixer never touches the live document or the AssetStore,
// so edits during playback cannot race the audio thread.
struct AudioSnapshot
{
    model::Project project;
    QHash<AssetHash, QString> contentPaths;
    MixOptions options;
};

// Resolves audio content files and copies the project.  `warnings` receives
// the assets that could not be resolved; an empty contentPaths means the range
// has no audio entity at all.
AudioSnapshot captureAudioSnapshot(const model::Project& project,
                                   const assets::AssetStore& assets,
                                   int startFrame, int endFrame, int fps,
                                   QStringList* warnings = nullptr);

// Mixes one bounded sub-range from a snapshot.  Never opens the AssetStore and
// never allocates more than the sub-range needs.
QByteArray mixSnapshotRange(const AudioSnapshot& snapshot, int startFrame,
                            int endFrame, QStringList* warnings = nullptr);

// Streaming PCM source for QAudioSink.  A worker thread mixes bounded chunks
// into a fixed ring buffer and readData() blocks on the audio thread until
// data arrives.  The UI thread never mixes a whole clip, seek only remixes
// from the requested position, and memory stays constant for any range length
// (issue #143).
class AudioStream : public QIODevice
{
public:
    // `sampleFormat` is Float or Int16; Int16 chunks are converted as they are
    // produced so the device still sees its native format.
    AudioStream(AudioSnapshot snapshot, QAudioFormat::SampleFormat sampleFormat,
                QObject* parent = nullptr);
    ~AudioStream() override;

    bool open(OpenMode mode) override;
    void close() override;
    // Cancels the producer and wakes every reader; safe from any thread.
    void cancel();

    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char*, qint64) override { return -1; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    QStringList warnings() const;
    bool finished() const;
    qint64 bufferCapacity() const;
    qint64 framesMixed() const;
    // Upper bound for one produced chunk, in PCM frames.
    int chunkFrames() const;

private:
    void produce();
    bool push(const QByteArray& pcm);
    QByteArray convertForDevice(const QByteArray& pcm) const;

    AudioSnapshot m_snapshot;
    QAudioFormat::SampleFormat m_sampleFormat = QAudioFormat::Float;

    mutable QMutex m_mutex;
    QWaitCondition m_notEmpty;
    QWaitCondition m_notFull;
    QByteArray m_ring;
    qint64 m_head = 0; // read position
    qint64 m_tail = 0; // write position
    qint64 m_size = 0;
    qint64 m_capacity = 0;
    bool m_finished = false;
    bool m_cancelled = false;
    bool m_started = false;
    QStringList m_warnings;

    std::atomic<qint64> m_framesMixed{0};
    std::thread m_thread;
};

} // namespace pvf::audio2
