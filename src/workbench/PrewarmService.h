#pragma once

#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace pvf::workbench {

// One background scan request.  Everything the worker needs travels by value;
// project/assets are shared so an edit can hand over a new immutable snapshot
// while the previous scan is still winding down.
struct PrewarmRequest
{
    QString cacheRoot; // <project>/.cache/workbench
    Options options;
    Uuid cameraUuid;
    int startFrame = 0;
    int endFrame = -1; // inclusive; -1 = last frame of project content
    int throttleMs = 2;
    std::shared_ptr<const model::Project> project;
    std::shared_ptr<const assets::AssetStore> assets;

    // Called on the worker thread.  Must be thread-safe: video/HTML providers
    // captured by the lambda are not created or torn down by this service.
    std::function<QImage(int frame)> renderFrame;
};

// Serial background frame scan.
//
// Each start() owns one QThread.  The worker constructs its own MetaBuilder
// and Cache inside run(), so the UI thread never touches a Cache instance or a
// QSqlDatabase connection owned by another thread.  Per frame it builds the
// frame meta, skips it when the cache meta matches, and otherwise asks
// renderFrame() for a picture and stores it.
//
// start() cancels the previous task and waits for it; pause()/resume() gate
// the loop between frames with a QWaitCondition; cancel() stops the current
// task and emits finished(true).  Public methods must be called from the
// service's own thread.
class PrewarmService : public QObject
{
    Q_OBJECT
public:
    explicit PrewarmService(QObject* parent = nullptr);
    ~PrewarmService() override;

    PrewarmService(const PrewarmService&) = delete;
    PrewarmService& operator=(const PrewarmService&) = delete;

    void start(PrewarmRequest request);
    void pause();
    void resume();
    void cancel();
    bool isRunning() const;
    bool isPaused() const;

    // Snapshot of the worker cache counters.  Safe to call while a scan runs;
    // the worker publishes its counters after every frame.
    CacheStats lastStats() const;

signals:
    void started(int totalFrames);
    void progress(int done, int total, int frame);
    void frameSkipped(int frame); // meta matched, nothing rendered
    void frameCached(int frame);
    void frameFailed(int frame, const QString& error);
    void finished(bool cancelled);

private:
    class Task;

    // Stops the worker (if any) and waits for its thread.  When report is
    // true a finished() signal is emitted with the task's actual outcome.
    void stopTask(bool report);
    void onTaskFinished(Task* task, quint64 generation);

    Task* m_task = nullptr;
    quint64 m_generation = 0;
    mutable QMutex m_statsMutex;
    CacheStats m_lastStats;
};

} // namespace pvf::workbench
