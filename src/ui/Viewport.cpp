#include "Viewport.h"

#include "app/AppIcon.h"

#include <QApplication>
#include <QDebug>
#include <QFontMetricsF>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
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

} // namespace


Viewport::Viewport(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

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

void Viewport::resetView()
{
    m_zoom = 1.0;
    m_pan = QPointF();
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

QRectF Viewport::entityLocalBounds(const Entity& e) const
{
    switch (e.type)
    {
    case EntityType::Image:
    case EntityType::Video:
        if (!e.image.isNull())
            return QRectF(-e.image.width() / 2.0, -e.image.height() / 2.0,
                          e.image.width(), e.image.height());
        return QRectF(-40, -40, 80, 80);

    case EntityType::Text:
    {
        QFont font(e.fontFamily());
        font.setPixelSize(std::max(1, e.fontSize()));
        const QString text = e.textContent();
        if (text.isEmpty())
            return QRectF(-10, -10, 20, 20);
        const QFontMetricsF metrics(font);
        const QRectF bounds =
            metrics.boundingRect(QRectF(0, 0, 4000, 4000), Qt::TextWordWrap, text);
        if (bounds.width() < 1 || bounds.height() < 1)
            return QRectF(-10, -10, 20, 20);
        // A little padding so glyph overhang and word-wrap rounding never clip.
        const double pad = 6.0;
        return QRectF(-bounds.width() / 2.0 - pad, -bounds.height() / 2.0 - pad,
                      bounds.width() + pad * 2.0, bounds.height() + pad * 2.0);
    }

    case EntityType::Vector:
    {
        const double width = std::max(1.0, e.vectorWidth());
        const double height =
            e.vectorShape() == 2 ? std::max(4.0, e.strokeWidth())
                                 : std::max(1.0, e.vectorHeight());
        return QRectF(-width / 2.0, -height / 2.0, width, height);
    }
    }
    return QRectF();
}

const Entity* Viewport::hitTest(const QPointF& pos) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const auto& entities = m_project->entities();

    // Topmost first.
    for (int i = entities.size() - 1; i >= 0; --i)
    {
        const Entity& e = entities[i];
        const QRectF bounds = entityLocalBounds(e);
        if (bounds.isEmpty())
            continue;

        const double x = m_project->value(e.id, "x", m_frame);
        const double y = m_project->value(e.id, "y", m_frame);
        const double rot = m_project->value(e.id, "rotation", m_frame);
        const double sx = m_project->value(e.id, "scaleX", m_frame);
        const double sy = m_project->value(e.id, "scaleY", m_frame);
        if (std::abs(sx) < 1e-9 || std::abs(sy) < 1e-9)
            continue;

        QTransform t;
        t.translate(canvas.center().x() + x * s, canvas.center().y() + y * s);
        t.rotate(rot);
        t.scale(sx * s, sy * s);

        bool ok = false;
        const QTransform inv = t.inverted(&ok);
        if (!ok)
            continue;

        const QPointF local = inv.map(pos);
        if (bounds.contains(local))
            return &e;
    }
    return nullptr;
}

void Viewport::drawEntity(QPainter& p, const Entity& e, const QRectF& canvas, double s)
{
    if (e.hidden)
        return;

    const double x = m_project->value(e.id, "x", m_frame);
    const double y = m_project->value(e.id, "y", m_frame);
    const double rot = m_project->value(e.id, "rotation", m_frame);
    const double sx = m_project->value(e.id, "scaleX", m_frame);
    const double sy = m_project->value(e.id, "scaleY", m_frame);
    const double opacity = std::clamp(m_project->value(e.id, "opacity", m_frame), 0.0, 1.0);
    if (opacity <= 0.0 || std::abs(sx) < 1e-9 || std::abs(sy) < 1e-9)
        return;

    const QRectF bounds = entityLocalBounds(e);
    if (bounds.isEmpty())
        return;

    const QPointF center = canvas.center() + QPointF(x * s, y * s);
    const QRectF dest(bounds.x() * s, bounds.y() * s, bounds.width() * s,
                      bounds.height() * s);

    p.save();
    p.setOpacity(opacity);
    p.translate(center);
    p.rotate(rot);
    p.scale(sx, sy);

    switch (e.type)
    {
    case EntityType::Image:
    case EntityType::Video:
        if (!e.image.isNull())
        {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(dest, e.image);
        }
        break;

    case EntityType::Text:
    {
        QFont font(e.fontFamily());
        font.setPixelSize(std::max(1, int(std::lround(e.fontSize() * s))));
        p.setFont(font);
        p.setPen(e.textColor());
        p.drawText(dest, e.textAlign() | Qt::TextWordWrap, e.textContent());
        break;
    }

    case EntityType::Vector:
    {
        const double w = e.vectorWidth() * s;
        const double h = e.vectorHeight() * s;
        const double stroke = e.strokeWidth() * s;

        QBrush brush(e.fillColor());
        if (e.fillColor().alpha() == 0)
            brush = Qt::NoBrush;
        p.setBrush(brush);

        if (e.vectorShape() == 2)
        {
            QPen pen(e.strokeColor().alpha() > 0 ? e.strokeColor() : e.fillColor());
            pen.setWidthF(std::max(1.0, stroke));
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(QPointF(-w / 2.0, 0), QPointF(w / 2.0, 0));
        }
        else
        {
            if (stroke > 0)
            {
                QPen pen(e.strokeColor());
                pen.setWidthF(stroke);
                p.setPen(pen);
            }
            else
            {
                p.setPen(Qt::NoPen);
            }
            const QRectF rect(-w / 2.0, -h / 2.0, w, h);
            if (e.vectorShape() == 1)
                p.drawEllipse(rect);
            else
                p.drawRect(rect);
        }
        break;
    }
    }

    p.restore();

    if (e.id == m_selected)
    {
        p.save();
        p.translate(center);
        p.rotate(rot);
        p.scale(sx, sy);
        QPen pen(QColor(77, 163, 255));
        pen.setCosmetic(true);
        pen.setWidthF(1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(dest);
        p.restore();
    }
}

QPointF Viewport::entityScreenCenter(const Entity& e) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const double x = m_project->value(e.id, "x", m_frame);
    const double y = m_project->value(e.id, "y", m_frame);
    return canvas.center() + QPointF(x * s, y * s);
}

bool Viewport::gizmoHitTest(const QPointF& pos, GizmoMode* mode) const
{
    if (m_selected == kInvalidEntity)
        return false;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity)
        return false;

    const QPointF delta = pos - entityScreenCenter(*entity);
    const double distance = std::hypot(delta.x(), delta.y());

    // The rotation ring sits on top of the axes.
    if (std::abs(distance - kGizmoRingRadius) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::Rotate;
        return true;
    }

    // Close to the centre the entity itself is grabbed instead.
    if (distance <= kGizmoCenterDeadzone)
        return false;

    if (std::abs(delta.y()) <= kGizmoHitWidth && std::abs(delta.x()) <= kGizmoAxisLength)
    {
        *mode = GizmoMode::AxisX;
        return true;
    }
    if (std::abs(delta.x()) <= kGizmoHitWidth && std::abs(delta.y()) <= kGizmoAxisLength)
    {
        *mode = GizmoMode::AxisY;
        return true;
    }
    return false;
}

