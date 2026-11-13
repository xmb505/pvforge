#pragma once

#include "core/EntityId.h"
#include "export2/Export.h"
#include "next/settings/SettingsStore.h"
#include "store/Store.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QSet>

#include <atomic>
#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QDockWidget;
class QFrame;
class QKeyEvent;
class QProgressBar;
class QLabel;
class QScrollArea;
class QTabWidget;
class QTimer;
class QToolButton;

namespace pvf {

namespace html {
class HtmlBrowser;
} // namespace html

namespace mcp2 {
class HttpServer;
}

namespace next {
class HistoryPanel;
}

class AssetLibraryPanel;
class CameraMonitor;
class CheckpointManager;
class EditorSession;
class EnvironmentPanel;
class ExportProgressDialog;
class McpLogPanel;
class PerfMonitorWindow;
class ProjectPropertiesPanel;
class PropertiesPanel;
class SettingsDialog;
class TimelinePanel;
class VideoProxyGenerator;
class Viewport;
class WorkbenchPanel;
class WorkbenchPanelHost;

// Crash recovery choices for the session shell.  The store scans uncommitted
// text when the project opens; the shell asks before touching anything.
enum class SessionCrashRecoveryChoice
{
    Restore,
    Discard,
    Commit,
    Cancel
};
using SessionCrashRecoveryHandler =
    std::function<SessionCrashRecoveryChoice(const QVector<store::DirtyText>&)>;

// Opens a `.pvproj` directory and resolves uncommitted text through `handler`
// (or a modal dialog when empty).  Exposed so the crash-recovery branches can
// be tested without driving a dialog.
std::unique_ptr<EditorSession> openSessionProjectWithRecovery(
    const QString& path, bool useGit, const SessionCrashRecoveryHandler& handler,
    QWidget* parent, QString* error);

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // P1 shell: takes ownership of the session and builds the window around
    // the session model (viewport, timeline, properties, assets, workbench).
    explicit MainWindow(EditorSession* session, QWidget* parent = nullptr);
    ~MainWindow() override;

    // P2a session shell: the window owns the session; tests read the Document
    // through this accessor instead of reaching into private members.
    EditorSession* editorSessionForTest() const { return m_session; }
    Viewport* viewportForTest() const { return m_viewport; }
    AssetLibraryPanel* assetLibraryForTest() const { return m_library; }
    bool replaceAssetForTest(const QString& uuid, const QString& hash,
                             QString* error = nullptr);
    EntityId importImageFile(const QString& path);
    void goToFrame(int frame) { setFrame(frame); }
    int currentFrame() const { return m_frame; }
    void togglePlayback();
    void setSelectedEntityForTest(EntityId id) { setSelectedEntity(id); }
    void applyPivotForTest(EntityId id, double ax, double ay) { applyPivot(id, ax, ay); }
    void applyCameraForTest(EntityId id, double x, double y, double scale, double rotation)
    {
        applyCamera(id, x, y, scale, rotation);
    }
    EntityId selectedEntityForTest() const { return m_selected; }
    int currentFrameForTest() const { return m_frame; }
    void pasteEntityForTest() { pasteEntity(); }
    void autoSaveNowForTest();
    void reloadAutosaveSettingsForTest();
    bool autosaveActiveForTest() const;
    int autosaveIntervalForTest() const;
    McpLogPanel* mcpLogPanelForTest() const { return m_mcpLog; }
    PerfMonitorWindow* perfMonitorWindowForTest() const { return m_perfWindow; }

