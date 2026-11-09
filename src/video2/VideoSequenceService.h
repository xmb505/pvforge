#pragma once

#include "common/Types.h"
#include "video2/VideoFrames.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QString>

#include <atomic>
#include <functional>

namespace pvf::video2 {

struct SequenceOptions
{
    QString cacheRoot;         // project .cache/video
    AssetHash assetHash;
    QString videoPath;
    int projectFps = 30;
    int maxWidth = 960;
    qint64 diskBudgetBytes = 2LL * 1024 * 1024 * 1024;
    int memoryEntries = 64;
};

struct SequenceProgress
{
    int done = 0;
    int total = 0;
    bool cancelled = false;
};

// Extracts a video into a content-addressed WebP image sequence using the
// project fps. Identical decoded frames share one WebP file and one memory
// entry. The index is a JSON file and survives restarts.
//
// Synchronous and not thread-safe: use one instance per thread. `extract()`
// can be cancelled from the progress callback (return false, or const_cast
// SequenceProgress::cancelled), or from another thread through cancel().
class VideoSequenceService
{
public:
    explicit VideoSequenceService(SequenceOptions options);
    ~VideoSequenceService();

    VideoSequenceService(const VideoSequenceService&) = delete;
    VideoSequenceService& operator=(const VideoSequenceService&) = delete;

    // Extracts every project frame. Returns false on failure or cancellation;
    // frames finished before a cancellation stay in the cache.
    bool extract(const std::function<bool(const SequenceProgress&)>& progress = {});

    // Returns the project frame from the memory cache, the disk sequence, or
    // the original video (fallback). A null image and `error` means failure.
    QImage frame(int projectFrame, QString* error = nullptr);

    bool hasFrame(int projectFrame) const;

    // Drops the in-memory state and deletes this cache key's files.
    void clear();

    struct Stats
    {
        int memoryHits = 0;
        int diskHits = 0;
        int fallbacks = 0;
        int uniqueImages = 0;
    };
    Stats stats() const;

    // Requests cancellation. Safe to call from another thread.
    void cancel();

    int totalFrames() const;
    QString cacheDirectory() const;
    QString indexFilePath() const;

    // Last error reported by extract()/frame().
    QString lastError() const;

private:
    struct ImageEntry
    {
        qint64 size = 0;
        qint64 lastAccess = 0;
        int width = 0;
        int height = 0;
    };

    QString webpPath(const AssetHash& hash) const;
    bool imageAvailable(const AssetHash& hash) const;
    bool loadIndex(QString* error);
    bool saveIndex(QString* error);
    void removeAllFiles();

    void touch(const AssetHash& hash);
    void scheduleIndexSave();
    void rememberMemory(const AssetHash& hash, const QImage& image);
    void evict(const AssetHash& hash);
    void enforceDiskBudget();

    SequenceOptions m_options;
    QString m_cacheDir;
    QString m_indexPath;
    QString m_setupError;

    VideoFrames m_decoder;
    QCache<QString, QImage> m_memory;

    QHash<int, AssetHash> m_frames;
    QHash<AssetHash, ImageEntry> m_images;
    qint64 m_totalBytes = 0;
    qint64 m_clock = 0;
    qint64 m_lastSaveMs = 0;
    int m_touchesSinceSave = 0;
    int m_totalFrames = 0;
    bool m_ready = false;
    bool m_indexDirty = false;
    std::atomic_bool m_cancelled{false};
    QString m_lastError;

    Stats m_stats;
};

} // namespace pvf::video2
