#pragma once

#include "common/Types.h"
#include "next/settings/SettingsStore.h"

#include <QHash>
#include <QMainWindow>
#include <QVector>

#include <memory>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDockWidget;
class QLabel;
class QSpinBox;

namespace pvf::mcp2 {
class HttpServer;
class Service;
}

namespace pvf::next {

class AssetPanel;
class McpLogPanel;
class MediaServices;
class PlaybackController;
class ProjectDocument;
class PropertiesPanel;
class SettingsDialog;
class TimelineWidget;
class ViewportWidget;

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

    mcp2::Service* mcpServiceForTest() const { return m_mcpService.get(); }
    mcp2::HttpServer* mcpServerForTest() const { return m_mcpServer.get(); }
    MediaServices* mediaServicesForTest() const { return m_media.get(); }

    // Settings are loaded from QSettings at construction.  The test accessor
    // returns the currently applied snapshot, not the store's next load().
    AppSettings settingsForTest() const { return m_settings; }
    QAction* settingsActionForTest() const { return m_settingsAction; }
    SettingsDialog* settingsDialogForTest() const { return m_settingsDialog; }
    void openSettingsForTest() { openSettings(); }

    McpLogPanel* mcpLogPanelForTest() const { return m_mcpLogPanel; }
    QDockWidget* mcpLogDockForTest() const { return m_mcpLogDock; }
    QAction* mcpLogActionForTest() const { return m_mcpLogAction; }

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
    void ensureMediaServices();
    void resetMediaServices();
    void attachMediaProviders();
    void updateActions();
    void updatePlaybackAction();
    void updateStatus();
    void rebuildCameras();
    void startMcpServer();
    void stopMcpServer();
    void applyHumanCommitDelay();
    void applySettings(const AppSettings& previous);
    void refreshAiLock();
    void resetAiLock();
    bool mcpToolRunning() const { return m_activeToolCalls > 0; }
    void updateSettingsDialogContext();
    void startExport(const Uuid& cameraUuid, int startFrame, int endFrame,
                     const QString& outputPath, bool includeAudio);
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
    bool m_mediaProvidersAttached = false;
    QString m_mcpConfigPath;
    QString m_mcpStatus;
    QString m_mcpError;

    std::unique_ptr<SettingsStore> m_settingsStore;
    AppSettings m_settings;
    SettingsDialog* m_settingsDialog = nullptr;

    McpLogPanel* m_mcpLogPanel = nullptr;
    QDockWidget* m_mcpLogDock = nullptr;
    QAction* m_mcpLogAction = nullptr;
    bool m_mcpAutoShown = false;
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
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_gizmoAction = nullptr;
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

    QHash<Uuid, QDialog*> m_privateTimelines;

    QString m_lastBrowseDir;
};

} // namespace pvf::next
