#include "video2/VideoExtractionQueue.h"

#include "video2/VideoSequenceService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

#include <memory>
#include <utility>

namespace pvf::video2 {
namespace {

SequenceOptions sequenceOptions(const ExtractionJob& job)
{
    SequenceOptions options;
    options.cacheRoot = job.cacheRoot;
    options.assetHash = job.assetHash;
    options.videoPath = job.videoPath;
    options.projectFps = job.projectFps;
    options.maxWidth = job.maxWidth;
    options.diskBudgetBytes = job.diskBudget;
    return options;
}

QString indexPath(const ExtractionJob& job)
{
    return QDir(job.cacheRoot)
        .filePath(job.assetHash + QLatin1Char('/')
                  + QString::number(job.projectFps) + QLatin1Char('/')
                  + QString::number(job.maxWidth) + QLatin1Char('/')
                  + QStringLiteral("index.json"));
}

bool indexComplete(const ExtractionJob& job)
{
    if (job.cacheRoot.isEmpty() || job.assetHash.isEmpty()
        || job.projectFps <= 0 || job.maxWidth <= 0)
        return false;

    QFile file(indexPath(job));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;

    const QJsonObject root = document.object();
    const int total = root.value(QStringLiteral("totalFrames")).toInt();
    const int mapped = root.value(QStringLiteral("frames")).toObject().size();
    return total > 0 && mapped == total;
}

bool validJob(const ExtractionJob& job, QString* error)
{
    if (job.assetHash.isEmpty())
    {
        if (error)
            *error = QStringLiteral("素材哈希为空");
        return false;
    }
    if (job.cacheRoot.isEmpty())
    {
        if (error)
            *error = QStringLiteral("缓存目录为空");
        return false;
    }
    if (job.projectFps <= 0)
    {
        if (error)
            *error = QStringLiteral("项目帧率必须大于 0");
        return false;
    }
    if (job.maxWidth <= 0)
    {
        if (error)
            *error = QStringLiteral("最大宽度必须大于 0");
        return false;
    }
    if (job.videoPath.isEmpty())
    {
        if (error)
            *error = QStringLiteral("视频路径为空");
        return false;
    }
    return true;
}

} // namespace

class VideoExtractionQueue::Worker final : public QThread
{
public:
    explicit Worker(VideoExtractionQueue* owner)
        : m_owner(owner)
    {
    }

    ~Worker() override
    {
        requestStop();
        wait();
    }

    void enqueue(const ExtractionJob& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_runningHash == job.assetHash)
            return;
        for (const ExtractionJob& pending : std::as_const(m_pending))
        {
            if (pending.assetHash == job.assetHash)
                return;
        }
        m_pending.append(job);
        m_wake.wakeAll();
    }

    void cancel(const AssetHash& assetHash)
    {
        std::shared_ptr<VideoSequenceService> service;
        bool wasQueued = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_runningHash == assetHash)
            {
                m_cancelRequested = true;
                service = m_current;
            }
            else
            {
                for (int i = 0; i < m_pending.size(); ++i)
                {
                    if (m_pending.at(i).assetHash != assetHash)
                        continue;
                    m_pending.removeAt(i);
                    wasQueued = true;
                    break;
                }
            }
        }
        if (service)
            service->cancel();
        if (wasQueued)
            m_owner->cancelled(assetHash);
    }

    void cancelAll()
    {
        QStringList queued;
        std::shared_ptr<VideoSequenceService> service;
        {
            QMutexLocker locker(&m_mutex);
            for (const ExtractionJob& job : std::as_const(m_pending))
                queued.append(job.assetHash);
            m_pending.clear();
            if (!m_runningHash.isEmpty())
            {
                m_cancelRequested = true;
                service = m_current;
            }
        }
        if (service)
            service->cancel();
        for (const QString& hash : std::as_const(queued))
            m_owner->cancelled(hash);
    }

    void requestStop()
    {
        std::shared_ptr<VideoSequenceService> service;
        {
            QMutexLocker locker(&m_mutex);
            m_stop = true;
            m_cancelRequested = true;
            service = m_current;
            m_wake.wakeAll();
        }
        if (service)
            service->cancel();
    }

    bool isRunning(const AssetHash& assetHash) const
    {
        QMutexLocker locker(&m_mutex);
        return !assetHash.isEmpty() && m_runningHash == assetHash;
    }

    bool isQueued(const AssetHash& assetHash) const
    {
        QMutexLocker locker(&m_mutex);
        for (const ExtractionJob& job : m_pending)
        {
            if (job.assetHash == assetHash)
                return true;
        }
        return false;
    }

    int pendingCount() const
    {
        QMutexLocker locker(&m_mutex);
        return m_pending.size();
    }

    bool stopRequested()
    {
        QMutexLocker locker(&m_mutex);
        return m_cancelRequested || m_stop;
    }

