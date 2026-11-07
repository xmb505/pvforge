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

// Snaps value to the nearest candidate within threshold.
double snapTo(double value, std::initializer_list<double> candidates, double threshold)
{
    for (double candidate : candidates)
        if (std::abs(value - candidate) <= threshold)
            return candidate;
    return value;
}

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
    const auto& entities = m_project->entities();

    // Topmost first.
    for (int i = entities.size() - 1; i >= 0; --i)
    {
        const Entity& e = entities[i];
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
    if (e.hidden)
        return;

    const double opacity = std::clamp(m_project->value(e.id, "opacity", m_frame), 0.0, 1.0);
    if (opacity <= 0.0)
        return;

    const QRectF bounds = entityLocalBounds(e);
    if (bounds.isEmpty())
        return;

    const QTransform transform = entityTransform(e);

    p.save();
    p.setOpacity(opacity);
    p.setTransform(transform);

    switch (e.type)
    {
    case EntityType::Image:
    case EntityType::Video:
        if (!e.image.isNull())
        {
            if (e.pixmap.isNull())
                e.pixmap = QPixmap::fromImage(e.image);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawPixmap(bounds, e.pixmap, QRectF(e.pixmap.rect()));
        }
        break;

    case EntityType::Text:
    {
        QFont font(e.fontFamily());
        font.setPixelSize(std::max(1, e.fontSize()));
        p.setFont(font);
        p.setPen(e.textColor());
        p.drawText(bounds, e.textAlign() | Qt::TextWordWrap, e.textContent());
        break;
    }

    case EntityType::Vector:
    {
        const double w = e.vectorWidth();
        const double h = e.vectorHeight();
        const double stroke = e.strokeWidth();

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
        p.setTransform(transform);
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
    const double x = m_project->value(e.id, "x", m_frame);
    const double y = m_project->value(e.id, "y", m_frame);
    return canvas.center() + QPointF(x * s, y * s);
}

QTransform Viewport::entityTransformForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? entityTransform(*entity) : QTransform();
}

QPointF Viewport::entityPivotScreen(const Entity& e) const
{
    QPointF point = entityScreenCenter(e);
    if (e.rotPointCustom())
    {
        const double s = viewScale();
        point += QPointF(m_project->value(e.id, "rotPointX", m_frame) * s,
                         m_project->value(e.id, "rotPointY", m_frame) * s);
    }
    return point;
}

QTransform Viewport::entityTransform(const Entity& e) const
{
    const double s = viewScale();
    const QPointF origin = entityScreenCenter(e);
    const double rotation = m_project->value(e.id, "rotation", m_frame);

    QTransform transform;
    transform.translate(origin.x(), origin.y());
    if (e.rotPointCustom())
    {
        const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
        const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;
        transform.translate(ax, ay);
        transform.rotate(rotation);
        transform.translate(-ax, -ay);
    }
    else
    {
        transform.rotate(rotation);
    }
    transform.scale(m_project->value(e.id, "scaleX", m_frame) * s,
                    m_project->value(e.id, "scaleY", m_frame) * s);
    return transform;
}

QTransform Viewport::entityRotationTransform(const Entity& e) const
{
    const double s = viewScale();
    const QPointF origin = entityScreenCenter(e);
    const double rotation = m_project->value(e.id, "rotation", m_frame);

    QTransform transform;
    transform.translate(origin.x(), origin.y());
    if (e.rotPointCustom())
    {
        const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
        const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;
        transform.translate(ax, ay);
        transform.rotate(rotation);
        transform.translate(-ax, -ay);
    }
    else
    {
        transform.rotate(rotation);
    }
    return transform;
}

bool Viewport::gizmoHitTest(const QPointF& pos, GizmoMode* mode) const
{
    if (m_selected == kInvalidEntity)
        return false;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity)
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

Viewport::Handle Viewport::hitHandle(const QPointF& pos) const
{
    if (m_selected == kInvalidEntity)
        return Handle::None;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity)
        return Handle::None;

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
}

