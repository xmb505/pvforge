#include "Viewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pvf {

Viewport::Viewport(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
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

const Entity* Viewport::hitTest(const QPointF& pos) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const auto& entities = m_project->entities();

    // Topmost first.
    for (int i = entities.size() - 1; i >= 0; --i)
    {
        const Entity& e = entities[i];
        if (e.type != EntityType::Image || e.image.isNull())
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
        t.scale(sx, sy);

        bool ok = false;
        const QTransform inv = t.inverted(&ok);
        if (!ok)
            continue;

        const QPointF local = inv.map(pos);
        const double halfW = e.image.width() * s / 2.0;
        const double halfH = e.image.height() * s / 2.0;
        if (std::abs(local.x()) <= halfW && std::abs(local.y()) <= halfH)
            return &e;
    }
    return nullptr;
}

void Viewport::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));

    const QRectF canvas = canvasRect();
    const double s = viewScale();

    // Canvas drop shadow and border.
    p.fillRect(canvas.translated(6, 6), QColor(0, 0, 0, 90));
    p.fillRect(canvas, m_project->background);
    p.setPen(QPen(QColor(70, 70, 80), 1));
    p.drawRect(canvas);

    // Center guides.
    p.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    p.drawLine(QPointF(canvas.center().x(), canvas.top()), QPointF(canvas.center().x(), canvas.bottom()));
    p.drawLine(QPointF(canvas.left(), canvas.center().y()), QPointF(canvas.right(), canvas.center().y()));

    for (const Entity& e : m_project->entities())
    {
        if (e.type != EntityType::Image || e.image.isNull())
            continue;

        const double x = m_project->value(e.id, "x", m_frame);
        const double y = m_project->value(e.id, "y", m_frame);
        const double rot = m_project->value(e.id, "rotation", m_frame);
        const double sx = m_project->value(e.id, "scaleX", m_frame);
        const double sy = m_project->value(e.id, "scaleY", m_frame);
        const double opacity = std::clamp(m_project->value(e.id, "opacity", m_frame), 0.0, 1.0);
        if (opacity <= 0.0 || std::abs(sx) < 1e-9 || std::abs(sy) < 1e-9)
            continue;

        const QPointF center = canvas.center() + QPointF(x * s, y * s);
        const QRectF dest(-e.image.width() * s / 2.0, -e.image.height() * s / 2.0,
                          e.image.width() * s, e.image.height() * s);

        p.save();
        p.setOpacity(opacity);
        p.translate(center);
        p.rotate(rot);
        p.scale(sx, sy);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dest, e.image);
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

    // Empty state hint.
    if (m_project->entities().isEmpty())
    {
        p.setPen(QColor(150, 150, 160));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("按 Ctrl+I 导入图片\n拖动图片摆位，属性修改会自动打关键帧"));
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

    const Entity* hit = hitTest(event->position());
    if (hit)
    {
        m_selected = hit->id;
        emit entitySelected(hit->id);
        m_dragging = true;
        m_dragEntity = hit->id;
        m_dragLast = event->position();
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
