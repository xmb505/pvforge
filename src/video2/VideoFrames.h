#pragma once

#include <QCache>
#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace pvf::video2 {

// One decoded frame request. `sourceFrame` is a frame number at the project
// fps.  It is a double so 2x/4x export sampling can ask for the exact source
// time instead of floor()ing to a project frame (issue #131).
struct Request
{
    QString path;            // video asset content.*
    double sourceFrame = 0.0; // frame number inside the asset (project fps)
    int fps = 30;
    QSize size;              // target size; empty = original size
    bool proxy = true;       // playback proxy
};

// Decodes single video frames on demand and keeps a small in-memory LRU.
//
// The decoder stays open between requests: an export that walks frames in
// order seeks once, then decodes forward, instead of re-opening and seeking
// for every frame (issue #131).  A request for an earlier frame outside the
// sequential window falls back to a seek.  Not thread-safe: use one instance
// per thread.
class VideoFrames
{
public:
    explicit VideoFrames(int proxyMaxWidth = 960, int memoryEntries = 64);
    ~VideoFrames();

    VideoFrames(const VideoFrames&) = delete;
    VideoFrames& operator=(const VideoFrames&) = delete;

    // Returns a null image and fills `error` on failure.
    QImage frame(const Request& request, QString* error = nullptr);

    // Clears the cached frames, the decoder state and the statistics.
    void clear();

    struct Stats
    {
        int hits = 0;
        int misses = 0;
        int decodes = 0;
        // Decoder session counters: a sequential export opens the file once and
        // seeks once; a random-access path opens/seeks per miss.
        int opens = 0;
        int seeks = 0;
    };
    Stats stats() const;

private:
    QString cacheKey(const Request& request) const;

    struct Decoder;

    int m_proxyMaxWidth = 960;
    QCache<QString, QImage> m_cache;
    std::unique_ptr<Decoder> m_decoder;
    Stats m_stats;
};

} // namespace pvf::video2
