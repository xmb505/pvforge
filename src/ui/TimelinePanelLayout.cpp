#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"
#include "model/Entity.h"
#include "model/Uuid.h"

#include <QFontMetrics>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf {

using ui::detail::findEntity;

int TimelinePanel::depthOf(EntityId id) const
{
    int depth = 0;
    QSet<EntityId> seen;
    const Entity* entity = entityById(id);
    while (entity && entity->parent != kInvalidEntity && !seen.contains(entity->parent))
    {
        seen.insert(entity->parent);
        ++depth;
        entity = entityById(entity->parent);
    }
    return depth;
}

int TimelinePanel::rowOfEntity(const Entity& entity) const
{
    return entity.data.value(QStringLiteral("timelineRow"), -1).toInt();
}

void TimelinePanel::assignMissingRows()
{
    if (sessionMode())
        return;
}

bool TimelinePanel::isValidRow(int row) const
{
    return m_popup ? row != kInvalidRow : row >= 0;
}

QVector<const Entity*> TimelinePanel::entitiesInRow(int row) const
{
    QVector<const Entity*> result;
    if (!isValidRow(row))
        return result;
    const QVector<Entity>& all = activeEntities();
    // One id set instead of a linear entityById() lookup per entity (#132).
    QSet<EntityId> knownIds;
    knownIds.reserve(all.size());
    for (const Entity& entity : all)
        knownIds.insert(entity.id);
    for (const Entity& entity : all)
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && !knownIds.contains(entity.parent);
        if (entity.parent != m_owner
            && !(m_owner == kInvalidEntity && dangling))
            continue;
        if (!rowMatchesFilter(entity))
            continue;
        if (rowOfEntity(entity) == row)
            result.append(&entity);
    }
    return result;
}

void TimelinePanel::popupTrackBounds(int* minNeg, int* maxPos) const
{
    int minOccupiedNeg = 0;
    int maxOccupiedPos = -1;
    // During a session clip drag the preview already contains the new row.
    // Resolve the tracks from the gesture-start snapshot so the blank target
    // row cannot move under the pointer while the preview grows the popup.
    const QVector<Entity>& entities =
        sessionMode() && m_dragMode == DragMode::MoveClip
            ? m_sessionBaseSnapshot.entities
            : activeEntities();
    for (const Entity& entity : entities)
    {
        if (entity.parent != m_owner)
            continue;
        const int r = rowOfEntity(entity);
        if (r < 0)
            minOccupiedNeg = std::min(minOccupiedNeg, r);
        else
            maxOccupiedPos = std::max(maxOccupiedPos, r);
    }
    // Always keep at least 1 negative track (-1) and 1 positive track (0 -> +1);
    // auto-expand by 1 empty track whenever the outermost track is occupied.
    const int neg = minOccupiedNeg < 0 ? minOccupiedNeg - 1 : -1;
    const int pos = maxOccupiedPos >= 0 ? maxOccupiedPos + 1 : 0;
    if (minNeg)
        *minNeg = neg;
    if (maxPos)
        *maxPos = pos;
}

int TimelinePanel::popupDividerTop() const
{
    int minNeg = -1;
    popupTrackBounds(&minNeg, nullptr);
    const int negCount = -minNeg;
    return bodyBottom() - negCount * kRowHeight + m_scrollY - kPopupDividerHeight;
}

QVector<EntityId> TimelinePanel::rowIds() const
{
    QHash<int, EntityId> byRow;
    int maxRow = -1;
    for (const Entity& entity : activeEntities())
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && entityById(entity.parent) == nullptr;
        if (entity.parent != m_owner
            && !(m_owner == kInvalidEntity && dangling))
            continue;
        if (!rowMatchesFilter(entity))
            continue;
        const int row = rowOfEntity(entity);
        if (row < 0)
            continue;
        if (!byRow.contains(row))
            byRow.insert(row, entity.id);
        maxRow = std::max(maxRow, row);
    }

    const int bodyHeight = std::max(0, bodyBottom() - kRulerHeight);
    const int pageRows =
        std::max(1, int(std::ceil(bodyHeight / double(kRowHeight))));
    const int count = std::max(maxRow + 1, pageRows + 3);
    QVector<EntityId> rows(count, kInvalidEntity);
    for (auto it = byRow.constBegin(); it != byRow.constEnd(); ++it)
        if (it.key() >= 0 && it.key() < rows.size())
            rows[it.key()] = it.value();
    return rows;
}

