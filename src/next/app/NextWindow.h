#pragma once

#include "common/Types.h"
#include "next/app/WorkbenchServices.h"
#include "next/settings/SettingsStore.h"
#include "store/Store.h"

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDockWidget;
class QLabel;
class QSpinBox;
class QTimer;

namespace pvf::mcp2 {
class HttpServer;
class Service;
}

namespace pvf::next {

class AssetPanel;
class HistoryPanel;
class McpLogPanel;
class MediaServices;
class PlaybackController;
class ProjectDocument;
class PropertiesPanel;
class SettingsDialog;
class TimelineWidget;
class ViewportWidget;
class WorkbenchPanel;

// What to do with uncommitted text found when opening a project after an
// unclean shutdown.  The default is Restore: keep the on-disk bytes.
enum class CrashRecoveryChoice
{
    Restore,
    Discard,
    Commit,
    Cancel
};

using CrashRecoveryHandler =
    std::function<CrashRecoveryChoice(const QVector<store::DirtyText>&)>;

// Application shell for the new UI.  Owns a ProjectDocument (which may be null
// while no project is open) and wires the asset bin, viewport, properties and
// timeline panels to it.  New/open project operations replace the document in
// place, so the window itself survives switching projects.
class NextWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NextWindow(std::unique_ptr<ProjectDocument> document,
                        QWidget* parent = nullptr);
    ~NextWindow() override;

    ProjectDocument* document() const { return m_document; }

    // Opens a .pvproj directory.  A path to an old single-file .pvproj is
    // rejected with the standard "旧工程格式已废弃" message.
    bool openProject(const QString& path, QString* error = nullptr);

    void setDocument(std::unique_ptr<ProjectDocument> document);
    void refreshAll();

    // Creates one human-authored entity on the current playhead / private
    // timeline, inserts it into the document and commits it once.
    void createEntity(ContentKind kind);

    // Test hook: point the MCP transport at a temporary config file before
    // opening a project.  Empty restores the default persistent location.
    void setMcpConfigPathForTest(const QString& path);

    // Test hook: decide the crash-recovery action without opening a modal
    // dialog.  An empty handler means "ask the human in a modal dialog".
    void setCrashRecoveryHandlerForTest(CrashRecoveryHandler handler);

    // Test hook: apply a settings snapshot immediately, as if the settings
    // dialog had saved it.
    void setSettingsForTest(const AppSettings& settings);

    // Test hook: capture the folder opened after a successful export instead
    // of launching the desktop file manager.
    void setOpenFolderHandlerForTest(std::function<void(const QString&)> handler);
    void openFolderForTest(const QString& path) { openOutputFolder(path); }

    // History dock accessors and a restore path that skips the modal
    // confirmation, so tests can exercise the store round trip.
    HistoryPanel* historyPanelForTest() const { return m_historyPanel; }
    QDockWidget* historyDockForTest() const { return m_historyDock; }
    QAction* historyActionForTest() const { return m_historyAction; }
    bool restoreCommitForTest(const QString& hash, QString* error = nullptr)
    {
        return restoreCommitInternal(hash, error);
    }

    // True once the first-run onboarding dialog was shown by this window.
    bool onboardingShownForTest() const { return m_onboardingShown; }

    mcp2::Service* mcpServiceForTest() const { return m_mcpService.get(); }
    mcp2::HttpServer* mcpServerForTest() const { return m_mcpServer.get(); }
    MediaServices* mediaServicesForTest() const { return m_media.get(); }

    // Workbench frame cache: the service is rebuilt on project/camera/settings
    // changes, so always fetch it through this accessor.
    WorkbenchServices* workbenchServicesForTest() const
    {
        return m_workbench.get();
    }
    workbench::CacheStats workbenchStatsForTest() const;
    QString workbenchCacheDirForTest() const;
    WorkbenchPrewarmProgress prewarmProgressForTest() const;
    QImage lookupForTest(int frame, const Uuid& cameraUuid = Uuid());
    // Starts the full scan immediately instead of waiting for the 500 ms
    // debounce (the panel and the 缓存 menu use the same entry point).
    void startWorkbenchScanForTest();
    WorkbenchPanel* workbenchPanelForTest() const { return m_workbenchPanel; }
    QAction* workbenchPanelActionForTest() const { return m_workbenchAction; }
    QAction* cacheAllActionForTest() const { return m_cacheAllAction; }
    QAction* cacheToPlayheadActionForTest() const
    {
        return m_cachePlayheadAction;
    }
    QAction* clearWorkbenchCacheActionForTest() const
    {
        return m_cacheClearAction;
    }

    // Settings are loaded from QSettings at construction.  The test accessor
    // returns the currently applied snapshot, not the store's next load().
    AppSettings settingsForTest() const { return m_settings; }
    QAction* settingsActionForTest() const { return m_settingsAction; }
    SettingsDialog* settingsDialogForTest() const { return m_settingsDialog; }
    void openSettingsForTest() { openSettings(); }