    // ---- P6 session-shell test accessors --------------------------------
    bool humanEditingEnabledForTest() const;
    TimelinePanel* timelineForTest() const { return m_timeline; }
    PropertiesPanel* propertiesForTest() const { return m_props; }
    WorkbenchPanel* workbenchForTest() const { return m_workbench; }
    WorkbenchPanelHost* workbenchHostForTest() const { return m_workbenchHost; }
    next::HistoryPanel* historyPanelForTest() const { return m_sessionHistory; }
    SettingsDialog* openSessionSettingsDialogForTest();
    // Absolute `.cache` path (or one of its subdirectories) for the open
    // project; tests assert the path without launching a file manager.
    QString sessionCacheFolderPathForTest(const QString& subdirectory = QString()) const;
    bool openSessionProjectForTest(const QString& path, QString* error = nullptr);
    void setCrashRecoveryHandlerForTest(SessionCrashRecoveryHandler handler)
    {
        m_crashRecoveryHandler = std::move(handler);
    }
    void showMcpLogForTest();
    QString mcpStatusTextForTest() const;
    void triggerSessionAutoSaveForTest();
    bool restoreSessionCommitForTest(const QString& hash, QString* error = nullptr);

    // ---- P7a export test hooks ------------------------------------------
    bool exportRunningForTest() const { return m_exportRunning; }
    void cancelSessionExportForTest();
    void startSessionExportForTest(const Uuid& cameraUuid, int startFrame,
                                   int endFrame, const QString& outputPath,
                                   bool includeAudio, int outputFps,
                                   int resolutionScale, int crf,
                                   const QString& encoder, const QString& preset,
                                   bool overwrite);
    void setExportOptionsProbeForTest(
        std::function<void(const export2::Options&)> probe)
    {
        m_exportOptionsProbe = std::move(probe);
    }
    void setExportFinishedHandlerForTest(
        std::function<void(bool, const QString&)> handler)
    {
        m_exportFinishedHandler = std::move(handler);
    }
    void setExportProgressHandlerForTest(
        std::function<void(int, int, double)> handler)
    {
        m_exportProgressHandler = std::move(handler);
    }
    // Tests assert the result through the finished handler instead of a modal
    // completion box.
    void setSuppressExportCompletionDialogForTest(bool suppress)
    {
        m_suppressExportCompletionDialog = suppress;
    }

    // Scene and annotation UI tests drive the same code paths as the menus
    // without opening modal dialogs.
    QString addSceneForTest(const QString& name, int startFrame, int endFrame,
                            const QColor& color = QColor(),
                            const QString& note = QString());
    bool updateSceneForTest(const QString& id, const QString& name, int startFrame,
                            int endFrame, const QColor& color,
                            const QString& note, QString* error = nullptr);
    bool removeSceneForTest(const QString& id);
    bool setSceneRetiredForTest(const QString& id, bool retired);
    QString addAnnotationForTest(int frame, const QString& title,
                                 const QString& note = QString(),
                                 const QStringList& tags = QStringList(),
                                 const QColor& color = QColor());
    bool updateAnnotationForTest(const QString& id, int frame, const QString& title,
                                 const QString& note, const QStringList& tags,
                                 const QColor& color, QString* error = nullptr);
    bool removeAnnotationForTest(const QString& id);
    bool setKeyframeNoteForTest(EntityId id, double frame, const QString& note);
    EntityId createFolderForTest(EntityId parent = kInvalidEntity);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();

    void setFrame(int frame);
    void setSelectedEntity(EntityId id);
    void applyValue(EntityId id, const QString& prop, double value);
    void applyPosition(EntityId id, double x, double y);
    void applyScale(EntityId id, double sx, double sy, double x, double y);
    void applyPivot(EntityId id, double ax, double ay);
    void applyEntityData(EntityId id, const QVariantMap& data);
    void applyComicTip(EntityId id, double tipX, double tipY);
    void replaceSourceFromDialog(EntityId id);
    void replaceSessionAssetFromDialog(const QString& uuid);

