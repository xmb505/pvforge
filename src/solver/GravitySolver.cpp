#include "SolverInternal.h"

#include <QMetaType>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pvf::solver::detail {
namespace {

struct Hit
{
    bool valid = false;
    QPointF normal;
    double depth = 0.0;
    QPointF point;
};

void appendUnique(QVector<QPointF> &points, const QPointF &point)
{
    for (const QPointF &existing : points)
    {
        if (std::hypot(existing.x() - point.x(), existing.y() - point.y()) < 1e-6)
            return;
    }
    points.append(point);
}

// Outline samples of the moving target.  A point target (empty shape) is
// represented by its origin; this keeps the solver usable for empty entities.
QVector<QPointF> outlinePoints(const QPainterPath &shape)
{
    QVector<QPointF> points;
    const QList<QPolygonF> polygons = shape.toSubpathPolygons();
    for (const QPolygonF &polygon : polygons)
    {
        if (polygon.size() < 2)
            continue;
        for (int i = 0; i < polygon.size(); ++i)
        {
            const QPointF a = polygon.at(i);
            const QPointF b = polygon.at((i + 1) % polygon.size());
            appendUnique(points, a);
            const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
            const int divisions = std::clamp(int(std::ceil(length / 8.0)), 1, 32);
            for (int k = 1; k <= divisions; ++k)
            {
                const double t = double(k) / double(divisions);
                appendUnique(points, a + (b - a) * t);
            }
        }
    }
    if (points.isEmpty())
        points.append(QPointF(0.0, 0.0));
    return points;
}

QPointF closestPointOnSegment(const QPointF &a, const QPointF &b, const QPointF &point)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared <= 1e-12)
        return a;
    double t = QPointF::dotProduct(point - a, ab) / lengthSquared;
    t = std::clamp(t, 0.0, 1.0);
    return a + ab * t;
}

// Nearest boundary point plus its outward normal.  The normal is resolved by
// probing which side of the boundary is inside the filled path, so a point that
// has sunk below a horizontal floor is still pushed up, not down.
bool nearestOnPath(const QPainterPath &path, const QPointF &point,
                   QPointF *nearest, QPointF *normal, double *distance)
{
    const QList<QPolygonF> polygons = path.toSubpathPolygons();
    double bestDistance = std::numeric_limits<double>::max();
    QPointF bestPoint;
    QPointF bestA;
    QPointF bestB;
    bool found = false;

    for (const QPolygonF &polygon : polygons)
    {
        if (polygon.size() < 2)
            continue;
        for (int i = 0; i < polygon.size(); ++i)
        {
            const QPointF a = polygon.at(i);
            const QPointF b = polygon.at((i + 1) % polygon.size());
            if (std::hypot(b.x() - a.x(), b.y() - a.y()) < 1e-9)
                continue;
            const QPointF candidate = closestPointOnSegment(a, b, point);
            const double d = std::hypot(candidate.x() - point.x(),
                                        candidate.y() - point.y());
            if (d < bestDistance)
            {
                bestDistance = d;
                bestPoint = candidate;
                bestA = a;
                bestB = b;
                found = true;
            }
        }
    }

    if (!found)
        return false;

    const QPointF tangent = bestB - bestA;
    const double length = std::hypot(tangent.x(), tangent.y());
    QPointF candidate = length > 1e-9
            ? QPointF(-tangent.y() / length, tangent.x() / length)
            : QPointF(0.0, -1.0);

    const QRectF bounds = path.boundingRect();
    const double probe = std::max(0.01,
            std::min(0.5, std::min(bounds.width(), bounds.height()) * 0.25));
    if (path.contains(bestPoint + candidate * probe))
        candidate = -candidate;

    *nearest = bestPoint;
    *normal = candidate;
    *distance = bestDistance;
    return true;
}

Hit deepestHit(const QPointF &position, const QVector<QPointF> &localPoints,
               const QVector<QPainterPath> &colliders)
{
    Hit best;
    for (const QPainterPath &path : colliders)
    {
        if (path.isEmpty())
            continue;
        const QRectF bounds = path.boundingRect();
        for (const QPointF &local : localPoints)
        {
            const QPointF point = position + local;
            if (!bounds.contains(point) || !path.contains(point))
                continue;

            QPointF nearest;
            QPointF normal;
            double distance = 0.0;
            if (!nearestOnPath(path, point, &nearest, &normal, &distance))
                continue;

            if (!best.valid || distance > best.depth)
            {
                best.valid = true;
                best.normal = normal;
                best.depth = distance;
                best.point = point;
            }
        }
    }
    return best;
}

// Resolve every penetration at the current position.  Applying the response
// more than once handles corners touching two faces of the same shape.
bool resolveCollisions(QPointF &position, QPointF &velocity,
                       const QVector<QPointF> &localPoints,
                       const QVector<QPainterPath> &colliders,
                       double elasticity, double friction,
                       double stopThreshold, bool &stopped)
{
    bool collided = false;
    constexpr int maxPasses = 4;
    for (int pass = 0; pass < maxPasses; ++pass)
    {
        const Hit hit = deepestHit(position, localPoints, colliders);
        if (!hit.valid)
            break;

        collided = true;
        position += hit.normal * (hit.depth + 0.01);

        const double normalVelocity = QPointF::dotProduct(velocity, hit.normal);
        if (normalVelocity < 0.0)
        {
            const QPointF normalPart = hit.normal * normalVelocity;
            const QPointF tangentPart = velocity - normalPart;
            velocity = tangentPart * (1.0 - friction)
                    + hit.normal * (-elasticity * normalVelocity);
        }
    }

    if (collided && stopThreshold > 0.0
        && std::hypot(velocity.x(), velocity.y()) < stopThreshold)
    {
        velocity = QPointF(0.0, 0.0);
        stopped = true;
    }
    return collided;
}

