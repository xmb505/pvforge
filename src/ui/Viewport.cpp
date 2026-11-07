#include "core/ComicBox.h"
#include "core/TextStyle.h"
#include "Viewport.h"

#include "app/AppIcon.h"
#include "core/Settings.h"
#include "render/SceneRenderer.h"

#include <QApplication>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

// Gizmo geometry in screen pixels, so it keeps a constant size while zooming.
constexpr double kGizmoAxisLength = 78.0;
constexpr double kGizmoRingRadius = 48.0;
constexpr double kGizmoHitWidth = 7.0;
constexpr double kGizmoCenterDeadzone = 14.0;

// Snaps value to the nearest candidate within threshold.
double snapTo(double value, std::initializer_list<double> candidates, double threshold)
{
    for (double candidate : candidates)
        if (std::abs(value - candidate) <= threshold)
            return candidate;
    return value;
}

double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared < 1e-9)
        return std::hypot(point.x() - a.x(), point.y() - a.y());
    const double t = std::clamp(((point.x() - a.x()) * ab.x()
                                 + (point.y() - a.y()) * ab.y())
                                    / lengthSquared,
                                0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(point.x() - closest.x(), point.y() - closest.y());
}

} // namespace


Viewport::Viewport(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_showCameraFrame = Settings::showCameraFrame();

    m_workbenchButton = new QToolButton(this);
    m_workbenchButton->setObjectName(QStringLiteral("workbenchButton"));
    m_workbenchButton->setIcon(makeCubeIcon());
    m_workbenchButton->setIconSize(QSize(34, 34));
    m_workbenchButton->setFixedSize(46, 46);
    m_workbenchButton->move(12, 12);
    m_workbenchButton->setToolTip(QStringLiteral("工作台：创建图片 / 视频 / 文字 / 矢量"));
    m_workbenchButton->setStyleSheet(QStringLiteral(
        "QToolButton#workbenchButton { background: rgba(40, 40, 48, 210);"
        " border: 1px solid #5a5a66; }"
        "QToolButton#workbenchButton:hover { background: rgba(70, 70, 82, 230);"
        " border-color: #8a8a96; }"
        "QToolButton#workbenchButton:pressed { background: rgba(30, 30, 36, 230); }"));
    connect(m_workbenchButton, &QToolButton::clicked, this, &Viewport::workbenchRequested);
}

void Viewport::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    update();
}

void Viewport::setSelectedEntity(EntityId id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    update();
}

void Viewport::setCameraFrameVisible(bool on)
{
    if (m_showCameraFrame == on)
        return;
    m_showCameraFrame = on;
    update();
}

void Viewport::resetView()
{
    m_zoom = 1.0;
    m_pan = QPointF();
    emit viewChanged();
    update();
}

double Viewport::viewScale() const
{
    return canvasRect().width() / std::max(1, m_project->width);
}

QRectF Viewport::canvasRect() const
{
    const double margin = 40.0;
    const double fitW = (width() - margin) / std::max(1, m_project->width);
    const double fitH = (height() - margin) / std::max(1, m_project->height);
    const double scale = std::max(0.0001, std::min(fitW, fitH) * m_zoom);
    const QSizeF size(m_project->width * scale, m_project->height * scale);
    const QPointF center = QRectF(rect()).center() + m_pan;
    return QRectF(center.x() - size.width() / 2.0, center.y() - size.height() / 2.0,
                  size.width(), size.height());
}

QPointF Viewport::screenToCanvas(const QPointF& pos) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const QPointF center = canvas.center();
    return QPointF((pos.x() - center.x()) / s, (pos.y() - center.y()) / s);
}

EntityId Viewport::activeCameraEntity() const
{
    const Entity* entity = m_project->entity(m_project->activeCamera);
    if (!entity || !entity->isCamera()
        || m_project->isEffectivelyHidden(entity->id))
        return kInvalidEntity;
    return entity->id;
}

Camera::State Viewport::cameraState(const Entity& e) const
{
    return Camera::state(*m_project, e.id, m_frame);
}

QPolygonF Viewport::cameraScreenPolygon() const
{
    if (!m_showCameraFrame)
        return QPolygonF();
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPolygonF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPolygonF();
    return worldToScreenTransform().map(Camera::polygon(cameraState(*entity)));
}

QPointF Viewport::cameraScreenCenter() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPointF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPointF();
    return worldToScreenTransform().map(cameraState(*entity).center);
}

QPointF Viewport::cameraRotationHandle() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPointF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPointF();

    const Camera::State state = cameraState(*entity);
    const QPointF topLocal(0.0, -state.size.height() / 2.0);
    const QPointF topProject = Camera::frameToProject(state).map(topLocal);
    const QPointF topScreen = worldToScreenTransform().map(topProject);

    QTransform direction;
    direction.rotate(state.rotation);
    const QPointF up = direction.map(QPointF(0.0, -1.0));
    return topScreen + up * 28.0;
}

QRectF Viewport::cameraTitleRect() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QRectF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QRectF();

    const QPolygonF polygon = cameraScreenPolygon();
    if (polygon.isEmpty())
        return QRectF();
    const QRectF box = polygon.boundingRect();
    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(entity->name);
    const QRectF title(box.left(), box.top() - 24.0, textWidth + 16.0, 20.0);
    if (title.top() < 2.0)
        return QRectF(box.left(), box.top() + 4.0, textWidth + 16.0, 20.0);
    return title;
}

Viewport::CameraPart Viewport::cameraHitTest(const QPointF& pos) const
{
    if (!m_showCameraFrame)
        return CameraPart::None;
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return CameraPart::None;
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return CameraPart::None;

    const bool selected = (m_selected == id);
    if (selected && !entity->locked && entity->parent == kInvalidEntity)
    {
        if (QLineF(pos, cameraRotationHandle()).length() <= 10.0)
            return CameraPart::Rotate;

        const QPolygonF polygon = cameraScreenPolygon();
        for (int i = 0; i < polygon.size(); ++i)
        {
            if (QLineF(pos, polygon[i]).length() <= 8.0)
                return CameraPart(i == 0 ? CameraPart::Corner0
                                  : i == 1 ? CameraPart::Corner1
                                  : i == 2 ? CameraPart::Corner2
                                           : CameraPart::Corner3);
        }
    }

    if (cameraTitleRect().contains(pos))
        return CameraPart::Move;

    const QPolygonF polygon = cameraScreenPolygon();
    for (int i = 0; i < polygon.size(); ++i)
    {
        if (distanceToSegment(pos, polygon[i], polygon[(i + 1) % polygon.size()]) <= 6.0)
            return CameraPart::Move;
    }
    return CameraPart::None;
}

