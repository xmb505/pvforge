#include "Gizmo.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pvf::gizmo {
namespace {

QPointF rotateVector(QPointF vector, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return QPointF(vector.x() * cosine - vector.y() * sine,
                   vector.x() * sine + vector.y() * cosine);
}

QPointF localCorner(const Bounds& bounds, Handle handle)
{
    switch (handle)
    {
    case Handle::ScaleTopLeft:
    case Handle::RotateTopLeft:
        return QPointF(bounds.left, bounds.top);
    case Handle::ScaleTopRight:
    case Handle::RotateTopRight:
        return QPointF(bounds.right, bounds.top);
    case Handle::ScaleBottomRight:
    case Handle::RotateBottomRight:
        return QPointF(bounds.right, bounds.bottom);
    case Handle::ScaleBottomLeft:
    case Handle::RotateBottomLeft:
        return QPointF(bounds.left, bounds.bottom);
    default:
        return boundsCenter(bounds);
    }
}

QPointF oppositeCorner(const Bounds& bounds, Handle handle)
{
    switch (handle)
    {
    case Handle::ScaleTopLeft:
    case Handle::RotateTopLeft:
        return QPointF(bounds.right, bounds.bottom);
    case Handle::ScaleTopRight:
    case Handle::RotateTopRight:
        return QPointF(bounds.left, bounds.bottom);
    case Handle::ScaleBottomRight:
    case Handle::RotateBottomRight:
        return QPointF(bounds.left, bounds.top);
    case Handle::ScaleBottomLeft:
    case Handle::RotateBottomLeft:
        return QPointF(bounds.right, bounds.top);
    default:
        return boundsCenter(bounds);
    }
}

QPointF cornerHandlePosition(const Transform& transform, const Bounds& bounds, Handle handle,
                             double pixels, bool outside, double viewScale)
{
    const QPointF corner = localToWorld(transform, localCorner(bounds, handle));
    QPointF inward = localToWorld(transform, boundsCenter(bounds)) - corner;
    double length = std::hypot(inward.x(), inward.y());
    if (length < 1e-9)
    {
        // Zero scale collapses every corner onto the pivot; fall back to the
        // rotated local diagonal so the handles keep their quadrants.
        inward = rotateVector(localCorner(bounds, handle) - boundsCenter(bounds),
                              transform.rotation);
        length = std::hypot(inward.x(), inward.y());
    }
    if (length < 1e-9)
        return corner;

    inward /= length;
    const double offset = pixels / viewScale;
    return outside ? corner - inward * offset : corner + inward * offset;
}

bool isScaleHandle(Handle handle)
{
    switch (handle)
    {
    case Handle::ScaleTopLeft:
    case Handle::ScaleTopRight:
    case Handle::ScaleBottomRight:
    case Handle::ScaleBottomLeft:
        return true;
    default:
        return false;
    }
}

QTransform localMatrix(const Transform& transform)
{
    QTransform matrix;
    matrix.translate(transform.x, transform.y);
    matrix.translate(transform.pivotX, transform.pivotY);
    matrix.rotate(transform.rotation);
    matrix.scale(transform.scaleX, transform.scaleY);
    matrix.translate(-transform.pivotX, -transform.pivotY);
    return matrix;
}

// Undoes translation and rotation but NOT scale:
//     pivot + R^-1 * (world - worldPivot)
// This is the "local axes" space used by centerScale(): +X/+Y are the
// entity's local axes, and a point's sign directly describes the quadrant.
QPointF worldToLocalAxes(const Transform& transform, QPointF world)
{
    return QPointF(transform.pivotX, transform.pivotY)
           + rotateVector(world - worldPivot(transform), -transform.rotation);
}

} // namespace

bool isRotateHandle(Handle handle)
{
    switch (handle)
    {
    case Handle::RotateTopLeft:
    case Handle::RotateTopRight:
    case Handle::RotateBottomRight:
    case Handle::RotateBottomLeft:
        return true;
    default:
        return false;
    }
}

Handle rotateHandleForVisualCorner(int visualCorner)
{
    switch (((visualCorner % 4) + 4) % 4)
    {
    case 0:
        return Handle::RotateTopLeft;
    case 1:
        return Handle::RotateTopRight;
    case 2:
        return Handle::RotateBottomRight;
    default:
        return Handle::RotateBottomLeft;
    }
}

