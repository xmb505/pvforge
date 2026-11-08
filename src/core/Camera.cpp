#include "Camera.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf {
namespace Camera {

namespace {

QTransform entityLocalTransform(const Project& project, const Entity& entity, int frame)
{
    const double x = project.value(entity.id, QStringLiteral("x"), frame);
    const double y = project.value(entity.id, QStringLiteral("y"), frame);
    const double rotation = project.value(entity.id, QStringLiteral("rotation"), frame);
    const double ax = project.value(entity.id, QStringLiteral("rotPointX"), frame);
    const double ay = project.value(entity.id, QStringLiteral("rotPointY"), frame);
    const double scaleX = project.value(entity.id, QStringLiteral("scaleX"), frame);
    const double scaleY = project.value(entity.id, QStringLiteral("scaleY"), frame);

    QTransform transform;
    transform.translate(x, y);
    transform.rotate(rotation);
    transform.translate(-ax, -ay);
    transform.scale(scaleX, scaleY);
    return transform;
}

QTransform parentWorldTransform(const Project& project, const Entity& entity, int frame)
{
    QTransform result;
    QSet<EntityId> seen;
    EntityId parent = entity.parent;
    while (parent != kInvalidEntity && !seen.contains(parent))
    {
        seen.insert(parent);
        const Entity* parentEntity = project.entity(parent);
        if (!parentEntity)
            break;
        // Nested cameras are not exposed by the UI; treating one as a normal
        // entity keeps this helper cycle-safe.
        result = result * entityLocalTransform(project, *parentEntity, frame);
        parent = parentEntity->parent;
    }
    return result;
}

} // namespace

State state(const Project& project, EntityId id, int frame)
{
    State result;
    const Entity* entity = project.entity(id);
    const double localScale =
        std::clamp(project.value(id, QStringLiteral("scaleX"), frame), kMinScale,
                   kMaxScale);
    const double localRotation = project.value(id, QStringLiteral("rotation"), frame);
    const QPointF localCenter(project.value(id, QStringLiteral("x"), frame),
                              project.value(id, QStringLiteral("y"), frame));

    if (entity && entity->parent != kInvalidEntity)
    {
        // A camera parented to an entity follows it: its local frame is mapped
        // through the parent chain. Rotation/scale are combined into the world
        // state used by the viewfinder and the camera renderer.
        const QTransform parent = parentWorldTransform(project, *entity, frame);
        result.center = parent.map(localCenter);
        const QPointF origin = parent.map(QPointF(0, 0));
        const QPointF xAxis = parent.map(QPointF(1, 0)) - origin;
        const QPointF yAxis = parent.map(QPointF(0, 1)) - origin;
        const double parentScale =
            (std::hypot(xAxis.x(), xAxis.y()) + std::hypot(yAxis.x(), yAxis.y()))
            / 2.0;
        const double parentRotation =
            std::atan2(xAxis.y(), xAxis.x()) * 180.0 / M_PI;
        result.rotation = localRotation + parentRotation;
        result.scale = std::clamp(localScale * parentScale, kMinScale, kMaxScale);
        // A scene folder is an identity parent. Keep the same project-space
        // clamp a root camera gets, otherwise v3 migration could shift an
        // out-of-bounds camera.
        if (parent.isIdentity())
            result.center = clampCenter(project, result.scale, result.center);
    }
    else
    {
        result.scale = localScale;
        result.rotation = localRotation;
        result.center = clampCenter(project, result.scale, localCenter);
    }

    result.size = QSizeF(project.width * result.scale, project.height * result.scale);
    return result;
}

QPointF clampCenter(const Project& project, double scale, const QPointF& center)
{
    const double frameW = project.width * std::clamp(scale, kMinScale, kMaxScale);
    const double frameH = project.height * std::clamp(scale, kMinScale, kMaxScale);
    const double maxX = std::max(0.0, (project.width - frameW) / 2.0);
    const double maxY = std::max(0.0, (project.height - frameH) / 2.0);
    return QPointF(std::clamp(center.x(), -maxX, maxX),
                   std::clamp(center.y(), -maxY, maxY));
}

QTransform frameToProject(const State& state)
{
    QTransform transform;
    transform.translate(state.center.x(), state.center.y());
    transform.rotate(state.rotation);
    return transform;
}

QPolygonF polygon(const State& state)
{
    QPolygonF rect;
    rect << QPointF(-state.size.width() / 2.0, -state.size.height() / 2.0)
         << QPointF(state.size.width() / 2.0, -state.size.height() / 2.0)
         << QPointF(state.size.width() / 2.0, state.size.height() / 2.0)
         << QPointF(-state.size.width() / 2.0, state.size.height() / 2.0);
    return frameToProject(state).map(rect);
}

} // namespace Camera
} // namespace pvf