bool Viewport::handleCameraPress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return false;

    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return false;

    const CameraPart part = cameraHitTest(event->position());
    if (part == CameraPart::None)
        return false;

    Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
        return true;

    emit entitySelected(id);

    // A parented camera follows its parent; direct frame dragging would write
    // project-space values into the parent's coordinate system.
    if (entity->parent != kInvalidEntity)
        return true;

    m_cameraPart = part;
    m_cameraStartState = cameraState(*entity);
    m_cameraStartMouse = event->position();
    const QPointF center = cameraScreenCenter();
    m_cameraStartAngle =
        std::atan2(event->position().y() - center.y(), event->position().x() - center.x())
            * 180.0 / M_PI
        + 90.0;
    m_dragging = false;
    m_gizmoMode = GizmoMode::None;
    m_activeHandle = Handle::None;
    m_dragEntity = kInvalidEntity;
    setCursor(part == CameraPart::Move ? Qt::SizeAllCursor
                                       : part == CameraPart::Rotate ? Qt::CrossCursor
                                                                    : Qt::SizeFDiagCursor);
    update();
    return true;
}

QPointF Viewport::snapCameraCenter(const Camera::State& start, const QPointF& center,
                                   bool* snappedX, double* guideX, bool* snappedY,
                                   double* guideY) const
{
    *snappedX = false;
    *snappedY = false;
    if (m_snapPixels <= 0)
        return center;

    const double threshold = m_snapPixels / viewScale();
    Camera::State candidate = start;
    candidate.center = center;
    const QRectF box = Camera::polygon(candidate).boundingRect();

    QVector<double> targetsX = {-m_project->width / 2.0, 0.0, m_project->width / 2.0};
    QVector<double> targetsY = {-m_project->height / 2.0, 0.0, m_project->height / 2.0};
    const EntityId cameraId = activeCameraEntity();
    for (const Entity& other : m_project->entities())
    {
        if (other.id == cameraId || other.hidden || other.isCamera())
            continue;
        const VisualBounds bounds =
            visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                           m_project->value(other.id, "y", m_frame));
        targetsX << bounds.left << bounds.right;
        targetsY << bounds.top << bounds.bottom;
        if (Settings::edgeMidpointSnap())
        {
            targetsX << (bounds.left + bounds.right) / 2.0;
            targetsY << (bounds.top + bounds.bottom) / 2.0;
        }
    }

    QPointF result = center;
    double bestX = threshold + 1.0;
    double bestTargetX = 0.0;
    for (double target : targetsX)
    {
        const double features[] = {box.left(), box.center().x(), box.right()};
        for (double feature : features)
        {
            const double distance = std::abs(feature - target);
            if (distance < bestX)
            {
                bestX = distance;
                bestTargetX = target;
            }
        }
    }
    if (bestX <= threshold)
    {
        result.setX(center.x() + (bestTargetX - box.center().x()));
        // Pick the frame feature that was closest to the winning target.
        double bestFeatureDistance = 1e9;
        double adjustment = 0.0;
        for (double feature : {box.left(), box.center().x(), box.right()})
        {
            const double distance = std::abs(feature - bestTargetX);
            if (distance < bestFeatureDistance)
            {
                bestFeatureDistance = distance;
                adjustment = bestTargetX - feature;
            }
        }
        result.setX(center.x() + adjustment);
        *snappedX = true;
        *guideX = bestTargetX;
    }

    double bestY = threshold + 1.0;
    double bestTargetY = 0.0;
    for (double target : targetsY)
    {
        for (double feature : {box.top(), box.center().y(), box.bottom()})
        {
            const double distance = std::abs(feature - target);
            if (distance < bestY)
            {
                bestY = distance;
                bestTargetY = target;
            }
        }
    }
    if (bestY <= threshold)
    {
        double bestFeatureDistance = 1e9;
        double adjustment = 0.0;
        for (double feature : {box.top(), box.center().y(), box.bottom()})
        {
            const double distance = std::abs(feature - bestTargetY);
            if (distance < bestFeatureDistance)
            {
                bestFeatureDistance = distance;
                adjustment = bestTargetY - feature;
            }
        }
        result.setY(center.y() + adjustment);
        *snappedY = true;
        *guideY = bestTargetY;
    }

    return result;
}

void Viewport::applyCameraDrag(const QPointF& pos, Qt::KeyboardModifiers modifiers)
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity || m_cameraPart == CameraPart::None)
        return;
    Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(entity->id))
        return;

    const Camera::State start = m_cameraStartState;
    m_showGuideX = false;
    m_showGuideY = false;

    if (m_cameraPart == CameraPart::Move)
    {
        const double s = viewScale();
        QPointF center = start.center
                         + QPointF((pos.x() - m_cameraStartMouse.x()) / s,
                                   (pos.y() - m_cameraStartMouse.y()) / s);
        bool snappedX = false;
        bool snappedY = false;
        double guideX = 0.0;
        double guideY = 0.0;
        if (!(modifiers & Qt::ControlModifier))
            center = snapCameraCenter(start, center, &snappedX, &guideX, &snappedY,
                                      &guideY);
        center = Camera::clampCenter(*m_project, start.scale, center);
        m_showGuideX = snappedX;
        m_showGuideY = snappedY;
        m_guideX = guideX;
        m_guideY = guideY;
        emit cameraEdited(id, center.x(), center.y(), start.scale, start.rotation);
        update();
        return;
    }

    if (m_cameraPart == CameraPart::Rotate)
    {
        const QPointF center = worldToScreenTransform().map(start.center);
        double angle =
            std::atan2(pos.y() - center.y(), pos.x() - center.x()) * 180.0 / M_PI + 90.0;
        double delta = angle - m_cameraStartAngle;
        while (delta > 180.0)
            delta -= 360.0;
        while (delta < -180.0)
            delta += 360.0;

        double rotation = start.rotation + delta;
        if (!(modifiers & Qt::ControlModifier))
        {
            constexpr double tolerance = 3.0;
            for (int snapAngle : Settings::snapAngles())
            {
                const double diff = std::remainder(rotation - double(snapAngle), 360.0);
                if (std::abs(diff) <= tolerance)
                {
                    rotation = snapAngle;
                    break;
                }
            }
        }
        emit cameraEdited(id, start.center.x(), start.center.y(), start.scale, rotation);
        update();
        return;
    }

    // Corner scaling keeps the camera centre fixed; the frame aspect follows
    // the project automatically.
    const QPointF mouse = screenToCanvas(pos);
    const QPointF delta = mouse - start.center;
    const double radians = -start.rotation * M_PI / 180.0;
    const double localX = delta.x() * std::cos(radians) - delta.y() * std::sin(radians);
    const double localY = delta.x() * std::sin(radians) + delta.y() * std::cos(radians);
    double scale = std::max(std::abs(localX) * 2.0 / std::max(1, m_project->width),
                            std::abs(localY) * 2.0 / std::max(1, m_project->height));
    scale = std::clamp(scale, Camera::kMinScale, Camera::kMaxScale);
    const QPointF center = Camera::clampCenter(*m_project, scale, start.center);
    emit cameraEdited(id, center.x(), center.y(), scale, start.rotation);
    update();
}

