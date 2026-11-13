#include "next/timeline/TimelineWidget.h"

#include "TimelineWidgetInternal.h"
#include "next/core/Document.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <climits>
#include <cmath>

namespace pvf::next {

namespace {

void collectSubtree(const model::Project& project, const Uuid& uuid,
                    QVector<Uuid>& out, QSet<Uuid>& visited)
{
    if (visited.contains(uuid))
        return;
    visited.insert(uuid);
    out.append(uuid);
    for (const Uuid& child : model::childrenOf(project, uuid))
        collectSubtree(project, child, out, visited);
}

} // namespace

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint& pos) const
{
    Hit hit;
    if (!m_document || !bodyRect().contains(pos))
        return hit;

    const QVector<Uuid> entities = visibleEntities();
    for (int i = entities.size() - 1; i >= 0; --i)
    {
        const Uuid& uuid = entities.at(i);
        const QRect bar = entityBarRect(uuid);
        if (!bar.contains(pos))
            continue;

        hit.uuid = uuid;
        if (pos.x() <= bar.left() + kEdgeGrabPixels)
            hit.zone = HitZone::LeftEdge;
        else if (pos.x() >= bar.right() - kEdgeGrabPixels + 1)
            hit.zone = HitZone::RightEdge;
        else
            hit.zone = HitZone::Body;
        break;
    }
    return hit;
}

