#include "html2/HtmlFrameService.h"

#include "html2/PvhtmlRenderer.h"
#include "htmlcache/DiskCache.h"
#include "htmlcache/MemoryCache.h"
#include "htmlcache/TieredCache.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <optional>
#include <utility>

namespace pvf::html2 {

namespace {

constexpr int kDiskStartTimeoutMs = 60000;

} // namespace

struct HtmlFrameService::LowResOutcome
{
    QMutex mutex;
    QImage image;
    QString error;
};

struct HtmlFrameService::LowResSubmission
{
    FrameRequest request; // original request, used for frameReady identity
    htmlcache::FrameKey key;
    QSharedPointer<QPromise<QImage>> promise;
    QSharedPointer<LowResOutcome> outcome;
};

// Frontend renderer thread. It owns one PvhtmlRenderer for its whole lifetime
// and processes only the newest queued request; a newer request cancels the
// queued one while the frame currently rendering is allowed to finish.
class FrontendWorker final : public QThread
{
public:
    FrontendWorker(HtmlFrameService* service, QString chromeExecutable)
        : m_service(service)
        , m_chromeExecutable(std::move(chromeExecutable))
    {
    }

    ~FrontendWorker() override
    {
        requestStop();
        wait();
    }

    bool waitForStart(QString* error)
    {
        QMutexLocker locker(&m_mutex);
        while (!m_startDone)
            m_startCond.wait(&m_mutex);
        if (error)
            *error = m_started ? QString() : m_startError;
        return m_started;
    }

    QString chromeVersion() const
    {
        QMutexLocker locker(&m_mutex);
        return m_chromeVersion;
    }

    void enqueue(HtmlFrameService::LowResSubmission submission)
    {
        std::optional<HtmlFrameService::LowResSubmission> preempted;
        {
            QMutexLocker locker(&m_mutex);
            preempted = std::move(m_pending);
            m_pending = std::move(submission);
            m_cond.wakeAll();
        }
        if (preempted)
        {
            m_service->finishLowRes(*preempted, QImage{},
                                    QStringLiteral("superseded by a newer request"), false);
        }
    }

    void cancelPending()
    {
        std::optional<HtmlFrameService::LowResSubmission> cancelled;
        {
            QMutexLocker locker(&m_mutex);
            cancelled = std::move(m_pending);
            m_pending.reset();
        }
        if (cancelled)
            m_service->finishLowRes(*cancelled, QImage{}, QStringLiteral("cancelled"), false);
    }

    void requestStop()
    {
        std::optional<HtmlFrameService::LowResSubmission> cancelled;
        {
            QMutexLocker locker(&m_mutex);
            m_stop = true;
            cancelled = std::move(m_pending);
            m_pending.reset();
            m_cond.wakeAll();
        }
        if (cancelled)
            m_service->finishLowRes(*cancelled, QImage{}, QStringLiteral("cancelled"), false);
    }

protected:
    void run() override
    {
        PvhtmlRenderer renderer;
        renderer.setChromeExecutable(m_chromeExecutable);
        QString error;
        const bool started = renderer.start(&error);
        {
            QMutexLocker locker(&m_mutex);
            m_startDone = true;
            m_started = started;
            m_startError = error;
            m_chromeVersion = renderer.chromeVersion().trimmed();
            m_startCond.wakeAll();
        }
        if (!started)
            return;

        for (;;)
        {
            std::optional<HtmlFrameService::LowResSubmission> pending;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stop && !m_pending)
                    m_cond.wait(&m_mutex);
                if (m_stop)
                    break;
                pending = std::move(m_pending);
                m_pending.reset();
            }

            // A prefetch or another request may have filled the cache between
            // the caller's lookup and this worker picking the job up.
            QImage cached;
            const HtmlFrameService::CacheTier tier =
                m_service->lookupCached(pending->key, &cached);
            if (tier != HtmlFrameService::CacheTier::Miss)
            {
                m_service->noteHit(tier);
                m_service->finishLowRes(*pending, cached, QString(), true);
                continue;
            }

            QString renderError;
            const QImage image = renderer.renderFrame(
                m_service->normalizedRequest(pending->request), &renderError);
            if (image.isNull())
            {
                m_service->finishLowRes(*pending, QImage{}, renderError, true);
                continue;
            }