    void copySelectedEntity();
    void pasteEntity();
    void duplicateEntity(EntityId id);
    void groupEntitiesIntoFolder(const QVector<EntityId>& ids);
    void onPropertyKeyframeToggled(EntityId id, const QString& prop, double frame,
                                   bool remove);
    void applyCamera(EntityId id, double x, double y, double scale, double rotation);
    void refreshAll();
    void ensureProxies();
    void onPlayTick();
    void updateStatus();
    void updateMcpStatus();
    void arrangeMcpLog();
    void wireMcpDialog(SettingsDialog* dialog);
    bool armMcpWrites(QString* error = nullptr);
    void disarmMcpWrites();
    void stopMcpWrites();
    void toggleWorkbench();
    void deleteSelectedEntity();
    void onUndo();
    void onRedo();
    void onEntityCreated(EntityId id);
    void updateWindowTitle();
    void updatePerfLabel();
    QAction* createPerfMonitorWindowAction();
    void showPerfMonitorWindow(bool show);
    void showImportBusy(int count);
    void hideImportBusy();
    bool saveProject();
    bool saveProjectAs();

    // ---- P1 session shell -----------------------------------------------
    void buildSessionShell();
    void createSessionMenus();
    void createSessionToolBar();
    void refreshSession();
    void updateSessionStatus();
    void updateSessionPlaybackUi();
    void sessionGoToFrame(int frame);
    bool saveSessionProject();
    void openSessionProject();
    void createSessionProject();
    void showSessionWindow(std::unique_ptr<EditorSession> session);
    void exportVideo();
    void startSessionExport(const Uuid& cameraUuid, int startFrame, int endFrame,
                            const QString& outputPath, bool includeAudio,
                            int outputFps, int resolutionScale, int crf,
                            const QString& encoder, const QString& preset,
                            bool overwrite);
    void autoSave();
    void updateAutosaveTimer();

    // ---- P6 MCP / Git / settings ----------------------------------------
    void wireSessionMcpServer();
    void onSessionMcpToolStarted(const QString& name, const QString& argsJson,
                                 bool write);
    void onSessionMcpToolFinished(const QString& name, const QString& resultJson,
                                  qint64 ms, bool ok);
    void onSessionMcpStoppedChanged(bool stopped);
    void refreshSessionAiLock();
    void setHumanEditingEnabled(bool enabled);
    void updateSessionActionStates();
    void refreshSessionAfterExternalChange();
    void updateSessionMcpStatus();
    void showSessionMcpLog();
    void openSessionSettings();
    void applySessionSettings(const next::AppSettings& settings);
    void wireSessionSettingsDialog(SettingsDialog* dialog);
    void updateSessionSettingsDialogContext();
    void refreshSessionCacheStats();
    void clearSessionWorkbenchCache();
    void openSessionCacheFolder(const QString& subdirectory);
    void refreshSessionHistory();
    void restoreSessionCommit(const QString& hash);
    bool restoreSessionCommitInternal(const QString& hash, QString* error);
    void showSessionOnboardingIfNeeded();
    void sessionAutoSave();

    // Scene and annotation UI helpers.
    void showSceneDialog(const QString& sceneId = QString());
    void showAnnotationDialog(const QString& annotationId = QString(),
                              int frame = -1);
    void editKeyframeNote(EntityId id, double frame);
    bool commitSceneCreate(const QString& name, int startFrame, int endFrame,
                           const QColor& color, const QString& note,
                           QString* error = nullptr, QString* createdId = nullptr);
    bool commitSceneUpdate(const QString& id, const QString& name, int startFrame,
                           int endFrame, const QColor& color, const QString& note,
                           QString* error = nullptr);
    bool commitSceneRemove(const QString& id);
    bool commitSceneRetired(const QString& id, bool retired);
    bool commitAnnotationCreate(int frame, const QString& title,
                                const QString& note, const QStringList& tags,
                                const QColor& color, QString* error = nullptr,
                                QString* createdId = nullptr);
    bool commitAnnotationUpdate(const QString& id, int frame, const QString& title,
                                const QString& note, const QStringList& tags,
                                const QColor& color, QString* error = nullptr);
    bool commitAnnotationRemove(const QString& id);
    bool commitKeyframeNote(EntityId id, double frame, const QString& note);
    void applyActiveSceneParent(EntityId id);

