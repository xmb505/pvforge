#pragma once

#include "core/History.h"
#include "core/Project.h"

#include <QElapsedTimer>
#include <QMainWindow>

class QAction;
class QCloseEvent;
class QKeyEvent;
class QLabel;
class QScrollArea;
class QTabWidget;
class QTimer;

namespace pvf {

class AssetLibraryPanel;
class EnvironmentPanel;
class ProjectPropertiesPanel;
class PropertiesPanel;
class TimelinePanel;
class Viewport;
class WorkbenchPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Project* project, QWidget* parent = nullptr);

    Project* project() const { return m_project; }
    EntityId importImageFile(const QString& path);
    void goToFrame(int frame) { setFrame(frame); }
    int currentFrame() const { return m_frame; }
    void togglePlayback();
    void setSelectedEntityForTest(EntityId id) { setSelectedEntity(id); }
    void applyPivotForTest(EntityId id, double ax, double ay) { applyPivot(id, ax, ay); }
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
    void applyPivot(EntityId id, double ax, double ay);
    void refreshAll();
    void onPlayTick();
    void updateStatus();
    void toggleWorkbench();
    void deleteSelectedEntity();
    void onUndo();
    void onRedo();
    void onEntityCreated(EntityId id);
    void onEntityFlagChanged(EntityId id, EntityFlag flag, bool value);
    void onDeleteKeyframesRequested();
    void updateWindowTitle();
    bool saveProject();
    bool saveProjectAs();
    void onEntityRenamed(EntityId id, const QString& name);
    void packAllAssets();

    Project* m_project = nullptr;
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
    WorkbenchPanel* m_workbench = nullptr;

    QTimer* m_playTimer = nullptr;
    QElapsedTimer m_playClock;
    QLabel* m_status = nullptr;

    QAction* m_playAction = nullptr;
    QAction* m_autoKeyAction = nullptr;
    QAction* m_packAssetsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_homeAction = nullptr;
    QAction* m_endAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_quitAction = nullptr;

    int m_frame = 0;
    int m_playStart = 0;
    bool m_playing = false;
    EntityId m_selected = kInvalidEntity;
};

} // namespace pvf
