#include "next/app/WorkbenchServices.h"

#include "next/app/ProjectDocument.h"
#include "perf/FrameProfiler.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

namespace pvf::next {
namespace {

// The explicit cache pass is deliberately slow enough for the HTML/video
// decoders to breathe between frames.
constexpr int kTickIntervalMs = 20;
// A held frame waits at most this long for the asynchronous HTML renderer.
constexpr int kHtmlWaitMs = 3000;
constexpr int kMaxCacheWidth = 1280;
// Playback misses beyond these bounds are dropped oldest-first.  One 1280x720
// frame is about 3.7 MB, so 64 frames also stay well under the byte cap.
constexpr int kMaxPendingStores = 64;
constexpr qint64 kMaxPendingStoreBytes = 256LL * 1024 * 1024;
constexpr int kStoreWorkerThreads = 1;

class StoreGuard
{
public:
    StoreGuard()
        : m_profiler(perf::FrameProfiler::instance())
    {
        m_profiler.beginStore();
    }
    ~StoreGuard() { m_profiler.endStore(); }

    StoreGuard(const StoreGuard&) = delete;
    StoreGuard& operator=(const StoreGuard&) = delete;

private:
    perf::FrameProfiler& m_profiler;
};

} // namespace

WorkbenchServices::WorkbenchServices(ProjectDocument* document,
                                     const AppSettings& settings,
                                     const Uuid& cameraUuid,
                                     Providers providers, QObject* parent)
    : QObject(parent)
    , m_document(document)
    , m_providers(std::move(providers))
    , m_cameraUuid(cameraUuid)
{
    m_enabled = settings.workbenchCacheEnabled && m_document != nullptr;
    if (!m_enabled)
        return;

    m_options.scalePercent = settings.workbenchCacheScalePercent;
    m_options.maxWidth = kMaxCacheWidth;
    // Only the editor canvas view expands; a camera frame is cropped to the
    // camera, so its cache stays exactly the canvas size.
    m_options.expandPercent = m_cameraUuid.isEmpty()
                                  ? std::clamp(
                                        settings.workbenchCacheExpandPercent, 0,
                                        200)
                                  : 0;
    m_options.transparentOutsideCanvas = m_options.expandPercent > 0;
    m_options.targetSize = workbench::cacheSizeFor(
        m_document->project(), m_options.scalePercent, kMaxCacheWidth,
        m_options.expandPercent);
    m_options.diskBudgetBytes = settings.workbenchCacheDiskBytes;
    m_options.memoryBudgetBytes = settings.workbenchCacheMemoryBytes;
    // Bump whenever the renderer's world mapping changes; the cache stores
    // full frames and must not reuse images from the old top-left/Y-down era.
    m_options.rendererVersion = QStringLiteral("yup-2");

    m_cacheRoot =
        QDir(m_document->root()).filePath(QStringLiteral(".cache/workbench"));

    // No Cache is constructed here: the directory and index appear on the
    // first store, so opening a project does not create cache files.  A
    // previous session's cache is opened lazily by the first lookup.
    m_tick.setInterval(kTickIntervalMs);
    connect(&m_tick, &QTimer::timeout, this, &WorkbenchServices::tick);

    m_htmlWaitTimeout.setSingleShot(true);
    m_htmlWaitTimeout.setInterval(kHtmlWaitMs);
    connect(&m_htmlWaitTimeout, &QTimer::timeout, this,
            &WorkbenchServices::onHtmlWaitTimeout);

    // One serial writer.  Encoding runs here, SQLite stays on the UI thread.
    m_storePool = new QThreadPool(this);
    m_storePool->setMaxThreadCount(kStoreWorkerThreads);
    m_storePool->setExpiryTimeout(30000);

    // Edits invalidate the memoised meta; they never start a scan.
    connect(m_document, &Document::changed, this,
            &WorkbenchServices::onDocumentChanged);

    m_progress.total = std::max(1, contentEnd());
}

WorkbenchServices::~WorkbenchServices()
{
    cancelScan();
    // In-flight encodes cannot be cancelled.  They finish before the pool is
    // destroyed, but their result callbacks are already forbidden from
    // writing (the queue was cleared) and the queued invocation is dropped
    // with this object.
    discardPendingStores();
    if (m_storePool)
        m_storePool->waitForDone();
}

bool WorkbenchServices::cacheValid() const
{
    if (!m_enabled)
        return false;
    // Not created yet is still a usable cache: the first store creates it.
    return !m_cache || m_cache->isValid();
}

QString WorkbenchServices::cacheError() const
{
    return m_cache ? m_cache->error() : QString();
}

QString WorkbenchServices::cacheDirectory() const
{
    return m_cache ? m_cache->directory() : QString();
}

QImage WorkbenchServices::lookup(int frame, const Uuid& cameraUuid, bool* hit)
{
    if (hit)
        *hit = false;
    if (!m_enabled || cameraUuid != m_cameraUuid)
        return {};

    perf::FrameProfiler& profiler = perf::FrameProfiler::instance();

    // Do not create the cache tree for a lookup.  If no previous session left
    // a cache behind this is a plain miss; store() creates it after rendering.
    if (!m_cache) {
        if (!QDir(m_cacheRoot).exists())
        {
            profiler.noteCacheLookup(false);
            return {};
        }
        ensureCache();
    }
    if (!m_cache || !m_cache->isValid())
    {
        profiler.noteCacheLookup(false);
        return {};
    }

    workbench::FrameMeta meta;
    {
        perf::Scope metaScope("元数据构建", perf::Category::Meta);
        meta = buildMeta(frame);
    }
    bool found = false;
    const QImage image = m_cache->lookup(frame, meta, &found);
    if (hit)
        *hit = found;
    profiler.noteCacheLookup(found);
    return image;
}

void WorkbenchServices::store(int frame, const Uuid& cameraUuid,
                              const QImage& image)
{
    if (!m_enabled || image.isNull() || cameraUuid != m_cameraUuid)
        return;

    // First miss creates the directory, index and the frame file (QOI for
    // pure vector frames, WebP when the meta flags image/video/HTML content).
    ensureCache();
    if (!m_cache || !m_cache->isValid())
        return;

    const workbench::FrameMeta meta = buildMeta(frame);

    // Playback raster misses are the expensive ones (37-47 ms WebP q80).
    // The paint frame only hands over the implicitly shared image and the
    // meta; a serial worker encodes it and the UI thread writes the file and
    // index row.  Vector QOI stays synchronous (~2 ms), and pausing returns to
    // the synchronous path so the next lookup is hot immediately.
    if (m_playbackActive && meta.hasRaster)
    {
        enqueueStore(frame, meta, image);
        return;
    }

    const StoreGuard storeGuard;
    m_cache->store(frame, meta, image);
}

void WorkbenchServices::enqueueStore(int frame,
                                     const workbench::FrameMeta& meta,
                                     const QImage& image)
{
    const qint64 bytes = image.sizeInBytes();

    // Same frame/meta already queued: the copy that will be written is
    // identical, so a second encode would only waste the writer.
    for (int i = 0; i < m_pendingStores.size(); ++i)
    {
        const PendingStore& pending = m_pendingStores.at(i);
        if (pending.frame != frame)
            continue;
        if (pending.meta.bytes == meta.bytes
            && pending.meta.sha1 == meta.sha1)
            return;
    }

    // A newer meta for the same frame supersedes the queued picture: writing
    // both would make the final file depend on writer order.
    for (int i = 0; i < m_pendingStores.size();)
    {
        if (m_pendingStores.at(i).frame != frame)
        {
            ++i;
            continue;
        }
        m_pendingStoreBytes -= m_pendingStores.at(i).bytes;
        m_pendingStores.removeAt(i);
        ++m_droppedStores;
        m_lastDroppedFrame = frame;
        perf::FrameProfiler::instance().endStore();
    }

    while (!m_pendingStores.isEmpty()
           && (m_pendingStores.size() >= kMaxPendingStores
               || m_pendingStoreBytes + bytes > kMaxPendingStoreBytes))
    {
        const PendingStore dropped = m_pendingStores.dequeue();
        m_pendingStoreBytes -= dropped.bytes;
        ++m_droppedStores;
        m_lastDroppedFrame = dropped.frame;
        perf::FrameProfiler::instance().endStore();
    }

    PendingStore task;
    task.id = ++m_nextStoreId;
    task.frame = frame;
    task.meta = meta;
    task.image = image;
    task.bytes = bytes;
    m_pendingStores.enqueue(task);
    m_pendingStoreBytes += bytes;
    perf::FrameProfiler::instance().beginStore();

    // Dispatch from the event loop, not from this call: a burst of misses (a
    // fast playback tick) fills the bounded queue first, so frames the queue
    // drops are never encoded.
    scheduleStoreDispatch();
}

void WorkbenchServices::scheduleStoreDispatch()
{
    if (m_storeDispatchPosted || m_storeTaskRunning)
        return;
    m_storeDispatchPosted = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_storeDispatchPosted = false;
            dispatchStoreTask();
        },
        Qt::QueuedConnection);
}