protected:
    void run() override
    {
        for (;;)
        {
            ExtractionJob job;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stop && m_pending.isEmpty())
                    m_wake.wait(&m_mutex);
                if (m_stop)
                    break;
                job = m_pending.takeFirst();
                m_runningHash = job.assetHash;
                m_cancelRequested = false;
            }

            auto service = std::make_shared<VideoSequenceService>(
                sequenceOptions(job));
            {
                QMutexLocker locker(&m_mutex);
                m_current = service;
                if (m_cancelRequested)
                    service->cancel();
            }

            bool cancelledBeforeStart = false;
            {
                QMutexLocker locker(&m_mutex);
                cancelledBeforeStart = m_cancelRequested;
            }

            bool ok = false;
            bool started = false;
            if (!cancelledBeforeStart)
            {
                ok = service->extract(
                    [this, &job, &started](const SequenceProgress& progress) {
                        if (!started)
                        {
                            started = true;
                            m_owner->started(job.assetHash, progress.total);
                        }
                        m_owner->progress(job.assetHash, progress.done,
                                          progress.total);
                        return !stopRequested();
                    });
            }

            const QString error = service->lastError();
            bool wasCancelled = false;
            bool stopping = false;
            {
                QMutexLocker locker(&m_mutex);
                wasCancelled = m_cancelRequested;
                stopping = m_stop;
                m_current.reset();
                m_runningHash.clear();
                m_cancelRequested = false;
            }

            if (stopping)
                continue;

            if (ok)
                m_owner->finished(job.assetHash);
            else if (wasCancelled)
                m_owner->cancelled(job.assetHash);
            else
                m_owner->failed(job.assetHash,
                                error.isEmpty()
                                    ? QStringLiteral("视频抽帧失败")
                                    : error);
        }
    }

private:
    VideoExtractionQueue* m_owner = nullptr;

    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QVector<ExtractionJob> m_pending;
    std::shared_ptr<VideoSequenceService> m_current;
    AssetHash m_runningHash;
    bool m_cancelRequested = false;
    bool m_stop = false;
};

VideoExtractionQueue::VideoExtractionQueue(QObject* parent)
    : QObject(parent)
    , m_worker(new Worker(this))
{
    m_worker->start();
}

VideoExtractionQueue::~VideoExtractionQueue()
{
    if (!m_worker)
        return;
    m_worker->requestStop();
    m_worker->wait();
    delete m_worker;
    m_worker = nullptr;
}

void VideoExtractionQueue::enqueue(const Job& job)
{
    QString error;
    if (!validJob(job, &error))
    {
        failed(job.assetHash, error);
        return;
    }
    if (indexComplete(job))
        return;
    m_worker->enqueue(job);
}

void VideoExtractionQueue::cancel(const AssetHash& assetHash)
{
    m_worker->cancel(assetHash);
}

void VideoExtractionQueue::cancelAll()
{
    m_worker->cancelAll();
}

bool VideoExtractionQueue::isRunning(const AssetHash& assetHash) const
{
    return m_worker->isRunning(assetHash);
}

bool VideoExtractionQueue::isQueued(const AssetHash& assetHash) const
{
    return m_worker->isQueued(assetHash);
}

int VideoExtractionQueue::pendingCount() const
{
    return m_worker->pendingCount();
}

} // namespace pvf::video2
