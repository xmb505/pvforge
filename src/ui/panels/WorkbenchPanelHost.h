#pragma once

#include <QList>
#include <QObject>
#include <QRect>

#include <functional>

class QEvent;
class QScreen;
class QTimer;
class QWidget;

namespace pvf {

class WorkbenchPanel;

// Owns the dock/floating state of one WorkbenchPanel.
//
// The panel instance never changes: docking reparents it under the viewport as
// a plain child, floating reparents it under the main window as Qt::Window.
// Geometry and mode live in QSettings (`panel/workbench/*`).  The host is not
// the panel's QObject parent and never deletes it.
class WorkbenchPanelHost : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        Docked,
        Floating
    };
    using AnchorProvider = std::function<QPoint()>;

    WorkbenchPanelHost(WorkbenchPanel* panel, QWidget* dockParent,
                       QWidget* windowParent, AnchorProvider anchor,
                       QObject* parent = nullptr);

    Mode mode() const { return m_mode; }
    bool isFloating() const { return m_mode == Mode::Floating; }
    bool isPanelVisible() const;
    WorkbenchPanel* panel() const { return m_panel; }

    void showPanel();
    void hidePanel();
    void toggle();
    void setFloating(bool floating);
    void toggleFloating();
    // Restores a panel that was visible when the application closed.  Deferred
    // so the main window has its final size before the geometry is clamped.
    void restoreOnStartup();

signals:
    void visibleChanged(bool visible);
    void floatingChanged(bool floating);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QRect dockGeometry() const;
    QRect defaultDockGeometry() const;
    QRect clampedWindowGeometry(const QRect& desired) const;
    QList<QRect> screenAreas() const;
    QRect primaryScreenArea() const;

    void applyDockParent();
    void applyFloatingWindow();
    void saveGeometryForMode();
    void persistMode();
    void persistVisible();
    void scheduleGeometrySave();
    void reclampFloating();
    void watchScreen(QScreen* screen);

    WorkbenchPanel* m_panel = nullptr;
    QWidget* m_dockParent = nullptr;
    QWidget* m_windowParent = nullptr;
    AnchorProvider m_anchor;
    QTimer* m_saveTimer = nullptr;
    Mode m_mode = Mode::Docked;
    bool m_switching = false;
    bool m_dockGeometryValid = false;
};

} // namespace pvf