void Viewport::applyHandleDrag(const QPointF& mousePos, bool uniform, bool snap)
{
    const Entity* entity = m_project->entity(m_dragEntity);
    if (!entity)
        return;

    const QRectF bounds = entityLocalBounds(*entity);
    const double s = viewScale();

    const QTransform base = entityRotationTransform(*entity);
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
        newScaleX = signX * local.x() / (halfW * s);
    if (signY != 0.0)
        newScaleY = signY * local.y() / (halfH * s);

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

    // Corner snapping: the dragged corner sticks to the canvas centre lines
    // and edges. Rotation makes the edge positions non-linear, so skip it.
    const double rotation = m_project->value(m_dragEntity, QStringLiteral("rotation"), m_frame);
    if (snap && std::abs(rotation) < 1e-6 && m_snapPixels > 0)
    {
        const double threshold = m_snapPixels / s;
        const double x = m_project->value(m_dragEntity, QStringLiteral("x"), m_frame);
        const double y = m_project->value(m_dragEntity, QStringLiteral("y"), m_frame);
        const double canvasHalfW = m_project->width / 2.0;
        const double canvasHalfH = m_project->height / 2.0;

        if (signX != 0.0)
        {
            const double handleX = x + signX * newScaleX * halfW;
            const double snapped =
                snapTo(handleX, {0.0, -canvasHalfW, canvasHalfW}, threshold);
            if (snapped != handleX)
                newScaleX = (snapped - x) / (signX * halfW);
        }
        if (signY != 0.0)
        {
            const double handleY = y + signY * newScaleY * halfH;
            const double snapped =
                snapTo(handleY, {0.0, -canvasHalfH, canvasHalfH}, threshold);
            if (snapped != handleY)
                newScaleY = (snapped - y) / (signY * halfH);
        }
    }

    emit valueEdited(m_dragEntity, QStringLiteral("scaleX"), newScaleX);
    emit valueEdited(m_dragEntity, QStringLiteral("scaleY"), newScaleY);
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
        {
            drawHandles(p, *entity);
            drawGizmo(p, *entity);
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

    // Scaling handles take priority over the gizmo axes.
    const Handle handle = hitHandle(event->position());
    if (handle != Handle::None)
    {
        m_gizmoMode = GizmoMode::Scale;
        m_activeHandle = handle;
        m_dragging = true;
        m_dragEntity = m_selected;
        m_scaleStartX = m_project->value(m_selected, "scaleX", m_frame);
        m_scaleStartY = m_project->value(m_selected, "scaleY", m_frame);
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

    if (m_gizmoMode == GizmoMode::Scale)
    {
        applyHandleDrag(event->position(), event->modifiers() & Qt::ControlModifier,
                        !(event->modifiers() & Qt::ControlModifier));
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
                const double threshold = m_snapPixels / s;
                px = snapTo(px,
                            {0.0, -bounds.width() / 2.0, bounds.width() / 2.0}, threshold);
                py = snapTo(py,
                            {0.0, -bounds.height() / 2.0, bounds.height() / 2.0}, threshold);
            }
        }
        emit valueEdited(m_dragEntity, QStringLiteral("rotPointX"), px);
        emit valueEdited(m_dragEntity, QStringLiteral("rotPointY"), py);
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
                const double threshold = m_snapPixels / s;
                px = snapTo(px,
                            {0.0, -bounds.width() / 2.0, bounds.width() / 2.0}, threshold);
            }
        }
        emit valueEdited(m_dragEntity, QStringLiteral("rotPointX"), px);
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
                const double threshold = m_snapPixels / s;
                py = snapTo(py,
                            {0.0, -bounds.height() / 2.0, bounds.height() / 2.0}, threshold);
            }
        }
        emit valueEdited(m_dragEntity, QStringLiteral("rotPointY"), py);
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
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfW =
                entity ? entityLocalBounds(*entity).width() / 2.0 * std::abs(scaleX) : 0.0;

            if (std::abs(x) <= threshold)
                x = 0.0;
            else if (std::abs(x - halfW + canvasHalfW) <= threshold)
                x = -canvasHalfW + halfW;
            else if (std::abs(x + halfW - canvasHalfW) <= threshold)
                x = canvasHalfW - halfW;
            else if (m_edgeCenterSnap && std::abs(x - halfW) <= threshold)
                x = halfW;
            else if (m_edgeCenterSnap && std::abs(x + halfW) <= threshold)
                x = -halfW;
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
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfH =
                entity ? entityLocalBounds(*entity).height() / 2.0 * std::abs(scaleY) : 0.0;

            if (std::abs(y) <= threshold)
                y = 0.0;
            else if (std::abs(y - halfH + canvasHalfH) <= threshold)
                y = -canvasHalfH + halfH;
            else if (std::abs(y + halfH - canvasHalfH) <= threshold)
                y = canvasHalfH - halfH;
            else if (m_edgeCenterSnap && std::abs(y - halfH) <= threshold)
                y = halfH;
            else if (m_edgeCenterSnap && std::abs(y + halfH) <= threshold)
                y = -halfH;
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
        emit valueEdited(m_dragEntity, QStringLiteral("rotation"),
                         m_gizmoStartRotation + delta);
        return;
    }

    const QPointF delta = (event->position() - m_dragLast) / s;
    m_dragLast = event->position();

    double x = m_project->value(m_dragEntity, QStringLiteral("x"), m_frame) + delta.x();
    double y = m_project->value(m_dragEntity, QStringLiteral("y"), m_frame) + delta.y();

    if (!(event->modifiers() & Qt::ControlModifier))
    {
        const int snapPixels = m_snapPixels;
        if (snapPixels > 0)
        {
            const double threshold = snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double canvasHalfH = m_project->height / 2.0;
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double halfW = bounds.width() / 2.0 * std::abs(scaleX);
                const double halfH = bounds.height() / 2.0 * std::abs(scaleY);

                // Centre first, then edges flush with the canvas borders,
                // then edges on the centre lines.
                if (std::abs(x) <= threshold)
                    x = 0.0;
                else if (std::abs(x - halfW + canvasHalfW) <= threshold)
                    x = -canvasHalfW + halfW;
                else if (std::abs(x + halfW - canvasHalfW) <= threshold)
                    x = canvasHalfW - halfW;
                else if (m_edgeCenterSnap && std::abs(x - halfW) <= threshold)
                    x = halfW;
                else if (m_edgeCenterSnap && std::abs(x + halfW) <= threshold)
                    x = -halfW;

                if (std::abs(y) <= threshold)
                    y = 0.0;
                else if (std::abs(y - halfH + canvasHalfH) <= threshold)
                    y = -canvasHalfH + halfH;
                else if (std::abs(y + halfH - canvasHalfH) <= threshold)
                    y = canvasHalfH - halfH;
                else if (m_edgeCenterSnap && std::abs(y - halfH) <= threshold)
                    y = halfH;
                else if (m_edgeCenterSnap && std::abs(y + halfH) <= threshold)
                    y = -halfH;
            }
        }
    }

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
        m_activeHandle = Handle::None;
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
