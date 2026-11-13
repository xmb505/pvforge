#include "TimelinePanel.h"

#include "app/EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

bool hasAssetMime(const QMimeData* mime)
{
    return mime
           && (mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-template"))
               || mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-html"))
               || mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset")));
}

} // namespace

void TimelinePanel::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double old = m_pixelsPerFrame;
        double next = std::clamp(old * std::pow(2.0, steps), 0.25, 32.0);
        if (next > 1.0)
            next = std::round(next);
        if (std::abs(next - old) < 1e-6)
            return;
        const int anchor = frameFromX(int(std::lround(event->position().x())));
        m_pixelsPerFrame = next;
        m_scrollX = std::max(
            0, int(std::lround(nameWidth()
                               + (anchor - contentStart()) * next
                               - event->position().x())));
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
        m_scrollX -= int(std::lround(steps * 120.0));
    }
    else
    {
        // Bottom-up: wheel up reveals higher rows, so the upward offset grows.
        m_scrollY += int(std::lround(steps * kRowHeight));
    }
    clampScroll();
    update();
    syncPopups();
    event->accept();
}

void TimelinePanel::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers plain =
        event->modifiers() & ~Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && plain == Qt::NoModifier)
    {
        const int direction = event->key() == Qt::Key_Right ? 1 : -1;
        if (!event->isAutoRepeat())
        {
            emit scrubStarted();
            m_holdDirection = direction;
            stepFrame(direction);
            if (m_holdDirection != 0)
                m_holdDelay->start();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_dragMode != DragMode::None)
    {
        cancelSessionDrag();
        return;
    }
    if (event->key() == Qt::Key_Delete
        || event->key() == Qt::Key_Backspace)
    {
        if (!m_selectedKeyframes.isEmpty())
            deleteSelectedKeyframes();
        else if (m_selected != kInvalidEntity)
            deleteSelectedEntities();
        return;
    }
    if (plain == Qt::ControlModifier && event->key() == Qt::Key_C)
    {
        copySelectedKeyframes();
        return;
    }
    if (plain == Qt::ControlModifier && event->key() == Qt::Key_V)
    {
        pasteKeyframes();
        return;
    }
    if (plain == Qt::NoModifier && event->key() == Qt::Key_S)
    {
        splitAtPlayhead();
        return;
    }
    if (plain == Qt::NoModifier && event->key() == Qt::Key_M)
    {
        addMarkerAtPlayhead();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TimelinePanel::keyReleaseEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && !event->isAutoRepeat())
    {
        stopHold();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void TimelinePanel::focusOutEvent(QFocusEvent* event)
{
    // A key release can be lost when the focus moves mid-hold.
    stopHold();
    QWidget::focusOutEvent(event);
}

void TimelinePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
        m_hScroll->setGeometry(nameWidth(), height() - kScrollBarSize,
                               visibleWidth(), kScrollBarSize);
    if (m_vScroll)
        m_vScroll->setGeometry(width() - kScrollBarSize, kRulerHeight,
                               kScrollBarSize,
                               std::max(0, height() - kRulerHeight
                                               - kScrollBarSize));
    // The name column is gone; unit and edit-mode toggles sit at the ruler's
    // right edge where the old mode button already lived.
    const int rulerRight = width() - kScrollBarSize;
    if (m_modeButton)
    {
        m_modeButton->setGeometry(std::max(4, rulerRight - 58), 3, 54,
                                  kRulerHeight - 7);
        m_modeButton->setVisible(m_owner < 0 && width() > 280);
    }
    if (m_unitButton)
    {
        m_unitButton->setGeometry(std::max(4, rulerRight - 58 - 42), 3, 36,
                                  kRulerHeight - 7);
        m_unitButton->setVisible(m_owner < 0 && width() > 200);
    }
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::leaveEvent(QEvent* event)
{
    if (m_dragMode == DragMode::None)
        unsetCursor();
    QWidget::leaveEvent(event);
}

bool TimelinePanel::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

void TimelinePanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!hasAssetMime(event->mimeData()))
        return;
    m_dropActive = true;
    m_dropFrame = std::max(contentStart(),
                           frameFromX(int(std::lround(event->position().x()))));
    if (m_owner >= 0)
        m_dropFrame = std::min(m_dropFrame, contentEnd() - 1);
    event->acceptProposedAction();
    update();
}

void TimelinePanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!hasAssetMime(event->mimeData()))
        return;
    m_dropActive = true;
    m_dropFrame = std::max(contentStart(),
                           frameFromX(int(std::lround(event->position().x()))));
    if (m_owner >= 0)
        m_dropFrame = std::min(m_dropFrame, contentEnd() - 1);
    event->acceptProposedAction();
    update();
}

void TimelinePanel::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    m_dropActive = false;
    update();
}

void TimelinePanel::dropEvent(QDropEvent* event)
{
    if (!hasAssetMime(event->mimeData()))
    {
        m_dropActive = false;
        update();
        return;
    }
    int frame = frameFromX(int(std::lround(event->position().x())));
    frame = std::max(contentStart(), frame);
    if (m_owner >= 0)
        frame = std::min(frame, contentEnd() - 1);
    int row = rowAt(int(std::lround(event->position().y())));
    if (row < 0)
        row = dropRowForY(int(std::lround(event->position().y())));
    const EntityId id = dropAsset(*event->mimeData(), frame, row);
    m_dropActive = false;
    update();
    if (id == kInvalidEntity)
        event->ignore();
    else
        event->acceptProposedAction();
}

EntityId TimelinePanel::dropSessionAsset(const QMimeData& mime, int frame, int row)
{
    if (!m_session || !m_session->document() || !hasAssetMime(&mime))
        return kInvalidEntity;

    const QString hash =
        QString::fromUtf8(mime.data(QStringLiteral("application/x-pvforge-asset")))
            .trimmed();
    if (hash.isEmpty())
        return kInvalidEntity;

    frame = std::max(contentStart(), frame);
    if (m_owner >= 0)
        frame = std::min(frame, contentEnd() - 1);

    const Uuid parent = m_owner >= 0 ? uuidForEntityId(m_owner) : Uuid();
    QString error;
    const QVector<Uuid> created = m_session->editActions()->addAssetEntities(
        hash, frame, row, parent, &error);
    if (created.isEmpty())
    {
        emit editFailed(error.isEmpty() ? QStringLiteral("无法添加素材") : error);
        return kInvalidEntity;
    }

    m_session->document()->setSelection(created.first());
    const auto asset = m_session->assets().get(hash);
    const QString name = asset && !asset->name.isEmpty() ? asset->name : hash;
    emit editCommitted(QStringLiteral("已添加素材 \"%1\"").arg(name));
    refreshFromSession();
    return entityIdForUuid(created.first());
}

EntityId TimelinePanel::dropAsset(const QMimeData& mime, int frame, int row)
{
    return dropSessionAsset(mime, frame, row);
}

} // namespace pvf
