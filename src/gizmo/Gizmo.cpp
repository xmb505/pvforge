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

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double pointSegmentDistance(QPointF point, QPointF start, QPointF end)
{
    const QPointF segment = end - start;
    const double lengthSquared = segment.x() * segment.x() + segment.y() * segment.y();
    if (lengthSquared < 1e-18)
        return distance(point, start);

    const QPointF offset = point - start;
    const double t = std::clamp((offset.x() * segment.x() + offset.y() * segment.y())
                                    / lengthSquared,
                                0.0, 1.0);
    return distance(point, start + segment * t);
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

} // namespace

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

QPointF worldToLocalAxes(const Transform& transform, QPointF world)
{
    return QPointF(transform.pivotX, transform.pivotY)
           + rotateVector(world - worldPivot(transform), -transform.rotation);
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
        return cornerHandlePosition(transform, bounds, handle, kRotateHandleOffset, true, scale);
    return pivot;
}

std::optional<Handle> hitTest(QPointF screenPos, const Transform& transform, const Bounds& bounds,
                              double viewScale)
{
    const double scale = viewScale > 0.0 ? viewScale : 1.0;
    const auto hits = [&](Handle handle) {
        return distance(screenPos, handlePosition(handle, transform, bounds, scale))
               <= kHitRadius;
    };

    // Scale handles sit inside the corners and win over the rotate handles
    // when a very small box makes them overlap.
    static constexpr Handle kScaleHandles[] = {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                               Handle::ScaleBottomRight,
                                               Handle::ScaleBottomLeft};
    for (Handle handle : kScaleHandles)
    {
        if (hits(handle))
            return handle;
    }

    if (hits(Handle::CenterScale))
        return Handle::CenterScale;

    static constexpr Handle kRotateHandles[] = {Handle::RotateTopLeft, Handle::RotateTopRight,
                                                Handle::RotateBottomRight,
                                                Handle::RotateBottomLeft};
    for (Handle handle : kRotateHandles)
    {
        if (hits(handle))
            return handle;
    }

    const QPointF pivot = worldPivot(transform);
    const auto hitsAxis = [&](QPointF direction) {
        const QPointF tip = pivot + direction * (kMoveAxisLength / scale);
        return pointSegmentDistance(screenPos, pivot, tip) <= kHitRadius;
    };
    if (hitsAxis(rotateVector(QPointF(1.0, 0.0), transform.rotation)))
        return Handle::MoveX;
    if (hitsAxis(rotateVector(QPointF(0.0, 1.0), transform.rotation)))
        return Handle::MoveY;

    return std::nullopt;
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
    DragResult result;
    result.transform = transform;
    result.transform.rotation = transform.rotation + angleDelta;
    result.flippedX = transform.scaleX < 0.0;
    result.flippedY = transform.scaleY < 0.0;

    if (!aroundBoundingBoxCenter)
        return result;

    // Keep F(boundsCenter) fixed while R changes:
    //   x' = x + R_old * S * (center - pivot) - R_new * S * (center - pivot)
    const QPointF pivot(transform.pivotX, transform.pivotY);
    const QPointF offset = boundsCenter(bounds) - pivot;
    const QPointF scaled(offset.x() * transform.scaleX, offset.y() * transform.scaleY);
    const QPointF before = rotateVector(scaled, transform.rotation);
    const QPointF after = rotateVector(scaled, transform.rotation + angleDelta);

    result.transform.x = transform.x + before.x() - after.x();
    result.transform.y = transform.y + before.y() - after.y();
    return result;
}

} // namespace pvf::gizmo