    // Project properties dialog and the common editing shortcuts.
    QAction* projectPropertiesActionForTest() const
    {
        return m_projectPropertiesAction;
    }
    QDialog* projectPropertiesDialogForTest() const
    {
        return m_projectPropertiesDialog;
    }
    void openProjectPropertiesForTest() { openProjectProperties(); }
    QAction* copyEntityActionForTest() const { return m_copyEntityAction; }
    QAction* pasteEntityActionForTest() const { return m_pasteEntityAction; }
    QAction* duplicateActionForTest() const { return m_duplicateAction; }
    QAction* fitViewActionForTest() const { return m_fitViewAction; }
    // Test hook: observe the export command without opening the modal dialog.
    void setExportHandlerForTest(std::function<void()> handler)
    {
        m_exportHandler = std::move(handler);
    }

    McpLogPanel* mcpLogPanelForTest() const { return m_mcpLogPanel; }
    QDockWidget* mcpLogDockForTest() const { return m_mcpLogDock; }
    QAction* mcpLogActionForTest() const { return m_mcpLogAction; }

    // Recent projects persisted under the QSettings key "recent/projects".
    int recentProjectCountForTest() const;
    QString recentProjectForTest(int index) const;
    void openRecentForTest(int index);

    // Playback test hooks: the same path as the Space shortcut.
    void togglePlaybackForTest();
    bool isPlayingForTest() const;
    PlaybackController* playbackControllerForTest() const { return m_playback.get(); }

    // Delete flow with the confirmation bypassed for tests.
    bool deleteSelectedEntityForTest(bool confirmed = true,
                                     QString* error = nullptr);
    QAction* deleteActionForTest() const { return m_deleteAction; }
    QCheckBox* autoKeyCheckBoxForTest() const { return m_autoKeyCheck; }

    // Accessors used by the shell tests and by future automation.
    ViewportWidget* viewportForTest() const { return m_viewport; }
    TimelineWidget* timelineForTest() const { return m_timeline; }
    AssetPanel* assetPanelForTest() const { return m_assetPanel; }
    PropertiesPanel* propertiesPanelForTest() const { return m_propertiesPanel; }
    QSpinBox* playheadSpinBoxForTest() const { return m_playheadSpin; }
    QComboBox* cameraComboBoxForTest() const { return m_cameraCombo; }
    QAction* undoActionForTest() const { return m_undoAction; }
    QAction* redoActionForTest() const { return m_redoAction; }
    QAction* exportActionForTest() const { return m_exportAction; }
    QAction* createEntityActionForTest(ContentKind kind) const;

    // Opens the private timeline overlay for a parent entity.  Exposed as a
    // test-friendly wrapper around the same slot the timeline signal uses.
    void openPrivateTimelineForTest(const Uuid& parentUuid);
    QDialog* privateTimelineDialogForTest(const Uuid& parentUuid) const;
    int privateTimelineCountForTest() const { return m_privateTimelines.size(); }

private slots:
    void openPrivateTimeline(const Uuid& parentUuid);
    void chooseNewProject();
    void chooseOpenProject();
    void exportVideo();
    void duplicateSelectedEntity();
    void copySelectedEntity();
    void pasteEntity();
    void fitView();
    void openProjectProperties();
    void deleteSelectedEntity();
    void undo();
    void redo();
    void openSettings();
    void reloadSettingsFromStore();
    void clearHtmlCache();
    void resetMcpToken();
    void detectChrome();
    void showMcpLog();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildEditor();
    void buildWelcome();
    void connectDocument();
    void loadRecentProjects();
    void saveRecentProjects() const;
    void pruneRecentProjects();
    void recordRecentProject(const QString& path);
    void removeRecentProject(const QString& path);
    void openRecentProject(const QString& path);
    QString recentProjectName(const QString& path) const;
    void ensureMediaServices();
    void resetMediaServices();
    void ensureWorkbenchServices();
    void resetWorkbenchServices();
    void updateWorkbenchStatus();
    void cacheAllFrames();
    void cacheToPlayhead();
    void clearWorkbenchCache();
    void configureVideoExtraction();
    CrashRecoveryChoice askCrashRecovery(
        const QVector<store::DirtyText>& dirty);
    void attachMediaProviders();
    void updateActions();
    void updatePlaybackAction();
    void updateStatus();
    void rebuildCameras();
    void startMcpServer();
    void stopMcpServer();
    void applyHumanCommitDelay();
    void applySettings(const AppSettings& previous);
    void showOnboardingIfNeeded();
    void refreshHistory();
    void restoreHistoryCommit(const QString& hash);
    bool restoreCommitInternal(const QString& hash, QString* error);
    void openOutputFolder(const QString& path);
    void refreshAiLock();
    void resetAiLock();
    bool mcpToolRunning() const { return m_activeToolCalls > 0; }
    void updateSettingsDialogContext();
    void startExport(const Uuid& cameraUuid, int startFrame, int endFrame,
                     const QString& outputPath, bool includeAudio,
                     int outputFps = 0, int resolutionScale = 1);
    void applyProjectSettings(const QString& name, const QString& background);
    // Clones `source` with a fresh UUID and commits it once.  Children are not
    // copied.  Returns the new UUID, or an empty one on failure.
    Uuid insertEntityCopy(const model::Entity& source, int offsetX, int offsetY,
                          const QString& message);
    // Ctrl+C/V belong to a focused timeline (keyframes) or text editor.  When
    // such a widget has focus the shortcut is replayed there and the entity
    // clipboard action must not consume it.
    bool forwardClipboardKey(int key) const;
    bool focusIsTextInput() const;
    QVector<Uuid> collectDeletionSet(QString* error) const;
    bool deleteEntitySet(const QVector<Uuid>& uuids, QString* error);
    void closeOrphanedPrivateTimelines();
    void closeAllPrivateTimelines();
    void replaceCentralWidget(QWidget* widget);
    void showError(const QString& title, const QString& error);