            m_service->storeCached(pending->key, image);
            m_service->noteRender();
            m_service->finishLowRes(*pending, image, QString(), true);
        }

        renderer.stop();
    }

private:
    HtmlFrameService* m_service = nullptr;
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    QWaitCondition m_startCond;
    bool m_startDone = false;
    bool m_started = false;
    bool m_stop = false;
    QString m_startError;
    QString m_chromeVersion;
    QString m_chromeExecutable;
    std::optional<HtmlFrameService::LowResSubmission> m_pending;
};

// Serial prefetch worker. The renderer is created inside run(), so the whole
// Chrome process and its QObject children belong to this thread and the
// frontend can keep the UI responsive in parallel.
class FrameCacheWorker final : public QThread
{
public:
    FrameCacheWorker(HtmlFrameService* service, QString chromeExecutable)
        : m_service(service)
        , m_chromeExecutable(std::move(chromeExecutable))
    {
    }

    ~FrameCacheWorker() override
    {
        requestStop();
        wait();
    }

    bool waitForStart(QString* error)
    {
        QMutexLocker locker(&m_mutex);
        while (!m_startDone)
            m_startCond.wait(&m_mutex);
        if (error)
            *error = m_started ? QString() : m_startError;
        return m_started;
    }

    // Replaces the pending batch. `m_generation` lets the running batch notice
    // that newer work arrived and stop after the frame currently rendering.
    void enqueue(QVector<FrameRequest> requests)
    {
        QMutexLocker locker(&m_mutex);
        m_batch = std::move(requests);
        ++m_generation;
        m_cond.wakeAll();
    }

    void requestStop()
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_batch.clear();
        ++m_generation;
        m_cond.wakeAll();
    }

    void removePendingAsset(const AssetHash& assetHash)
    {
        QMutexLocker locker(&m_mutex);
        for (int i = m_batch.size() - 1; i >= 0; --i)
        {
            if (m_batch.at(i).assetHash == assetHash)
                m_batch.removeAt(i);
        }
    }

protected:
    void run() override
    {
        PvhtmlRenderer renderer;
        renderer.setChromeExecutable(m_chromeExecutable);
        QString error;
        const bool started = renderer.start(&error);
        {
            QMutexLocker locker(&m_mutex);
            m_startDone = true;
            m_started = started;
            m_startError = error;
            m_startCond.wakeAll();
        }
        if (!started)
            return;

        for (;;)
        {
            QVector<FrameRequest> batch;
            quint64 generation = 0;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stop && m_batch.isEmpty())
                    m_cond.wait(&m_mutex);
                if (m_stop)
                    break;
                batch = std::move(m_batch);
                m_batch.clear();
                generation = m_generation;
            }

            for (const FrameRequest& request : batch)
            {
                if (stopRequested() || currentGeneration() != generation)
                    break;
                processOne(renderer, request);
            }
        }

        renderer.stop();
    }

private:
    bool stopRequested()
    {
        QMutexLocker locker(&m_mutex);
        return m_stop;
    }

    quint64 currentGeneration()
    {
        QMutexLocker locker(&m_mutex);
        return m_generation;
    }

    void processOne(PvhtmlRenderer& renderer, const FrameRequest& request)
    {
        const FrameRequest normalized = m_service->normalizedRequest(request);
        const htmlcache::FrameKey key = m_service->keyFor(normalized);

        QImage cached;
        const HtmlFrameService::CacheTier tier = m_service->lookupCached(key, &cached);
        if (tier != HtmlFrameService::CacheTier::Miss)
        {
            // Another render already filled this key; report availability
            // without rendering again.
            m_service->noteHit(tier);
            m_service->queuePrefetchedSignal(request.assetHash, request.localFrame);
            return;
        }

        QString error;
        const QImage image = renderer.renderFrame(normalized, &error);
        if (image.isNull())
        {
            m_service->queuePrefetchError(
                QStringLiteral("prefetch of %1 frame %2 failed: %3")
                    .arg(request.assetHash)
                    .arg(request.localFrame)
                    .arg(error));
            return;
        }

        m_service->storeCached(key, image);
        m_service->noteRender();
        m_service->notePrefetched();
        m_service->queuePrefetchedSignal(request.assetHash, request.localFrame);
    }

    HtmlFrameService* m_service = nullptr;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QWaitCondition m_startCond;
    bool m_startDone = false;
    bool m_started = false;
    bool m_stop = false;
    QString m_startError;
    QString m_chromeExecutable;
    QVector<FrameRequest> m_batch;
    quint64 m_generation = 0;
};

