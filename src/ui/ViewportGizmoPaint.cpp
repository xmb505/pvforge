#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "app/EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"

#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace pvf::ui::detail {

// Inkscape's draw_carrow(): one closed path with a 90-degree outer arc, a
// 90-degree inner arc and two triangular arrow heads.  The control points are
// the same as src/display/control/ctrl-handle-rendering.cpp, translated so the
// glyph is centred on the origin of its own S x S box.
QPainterPath carrowGlyphPath()
{
    const double size = gizmo::kRotateHandleSize;
    const double delta = std::round(1.5 + (size - 3.0) / 4.0) - 1.5;
    const double arcCenterX = 1.5 + delta;
    const double arcCenterY = size - 1.5 - delta;
    const double outerRadius = size - 3.0 - 1.5 * delta;
    const double innerRadius = size - 3.0 - 2.5 * delta;

    QPainterPath path;
    // First arrow head: tip, outer side, inner notch (= outer arc start).
    path.moveTo(1.5, 1.5 + delta);
    path.lineTo(arcCenterX, 1.5);
    path.lineTo(arcCenterX, arcCenterY - outerRadius);
    // 90-degree outer arc from 12 o'clock to 3 o'clock (Qt angles: 90 -> 0).
    path.arcTo(QRectF(arcCenterX - outerRadius, arcCenterY - outerRadius,
                      2.0 * outerRadius, 2.0 * outerRadius),
               90.0, -90.0);
    // Second arrow head: tip, outer side, inner notch (= inner arc start).
    path.lineTo(arcCenterX + outerRadius - delta / 2.0, arcCenterY + delta);
    path.lineTo(arcCenterX + outerRadius + delta, arcCenterY);
    path.lineTo(arcCenterX + innerRadius, arcCenterY);
    // 90-degree inner arc back from 3 o'clock to 12 o'clock.
    path.arcTo(QRectF(arcCenterX - innerRadius, arcCenterY - innerRadius,
                      2.0 * innerRadius, 2.0 * innerRadius),
               0.0, 90.0);
    path.lineTo(1.5, 1.5 + delta);
    path.closeSubpath();
    return path.translated(-size / 2.0, -size / 2.0);
}