EntityId TimelinePanel::entityAtRow(int row) const
{
    const QVector<const Entity*> list = entitiesInRow(row);
    return list.isEmpty() ? kInvalidEntity : list.first()->id;
}

int TimelinePanel::rowTop(int row) const
{
    if (m_popup)
    {
        int minNeg = -1;
        popupTrackBounds(&minNeg, nullptr);
        if (row < 0)
        {
            const int negIndex = row - minNeg; // 0 = bottom-most, negCount-1 = -1
            return bodyBottom() - (negIndex + 1) * kRowHeight + 1 + m_scrollY;
        }
        return popupDividerTop() - (row + 1) * kRowHeight + 1;
    }
    // Bottom-up: row 0 sits on the body bottom, larger rows stack upwards.
    return bodyBottom() - (row + 1) * kRowHeight + 1 + m_scrollY;
}

int TimelinePanel::nameWidth() const
{
    // Clip-first: the entity name is drawn inside each clip, so the timeline
    // content starts at x=0 for both the main panel and private timelines.
    return 0;
}

QString TimelinePanel::searchFilter() const
{
    // Entity search lives in the asset bin on the left; the timeline itself
    // never filters clips by name.
    return QString();
}

bool TimelinePanel::rowMatchesFilter(const Entity&) const
{
    return true;
}

void TimelinePanel::toggleSessionFlag(EntityId id, bool hidden)
{
    if (!m_session || !m_session->editActions())
        return;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* source = sessionEntity(uuid);
    if (source == nullptr)
        return;
    const bool target = hidden ? !source->hidden : !source->locked;
    const QString text = hidden
                             ? (target ? QStringLiteral("隐藏片段")
                                       : QStringLiteral("显示片段"))
                             : (target ? QStringLiteral("锁定片段")
                                       : QStringLiteral("解锁片段"));
    QString error;
    if (!m_session->editActions()->updateEntity(
            uuid, text,
            [hidden, target](model::Entity& entity) {
                if (hidden)
                    entity.hidden = target;
                else
                    entity.locked = target;
                return true;
            },
            &error, true))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    if (!hidden && target)
    {
        m_selectedKeyframes.erase(
            std::remove_if(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                           [id](const KeyframeSelection& selection) {
                               return selection.entity == id;
                           }),
            m_selectedKeyframes.end());
        emit keyframeSelectionChanged(m_selectedKeyframes);
    }
    emit editCommitted(text);
    refreshFromSession();
}

void TimelinePanel::handleRowButton(int row, int index)
{
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || id == kInvalidEntity || index < 0 || index > 1)
        return;

    if (sessionMode())
    {
        toggleSessionFlag(id, index == 0);
        return;
    }

    emit entityFlagChanged(id, index == 0 ? EntityFlag::Hidden
                                         : EntityFlag::Locked,
                           !(index == 0 ? entity->hidden : entity->locked));
    update();
}

bool TimelinePanel::clipShowsInlineButtons(const Entity& entity) const
{
    const QRect clip = clipRect(entity);
    return !clip.isEmpty() && clip.width() >= kInlineButtonsMinWidth;
}

int TimelinePanel::clipIconCount(const Entity& entity) const
{
    const QRect clip = clipRect(entity);
    if (clip.isEmpty())
        return 0;
    if (m_altHeld)
        return clip.width() >= kInlineButtonsMinWidth ? 2 : 0;
    // Status indicators are not click targets, so even short clips can show
    // the hidden/locked mark.
    if (clip.width() < kButtonSize + 4)
        return 0;
    return int(entity.hidden) + int(entity.locked);
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kRulerHeight || y >= bodyBottom())
        return kInvalidRow;
    if (m_popup)
    {
        int minNeg = -1;
        int maxPos = 0;
        popupTrackBounds(&minNeg, &maxPos);
        const int divTop = popupDividerTop();
        const int divBot = divTop + kPopupDividerHeight;
        if (y >= divTop && y < divBot)
            return kInvalidRow;
        if (y < divTop)
        {
            const int row = (divTop - y) / kRowHeight;
            return (row >= 0 && row <= maxPos) ? row : kInvalidRow;
        }
        const int negIndex = (bodyBottom() - y + m_scrollY) / kRowHeight;
        const int row = minNeg + negIndex;
        return (row >= minNeg && row <= -1) ? row : kInvalidRow;
    }
    const int row = (bodyBottom() - y + m_scrollY) / kRowHeight;
    return row >= 0 && row < rowIds().size() ? row : kInvalidRow;
}