    std::unique_ptr<ProjectDocument> m_ownedDocument;
    ProjectDocument* m_document = nullptr;
    std::unique_ptr<mcp2::Service> m_mcpService;
    std::unique_ptr<mcp2::HttpServer> m_mcpServer;
    std::unique_ptr<PlaybackController> m_playback;
    std::unique_ptr<MediaServices> m_media;
    std::unique_ptr<WorkbenchServices> m_workbench;
    bool m_mediaProvidersAttached = false;
    // Keeps the window open and the document/media services alive while the
    // export worker holds raw providers into them.
    bool m_exportRunning = false;
    QString m_mcpConfigPath;
    QString m_mcpStatus;
    QString m_mcpError;

    std::unique_ptr<SettingsStore> m_settingsStore;
    AppSettings m_settings;
    SettingsDialog* m_settingsDialog = nullptr;
    QDialog* m_projectPropertiesDialog = nullptr;

    McpLogPanel* m_mcpLogPanel = nullptr;
    QDockWidget* m_mcpLogDock = nullptr;
    QAction* m_mcpLogAction = nullptr;
    bool m_mcpAutoShown = false;

    HistoryPanel* m_historyPanel = nullptr;
    QDockWidget* m_historyDock = nullptr;
    QAction* m_historyAction = nullptr;

    WorkbenchPanel* m_workbenchPanel = nullptr;
    QDockWidget* m_workbenchDock = nullptr;
    QAction* m_workbenchAction = nullptr;
    QAction* m_cacheAllAction = nullptr;
    QAction* m_cachePlayheadAction = nullptr;
    QAction* m_cacheClearAction = nullptr;
    QTimer* m_workbenchUiTimer = nullptr;

    bool m_onboardingShown = false;
    std::function<void(const QString&)> m_openFolderHandler;
    std::function<void()> m_exportHandler;
    // Application-internal clipboard; only one entity is supported.
    model::Entity m_entityClipboard;
    bool m_hasEntityClipboard = false;
    bool m_aiStopped = false;
    int m_activeToolCalls = 0;
    int m_aiBusyWrites = 0;
    QHash<QString, QString> m_runningToolArgs;
    QHash<QString, int> m_runningWrites;

    AssetPanel* m_assetPanel = nullptr;
    ViewportWidget* m_viewport = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    TimelineWidget* m_timeline = nullptr;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_projectPropertiesAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_gizmoAction = nullptr;
    QAction* m_copyEntityAction = nullptr;
    QAction* m_pasteEntityAction = nullptr;
    QAction* m_duplicateAction = nullptr;
    QAction* m_fitViewAction = nullptr;
    QAction* m_createEmptyAction = nullptr;
    QAction* m_createTextAction = nullptr;
    QAction* m_createVectorAction = nullptr;
    QAction* m_createCameraAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_playPauseAction = nullptr;
    QAction* m_homeAction = nullptr;
    QAction* m_endAction = nullptr;
    QAction* m_prevFrameAction = nullptr;
    QAction* m_nextFrameAction = nullptr;

    QSpinBox* m_playheadSpin = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QCheckBox* m_gizmoCheck = nullptr;
    QCheckBox* m_autoKeyCheck = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_resolutionLabel = nullptr;

    QLabel* m_statusDir = nullptr;
    QLabel* m_statusFrame = nullptr;
    QLabel* m_statusFps = nullptr;
    QLabel* m_statusMcp = nullptr;
    QLabel* m_statusCommit = nullptr;
    QLabel* m_statusAi = nullptr;
    QLabel* m_statusWorkbench = nullptr;

    QHash<Uuid, QDialog*> m_privateTimelines;

    QString m_lastBrowseDir;
    QStringList m_recentProjects;
    CrashRecoveryHandler m_crashRecoveryHandler;
};

} // namespace pvf::next
