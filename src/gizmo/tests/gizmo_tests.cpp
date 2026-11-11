#include "gizmo/Gizmo.h"

#include <QCoreApplication>
#include <QDebug>

#include <array>
#include <cmath>

using namespace pvf;
using namespace pvf::gizmo;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const QString& detail, const char* file,
           int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    qCritical().noquote() << QStringLiteral("FAIL %1:%2  %3  %4")
                                 .arg(QString::fromLatin1(file))
                                 .arg(line)
                                 .arg(QString::fromLatin1(expression), detail);
}

#define CHECK(condition) check((condition), #condition, QString(), __FILE__, __LINE__)
#define CHECK_DETAIL(condition, detail) check((condition), #condition, (detail), __FILE__, __LINE__)

constexpr double kEps = 1e-6;

bool nearly(double a, double b, double epsilon = 1e-9)
{
    return std::abs(a - b) <= epsilon;
}

bool nearlyPoint(QPointF a, QPointF b, double epsilon = kEps)
{
    return nearly(a.x(), b.x(), epsilon) && nearly(a.y(), b.y(), epsilon);
}

QPointF rotated(QPointF vector, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return QPointF(vector.x() * cosine - vector.y() * sine,
                   vector.x() * sine + vector.y() * cosine);
}

Transform sampleTransform()
{
    Transform transform;
    transform.x = 75.0;
    transform.y = -30.0;
    transform.rotation = 28.0;
    transform.scaleX = 1.6;
    transform.scaleY = 0.75;
    transform.pivotX = 11.0;
    transform.pivotY = -7.0;
    return transform;
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

QPointF localOppositeCorner(const Bounds& bounds, Handle handle)
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

void testLocalMatrix()
{
    const Transform transform = sampleTransform();

    const QPointF samples[] = {QPointF(0, 0), QPointF(13, -9), QPointF(-80, 45),
                               QPointF(120, 30)};
    for (QPointF point : samples)
    {
        const QPointF pivot(transform.pivotX, transform.pivotY);
        const QPointF scaled((point.x() - pivot.x()) * transform.scaleX,
                             (point.y() - pivot.y()) * transform.scaleY);
        const QPointF expected(transform.x + pivot.x(), transform.y + pivot.y());
        CHECK(nearlyPoint(localToWorld(transform, point),
                          expected + rotated(scaled, transform.rotation)));
        CHECK(nearlyPoint(worldToLocal(transform, localToWorld(transform, point)), point));
    }

    CHECK(nearlyPoint(localToWorld(transform, QPointF(transform.pivotX, transform.pivotY)),
                      worldPivot(transform)));
    CHECK(nearlyPoint(worldPivot(transform),
                      QPointF(transform.x + transform.pivotX,
                              transform.y + transform.pivotY)));

    // F(p) - F(q) must equal R * S * (p - q); Office corner scaling relies on
    // this because x/y/pivot cancel out.
    const QPointF p(30, -12);
    const QPointF q(-45, 60);
    const QPointF difference = localToWorld(transform, p) - localToWorld(transform, q);
    const QPointF localDifference((p.x() - q.x()) * transform.scaleX,
                                  (p.y() - q.y()) * transform.scaleY);
    CHECK(nearlyPoint(difference, rotated(localDifference, transform.rotation)));

    CHECK(nearlyPoint(worldToLocalAxes(transform,
                                       worldPivot(transform)
                                           + rotated(QPointF(17, -23), transform.rotation)),
                      QPointF(transform.pivotX + 17, transform.pivotY - 23)));
    CHECK(nearlyPoint(worldToLocalAxesPixels(transform,
                                             worldPivot(transform)
                                                 + rotated(QPointF(17, -23),
                                                           transform.rotation),
                                             2.5),
                      QPointF(transform.pivotX + 42.5, transform.pivotY - 57.5)));
}

void testHandleRotation()
{
    const Bounds bounds{-120, 70, 85, -50}; // left, right, top, bottom (Y-up)
    Transform flat = sampleTransform();
    flat.rotation = 0.0;
    const double viewScale = 1.5;

    static constexpr Handle kHandles[] = {
        Handle::MoveX,           Handle::MoveY,
        Handle::RotateTopLeft,   Handle::RotateTopRight,
        Handle::RotateBottomRight, Handle::RotateBottomLeft,
        Handle::ScaleTopLeft,    Handle::ScaleTopRight,
        Handle::ScaleBottomRight, Handle::ScaleBottomLeft,
        Handle::CenterScale};

    for (double rotation : {17.0, 63.0, 90.0, -135.0})
    {
        Transform turned = flat;
        turned.rotation = rotation;
        for (Handle handle : kHandles)
        {
            const QPointF before = handlePosition(handle, flat, bounds, viewScale);
            const QPointF after = handlePosition(handle, turned, bounds, viewScale);
            CHECK_DETAIL(nearlyPoint(after - worldPivot(turned),
                                     rotated(before - worldPivot(flat), rotation)),
                         QStringLiteral("handle %1 rotation %2")
                             .arg(int(handle))
                             .arg(rotation));
        }
    }

    // The move axes are screen-length arrows along the rotated local axes.
    Transform turned = flat;
    turned.rotation = 90.0;
    CHECK(nearlyPoint(handlePosition(Handle::MoveX, turned, bounds, viewScale),
                      worldPivot(turned) + QPointF(0, kMoveAxisLength / viewScale)));
    CHECK(nearlyPoint(handlePosition(Handle::MoveY, turned, bounds, viewScale),
                      worldPivot(turned) + QPointF(-kMoveAxisLength / viewScale, 0)));

    // Corner handles stay a fixed pixel distance from the transformed corners.
    const QPointF corner = localToWorld(turned, localCorner(bounds, Handle::ScaleTopLeft));
    CHECK(nearly(std::hypot(handlePosition(Handle::ScaleTopLeft, turned, bounds, viewScale).x()
                                - corner.x(),
                            handlePosition(Handle::ScaleTopLeft, turned, bounds, viewScale).y()
                                - corner.y()),
                 kScaleHandleInset / viewScale, 1e-9));
    CHECK(nearly(std::hypot(handlePosition(Handle::RotateTopLeft, turned, bounds, viewScale).x()
                                - corner.x(),
                            handlePosition(Handle::RotateTopLeft, turned, bounds, viewScale).y()
                                - corner.y()),
                 kRotateHandleInset / viewScale, 1e-9));

    // The center handle is in the local +X/+Y quadrant (rotated with the
    // entity), at a fixed screen-pixel distance from the pivot.
    const QPointF expectedCenter = worldPivot(turned)
                                   + rotated(QPointF(kCenterScaleOffset / viewScale,
                                                     kCenterScaleOffset / viewScale),
                                             turned.rotation);
    CHECK(nearlyPoint(handlePosition(Handle::CenterScale, turned, bounds, viewScale),
                      expectedCenter));
}

void testAxisDirectionsFollowRotation()
{
    const Bounds bounds;
    const double viewScale = 1.0;
    const double length = kMoveAxisLength;

    // Model space is Y-up, so +Y is up on screen after the viewport's single
    // Y flip.  Positive rotation is counter-clockwise.
    const struct
    {
        double rotation;
        QPointF xAxis;
        QPointF yAxis;
    } cases[] = {
        {0.0, QPointF(length, 0.0), QPointF(0.0, length)},
        {90.0, QPointF(0.0, length), QPointF(-length, 0.0)},
        {180.0, QPointF(-length, 0.0), QPointF(0.0, -length)},
        {-90.0, QPointF(0.0, -length), QPointF(length, 0.0)},
    };

    for (const auto& entry : cases)
    {
        Transform transform;
        transform.rotation = entry.rotation;
        const QPointF pivot = worldPivot(transform);
        CHECK_DETAIL(nearlyPoint(handlePosition(Handle::MoveX, transform, bounds,
                                                viewScale),
                                 pivot + entry.xAxis),
                     QStringLiteral("MoveX rotation %1").arg(entry.rotation));
        CHECK_DETAIL(nearlyPoint(handlePosition(Handle::MoveY, transform, bounds,
                                                viewScale),
                                 pivot + entry.yAxis),
                     QStringLiteral("MoveY rotation %1").arg(entry.rotation));
    }
}

void testCornerScaleKeepsOppositeCorner()
{
    const Transform transform = sampleTransform();
    const Bounds bounds{-90, 70, 60, -50};

    static constexpr Handle kHandles[] = {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                          Handle::ScaleBottomRight,
                                          Handle::ScaleBottomLeft};

    for (Handle handle : kHandles)
    {
        const QPointF dragged = localCorner(bounds, handle);
        const QPointF fixed = localOppositeCorner(bounds, handle);
        const QPointF fixedWorld = localToWorld(transform, fixed);
        const QPointF mouseLocal = dragged + QPointF(25, -18);

        const DragResult result = cornerScale(transform, bounds, handle, mouseLocal, false, true);

        CHECK_DETAIL(nearlyPoint(localToWorld(result.transform, fixed), fixedWorld),
                     QStringLiteral("handle %1").arg(int(handle)));
        CHECK_DETAIL(nearlyPoint(localToWorld(result.transform, dragged),
                                 localToWorld(transform, mouseLocal)),
                     QStringLiteral("handle %1").arg(int(handle)));
        CHECK(nearly(result.transform.rotation, transform.rotation));
        CHECK(nearly(result.transform.pivotX, transform.pivotX));
        CHECK(nearly(result.transform.pivotY, transform.pivotY));
        CHECK(!result.flippedX && !result.flippedY);
    }
}

void testCornerScaleFlip()
{
    const Bounds bounds{-50, 50, 50, -50}; // Y-up: top +50, bottom -50
    const Transform transform;

    // Drag the visual top-left corner (local (-50, +50)) past the
    // bottom-right one: both axes flip.
    const DragResult flipped =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, QPointF(70, -70), false, true);
    CHECK(nearly(flipped.transform.scaleX, -0.2));
    CHECK(nearly(flipped.transform.scaleY, -0.2));
    CHECK(flipped.flippedX);
    CHECK(flipped.flippedY);
    CHECK(nearlyPoint(localToWorld(flipped.transform, QPointF(50, -50)),
                      QPointF(50, -50)));
    CHECK(nearlyPoint(localToWorld(flipped.transform, QPointF(-50, 50)),
                      QPointF(70, -70)));

    // Crossing only horizontally flips X.
    const DragResult horizontal =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, QPointF(70, 50), false, true);
    CHECK(nearly(horizontal.transform.scaleX, -0.2));
    CHECK(nearly(horizontal.transform.scaleY, 1.0));
    CHECK(horizontal.flippedX);
    CHECK(!horizontal.flippedY);

    // allowFlip=false clamps instead of flipping.
    const DragResult clamped =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, QPointF(70, -70), false, false);
    CHECK(nearly(clamped.transform.scaleX, kMinScale));
    CHECK(nearly(clamped.transform.scaleY, kMinScale));
    CHECK(!clamped.flippedX);
    CHECK(!clamped.flippedY);
}