int TimelinePanel::dropRowForY(int y) const
{
    if (m_popup)
    {
        int minNeg = -1;
        int maxPos = 0;
        popupTrackBounds(&minNeg, &maxPos);
        const int divMid = popupDividerTop() + kPopupDividerHeight / 2;
        if (y < divMid)
        {
            if (y < kRulerHeight)
                return maxPos;
            const int divTop = popupDividerTop();
            const int row = std::max(0, (divTop - y) / kRowHeight);
            return std::min(row, maxPos);
        }
        if (y >= bodyBottom())
            return minNeg;
        const int negIndex = std::max(0, (bodyBottom() - y + m_scrollY) / kRowHeight);
        return std::clamp(minNeg + negIndex, minNeg, -1);
    }
    const int highest = highestRowForDrop();
    if (y < kRulerHeight)
        return highest + 1;
    if (y >= bodyBottom())
        return 0;
    const int row = std::max(0, (bodyBottom() - y + m_scrollY) / kRowHeight);
    // Empty space above the top clip is unbounded; land right above it.
    return std::min(row, highest + 1);
}

int TimelinePanel::highestRowForDrop() const
{
    // Clamp against the rows at gesture start, otherwise the preview clip
    // would raise its own ceiling every time it moves and run away.
    const QVector<Entity>& entities =
        sessionMode() && m_dragMode == DragMode::MoveClip
            ? m_sessionBaseSnapshot.entities
            : (!sessionMode() && m_dragging ? m_before : activeEntities());
    int highest = -1;
    for (const Entity& entity : entities)
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && entityById(entity.parent) == nullptr;
        if (entity.parent != m_owner
            && !(m_owner == kInvalidEntity && dangling))
            continue;
        const int row = rowOfEntity(entity);
        if (row >= 0)
            highest = std::max(highest, row);
    }
    return highest;
}

int TimelinePanel::contentStart() const
{
    if (m_owner < 0)
        return 0;
    const Entity* ownerEntity = rangeOwner();
    return ownerEntity ? clipStartOf(*ownerEntity) : 0;
}

int TimelinePanel::displayFrame(int frame) const
{
    // Only a private timeline in relative mode renumbers its window.  The
    // playhead, clips and keyframes all keep absolute project frames; this
    // helper is read by the ruler painter alone.
    if (m_popup && m_relativeTime)
        return frame - contentStart();
    return frame;
}

const QVector<Entity>& TimelinePanel::activeEntities() const
{
    return m_snapshot.entities;
}

const Entity* TimelinePanel::rangeOwner() const
{
    if (m_owner < 0)
        return nullptr;
    return findEntity(activeEntities(), m_owner);
}

Entity* TimelinePanel::activeEntity(EntityId id)
{
    if (sessionMode())
        return nullptr;
    return findEntity(m_preview, id);
}

int TimelinePanel::clipStartOf(const Entity& entity) const
{
    if (sessionMode())
        return std::max(0, entity.data.value(QStringLiteral("clipStart"), 0).toInt());
    if (entity.isAudio())
        return std::max(0, int(std::floor(
                               entity.data.value(QStringLiteral("startFrame"), 0.0)
                                   .toDouble())));
    return std::max(0, entity.data.value(QStringLiteral("clipStart"), 0).toInt());
}

int TimelinePanel::clipEndOf(const Entity& entity) const
{
    const int start = clipStartOf(entity);
    if (sessionMode())
    {
        const int stored = entity.data.value(QStringLiteral("clipEnd"), start)
                               .toInt();
        return std::max(start + 1, stored);
    }
    if (entity.isAudio()
        && entity.data.contains(QStringLiteral("duration")))
    {
        const double media =
            entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double trimStart =
            entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            entity.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        const double fps = 30.0;
        const double frames =
            std::max(0.0, (media - trimStart - trimEnd) * fps);
        return start + std::max(1, int(std::ceil(frames)));
    }
    const int stored = entity.data.value(QStringLiteral("clipEnd"), 0).toInt();
    if (stored > start)
        return stored;
    return std::max(start + 1, 180);
}