int visualCornerForRotateHandle(Handle handle)
{
    switch (handle)
    {
    case Handle::RotateTopRight:
        return 1;
    case Handle::RotateBottomRight:
        return 2;
    case Handle::RotateBottomLeft:
        return 3;
    default:
        return 0;
    }
}

int localCornerForVisualCorner(int visualCorner, bool flippedX, bool flippedY)
{
    // Corner order is TL, TR, BR, BL.  A negative X scale swaps left/right
    // (XOR 1); a negative Y scale swaps top/bottom, which in this order swaps
    // both index bits (XOR 3).
    int local = ((visualCorner % 4) + 4) % 4;
    if (flippedX)
        local ^= 1;
    if (flippedY)
        local ^= 3;
    return local;
}

QPointF localToWorld(const Transform& transform, QPointF local)
{
    return localMatrix(transform).map(local);
}

QPointF worldToLocal(const Transform& transform, QPointF world)
{
    bool invertible = false;
    const QTransform inverse = localMatrix(transform).inverted(&invertible);
    return invertible ? inverse.map(world) : world;
}

QPointF worldPivot(const Transform& transform)
{
    return QPointF(transform.x + transform.pivotX, transform.y + transform.pivotY);
}

QPointF worldToLocalAxesPixels(const Transform& transform, QPointF world, double viewScale)
{
    const double scale = viewScale > 0.0 ? viewScale : 1.0;
    const QPointF pivot(transform.pivotX, transform.pivotY);
    return pivot + (worldToLocalAxes(transform, world) - pivot) * scale;
}

QPointF boundsCenter(const Bounds& bounds)
{
    return QPointF((bounds.left + bounds.right) / 2.0, (bounds.top + bounds.bottom) / 2.0);
}

QPointF handlePosition(Handle handle, const Transform& transform, const Bounds& bounds,
                       double viewScale)
{
    const double scale = viewScale > 0.0 ? viewScale : 1.0;
    const QPointF pivot = worldPivot(transform);

    switch (handle)
    {
    case Handle::MoveX:
        return pivot + rotateVector(QPointF(kMoveAxisLength / scale, 0.0), transform.rotation);
    case Handle::MoveY:
        return pivot + rotateVector(QPointF(0.0, kMoveAxisLength / scale), transform.rotation);
    case Handle::CenterScale:
        return pivot
               + rotateVector(QPointF(kCenterScaleOffset / scale, kCenterScaleOffset / scale),
                              transform.rotation);
    default:
        break;
    }

    if (isScaleHandle(handle))
        return cornerHandlePosition(transform, bounds, handle, kScaleHandleInset, false, scale);
    if (isRotateHandle(handle))
        return cornerHandlePosition(transform, bounds, handle, kRotateHandleInset, false, scale);
    return pivot;
}

DragResult cornerScale(const Transform& transform, const Bounds& bounds, Handle handle,
                       QPointF mouseLocal, bool uniform, bool allowFlip)
{
    DragResult result;
    result.transform = transform;
    result.flippedX = transform.scaleX < 0.0;
    result.flippedY = transform.scaleY < 0.0;

    if (!isScaleHandle(handle))
        return result;

    const QPointF dragged = localCorner(bounds, handle);
    const QPointF fixed = oppositeCorner(bounds, handle);
    const double spanX = dragged.x() - fixed.x();
    const double spanY = dragged.y() - fixed.y();
    if (std::abs(spanX) < 1e-12 || std::abs(spanY) < 1e-12)
        return result;

    const QPointF delta = mouseLocal - fixed;
    const double factorX = delta.x() / spanX;
    const double factorY = delta.y() / spanY;

    double scaleX = transform.scaleX;
    double scaleY = transform.scaleY;
    if (uniform)
    {
        // Least-squares uniform factor: project the mouse delta onto the
        // original diagonal, which is the closest the locked aspect ratio can
        // get to following the cursor.
        const double diagonalSquared = spanX * spanX + spanY * spanY;
        const double factor = (delta.x() * spanX + delta.y() * spanY) / diagonalSquared;
        scaleX *= factor;
        scaleY *= factor;
    }
    else
    {
        scaleX *= factorX;
        scaleY *= factorY;
    }

    if (!allowFlip)
    {
        scaleX = std::max(scaleX, kMinScale);
        scaleY = std::max(scaleY, kMinScale);
    }

    // F(fixed) must not move: F(fixed) = (x, y) + pivot + R * S * (fixed -
    // pivot). Rotation and pivot stay untouched, so only x/y need to absorb
    // the new scale term.
    const QPointF fixedWorld = localToWorld(transform, fixed);
    const QPointF pivot(transform.pivotX, transform.pivotY);
    const QPointF pivotOffset = fixed - pivot;
    const QPointF scaled(pivotOffset.x() * scaleX, pivotOffset.y() * scaleY);
    const QPointF rotated = rotateVector(scaled, transform.rotation);

    result.transform.scaleX = scaleX;
    result.transform.scaleY = scaleY;
    result.transform.x = fixedWorld.x() - transform.pivotX - rotated.x();
    result.transform.y = fixedWorld.y() - transform.pivotY - rotated.y();
    result.flippedX = scaleX < 0.0;
    result.flippedY = scaleY < 0.0;
    return result;
}