void testCornerScaleUniform()
{
    const Transform transform = sampleTransform();
    const Bounds bounds{-80, 60, 50, -40};
    const QPointF dragged = localCorner(bounds, Handle::ScaleTopLeft);
    const QPointF fixed = localOppositeCorner(bounds, Handle::ScaleTopLeft);
    const QPointF span = dragged - fixed;
    const double startRatio = transform.scaleX / transform.scaleY;

    // Raw axis factors differ (-1.5 and 2.5); the uniform projection is the
    // least-squares factor onto the diagonal.
    const QPointF mouse = fixed + QPointF(span.x() * -1.5, span.y() * 2.5);
    const double projected = (-1.5 * span.x() * span.x() + 2.5 * span.y() * span.y())
                             / (span.x() * span.x() + span.y() * span.y());
    const DragResult result =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, mouse, true, true);
    CHECK(nearly(result.transform.scaleX / result.transform.scaleY, startRatio, 1e-9));
    CHECK(nearly(result.transform.scaleX, transform.scaleX * projected, 1e-9));
    CHECK(nearly(result.transform.scaleY, transform.scaleY * projected, 1e-9));
    CHECK(nearlyPoint(localToWorld(result.transform, fixed), localToWorld(transform, fixed)));

    // Dragging along the diagonal makes the uniform corner follow exactly.
    const QPointF diagonalMouse = fixed + span * 2.0;
    const DragResult doubled =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, diagonalMouse, true, true);
    CHECK(nearly(doubled.transform.scaleX, transform.scaleX * 2.0, 1e-9));
    CHECK(nearly(doubled.transform.scaleY, transform.scaleY * 2.0, 1e-9));
    CHECK(nearlyPoint(localToWorld(doubled.transform, dragged),
                      localToWorld(transform, diagonalMouse)));

    // A negative uniform factor flips both axes.
    const QPointF crossed = fixed - span * 0.5;
    const DragResult flipped =
        cornerScale(transform, bounds, Handle::ScaleTopLeft, crossed, true, true);
    CHECK(flipped.flippedX);
    CHECK(flipped.flippedY);
}

