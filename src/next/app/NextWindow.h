#pragma once

#include <QMainWindow>

#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

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

    // Accessors used by the shell tests and by future automation.
    ViewportWidget* viewportForTest() const { return m_viewport; }
    TimelineWidget* timelineForTest() const { return m_timeline; }
    AssetPanel* assetPanelForTest() const { return m_assetPanel; }
    PropertiesPanel* propertiesPanelForTest() const { return m_propertiesPanel; }
    QSpinBox* playheadSpinBoxForTest() const { return m_playheadSpin; }
    QComboBox* cameraComboBoxForTest() const { return m_cameraCombo; }
    QAction* undoActionForTest() const { return m_undoAction; }
    QAction* redoActionForTest() const { return m_redoAction; }

private slots:
    void chooseNewProject();
    void chooseOpenProject();
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
    void replaceCentralWidget(QWidget* widget);
    void showError(const QString& title, const QString& error);

    std::unique_ptr<ProjectDocument> m_ownedDocument;
    ProjectDocument* m_document = nullptr;

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

    QSpinBox* m_playheadSpin = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QCheckBox* m_gizmoCheck = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_resolutionLabel = nullptr;

    QLabel* m_statusDir = nullptr;
    QLabel* m_statusFrame = nullptr;
    QLabel* m_statusFps = nullptr;

    QString m_lastBrowseDir;
};

} // namespace pvf::next
