#pragma once

#include "html2/PvhtmlRenderer.h"
#include "htmlcache/FrameKey.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace pvf::htmlcache {
class MemoryCache;
class TieredCache;
}

namespace pvf::html2 {

class FrameCacheWorker;
class FrameDiskWorker;
class DiskCacheProxy;
class LockedFrameCache;

struct Stats
{
    int memoryHits = 0;
    int diskHits = 0;
    int renders = 0;
    int prefetched = 0;
};

// Manages the three headless browsers described in docs/ENTITY_MODEL.md 6.5:
//
//  - frontend: renders the current low-resolution frame on the caller's thread
//    and is started by start();
//  - cache:    renders prefetched low-resolution frames on a private QThread,
//    one at a time, and is started by start();
//  - final:    renders high-resolution frames for export, started lazily by the
//    first finalFrame() call. Final frames are never cached.
//
// Low-resolution frames go through TieredCache(MemoryCache 256 MB,
// DiskCache(cacheRoot, 2 GB)). The key is assetHash + local frame + variables +
// output size + frontend Chrome version + entry file hash. With
// timeDependent=false the request is normalised to localFrame=0, time=0 before
// both the lookup and the render, so every timeline frame shares one entry.
//
// frame(), finalFrame() and prefetch() are meant to be called from the thread
// that owns the service (usually the UI thread). The memory tier is guarded by
// a mutex; the SQLite-backed DiskCache lives on its own thread because
// QSqlDatabase connections are bound to their creating thread. Prefetch
// failures are warnings; they never terminate the service.
class HtmlFrameService : public QObject
{
    Q_OBJECT
    friend class FrameCacheWorker;

public:
    explicit HtmlFrameService(QString cacheRoot, QObject* parent = nullptr);
    ~HtmlFrameService() override;

    HtmlFrameService(const HtmlFrameService&) = delete;
    HtmlFrameService& operator=(const HtmlFrameService&) = delete;

    // Starts the frontend and cache browsers. Returns false when either fails;
    // in that case nothing is left running. The final browser is not started.
    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const;

    // Low resolution: memory -> disk cache, then the frontend browser. A miss
    // renders synchronously and writes the result back to both tiers.
    QImage frame(const FrameRequest& request, QString* error = nullptr);
    // High resolution: always uses the final browser. Nothing is cached.
    QImage finalFrame(const FrameRequest& request, QString* error = nullptr);

    // Replaces the pending prefetch batch with `requests` (older pending work
    // is discarded, the frame currently rendering is allowed to finish) and
    // wakes the cache thread. Requests already in the cache are not rendered
    // again but still emit prefetchedFrame so the batch stays observable.
    void prefetch(const QVector<FrameRequest>& requests);
    void clearAsset(const AssetHash& assetHash);

    QString chromeVersion() const;
    Stats stats() const;

signals:
    void prefetchedFrame(const AssetHash& assetHash, int localFrame);
    void prefetchError(const QString& message);

private:
    enum class CacheTier
    {
        Miss,
        Memory,
        Disk
    };

    bool ensureStarted(QString* error);
    FrameRequest normalizedRequest(const FrameRequest& request) const;
    htmlcache::FrameKey keyFor(const FrameRequest& request) const;
    QString entryHashFor(const QString& bundleDir, const QString& entry) const;

    CacheTier lookupCached(const htmlcache::FrameKey& key, QImage* image);
    void storeCached(const htmlcache::FrameKey& key, const QImage& image);

    void noteHit(CacheTier tier);
    void noteRender();
    void notePrefetched();
    void queuePrefetchedSignal(const AssetHash& assetHash, int localFrame);
    void queuePrefetchError(const QString& message);

    QString m_cacheRoot;
    QString m_chromeVersion;
    bool m_running = false;

    PvhtmlRenderer* m_frontend = nullptr;
    PvhtmlRenderer* m_final = nullptr;
    FrameCacheWorker* m_cacheWorker = nullptr;

    QMutex m_cacheMutex;
    std::unique_ptr<htmlcache::MemoryCache> m_memory;
    FrameDiskWorker* m_diskWorker = nullptr;
    std::unique_ptr<LockedFrameCache> m_memoryProxy;
    std::unique_ptr<DiskCacheProxy> m_diskProxy;
    std::unique_ptr<htmlcache::TieredCache> m_tiered;

    std::atomic<int> m_memoryHits{0};
    std::atomic<int> m_diskHits{0};
    std::atomic<int> m_renders{0};
    std::atomic<int> m_prefetched{0};
};

} // namespace pvf::html2