void testCenterScaleQuadrants()
{
    Transform transform;
    transform.x = 40.0;
    transform.y = -25.0;
    transform.rotation = 37.0;
    transform.scaleX = 2.0;
    transform.scaleY = 3.0;
    transform.pivotX = 10.0;
    transform.pivotY = -20.0;

    const auto axesPoint = [&](double x, double y) {
        return QPointF(transform.pivotX + x, transform.pivotY + y);
    };
    const auto expectScale = [&](const DragResult& result, double scaleX, double scaleY,
                                 bool flippedX, bool flippedY, const char* label) {
        CHECK_DETAIL(nearly(result.transform.scaleX, scaleX, 1e-9), QString::fromLatin1(label));
        CHECK_DETAIL(nearly(result.transform.scaleY, scaleY, 1e-9), QString::fromLatin1(label));
        CHECK_DETAIL(result.flippedX == flippedX, QString::fromLatin1(label));
        CHECK_DETAIL(result.flippedY == flippedY, QString::fromLatin1(label));
        CHECK(nearly(result.transform.x, transform.x));
        CHECK(nearly(result.transform.y, transform.y));
        CHECK(nearly(result.transform.rotation, transform.rotation));
        CHECK(nearly(result.transform.pivotX, transform.pivotX));
        CHECK(nearly(result.transform.pivotY, transform.pivotY));
        CHECK(nearlyPoint(worldPivot(result.transform), worldPivot(transform)));
    };

    // Grabbing the handle is neutral.
    expectScale(centerScale(transform, axesPoint(kCenterScaleOffset, kCenterScaleOffset),
                            false),
                2.0, 3.0, false, false, "neutral");

    // Q1: both positive, scale doubles.
    expectScale(centerScale(transform, axesPoint(56, 56), false), 4.0, 6.0, false, false, "Q1");

    // Q2: only X flips.
    expectScale(centerScale(transform, axesPoint(-56, 56), false), -4.0, 6.0, true, false,
                "Q2");

    // Q3: both flip.
    expectScale(centerScale(transform, axesPoint(-56, -56), false), -4.0, -6.0, true, true,
                "Q3");

    // Q4: only Y flips.
    expectScale(centerScale(transform, axesPoint(56, -56), false), 4.0, -6.0, false, true,
                "Q4");

    // Crossing the pivot flips exactly at zero.
    expectScale(centerScale(transform, axesPoint(0, kCenterScaleOffset), false), 0.0, 3.0,
                false, false, "axis zero");

    // Shift keeps the scale ratio and uses the larger axis factor.
    expectScale(centerScale(transform, axesPoint(56, 42), true), 4.0, 6.0, false, false,
                "uniform Q1");
    expectScale(centerScale(transform, axesPoint(-56, 42), true), -4.0, 6.0, true, false,
                "uniform Q2");
    expectScale(centerScale(transform, axesPoint(-56, -42), true), -4.0, -6.0, true, true,
                "uniform Q3");
    expectScale(centerScale(transform, axesPoint(56, -42), true), 4.0, -6.0, false, true,
                "uniform Q4");

    // A flipped entity stays flipped when the handle is grabbed without moving.
    Transform flipped = transform;
    flipped.scaleX = -2.0;
    const DragResult grabbed =
        centerScale(flipped, axesPoint(kCenterScaleOffset, kCenterScaleOffset), true);
    CHECK(nearly(grabbed.transform.scaleX, -2.0, 1e-9));
    CHECK(nearly(grabbed.transform.scaleY, 3.0, 1e-9));
}