void TimelineWidget::updateHoverCursor(const QPoint& pos)
{
    if (!m_document)
    {
        unsetCursor();
        return;
    }

    if (markerLaneRect().contains(pos) && !markerIdAt(pos).isEmpty())
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }

    for (const Uuid& uuid : visibleEntities())
    {
        const QRect addRect = addButtonRect(uuid);
        if (!addRect.isEmpty() && addRect.contains(pos))
        {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    if (keyframeAt(pos) >= 0)
    {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty())
    {
        unsetCursor();
        return;
    }

    if (detail::entityIsLocked(m_document, hit.uuid))
    {
        unsetCursor();
        return;
    }

    if (hit.zone == HitZone::LeftEdge || hit.zone == HitZone::RightEdge)
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::OpenHandCursor);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_document || event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);
    const QPoint pos = event->position().toPoint();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    const QString pressedMarker =
        markerLaneRect().contains(pos) ? markerIdAt(pos) : QString();
    if (!pressedMarker.isEmpty())
    {
        const int index = markerIndex(pressedMarker);
        m_dragMode = DragMode::Marker;
        m_dragMarkerId = pressedMarker;
        m_dragMarkerSnapshot = m_document->project().markers;
        m_dragPressPos = pos;
        m_dragChanged = false;
        if (index >= 0)
            m_document->setPlayhead(
                std::max(0, m_document->project().markers.at(index).frame));
        setCursor(Qt::SizeHorCursor);
        return;
    }

    if (!m_parentUuid.isEmpty() && breadcrumbRect().contains(pos))
    {
        setParentUuid(Uuid());
        return;
    }

    if (rulerRect().contains(pos) || markerLaneRect().contains(pos))
    {
        m_dragMode = DragMode::Scrub;
        m_dragEntity.clear();
        m_dragChanged = false;
        setPlayhead(frameFromX(pos.x()));
        return;
    }

    // The “+” button on the left of an entity bar opens its private timeline.
    for (const Uuid& uuid : visibleEntities())
    {
        const QRect addRect = addButtonRect(uuid);
        if (addRect.isEmpty() || !addRect.contains(pos))
            continue;

        m_dragMode = DragMode::None;
        m_dragEntity.clear();
        m_dragChanged = false;
        m_document->setSelection(uuid);
        emit privateTimelineRequested(uuid);
        return;
    }

    // Merged keyframe diamonds of the selected entity.
    const int keyFrame = keyframeAt(pos);
    if (keyFrame >= 0)
    {
        const auto entityIt =
            m_document->project().entities.constFind(m_document->selection());
        if (entityIt == m_document->project().entities.constEnd())
            return;

        const model::Entity& entity = entityIt.value();
        if (detail::entityIsLocked(m_document, entity.uuid))
        {
            emit statusMessage(
                tr("实体已锁定：%1").arg(entityDisplayName(entity)));
            return;
        }

        if (m_keyframeSelectionUuid != entity.uuid)
        {
            m_keyframeSelectionUuid = entity.uuid;
            m_selectedKeyframes.clear();
        }

        const bool toggle = event->modifiers()
                            & (Qt::ControlModifier | Qt::ShiftModifier);
        if (toggle)
        {
            if (m_selectedKeyframes.contains(keyFrame))
                m_selectedKeyframes.remove(keyFrame);
            else
                m_selectedKeyframes.insert(keyFrame);
        }
        else if (!m_selectedKeyframes.contains(keyFrame))
        {
            m_selectedKeyframes = QSet<int>{keyFrame};
        }

        m_dragMode = DragMode::Keyframes;
        m_dragEntity = entity.uuid;
        m_dragPressPos = pos;
        m_dragKeyPressFrame = keyFrame;
        m_dragKeyFrames = m_selectedKeyframes;
        for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
             ++it)
            m_dragKeyTracks.insert(it.key(), it.value().keys);
        m_dragChanged = false;
        update();
        return;
    }

    // A click that is not on a diamond drops the frame selection.
    clearKeyframeSelection();

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty())
    {
        m_dragMode = DragMode::None;
        m_dragEntity.clear();
        m_dragChanged = false;
        m_document->setSelection(Uuid());
        update();
        return;
    }

    const auto it = m_document->project().entities.constFind(hit.uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    m_document->setSelection(hit.uuid);
    m_dragEntity = hit.uuid;
    m_dragPressPos = pos;
    m_dragOriginalStart = it.value().start;
    m_dragOriginalEnd = it.value().end;
    m_dragChanged = false;

    if (detail::entityIsLocked(m_document, hit.uuid))
    {
        m_dragMode = DragMode::None;
        setCursor(Qt::ArrowCursor);
    }
    else if (hit.zone == HitZone::LeftEdge)
    {
        m_dragMode = DragMode::TrimStart;
        setCursor(Qt::SizeHorCursor);
    }
    else if (hit.zone == HitZone::RightEdge)
    {
        m_dragMode = DragMode::TrimEnd;
        setCursor(Qt::SizeHorCursor);
    }
    else
    {
        m_dragMode = DragMode::Move;
        QSet<Uuid> visited;
        collectSubtree(m_document->project(), m_dragEntity, m_dragMoveUuids,
                       visited);
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto entity =
                m_document->project().entities.constFind(uuid);
            if (entity == m_document->project().entities.constEnd())
                continue;
            if (detail::entityIsLocked(m_document, uuid))
            {
                emit statusMessage(
                    tr("子树包含锁定实体，不能移动：%1")
                        .arg(entityDisplayName(entity.value())));
                m_dragMode = DragMode::None;
                m_dragMoveUuids.clear();
                setCursor(Qt::ArrowCursor);
                update();
                return;
            }
            m_dragOriginalRanges.insert(
                uuid, qMakePair(entity.value().start, entity.value().end));
        }
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_dragMode == DragMode::Move
        || m_dragMode == DragMode::TrimStart
        || m_dragMode == DragMode::TrimEnd)
    {
        m_dragProjectSnapshot = m_document->project();
        m_dragHasSnapshot = true;
    }
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_document)
        return;

    const QPoint pos = event->position().toPoint();
    if (m_dragMode == DragMode::Scrub)
    {
        setPlayhead(frameFromX(pos.x()));
        return;
    }

    if (m_dragMode == DragMode::Marker)
    {
        const int index = markerIndex(m_dragMarkerId);
        if (index < 0)
            return;
        const int frame = std::max(0, frameFromX(pos.x()));
        model::Marker& marker =
            m_document->mutableProject().markers[index];
        if (marker.frame != frame)
        {
            marker.frame = frame;
            m_dragChanged = true;
            updateScrollBar();
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::Keyframes)
    {
        applyKeyframeDrag(pos);
        return;
    }

    if (m_dragMode == DragMode::Move || m_dragMode == DragMode::TrimStart
        || m_dragMode == DragMode::TrimEnd)
    {
        applyDrag(pos, event->modifiers());
        return;
    }

    updateHoverCursor(pos);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    finishDrag();
    updateHoverCursor(event->position().toPoint());
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_document || event->button() != Qt::LeftButton)
        return;

    const QPoint pos = event->position().toPoint();
    if (markerLaneRect().contains(pos))
    {
        const QString markerId = markerIdAt(pos);
        if (!markerId.isEmpty())
        {
            openMarkerEditor(markerId);
            event->accept();
        }
        return;
    }

    if (rulerRect().contains(pos) || toolbarRect().contains(pos))
        return;

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty() || hit.zone != HitZone::Body)
        return;

    // The “+” button never writes keys.
    const QRect addRect = addButtonRect(hit.uuid);
    if (!addRect.isEmpty() && addRect.contains(pos))
        return;

    const auto it = m_document->project().entities.constFind(hit.uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    m_document->setSelection(hit.uuid);
    clearKeyframeSelection();

    const int duration = it.value().duration();
    if (duration <= 0)
        return;

    const int relativeFrame =
        std::clamp(frameFromX(pos.x()) - it.value().start, 0, duration - 1);
    insertKeyframesAt(hit.uuid, relativeFrame);
    event->accept();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_document)
    {
        QWidget::contextMenuEvent(event);
        return;
    }

    const QPoint pos = event->pos();
    const bool onRuler =
        rulerRect().contains(pos) || markerLaneRect().contains(pos);
    const QString markerId = markerIdAt(pos);
    const Hit entityHit = hitTest(pos);
    if (!onRuler && markerId.isEmpty() && entityHit.uuid.isEmpty())
    {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction* addAction = nullptr;
    QAction* editAction = nullptr;
    QAction* deleteAction = nullptr;
    QAction* splitAction = nullptr;
    if (onRuler || !markerId.isEmpty())
    {
        addAction = menu.addAction(tr("在播放头添加标记…"));
        if (!markerId.isEmpty())
        {
            menu.addSeparator();
            editAction = menu.addAction(tr("编辑标记…"));
            deleteAction = menu.addAction(tr("删除标记"));
        }
    }
    if (!entityHit.uuid.isEmpty())
    {
        if (!menu.isEmpty())
            menu.addSeparator();
        const auto entity =
            m_document->project().entities.constFind(entityHit.uuid);
        const bool canSplit =
            entity != m_document->project().entities.constEnd()
            && playhead() > entity.value().start
            && playhead() < entity.value().end;
        splitAction = menu.addAction(tr("在播放头切分"));
        splitAction->setEnabled(canSplit);
    }

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == addAction)
        openNewMarkerDialog();
    else if (chosen == editAction)
        openMarkerEditor(markerId);
    else if (chosen == deleteAction)
        deleteMarkerWithConfirm(markerId);
    else if (chosen == splitAction)
    {
        m_document->setSelection(entityHit.uuid);
        QString error;
        splitSelectedAtPlayhead(&error);
    }
    event->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_document)
    {
        QWidget::keyPressEvent(event);
        return;
    }

    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if (event->key() == Qt::Key_Escape && m_dragMode != DragMode::None)
    {
        cancelDrag();
        event->accept();
        return;
    }

    if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier
        && event->key() == Qt::Key_M)
    {
        addMarkerAtPlayhead();
        event->accept();
        return;
    }

    if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier
        && event->key() == Qt::Key_S)
    {
        QString error;
        splitSelectedAtPlayhead(&error);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (!m_selectedKeyframes.isEmpty())
        {
            deleteSelectedKeyframes();
            event->accept();
            return;
        }
    }
    else if (ctrl && event->key() == Qt::Key_C)
    {
        copySelectedKeyframes();
        event->accept();
        return;
    }
    else if (ctrl && event->key() == Qt::Key_V)
    {
        pasteKeyframes();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int delta = event->angleDelta().y();
        if (delta == 0)
        {
            event->ignore();
            return;
        }

        const int cursorX = int(event->position().x());
        const double frame =
            (cursorX + m_scrollX) / std::max(0.0001, m_pixelsPerFrame);
        const double factor = std::pow(1.2, delta / 120.0);
        setPixelsPerFrame(m_pixelsPerFrame * factor);
        setScrollX(int(std::lround(frame * m_pixelsPerFrame - cursorX)));
        event->accept();
        return;
    }

    int deltaX = event->angleDelta().x();
    int deltaY = event->angleDelta().y();
    if (deltaX == 0 && deltaY == 0)
    {
        event->ignore();
        return;
    }

    if (deltaX != 0)
    {
        setScrollX(m_scrollX - deltaX);
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
        setScrollX(m_scrollX - deltaY);
    }
    else
    {
        // Plain vertical wheel scrolls rows; the timeline is bottom-anchored,
        // so wheel-up reveals deeper rows.
        setRowScroll(m_rowScroll
                     + int(std::lround(deltaY / 120.0 * kRowHeight)));
    }
    event->accept();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
    {
        m_hScroll->setGeometry(0, height() - kScrollBarHeight, width(),
                               kScrollBarHeight);
    }
    if (m_vScroll)
    {
        const QRect body = bodyRect();
        m_vScroll->setGeometry(width() - kScrollBarWidth, body.top(),
                               kScrollBarWidth, body.height());
    }
    updateScrollBar();
}

