#pragma once

#include "anim/Anim.h"
#include "common/Types.h"
#include "mcp2/Service.h"
#include "next/settings/SettingsStore.h"
#include "solver/Solver.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace pvf {

namespace model {
struct Project;
struct ProjectSettings;
}

namespace assets {
class AssetStore;
}

namespace next {
class MediaServices;
class PlaybackController;
class ProjectDocument;
class WorkbenchServices;
} // namespace next

namespace mcp2 {
class HttpServer;
} // namespace mcp2

class EditActions;

// Thin adapter between the legacy application shell and the new
// next::ProjectDocument.  It owns the document and the per-project backend
// services the old shell needs (media decoders, the workbench cache and the
// playback clock).  It deliberately keeps no shadow copy of the project model.
class EditorSession
{
public:
    ~EditorSession();

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    // Creates a new `.pvproj` directory project.  Git is off in P1: the store
    // still writes atomically and keeps in-memory undo.
    static std::unique_ptr<EditorSession> createProject(
        const QString& projectDir, const model::ProjectSettings& settings,
        QString* error = nullptr, bool useGit = false);

    // Opens an existing `.pvproj` directory.  Legacy single-file projects are
    // rejected with a migration hint instead of being parsed.
    static std::unique_ptr<EditorSession> openProject(const QString& projectDir,
                                                      QString* error = nullptr,
                                                      bool useGit = false);

    next::ProjectDocument* document() const { return m_document.get(); }
    // The only human write entry point; one gesture becomes one commit.
    EditActions* editActions() const { return m_editActions.get(); }

    const model::Project& project() const;
    model::Project& mutableProject();
    assets::AssetStore& assets();

    const model::ProjectSettings& settings() const;
    QString root() const;
    QString name() const;

    // Backend services.  MediaServices and WorkbenchServices must be destroyed
    // before the document; member order in the implementation guarantees it.
    next::MediaServices* media() const { return m_media.get(); }
    next::PlaybackController* playback() const { return m_playback.get(); }
    // Returns the workbench cache for the current camera, rebuilding it when
    // the active camera or the expand setting changed.  May return nullptr when
    // the cache is disabled.  Creating the service does not create files;
    // frames are written only when the viewport requests and misses them.
    next::WorkbenchServices* workbench();
    const next::AppSettings& appSettings() const { return m_appSettings; }
    next::SettingsStore* settingsStore() const { return m_settingsStore.get(); }

    // Applies an AppSettings snapshot to every backend service without
    // recreating the window: media providers, playback watchdog, workbench
    // cache, human commit debounce, Git state and the MCP endpoint.  The
    // snapshot is persisted.  A failed Git switch is reported through `error`
    // and `gitEnabled` is corrected to the state the store actually reached.
    bool applyAppSettings(const next::AppSettings& settings,
                          QString* error = nullptr);

    // Drops the workbench cache; the next workbench() call rebuilds it.
    // Frames return on demand; this never starts a scan.  MCP write tools call
    // this after changing project bytes.
    void invalidateWorkbench();

    // Persistent animation/solver cache.  Poses and segment results live for
    // the whole project lifetime; a document edit clears them and queues one
    // whole-project precompute.  Every viewport path, the timeline and the
    // properties panel read this same instance, so one segment is solved once
    // per edit instead of once per painted frame.
    anim::Cache &animCache() const;
    // Clears the cache and arranges exactly one re-warm.  During playback the
    // clear is deferred so painting only ever reads a warm cache.
    void invalidateAnimCache();

    // Shared solver-result map borrowed by the media/workbench services.
    // Prefer animCache()/solverResultFor() for new call sites.
    QHash<QString, solver::Result> &solverResults() const;
    void invalidateSolverResults();
    // Diagnostic/test hook: real solver::evaluateResult() calls since process
    // start (cache hits do not count).
    int solverEvaluationCount() const;
    // Cached segment result, evaluated once on a cold cache while paused.
    // nullptr while playing with a cold cache.
    const solver::Result *solverResultFor(const Uuid &entityUuid,
                                          const Uuid &solverId) const;
    // Effective band end: Result::stopFrame clipped by the structural end, or
    // -1 when the segment cannot be solved.
    int solverStopFrame(const Uuid &entityUuid, const Uuid &solverId) const;