void testRotateAroundPivot()
{
    Transform transform = sampleTransform();
    const QPointF pivotBefore = worldPivot(transform);

    const DragResult result = rotate(transform, 45.0, false);
    CHECK(nearly(result.transform.rotation, 73.0, 1e-9));
    CHECK(nearly(result.transform.x, transform.x));
    CHECK(nearly(result.transform.y, transform.y));
    CHECK(nearly(result.transform.scaleX, transform.scaleX));
    CHECK(nearly(result.transform.scaleY, transform.scaleY));
    CHECK(nearlyPoint(worldPivot(result.transform), pivotBefore));

    // Every world vector turns by exactly 45 degrees around the pivot.
    const QPointF sample(32, -11);
    const QPointF before = localToWorld(transform, sample) - pivotBefore;
    const QPointF after = localToWorld(result.transform, sample) - pivotBefore;
    CHECK(nearlyPoint(after, rotated(before, 45.0)));
    CHECK(!result.flippedX && !result.flippedY);

    Transform flipped = transform;
    flipped.scaleX = -1.2;
    const DragResult flippedResult = rotate(flipped, 10.0, false);
    CHECK(flippedResult.flippedX);
    CHECK(!flippedResult.flippedY);
}

void testRotateAroundBoundsCenter()
{
    const Bounds centeredBounds{-50, 50, -50, 50};
    Transform transform = sampleTransform();
    const QPointF centerBefore = localToWorld(transform, boundsCenter(centeredBounds));
    const QPointF pivotBefore = worldPivot(transform);

    const DragResult result = rotate(transform, 90.0, true);
    CHECK(nearly(result.transform.rotation, transform.rotation + 90.0, 1e-9));
    CHECK(nearlyPoint(localToWorld(result.transform, boundsCenter(centeredBounds)),
                      centerBefore));
    CHECK(!nearlyPoint(worldPivot(result.transform), pivotBefore, 1e-3));

    // The explicit-bounds overload handles boxes that are not centered on the
    // local origin.
    const Bounds asymmetric{-100, 50, 80, -20};
    const QPointF asymmetricCenter = boundsCenter(asymmetric);
    const QPointF asymmetricBefore = localToWorld(transform, asymmetricCenter);
    const DragResult asymmetricResult = rotate(transform, asymmetric, 70.0, true);
    CHECK(nearlyPoint(localToWorld(asymmetricResult.transform, asymmetricCenter),
                      asymmetricBefore));

    // When the pivot already is the bounds center, rotating around the center
    // does not need any x/y compensation.
    Transform pivotAtCenter = transform;
    pivotAtCenter.pivotX = asymmetricCenter.x();
    pivotAtCenter.pivotY = asymmetricCenter.y();
    const DragResult pivotResult = rotate(pivotAtCenter, asymmetric, 33.0, true);
    CHECK(nearly(pivotResult.transform.x, pivotAtCenter.x));
    CHECK(nearly(pivotResult.transform.y, pivotAtCenter.y));

    // The three-argument form assumes the default Bounds, whose center is the
    // local origin; for that box it must match the explicit overload.
    const DragResult implicitResult = rotate(transform, 35.0, true);
    const DragResult explicitResult = rotate(transform, centeredBounds, 35.0, true);
    CHECK(nearly(implicitResult.transform.x, explicitResult.transform.x));
    CHECK(nearly(implicitResult.transform.y, explicitResult.transform.y));
    CHECK(nearly(implicitResult.transform.rotation, explicitResult.transform.rotation));
}