DragResult centerScale(const Transform& transform, QPointF mouseLocal, bool uniform)
{
    DragResult result;
    result.transform = transform;

    // mouseLocal is in pixel-scaled axes space, so these are the signed pixel
    // offsets that decide the quadrant and the scale factor.
    const double offsetX = mouseLocal.x() - transform.pivotX;
    const double offsetY = mouseLocal.y() - transform.pivotY;
    const double factorX = offsetX / kCenterScaleOffset;
    const double factorY = offsetY / kCenterScaleOffset;

    if (uniform)
    {
        // One factor for both axes preserves |scaleX| : |scaleY|. The sign of
        // each raw factor decides the axis direction, so grabbing the handle
        // (factorX == factorY == 1) keeps an already flipped entity flipped.
        const double factor = std::max(std::abs(factorX), std::abs(factorY));
        result.transform.scaleX = transform.scaleX * (factorX < 0.0 ? -factor : factor);
        result.transform.scaleY = transform.scaleY * (factorY < 0.0 ? -factor : factor);
    }
    else
    {
        result.transform.scaleX = transform.scaleX * factorX;
        result.transform.scaleY = transform.scaleY * factorY;
    }

    result.flippedX = result.transform.scaleX < 0.0;
    result.flippedY = result.transform.scaleY < 0.0;
    return result;
}

DragResult rotate(const Transform& transform, double angleDelta,
                  bool aroundBoundingBoxCenter)
{
    // The three-argument form assumes the default symmetric Bounds, whose
    // center is the local origin.
    return rotate(transform, Bounds{}, angleDelta, aroundBoundingBoxCenter);
}

DragResult rotate(const Transform& transform, const Bounds& bounds, double angleDelta,
                  bool aroundBoundingBoxCenter)
{
    if (!aroundBoundingBoxCenter)
    {
        DragResult result;
        result.transform = transform;
        result.transform.rotation = transform.rotation + angleDelta;
        result.flippedX = transform.scaleX < 0.0;
        result.flippedY = transform.scaleY < 0.0;
        return result;
    }

    return rotateAround(transform, localToWorld(transform, boundsCenter(bounds)), angleDelta);
}

DragResult rotateAround(const Transform& transform, QPointF worldPoint, double angleDelta)
{
    DragResult result;
    result.transform = transform;
    result.transform.rotation = transform.rotation + angleDelta;
    result.flippedX = transform.scaleX < 0.0;
    result.flippedY = transform.scaleY < 0.0;

    // Keep F(worldPoint) fixed while R changes:
    //   x' = x + R_old * S * (local - pivot) - R_new * S * (local - pivot)
    const QPointF local = worldToLocal(transform, worldPoint);
    const QPointF pivot(transform.pivotX, transform.pivotY);
    const QPointF offset = local - pivot;
    const QPointF scaled(offset.x() * transform.scaleX, offset.y() * transform.scaleY);
    const QPointF before = rotateVector(scaled, transform.rotation);
    const QPointF after = rotateVector(scaled, transform.rotation + angleDelta);

    result.transform.x = transform.x + before.x() - after.x();
    result.transform.y = transform.y + before.y() - after.y();
    return result;
}

} // namespace pvf::gizmo
