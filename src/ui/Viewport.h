#pragma once

#include "common/Types.h"
#include "core/Camera.h"
#include "core/Project.h"
#include "gizmo/Gizmo.h"
#include "model/Entity.h"

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QVector>
#include <QWidget>

#include <optional>

class QKeyEvent;
class QPainter;
class QToolButton;

namespace pvf {

class EditorSession;

class Viewport : public QWidget
{
    Q_OBJECT

public:
    // Legacy shell constructor (old core::Project data source).
    explicit Viewport(Project* project, QWidget* parent = nullptr);
    // Delivery shell constructor: draws the new entity model through render2
    // and the per-project MediaServices/WorkbenchServices.
    explicit Viewport(EditorSession* session, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);
    void resetView();

    // Drops the cached render2 frame and repaints (document/media changes).
    void refreshSession();

    // Renders the current frame at project resolution for tests/previews.
    QImage grabProjectFrame(const Uuid& cameraUuid = Uuid()) const;
    QPointF projectToWidgetForTest(const QPointF& pos) const;
    QImage sessionImageForTest() const { return m_sessionImage; }

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

    // Session gizmo: auto-key writes the current frame's keyframe in addition
    // to the static value (same rule as the new viewport).
    void setAutoKeyEnabled(bool on) { m_autoKeyEnabled = on; }
    bool autoKeyEnabled() const { return m_autoKeyEnabled; }

    // AI write lock: selection, panning, zooming and the camera monitor keep
    // working; gizmo/camera drags that would write are ignored.
    void setEditingEnabled(bool on) { m_editingEnabled = on; }
    bool editingEnabled() const { return m_editingEnabled; }

    // Session gizmo test hooks.
    gizmo::Transform gizmoTransformForTest(const Uuid& uuid) const;
    gizmo::Bounds gizmoBoundsForTest(const Uuid& uuid) const;
    QPointF gizmoHandleForTest(const Uuid& uuid, gizmo::Handle handle) const;
    QPointF sessionPivotForTest() const;
    double snapAngleForTest(double degrees, bool force) const;
    bool sessionDraggingForTest() const
    {
        return m_sessionDragKind != SessionDragKind::None;
    }
    Uuid sessionDragUuidForTest() const { return m_sessionDragUuid; }

signals:
    void entitySelected(EntityId id);
    // Session shell selection: UUIDs are the new model's entity identity.
    void uuidSelected(const Uuid& uuid);
    // A complete render2 frame reached the screen (playback watchdog input).
    void framePresented(int frame);
    void valueEdited(EntityId id, const QString& prop, double value);
    void positionEdited(EntityId id, double x, double y);
    void scaleEdited(EntityId id, double sx, double sy, double x, double y);
    void pivotEdited(EntityId id, double ax, double ay);
    void cameraEdited(EntityId id, double x, double y, double scale, double rotation);
    // The comic balloon spike was dragged to a new tip position.
    void comicTipEdited(EntityId id, double tipX, double tipY);
    void workbenchRequested();
    void viewChanged();
    // A session gizmo/camera commit failed; the shell shows the message.
    void sessionEditFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

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

    bool sessionMode() const { return m_session != nullptr; }
    QSize canvasSize() const;
    QRectF canvasRect() const;
    double viewScale() const;
    QTransform worldToScreenTransform() const;
    QPointF screenToCanvas(const QPointF& pos) const;

    // ---- session (new model) rendering ---------------------------------
    Uuid sessionCameraUuid() const;
    int sessionExpandPercent() const;
    QRectF sessionSceneRect() const;
    QTransform sessionWorldToScreen() const;
    QPointF sessionScreenToProject(const QPointF& pos) const;
    QImage renderSessionFrame(const QSize& size, const Uuid& cameraUuid,
                              int expandPercent, bool* mediaMissing) const;
    QImage ensureSessionImage();
    void invalidateSessionImage();
    void paintSession(QPainter& painter);
    void drawSessionSelection(QPainter& painter);
    const model::Entity* hitTestSession(const QPointF& pos) const;

    // ---- session gizmo (gizmo:: math) -----------------------------------
    enum class SessionDragKind
    {
        None,
        Move,
        MoveX,
        MoveY,
        Rotate,
        Scale,
        CenterScale,
        Pivot,
        CameraMove,
        CameraRotate,
        CameraScale
    };

    gizmo::Transform sessionGizmoTransform(const model::Entity& entity) const;
    gizmo::Bounds sessionGizmoBounds(const model::Entity& entity) const;
    QTransform sessionParentToWidget(const model::Entity& entity) const;
    double sessionViewScaleFor(const QTransform& parentToWidget) const;
    std::optional<gizmo::Handle> hitSessionGizmo(const QPointF& pos) const;
    void startSessionGizmoDrag(const Uuid& uuid, SessionDragKind kind,
                               gizmo::Handle handle, const QPointF& pos);
    void applySessionGizmoDrag(const QPointF& pos, Qt::KeyboardModifiers modifiers);
    void finishSessionGizmoDrag();
    void cancelSessionDrag();
    void writeSessionGizmoValue(model::Entity& entity, const QString& property,
                                double value);
    void drawSessionGizmo(QPainter& painter);
    void collectSessionSnapTargets(const Uuid& uuid);
    double snapSessionAxis(double value, bool xAxis);
    double snapSessionPixel(double value) const;
    double snapSessionAngle(double degrees, bool force) const;

    // ---- session camera viewfinder --------------------------------------
    QPolygonF sessionCameraPolygon() const;
    QPointF sessionCameraScreenCenter() const;
    QPointF sessionCameraRotationHandle() const;
    QRectF sessionCameraTitleRect() const;
    CameraPart sessionCameraHitTest(const QPointF& pos) const;
    bool handleSessionCameraPress(QMouseEvent* event);
    void applySessionCameraDrag(const QPointF& pos, Qt::KeyboardModifiers modifiers);
    void drawSessionCameraOverlay(QPainter& painter);

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
    EditorSession* m_session = nullptr;
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

    // Cached render2 image for the session shell.  Workbench lookups may
    // return a lower-resolution image; drawImage() scales it to the scene rect.
    mutable QImage m_sessionImage;
    bool m_sessionImageValid = false;
    bool m_sessionMediaMissing = false;
    QSize m_sessionImageSize;
    int m_sessionImageFrame = -1;
    Uuid m_sessionImageCamera;
    int m_sessionImageExpand = -1;

    // ---- session gizmo state --------------------------------------------
    bool m_autoKeyEnabled = true;
    bool m_editingEnabled = true;
    SessionDragKind m_sessionDragKind = SessionDragKind::None;
    Uuid m_sessionDragUuid;
    model::Entity m_sessionDragStart;
    gizmo::Handle m_sessionDragHandle = gizmo::Handle::MoveX;
    gizmo::Transform m_sessionDragTransform;
    gizmo::Bounds m_sessionDragBounds;
    QTransform m_sessionDragParentToWidget;
    double m_sessionDragViewScale = 1.0;
    QPointF m_sessionDragStartParent;
    QPointF m_sessionDragPivotStart;
    QPointF m_sessionDragCenterStart;
    double m_sessionDragAnglePivot = 0.0;
    double m_sessionDragAngleCenter = 0.0;
    QPointF m_sessionDragGrabOffset;
    bool m_sessionDragChanged = false;
    bool m_sessionDragSnapRoot = false;
    QRectF m_sessionDragStartWorldBounds;
    QVector<double> m_sessionSnapTargetsX;
    QVector<double> m_sessionSnapTargetsY;
    double m_sessionCameraStartAngle = 0.0;
};

} // namespace pvf