void Viewport::drawGizmo(QPainter& p, const Entity& e)
{
    const QPointF center = entityScreenCenter(e);
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

    // X axis.
    const bool hotX = m_gizmoMode == GizmoMode::AxisX;
    const QColor xColor = hotX ? QColor(255, 150, 150) : QColor(226, 86, 86);
    p.setPen(QPen(xColor, hotX ? 4.0 : 3.0));
    p.drawLine(center - QPointF(len, 0), center + QPointF(len, 0));
    arrow(center + QPointF(len, 0), center, xColor);
    arrow(center - QPointF(len, 0), center, xColor);

    // Y axis.
    const bool hotY = m_gizmoMode == GizmoMode::AxisY;
    const QColor yColor = hotY ? QColor(150, 235, 150) : QColor(96, 200, 96);
    p.setPen(QPen(yColor, hotY ? 4.0 : 3.0));
    p.drawLine(center - QPointF(0, len), center + QPointF(0, len));
    arrow(center + QPointF(0, len), center, yColor);
    arrow(center - QPointF(0, len), center, yColor);

    // Rotation ring.
    const bool hotRotate = m_gizmoMode == GizmoMode::Rotate;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(hotRotate ? QColor(255, 214, 120) : QColor(210, 200, 160),
                  hotRotate ? 3.5 : 2.0));
    p.drawEllipse(center, ring, ring);
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

    // Gizmo on top of the selected entity.
    if (m_selected != kInvalidEntity)
    {
        if (const Entity* entity = m_project->entity(m_selected))
            drawGizmo(p, *entity);
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

    // The gizmo takes priority over the entity body.
    GizmoMode gizmoMode = GizmoMode::None;
    if (gizmoHitTest(event->position(), &gizmoMode))
    {
        const Entity* entity = m_project->entity(m_selected);
        m_gizmoMode = gizmoMode;
        m_dragging = true;
        m_dragEntity = m_selected;
        m_gizmoCenter = entityScreenCenter(*entity);
        m_gizmoStartMouse = event->position();
        m_gizmoStartAngle = std::atan2(event->position().y() - m_gizmoCenter.y(),
                                      event->position().x() - m_gizmoCenter.x())
                            * 180.0 / M_PI;
        m_gizmoStartRotation = m_project->value(m_selected, "rotation", m_frame);
        m_gizmoStartX = m_project->value(m_selected, "x", m_frame);
        m_gizmoStartY = m_project->value(m_selected, "y", m_frame);
        m_dragLast = event->position();
        return;
    }

    const Entity* hit = hitTest(event->position());
    if (hit)
    {
        m_selected = hit->id;
        emit entitySelected(hit->id);
        if (!hit->locked)
        {
            m_dragging = true;
            m_dragEntity = hit->id;
            m_dragLast = event->position();
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
    if (m_panning)
    {
        m_pan += event->position() - m_panLast;
        m_panLast = event->position();
        update();
        return;
    }

    if (!m_dragging)
        return;

    const double s = viewScale();

    if (m_gizmoMode == GizmoMode::AxisX)
    {
        const double total = (event->position().x() - m_gizmoStartMouse.x()) / s;
        emit valueEdited(m_dragEntity, QStringLiteral("x"), m_gizmoStartX + total);
        return;
    }

    if (m_gizmoMode == GizmoMode::AxisY)
    {
        const double total = (event->position().y() - m_gizmoStartMouse.y()) / s;
        emit valueEdited(m_dragEntity, QStringLiteral("y"), m_gizmoStartY + total);
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
        emit valueEdited(m_dragEntity, QStringLiteral("rotation"),
                         m_gizmoStartRotation + delta);
        return;
    }

    const QPointF delta = (event->position() - m_dragLast) / s;
    m_dragLast = event->position();

    const double x = m_project->value(m_dragEntity, "x", m_frame) + delta.x();
    const double y = m_project->value(m_dragEntity, "y", m_frame) + delta.y();
    emit valueEdited(m_dragEntity, QStringLiteral("x"), x);
    emit valueEdited(m_dragEntity, QStringLiteral("y"), y);
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
    update();
}

} // namespace pvf