void appendCandidates(const QVariant &value, const Uuid &excludedUuid,
                      QVector<Uuid> &out, QSet<Uuid> &seen)
{
    if (!value.isValid())
        return;

    QStringList list;
    if (value.typeId() == QMetaType::QStringList)
        list = value.toStringList();
    else if (value.typeId() == QMetaType::QString)
        list = value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    else if (value.canConvert<QVariantList>())
    {
        const QVariantList variants = value.toList();
        for (const QVariant &variant : variants)
        {
            if (variant.canConvert<QString>())
                list.append(variant.toString());
        }
    }

    for (const QString &raw : list)
    {
        const Uuid uuid = raw.trimmed();
        if (uuid.isEmpty() || uuid == excludedUuid || seen.contains(uuid))
            continue;
        seen.insert(uuid);
        out.append(uuid);
    }
}

QVector<Uuid> collisionCandidates(const Segment &segment, const World &world, int frame)
{
    QVector<Uuid> candidates;
    QSet<Uuid> seen;

    const auto add = [&](const Uuid &uuid)
    {
        if (uuid.isEmpty() || uuid == segment.entityUuid || seen.contains(uuid))
            return;
        seen.insert(uuid);
        candidates.append(uuid);
    };

    for (const Uuid &uuid : world.physicsEntities(frame))
        add(uuid);

    // The direction/ground entity may not be enumerated by World; it always
    // participates when physicsInteract() says so (filtered in collidersAt).
    add(segment.targetUuid);

    appendCandidates(segment.params.value(QStringLiteral("collisionUuids")),
                     segment.entityUuid, candidates, seen);
    return candidates;
}

QVector<QPainterPath> collidersAt(const World &world, const QVector<Uuid> &candidates,
                                  int frame)
{
    QVector<QPainterPath> colliders;
    for (const Uuid &uuid : candidates)
    {
        if (!world.physicsInteract(uuid, frame))
            continue;
        const QPainterPath shape = world.shapeOf(uuid, frame);
        if (!shape.isEmpty())
            colliders.append(shape);
    }
    return colliders;
}

} // namespace

QVector<Sample> evaluateGravity(const Segment &segment, const World &world,
                                const FrameRange &range, int fps)
{
    const double strength = paramNumber(segment.params, "strength", 980.0);
    const double elasticity = std::clamp(
            paramNumber(segment.params, "elasticity", 0.35), 0.0, 1.0);
    const double friction = std::clamp(
            paramNumber(segment.params, "friction", 0.2), 0.0, 1.0);
    const double stopThreshold = std::max(
            0.0, paramNumber(segment.params, "stopThreshold", 2.0));

    const double dt = 1.0 / double(fps);
    Pose pose = segment.startPose;
    QPointF velocity = segment.initialVelocity; // solver space
    bool stopped = false;

    QVector<Sample> samples;
    samples.reserve(range.count);

    for (int i = 0; i < range.count; ++i)
    {
        bool collided = false;
        const int frame = range.startFrame + i;

        if (i > 0 && !stopped)
        {
            // Gravity follows targetUuid's current rotation.  The driven
            // entity's own rotation must not influence the direction.
            const Pose directionPose = world.poseOf(segment.targetUuid, frame);
            const QPointF acceleration = rotatePoint(QPointF(0.0, 1.0),
                                                     directionPose.rotation)
                    * strength;

            // The moving entity supplies its own collision shape.  Translate
            // the solver-space shape back to a shape local to its own pose.
            const QPainterPath movingShape = world.shapeOf(segment.entityUuid, frame);
            QVector<QPointF> localPoints;
            if (movingShape.isEmpty())
            {
                localPoints.append(QPointF(0.0, 0.0));
            }
            else
            {
                const Pose movingPose = world.poseOf(segment.entityUuid, frame);
                localPoints = outlinePoints(
                        movingShape.translated(-movingPose.position));
            }

            const QVector<Uuid> candidates = collisionCandidates(
                    segment, world, frame);
            const QVector<QPainterPath> colliders = collidersAt(
                    world, candidates, frame);

            const double travel = std::hypot(velocity.x(), velocity.y()) * dt
                    + 0.5 * std::hypot(acceleration.x(), acceleration.y())
                            * dt * dt;
            const int substeps = std::clamp(int(std::ceil(travel / 3.0)), 1, 64);
            const double subDt = dt / double(substeps);

            for (int step = 0; step < substeps && !stopped; ++step)
            {
                pose.position += velocity * subDt
                        + acceleration * (0.5 * subDt * subDt);
                velocity += acceleration * subDt;
                if (resolveCollisions(pose.position, velocity, localPoints,
                                      colliders, elasticity, friction,
                                      stopThreshold, stopped))
                    collided = true;
            }
        }

        const bool last = (i == range.count - 1);
        if (last)
        {
            velocity = QPointF(0.0, 0.0);
            stopped = true;
        }

        Sample sample;
        sample.frame = frame;
        sample.pose = pose;
        sample.velocity = velocity;
        sample.collided = collided;
        sample.stopped = stopped;
        samples.append(sample);
    }

    return samples;
}

} // namespace pvf::solver::detail