void drawRotateGlyph(QPainter& painter, QPointF center, double angleDegrees,
                     bool highlighted)
{
    static const QPainterPath path = carrowGlyphPath();
    painter.save();
    painter.translate(center);
    painter.rotate(angleDegrees);
    // Dark halo under a light fill: the old UI's light-icon/dark-stroke style
    // stays readable on white entities while the light core shows on dark
    // backgrounds (same trick as Inkscape's outline).
    painter.setPen(QPen(QColor(18, 21, 26), 2.2, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.setPen(QPen(QColor(18, 21, 26), 1.0, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(highlighted ? QColor(116, 206, 255) : QColor(246, 249, 255));
    painter.drawPath(path);
    painter.restore();
}

// Rotation cursor drawn at runtime: no new SVG/resource is added to the UI.
QCursor rotateCursor()
{
    static const QCursor cursor = [] {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPointF center(12.0, 12.0);
        const double radius = 7.0;
        const double startDegrees = 55.0;
        const QRectF circle(center.x() - radius, center.y() - radius,
                            2.0 * radius, 2.0 * radius);
        const auto paintArrow = [&](const QColor& color, double width) {
            painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(circle, int(startDegrees * 16.0), int(270.0 * 16.0));

            const double radians = qDegreesToRadians(startDegrees);
            const QPointF tangent(-std::sin(radians), -std::cos(radians));
            const QPointF normal(-tangent.y(), tangent.x());
            const QPointF end = center
                                + QPointF(std::cos(radians) * radius,
                                          -std::sin(radians) * radius);
            QPolygonF head;
            head << end + tangent * 4.5 << end + normal * 2.8 - tangent * 1.2
                 << end - normal * 2.8 - tangent * 1.2;
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(head);
        };
        paintArrow(QColor(18, 21, 26), 3.6);
        paintArrow(QColor(255, 255, 255), 1.6);
        painter.end();
        return QCursor(pixmap, 12, 12);
    }();
    return cursor;
}

void drawRotationOriginMarker(QPainter& painter, QPointF center)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(18, 21, 26), 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(center, 5.0, 5.0);
    painter.drawLine(center + QPointF(-9.0, 0.0), center + QPointF(9.0, 0.0));
    painter.drawLine(center + QPointF(0.0, -9.0), center + QPointF(0.0, 9.0));
    painter.setPen(QPen(QColor(255, 255, 255), 1.2, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(center, 5.0, 5.0);
    painter.drawLine(center + QPointF(-9.0, 0.0), center + QPointF(9.0, 0.0));
    painter.drawLine(center + QPointF(0.0, -9.0), center + QPointF(0.0, 9.0));
}

void drawRotationGripMarker(QPainter& painter, QPointF center)
{
    painter.setPen(QPen(QColor(18, 21, 26), 2.4));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(center, 3.2, 3.2);
    painter.setPen(QPen(QColor(18, 21, 26), 1.0));
    painter.drawEllipse(center, 3.2, 3.2);
}

} // namespace pvf::ui::detail

namespace pvf {

void Viewport::drawSessionGizmo(QPainter& painter)
{
    if (!m_session || !m_session->document())
        return;
    const Uuid selected = m_session->document()->selection();
    const auto it = m_session->project().entities.constFind(selected);
    if (it == m_session->project().entities.constEnd() || it->locked
        || it->content.kind == ContentKind::Camera)
        return;

    const model::Entity& entity = it.value();
    const SessionGizmoGeometry geometry = sessionGizmoGeometry(entity);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (entity.hidden)
        painter.setOpacity(0.45);

    // Inkscape hides the selection box and every handle during a rotation
    // drag; only the rotation origin and a cursor marker remain.
    if (m_sessionDragKind == SessionDragKind::Rotate)
    {
        ui::detail::drawRotationOriginMarker(
            painter, geometry.parentToWidget.map(m_sessionDragRotateOrigin));
        ui::detail::drawRotationGripMarker(painter, m_sessionDragLastWidget);
        painter.restore();
        return;
    }

    painter.setPen(QPen(QColor(116, 206, 255, 175), 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(geometry.box);

    ui::detail::drawArrowLine(painter, geometry.pivot, geometry.moveX,
                              QColor(255, 122, 122));
    ui::detail::drawArrowLine(painter, geometry.pivot, geometry.moveY,
                              QColor(125, 255, 158));

    painter.setPen(QPen(QColor(18, 21, 26), 1.0));
    painter.setBrush(QColor(246, 249, 255));
    for (int corner = 0; corner < 4; ++corner)
    {
        const QPointF position = geometry.scaleCenter[corner];
        painter.drawRect(QRectF(position.x() - 3.5, position.y() - 3.5, 7.0, 7.0));
    }

    painter.setPen(QPen(QColor(116, 206, 255), 1.2));
    painter.setBrush(QColor(34, 46, 58));
    painter.drawRect(QRectF(geometry.centerScale.x() - 4.5,
                            geometry.centerScale.y() - 4.5, 9.0, 9.0));
    painter.setPen(QPen(QColor(116, 206, 255, 90), 1.0, Qt::DotLine));
    painter.drawLine(geometry.pivot, geometry.centerScale);

    for (int visual = 0; visual < 4; ++visual)
    {
        const gizmo::Handle handle = gizmo::rotateHandleForVisualCorner(visual);
        const bool highlighted = m_hoveredRotateHandle
                                 && *m_hoveredRotateHandle == handle;
        ui::detail::drawRotateGlyph(painter, geometry.rotateCenter[visual],
                                    geometry.rotateAngle[visual], highlighted);
    }

    painter.setPen(QPen(QColor(18, 21, 26), 1.5));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(geometry.pivot, 4.0, 4.0);

    painter.restore();
}

} // namespace pvf
