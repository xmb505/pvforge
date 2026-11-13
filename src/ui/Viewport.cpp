#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "core/Settings.h"
#include "next/app/MediaServices.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf {

void Viewport::initShell()
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

Viewport::Viewport(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    initShell();
    m_session = session;
    if (!m_session)
        return;

    if (auto* document = m_session->document())
    {
        connect(document, &next::Document::changed, this, [this] {
            invalidateSessionImage();
            update();
        });
        connect(document, &next::Document::playheadChanged, this, [this] {
            invalidateSessionImage();
            update();
        });
        connect(document, &next::Document::selectionChanged, this, [this] {
            m_hoveredRotateHandle.reset();
            invalidateSessionImage();
            update();
        });
    }

    if (auto* media = m_session->media())
    {
        connect(media, &next::MediaServices::htmlFrameReady, this, [this] {
            invalidateSessionImage();
            update();
        }, Qt::QueuedConnection);
        connect(media, &next::MediaServices::htmlServiceFailed, this,
                [this](const QString& message) {
                    setToolTip(QStringLiteral("HTML 服务未启动：%1").arg(message));
                });
    }
}

void Viewport::refreshSession()
{
    invalidateSessionImage();
    update();
}

QSize Viewport::canvasSize() const
{
    if (m_session)
    {
        const model::ProjectSettings& settings = m_session->settings();
        return QSize(std::max(1, settings.width), std::max(1, settings.height));
    }
    return QSize(16, 9);
}

void Viewport::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    if (sessionMode())
        invalidateSessionImage();
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
    return canvasRect().width() / std::max(1, canvasSize().width());
}

QRectF Viewport::canvasRect() const
{
    const QSize canvas = canvasSize();
    const double margin = 40.0;
    const double fitW = (width() - margin) / std::max(1, canvas.width());
    const double fitH = (height() - margin) / std::max(1, canvas.height());
    const double scale = std::max(0.0001, std::min(fitW, fitH) * m_zoom);
    const QSizeF size(canvas.width() * scale, canvas.height() * scale);
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

void Viewport::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (!m_session)
    {
        painter.fillRect(rect(), QApplication::palette().color(QPalette::Window));
        return;
    }
    paintSession(painter);
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (!m_session)
    {
        // Without a session the viewport is only a placeholder canvas: it
        // still pans with the middle button, like the old shell did.
        if (event->button() == Qt::MiddleButton)
        {
            m_panning = true;
            m_panLast = event->position();
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }

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
    if (m_editingEnabled && handleSessionCameraPress(event))
    {
        update();
        return;
    }

    if (m_editingEnabled)
    {
        if (const auto handle = hitSessionGizmo(event->position()))
        {
            const SessionDragKind kind =
                *handle == gizmo::Handle::MoveX ? SessionDragKind::MoveX
                : *handle == gizmo::Handle::MoveY ? SessionDragKind::MoveY
                : *handle == gizmo::Handle::CenterScale
                    ? SessionDragKind::CenterScale
                : *handle == gizmo::Handle::ScaleTopLeft
                          || *handle == gizmo::Handle::ScaleTopRight
                          || *handle == gizmo::Handle::ScaleBottomRight
                          || *handle == gizmo::Handle::ScaleBottomLeft
                    ? SessionDragKind::Scale
                    : SessionDragKind::Rotate;
            startSessionGizmoDrag(m_session->document()->selection(), kind,
                                  *handle, event->position());
            update();
            return;
        }
    }

    const model::Entity* hit = hitTestSession(event->position());
    if (hit)
    {
        if (m_session->document())
            m_session->document()->setSelection(hit->uuid);
        emit uuidSelected(hit->uuid);
        if (m_editingEnabled && !hit->locked)
            startSessionGizmoDrag(hit->uuid, SessionDragKind::Move,
                                  gizmo::Handle::MoveX, event->position());
    }
    else
    {
        if (m_session->document())
            m_session->document()->setSelection(Uuid());
        emit uuidSelected(Uuid());
    }
    update();
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning)
    {
        m_pan += event->position() - m_panLast;
        m_panLast = event->position();
        emit viewChanged();
        update();
        return;
    }
    if (m_sessionDragKind == SessionDragKind::CameraMove
        || m_sessionDragKind == SessionDragKind::CameraRotate
        || m_sessionDragKind == SessionDragKind::CameraScale)
    {
        applySessionCameraDrag(event->position(), event->modifiers());
        return;
    }
    if (m_sessionDragKind != SessionDragKind::None)
    {
        applySessionGizmoDrag(event->position(), event->modifiers());
        return;
    }
    if (!m_session)
        return;

    // Hover feedback: the corner rotate handles switch to a rotation
    // cursor and highlight.  Other handles keep the legacy arrow cursor.
    if (m_editingEnabled && m_session->document())
    {
        const std::optional<gizmo::Handle> hit =
            hitSessionGizmo(event->position());
        const std::optional<gizmo::Handle> rotate =
            hit && gizmo::isRotateHandle(*hit) ? hit : std::nullopt;
        if (rotate != m_hoveredRotateHandle)
        {
            m_hoveredRotateHandle = rotate;
            if (rotate)
                setCursor(ui::detail::rotateCursor());
            else
                unsetCursor();
            update();
        }
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        unsetCursor();
    }
    if (event->button() == Qt::LeftButton
        && m_sessionDragKind != SessionDragKind::None)
        finishSessionGizmoDrag();
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    const double s0 = viewScale();
    const QPointF before = sessionScreenToProject(event->position());
    m_zoom = std::clamp(m_zoom * std::pow(1.15, steps), 0.1, 8.0);
    const double s1 = viewScale();
    // Keep the model point under the cursor fixed.  `before` is Y-up
    // model space, while m_pan is a Y-down widget offset, hence the flip.
    m_pan += (s0 - s1) * QPointF(before.x(), -before.y());
    invalidateSessionImage();
    emit viewChanged();
    update();
}

void Viewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (sessionMode())
        invalidateSessionImage();
    emit viewChanged();
}

void Viewport::keyPressEvent(QKeyEvent* event)
{
    if (sessionMode() && event->key() == Qt::Key_Escape
        && m_sessionDragKind != SessionDragKind::None)
    {
        cancelSessionDrag();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace pvf