void WorkbenchServices::dispatchStoreTask()
{
    if (m_storeTaskRunning || m_pendingStores.isEmpty() || !m_storePool)
        return;
    m_storeTaskRunning = true;
    startStoreTask(m_pendingStores.head());
}

void WorkbenchServices::startStoreTask(const PendingStore& task)
{
    if (!m_storePool)
        return;

    WorkbenchServices* self = this;
    const quint64 id = task.id;
    const QImage image = task.image;
    const QSize targetSize = m_options.targetSize;
    const bool hasRaster = task.meta.hasRaster;

    m_storePool->start(QRunnable::create(
        [self, id, image, targetSize, hasRaster] {
            QElapsedTimer timer;
            timer.start();
            workbench::EncodedFrame encoded =
                workbench::Cache::encode(image, targetSize, hasRaster);
            const double encodeMs =
                static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
            QMetaObject::invokeMethod(
                self,
                [self, id, encoded = std::move(encoded), encodeMs]() mutable {
                    self->onStoreEncoded(id, encoded, encodeMs);
                },
                Qt::QueuedConnection);
        }));
}

void WorkbenchServices::onStoreEncoded(quint64 id,
                                       const workbench::EncodedFrame& encoded,
                                       double encodeMs)
{
    m_storeTaskRunning = false;
    ++m_workerEncodes;
    m_workerEncodeMs += encodeMs;

    int index = -1;
    for (int i = 0; i < m_pendingStores.size(); ++i)
    {
        if (m_pendingStores.at(i).id == id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        // Superseded, document edit, shutdown or clearCache(): the picture
        // belongs to the old state.  Keep draining whatever is still valid.
        dispatchStoreTask();
        return;
    }

    const PendingStore task = m_pendingStores.at(index);
    m_pendingStoreBytes -= task.bytes;
    m_pendingStores.removeAt(index);
    perf::FrameProfiler::instance().endStore();

    if (m_cache && m_cache->isValid() && !encoded.bytes.isEmpty()
        && m_cache->storeEncoded(task.frame, task.meta, encoded))
        ++m_asyncStoresWritten;

    dispatchStoreTask();
}

double WorkbenchServices::workerEncodeMeanMs() const
{
    return m_workerEncodes == 0 ? 0.0
                                : m_workerEncodeMs / double(m_workerEncodes);
}

bool WorkbenchServices::waitForPendingStores(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!m_pendingStores.isEmpty() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return m_pendingStores.isEmpty();
}

void WorkbenchServices::discardPendingStores()
{
    while (!m_pendingStores.isEmpty())
    {
        const PendingStore dropped = m_pendingStores.dequeue();
        m_pendingStoreBytes -= dropped.bytes;
        perf::FrameProfiler::instance().endStore();
    }
}

void WorkbenchServices::startScanNow(int startFrame, int endFrame)
{
    if (!m_enabled || !m_document)
        return;
    ensureCache();
    if (!m_cache || !m_cache->isValid()) {
        m_progress.total = std::max(1, contentEnd());
        return;
    }

    m_tick.stop();
    m_htmlWaitTimeout.stop();
    m_waitingHtml = false;
    m_htmlWaitDisabled = false;

    const int lastFrame = std::max(0, contentEnd() - 1);
    const int wantedEnd =
        endFrame < 0 ? lastFrame : std::min(endFrame, lastFrame);
    m_scanStart = std::clamp(startFrame, 0, lastFrame);
    m_scanEnd = std::max(m_scanStart, wantedEnd);
    m_scanFrame = m_scanStart;

    m_progress = WorkbenchPrewarmProgress{};
    m_progress.scanning = true;
    m_progress.paused = m_playbackActive;
    m_progress.total = m_scanEnd - m_scanStart + 1;

    // While playback runs the pass stays armed but does not tick; stopping
    // playback restarts it from the same frame.
    if (m_playbackActive) {
        m_tick.stop();
        return;
    }

    m_tick.start();
    // Process the first frame immediately so the UI shows a cached frame right
    // away and tests do not depend on a 20 ms timer tick.
    tick();
}

void WorkbenchServices::cancelScan()
{
    m_tick.stop();
    m_htmlWaitTimeout.stop();
    m_waitingHtml = false;
    m_progress.waitingForHtml = false;
    m_progress.scanning = false;
    m_progress.paused = false;
}

void WorkbenchServices::setPlaybackActive(bool active)
{
    if (m_playbackActive == active)
        return;
    m_playbackActive = active;
    m_progress.paused = active && m_progress.scanning;

    if (active) {
        m_tick.stop();
    } else if (m_progress.scanning && !m_waitingHtml) {
        m_tick.start();
    }
}

void WorkbenchServices::notifyHtmlFrameReady()
{
    // The renderer recovered; allow waiting on the next HTML miss again.
    m_htmlWaitDisabled = false;
    if (!m_waitingHtml)
        return;

    m_waitingHtml = false;
    m_progress.waitingForHtml = false;
    m_htmlWaitTimeout.stop();
    if (m_progress.scanning && !m_playbackActive)
        m_tick.start();
}

void WorkbenchServices::clearCache()
{
    cancelScan();
    // Never let a queued playback frame repopulate the cache after the clear.
    discardPendingStores();
    invalidateMeta();
    if (m_cache && m_cache->isValid())
        m_cache->clear();
}

void WorkbenchServices::setSolverResults(
    QHash<QString, solver::Result>* results)
{
    m_sharedSolverResults = results;
}

workbench::CacheStats WorkbenchServices::stats() const
{
    workbench::CacheStats stats;
    if (m_cache)
        stats = m_cache->stats();
    else
        stats.budgetBytes = m_options.diskBudgetBytes;
    stats.pendingStores = pendingStores();
    return stats;
}

int WorkbenchServices::totalFrames() const
{
    return std::max(1, contentEnd());
}

QImage WorkbenchServices::lookupForTest(int frame, const Uuid& cameraUuid)
{
    bool hit = false;
    const QImage image = lookup(frame, cameraUuid, &hit);
    return hit ? image : QImage();
}

workbench::FrameMeta WorkbenchServices::buildMetaForTest(int frame)
{
    return buildMeta(frame);
}

void WorkbenchServices::tick()
{
    if (!m_enabled || !m_document) {
        finishScan();
        return;
    }
    if (m_playbackActive || m_waitingHtml)
        return;
    if (!cacheValid()) {
        finishScan();
        return;
    }
    if (m_scanFrame > m_scanEnd) {
        finishScan();
        return;
    }

    const int frame = m_scanFrame;
    const workbench::FrameMeta meta = buildMeta(frame);
    if (m_cache->matches(frame, meta)) {
        ++m_progress.hits;
    } else {
        bool htmlMissing = false;
        bool videoMissing = false;
        const QImage image = renderForCache(frame, &htmlMissing, &videoMissing);
        if (image.isNull() || htmlMissing || videoMissing) {
            if (htmlMissing && !m_htmlWaitDisabled && htmlPending()) {
                // The HTML frontend is already rendering this exact frame;
                // hold the frame and retry when it lands instead of writing
                // render2's placeholder.
                enterHtmlWait();
                return;
            }
            ++m_progress.skipped;
        } else {
            if (m_cache->store(frame, meta, image))
                ++m_progress.cached;
            else
                ++m_progress.failed;
        }
    }

    ++m_progress.done;
    m_progress.frame = frame;
    ++m_scanFrame;
    if (m_scanFrame > m_scanEnd)
        finishScan();
}

void WorkbenchServices::onDocumentChanged()
{
    // Any committed edit can change a frame's meta.  Drop the queued raster
    // frames before their file/index write: they belong to the old state and
    // would otherwise turn into permanent cache entries.  An encode already
    // running finishes but its result is discarded (its ID is gone).
    discardPendingStores();
    // The next viewport lookup builds a fresh meta and misses; nothing is
    // rendered in the background.
    invalidateMeta();
}

void WorkbenchServices::onHtmlWaitTimeout()
{
    m_waitingHtml = false;
    m_progress.waitingForHtml = false;
    m_htmlWaitDisabled = true;

    if (m_scanFrame <= m_scanEnd) {
        // Give up on this frame for the current pass; a later scan retries.
        ++m_progress.skipped;
        ++m_progress.done;
        m_progress.frame = m_scanFrame;
        ++m_scanFrame;
    }

    if (m_scanFrame > m_scanEnd) {
        finishScan();
    } else if (!m_playbackActive) {
        m_tick.start();
    }
}

void WorkbenchServices::finishScan()
{
    m_tick.stop();
    m_htmlWaitTimeout.stop();
    m_waitingHtml = false;
    m_progress.waitingForHtml = false;
    if (m_progress.scanning)
        m_progress.frame = m_scanFrame > m_scanEnd ? m_scanEnd : m_scanFrame;
    m_progress.scanning = false;
    m_progress.paused = false;
}

void WorkbenchServices::enterHtmlWait()
{
    m_waitingHtml = true;
    m_progress.waitingForHtml = true;
    m_tick.stop();
    m_htmlWaitTimeout.start();
}

void WorkbenchServices::invalidateMeta()
{
    m_lastMetaValid = false;
    m_lastMetaFrame = -1;
    m_lastMeta = {};
}

workbench::FrameMeta WorkbenchServices::buildMeta(int frame)
{
    if (m_lastMetaValid && m_lastMetaFrame == frame)
        return m_lastMeta;

    // Poses serialised into the meta reuse the session's solved segments; an
    // image-cache hit still builds the meta but never re-runs physics.
    workbench::MetaBuilder builder(m_document->project(), m_document->assets(),
                                   m_sharedSolverResults);
    m_lastMeta = builder.build(frame, m_cameraUuid, m_options);
    m_lastMetaFrame = frame;
    m_lastMetaValid = true;
    return m_lastMeta;
}

QImage WorkbenchServices::renderForCache(int frame, bool* htmlMissing,
                                         bool* videoMissing)
{
    bool missingHtml = false;
    bool missingVideo = false;

    render2::Options options;
    options.projectFrame = frame;
    options.cameraUuid = m_cameraUuid;
    options.outputSize = m_options.targetSize;
    if (m_options.expandPercent > 0 && m_cameraUuid.isEmpty()) {
        options.worldRect = workbench::renderRect(
            m_document->project(), m_options.expandPercent);
        options.transparentOutsideCanvas =
            m_options.transparentOutsideCanvas;
    }
    options.videoFrame = [this, &missingVideo](const Uuid& uuid, double at) {
        perf::Scope videoScope("视频帧", perf::Category::Video);
        const QImage image = m_providers.videoFrame
                                 ? m_providers.videoFrame(uuid, at)
                                 : QImage();
        if (image.isNull())
            missingVideo = true;
        return image;
    };
    options.htmlFrame = [this, &missingHtml](const Uuid& uuid, double at) {
        perf::Scope htmlScope("HTML 帧", perf::Category::Html);
        const QImage image = m_providers.htmlFrame
                                 ? m_providers.htmlFrame(uuid, at)
                                 : QImage();
        if (image.isNull())
            missingHtml = true;
        return image;
    };

    const model::Project& project = m_document->project();
    anim::Cache cache;
    cache.sharedSolverResults = m_sharedSolverResults;
    render2::SceneWorld world(project, m_document->assets(), &cache);
    const QImage image = render2::renderFrame(project, m_document->assets(),
                                              options, world, cache);

    if (htmlMissing)
        *htmlMissing = missingHtml;
    if (videoMissing)
        *videoMissing = missingVideo;
    return image;
}

int WorkbenchServices::contentEnd() const
{
    if (!m_document)
        return 0;

    int end = 0;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        if (it.value().parentUuid.isEmpty())
            end = std::max(end, it.value().end);
    }
    return std::max(0, end);
}

bool WorkbenchServices::htmlPending() const
{
    return m_providers.htmlPending && m_providers.htmlPending();
}

void WorkbenchServices::ensureCache()
{
    if (m_cache || !m_enabled || m_cacheRoot.isEmpty())
        return;

    // No QObject parent: the unique_ptr owns it.  Constructing the Cache
    // creates the per-camera directory, so this is only called from store()
    // (first miss) or from an explicit manual scan.
    m_cache = std::make_unique<workbench::Cache>(m_cacheRoot, m_cameraUuid,
                                                 m_options);
}

} // namespace pvf::next
