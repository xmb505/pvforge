#pragma once

#include "core/Project.h"

#include <QElapsedTimer>
#include <QMainWindow>

class QAction;
class QLabel;
class QTimer;

namespace pvf {

class ObjectTreePanel;
class PropertiesPanel;
class TimelinePanel;
class Viewport;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    Project* project() const { return m_project; }
    EntityId importImageFile(const QString& path);
    void goToFrame(int frame) { setFrame(frame); }
    int currentFrame() const { return m_frame; }
    void togglePlayback();

private:
    void createActions();
    void createMenus();
    void createToolBar();

    void importImage();
    void setFrame(int frame);
    void setSelectedEntity(EntityId id);
    void applyValue(EntityId id, const QString& prop, double value);
    void refreshAll();
    void onPlayTick();
    void updateStatus();

    Project* m_project = nullptr;
    Viewport* m_viewport = nullptr;
    ObjectTreePanel* m_tree = nullptr;
    PropertiesPanel* m_props = nullptr;
    TimelinePanel* m_timeline = nullptr;

    QTimer* m_playTimer = nullptr;
    QElapsedTimer m_playClock;
    QLabel* m_status = nullptr;

    QAction* m_importAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_autoKeyAction = nullptr;
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