void Viewport::drawCameraOverlay(QPainter& painter)
{
    if (!m_showCameraFrame)
        return;
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return;
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return;

    const Camera::State state = cameraState(*entity);
    const QPolygonF polygon = cameraScreenPolygon();
    if (polygon.isEmpty())
        return;

    const bool selected = (m_selected == id);
    const bool locked = entity->locked;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outer;
    outer.addRect(QRectF(rect()));
    QPainterPath inner;
    inner.addPolygon(polygon);
    inner.closeSubpath();
    painter.fillPath(outer.subtracted(inner), QColor(10, 10, 14, 140));

    QPen framePen(QColor(90, 210, 255, selected ? 255 : 200),
                  selected ? 2.0 : 1.2, Qt::DashLine);
    if (locked)
        framePen.setColor(QColor(150, 150, 160, 200));
    painter.setPen(framePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);

    const QRectF title = cameraTitleRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 20, 26, 225));
    painter.drawRect(title);
    painter.setPen(QColor(90, 210, 255, 220));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(title);
    painter.setPen(locked ? QColor(170, 170, 180) : QColor(235, 245, 255));
    painter.drawText(title.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     entity->name);

    if (selected && !locked && entity->parent == kInvalidEntity)
    {
        painter.setPen(QPen(QColor(20, 20, 26), 1.0));
        painter.setBrush(QColor(90, 210, 255));
        for (const QPointF& corner : polygon)
            painter.drawRect(QRectF(corner.x() - 4.0, corner.y() - 4.0, 8.0, 8.0));

        const QPointF handle = cameraRotationHandle();
        QPolygonF frame = polygon;
        QPointF topCenter = (frame[0] + frame[1]) / 2.0;
        if (frame.size() >= 2)
            topCenter = (frame[0] + frame[1]) / 2.0;
        painter.setPen(QPen(QColor(90, 210, 255), 1.5));
        painter.drawLine(topCenter, handle);
        painter.setBrush(QColor(20, 24, 30));
        painter.drawEllipse(handle, 5.0, 5.0);
    }

    painter.restore();
}


QRectF Viewport::entityLocalBounds(const Entity& e) const
{
    return SceneRenderer::entityLocalBounds(e);
}

const Entity* Viewport::hitTest(const QPointF& pos) const
{
    // Topmost first, following the hierarchy layer order.
    const QVector<int> order = m_project->entityOrder();
    for (int oi = int(order.size()) - 1; oi >= 0; --oi)
    {
        const Entity& e = m_project->entities()[order[oi]];
        if (e.isFolder() || e.isCamera()
            || m_project->isEffectivelyHidden(e.id)
            || m_project->isEffectivelyLocked(e.id))
        {
            continue;
        }
        const QRectF bounds = entityLocalBounds(e);
        if (bounds.isEmpty())
            continue;

        bool ok = false;
        const QTransform inverse = entityTransform(e).inverted(&ok);
        if (!ok)
            continue;

        if (bounds.contains(inverse.map(pos)))
            return &e;
    }
    return nullptr;
}

void Viewport::drawEntity(QPainter& p, const Entity& e, const QRectF&, double)
{
    if (e.hidden || e.isCamera() || e.isFolder()
        || m_project->isEffectivelyHidden(e.id))
        return;

    const double opacity =
        std::clamp(m_project->value(e.id, QStringLiteral("opacity"), m_frame), 0.0, 1.0);
    if (opacity <= 0.0)
        return;

    const QRectF bounds = entityLocalBounds(e);
    if (bounds.isEmpty())
        return;

    SceneRenderer::drawEntity(p, *m_project, e, m_frame, worldToScreenTransform(), false);

    if (e.id == m_selected && !e.locked && !m_project->isEffectivelyLocked(e.id))
    {
        p.save();
        p.setTransform(entityTransform(e));
        QPen pen(QColor(77, 163, 255));
        pen.setCosmetic(true);
        pen.setWidthF(1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(bounds);
        p.restore();
    }
}

QPointF Viewport::entityScreenCenter(const Entity& e) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const double x = m_project->value(e.id, "x", m_frame) * s;
    const double y = m_project->value(e.id, "y", m_frame) * s;
    const double rotation = m_project->value(e.id, "rotation", m_frame);
    const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
    const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;

    const double rad = qDegreesToRadians(rotation);
    const double dx = ax * std::cos(rad) - ay * std::sin(rad);
    const double dy = ax * std::sin(rad) + ay * std::cos(rad);

    return canvas.center() + QPointF(x - dx, y - dy);
}

QTransform Viewport::entityTransformForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? entityTransform(*entity) : QTransform();
}

QPointF Viewport::entityPivotForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? entityPivotScreen(*entity) : QPointF();
}

QPointF Viewport::comicTipForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? comicTipScreenPos(*entity) : QPointF();
}

QPointF Viewport::entityPivotScreen(const Entity& e) const
{
    // X/Y are the pivot position in parent space. Applying the parent chain
    // (without the entity's own scale) keeps the gizmo on the pivot.
    const double x = m_project->value(e.id, "x", m_frame);
    const double y = m_project->value(e.id, "y", m_frame);
    const QPointF world =
        SceneRenderer::parentWorldTransform(*m_project, e, m_frame)
            .map(QPointF(x, y));
    return worldToScreenTransform().map(world);
}

Viewport::VisualBounds Viewport::visualBoundsOf(const Entity& e, double posX,
                                                double posY) const
{
    const QRectF bounds = entityLocalBounds(e);
    const double rotation = m_project->value(e.id, "rotation", m_frame);
    const double radians = rotation * M_PI / 180.0;
    const double cosA = std::abs(std::cos(radians));
    const double sinA = std::abs(std::sin(radians));
    const double ax = m_project->value(e.id, "rotPointX", m_frame);
    const double ay = m_project->value(e.id, "rotPointY", m_frame);
    const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
    const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
    const double halfW = bounds.width() / 2.0
                         * std::abs(m_project->value(e.id, "scaleX", m_frame));
    const double halfH = bounds.height() / 2.0
                         * std::abs(m_project->value(e.id, "scaleY", m_frame));
    const double aabbHalfW = halfW * cosA + halfH * sinA;
    const double aabbHalfH = halfW * sinA + halfH * cosA;
    const double centerX = posX - rotatedAx;
    const double centerY = posY - rotatedAy;

    VisualBounds result;
    result.left = centerX - aabbHalfW;
    result.right = centerX + aabbHalfW;
    result.top = centerY - aabbHalfH;
    result.bottom = centerY + aabbHalfH;
    return result;
}

QTransform Viewport::entityTransform(const Entity& e) const
{
    return SceneRenderer::worldTransform(*m_project, e, m_frame)
           * worldToScreenTransform();
}

QTransform Viewport::worldToScreenTransform() const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    QTransform transform;
    transform.translate(canvas.center().x(), canvas.center().y());
    transform.scale(s, s);
    return transform;
}

