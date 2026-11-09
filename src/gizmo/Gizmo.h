#pragma once

// 2D gizmo math for the new entity model.
//
// Coordinate spaces
// -----------------
// "Local" is the entity's untransformed content space: Bounds, pivot and the
// mouse passed to cornerScale() all live here. "World" (also called canvas or
// screen in the API) is the space localToWorld() returns and the space the
// viewport draws in. viewScale is pixels per local unit (the viewport zoom), so
// a handle that should stay 10 px wide is 10 / viewScale local units.
//
// The transform is deliberately the Mine-imator "pivot outside the local
// matrix" form:
//
//     localToWorld(p) = T(x, y) * T(pivotX, pivotY) * R(rotation)
//                       * S(scaleX, scaleY) * T(-pivotX, -pivotY) * p
//                     = (x, y) + pivot + R * (S * (p - pivot))
//
// Consequences used by the rest of this file:
//   * F(pivot) == (x, y) + pivot, so the pivot is the fixed point of BOTH
//     rotation and scaling. Scaling anchors the pivot, rotation turns around
//     it. That is the "pivot on the left edge anchors the left edge" rule.
//   * F(p) - F(q) == R * S * (p - q) for any p and q. Only the linear part
//     matters for differences; x/y/pivot cancel out. Office corner scaling
//     uses this to keep the opposite corner fixed by changing scale and
//     compensating x/y.
//
// Rotation is in degrees with Qt's convention: a positive angle maps local +X
// towards local +Y. Qt's Y axis grows downwards, so on screen that looks
// clockwise.
//
// The axis-aligned Bounds are the entity's local content box. Corner handles
// are derived from it; the move axes and the center scale handle are anchored
// at the pivot. The entity's own scale stretches the box, but gizmo handles
// keep a fixed pixel size. centerScale() is the one exception to the "local"
// rule above: it receives pixel-scaled local axes coordinates (see
// worldToLocalAxesPixels()) because the center handle has a fixed pixel offset.

#include <QPointF>
#include <QTransform>

#include <optional>

namespace pvf::gizmo {

struct Transform
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
};

struct Bounds
{
    double left = -50.0;
    double right = 50.0;
    double top = -50.0;
    double bottom = 50.0;
};

enum class Handle
{
    MoveX,
    MoveY,
    RotateTopLeft,
    RotateTopRight,
    RotateBottomRight,
    RotateBottomLeft,
    ScaleTopLeft,
    ScaleTopRight,
    ScaleBottomRight,
    ScaleBottomLeft,
    CenterScale
};

// Handle geometry in pixels (converted with viewScale).
inline constexpr double kHitRadius = 7.0;
inline constexpr double kScaleHandleInset = 10.0;
inline constexpr double kRotateHandleOffset = 16.0;
inline constexpr double kMoveAxisLength = 48.0;
inline constexpr double kCenterScaleOffset = 28.0;
inline constexpr double kMinScale = 1e-4;

QTransform localMatrix(const Transform& transform);
QPointF localToWorld(const Transform& transform, QPointF local);
QPointF worldToLocal(const Transform& transform, QPointF world);

// World point of the pivot. Equal to (x + pivotX, y + pivotY).
QPointF worldPivot(const Transform& transform);

// Undoes translation and rotation but NOT scale:
//     pivot + R^-1 * (world - worldPivot)
// This is the "local axes" space used by centerScale(): +X/+Y are the
// entity's local axes, and a point's sign directly describes the quadrant.
QPointF worldToLocalAxes(const Transform& transform, QPointF world);

// centerScale() works in pixels so the center handle can stay a fixed size on
// screen. This helper is worldToLocalAxes() with the pivot-relative offset
// multiplied by viewScale; pass its result straight to centerScale().
QPointF worldToLocalAxesPixels(const Transform& transform, QPointF world, double viewScale);

QPointF boundsCenter(const Bounds& bounds);

// All positions are canvas coordinates. Corner handles sit 10 px inside the
// transformed corner; rotate handles sit 16 px outside it; move axis tips are
// 48 px from the pivot. The center scale handle sits kCenterScaleOffset px
// along the rotated +X/+Y diagonal. Pixel distances are divided by viewScale
// to convert them to canvas units.
QPointF handlePosition(Handle handle, const Transform& transform, const Bounds& bounds,
                       double viewScale);

// screenPos is in the same canvas space as handlePosition().
std::optional<Handle> hitTest(QPointF screenPos, const Transform& transform, const Bounds& bounds,
                              double viewScale);

struct DragResult
{
    Transform transform;
    // True when the resulting scale on that axis is negative (the entity is
    // flipped on that axis), not merely that this drag crossed the pivot.
    bool flippedX = false;
    bool flippedY = false;
};

// Office corner scaling. The opposite corner stays fixed in world space and
// the dragged corner follows the mouse exactly when the drag is not uniform.
//
// mouseLocal is the position the dragged corner should move to, in the local
// content space of the transform passed in. worldToLocal(transform,
// mouseWorld) is the raw cursor; when the scale handle is drawn inset, add the
// local grab offset (cursorAtPress - corner) so the box does not jump on press.
//
// Callers normally keep the drag-start transform and compute mouseLocal with
// that same snapshot for the whole drag. Passing each returned transform and
// recomputing mouseLocal with it also works; do not pass a new transform while
// reusing a mouseLocal derived from the old one.
//
// With uniform=true the same factor is applied to scaleX and scaleY, so their
// ratio (and the visual aspect) is preserved. allowFlip=false clamps both
// scales to at least kMinScale; true lets a drag past the opposite corner
// produce negative scale.
DragResult cornerScale(const Transform& transform, const Bounds& bounds, Handle handle,
                       QPointF mouseLocal, bool uniform, bool allowFlip);

// Center scaling around the pivot. mouseLocal is in the pixel-scaled local
// axes space returned by worldToLocalAxesPixels(), so it is not divided by
// scale and its sign directly describes the quadrant:
//     pivot + R^-1 * (mouseWorld - worldPivot) * viewScale
// The drag is neutral when mouseLocal is at the center handle, i.e. pivot +
// (kCenterScaleOffset, kCenterScaleOffset). Dragging into Q2 flips only X, Q3
// flips both, Q4 flips only Y. With uniform=true both scales change by the
// same factor and keep their sign pattern. x/y/rotation/pivot are left
// untouched, so the pivot stays fixed.
DragResult centerScale(const Transform& transform, QPointF mouseLocal, bool uniform);

// rotation += angleDelta. With aroundBoundingBoxCenter=false (the default) the
// pivot stays fixed and x/y are unchanged. With true, x/y are compensated so
// the local origin (which is the bounds center for the default symmetric
// Bounds) stays fixed in world space; use the Bounds overload when the box is
// not centered on the origin.
DragResult rotate(const Transform& transform, double angleDelta,
                  bool aroundBoundingBoxCenter);
DragResult rotate(const Transform& transform, const Bounds& bounds, double angleDelta,
                  bool aroundBoundingBoxCenter);

} // namespace pvf::gizmo