void testHitTest()
{
    const Bounds bounds{-100, 100, 60, -60}; // Y-up: top is +60
    Transform transform;
    const double viewScale = 1.0;

    static constexpr Handle kHandles[] = {
        Handle::ScaleTopLeft,     Handle::ScaleTopRight,
        Handle::ScaleBottomRight, Handle::ScaleBottomLeft,
        Handle::RotateTopLeft,    Handle::RotateTopRight,
        Handle::RotateBottomRight, Handle::RotateBottomLeft,
        Handle::CenterScale};

    for (Handle handle : kHandles)
    {
        const QPointF position = handlePosition(handle, transform, bounds, viewScale);
        const std::optional<Handle> hit = hitTest(position, transform, bounds, viewScale);
        CHECK_DETAIL(hit.has_value() && *hit == handle, QStringLiteral("handle %1").arg(int(handle)));
    }

    // Move axes are picked anywhere along their stem, not only at the tip.
    CHECK(hitTest(QPointF(kMoveAxisLength / 2.0, 0), transform, bounds, viewScale)
          == Handle::MoveX);
    CHECK(hitTest(QPointF(0, kMoveAxisLength / 2.0), transform, bounds, viewScale)
          == Handle::MoveY);
    CHECK(hitTest(QPointF(kMoveAxisLength - 1.0, 0), transform, bounds, viewScale)
          == Handle::MoveX);

    // Empty canvas and edge midpoints miss.
    CHECK(!hitTest(QPointF(400, 400), transform, bounds, viewScale).has_value());
    CHECK(!hitTest(QPointF(0, -60), transform, bounds, viewScale).has_value());
    CHECK(!hitTest(QPointF(20, 20), transform, bounds, viewScale).has_value());

    // Handles rotate with the entity, so the old screen position misses.
    Transform turned = transform;
    turned.rotation = 90.0;
    CHECK(hitTest(handlePosition(Handle::MoveX, turned, bounds, viewScale), turned, bounds,
                  viewScale)
          == Handle::MoveX);
    CHECK(!hitTest(QPointF(kMoveAxisLength, 0), turned, bounds, viewScale).has_value());
    for (Handle handle : kHandles)
    {
        const QPointF position = handlePosition(handle, turned, bounds, viewScale);
        const std::optional<Handle> hit = hitTest(position, turned, bounds, viewScale);
        CHECK_DETAIL(hit.has_value() && *hit == handle,
                     QStringLiteral("rotated handle %1").arg(int(handle)));
    }

    // Pixel offsets shrink with viewScale.
    const QPointF corner = localToWorld(transform, localCorner(bounds, Handle::ScaleTopLeft));
    const QPointF zoomed = handlePosition(Handle::ScaleTopLeft, transform, bounds, 2.0);
    CHECK(nearly(std::hypot(zoomed.x() - corner.x(), zoomed.y() - corner.y()),
                 kScaleHandleInset / 2.0, 1e-9));
}

