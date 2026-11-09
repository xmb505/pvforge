#pragma once

#include "common/Types.h"

#include <QHash>
#include <QMainWindow>

#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QSpinBox;

namespace pvf::mcp2 {
class HttpServer;
class Service;
}

namespace pvf::next {

class AssetPanel;
class ProjectDocument;
class PropertiesPanel;
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
    void undo();
    void redo();

private:
    void buildUi();
    void buildEditor();
    void buildWelcome();
    void connectDocument();
    void updateActions();
    void updateStatus();
    void rebuildCameras();
    void startMcpServer();
    void stopMcpServer();
    void startExport(const Uuid& cameraUuid, int startFrame, int endFrame,
                     const QString& outputPath, bool includeAudio);
    void closeOrphanedPrivateTimelines();
    void closeAllPrivateTimelines();
    void replaceCentralWidget(QWidget* widget);
    void showError(const QString& title, const QString& error);

    std::unique_ptr<ProjectDocument> m_ownedDocument;
    ProjectDocument* m_document = nullptr;
    std::unique_ptr<mcp2::Service> m_mcpService;
    std::unique_ptr<mcp2::HttpServer> m_mcpServer;
    QString m_mcpConfigPath;
    QString m_mcpStatus;

    AssetPanel* m_assetPanel = nullptr;
    ViewportWidget* m_viewport = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    TimelineWidget* m_timeline = nullptr;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_gizmoAction = nullptr;
    QAction* m_createEmptyAction = nullptr;
    QAction* m_createTextAction = nullptr;
    QAction* m_createVectorAction = nullptr;
    QAction* m_createCameraAction = nullptr;
    QAction* m_exportAction = nullptr;

    QSpinBox* m_playheadSpin = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QCheckBox* m_gizmoCheck = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_resolutionLabel = nullptr;

    QLabel* m_statusDir = nullptr;
    QLabel* m_statusFrame = nullptr;
    QLabel* m_statusFps = nullptr;
    QLabel* m_statusMcp = nullptr;

    QHash<Uuid, QDialog*> m_privateTimelines;

    QString m_lastBrowseDir;
};

} // namespace pvf::next
