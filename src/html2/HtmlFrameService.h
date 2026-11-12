#pragma once

#include "html2/PvhtmlRenderer.h"
#include "htmlcache/FrameKey.h"

#include <QFuture>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSharedPointer>
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
class FrontendWorker;
class DiskCacheProxy;
class LockedFrameCache;

struct Stats
{
    int memoryHits = 0;
    int diskHits = 0;
    int renders = 0;
    int prefetched = 0;
};

// Manages the three headless browsers described in docs/AI/ENTITY_MODEL.md 6.5:
//
//  - frontend: renders the current low-resolution frame on a private QThread,
//    one at a time, and is started by start();
//  - cache:    renders prefetched low-resolution frames on a second private
//    QThread, one at a time, and is started by start();
//  - final:    renders high-resolution frames for export on the caller's
//    thread, started lazily by the first finalFrame() call. Final frames are
//    never cached.
//
// Threading of the final renderer: the first finalFrame() creates the final
// PvhtmlRenderer (and its browser) on the calling thread, and Qt keeps that
// QObject and its CDP sockets bound to it. finalFrame() must therefore always
// be called from exactly one thread. Successive export tasks must reuse that
// thread, or call stopFinal() on it when the task ends. There is no
// cross-thread migration; stopFinal() itself must run on the thread that first
// called finalFrame(), so a later task on another thread can create a fresh
// renderer. Violating either rule is reported as a warning and the call fails
// instead of silently rendering from the wrong thread.
//
// Low-resolution frames go through TieredCache(MemoryCache,
// DiskCache(cacheRoot)), with 256 MB / 2 GB defaults that the constructor
// overrides. The key is assetHash + local frame + variables +
// output size + frontend Chrome version + entry file hash. With
// timeDependent=false the request is normalised to localFrame=0, time=0 before
// both the lookup and the render, so every timeline frame shares one entry.
//
// requestLowRes(), lowResWait(), frame(), finalFrame(), prefetch() and the
// cancellation helpers are meant to be called from the thread that owns the
// service (usually the UI thread). The memory tier is guarded by a mutex; the
// SQLite-backed DiskCache lives on its own thread because QSqlDatabase
// connections are bound to their creating thread. Prefetch failures are
// warnings; they never terminate the service.
class HtmlFrameService : public QObject
{
    Q_OBJECT
    friend class FrameCacheWorker;
    friend class FrontendWorker;

public:
    explicit HtmlFrameService(QString cacheRoot,
                              qint64 memoryBytes = 256LL * 1024 * 1024,
                              qint64 diskBytes = 2LL * 1024 * 1024 * 1024,
                              QObject* parent = nullptr);
    ~HtmlFrameService() override;

    HtmlFrameService(const HtmlFrameService&) = delete;
    HtmlFrameService& operator=(const HtmlFrameService&) = delete;

    // Explicit Chromium-based browser path for all three renderers. Empty
    // means auto-detection. Starting with a path that cannot be executed makes
    // start() (or the first finalFrame()) fail. Calling this while running is
    // recorded but needs stop()/start() to take effect.
    void setChromeExecutable(const QString& path);
    QString chromeExecutable() const { return m_chromeExecutable; }

    // Starts the frontend and cache browsers. Returns false when either fails;
    // in that case nothing is left running. The final browser is not started.
    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const;

    // Low resolution, synchronous compatibility entry point. Internally it is
    // requestLowRes() plus an unbounded wait, so the caller blocks but the
    // frontend renderer still runs on its own thread.
    QImage frame(const FrameRequest& request, QString* error = nullptr);
    // High resolution: always uses the final browser. Nothing is cached. The
    // final browser is created lazily on this thread; see the class comment for
    // the single-thread contract. An out-of-range size is rejected before any
    // browser is started.
    QImage finalFrame(const FrameRequest& request, QString* error = nullptr);

    // Releases the final renderer and its browser, so the next finalFrame() can
    // create a fresh one (possibly on another thread). Must be called from the
    // thread that first called finalFrame(); a call from any other thread warns
    // and does nothing. Safe to call when no final renderer exists.
    void stopFinal();
    // True while the final renderer exists and its browser is running.
    bool finalRunning() const;

