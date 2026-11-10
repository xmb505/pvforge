#pragma once

#include "common/Types.h"
#include "gizmo/Gizmo.h"
#include "render2/Render.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTransform>
#include <QVector>
#include <QWidget>

#include <optional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace pvf::model {
struct AssetContent;
struct Entity;
}

namespace pvf::next {

class Document;

// Scene viewport for the new entity model.
//
// The project canvas is fitted into the widget and can be zoomed with the
// wheel and panned with the middle button or space + left button.  The scene
// is rendered through render2::renderFrame with a SceneWorld; when a camera
// UUID is set the rendered image is the camera view, otherwise it is the
// whole project canvas.
//
// Left click hit-tests visible scene shapes (back to front) and changes the
// selection.  The selected entity gets a 2D gizmo: local X/Y move arrows,
// Office corner scaling (opposite corner fixed), a center scale square, four
// outside rotation handles and a draggable pivot dot.  Every drag previews in
// place and commits exactly once on mouse release.
//
// Solver initial-velocity handles are intentionally left for a later pass.
// TODO(viewport): draw and drag solver segment initial-velocity handles once
// the solver segment UI exists; this first pass only exposes transform gizmos.
class ViewportWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ViewportWidget(Document* document, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setCameraUuid(const Uuid& uuid);
    Uuid cameraUuid() const { return m_cameraUuid; }

    // Optional decoded-frame sources for video and HTML assets.  An unset
    // provider leaves render2's placeholder visible.
    void setVideoFrameProvider(render2::FrameProvider provider);
    void setHtmlFrameProvider(render2::FrameProvider provider);

    void setShowGizmo(bool show);
    bool showGizmo() const { return m_showGizmo; }

    // Snapping is on by default and Alt turns it off for the current drag.
    // Move drags snap the canvas centre lines first, then other entities'
    // edges/midpoints, and fall back to the pixel grid.  Scale drags snap to
    // the pixel grid; rotation snaps to snapAngleDegrees() multiples when the
    // result is within 3 degrees.
    void setSnapEnabled(bool enabled);
    bool snapEnabled() const { return m_snapEnabled; }
    // Pixel grid step in project pixels for moves and scales.  <= 0 disables
    // the pixel grid.
    void setSnapPixels(int pixels);
    void setSnapAngleDegrees(double degrees);

    void refresh();

    // Renders the current project canvas/camera at the project resolution.
    // The widget background and gizmo are not included.
    QImage grabFrame() const;

    QPointF projectToWidget(QPointF projectPos) const;
    QPointF widgetToProject(QPointF widgetPos) const;

    // Zoom relative to the initial "fit canvas" scale; 1.0 means fitted.
    double zoom() const { return m_zoom; }

    QSize minimumSizeHint() const override { return QSize(320, 180); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    enum class DragKind
    {
        None,
        Pan,
        MoveX,
        MoveY,
        Rotate,
        Scale,
        CenterScale,
        Pivot
    };

    QRectF viewRect() const;
    QTransform projectToWidgetTransform() const;
    QTransform parentWorldTransform(const Uuid& uuid) const;
    QTransform parentToWidgetTransform(const Uuid& uuid) const;
    double viewScaleFor(const QTransform& parentToWidget) const;

    gizmo::Transform gizmoTransform(const model::Entity& entity) const;
    gizmo::Bounds gizmoBounds(const model::Entity& entity) const;
    QSizeF assetContentSize(const model::AssetContent& asset) const;

    Uuid entityAt(const QPointF& widgetPos) const;
    std::optional<gizmo::Handle> hitGizmoHandle(const QPointF& widgetPos,
                                                const model::Entity& entity,
                                                const QTransform& parentToWidget,
                                                double viewScale) const;

    QRectF entityWorldBounds(const model::Entity& entity,
                             const QTransform& worldTransform) const;
    void collectSnapTargets(const Uuid& draggedUuid);
    double snapMoveAxis(double value, bool xAxis) const;
    double snapPixel(double value) const;
    double snapAngle(double degrees, bool force) const;

    void startGizmoDrag(const Uuid& uuid, DragKind kind, gizmo::Handle handle,
                        const QPointF& widgetPos);
    void applyGizmoDrag(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers);
    void finishGizmoDrag();
    void cancelGizmoDrag();

    void drawSelection(QPainter& painter, const Uuid& uuid);
    void drawCameraFrame(QPainter& painter, const Uuid& uuid,
                         const QTransform& projectToWidget) const;

    void invalidateScene();
    QImage ensureSceneImage(const QSize& size);

    void updateHoverCursor(const QPointF& widgetPos);

    Document* m_document = nullptr;
    Uuid m_cameraUuid;
    bool m_showGizmo = true;

    bool m_snapEnabled = true;
    int m_snapPixels = 1;
    double m_snapAngleDegrees = 15.0;

    double m_zoom = 1.0;
    QPointF m_pan;

    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_panLast;

    DragKind m_dragKind = DragKind::None;
    Uuid m_dragEntity;
    gizmo::Handle m_dragHandle = gizmo::Handle::MoveX;
    gizmo::Transform m_dragStartTransform;
    gizmo::Bounds m_dragStartBounds;
    QTransform m_dragParentToWidget;
    double m_dragViewScale = 1.0;
    QPointF m_dragStartParent;
    QPointF m_dragPivotStart;
    QPointF m_dragCenterStart;
    double m_dragAnglePivot = 0.0;
    double m_dragAngleCenter = 0.0;
    QPointF m_dragGrabOffset;
    bool m_dragChanged = false;

    // Snapshot taken on press so every mouse move snaps against a stable
    // scene.  Only root entities get centre-line/entity snapping; parented
    // entities only use the pixel grid because their axes can be rotated by
    // the parent.
    bool m_dragSnapRoot = false;
    QRectF m_dragStartWorldBounds;
    QVector<double> m_snapTargetsX;
    QVector<double> m_snapTargetsY;

    render2::FrameProvider m_videoFrameProvider;
    render2::FrameProvider m_htmlFrameProvider;

    QImage m_sceneImage;
    QSize m_sceneSize;
    Uuid m_sceneCamera;
    int m_sceneFrame = -1;
    bool m_sceneValid = false;

    static constexpr double kCanvasMargin = 24.0;
    static constexpr double kPivotHitRadius = 7.0;
    static constexpr int kMaxRenderDimension = 4096;
};

} // namespace pvf::next