// Owns the DiskCache on a private thread. QSqlDatabase connections are bound
// to their creating thread, so the SQLite-backed DiskCache must never be
// touched from the UI thread or from the prefetch thread. Callers submit one
// job at a time and block until it finishes; disk I/O is serial either way.
class FrameDiskWorker final : public QThread
{
public:
    FrameDiskWorker(QString cacheRoot, qint64 diskBytes)
        : m_root(std::move(cacheRoot))
        , m_diskBytes(diskBytes)
    {
    }

    ~FrameDiskWorker() override
    {
        requestStop();
        wait();
    }

    bool waitForStart(QString* error)
    {
        QMutexLocker locker(&m_mutex);
        while (!m_startDone)
        {
            if (!m_startCond.wait(&m_mutex, kDiskStartTimeoutMs))
            {
                if (error)
                    *error = QStringLiteral("timed out opening the disk cache");
                return false;
            }
        }
        if (error)
            *error = m_ready ? QString() : m_lastError;
        return m_ready;
    }

    std::optional<QImage> get(const htmlcache::FrameKey& key)
    {
        QMutexLocker callLocker(&m_callMutex);
        QMutexLocker locker(&m_mutex);
        m_job = Job{};
        m_job.op = Op::Get;
        m_job.key = key;
        m_jobActive = true;
        m_jobDone = false;
        m_cond.wakeAll();
        while (!m_jobDone)
            m_doneCond.wait(&m_mutex);
        std::optional<QImage> result = std::move(m_job.result);
        m_jobActive = false;
        return result;
    }

    void put(const htmlcache::FrameKey& key, const QImage& image)
    {
        QMutexLocker callLocker(&m_callMutex);
        QMutexLocker locker(&m_mutex);
        m_job = Job{};
        m_job.op = Op::Put;
        m_job.key = key;
        m_job.image = image;
        m_jobActive = true;
        m_jobDone = false;
        m_cond.wakeAll();
        while (!m_jobDone)
            m_doneCond.wait(&m_mutex);
        m_jobActive = false;
    }

    void clearAsset(const AssetHash& assetHash)
    {
        QMutexLocker callLocker(&m_callMutex);
        QMutexLocker locker(&m_mutex);
        m_job = Job{};
        m_job.op = Op::Clear;
        m_job.assetHash = assetHash;
        m_jobActive = true;
        m_jobDone = false;
        m_cond.wakeAll();
        while (!m_jobDone)
            m_doneCond.wait(&m_mutex);
        m_jobActive = false;
    }

    void requestStop()
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_cond.wakeAll();
    }

protected:
    void run() override
    {
        htmlcache::DiskCache cache(m_root, m_diskBytes);
        {
            QMutexLocker locker(&m_mutex);
            m_startDone = true;
            m_ready = cache.isReady();
            m_lastError = cache.lastError();
            m_startCond.wakeAll();
        }

        for (;;)
        {
            Job job;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stop && !m_jobActive)
                    m_cond.wait(&m_mutex);
                if (m_stop && !m_jobActive)
                    break;
                job = std::move(m_job);
                m_job = Job{};
            }

            switch (job.op)
            {
            case Op::Get:
                job.result = cache.get(job.key);
                break;
            case Op::Put:
                cache.put(job.key, job.image);
                break;
            case Op::Clear:
                cache.clearAsset(job.assetHash);
                break;
            case Op::None:
                break;
            }

            {
                QMutexLocker locker(&m_mutex);
                m_job = std::move(job);
                m_jobActive = false;
                m_jobDone = true;
                m_doneCond.wakeAll();
            }

            if (m_stop)
                break;
        }
    }

private:
    enum class Op
    {
        None,
        Get,
        Put,
        Clear
    };

    struct Job
    {
        Op op = Op::None;
        htmlcache::FrameKey key;
        QImage image;
        AssetHash assetHash;
        std::optional<QImage> result;
    };

    QString m_root;
    qint64 m_diskBytes = 0;
    QMutex m_mutex;
    QMutex m_callMutex;
    QWaitCondition m_cond;
    QWaitCondition m_startCond;
    QWaitCondition m_doneCond;
    bool m_startDone = false;
    bool m_ready = false;
    bool m_stop = false;
    bool m_jobActive = false;
    bool m_jobDone = false;
    QString m_lastError;
    Job m_job;
};