QTransform Viewport::entityRotationTransform(const Entity& e) const
{
    const double s = viewScale();
    const QRectF canvas = canvasRect();
    const QPointF origin =
        canvas.center() + QPointF(m_project->value(e.id, "x", m_frame) * s,
                                  m_project->value(e.id, "y", m_frame) * s);
    const double rotation = m_project->value(e.id, "rotation", m_frame);

    const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
    const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;

    QTransform transform;
    transform.translate(origin.x(), origin.y());
    transform.rotate(rotation);
    transform.translate(-ax, -ay);
    return transform;
}

bool Viewport::gizmoHitTest(const QPointF& pos, GizmoMode* mode) const
{
    if (m_selected == kInvalidEntity)
        return false;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity || entity->isCamera() || entity->isFolder()
        || m_project->isEffectivelyLocked(entity->id))
        return false;

    const QPointF delta = pos - entityPivotScreen(*entity);
    const double distance = std::hypot(delta.x(), delta.y());

    // Dragging the pivot dot moves the custom pivot freely.
    if (entity->rotPointCustom() && distance <= 10.0)
    {
        *mode = GizmoMode::PivotFree;
        return true;
    }

    // The rotation ring sits on top of the axes.
    if (std::abs(distance - kGizmoRingRadius) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::Rotate;
        return true;
    }

    // Close to the centre the entity itself is grabbed instead.
    if (distance <= kGizmoCenterDeadzone)
        return false;

    // X points right, Y points up: a right angle like Mine-imator.
    if (delta.x() > kGizmoCenterDeadzone && delta.x() <= kGizmoAxisLength
        && std::abs(delta.y()) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::AxisX;
        return true;
    }
    if (delta.y() < -kGizmoCenterDeadzone && delta.y() >= -kGizmoAxisLength
        && std::abs(delta.x()) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::AxisY;
        return true;
    }
    return false;
}

void Viewport::drawGizmo(QPainter& p, const Entity& e)
{
    const QPointF center = entityPivotScreen(e);
    const double len = kGizmoAxisLength;
    const double ring = kGizmoRingRadius;

    const auto arrow = [&p](const QPointF& tip, const QPointF& back, const QColor& color) {
        const QPointF dir = tip - back;
        const double length = std::hypot(dir.x(), dir.y());
        if (length < 1.0)
            return;
        const QPointF unit = dir / length;
        const QPointF side(-unit.y(), unit.x());
        QPolygonF triangle;
        triangle << tip << tip - unit * 11.0 + side * 5.0 << tip - unit * 11.0 - side * 5.0;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(triangle);
    };

    // X axis: right only.
    const bool hotX = m_gizmoMode == GizmoMode::AxisX
                      || m_gizmoMode == GizmoMode::PivotX;
    const QColor xColor = hotX ? QColor(255, 150, 150) : QColor(226, 86, 86);
    p.setPen(QPen(xColor, hotX ? 4.0 : 3.0));
    p.drawLine(center, center + QPointF(len, 0));
    arrow(center + QPointF(len, 0), center, xColor);

    // Y axis: up only.
    const bool hotY = m_gizmoMode == GizmoMode::AxisY
                      || m_gizmoMode == GizmoMode::PivotY;
    const QColor yColor = hotY ? QColor(150, 235, 150) : QColor(96, 200, 96);
    p.setPen(QPen(yColor, hotY ? 4.0 : 3.0));
    p.drawLine(center, center - QPointF(0, len));
    arrow(center - QPointF(0, len), center, yColor);

    // Pivot dot: drag to move the custom pivot.
    if (e.rotPointCustom())
    {
        p.setBrush(QColor(255, 255, 255, 230));
        p.setPen(QPen(QColor(120, 170, 255), 1.5));
        p.drawEllipse(center, 5, 5);
    }

    // Rotation ring.
    const bool hotRotate = m_gizmoMode == GizmoMode::Rotate;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(hotRotate ? QColor(255, 214, 120) : QColor(210, 200, 160),
                  hotRotate ? 3.5 : 2.0));
    p.drawEllipse(center, ring, ring);
}

QPointF Viewport::handleScreenPos(const Entity& e, Handle handle) const
{
    const QRectF bounds = entityLocalBounds(e);
    QPointF local;
    switch (handle)
    {
    case Handle::ComicTip:
        return comicTipScreenPos(e);
    case Handle::TopLeft:
        local = bounds.topLeft();
        break;
    case Handle::Top:
        local = QPointF(bounds.center().x(), bounds.top());
        break;
    case Handle::TopRight:
        local = bounds.topRight();
        break;
    case Handle::Right:
        local = QPointF(bounds.right(), bounds.center().y());
        break;
    case Handle::BottomRight:
        local = bounds.bottomRight();
        break;
    case Handle::Bottom:
        local = QPointF(bounds.center().x(), bounds.bottom());
        break;
    case Handle::BottomLeft:
        local = bounds.bottomLeft();
        break;
    case Handle::Left:
        local = QPointF(bounds.left(), bounds.center().y());
        break;
    case Handle::None:
        return QPointF();
    }

    return entityTransform(e).map(local);
}

QPointF Viewport::comicTipScreenPos(const Entity& e) const
{
    const ComicBox box =
        ComicBox::build(TextStyle::fromData(e.data), BubbleStyle::fromData(e.data));
    return entityTransform(e).map(box.tip());
}

QPointF Viewport::screenToEntityLocal(const Entity& e, const QPointF& screenPos) const
{
    bool ok = false;
    const QTransform inverse = entityTransform(e).inverted(&ok);
    if (!ok)
        return QPointF();
    return inverse.map(screenPos);
}

Viewport::Handle Viewport::hitHandle(const QPointF& pos) const
{
    if (m_selected == kInvalidEntity)
        return Handle::None;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity || entity->isCamera() || entity->isFolder()
        || m_project->isEffectivelyLocked(entity->id))
        return Handle::None;

    // The comic balloon spike has its own handle, drawn on the tip.
    if (entity->type == EntityType::ComicText)
    {
        const QPointF tip = comicTipScreenPos(*entity);
        if (std::hypot(tip.x() - pos.x(), tip.y() - pos.y()) <= 8.0)
            return Handle::ComicTip;
    }

    // Only corner handles: edge midpoints sit exactly on the gizmo axes and
    // would steal axis drags.
    static const Handle handles[] = {Handle::TopLeft, Handle::TopRight,
                                     Handle::BottomRight, Handle::BottomLeft};
    for (Handle handle : handles)
    {
        const QPointF point = handleScreenPos(*entity, handle);
        if (std::hypot(point.x() - pos.x(), point.y() - pos.y()) <= 6.0)
            return handle;
    }
    return Handle::None;
}

