#include "CameraRenderer.h"

#include "SceneRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>

namespace pvf {
namespace CameraRenderer {

namespace {

QTransform cameraWorldToTarget(const Camera::State& state, const QRectF& target)
{
    QTransform transform;
    transform.translate(target.center().x(), target.center().y());
    transform.scale(target.width() / std::max(1.0, state.size.width()),
                    target.height() / std::max(1.0, state.size.height()));
    transform.rotate(-state.rotation);
    transform.translate(-state.center.x(), -state.center.y());
    return transform;
}

QRectF canvasRect(const Project& project)
{
    return QRectF(-project.width / 2.0, -project.height / 2.0, project.width,
                  project.height);
}

} // namespace

void renderCamera(QPainter& painter, const Project& project, int frame,
                  const QRectF& target, const Camera::State& state, bool useThumbnails)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(target, Qt::black);
    painter.setClipRect(target);

    const QTransform worldToTarget = cameraWorldToTarget(state, target);
    QPolygonF canvasPolygon = worldToTarget.map(QPolygonF(canvasRect(project)));
    QPainterPath canvasPath;
    canvasPath.addPolygon(canvasPolygon);
    canvasPath.closeSubpath();

    painter.fillPath(canvasPath, project.background);
    painter.setClipPath(canvasPath, Qt::IntersectClip);
    SceneRenderer::drawEntities(painter, project, frame, worldToTarget, useThumbnails);
    painter.restore();
}

QImage renderCameraImage(const Project& project, int frame, const Camera::State& state,
                         const QSize& size, bool useThumbnails)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    if (size.isEmpty())
        return image;

    QPainter painter(&image);
    renderCamera(painter, project, frame, QRectF(QPointF(0, 0), QSizeF(size)), state,
                 useThumbnails);
    painter.end();
    return image;
}

void renderWorkCamera(QPainter& painter, const Project& project, int frame,
                      const QRectF& target,
                      const QTransform& viewportWorldToScreen,
                      const QSize& viewportSize, const QColor& outsideColor,
                      bool useThumbnails)
{
    painter.save();
    painter.fillRect(target, outsideColor);
    painter.setClipRect(target);

    const double sx = target.width() / std::max(1, viewportSize.width());
    const double sy = target.height() / std::max(1, viewportSize.height());
    QTransform viewportToTarget;
    viewportToTarget.translate(target.left(), target.top());
    viewportToTarget.scale(sx, sy);
    const QTransform worldToTarget = viewportWorldToScreen * viewportToTarget;

    // The work camera mirrors the viewport, but the monitor is a preview of
    // the project: objects outside the canvas must not leak into it.
    QPolygonF canvasPolygon = worldToTarget.map(QPolygonF(canvasRect(project)));
    QPainterPath canvasPath;
    canvasPath.addPolygon(canvasPolygon);
    canvasPath.closeSubpath();
    painter.fillPath(canvasPath, project.background);
    painter.setClipPath(canvasPath, Qt::IntersectClip);
    SceneRenderer::drawEntities(painter, project, frame, worldToTarget, useThumbnails);
    painter.restore();
}

} // namespace CameraRenderer
} // namespace pvf