// FrameCache adapter used by TieredCache so the disk tier is always entered on
// the FrameDiskWorker thread.
class DiskCacheProxy final : public htmlcache::FrameCache
{
public:
    explicit DiskCacheProxy(FrameDiskWorker* worker)
        : m_worker(worker)
    {
    }

    std::optional<QImage> get(const htmlcache::FrameKey& key) override
    {
        return m_worker ? m_worker->get(key) : std::nullopt;
    }

    void put(const htmlcache::FrameKey& key, const QImage& image) override
    {
        if (m_worker)
            m_worker->put(key, image);
    }

    void clearAsset(const AssetHash& assetHash) override
    {
        if (m_worker)
            m_worker->clearAsset(assetHash);
    }

private:
    FrameDiskWorker* m_worker = nullptr;
};

// The in-memory tier is shared by the UI and prefetch threads, so every access
// goes through this lock.
class LockedFrameCache final : public htmlcache::FrameCache
{
public:
    LockedFrameCache(htmlcache::FrameCache* inner, QMutex* mutex)
        : m_inner(inner)
        , m_mutex(mutex)
    {
    }

    std::optional<QImage> get(const htmlcache::FrameKey& key) override
    {
        QMutexLocker locker(m_mutex);
        return m_inner ? m_inner->get(key) : std::nullopt;
    }

    void put(const htmlcache::FrameKey& key, const QImage& image) override
    {
        QMutexLocker locker(m_mutex);
        if (m_inner)
            m_inner->put(key, image);
    }

    void clearAsset(const AssetHash& assetHash) override
    {
        QMutexLocker locker(m_mutex);
        if (m_inner)
            m_inner->clearAsset(assetHash);
    }

private:
    htmlcache::FrameCache* m_inner = nullptr;
    QMutex* m_mutex = nullptr;
};

HtmlFrameService::HtmlFrameService(QString cacheRoot, qint64 memoryBytes, qint64 diskBytes,
                                   QObject* parent)
    : QObject(parent)
    , m_cacheRoot(std::move(cacheRoot))
    , m_memoryBytes(memoryBytes)
    , m_diskBytes(diskBytes)
{
}

HtmlFrameService::~HtmlFrameService()
{
    stop();
}

bool HtmlFrameService::start(QString* error)
{
    if (isRunning())
    {
        if (error)
            error->clear();
        return true;
    }

    // A previous attempt may have left a partially started service (for
    // example the frontend crashed while the cache thread survived).
    if (m_frontendWorker || m_cacheWorker || m_diskWorker)
        stop();

    if (!m_memory)
    {
        m_memory = std::make_unique<htmlcache::MemoryCache>(m_memoryBytes);

        m_diskWorker = new FrameDiskWorker(m_cacheRoot, m_diskBytes);
        m_diskWorker->start();
        QString diskError;
        if (!m_diskWorker->waitForStart(&diskError))
        {
            qWarning().noquote() << QStringLiteral("html2: disk frame cache disabled: %1")
                                        .arg(diskError);
        }

        m_memoryProxy = std::make_unique<LockedFrameCache>(m_memory.get(), &m_cacheMutex);
        m_diskProxy = std::make_unique<DiskCacheProxy>(m_diskWorker);
        m_tiered = std::make_unique<htmlcache::TieredCache>(m_memoryProxy.get(),
                                                            m_diskProxy.get());
    }

    m_frontendWorker = new FrontendWorker(this, m_chromeExecutable);
    m_frontendWorker->start();

    QString frontendError;
    if (!m_frontendWorker->waitForStart(&frontendError))
    {
        stop();
        if (error)
        {
            *error = frontendError.isEmpty()
                         ? QStringLiteral("the HTML frontend browser failed to start")
                         : frontendError;
        }
        return false;
    }
    // The frontend browser is the version authority for every cache key.
    m_chromeVersion = m_frontendWorker->chromeVersion().trimmed();

    m_cacheWorker = new FrameCacheWorker(this, m_chromeExecutable);
    m_cacheWorker->start();

    QString cacheError;
    if (!m_cacheWorker->waitForStart(&cacheError))
    {
        m_cacheWorker->wait();
        delete m_cacheWorker;
        m_cacheWorker = nullptr;
        stop();
        if (error)
        {
            *error = cacheError.isEmpty()
                         ? QStringLiteral("the HTML prefetch browser failed to start")
                         : cacheError;
        }
        return false;
    }

    m_running = true;
    if (error)
        error->clear();
    return true;
}

