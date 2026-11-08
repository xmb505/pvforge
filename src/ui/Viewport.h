#pragma once

#include "core/Camera.h"
#include "core/Project.h"

#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QWidget>

class QPainter;
class QToolButton;

namespace pvf {

class Viewport : public QWidget
{
    Q_OBJECT

public:
    explicit Viewport(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);
    void resetView();

    QToolButton* workbenchButton() const { return m_workbenchButton; }

    // Canvas pixels per screen pixel, used by the gizmo and tests.
    double currentScale() const { return viewScale(); }

    void setSnapPixels(int pixels) { m_snapPixels = pixels; }
    QTransform entityTransformForTest(EntityId id) const;
    int gizmoModeForTest() const { return int(m_gizmoMode); }
    QPointF entityPivotForTest(EntityId id) const;
    QPointF comicTipForTest(EntityId id) const;
    void setEdgeCenterSnap(bool on) { m_edgeCenterSnap = on; }

    QTransform worldToScreenForTest() const { return worldToScreenTransform(); }
    EntityId hitTestForTest(const QPointF& pos) const
    {
        const Entity* entity = hitTest(pos);
        return entity ? entity->id : kInvalidEntity;
    }
    QPolygonF cameraFrameForTest() const { return cameraScreenPolygon(); }
    void setCameraFrameVisible(bool on);
    QSize minimumSizeHint() const override { return {480, 270}; }

signals:
    void entitySelected(EntityId id);
    void valueEdited(EntityId id, const QString& prop, double value);
    void positionEdited(EntityId id, double x, double y);
    void scaleEdited(EntityId id, double sx, double sy, double x, double y);
    void pivotEdited(EntityId id, double ax, double ay);
    void cameraEdited(EntityId id, double x, double y, double scale, double rotation);
    // The comic balloon spike was dragged to a new tip position.
    void comicTipEdited(EntityId id, double tipX, double tipY);
    void workbenchRequested();
    void viewChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class GizmoMode
    {
        None,
        Move,
        AxisX,
        AxisY,
        Rotate,
        Scale,
        PivotFree,
        PivotX,
        PivotY,
        ComicTip
    };
    enum class Handle
    {
        None,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
        ComicTip
    };

    enum class CameraPart { None, Move, Corner0, Corner1, Corner2, Corner3, Rotate };

    QRectF canvasRect() const;
    double viewScale() const;
    QTransform worldToScreenTransform() const;
    QPointF screenToCanvas(const QPointF& pos) const;
    EntityId activeCameraEntity() const;
    Camera::State cameraState(const Entity& e) const;
    QPolygonF cameraScreenPolygon() const;
    QPointF cameraScreenCenter() const;
    QPointF cameraRotationHandle() const;
    QRectF cameraTitleRect() const;
    CameraPart cameraHitTest(const QPointF& pos) const;
    bool handleCameraPress(QMouseEvent* event);
    void applyCameraDrag(const QPointF& pos, Qt::KeyboardModifiers modifiers);
    QPointF snapCameraCenter(const Camera::State& start, const QPointF& center,
                             bool* snappedX, double* guideX, bool* snappedY,
                             double* guideY) const;
    void drawCameraOverlay(QPainter& painter);
    QRectF entityLocalBounds(const Entity& e) const;
    const Entity* hitTest(const QPointF& pos) const;
    void drawEntity(QPainter& painter, const Entity& e, const QRectF& canvas, double scale);
    QPointF entityScreenCenter(const Entity& e) const;
    QPointF entityPivotScreen(const Entity& e) const;
    QPointF comicTipScreenPos(const Entity& e) const;
    QPointF screenToEntityLocal(const Entity& e, const QPointF& screenPos) const;
    struct VisualBounds
    {
        double left = 0.0;
        double right = 0.0;
        double top = 0.0;
        double bottom = 0.0;
    };
    VisualBounds visualBoundsOf(const Entity& e, double posX, double posY) const;
    QTransform entityTransform(const Entity& e) const;
    QTransform entityRotationTransform(const Entity& e) const;
    bool gizmoHitTest(const QPointF& pos, GizmoMode* mode) const;
    void drawGizmo(QPainter& painter, const Entity& e);
    QPointF handleScreenPos(const Entity& e, Handle handle) const;
    Handle hitHandle(const QPointF& pos) const;
    void drawHandles(QPainter& painter, const Entity& e);
    void applyHandleDrag(const QPointF& mousePos, bool uniform, bool snap);

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    double m_zoom = 1.0;
    QPointF m_pan;

    bool m_dragging = false;
    QPointF m_dragLast;
    EntityId m_dragEntity = kInvalidEntity;
    bool m_showCameraFrame = true;

    GizmoMode m_gizmoMode = GizmoMode::None;
    QPointF m_gizmoCenter;             // screen position of the gizmo
    QPointF m_gizmoStartMouse;
    double m_gizmoStartAngle = 0.0;
    double m_gizmoStartRotation = 0.0;
    double m_gizmoStartX = 0.0;
    double m_gizmoStartY = 0.0;

    Handle m_activeHandle = Handle::None;
    double m_scaleStartX = 1.0;
    double m_scaleStartY = 1.0;
    double m_scaleStartPosX = 0.0;
    double m_scaleStartPosY = 0.0;
    int m_snapPixels = 6;
    bool m_edgeCenterSnap = true;
    bool m_showGuideX = false;
    bool m_showGuideY = false;
    double m_guideX = 0.0;
    double m_guideY = 0.0;

    bool m_panning = false;
    QPointF m_panLast;

    CameraPart m_cameraPart = CameraPart::None;
    Camera::State m_cameraStartState;
    QPointF m_cameraStartMouse;
    double m_cameraStartAngle = 0.0;

    QToolButton* m_workbenchButton = nullptr;
};

} // namespace pvf