int TimelineWidget::snapFrame(int frame, bool enabled,
                              const QSet<Uuid>& excluded) const
{
    if (!enabled || !m_document)
        return frame;

    const int x = xFromFrame(frame);
    int best = frame;
    int bestDistance = kSnapPixels + 1;
    for (const Uuid& uuid : visibleEntities())
    {
        if (excluded.contains(uuid))
            continue;
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            continue;

        const int edges[] = {it.value().start, it.value().end};
        for (int edge : edges)
        {
            const int distance = std::abs(xFromFrame(edge) - x);
            if (distance <= kSnapPixels && distance < bestDistance)
            {
                bestDistance = distance;
                best = edge;
            }
        }
    }
    return best;
}

void TimelineWidget::applyDrag(const QPoint& pos,
                               Qt::KeyboardModifiers modifiers)
{
    if (!m_document || m_dragEntity.isEmpty())
        return;

    auto it = m_document->mutableProject().entities.find(m_dragEntity);
    if (it == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = it.value();
    const int delta = frameFromX(pos.x()) - frameFromX(m_dragPressPos.x());
    const int duration = std::max(1, m_dragOriginalEnd - m_dragOriginalStart);
    const bool snapEnabled = !(modifiers & Qt::ControlModifier);

    int minStart = 0;
    int maxEnd = INT_MAX;
    if (!entity.parentUuid.isEmpty())
    {
        const auto parent =
            m_document->project().entities.constFind(entity.parentUuid);
        if (parent != m_document->project().entities.constEnd())
        {
            minStart = parent.value().start;
            maxEnd = parent.value().end;
        }
    }

    switch (m_dragMode)
    {
    case DragMode::Move:
    {
        const int lastStart = maxEnd == INT_MAX ? INT_MAX : maxEnd - duration;
        const int maxStart = std::max(minStart, lastStart);
        int newStart =
            std::clamp(m_dragOriginalStart + delta, minStart, maxStart);
        if (snapEnabled)
        {
            QSet<Uuid> excluded;
            for (const Uuid& moved : m_dragMoveUuids)
                excluded.insert(moved);

            int bestDelta = 0;
            int bestDistance = kSnapPixels + 1;
            const int rawStart = newStart;
            const int rawEnd = newStart + duration;
            for (int edge : {rawStart, rawEnd})
            {
                const int snapped = snapFrame(edge, true, excluded);
                if (snapped == edge)
                    continue;
                const int distance =
                    std::abs(xFromFrame(edge) - xFromFrame(snapped));
                if (distance <= kSnapPixels && distance < bestDistance)
                {
                    bestDistance = distance;
                    bestDelta = snapped - edge;
                }
            }
            newStart = std::clamp(newStart + bestDelta, minStart, maxStart);
        }
        int appliedDelta = newStart - m_dragOriginalStart;

        // Never push any entity of the moved subtree before frame 0.
        int earliestStart = INT_MAX;
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto range = m_dragOriginalRanges.constFind(uuid);
            if (range != m_dragOriginalRanges.constEnd())
                earliestStart = std::min(earliestStart, range.value().first);
        }
        if (earliestStart != INT_MAX)
            appliedDelta = std::max(appliedDelta, -earliestStart);

        bool changed = false;
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto range = m_dragOriginalRanges.constFind(uuid);
            if (range == m_dragOriginalRanges.constEnd())
                continue;

            auto entityIt = m_document->mutableProject().entities.find(uuid);
            if (entityIt == m_document->mutableProject().entities.end())
                continue;

            const int shiftedStart = range.value().first + appliedDelta;
            const int shiftedEnd = range.value().second + appliedDelta;
            if (entityIt.value().start != shiftedStart
                || entityIt.value().end != shiftedEnd)
            {
                entityIt.value().start = shiftedStart;
                entityIt.value().end = shiftedEnd;
                changed = true;
            }
        }
        if (changed)
            m_dragChanged = true;
        break;
    }
    case DragMode::TrimStart:
    {
        const int lastStart = std::min(m_dragOriginalEnd - 1,
                                       maxEnd == INT_MAX ? INT_MAX : maxEnd - 1);
        int newStart = std::clamp(m_dragOriginalStart + delta, minStart,
                                  std::max(minStart, lastStart));
        if (snapEnabled)
        {
            QSet<Uuid> excluded;
            excluded.insert(entity.uuid);
            newStart = std::clamp(snapFrame(newStart, true, excluded),
                                  minStart, std::max(minStart, lastStart));
        }
        if (newStart != entity.start)
        {
            entity.start = newStart;
            m_dragChanged = true;
        }
        // Preview only moves the visible edge. Keyframes, sourceIn and solver
        // segments are trimmed destructively once, on release.
        break;
    }
    case DragMode::TrimEnd:
    {
        const int firstEnd = m_dragOriginalStart + 1;
        int newEnd = std::max(
            firstEnd, std::min(m_dragOriginalEnd + delta,
                               maxEnd == INT_MAX ? INT_MAX : maxEnd));
        if (snapEnabled)
        {
            QSet<Uuid> excluded;
            excluded.insert(entity.uuid);
            newEnd = std::max(
                firstEnd,
                std::min(snapFrame(newEnd, true, excluded),
                         maxEnd == INT_MAX ? INT_MAX : maxEnd));
        }
        if (newEnd != entity.end)
        {
            entity.end = newEnd;
            m_dragChanged = true;
        }
        // Preview only; see TrimStart above.
        break;
    }
    default:
        break;
    }

    if (m_dragChanged)
    {
        updateScrollBar();
        update();
    }
}