void HtmlFrameService::stop()
{
    if (m_frontendWorker)
    {
        m_frontendWorker->requestStop();
        m_frontendWorker->wait();
        delete m_frontendWorker;
        m_frontendWorker = nullptr;
    }
    if (m_cacheWorker)
    {
        m_cacheWorker->requestStop();
        m_cacheWorker->wait();
        delete m_cacheWorker;
        m_cacheWorker = nullptr;
    }
    if (m_final)
    {
        m_final->stop();
        delete m_final;
        m_final = nullptr;
    }
    m_finalThreadId = nullptr;

    m_tiered.reset();
    m_diskProxy.reset();
    m_memoryProxy.reset();
    if (m_diskWorker)
    {
        m_diskWorker->requestStop();
        m_diskWorker->wait();
        delete m_diskWorker;
        m_diskWorker = nullptr;
    }
    m_memory.reset();

    m_running = false;
    m_chromeVersion.clear();
}

bool HtmlFrameService::isRunning() const
{
    return m_running && m_frontendWorker && m_frontendWorker->isRunning() && m_cacheWorker
           && m_cacheWorker->isRunning();
}

bool HtmlFrameService::ensureStarted(QString* error)
{
    if (isRunning())
    {
        if (error)
            error->clear();
        return true;
    }
    return start(error);
}

QFuture<QImage> HtmlFrameService::requestLowRes(const FrameRequest& request)
{
    return submitLowRes(request, nullptr);
}

QImage HtmlFrameService::lowResWait(const FrameRequest& request, int maxWaitMs, QString* error)
{
    QSharedPointer<LowResOutcome> outcome;
    QFuture<QImage> future = submitLowRes(request, &outcome);

    if (!future.isFinished() && maxWaitMs != 0)
    {
        if (maxWaitMs < 0)
        {
            future.waitForFinished();
        }
        else
        {
            QEventLoop loop;
            QFutureWatcher<QImage> watcher;
            QObject::connect(&watcher, &QFutureWatcher<QImage>::finished, &loop,
                             &QEventLoop::quit);
            watcher.setFuture(future);

            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(maxWaitMs);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }
    }

    if (!future.isFinished())
    {
        if (error)
            *error = QStringLiteral("timeout");
        return QImage{};
    }

    QString message;
    QImage image;
    {
        QMutexLocker locker(&outcome->mutex);
        message = outcome->error;
        image = outcome->image;
    }

    if (error)
    {
        if (!image.isNull())
            error->clear();
        else
            *error = message.isEmpty() ? QStringLiteral("HTML frame failed") : message;
    }
    return image;
}

void HtmlFrameService::cancelPendingLowRes()
{
    if (m_frontendWorker)
        m_frontendWorker->cancelPending();
}

QImage HtmlFrameService::frame(const FrameRequest& request, QString* error)
{
    QSharedPointer<LowResOutcome> outcome;
    QFuture<QImage> future = submitLowRes(request, &outcome);
    future.waitForFinished();

    QString message;
    QImage image;
    {
        QMutexLocker locker(&outcome->mutex);
        message = outcome->error;
        image = outcome->image;
    }

    if (error)
    {
        if (!image.isNull())
            error->clear();
        else
            *error = message.isEmpty() ? QStringLiteral("HTML frame failed") : message;
    }
    return image;
}