void Viewport::drawHandles(QPainter& p, const Entity& e)
{
    // Only corner handles: edge midpoints sit exactly on the gizmo axes and
    // would steal axis drags.
    static const Handle handles[] = {Handle::TopLeft, Handle::TopRight,
                                     Handle::BottomRight, Handle::BottomLeft};
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    p.setPen(QPen(accent, 1.5));
    p.setBrush(Qt::white);
    for (Handle handle : handles)
    {
        const QPointF point = handleScreenPos(e, handle);
        p.drawRect(QRectF(point.x() - 4, point.y() - 4, 8, 8));
    }

    if (e.type == EntityType::ComicText)
    {
        const QPointF tip = comicTipScreenPos(e);
        p.drawEllipse(QPointF(tip), 5.0, 5.0);
    }
}

void Viewport::applyHandleDrag(const QPointF& mousePos, bool uniform, bool snap)
{
    const Entity* entity = m_project->entity(m_dragEntity);
    if (!entity)
        return;

    const QRectF bounds = entityLocalBounds(*entity);
    const double s = viewScale();
    const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
    const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
    const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
    const bool parented = entity->parent != kInvalidEntity;
    m_showGuideX = false;
    m_showGuideY = false;

    // Measure against the drag-start centre: the fixed opposite corner moves
    // the entity while dragging, so the current centre would drift.
    const QTransform parentTransform =
        SceneRenderer::parentWorldTransform(*m_project, *entity, m_frame);
    QTransform childBase;
    childBase.translate(m_scaleStartPosX, m_scaleStartPosY);
    childBase.rotate(rotation);
    childBase.translate(-ax, -ay);
    QTransform base =
        childBase * parentTransform * worldToScreenTransform();
    bool ok = false;
    const QPointF local = base.inverted(&ok).map(mousePos);
    if (!ok)
        return;

    const double halfW = std::max(1.0, bounds.width() / 2.0);
    const double halfH = std::max(1.0, bounds.height() / 2.0);

    double signX = 0.0;
    double signY = 0.0;
    switch (m_activeHandle)
    {
    case Handle::ComicTip:
        return;
    case Handle::TopLeft:
        signX = -1.0;
        signY = -1.0;
        break;
    case Handle::Top:
        signY = -1.0;
        break;
    case Handle::TopRight:
        signX = 1.0;
        signY = -1.0;
        break;
    case Handle::Right:
        signX = 1.0;
        break;
    case Handle::BottomRight:
        signX = 1.0;
        signY = 1.0;
        break;
    case Handle::Bottom:
        signY = 1.0;
        break;
    case Handle::BottomLeft:
        signX = -1.0;
        signY = 1.0;
        break;
    case Handle::Left:
        signX = -1.0;
        break;
    case Handle::None:
        return;
    }

    double newScaleX = m_scaleStartX;
    double newScaleY = m_scaleStartY;
    if (signX != 0.0)
        newScaleX = signX * local.x() / halfW;
    if (signY != 0.0)
        newScaleY = signY * local.y() / halfH;

    if (uniform)
    {
        const double ratio = (std::abs(m_scaleStartX) > 1e-6)
                                 ? m_scaleStartY / m_scaleStartX
                                 : 1.0;
        if (signX != 0.0 && signY != 0.0)
        {
            if (std::abs(newScaleX) >= std::abs(newScaleY))
                newScaleY = newScaleX * ratio;
            else
                newScaleX = newScaleY / ratio;
        }
        else if (signX != 0.0)
        {
            newScaleY = newScaleX * ratio;
        }
        else if (signY != 0.0)
        {
            newScaleX = newScaleY / ratio;
        }
    }

    const double currentX = m_project->value(m_dragEntity, "x", m_frame);
    const double currentY = m_project->value(m_dragEntity, "y", m_frame);

    // Shift: uniform scaling around the entity anchor. Keeping the anchor
    // fixed makes it possible to fill the canvas from the centre, and the
    // visual AABB can snap to canvas/entity edges even when rotated or inside
    // a transformed parent.
    if (uniform)
    {
        const QRectF localBounds = entityLocalBounds(*entity);
        QTransform localTransform;
        localTransform.translate(currentX, currentY);
        localTransform.rotate(rotation);
        localTransform.translate(-ax, -ay);
        localTransform.scale(newScaleX, newScaleY);
        const QTransform visual = localTransform * parentTransform;

        const QPointF c0 = visual.map(localBounds.topLeft());
        const QPointF c1 = visual.map(localBounds.topRight());
        const QPointF c2 = visual.map(localBounds.bottomRight());
        const QPointF c3 = visual.map(localBounds.bottomLeft());
        const double minX = std::min({c0.x(), c1.x(), c2.x(), c3.x()});
        const double maxX = std::max({c0.x(), c1.x(), c2.x(), c3.x()});
        const double minY = std::min({c0.y(), c1.y(), c2.y(), c3.y()});
        const double maxY = std::max({c0.y(), c1.y(), c2.y(), c3.y()});
        const double centerX = (minX + maxX) / 2.0;
        const double centerY = (minY + maxY) / 2.0;
        double halfVisualW = (maxX - minX) / 2.0;
        double halfVisualH = (maxY - minY) / 2.0;

        if (snap && m_snapPixels > 0 && halfVisualW > 1e-9 && halfVisualH > 1e-9)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double canvasHalfH = m_project->height / 2.0;
            QVector<double> targetsX = {-canvasHalfW, 0.0, canvasHalfW};
            QVector<double> targetsY = {-canvasHalfH, 0.0, canvasHalfH};

            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden)
                    continue;
                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds otherBounds =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));
                targetsX << otherBounds.left << otherBounds.right;
                targetsY << otherBounds.top << otherBounds.bottom;
                if (Settings::edgeMidpointSnap())
                {
                    targetsX << (otherBounds.left + otherBounds.right) / 2.0;
                    targetsY << (otherBounds.top + otherBounds.bottom) / 2.0;
                }
            }

            double bestDelta = threshold;
            double bestFactor = 1.0;
            double bestTarget = 0.0;
            bool bestIsX = false;
            const auto considerTarget = [&](double target, double center,
                                            double half, bool isX) {
                const double targetHalf = std::abs(target - center);
                if (targetHalf <= 1e-9)
                    return;
                const double delta = std::abs(targetHalf - half);
                if (delta <= bestDelta)
                {
                    bestDelta = delta;
                    bestFactor = targetHalf / half;
                    bestTarget = target;
                    bestIsX = isX;
                }
            };
            for (double target : targetsX)
                considerTarget(target, centerX, halfVisualW, true);
            for (double target : targetsY)
                considerTarget(target, centerY, halfVisualH, false);

            if (bestFactor != 1.0)
            {
                newScaleX *= bestFactor;
                newScaleY *= bestFactor;
                halfVisualW *= bestFactor;
                halfVisualH *= bestFactor;
                if (bestIsX)
                {
                    m_showGuideX = true;
                    m_guideX = bestTarget;
                }
                else
                {
                    m_showGuideY = true;
                    m_guideY = bestTarget;
                }
            }
        }

        emit scaleEdited(m_dragEntity, newScaleX, newScaleY, currentX, currentY);
        return;
    }

    // PPT-style corner scaling: the opposite corner stays fixed.
    if (std::abs(rotation) < 1e-6 && signX != 0.0 && signY != 0.0)
    {
        const double startCenterX = m_scaleStartPosX - ax;
        const double startCenterY = m_scaleStartPosY - ay;
        const double fixedX = startCenterX - signX * halfW * m_scaleStartX;
        const double fixedY = startCenterY - signY * halfH * m_scaleStartY;

        double handleX = fixedX + 2.0 * signX * halfW * newScaleX;
        double handleY = fixedY + 2.0 * signY * halfH * newScaleY;

        if (snap && m_snapPixels > 0 && !parented)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double canvasHalfH = m_project->height / 2.0;

            QVector<double> targetsX = {-canvasHalfW, 0.0, canvasHalfW};
            QVector<double> targetsY = {-canvasHalfH, 0.0, canvasHalfH};

            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden)
                    continue;
                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds otherBounds =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));
                targetsX << otherBounds.left << otherBounds.right;
                targetsY << otherBounds.top << otherBounds.bottom;
                if (Settings::edgeMidpointSnap())
                {
                    targetsX << (otherBounds.left + otherBounds.right) / 2.0;
                    targetsY << (otherBounds.top + otherBounds.bottom) / 2.0;
                }
            }

            double bestX = threshold + 1.0;
            double snapX = handleX;
            for (double target : targetsX)
            {
                const double distance = std::abs(handleX - target);
                if (distance < bestX)
                {
                    bestX = distance;
                    snapX = target;
                }
            }
            if (bestX <= threshold)
            {
                newScaleX = signX * (snapX - fixedX) / (2.0 * halfW);
                handleX = snapX;
                m_showGuideX = true;
                m_guideX = snapX;
            }

            double bestY = threshold + 1.0;
            double snapY = handleY;
            for (double target : targetsY)
            {
                const double distance = std::abs(handleY - target);
                if (distance < bestY)
                {
                    bestY = distance;
                    snapY = target;
                }
            }
            if (bestY <= threshold)
            {
                newScaleY = signY * (snapY - fixedY) / (2.0 * halfH);
                m_showGuideY = true;
                m_guideY = snapY;
            }
        }

        const double newCenterX = fixedX + signX * halfW * newScaleX;
        const double newCenterY = fixedY + signY * halfH * newScaleY;
        emit scaleEdited(m_dragEntity, newScaleX, newScaleY, newCenterX + ax,
                         newCenterY + ay);
        return;
    }

    // Rotated entities still scale around their centre.
    emit scaleEdited(m_dragEntity, newScaleX, newScaleY, currentX, currentY);
}