int TimelinePanel::contentEnd() const
{
    if (m_owner >= 0)
    {
        const Entity* ownerEntity = rangeOwner();
        if (!ownerEntity)
            return 180;
        return std::max(contentStart() + 1, clipEndOf(*ownerEntity));
    }
    int end = 180;
    for (const Entity& entity : activeEntities())
        end = std::max(end, clipEndOf(entity));
    if (sessionMode())
        end = std::max(end, m_snapshot.contentEnd);
    return end;
}

QRect TimelinePanel::clipRect(const Entity& entity) const
{
    const bool dangling = entity.parent != kInvalidEntity
                          && entityById(entity.parent) == nullptr;
    if (entity.parent != m_owner
        && !(m_owner == kInvalidEntity && dangling))
        return QRect();
    const int row = rowOfEntity(entity);
    if (!isValidRow(row))
        return QRect();
    const int start = std::max(clipStartOf(entity), contentStart());
    const int end = std::min(clipEndOf(entity), contentEnd());
    if (end <= start)
        return QRect();
    const int raw0 = xFromFrame(start);
    const int raw1 = xFromFrame(end);
    if (raw1 <= nameWidth())
        return QRect();
    const int x0 = std::max(nameWidth(), raw0);
    const int x1 = std::max(x0 + 1, raw1);
    return QRect(x0, rowTop(row) + 3, std::max(1, x1 - x0), kRowHeight - 6);
}

QRect TimelinePanel::clipRectFor(EntityId id) const
{
    const Entity* entity = findEntity(activeEntities(), id);
    return entity ? clipRect(*entity) : QRect();
}

QVector<int> TimelinePanel::keyframeFrames(const Entity& entity) const
{
    QVector<int> result;
    result.reserve(entity.keyframes.size());
    for (const Keyframe& key : entity.keyframes)
        result.append(int(std::lround(key.frame)));
    std::sort(result.begin(), result.end());
    return result;
}

bool TimelinePanel::keyframeHasNote(EntityId id, double frame) const
{
    const Entity* entity = entityById(id);
    if (!entity)
        return false;
    for (const Keyframe& key : entity->keyframes)
        if (std::abs(key.frame - frame) < 0.5 && !key.note.isEmpty())
            return true;
    return false;
}

int TimelinePanel::visibleWidth() const
{
    return std::max(1, width() - nameWidth() - kScrollBarSize);
}

int TimelinePanel::bodyBottom() const
{
    return std::max(kRulerHeight, height() - kScrollBarSize);
}

int TimelinePanel::timelineLength() const
{
    return std::max(1, contentEnd());
}

int TimelinePanel::xFromFrame(int frame) const
{
    return nameWidth()
           + int(std::lround((frame - contentStart()) * m_pixelsPerFrame
                             - m_scrollX));
}

int TimelinePanel::frameFromX(int x) const
{
    return std::max(0, int(std::lround(
                           contentStart()
                           + (x - nameWidth() + m_scrollX) / m_pixelsPerFrame)));
}

void TimelinePanel::clampScroll()
{
    if (!sessionMode())
        return;
    const int visible = visibleWidth();
    const int tailPad = std::max(400, visible);
    const int contentWidth =
        int((contentEnd() - contentStart()) * m_pixelsPerFrame) + tailPad;
    const int maxScroll = std::max(0, contentWidth - visible);
    m_scrollX = std::clamp(m_scrollX, 0, maxScroll);
    if (m_hScroll)
    {
        const QSignalBlocker blocker(m_hScroll);
        m_hScroll->setRange(0, maxScroll);
        m_hScroll->setPageStep(visible);
        m_hScroll->setSingleStep(60);
        m_hScroll->setValue(m_scrollX);
        m_hScroll->setVisible(true);
    }

    int totalContentHeight = int(rowIds().size()) * kRowHeight;
    if (m_popup)
    {
        int minNeg = -1;
        int maxPos = 0;
        popupTrackBounds(&minNeg, &maxPos);
        totalContentHeight =
            ((-minNeg) + (maxPos + 1)) * kRowHeight + kPopupDividerHeight;
    }
    const int bodyHeight = std::max(0, bodyBottom() - kRulerHeight);
    const int maxY = std::max(0, totalContentHeight - bodyHeight);
    m_scrollY = std::clamp(m_scrollY, 0, maxY);
    if (m_vScroll)
    {
        const QSignalBlocker blocker(m_vScroll);
        m_vScroll->setRange(0, maxY);
        m_vScroll->setPageStep(std::max(1, bodyHeight));
        m_vScroll->setSingleStep(kRowHeight);
        // Top of the bar = topmost rows, bottom = row 0.
        m_vScroll->setValue(maxY - m_scrollY);
        m_vScroll->setVisible(true);
    }
}

