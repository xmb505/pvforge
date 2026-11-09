#pragma once

#include "common/Types.h"

#include <QObject>
#include <QString>

namespace pvf::video2 {

// One background video-to-WebP-sequence extraction request. `cacheRoot` is the
// project's `.cache/video` directory; the final cache key adds hash/fps/maxWidth.
struct ExtractionJob
{
    AssetHash assetHash;
    QString videoPath;
    int projectFps = 30;
    int maxWidth = 960;
    QString cacheRoot;
    qint64 diskBudget = 2LL * 1024 * 1024 * 1024;
};

using VideoExtractionJob = ExtractionJob;

// Serialises whole-video frame extraction on one private QThread so imports
// never block the UI. Jobs with a complete cache index are skipped, and the
// same asset hash is never queued twice.
//
// Signals are emitted from the worker thread and delivered to the caller's
// thread through normal queued connections.
class VideoExtractionQueue : public QObject
{
    Q_OBJECT

public:
    using Job = ExtractionJob;

    explicit VideoExtractionQueue(QObject* parent = nullptr);
    ~VideoExtractionQueue() override;

    VideoExtractionQueue(const VideoExtractionQueue&) = delete;
    VideoExtractionQueue& operator=(const VideoExtractionQueue&) = delete;

    // Skips jobs whose index already covers every frame. Invalid jobs emit
    // failed() immediately and are not queued.
    void enqueue(const Job& job);

    // Cancels the running job (if any) or removes the queued job. Emits
    // cancelled() once the worker stops a running job, or immediately for a
    // queued job. Unknown hashes are ignored.
    void cancel(const AssetHash& assetHash);

    // Cancels the running job and every queued job. The queue stays usable.
    void cancelAll();

    bool isRunning(const AssetHash& assetHash) const;
    bool isQueued(const AssetHash& assetHash) const;
    int pendingCount() const;

    // Copies the pending or running job for `assetHash` into `job`. Returns
    // false when the hash is neither queued nor running, or when `job` is null.
    // Read-only; lets callers verify the effective maxWidth/diskBudget.
    bool jobFor(const AssetHash& assetHash, ExtractionJob* job) const;

signals:
    void started(const QString& hash, int total);
    void progress(const QString& hash, int done, int total);
    void finished(const QString& hash);
    void failed(const QString& hash, const QString& error);
    void cancelled(const QString& hash);

private:
    class Worker;
    Worker* m_worker = nullptr;
};

} // namespace pvf::video2
