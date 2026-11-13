#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "render2/Render.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace pvf {

// ---- session camera viewfinder ------------------------------------------

QPolygonF Viewport::sessionCameraPolygon() const
{
    if (!m_session || !m_showCameraFrame)
        return QPolygonF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QPolygonF();
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd()
        || it->content.kind != ContentKind::Camera || it->hidden)
        return QPolygonF();

    const double width = double(std::max(1, project.settings.width));
    const double height = double(std::max(1, project.settings.height));
    // Camera-local corners are Y-up too, matching render2::cameraView():
    // top-left is (-w/2, +h/2).  The widget transform performs the flip.
    QPolygonF source;
    source << QPointF(-width / 2.0, height / 2.0)
           << QPointF(width / 2.0, height / 2.0)
           << QPointF(width / 2.0, -height / 2.0)
           << QPointF(-width / 2.0, -height / 2.0);

    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    const anim::Pose pose =
        anim::evaluateEntity(project, uuid, m_session->playhead(), world, cache);
    return sessionWorldToScreen().map(pose.world.map(source));
}

QPointF Viewport::sessionCameraScreenCenter() const
{
    if (!m_session)
        return QPointF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QPointF();
    const model::Project& project = m_session->project();
    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    const anim::Pose pose = anim::evaluateEntity(
        project, uuid, m_session->playhead(), world, cache);
    return sessionWorldToScreen().map(pose.world.map(QPointF(0.0, 0.0)));
}

QPointF Viewport::sessionCameraRotationHandle() const
{
    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.size() < 4)
        return QPointF();
    const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
    const QPointF center = sessionCameraScreenCenter();
    QPointF up = topCenter - center;
    const double length = std::hypot(up.x(), up.y());
    if (length < 1e-6)
        return topCenter;
    return topCenter + up / length * 28.0;
}

QRectF Viewport::sessionCameraTitleRect() const
{
    if (!m_session)
        return QRectF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QRectF();
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return QRectF();
    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.isEmpty())
        return QRectF();
    const QRectF box = polygon.boundingRect();
    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(it->name);
    const QRectF title(box.left(), box.top() - 24.0, textWidth + 16.0, 20.0);
    if (title.top() < 2.0)
        return QRectF(box.left(), box.top() + 4.0, textWidth + 16.0, 20.0);
    return title;
}

Viewport::CameraPart Viewport::sessionCameraHitTest(const QPointF& pos) const
{
    if (!m_showCameraFrame || !m_session)
        return CameraPart::None;
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return CameraPart::None;
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd()
        || it->content.kind != ContentKind::Camera)
        return CameraPart::None;

    const bool selected =
        m_session->document() && m_session->document()->selection() == uuid;
    if (selected && !it->locked && it->parentUuid.isEmpty())
    {
        if (QLineF(pos, sessionCameraRotationHandle()).length() <= 10.0)
            return CameraPart::Rotate;

        const QPolygonF polygon = sessionCameraPolygon();
        for (int i = 0; i < polygon.size(); ++i)
        {
            if (QLineF(pos, polygon[i]).length() <= 8.0)
                return CameraPart(i == 0 ? CameraPart::Corner0
                                  : i == 1 ? CameraPart::Corner1
                                  : i == 2 ? CameraPart::Corner2
                                           : CameraPart::Corner3);
        }
    }

    if (sessionCameraTitleRect().contains(pos))
        return CameraPart::Move;

    const QPolygonF polygon = sessionCameraPolygon();
    for (int i = 0; i < polygon.size(); ++i)
    {
        if (ui::detail::distanceToSegment(
                pos, polygon[i], polygon[(i + 1) % polygon.size()])
            <= 6.0)
            return CameraPart::Move;
    }
    return CameraPart::None;
}

