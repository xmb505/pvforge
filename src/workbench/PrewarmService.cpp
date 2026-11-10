#include "workbench/PrewarmService.h"

#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <utility>

namespace pvf::workbench {
namespace {

// Exclusive end of the timeline: one past the last frame of every entity.
int contentEndFrame(const model::Project& project)
{
    int end = 0;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        end = std::max(end, it.value().end);
    }
    return end;
}

// Resolves -1 to "last content frame" and guarantees an empty range when the
// requested start is at or past the end.
int resolvedEndFrame(const PrewarmRequest& request)
{
    const int start = std::max(0, request.startFrame);
    const int end = request.endFrame >= 0 ? request.endFrame
                                          : contentEndFrame(*request.project) - 1;
    return std::max(start - 1, end);
}

} // namespace

class PrewarmService::Task : public QThread
{
public:
    Task(PrewarmService* service, PrewarmRequest request)
        : QThread(service)
        , m_service(service)
        , m_request(std::move(request))
    {
        setObjectName(QStringLiteral("pvf-workbench-prewarm"));
    }

    void requestCancel()
    {
        QMutexLocker lock(&m_controlMutex);
        m_cancelled = true;
        m_paused = false;
        m_wait.wakeAll();
    }

    void setPaused(bool paused)
    {
        QMutexLocker lock(&m_controlMutex);
        m_paused = paused;
        m_wait.wakeAll();
    }

    bool paused() const
    {
        QMutexLocker lock(&m_controlMutex);
        return m_paused;
    }

    bool wasCancelled() const { return m_outcomeCancelled.load(); }

    CacheStats stats() const
    {
        QMutexLocker lock(&m_statsMutex);
        return m_stats;
    }

protected:
    void run() override;

private:
    bool waitForResume(); // false when cancelled while paused
    bool cancelled();

    void publishStats(const Cache& cache)
    {
        QMutexLocker lock(&m_statsMutex);
        m_stats = cache.stats();
    }

    PrewarmService* m_service;
    PrewarmRequest m_request;

    mutable QMutex m_controlMutex;
    QWaitCondition m_wait;
    bool m_paused = false;
    bool m_cancelled = false;

    mutable QMutex m_statsMutex;
    CacheStats m_stats;
    std::atomic<bool> m_outcomeCancelled{false};
};

bool PrewarmService::Task::waitForResume()
{
    QMutexLocker lock(&m_controlMutex);
    while (m_paused && !m_cancelled)
        m_wait.wait(&m_controlMutex);
    return !m_cancelled;
}

bool PrewarmService::Task::cancelled()
{
    QMutexLocker lock(&m_controlMutex);
    return m_cancelled;
}

void PrewarmService::Task::run()
{
    QThread::currentThread()->setPriority(QThread::LowPriority);

    const model::Project& project = *m_request.project;
    const assets::AssetStore& assets = *m_request.assets;
    const MetaBuilder builder(project, assets);

    const int start = std::max(0, m_request.startFrame);
    const int end = resolvedEndFrame(m_request);
    const int total = end >= start ? end - start + 1 : 0;

    // Cache and its SQLite connection live and die on this thread.
    Cache cache(m_request.cacheRoot, m_request.cameraUuid, m_request.options);
    const bool cacheValid = cache.isValid();
    const QString cacheError = cache.error();

    emit m_service->started(total);

    int done = 0;
    for (int frame = start; frame <= end; ++frame)
    {
        if (!waitForResume())
            break;

        if (!cacheValid)
        {
            // Without an index there is nothing to skip and nowhere to store,
            // so report the reason once per requested frame.
            ++done;
            emit m_service->frameFailed(frame, cacheError);
        }
        else
        {
            const FrameMeta meta =
                builder.build(frame, m_request.cameraUuid, m_request.options);
            if (cancelled())
                break;

            if (cache.matches(frame, meta))
            {
                ++done;
                emit m_service->frameSkipped(frame);
            }
            else
            {
                const QImage image = m_request.renderFrame ? m_request.renderFrame(frame)
                                                           : QImage();
                // A cancel during the render must not write another frame.
                if (cancelled())
                    break;

                if (image.isNull())
                {
                    ++done;
                    emit m_service->frameFailed(
                        frame, QStringLiteral("renderer returned a null image"));
                }
                else
                {
                    QString error;
                    const bool stored = cache.store(frame, meta, image, &error);
                    ++done;
                    if (stored)
                    {
                        emit m_service->frameCached(frame);
                    }
                    else
                    {
                        emit m_service->frameFailed(
                            frame, error.isEmpty()
                                       ? QStringLiteral("cache store failed")
                                       : error);
                    }
                }
            }
        }

        emit m_service->progress(done, total, frame);
        publishStats(cache);

        const int throttle = std::max(0, m_request.throttleMs);
        if (throttle > 0 && frame < end)
            QThread::msleep(static_cast<unsigned long>(throttle));
    }

    m_outcomeCancelled.store(cancelled());
    publishStats(cache);
}

PrewarmService::PrewarmService(QObject* parent)
    : QObject(parent)
{
}

PrewarmService::~PrewarmService()
{
    stopTask(false);
}

void PrewarmService::start(PrewarmRequest request)
{
    stopTask(true);

    const quint64 generation = ++m_generation;
    if (!request.project || !request.assets || !request.renderFrame)
    {
        emit started(0);
        emit finished(false);
        return;
    }

    auto* task = new Task(this, std::move(request));
    m_task = task;
    // QThread::finished is emitted on the worker thread; the queued lambda
    // runs on the service thread, where m_task and the cache snapshot live.
    connect(task, &QThread::finished, this,
            [this, task, generation]() { onTaskFinished(task, generation); },
            Qt::QueuedConnection);
    task->start();
}

void PrewarmService::pause()
{
    if (m_task)
        m_task->setPaused(true);
}

void PrewarmService::resume()
{
    if (m_task)
        m_task->setPaused(false);
}

void PrewarmService::cancel()
{
    stopTask(true);
}

bool PrewarmService::isRunning() const
{
    return m_task && m_task->isRunning();
}

bool PrewarmService::isPaused() const
{
    return m_task && m_task->paused();
}

CacheStats PrewarmService::lastStats() const
{
    if (m_task)
        return m_task->stats();

    QMutexLocker lock(&m_statsMutex);
    return m_lastStats;
}

void PrewarmService::stopTask(bool report)
{
    Task* task = m_task;
    if (!task)
        return;

    m_task = nullptr;
    ++m_generation; // invalidate the queued onTaskFinished() of this task

    task->requestCancel();
    task->wait();

    {
        QMutexLocker lock(&m_statsMutex);
        m_lastStats = task->stats();
    }
    const bool cancelled = task->wasCancelled();
    delete task;

    if (report)
        emit finished(cancelled);
}

void PrewarmService::onTaskFinished(Task* task, quint64 generation)
{
    if (m_task != task || generation != m_generation)
        return;

    m_task = nullptr;
    task->wait();

    {
        QMutexLocker lock(&m_statsMutex);
        m_lastStats = task->stats();
    }
    const bool cancelled = task->wasCancelled();
    task->deleteLater();

    emit finished(cancelled);
}

} // namespace pvf::workbench