void testCenterScaleHandlePosition()
{
    const Bounds bounds{-100, 100, 60, -60};
    Transform transform;
    transform.x = 12.0;
    transform.y = -8.0;
    transform.rotation = 45.0;
    transform.pivotX = 6.0;
    transform.pivotY = 3.0;

    const QPointF handle = handlePosition(Handle::CenterScale, transform, bounds, 1.0);
    CHECK(nearlyPoint(handle,
                      worldPivot(transform)
                          + rotated(QPointF(kCenterScaleOffset, kCenterScaleOffset), 45.0)));

    // The center handle keeps a fixed pixel distance; at 4x zoom its canvas
    // offset is a quarter of that.
    const QPointF zoomed = handlePosition(Handle::CenterScale, transform, bounds, 4.0);
    CHECK(nearly(std::hypot(zoomed.x() - worldPivot(transform).x(),
                            zoomed.y() - worldPivot(transform).y()),
                 std::sqrt(2.0) * kCenterScaleOffset / 4.0, 1e-9));

    // The handle is a drag reference: grabbing it is neutral at any zoom.
    for (double viewScale : {1.0, 4.0})
    {
        const QPointF handleAtZoom =
            handlePosition(Handle::CenterScale, transform, bounds, viewScale);
        const QPointF axes =
            worldToLocalAxesPixels(transform, handleAtZoom, viewScale);
        const DragResult result = centerScale(transform, axes, false);
        CHECK(nearly(result.transform.scaleX, transform.scaleX));
        CHECK(nearly(result.transform.scaleY, transform.scaleY));
        CHECK(hitTest(handleAtZoom, transform, bounds, viewScale) == Handle::CenterScale);
    }
}

void testRotateHandleRidesCorner()
{
    const Bounds bounds{-100, 100, 60, -60};
    Transform transform;
    transform.rotation = 23.0;

    // The icon centre sits kRotateHandleInset px inside the corner along the
    // inward diagonal, for every zoom.  The glyph box itself straddles the
    // corner, hence the "riding" look instead of the old outside offset.
    for (double viewScale : {0.5, 1.0, 3.0})
    {
        const QPointF pivot = worldPivot(transform);
        const auto centre = [&](Handle handle) {
            const QPointF corner = localToWorld(transform, localCorner(bounds, handle));
            QPointF inward = pivot - corner;
            const double length = std::hypot(inward.x(), inward.y());
            inward /= length;
            return corner + inward * (kRotateHandleInset / viewScale);
        };

        static constexpr Handle kRotateHandles[] = {
            Handle::RotateTopLeft, Handle::RotateTopRight,
            Handle::RotateBottomRight, Handle::RotateBottomLeft};
        for (Handle handle : kRotateHandles)
        {
            CHECK_DETAIL(nearlyPoint(handlePosition(handle, transform, bounds, viewScale),
                                     centre(handle)),
                         QStringLiteral("handle %1 scale %2")
                             .arg(int(handle))
                             .arg(viewScale));
        }
    }
}