bool Viewport::handleSessionCameraPress(QMouseEvent* event)
{
    if (!m_session || event->button() != Qt::LeftButton)
        return false;

    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return false;
    const CameraPart part = sessionCameraHitTest(event->position());
    if (part == CameraPart::None)
        return false;

    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return false;
    if (m_session->document())
        m_session->document()->setSelection(uuid);
    emit uuidSelected(uuid);

    if (it->locked || !it->parentUuid.isEmpty())
        return true;

    m_sessionDragKind = part == CameraPart::Move
                            ? SessionDragKind::CameraMove
                            : part == CameraPart::Rotate
                                  ? SessionDragKind::CameraRotate
                                  : SessionDragKind::CameraScale;
    m_sessionDragUuid = uuid;
    m_sessionDragStart = it.value();
    m_sessionDragTransform = sessionGizmoTransform(it.value());
    m_sessionDragBounds = sessionGizmoBounds(it.value());
    m_sessionDragParentToWidget = sessionWorldToScreen();
    m_sessionDragViewScale = viewScale();
    m_sessionDragStartParent = sessionScreenToProject(event->position());
    // Keep the drag maths in model space (Y-up, CCW-positive): the camera
    // pivot is the pose origin, and the start angle is measured there.
    m_sessionDragPivotStart = gizmo::worldPivot(m_sessionDragTransform);
    m_sessionCameraStartAngle =
        std::atan2(m_sessionDragStartParent.y() - m_sessionDragPivotStart.y(),
                   m_sessionDragStartParent.x() - m_sessionDragPivotStart.x())
        * 180.0 / M_PI;
    collectSessionSnapTargets(uuid);
    m_sessionDragChanged = false;
    m_showGuideX = false;
    m_showGuideY = false;
    setCursor(part == CameraPart::Move ? Qt::SizeAllCursor
               : part == CameraPart::Rotate ? Qt::CrossCursor
                                            : Qt::SizeFDiagCursor);
    return true;
}

void Viewport::applySessionCameraDrag(const QPointF& pos,
                                      Qt::KeyboardModifiers modifiers)
{
    if (!m_session || m_sessionDragUuid.isEmpty())
        return;
    auto it = m_session->mutableProject().entities.find(m_sessionDragUuid);
    if (it == m_session->mutableProject().entities.end() || it->locked)
        return;
    model::Entity& entity = it.value();
    const gizmo::Transform start = m_sessionDragTransform;
    const QPointF mouse = sessionScreenToProject(pos);
    const QPointF delta = mouse - m_sessionDragStartParent;
    const bool snapActive = !modifiers.testFlag(Qt::ControlModifier);
    m_showGuideX = false;
    m_showGuideY = false;

    if (m_sessionDragKind == SessionDragKind::CameraMove)
    {
        double x = start.x + delta.x();
        double y = start.y + delta.y();
        if (snapActive)
        {
            const next::AppSettings& settings = m_session->appSettings();
            const double threshold =
                double(std::max(1, settings.snapPixels)) / viewScale();
            const QPointF center = QPointF(start.x + start.pivotX,
                                           start.y + start.pivotY);
            // Model origin is the canvas centre.
            const double canvasCenterX = 0.0;
            const double canvasCenterY = 0.0;
            if (settings.snapCanvasCenter)
            {
                if (std::abs(center.x() - canvasCenterX) <= threshold)
                {
                    x += canvasCenterX - center.x();
                    m_showGuideX = true;
                    m_guideX = canvasCenterX;
                }
                if (std::abs(center.y() - canvasCenterY) <= threshold)
                {
                    y += canvasCenterY - center.y();
                    m_showGuideY = true;
                    m_guideY = canvasCenterY;
                }
            }
            if (!m_sessionSnapTargetsX.isEmpty())
            {
                const auto snap = ui::detail::nearestFeatureSnap(
                    {center.x(), center.x(), center.x()}, m_sessionSnapTargetsX,
                    threshold);
                if (snap)
                {
                    x += *snap;
                    m_showGuideX = true;
                    m_guideX = center.x() + *snap;
                }
            }
            if (!m_sessionSnapTargetsY.isEmpty())
            {
                const auto snap = ui::detail::nearestFeatureSnap(
                    {center.y(), center.y(), center.y()}, m_sessionSnapTargetsY,
                    threshold);
                if (snap)
                {
                    y += *snap;
                    m_showGuideY = true;
                    m_guideY = center.y() + *snap;
                }
            }
        }
        writeSessionGizmoValue(entity, QStringLiteral("x"), x);
        writeSessionGizmoValue(entity, QStringLiteral("y"), y);
    }
    else if (m_sessionDragKind == SessionDragKind::CameraRotate)
    {
        // Model space: +Y up, positive angles counter-clockwise.  The drag
        // delta is measured about the model pivot, so a clockwise drag lowers
        // the angle exactly like the entity gizmo.
        const QPointF center = m_sessionDragPivotStart;
        const double angle =
            std::atan2(mouse.y() - center.y(), mouse.x() - center.x()) * 180.0
            / M_PI;
        double rotationDelta = angle - m_sessionCameraStartAngle;
        while (rotationDelta > 180.0)
            rotationDelta -= 360.0;
        while (rotationDelta < -180.0)
            rotationDelta += 360.0;
        double rotation = start.rotation + rotationDelta;
        if (snapActive)
            rotation = snapSessionAngle(rotation,
                                        modifiers.testFlag(Qt::ShiftModifier));
        writeSessionGizmoValue(entity, QStringLiteral("rotation"), rotation);
    }
    else if (m_sessionDragKind == SessionDragKind::CameraScale)
    {
        const QPointF center = QPointF(start.x + start.pivotX,
                                       start.y + start.pivotY);
        const QPointF local =
            ui::detail::rotateVector(mouse - center, -start.rotation);
        const double width = std::max(1, m_session->settings().width);
        const double height = std::max(1, m_session->settings().height);
        double scale = std::max(std::abs(local.x()) * 2.0 / width,
                                std::abs(local.y()) * 2.0 / height);
        scale = std::clamp(scale, 0.05, 1.0);
        writeSessionGizmoValue(entity, QStringLiteral("scaleX"), scale);
        writeSessionGizmoValue(entity, QStringLiteral("scaleY"), scale);
    }
    else
    {
        return;
    }

    m_sessionDragChanged =
        !ui::detail::transformsEqual(sessionGizmoTransform(entity), start);
    invalidateSessionImage();
    update();
}

