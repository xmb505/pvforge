#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace pvf {
namespace Camera {

State state(const Project& project, EntityId id, int frame)
{
    State result;
    result.scale = std::clamp(project.value(id, QStringLiteral("scaleX"), frame),
                              kMinScale, kMaxScale);
    result.rotation = project.value(id, QStringLiteral("rotation"), frame);
    result.center = QPointF(project.value(id, QStringLiteral("x"), frame),
                            project.value(id, QStringLiteral("y"), frame));
    result.center = clampCenter(project, result.scale, result.center);
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