    // Low resolution, asynchronous. A cache hit returns an already finished
    // future; a miss is queued for the frontend worker. Queuing a newer miss
    // cancels the previously queued (not yet running) miss.
    QFuture<QImage> requestLowRes(const FrameRequest& request);

    // Cache-only lookup; never renders, waits, or runs an event loop. Safe to
    // call from paintEvent(). Returns a null image on a miss.
    QImage cachedLowRes(const FrameRequest& request);

    // requestLowRes() plus at most maxWaitMs of waiting (negative waits
    // forever). A timeout returns a null image with error="timeout" while the
    // background request keeps running; it is not cancelled.
    QImage lowResWait(const FrameRequest& request, int maxWaitMs, QString* error = nullptr);

    // Cancels the queued frontend request. The frame currently rendering is
    // allowed to finish; a cancelled request emits no frameReady/frameFailed.
    void cancelPendingLowRes();

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
    // Emitted when a low-resolution request produced an image (fresh render or
    // a cache fill that happened after the caller's initial miss).
    void frameReady(const AssetHash& assetHash, int localFrame, const QImage& image);
    void frameFailed(const AssetHash& assetHash, int localFrame, const QString& error);

private:
    enum class CacheTier
    {
        Miss,
        Memory,
        Disk
    };

    struct LowResOutcome;
    struct LowResSubmission;

    bool ensureStarted(QString* error);
    FrameRequest normalizedRequest(const FrameRequest& request) const;
    htmlcache::FrameKey keyFor(const FrameRequest& request) const;
    QString entryHashFor(const QString& bundleDir, const QString& entry) const;

    QFuture<QImage> submitLowRes(const FrameRequest& request,
                                 QSharedPointer<LowResOutcome>* outcome);
    void finishLowRes(const LowResSubmission& submission, const QImage& image,
                      const QString& error, bool notify);
    void queueFrameReady(const AssetHash& assetHash, int localFrame, const QImage& image);
    void queueFrameFailed(const AssetHash& assetHash, int localFrame, const QString& error);

    CacheTier lookupCached(const htmlcache::FrameKey& key, QImage* image);
    void storeCached(const htmlcache::FrameKey& key, const QImage& image);

    void noteHit(CacheTier tier);
    void noteRender();
    void notePrefetched();
    void queuePrefetchedSignal(const AssetHash& assetHash, int localFrame);
    void queuePrefetchError(const QString& message);

    QString m_cacheRoot;
    qint64 m_memoryBytes = 0;
    qint64 m_diskBytes = 0;
    QString m_chromeExecutable;
    QString m_chromeVersion;
    bool m_running = false;

    // Worker threads, one owner each.  Declaration order is destruction
    // order (reverse): the cache/disk proxies below are declared after the
    // disk worker, so they die first and never touch a destroyed worker.  All
    // three hold a raw back-pointer to this service and are started by start()
    // and joined by stop(); the destructor calls stop() before any member dies,
    // so a worker can never signal a half-destroyed service.
    std::unique_ptr<FrontendWorker> m_frontendWorker;
    PvhtmlRenderer* m_final = nullptr;
    Qt::HANDLE m_finalThreadId = nullptr; // thread that created m_final
    std::unique_ptr<FrameCacheWorker> m_cacheWorker;

    QMutex m_cacheMutex;
    std::unique_ptr<htmlcache::MemoryCache> m_memory;
    std::unique_ptr<FrameDiskWorker> m_diskWorker;
    std::unique_ptr<LockedFrameCache> m_memoryProxy;
    std::unique_ptr<DiskCacheProxy> m_diskProxy;
    std::unique_ptr<htmlcache::TieredCache> m_tiered;

    std::atomic<int> m_memoryHits{0};
    std::atomic<int> m_diskHits{0};
    std::atomic<int> m_renders{0};
    std::atomic<int> m_prefetched{0};
};

} // namespace pvf::html2