void TimelineWidget::finishDrag()
{
    const DragMode mode = m_dragMode;
    const Uuid uuid = m_dragEntity;
    const bool changed = m_dragChanged;
    const QVector<Uuid> moveUuids = m_dragMoveUuids;
    const QHash<Uuid, QPair<int, int>> moveRanges = m_dragOriginalRanges;
    const model::Project trimSnapshot = m_dragProjectSnapshot;
    const bool hasTrimSnapshot = m_dragHasSnapshot;
    const QHash<QString, QVector<model::Keyframe>> keyTracks = m_dragKeyTracks;
    const QSet<int> keyFrames = m_dragKeyFrames;
    const int keyDelta = m_dragKeyDelta;
    const bool keyConflict = m_dragKeyConflict;
    const QString dragMarkerId = m_dragMarkerId;
    const QVector<model::Marker> markerSnapshot = m_dragMarkerSnapshot;

    int previewStart = 0;
    int previewEnd = 0;
    bool hasPreview = false;
    if (m_document && !uuid.isEmpty()
        && (mode == DragMode::TrimStart || mode == DragMode::TrimEnd))
    {
        const auto preview = m_document->project().entities.constFind(uuid);
        if (preview != m_document->project().entities.constEnd())
        {
            previewStart = preview.value().start;
            previewEnd = preview.value().end;
            hasPreview = true;
        }
    }

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    if (!m_document)
        return;

    if (mode == DragMode::Marker)
    {
        if (!changed || dragMarkerId.isEmpty())
            return;
        const int index = markerIndex(dragMarkerId);
        if (index < 0)
            return;

        const QString title = m_document->project().markers.at(index).title;
        QString error;
        commitMarkers(markerSnapshot,
                      tr("pvf: move marker \"%1\"")
                          .arg(title.isEmpty() ? dragMarkerId : title),
                      &error);
        return;
    }

    if (uuid.isEmpty())
        return;

    if (mode == DragMode::Keyframes)
    {
        auto it = m_document->mutableProject().entities.find(uuid);
        if (it == m_document->mutableProject().entities.end())
            return;

        auto restoreTracks = [this, &uuid, &keyTracks]() {
            auto restoreIt = m_document->mutableProject().entities.find(uuid);
            if (restoreIt == m_document->mutableProject().entities.end())
                return;
            restoreIt.value().tracks.clear();
            for (auto trackIt = keyTracks.constBegin();
                 trackIt != keyTracks.constEnd(); ++trackIt)
            {
                model::Track track;
                track.keys = trackIt.value();
                restoreIt.value().tracks.insert(trackIt.key(), track);
            }
        };

        if (keyConflict)
        {
            restoreTracks();
            m_keyframeSelectionUuid = uuid;
            m_selectedKeyframes = keyFrames;
            emit statusMessage(
                tr("关键帧不能移动求解段开始帧，也不能移入求解段驱动的区间"));
            update();
            return;
        }
        if (!changed || keyDelta == 0)
        {
            update();
            return;
        }

        const QString name = entityDisplayName(it.value());
        QString error;
        const bool ok = m_document->commitEntity(
            uuid, QStringLiteral("pvf: move keyframes \"%1\"").arg(name),
            &error);
        if (!ok)
        {
            restoreTracks();
            m_keyframeSelectionUuid = uuid;
            m_selectedKeyframes = keyFrames;
            emit statusMessage(error.isEmpty()
                                   ? tr("关键帧移动失败")
                                   : tr("关键帧移动失败：%1").arg(error));
        }
        update();
        return;
    }

    if (!changed)
        return;

    if (mode == DragMode::Move)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            return;
        const QString name = it.value().name.isEmpty() ? uuid : it.value().name;

        QVector<Uuid> commitUuids;
        bool differs = false;
        for (const Uuid& moved : moveUuids)
        {
            const auto range = moveRanges.constFind(moved);
            const auto entity =
                m_document->project().entities.constFind(moved);
            if (range == moveRanges.constEnd()
                || entity == m_document->project().entities.constEnd())
                continue;

            commitUuids.append(moved);
            if (entity.value().start != range.value().first
                || entity.value().end != range.value().second)
                differs = true;
        }
        if (!differs || commitUuids.isEmpty())
            return;

        m_document->commitEntities(
            commitUuids, QStringLiteral("pvf: move entity \"%1\"").arg(name));
        updateScrollBar();
        update();
        return;
    }

    if (mode != DragMode::TrimStart && mode != DragMode::TrimEnd)
        return;
    if (!hasTrimSnapshot || !hasPreview)
        return;
    if (previewStart == m_dragOriginalStart && previewEnd == m_dragOriginalEnd)
        return;

    const auto snapshotEntity = trimSnapshot.entities.constFind(uuid);
    if (snapshotEntity == trimSnapshot.entities.constEnd())
        return;
    const QString name =
        snapshotEntity.value().name.isEmpty() ? uuid : snapshotEntity.value().name;

    // Rebuild the destructive trim from the pre-drag snapshot; the mouse-move
    // preview only changed start/end in memory.
    m_document->mutableProject() = trimSnapshot;

    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    QString error;
    if (!trimEntityTree(uuid, previewStart, previewEnd, upserts, deletes,
                        &error))
    {
        m_document->mutableProject() = trimSnapshot;
        emit statusMessage(
            error.isEmpty() ? tr("裁剪失败")
                            : tr("裁剪失败：%1").arg(error));
        updateScrollBar();
        update();
        return;
    }

    const bool committed = m_document->commitChanges(
        upserts, deletes,
        QStringLiteral("pvf: trim entity \"%1\"").arg(name), &error);
    if (!committed)
    {
        m_document->mutableProject() = trimSnapshot;
        emit statusMessage(
            error.isEmpty() ? tr("裁剪提交失败")
                            : tr("裁剪提交失败：%1").arg(error));
    }

    updateScrollBar();
    update();
}

void TimelineWidget::cancelDrag()
{
    if (!m_document || m_dragMode == DragMode::None)
        return;

    if (m_dragHasSnapshot)
        m_document->mutableProject() = m_dragProjectSnapshot;
    else if (m_dragMode == DragMode::Keyframes && !m_dragEntity.isEmpty())
    {
        auto it = m_document->mutableProject().entities.find(m_dragEntity);
        if (it != m_document->mutableProject().entities.end())
        {
            it.value().tracks.clear();
            for (auto trackIt = m_dragKeyTracks.constBegin();
                 trackIt != m_dragKeyTracks.constEnd(); ++trackIt)
            {
                model::Track track;
                track.keys = trackIt.value();
                it.value().tracks.insert(trackIt.key(), track);
            }
        }
    }
    else if (m_dragMode == DragMode::Marker && !m_dragMarkerId.isEmpty())
    {
        m_document->mutableProject().markers = m_dragMarkerSnapshot;
    }

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    unsetCursor();
    updateScrollBar();
    update();
}

} // namespace pvf::next