void TimelinePanel::updateScrollBars()
{
    clampScroll();
}

void TimelinePanel::ensureFrameVisible(int frame)
{
    const int x = xFromFrame(frame);
    const int left = nameWidth() + 24;
    const int right = width() - kScrollBarSize - 24;
    const int margin = 24;
    if (x < left)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - margin)));
    else if (x > right)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - (right - left))));
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::setScrubFrame(double rawX)
{
    // The pointer can leave the widget while scrubbing. Clamp it to the
    // visible track so the playhead line can never disappear outside it.
    const int x = std::clamp(int(std::lround(rawX)), nameWidth() + 1,
                             width() - kScrollBarSize - 1);
    m_frame = std::max(0, frameFromX(x));
    // Audible signal first so the shell can capture the previous playhead
    // before the generic frameRequested connection updates its widget state.
    emit scrubFrameRequested(m_frame);
    emit frameRequested(m_frame);
    update();
}

void TimelinePanel::stepFrame(int delta)
{
    int next = m_frame + delta;
    if (next < 0)
    {
        next = 0;
        stopHold();
    }

    const int length = timelineLength();
    // The playhead loops: stepping past the end returns to frame 0.
    if (delta > 0 && length > 0 && next >= length)
        next = 0;

    m_frame = std::max(0, next);
    emit scrubFrameRequested(m_frame);
    emit frameRequested(m_frame);
    update();
}

void TimelinePanel::stopHold()
{
    m_holdDirection = 0;
    if (m_holdTimer)
        m_holdTimer->stop();
    if (m_holdDelay)
        m_holdDelay->stop();
}

QRect TimelinePanel::markerRect(int index) const
{
    if (index < 0 || index >= m_snapshot.markers.size())
        return QRect();
    const TimelineMarkerView& marker = m_snapshot.markers.at(index);
    if (marker.frame < contentStart() || marker.frame > contentEnd())
        return QRect();
    const int x = xFromFrame(marker.frame);
    const int right = width() - kScrollBarSize;
    if (x < nameWidth() || x > right)
        return QRect();
    const QFontMetrics metrics(font());
    const int markerWidth = std::max(
        10, std::min(140, metrics.horizontalAdvance(marker.title) + 12));
    return QRect(x + 1, kRulerHeight - 14, markerWidth, 12);
}

QRect TimelinePanel::clipButtonRect(const Entity& entity, int index) const
{
    if (index < 0 || index > 1)
        return QRect();
    const QRect clip = clipRect(entity);
    if (clip.isEmpty())
        return QRect();
    // The row buttons live inside the clip's left edge.
    const int x = clip.left() + 2 + index * (kButtonSize + kButtonGap);
    const int y = clip.top() + (clip.height() - kButtonSize) / 2;
    return QRect(x, y, kButtonSize, kButtonSize);
}

QRect TimelinePanel::playheadHandleRect() const
{
    const int center = xFromFrame(m_frame);
    return QRect(center - kPlayheadHandleWidth / 2, 0, kPlayheadHandleWidth,
                 kPlayheadHandleHeight);
}

QRect TimelinePanel::rowButtonRect(int row, int index) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? clipButtonRect(*entity, index) : QRect();
}

int TimelinePanel::hitKeyframeRow(int row, int x, int y) const
{
    // Alt is the clip layer: diamonds are dimmed and cannot be grabbed.
    if (m_altHeld)
        return -1;
    int best = -1;
    int bestDistance = 10;
    for (const Entity* entity : entitiesInRow(row))
    {
        const QRect clip = clipRect(*entity);
        if (clip.isEmpty() || std::abs(y - clip.center().y()) > 9)
            continue;
        for (int frame : keyframeFrames(*entity))
        {
            const int distance = std::abs(x - xFromFrame(frame));
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = frame;
            }
        }
    }
    return best;
}

EntityId TimelinePanel::hitClipRow(int row, int x) const
{
    for (const Entity* entity : entitiesInRow(row))
    {
        const QRect clip = clipRect(*entity);
        // The end-boundary diamond is drawn half outside the clip body, so a
        // small horizontal tolerance keeps it clickable.
        if (clip.adjusted(-6, 0, 6, 0).contains(x, clip.center().y()))
            return entity->id;
    }
    return kInvalidEntity;
}

} // namespace pvf