    EditorSession* m_session = nullptr;
    // Declared before the session-mode panels so the session outlives them.
    std::unique_ptr<EditorSession> m_sessionOwner;
    Viewport* m_viewport = nullptr;
    PropertiesPanel* m_props = nullptr;
    AssetLibraryPanel* m_library = nullptr;
    ProjectPropertiesPanel* m_projectProps = nullptr;
    EnvironmentPanel* m_environment = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QWidget* m_projectTab = nullptr;
    QScrollArea* m_objectTab = nullptr;
    TimelinePanel* m_timeline = nullptr;
    WorkbenchPanel* m_workbench = nullptr;
    WorkbenchPanelHost* m_workbenchHost = nullptr;
    CameraMonitor* m_monitor = nullptr;
    McpLogPanel* m_mcpLog = nullptr;
    PerfMonitorWindow* m_perfWindow = nullptr;
    // Session-shell MCP endpoint wiring.  The listener belongs to EditorSession;
    // this only tracks the server the signals are connected to so a restart
    // does not double-connect.
    mcp2::HttpServer* m_wiredSessionMcpServer = nullptr;
    QAction* m_mcpLogAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_historyAction = nullptr;
    QDockWidget* m_sessionHistoryDock = nullptr;
    next::HistoryPanel* m_sessionHistory = nullptr;
    SettingsDialog* m_sessionSettingsDialog = nullptr;
    bool m_sessionAiStopped = false;
    int m_sessionAiBusyWrites = 0;
    QHash<QString, int> m_sessionRunningWrites;
    QHash<QString, QString> m_sessionRunningToolArgs;
    SessionCrashRecoveryHandler m_crashRecoveryHandler;
    // Created only while a human has armed writes for one scene. Global
    // objects (active camera, audio, root-level logos) stay locked because
    // McpCommandService::allowGlobal() is false by default; a future UI can
    // call setAllowGlobal(true) after a separate human confirmation.
    std::unique_ptr<CheckpointManager> m_mcpCheckpoints;

    QTimer* m_playTimer = nullptr;
    QTimer* m_scrubTimer = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    QElapsedTimer m_playClock;
    QElapsedTimer m_scrubClock;
    int m_lastScrubFrame = -1;
    QLabel* m_status = nullptr;
    QLabel* m_perfLabel = nullptr;
    QToolButton* m_mcpStatusButton = nullptr;
    QString m_mcpLastError;
    QString m_mcpCheckpointId;
    QTimer* m_perfTimer = nullptr;
    QAction* m_perfAction = nullptr;
    QProgressBar* m_importProgress = nullptr;

    QAction* m_playAction = nullptr;
    QAction* m_autoKeyAction = nullptr;
    QAction* m_packAssetsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_exportAction = nullptr;
    // P7a export worker.  The dialog and watcher live on the UI thread; the
    // atomics are the only state the worker touches.
    QFutureWatcher<export2::Result>* m_exportWatcher = nullptr;
    ExportProgressDialog* m_exportProgress = nullptr;
    QTimer* m_exportTimer = nullptr;
    std::shared_ptr<std::atomic_bool> m_exportCancelled;
    std::shared_ptr<std::atomic_int> m_exportFrame;
    std::shared_ptr<std::atomic_int> m_exportTotal;
    std::shared_ptr<std::atomic<double>> m_exportEta;
    bool m_exportRunning = false;
    bool m_suppressExportCompletionDialog = false;
    std::function<void(const export2::Options&)> m_exportOptionsProbe;
    std::function<void(bool, const QString&)> m_exportFinishedHandler;
    std::function<void(int, int, double)> m_exportProgressHandler;
    QAction* m_homeAction = nullptr;
    QAction* m_endAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_quitAction = nullptr;

    int m_frame = 0;
    int m_playStart = 0;
    bool m_playing = false;
    EntityId m_selected = kInvalidEntity;

    // P1 session shell widgets/actions.
    ProjectPropertiesPanel* m_sessionProps = nullptr;
    EnvironmentPanel* m_sessionEnvironment = nullptr;
};

} // namespace pvf
