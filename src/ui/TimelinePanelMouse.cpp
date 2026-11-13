#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"

#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf {

using ui::detail::plusButtonRect;

void TimelinePanel::mousePressEvent(QMouseEvent* event)
{
    if (!sessionMode() || event->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    if (m_popup)
        if (QWidget* popup = parentWidget())
            popup->raise();

    const QPointF pos = event->position();
    if (pos.y() < kRulerHeight)
    {
        if (pos.x() < nameWidth())
            return;
        const QString markerId = markerAt(pos.toPoint());
        if (!markerId.isEmpty())
        {
            beginMarkerDrag(markerId, int(std::lround(pos.x())));
            return;
        }
        m_dragMode = DragMode::Scrub;
        m_lastDragPos = pos;
        m_autoScroll->start(30);
        emit scrubStarted();
        setScrubFrame(pos.x());
        return;
    }

    const int row = rowAt(int(pos.y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);

    // Inline clip buttons win over trim/keyframe gestures, but only where
    // they are actually painted (short clips use the context menu).
    if (entity && clipRect(*entity).width() >= kInlineButtonsMinWidth)
    {
        for (int index = 0; index < 3; ++index)
        {
            if (rowButtonRect(row, index).contains(pos.toPoint()))
            {
                handleRowButton(row, index);
                return;
            }
        }
    }
    const int pressX = int(std::lround(pos.x()));
    if (!entity)
    {
        m_dragMode = DragMode::PendingSelect;
        m_pressPos = pos;
        m_boxStart = pos;
        m_boxEnd = pos;
        m_pressFrame = std::max(0, frameFromX(pressX));
        m_pressEntity = kInvalidEntity;
        update();
        return;
    }

    const QRect clip = clipRect(*entity);
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(pos.toPoint()))
    {
        openPopup(id);
        return;
    }

    const int x = int(std::lround(pos.x()));
    m_dragEntity = id;
    m_dragRow = row;
    m_dragStartFrame = frameFromX(x);
    m_dragStartRow = row;
    m_dragClipStart = clipStartOf(*entity);
    m_dragClipEnd = clipEndOf(*entity);
    m_lastDragPos = pos;
    m_moved = false;

    if (!isEntityLocked(id))
    {
        const int keyFrame = hitKeyframeRow(row, x, int(std::lround(pos.y())));
        if (keyFrame >= 0)
        {
            selectEntity(id, true);
            const QStringList frameTracks = tracksAtFrame(id, keyFrame);
            const bool singleTrack =
                (event->modifiers() & Qt::AltModifier) != 0
                && !frameTracks.isEmpty();
            QString track;
            if (singleTrack)
            {
                track = frameTracks.contains(m_selectedTrack)
                            ? m_selectedTrack
                            : frameTracks.first();
            }
            KeyframeSelection selection{id, double(keyFrame), track};
            const bool toggle =
                event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            auto it = std::find(m_selectedKeyframes.begin(),
                                m_selectedKeyframes.end(), selection);
            if (toggle)
            {
                if (it != m_selectedKeyframes.end())
                    m_selectedKeyframes.erase(it);
                else
                    m_selectedKeyframes.append(selection);
            }
            else if (it == m_selectedKeyframes.end())
            {
                m_selectedKeyframes.clear();
                m_selectedKeyframes.append(selection);
            }
            if (!track.isEmpty())
                m_selectedTrack = track;
            else if (!frameTracks.isEmpty())
                m_selectedTrack = frameTracks.first();

            m_dragMode = DragMode::MoveKeyframe;
            m_dragKeyTrack = track;
            m_keyDragStart = keyFrame;
            m_dragStartFrame = keyFrame;
            m_frame = std::max(0, keyFrame);
            emit frameRequested(m_frame);
            emit keyframeSelectionChanged(m_selectedKeyframes);
            beginSessionDrag();
            update();
            return;
        }
    }

    if (m_editMode == EditMode::Clips && !isEntityLocked(id))
    {
        const bool nearLeft = std::abs(x - clip.left()) <= kTrimGrab;
        const bool nearRight = std::abs(x - clip.right()) <= kTrimGrab;
        if (nearLeft || nearRight)
        {
            selectEntity(id, true);
            m_dragMode = nearLeft ? DragMode::TrimStart : DragMode::TrimEnd;
            beginSessionDrag();
            update();
            return;
        }
        if (clip.contains(pos.toPoint()))
        {
            selectEntity(id, true);
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            m_dragMode = DragMode::MoveClip;
            beginSessionDrag();
            update();
            return;
        }
    }

    m_dragMode = DragMode::PendingSelect;
    m_pressPos = pos;
    m_boxStart = pos;
    m_boxEnd = pos;
    m_pressFrame = std::max(0, m_dragStartFrame);
    m_pressEntity = id;
    update();
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!sessionMode())
        return;
    if (m_dragMode == DragMode::MoveMarker)
    {
        updateMarkerDrag(int(std::lround(event->position().x())));
        return;
    }
    if (m_dragMode == DragMode::Scrub)
    {
        m_lastDragPos = event->position();
        setScrubFrame(event->position().x());
        if (event->position().x() > visibleWidth() - 12
            || event->position().x() < 12)
            m_autoScroll->start(30);
        return;
    }

    if (m_dragMode == DragMode::PendingSelect)
    {
        const QPointF delta = event->position() - m_pressPos;
        if (std::abs(delta.x()) + std::abs(delta.y()) <= 5.0)
            return;
        m_dragMode = DragMode::BoxSelect;
        m_boxStart = m_pressPos;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        m_boxEnd = event->position();
        update();
        return;
    }

    if (m_dragMode == DragMode::None)
    {
        if (event->position().y() < kRulerHeight)
        {
            setCursor(markerAt(event->position().toPoint()).isEmpty()
                          ? Qt::ArrowCursor
                          : Qt::SizeHorCursor);
            return;
        }
        const int row = rowAt(int(event->position().y()));
        const Entity* entity = entityById(entityAtRow(row));
        m_hoverRow = row;
        m_hoverLeft = false;
        m_hoverRight = false;
        if (entity)
        {
            const QRect clip = clipRect(*entity);
            const int x = int(std::lround(event->position().x()));
            const int startFrame = clipStartOf(*entity);
            const int endFrame = clipEndOf(*entity);
            setToolTip(QStringLiteral("%1\n%2–%3 帧 · 时长 %4 帧")
                           .arg(entity->name)
                           .arg(startFrame)
                           .arg(endFrame)
                           .arg(endFrame - startFrame));
            QString tip = toolTip();
            if (entity->locked)
                tip += QStringLiteral("\n已锁定（不能拖动或裁剪）");
            if (entity->hidden)
                tip += QStringLiteral("\n已隐藏");
            if (entity->isAudio())
            {
                const QString hash =
                    entity->data.value(QStringLiteral("assetHash")).toString();
                if (!hash.isEmpty())
                    tip += QStringLiteral("\n素材 %1…").arg(hash.left(12));
            }
            setToolTip(tip);
            if (!entity->locked && m_editMode == EditMode::Clips
                && (std::abs(x - clip.left()) <= kTrimGrab
                    || std::abs(x - clip.right()) <= kTrimGrab))
                setCursor(Qt::SizeHorCursor);
            else if (!entity->locked
                     && hitKeyframeRow(row, x,
                                       int(std::lround(event->position().y())))
                            >= 0)
                setCursor(Qt::PointingHandCursor);
            else if (clip.contains(event->position().toPoint()))
                setCursor(entity->locked ? Qt::ArrowCursor
                                         : Qt::OpenHandCursor);
            else
                unsetCursor();
        }
        else
        {
            unsetCursor();
            setToolTip(QString());
        }
        update();
        return;
    }

    m_lastDragPos = event->position();
    if (m_dragMode != DragMode::Scrub
        && (event->position().x() > visibleWidth() - 24
            || event->position().x() < 24
            || event->position().y() > bodyBottom() - 20
            || event->position().y() < kRulerHeight + 20))
    {
        m_autoScroll->start(30);
    }
    updateDragPreview(event->position());
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_dragMode == DragMode::MoveMarker)
    {
        finishMarkerDrag();
        return;
    }
    if (m_dragMode == DragMode::PendingSelect)
    {
        m_dragMode = DragMode::None;
        m_frame = std::max(0, m_pressFrame);
        emit frameRequested(m_frame);
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        if (m_pressEntity != kInvalidEntity
            && (sessionMode() || !isEntityLocked(m_pressEntity)))
            selectEntity(m_pressEntity, true);
        update();
        return;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        const QVector<EntityId> rows = rowIds();
        // The old timeline's rubber band always picked keyframes, in every
        // edit mode.  Keep that muscle memory instead of making clip mode
        // change what a drag means.
        const int topRow = rowAt(int(box.top()));
        const int bottomRow = rowAt(int(box.bottom()));
        const int firstRow = bottomRow >= 0 ? bottomRow : 0;
        const int lastRow = topRow >= 0 ? topRow : int(rows.size()) - 1;
        const int firstFrame = std::max(0, frameFromX(int(box.left())));
        const int lastFrame = std::max(0, frameFromX(int(box.right())));
        const bool additive = event->modifiers() & Qt::ShiftModifier;
        if (!additive)
            m_selectedKeyframes.clear();
        for (int row = std::max(0, firstRow);
             row <= std::min(lastRow, int(rows.size()) - 1); ++row)
        {
            const Entity* entity = entityById(rows[row]);
            if (!entity || isEntityLocked(entity->id))
                continue;
            for (int frame : keyframeFrames(*entity))
            {
                if (frame < firstFrame || frame > lastFrame)
                    continue;
                const KeyframeSelection selection{entity->id, double(frame)};
                if (std::find(m_selectedKeyframes.begin(),
                              m_selectedKeyframes.end(), selection)
                    == m_selectedKeyframes.end())
                    m_selectedKeyframes.append(selection);
            }
        }
        // The lowest selected row becomes the active entity.
        if (firstRow >= 0 && firstRow < rows.size())
        {
            const Entity* last = entityById(rows[firstRow]);
            if (last && !isEntityLocked(last->id))
                selectEntity(last->id, true);
        }
        emit keyframeSelectionChanged(m_selectedKeyframes);
        m_dragMode = DragMode::None;
        return;
    }
    if (m_dragMode == DragMode::Scrub)
    {
        if (m_autoScroll)
            m_autoScroll->stop();
        m_dragMode = DragMode::None;
        return;
    }
    m_lastDragPos = event->position();
    finishSessionDrag();
}

void TimelinePanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!sessionMode() || event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight)
        return;
    if (event->position().x() < nameWidth())
        return;
    const int row = rowAt(int(event->position().y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || isEntityLocked(id))
        return;

    const int x = int(std::lround(event->position().x()));
    const int keyFrame =
        hitKeyframeRow(row, x, int(std::lround(event->position().y())));
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(KeyframeSelection{id, double(keyFrame), QString()});
        const QStringList frameTracks = tracksAtFrame(id, keyFrame);
        if (!frameTracks.isEmpty())
            m_selectedTrack = frameTracks.first();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();
        // Easing lives in the Properties keyframe section, not on the clip.
        return;
    }

    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->position().toPoint()))
        return;
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(event->position().toPoint()))
        return;

    const int frame = std::max(0, frameFromX(x));
    m_frame = frame;
    emit frameRequested(m_frame);
    selectEntity(id, true);

    const Uuid uuid = uuidForEntityId(id);
    const QStringList tracks =
        (m_session && m_session->editActions())
            ? m_session->editActions()->trackNames(uuid)
            : QStringList();
    if (m_selectedTrack.isEmpty() || !tracks.contains(m_selectedTrack))
        promptTrackAndAddKey(id, frame, event->globalPosition().toPoint());
    else
        addKeyframeAt(id, frame, m_selectedTrack);
    update();
}

} // namespace pvf