void testRotateHandleSquareHit()
{
    const Bounds bounds{-100, 100, 60, -60};
    Transform transform;
    transform.x = 30.0;
    transform.y = -20.0;
    const double viewScale = 2.0;

    const QPointF centre =
        handlePosition(Handle::RotateTopRight, transform, bounds, viewScale);
    const double half = kRotateHitSize / 2.0 / viewScale;

    // Axis-aligned square in model units: the diagonal just inside the
    // half-size hits, one pixel outside misses.
    CHECK(hitTest(centre + QPointF(half * 0.98, half * 0.98), transform, bounds,
                  viewScale)
          == Handle::RotateTopRight);
    CHECK(!hitTest(centre + QPointF(half * 1.1, 0.0), transform, bounds, viewScale)
               .has_value());

    // The square does not care about the icon's own rotation.
    transform.rotation = 90.0;
    const QPointF turned =
        handlePosition(Handle::RotateTopRight, transform, bounds, viewScale);
    CHECK(hitTest(turned + QPointF(-half * 0.9, half * 0.9), transform, bounds,
                  viewScale)
          == Handle::RotateTopRight);
}

void testRotateAroundWorldPoint()
{
    Transform transform = sampleTransform();
    const QPointF origin = localToWorld(transform, QPointF(boundsCenter(Bounds{})));
    const QPointF before = localToWorld(transform, QPointF(60, -35));

    const DragResult result = rotateAround(transform, origin, 37.0);
    CHECK(nearly(result.transform.rotation, transform.rotation + 37.0, 1e-9));
    CHECK(nearlyPoint(localToWorld(result.transform, QPointF(60, -35)),
                      origin + rotated(before - origin, 37.0)));
    CHECK(nearlyPoint(localToWorld(result.transform, worldToLocal(transform, origin)),
                      origin));
    CHECK(result.flippedX == (transform.scaleX < 0.0));
    CHECK(result.flippedY == (transform.scaleY < 0.0));
}

void testVisualCornerFlipMapping()
{
    // Without flips the visual corners map 1:1 to local corners.
    for (int corner = 0; corner < 4; ++corner)
    {
        CHECK(localCornerForVisualCorner(corner, false, false) == corner);
        CHECK(visualCornerForRotateHandle(rotateHandleForVisualCorner(corner))
              == corner);
    }

    // A negative X scale swaps left/right, a negative Y scale swaps
    // top/bottom; both mirror the box by 180 degrees.  RotateTopLeft stays the
    // visual top-left handle.
    CHECK(localCornerForVisualCorner(0, true, false) == 1);
    CHECK(localCornerForVisualCorner(1, true, false) == 0);
    CHECK(localCornerForVisualCorner(0, false, true) == 3);
    CHECK(localCornerForVisualCorner(3, false, true) == 0);
    CHECK(localCornerForVisualCorner(0, true, true) == 2);
    CHECK(localCornerForVisualCorner(2, true, true) == 0);
    CHECK(rotateHandleForVisualCorner(0) == Handle::RotateTopLeft);
    CHECK(rotateHandleForVisualCorner(3) == Handle::RotateBottomLeft);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testLocalMatrix();
    testHandleRotation();
    testAxisDirectionsFollowRotation();
    testCornerScaleKeepsOppositeCorner();
    testCornerScaleFlip();
    testCornerScaleUniform();
    testCenterScaleQuadrants();
    testRotateAroundPivot();
    testRotateAroundBoundsCenter();
    testRotateAroundWorldPoint();
    testHitTest();
    testRotateHandleRidesCorner();
    testRotateHandleSquareHit();
    testVisualCornerFlipMapping();
    testCenterScaleHandlePosition();

    qInfo().noquote() << QStringLiteral("%1 checks, %2 failures").arg(gChecks).arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