QFuture<QImage> HtmlFrameService::submitLowRes(const FrameRequest& request,
                                               QSharedPointer<LowResOutcome>* outcome)
{
    auto state = QSharedPointer<LowResOutcome>::create();
    auto promise = QSharedPointer<QPromise<QImage>>::create();
    promise->start();
    QFuture<QImage> future = promise->future();
    if (outcome)
        *outcome = state;

    QString startError;
    if (!ensureStarted(&startError))
    {
        const QString message = startError.isEmpty()
                                    ? QStringLiteral("the HTML frame service is not running")
                                    : startError;
        {
            QMutexLocker locker(&state->mutex);
            state->error = message;
        }
        promise->finish();
        queueFrameFailed(request.assetHash, request.localFrame, message);
        return future;
    }

    const FrameRequest normalized = normalizedRequest(request);
    const htmlcache::FrameKey key = keyFor(normalized);

    QImage cached;
    const CacheTier tier = lookupCached(key, &cached);
    if (tier != CacheTier::Miss)
    {
        // Cache hit: no browser work and an already finished future.
        noteHit(tier);
        {
            QMutexLocker locker(&state->mutex);
            state->image = cached;
        }
        promise->addResult(cached);
        promise->finish();
        return future;
    }

    LowResSubmission submission;
    submission.request = request;
    submission.key = key;
    submission.promise = promise;
    submission.outcome = state;
    m_frontendWorker->enqueue(std::move(submission));
    return future;
}

void HtmlFrameService::finishLowRes(const LowResSubmission& submission, const QImage& image,
                                    const QString& error, bool notify)
{
    {
        QMutexLocker locker(&submission.outcome->mutex);
        submission.outcome->image = image;
        submission.outcome->error = error;
    }

    if (!image.isNull())
        submission.promise->addResult(image);
    submission.promise->finish();

    if (!notify)
        return;
    if (image.isNull())
        queueFrameFailed(submission.request.assetHash, submission.request.localFrame, error);
    else
        queueFrameReady(submission.request.assetHash, submission.request.localFrame, image);
}

void HtmlFrameService::queueFrameReady(const AssetHash& assetHash, int localFrame,
                                       const QImage& image)
{
    QMetaObject::invokeMethod(
        this, [this, assetHash, localFrame, image]() {
            emit frameReady(assetHash, localFrame, image);
        },
        Qt::QueuedConnection);
}

void HtmlFrameService::queueFrameFailed(const AssetHash& assetHash, int localFrame,
                                        const QString& error)
{
    QMetaObject::invokeMethod(
        this, [this, assetHash, localFrame, error]() {
            emit frameFailed(assetHash, localFrame, error);
        },
        Qt::QueuedConnection);
}

QImage HtmlFrameService::finalFrame(const FrameRequest& request, QString* error)
{
    // Reject an out-of-range size before any browser (frontend or final)
    // starts; the same rule as PvhtmlRenderer::renderFrame().
    const QString sizeError = validateFrameSize(request.size);
    if (!sizeError.isEmpty())
    {
        if (error)
            *error = sizeError;
        return QImage{};
    }

    if (!ensureStarted(error))
        return QImage{};

    if (!m_final)
    {
        m_final = new PvhtmlRenderer(this);
        m_finalThreadId = QThread::currentThreadId();
    }
    else if (m_finalThreadId && m_finalThreadId != QThread::currentThreadId())
    {
        const QString message =
            QStringLiteral("finalFrame() must be called from the thread that created the "
                           "final renderer; call stopFinal() on that thread first");
        qWarning().noquote() << QStringLiteral("html2:") << message;
        if (error)
            *error = message;
        return QImage{};
    }
    m_final->setChromeExecutable(m_chromeExecutable);
    if (!m_final->isRunning())
    {
        QString startError;
        if (!m_final->start(&startError))
        {
            if (error)
                *error = startError;
            return QImage{};
        }
    }

    FrameRequest finalRequest = request;
    finalRequest.final = true;

    QString renderError;
    const QImage image = m_final->renderFrame(finalRequest, &renderError);
    if (image.isNull())
    {
        if (error)
            *error = renderError;
        return QImage{};
    }

    // Final frames are never cached, but they are real renders.
    noteRender();
    if (error)
        error->clear();
    return image;
}

void HtmlFrameService::stopFinal()
{
    if (!m_final)
        return;
    if (m_finalThreadId && m_finalThreadId != QThread::currentThreadId())
    {
        qWarning().noquote()
            << QStringLiteral("html2: stopFinal() must be called from the thread that "
                              "created the final renderer; ignoring this call");
        return;
    }

    m_final->stop();
    delete m_final;
    m_final = nullptr;
    m_finalThreadId = nullptr;
}

bool HtmlFrameService::finalRunning() const
{
    return m_final && m_final->isRunning();
}

