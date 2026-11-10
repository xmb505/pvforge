#pragma once

#include "core/History.h"
#include "core/Project.h"
#include "html/HtmlAsset.h"
#include "media/VideoPlayer.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QSet>

#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
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
class HtmlRenderer;
} // namespace html

class AssetLibraryPanel;
class AudioEngine;
class CameraMonitor;
class CheckpointManager;
class CurveEditor;
class EditorSession;
class EnvironmentPanel;
class McpCommandService;
class McpLogPanel;
class McpServer;
class ProjectPropertiesPanel;
class PropertiesPanel;
class SettingsDialog;
class TimelinePanel;
class VideoExporter;
class VideoProxyGenerator;
class Viewport;
class WaveformView;
class WorkbenchPanel;

struct MediaImportResult;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Legacy shell used by the pre-P2 panels and the existing self-tests.
    explicit MainWindow(Project* project, QWidget* parent = nullptr);
    // P1 shell: takes ownership of the session and builds the minimal window
    // (project properties + read-only placeholder). Panels from the legacy
    // shell are intentionally not instantiated in this mode.
    explicit MainWindow(EditorSession* session, QWidget* parent = nullptr);
    ~MainWindow() override;

    Project* project() const { return m_project; }
    // P2a session shell: the window owns the session; tests read the Document
    // through this accessor instead of reaching into private members.
    EditorSession* editorSessionForTest() const { return m_session; }
    EntityId importImageFile(const QString& path);
    void goToFrame(int frame) { setFrame(frame); }
    int currentFrame() const { return m_frame; }
    void togglePlayback();
    void setSelectedEntityForTest(EntityId id) { setSelectedEntity(id); }
    void importMediaFiles(const QStringList& files,
                          std::function<void(const QVector<MediaImportResult>&)> done);
    void applyPivotForTest(EntityId id, double ax, double ay) { applyPivot(id, ax, ay); }
    void applyCameraForTest(EntityId id, double x, double y, double scale, double rotation)
    {
        applyCamera(id, x, y, scale, rotation);
    }
    EntityId selectedEntityForTest() const { return m_selected; }
    int currentFrameForTest() const { return m_frame; }
    void copyEntityForTest(EntityId id) { copyEntityToClipboard(id); }
    void pasteEntityForTest() { pasteEntity(); }
    void duplicateEntityForTest(EntityId id) { duplicateEntity(id); }
    void groupEntitiesForTest(const QVector<EntityId>& ids) { groupEntitiesIntoFolder(ids); }
    void autoSaveNowForTest();
    void reloadAutosaveSettingsForTest();
    bool autosaveActiveForTest() const;
    int autosaveIntervalForTest() const;
    McpServer* mcpServerForTest() const { return m_mcpServer; }
    McpLogPanel* mcpLogPanelForTest() const { return m_mcpLog; }
    McpCommandService* mcpCommandsForTest() const { return m_mcpCommands.get(); }
    bool armMcpWritesForTest(QString* error = nullptr) { return armMcpWrites(error); }
    void stopMcpWritesForTest() { stopMcpWrites(); }
    void disarmMcpWritesForTest() { disarmMcpWrites(); }
    // Test entry point that uses the same dialog wiring as Edit -> Settings,
    // but shows the dialog non-modally so a test can drive its controls.
    SettingsDialog* openSettingsDialogForTest();

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

    struct EntityClipboardEntry
    {
        Entity entity;
        EntityId parent = kInvalidEntity;
    };
    struct EntityClipboard
    {
        QVector<EntityClipboardEntry> entries;   // subtree, root first
        EntityId rootId = kInvalidEntity;
        bool hasPairedAudio = false;
        Entity pairedAudio;
    };
    EntityClipboard buildEntityClip(EntityId id) const;
    void copyEntityToClipboard(EntityId id);
    void pasteEntityClip(EntityClipboard& clip);
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
    void onEntityFlagChanged(EntityId id, EntityFlag flag, bool value);
    void onDeleteKeyframesRequested();
    void onKeyframeEasingRequested(const EasingSpec& spec);
    void onKeyframeEasingEdited(EntityId id, int frame, const EasingSpec& spec);
    void updateWindowTitle();
    void updatePerfLabel();
    void showImportBusy(int count);
    void hideImportBusy();
    bool saveProject();
    bool saveProjectAs();

    // ---- P1 session shell -----------------------------------------------
    void buildSessionShell();
    void createSessionMenus();
    void refreshSession();
    void updateSessionPlaceholder();
    bool saveSessionProject();
    void openSessionProject();
    void createSessionProject();
    void showSessionWindow(std::unique_ptr<EditorSession> session);
    void exportVideo();
    void autoSave();
    void updateAutosaveTimer();
    void onEntityRenamed(EntityId id, const QString& name);
    void onAudioMoved(EntityId id, double startFrame);
    void onParentEdited(EntityId id, EntityId parent);
    void onEntityMoveRequested(EntityId id, EntityId parent, int index);
    void onEntitiesMoveRequested(const QVector<EntityId>& ids, EntityId parent,
                                 int index);
    void onKeyframesMoveRequested(const QVector<KeyframeRecord>& records,
                                  double delta);
    void onCreateFolderRequested(EntityId parent);
    void packAllAssets();

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

    // ---- HTML layers ----------------------------------------------------
    void scheduleHtmlPreview(bool immediate = false);
    void requestHtmlPreviews();
    void cleanupHtmlRenderers();
    QVector<EntityId> htmlLayersForFrame(int frame) const;
    QSize htmlPreviewSize() const;
    html::HtmlAsset htmlAssetFor(const Entity& entity, QString* error);
    html::HtmlRenderer* htmlRendererFor(EntityId id);
    void applyHtmlFrame(EntityId id, quint64 token, const QString& cacheKey,
                        const QImage& image, const QString& error);

    Project* m_project = nullptr;
    EditorSession* m_session = nullptr;
    // Declared before the session-mode panels so the session outlives them.
    std::unique_ptr<EditorSession> m_sessionOwner;
    History* m_history = nullptr;
    Viewport* m_viewport = nullptr;
    PropertiesPanel* m_props = nullptr;
    AssetLibraryPanel* m_library = nullptr;
    ProjectPropertiesPanel* m_projectProps = nullptr;
    EnvironmentPanel* m_environment = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QWidget* m_projectTab = nullptr;
    QScrollArea* m_objectTab = nullptr;
    TimelinePanel* m_timeline = nullptr;
    CurveEditor* m_curve = nullptr;
    WaveformView* m_wave = nullptr;
    WorkbenchPanel* m_workbench = nullptr;
    CameraMonitor* m_monitor = nullptr;
    McpServer* m_mcpServer = nullptr;
    McpLogPanel* m_mcpLog = nullptr;
    // Created only while a human has armed writes for one scene. Global
    // objects (active camera, audio, root-level logos) stay locked because
    // McpCommandService::allowGlobal() is false by default; a future UI can
    // call setAllowGlobal(true) after a separate human confirmation.
    std::unique_ptr<McpCommandService> m_mcpCommands;
    std::unique_ptr<CheckpointManager> m_mcpCheckpoints;
    html::HtmlBrowser* m_htmlBrowser = nullptr;
    QHash<EntityId, html::HtmlRenderer*> m_htmlRenderers;
    QHash<QString, html::HtmlAsset> m_htmlAssets;
    QHash<QString, QString> m_htmlAssetStamps;
    QHash<EntityId, quint64> m_htmlTokens;
    QHash<EntityId, QString> m_htmlCacheKeys;
    QSet<EntityId> m_htmlInFlight;
    QTimer* m_htmlPreviewTimer = nullptr;
    QElapsedTimer m_htmlPreviewClock;
    QElapsedTimer m_htmlBrowserRetryClock;
    bool m_htmlExporting = false;
    QString m_htmlExportError;
    VideoExporter* m_exporter = nullptr;
    AudioEngine* m_audioEngine = nullptr;
    VideoPlayer m_videoPlayer;
    VideoProxyGenerator* m_proxyGenerator = nullptr;

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
    QAction* m_homeAction = nullptr;
    QAction* m_endAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_quitAction = nullptr;

    int m_frame = 0;
    int m_playStart = 0;
    bool m_playing = false;
    EntityId m_selected = kInvalidEntity;
    std::unique_ptr<EntityClipboard> m_entityClip;

    // P1 session shell widgets/actions (unused by the legacy constructor).
    ProjectPropertiesPanel* m_sessionProps = nullptr;
    EnvironmentPanel* m_sessionEnvironment = nullptr;
    QFrame* m_sessionCanvas = nullptr;
    QLabel* m_sessionTitle = nullptr;
    QLabel* m_sessionDetails = nullptr;
    QLabel* m_sessionPath = nullptr;
    QLabel* m_sessionHint = nullptr;
};

} // namespace pvf