void Viewport::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QApplication::palette().color(QPalette::Window));

    const QRectF canvas = canvasRect();
    const double s = viewScale();

    // Canvas drop shadow and border.
    p.fillRect(canvas.translated(6, 6), QColor(0, 0, 0, 90));
    p.fillRect(canvas, m_project->background);
    p.setPen(QPen(QColor(70, 70, 80), 1));
    p.drawRect(canvas);

    // Center guides.
    p.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    p.drawLine(QPointF(canvas.center().x(), canvas.top()),
               QPointF(canvas.center().x(), canvas.bottom()));
    p.drawLine(QPointF(canvas.left(), canvas.center().y()),
               QPointF(canvas.right(), canvas.center().y()));

    for (const Entity& e : m_project->entities())
        drawEntity(p, e, canvas, s);

    // Camera viewfinder and the veil outside it.
    drawCameraOverlay(p);

    // CAD-style alignment guides while dragging.
    if (m_showGuideX)
    {
        const int gx = int(std::lround(canvas.center().x() + m_guideX * s));
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(gx, int(canvas.top()), gx, int(canvas.bottom()));
    }
    if (m_showGuideY)
    {
        const int gy = int(std::lround(canvas.center().y() + m_guideY * s));
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(int(canvas.left()), gy, int(canvas.right()), gy);
    }

    // Gizmo on top of the selected entity. Cameras have their own frame
    // handles and must not get the generic entity gizmo.
    if (m_selected != kInvalidEntity)
    {
        if (const Entity* entity = m_project->entity(m_selected))
        {
            if (!entity->isCamera() && !entity->isFolder()
                && !m_project->isEffectivelyLocked(entity->id))
            {
                drawHandles(p, *entity);
                drawGizmo(p, *entity);
            }
        }
    }

    // Empty state hint.
    if (m_project->entities().isEmpty())
    {
        p.setPen(QColor(150, 150, 160));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("点击左上角工作台按钮\n创建图片 / 视频 / 文字 / 矢量"));
    }
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_panLast = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);

    // The camera frame is selected through its border/title; clicks inside
    // pass through to the scene so entities remain editable.
    if (handleCameraPress(event))
        return;

    const Entity* selectedEntity = m_project->entity(m_selected);
    if (!selectedEntity || !m_project->isEffectivelyLocked(selectedEntity->id))
    {
        // Scaling handles take priority over the gizmo axes.
        const Handle handle = hitHandle(event->position());
        if (handle != Handle::None)
        {
            m_gizmoMode = handle == Handle::ComicTip ? GizmoMode::ComicTip
                                                      : GizmoMode::Scale;
            m_activeHandle = handle;
            m_dragging = true;
            m_dragEntity = m_selected;
            m_scaleStartX = m_project->value(m_selected, "scaleX", m_frame);
            m_scaleStartY = m_project->value(m_selected, "scaleY", m_frame);
            m_scaleStartPosX = m_project->value(m_selected, "x", m_frame);
            m_scaleStartPosY = m_project->value(m_selected, "y", m_frame);
            m_dragLast = event->position();
            return;
        }

        // The gizmo takes priority over the entity body.
        GizmoMode gizmoMode = GizmoMode::None;
        if (gizmoHitTest(event->position(), &gizmoMode))
        {
            const Entity* entity = m_project->entity(m_selected);
            if (entity && entity->rotPointCustom())
            {
                if (gizmoMode == GizmoMode::AxisX)
                    gizmoMode = GizmoMode::PivotX;
                else if (gizmoMode == GizmoMode::AxisY)
                    gizmoMode = GizmoMode::PivotY;
            }

            m_gizmoMode = gizmoMode;
            m_dragging = true;
            m_dragEntity = m_selected;
            m_gizmoCenter = entityPivotScreen(*entity);
            m_gizmoStartMouse = event->position();
            m_gizmoStartAngle = std::atan2(event->position().y() - m_gizmoCenter.y(),
                                          event->position().x() - m_gizmoCenter.x())
                                * 180.0 / M_PI;
            m_gizmoStartRotation = m_project->value(m_selected, "rotation", m_frame);
            if (gizmoMode == GizmoMode::PivotFree || gizmoMode == GizmoMode::PivotX
                || gizmoMode == GizmoMode::PivotY)
            {
                m_gizmoStartX = m_project->value(m_selected, "rotPointX", m_frame);
                m_gizmoStartY = m_project->value(m_selected, "rotPointY", m_frame);
            }
            else
            {
                m_gizmoStartX = m_project->value(m_selected, "x", m_frame);
                m_gizmoStartY = m_project->value(m_selected, "y", m_frame);
            }
            m_dragLast = event->position();
            return;
        }
    }

    const Entity* hit = hitTest(event->position());
    if (hit)
    {
        m_selected = hit->id;
        emit entitySelected(hit->id);
        update();   // show the gizmo immediately, even without dragging
        if (!hit->locked)
        {
            m_dragging = true;
            m_dragEntity = hit->id;
            m_dragLast = event->position();
            m_gizmoMode = GizmoMode::Move;
            m_gizmoStartMouse = event->position();
            m_gizmoStartX = m_project->value(hit->id, "x", m_frame);
            m_gizmoStartY = m_project->value(hit->id, "y", m_frame);
        }
    }
    else
    {
        m_selected = kInvalidEntity;
        emit entitySelected(kInvalidEntity);
        update();
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (m_cameraPart != CameraPart::None)
    {
        applyCameraDrag(event->position(), event->modifiers());
        return;
    }

    if (m_panning)
    {
        m_pan += event->position() - m_panLast;
        m_panLast = event->position();
        emit viewChanged();
        update();
        return;
    }

    if (!m_dragging)
        return;

    const double s = viewScale();

    if (m_gizmoMode == GizmoMode::Scale)
    {
        applyHandleDrag(event->position(), event->modifiers() & Qt::ShiftModifier,
                        !(event->modifiers() & Qt::ControlModifier));
        return;
    }

    if (m_gizmoMode == GizmoMode::ComicTip)
    {
        const Entity* entity = m_project->entity(m_dragEntity);
        if (!entity)
            return;
        const QPointF local = screenToEntityLocal(*entity, event->position());
        const ComicBox box = ComicBox::build(TextStyle::fromData(entity->data),
                                              BubbleStyle::fromData(entity->data));
        const QSizeF body = box.bodySize();
        if (body.width() < 1 || body.height() < 1)
            return;
        emit comicTipEdited(m_dragEntity, local.x() / (body.width() / 2.0),
                             local.y() / (body.height() / 2.0));
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotFree)
    {
        double px = m_gizmoStartX + (event->position().x() - m_gizmoStartMouse.x()) / s;
        double py = m_gizmoStartY + (event->position().y() - m_gizmoStartMouse.y()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleX =
                    std::abs(m_project->value(m_dragEntity, "scaleX", m_frame));
                const double scaleY =
                    std::abs(m_project->value(m_dragEntity, "scaleY", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfW = bounds.width() / 2.0 * scaleX;
                const double halfH = bounds.height() / 2.0 * scaleY;
                px = snapTo(px, {0.0, -halfW, halfW}, threshold);
                py = snapTo(py, {0.0, -halfH, halfH}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity, px, py);
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotX)
    {
        double px = m_gizmoStartX + (event->position().x() - m_gizmoStartMouse.x()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleX =
                    std::abs(m_project->value(m_dragEntity, "scaleX", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfW = bounds.width() / 2.0 * scaleX;
                px = snapTo(px, {0.0, -halfW, halfW}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity, px,
                         m_project->value(m_dragEntity, "rotPointY", m_frame));
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotY)
    {
        double py = m_gizmoStartY + (event->position().y() - m_gizmoStartMouse.y()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleY =
                    std::abs(m_project->value(m_dragEntity, "scaleY", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfH = bounds.height() / 2.0 * scaleY;
                py = snapTo(py, {0.0, -halfH, halfH}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity,
                         m_project->value(m_dragEntity, "rotPointX", m_frame), py);
        return;
    }

    if (m_gizmoMode == GizmoMode::AxisX)
    {
        const double total = (event->position().x() - m_gizmoStartMouse.x()) / s;
        double x = m_gizmoStartX + total;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
            const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
            const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
            const double radians = rotation * M_PI / 180.0;
            const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfW =
                entity ? entityLocalBounds(*entity).width() / 2.0 * std::abs(scaleX) : 0.0;
            const double halfH =
                entity ? entityLocalBounds(*entity).height() / 2.0 * std::abs(scaleY) : 0.0;
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfW = halfW * cosA + halfH * sinA;
            const double halfWForSnap = aabbHalfW;

            const double visual = x - rotatedAx;
            if (std::abs(visual) <= threshold)
                x -= visual;
            else if (std::abs(visual - (-canvasHalfW + halfWForSnap)) <= threshold)
                x += (-canvasHalfW + halfWForSnap) - visual;
            else if (std::abs(visual - (canvasHalfW - halfWForSnap)) <= threshold)
                x += (canvasHalfW - halfWForSnap) - visual;
            else if (m_edgeCenterSnap && std::abs(visual - halfWForSnap) <= threshold)
                x += halfWForSnap - visual;
            else if (m_edgeCenterSnap && std::abs(visual + halfWForSnap) <= threshold)
                x += -halfWForSnap - visual;
        }
        emit valueEdited(m_dragEntity, QStringLiteral("x"), x);
        return;
    }

    if (m_gizmoMode == GizmoMode::AxisY)
    {
        const double total = (event->position().y() - m_gizmoStartMouse.y()) / s;
        double y = m_gizmoStartY + total;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfH = m_project->height / 2.0;
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
            const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
            const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
            const double radians = rotation * M_PI / 180.0;
            const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfH =
                entity ? entityLocalBounds(*entity).height() / 2.0 * std::abs(scaleY) : 0.0;
            const double halfW =
                entity ? entityLocalBounds(*entity).width() / 2.0 * std::abs(scaleX) : 0.0;
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfH = halfW * sinA + halfH * cosA;
            const double halfHForSnap = aabbHalfH;

            const double visual = y - rotatedAy;
            if (std::abs(visual) <= threshold)
                y -= visual;
            else if (std::abs(visual - (-canvasHalfH + halfHForSnap)) <= threshold)
                y += (-canvasHalfH + halfHForSnap) - visual;
            else if (std::abs(visual - (canvasHalfH - halfHForSnap)) <= threshold)
                y += (canvasHalfH - halfHForSnap) - visual;
            else if (m_edgeCenterSnap && std::abs(visual - halfHForSnap) <= threshold)
                y += halfHForSnap - visual;
            else if (m_edgeCenterSnap && std::abs(visual + halfHForSnap) <= threshold)
                y += -halfHForSnap - visual;
        }
        emit valueEdited(m_dragEntity, QStringLiteral("y"), y);
        return;
    }

    if (m_gizmoMode == GizmoMode::Rotate)
    {
        const double angle = std::atan2(event->position().y() - m_gizmoCenter.y(),
                                         event->position().x() - m_gizmoCenter.x())
                             * 180.0 / M_PI;
        double delta = angle - m_gizmoStartAngle;
        while (delta > 180.0)
            delta -= 360.0;
        while (delta < -180.0)
            delta += 360.0;

        double rotation = m_gizmoStartRotation + delta;
        if (!(event->modifiers() & Qt::ControlModifier))
        {
            constexpr double tolerance = 3.0;
            for (int snapAngle : Settings::snapAngles())
            {
                // Compare on the circle so 268° still snaps to 270°.
                const double diff =
                    std::remainder(rotation - double(snapAngle), 360.0);
                if (std::abs(diff) <= tolerance)
                {
                    rotation = snapAngle;
                    break;
                }
            }
        }
        emit valueEdited(m_dragEntity, QStringLiteral("rotation"), rotation);
        return;
    }

    const double totalX = (event->position().x() - m_gizmoStartMouse.x()) / s;
    const double totalY = (event->position().y() - m_gizmoStartMouse.y()) / s;

    const Entity* moving = m_project->entity(m_dragEntity);
    const bool parented = moving && moving->parent != kInvalidEntity;
    double localDeltaX = totalX;
    double localDeltaY = totalY;
    if (parented)
    {
        const QTransform parent =
            SceneRenderer::parentWorldTransform(*m_project, *moving, m_frame);
        const QPointF origin = parent.map(QPointF(0, 0));
        const QPointF mapped = parent.map(QPointF(totalX, totalY));
        localDeltaX = mapped.x() - origin.x();
        localDeltaY = mapped.y() - origin.y();
    }

    double x = m_gizmoStartX + localDeltaX;
    double y = m_gizmoStartY + localDeltaY;

    m_showGuideX = false;
    m_showGuideY = false;

    const bool free = event->modifiers() & Qt::ControlModifier;
    bool lockX = false;
    bool lockY = false;
    if (!parented && !free && std::abs(totalX) > 1e-6
        && std::abs(totalY) > 1e-6)
    {
        if (std::abs(totalX) >= std::abs(totalY) * 4.0)
        {
            y = m_gizmoStartY;   // horizontal drag
            lockY = true;
        }
        else if (std::abs(totalY) >= std::abs(totalX) * 4.0)
        {
            x = m_gizmoStartX;   // vertical drag
            lockX = true;
        }
    }

    if (!parented && !free && m_snapPixels > 0)
    {
        const double threshold = m_snapPixels / s;
        const double canvasHalfW = m_project->width / 2.0;
        const double canvasHalfH = m_project->height / 2.0;
        const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
        const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
        const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
        const double radians = rotation * M_PI / 180.0;
        const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
        const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
        const Entity* entity = m_project->entity(m_dragEntity);
        if (entity)
        {
            const QRectF bounds = entityLocalBounds(*entity);
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const double halfW = bounds.width() / 2.0 * std::abs(scaleX);
            const double halfH = bounds.height() / 2.0 * std::abs(scaleY);
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfW = halfW * cosA + halfH * sinA;
            const double aabbHalfH = halfW * sinA + halfH * cosA;

            const auto snapVisual = [&](double pos, double offset, double half,
                                        double canvasHalf) {
                const double visual = pos - offset;
                if (std::abs(visual) <= threshold)
                    return pos - visual;
                if (std::abs(visual - (-canvasHalf + half)) <= threshold)
                    return pos + (-canvasHalf + half) - visual;
                if (std::abs(visual - (canvasHalf - half)) <= threshold)
                    return pos + (canvasHalf - half) - visual;
                if (m_edgeCenterSnap && std::abs(visual - half) <= threshold)
                    return pos + half - visual;
                if (m_edgeCenterSnap && std::abs(visual + half) <= threshold)
                    return pos - half - visual;
                return pos;
            };

            if (!lockX)
                x = snapVisual(x, rotatedAx, aabbHalfW, canvasHalfW);
            if (!lockY)
                y = snapVisual(y, rotatedAy, aabbHalfH, canvasHalfH);

            // Align to the edges of other axis-aligned entities.
            VisualBounds mine = visualBoundsOf(*entity, x, y);
            bool snappedX = false;
            bool snappedY = false;
            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden || snappedX && snappedY)
                    continue;

                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds target =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));

                if (!lockX && !snappedX)
                {
                    const double myEdges[3] = {mine.left, (mine.left + mine.right) / 2.0,
                                               mine.right};
                    const double targetEdges[3] = {target.left,
                                                   (target.left + target.right) / 2.0,
                                                   target.right};
                    for (double myEdge : myEdges)
                    {
                        bool matched = false;
                        for (double targetEdge : targetEdges)
                        {
                            if (std::abs(myEdge - targetEdge) <= threshold)
                            {
                                x += targetEdge - myEdge;
                                m_guideX = targetEdge;
                                m_showGuideX = true;
                                snappedX = true;
                                matched = true;
                                break;
                            }
                        }
                        if (matched)
                            break;
                    }
                }

                if (!lockY && !snappedY)
                {
                    const double myEdges[3] = {mine.top, (mine.top + mine.bottom) / 2.0,
                                               mine.bottom};
                    const double targetEdges[3] = {target.top,
                                                   (target.top + target.bottom) / 2.0,
                                                   target.bottom};
                    for (double myEdge : myEdges)
                    {
                        bool matched = false;
                        for (double targetEdge : targetEdges)
                        {
                            if (std::abs(myEdge - targetEdge) <= threshold)
                            {
                                y += targetEdge - myEdge;
                                m_guideY = targetEdge;
                                m_showGuideY = true;
                                snappedY = true;
                                matched = true;
                                break;
                            }
                        }
                        if (matched)
                            break;
                    }
                }

                if (snappedX || snappedY)
                    mine = visualBoundsOf(*entity, x, y);
            }

            if (lockY)
            {
                m_guideY = (mine.top + mine.bottom) / 2.0;
                m_showGuideY = true;
            }
            if (lockX)
            {
                m_guideX = (mine.left + mine.right) / 2.0;
                m_showGuideX = true;
            }
        }
    }

    emit positionEdited(m_dragEntity, x, y);
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        unsetCursor();
    }
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        m_dragEntity = kInvalidEntity;
        m_gizmoMode = GizmoMode::None;
        m_activeHandle = Handle::None;
        m_cameraPart = CameraPart::None;
        m_showGuideX = false;
        m_showGuideY = false;
        unsetCursor();
        update();
    }
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    const double s0 = viewScale();
    const QPointF before = screenToCanvas(event->position());
    m_zoom = std::clamp(m_zoom * std::pow(1.15, steps), 0.1, 8.0);
    const double s1 = viewScale();
    m_pan += before * (s0 - s1);
    emit viewChanged();
    update();
}

void Viewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit viewChanged();
}

} // namespace pvf