void HtmlFrameService::prefetch(const QVector<FrameRequest>& requests)
{
    if (!ensureStarted(nullptr))
    {
        queuePrefetchError(QStringLiteral("cannot prefetch HTML frames: the service is not running"));
        return;
    }
    if (m_cacheWorker)
        m_cacheWorker->enqueue(requests);
}

void HtmlFrameService::clearAsset(const AssetHash& assetHash)
{
    if (m_cacheWorker)
        m_cacheWorker->removePendingAsset(assetHash);

    if (m_tiered)
        m_tiered->clearAsset(assetHash);
}

void HtmlFrameService::setChromeExecutable(const QString& path)
{
    m_chromeExecutable = path;
    if (m_final)
        m_final->setChromeExecutable(path);
    if (m_running)
    {
        qWarning().noquote()
            << QStringLiteral("html2: Chrome executable change takes effect after stop()/start()");
    }
}

QString HtmlFrameService::chromeVersion() const
{
    return m_chromeVersion;
}

Stats HtmlFrameService::stats() const
{
    Stats result;
    result.memoryHits = m_memoryHits.load(std::memory_order_relaxed);
    result.diskHits = m_diskHits.load(std::memory_order_relaxed);
    result.renders = m_renders.load(std::memory_order_relaxed);
    result.prefetched = m_prefetched.load(std::memory_order_relaxed);
    return result;
}

FrameRequest HtmlFrameService::normalizedRequest(const FrameRequest& request) const
{
    FrameRequest normalized = request;
    if (!normalized.timeDependent)
    {
        normalized.localFrame = 0;
        normalized.time = 0.0;
    }
    normalized.final = false;
    return normalized;
}

htmlcache::FrameKey HtmlFrameService::keyFor(const FrameRequest& request) const
{
    htmlcache::FrameKey key;
    key.assetHash = request.assetHash;
    key.localFrame = request.localFrame;
    key.variables = request.vars;
    key.size = request.size;
    key.chromeVersion = m_chromeVersion;
    key.entryHash = entryHashFor(request.bundleDir, request.entry);
    return key;
}

QString HtmlFrameService::entryHashFor(const QString& bundleDir, const QString& entry) const
{
    if (bundleDir.isEmpty() || entry.isEmpty())
        return QString();

    QFile file(QDir(bundleDir).filePath(entry));
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

HtmlFrameService::CacheTier HtmlFrameService::lookupCached(const htmlcache::FrameKey& key,
                                                           QImage* image)
{
    if (!m_memoryProxy)
        return CacheTier::Miss;

    if (std::optional<QImage> hot = m_memoryProxy->get(key))
    {
        if (image)
            *image = *hot;
        return CacheTier::Memory;
    }

    if (m_diskProxy)
    {
        if (std::optional<QImage> cold = m_diskProxy->get(key))
        {
            // Same policy as TieredCache::get: backfill the memory tier.
            m_memoryProxy->put(key, *cold);
            if (image)
                *image = *cold;
            return CacheTier::Disk;
        }
    }
    return CacheTier::Miss;
}

void HtmlFrameService::storeCached(const htmlcache::FrameKey& key, const QImage& image)
{
    if (m_tiered)
        m_tiered->put(key, image);
}

void HtmlFrameService::noteHit(CacheTier tier)
{
    if (tier == CacheTier::Memory)
        m_memoryHits.fetch_add(1, std::memory_order_relaxed);
    else if (tier == CacheTier::Disk)
        m_diskHits.fetch_add(1, std::memory_order_relaxed);
}

void HtmlFrameService::noteRender()
{
    m_renders.fetch_add(1, std::memory_order_relaxed);
}

void HtmlFrameService::notePrefetched()
{
    m_prefetched.fetch_add(1, std::memory_order_relaxed);
}

void HtmlFrameService::queuePrefetchedSignal(const AssetHash& assetHash, int localFrame)
{
    QMetaObject::invokeMethod(
        this,
        [this, assetHash, localFrame]() { emit prefetchedFrame(assetHash, localFrame); },
        Qt::QueuedConnection);
}

void HtmlFrameService::queuePrefetchError(const QString& message)
{
    qWarning().noquote() << QStringLiteral("html2:") << message;
    QMetaObject::invokeMethod(
        this, [this, message]() { emit prefetchError(message); }, Qt::QueuedConnection);
}

} // namespace pvf::html2