    // MCP endpoint.  The service and HTTP listener belong to the session and
    // are started when the project opens, stopped on close/switch.  The port
    // comes from AppSettings; the Bearer token is persisted by HttpServer.
    mcp2::Service* mcpService() const { return m_mcpService.get(); }
    mcp2::HttpServer* mcpServer() const { return m_mcpServer.get(); }
    QString mcpError() const { return m_mcpError; }
    void restartMcpServer();
    // Stops the listener without destroying the session; used when switching
    // projects so the next session can bind the same configured port.
    void stopMcpServer();

#ifdef PVFORGE_TEST_HOOKS
    // Test hooks: a temporary config file (usually with port 0) and a port
    // override keep the test process off the user's real endpoint file.
    static void setMcpConfigPathForTest(const QString& path);
    static void setMcpPortOverrideForTest(quint16 port);
#endif

    // AI write lock.  While false every human write path (EditActions and
    // ProjectDocument) refuses with an error, and the shell disables its
    // editing widgets.  Read-only views keep working.
    void setHumanEditingEnabled(bool enabled);
    bool humanEditingEnabled() const { return m_humanEditingEnabled; }

    // The shell enriches view.state with timeline-only fields (parent/zoom).
    void setViewStateExtras(std::function<void(mcp2::ViewState&)> extras)
    {
        m_viewStateExtras = std::move(extras);
    }

    // Camera used for the viewport viewfinder overlay and the camera monitor.
    // The explicitly chosen monitor camera wins; otherwise the selected
    // camera.  Empty means "whole canvas" and no frame.
    Uuid activeCameraUuid() const;
    void setMonitorCameraUuid(const Uuid& uuid);
    // Mirrors the legacy "selecting a camera makes it the framing camera"
    // rule.  Clearing the selection returns to the explicit monitor choice.
    void syncSelectionCamera(const Uuid& selected);

    int playhead() const;
    void setPlayhead(int frame);
    void savePlayhead();

    // Commits any pending debounced write and persists the playhead.  The shell
    // calls this on Ctrl+S, close and project switch.
    void flush(QString* error = nullptr);

    bool hasPendingCommit() const;
    bool commitProject(const QString& message = QStringLiteral("pvf: 工程设置"),
                       QString* error = nullptr);

private:
    explicit EditorSession(std::unique_ptr<next::ProjectDocument> document);

    void startMcpServer();
    // Coalesces re-warm requests into one queued call; a request that arrives
    // while the precompute is queued or running is dropped.
    void scheduleSolverPrecompute();
    // Solves every segment of every solver entity once and stores the results
    // in the shared map.  Never runs while playback is advancing.
    void precomputeSolverResults();

    // Declared before the services so the services die first.
    std::unique_ptr<next::ProjectDocument> m_document;
    // Shared solver results; declared early so every borrowing service
    // (workbench, viewport caches) is destroyed before this map.  Mutable
    // because the const paint paths write through it.
    mutable QHash<QString, solver::Result> m_solverResults;
    // Persistent cache handed to every session render path.  Its shared map
    // points at m_solverResults so the media/workbench services see the same
    // solved segments.
    mutable anim::Cache m_animCache;
    // A project edit cleared the map but playback prevented the re-warm; the
    // pause handler retries.
    bool m_solverResultsDirty = true;
    bool m_solverPrecomputeScheduled = false;
    bool m_solverPrecomputeRunning = false;
    std::unique_ptr<next::SettingsStore> m_settingsStore;
    next::AppSettings m_appSettings;
    std::unique_ptr<next::MediaServices> m_media;
    std::unique_ptr<next::WorkbenchServices> m_workbench;
    std::unique_ptr<next::PlaybackController> m_playback;
    std::unique_ptr<EditActions> m_editActions;
    std::unique_ptr<mcp2::Service> m_mcpService;
    std::unique_ptr<mcp2::HttpServer> m_mcpServer;
    QString m_mcpError;
    std::function<void(mcp2::ViewState&)> m_viewStateExtras;
    bool m_humanEditingEnabled = true;
    Uuid m_monitorCamera;
    bool m_monitorCameraExplicit = false;
    bool m_monitorCameraFromSelection = false;
};

// Recent project directories, newest first.  Entries that are no longer a
// `.pvproj` directory with a project.json are dropped when read.
QStringList recentProjects();
void rememberRecentProject(const QString& path);

} // namespace pvf