void Viewport::drawSessionCameraOverlay(QPainter& painter)
{
    if (!m_session || !m_showCameraFrame)
        return;
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return;
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd()
        || it->content.kind != ContentKind::Camera)
        return;

    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.isEmpty())
        return;

    const bool selected =
        m_session->document() && m_session->document()->selection() == uuid;
    const bool locked = it->locked;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath veil;
    veil.setFillRule(Qt::OddEvenFill);
    veil.addRect(QRectF(rect()));
    veil.addPolygon(polygon);
    veil.closeSubpath();
    painter.fillPath(veil, QColor(10, 10, 14, 140));

    QPen framePen(QColor(90, 210, 255, selected ? 255 : 200),
                  selected ? 2.0 : 1.2, Qt::DashLine);
    if (locked)
        framePen.setColor(QColor(150, 150, 160, 200));
    painter.setPen(framePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);

    const QRectF title = sessionCameraTitleRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 20, 26, 225));
    painter.drawRect(title);
    painter.setPen(QColor(90, 210, 255, 220));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(title);
    painter.setPen(locked ? QColor(170, 170, 180) : QColor(235, 245, 255));
    painter.drawText(title.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     it->name);

    if (selected && !locked && it->parentUuid.isEmpty())
    {
        painter.setPen(QPen(QColor(20, 20, 26), 1.0));
        painter.setBrush(QColor(90, 210, 255));
        for (const QPointF& corner : polygon)
            painter.drawRect(QRectF(corner.x() - 4.0, corner.y() - 4.0, 8.0, 8.0));

        const QPointF handle = sessionCameraRotationHandle();
        const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
        painter.setPen(QPen(QColor(90, 210, 255), 1.5));
        painter.drawLine(topCenter, handle);
        painter.setBrush(QColor(20, 24, 30));
        painter.drawEllipse(handle, 5.0, 5.0);
    }

    painter.restore();
}

} // namespace pvf
