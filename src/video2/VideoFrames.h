#pragma once

#include <QCache>
#include <QImage>
#include <QSize>
#include <QString>

namespace pvf::video2 {

// One decoded frame request. `sourceFrame` is a frame number at the project
// fps; the decoder converts it to the video's own time base.
struct Request
{
    QString path;          // video asset content.*
    int sourceFrame = 0;   // frame number inside the asset (project fps)
    int fps = 30;
    QSize size;            // target size; empty = original size
    bool proxy = true;     // playback proxy
};

// Decodes single video frames on demand and keeps a small in-memory LRU.
// Not thread-safe: use one instance per thread.
class VideoFrames
{
public:
    explicit VideoFrames(int proxyMaxWidth = 960, int memoryEntries = 64);

    // Returns a null image and fills `error` on failure.
    QImage frame(const Request& request, QString* error = nullptr);

    // Clears the cached frames and resets the statistics.
    void clear();

    struct Stats
    {
        int hits = 0;
        int misses = 0;
        int decodes = 0;
    };
    Stats stats() const;

private:
    QString cacheKey(const Request& request) const;

    int m_proxyMaxWidth = 960;
    QCache<QString, QImage> m_cache;
    Stats m_stats;
};

} // namespace pvf::video2
