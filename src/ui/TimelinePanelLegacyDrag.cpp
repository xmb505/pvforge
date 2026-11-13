#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf {

using ui::detail::childIds;
using ui::detail::collectSubtree;
using ui::detail::findEntity;

void TimelinePanel::beginDrag()
{
    m_preview = m_before;
    m_dragging = false;
    m_moved = false;
}

void TimelinePanel::resetPreview()
{
    m_preview = m_before;
}

void TimelinePanel::endDrag(const QString& text)
{
    if (m_autoScroll)
        m_autoScroll->stop();
    if (m_dragging && m_moved && !m_before.isEmpty())
        emit snapshotEditRequested(m_before, m_preview, text);
    m_dragging = false;
    m_preview.clear();
    m_before.clear();
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    m_moved = false;
    update();
    syncPopups();
}

void TimelinePanel::applyShift(QVector<Entity>& entities, EntityId id, int delta)
{
    QSet<EntityId> ids;
    collectSubtree(entities, id, ids);
    int minStart = 1 << 30;
    for (EntityId entityId : ids)
        if (const Entity* entity = findEntity(entities, entityId))
            minStart = std::min(minStart, clipStartOf(*entity));
    if (minStart == (1 << 30))
        return;
    const int lower = std::max(0, contentStart());
    delta = std::max(delta, lower - minStart);
    if (delta == 0)
        return;
    for (EntityId entityId : ids)
    {
        Entity* entity = findEntity(entities, entityId);
        if (!entity)
            continue;
        setClipRange(*entity, clipStartOf(*entity) + delta,
                     clipEndOf(*entity) + delta);
        for (auto track = entity->tracks.begin(); track != entity->tracks.end();
             ++track)
            for (Keyframe& key : track->keys)
                key.frame += delta;
    }
}

void TimelinePanel::setClipRange(Entity& entity, int start, int end)
{
    start = std::max(0, start);
    if (end <= start)
        end = start + 1;
    if (entity.isAudio())
    {
        const int oldStart = clipStartOf(entity);
        double audioStart =
            entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        audioStart += start - oldStart;
        audioStart = std::max(0.0, audioStart);
        entity.data[QStringLiteral("startFrame")] = audioStart;
        const double media =
            entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double trimStart =
            entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double fps = 30.0;
        if (media > 0.0)
        {
            double trimEnd =
                media - trimStart - (end - start) / double(fps);
            trimEnd = std::clamp(trimEnd, 0.0, std::max(0.0, media - trimStart));
            entity.data[QStringLiteral("trimEnd")] = trimEnd;
        }
        return;
    }
    entity.data[QStringLiteral("clipStart")] = start;
    entity.data[QStringLiteral("clipEnd")] = end;
}

void TimelinePanel::cleanKeys(Entity& entity)
{
    const int start = clipStartOf(entity);
    const int end = clipEndOf(entity);
    for (auto track = entity.tracks.begin(); track != entity.tracks.end(); ++track)
    {
        auto& keys = track->keys;
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [start, end](const Keyframe& key) {
                                      const int frame = int(std::lround(key.frame));
                                      return frame < start || frame >= end;
                                  }),
                   keys.end());
    }
}

void TimelinePanel::trimStartRec(QVector<Entity>& entities, EntityId parentId,
                                 int limit, QSet<EntityId>& doomed)
{
    for (EntityId childId : childIds(entities, parentId))
    {
        Entity* child = findEntity(entities, childId);
        if (!child)
            continue;
        const int childStart = clipStartOf(*child);
        const int childEnd = clipEndOf(*child);
        if (childEnd <= limit)
        {
            collectSubtree(entities, childId, doomed);
            continue;
        }
        if (childStart < limit)
        {
            setClipRange(*child, limit, childEnd);
            cleanKeys(*child);
        }
        trimStartRec(entities, childId, limit, doomed);
    }
}

void TimelinePanel::trimEndRec(QVector<Entity>& entities, EntityId parentId,
                               int limit, QSet<EntityId>& doomed)
{
    for (EntityId childId : childIds(entities, parentId))
    {
        Entity* child = findEntity(entities, childId);
        if (!child)
            continue;
        const int childStart = clipStartOf(*child);
        const int childEnd = clipEndOf(*child);
        if (childStart >= limit)
        {
            collectSubtree(entities, childId, doomed);
            continue;
        }
        if (childEnd > limit)
        {
            setClipRange(*child, childStart, limit);
            cleanKeys(*child);
        }
        trimEndRec(entities, childId, limit, doomed);
    }
}

void TimelinePanel::removeEntities(QVector<Entity>& entities,
                                   const QSet<EntityId>& doomed)
{
    if (doomed.isEmpty())
        return;
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [&doomed](const Entity& entity) {
                                      return doomed.contains(entity.id);
                                  }),
                   entities.end());
}

void TimelinePanel::trimStart(QVector<Entity>& entities, EntityId id, int newStart)
{
    Entity* entity = findEntity(entities, id);
    if (!entity)
        return;
    const int oldStart = clipStartOf(*entity);
    const int end = clipEndOf(*entity);
    newStart = std::max({0, contentStart(), std::min(newStart, end - 1)});
    setClipRange(*entity, newStart, end);
    if (newStart <= oldStart)
        return;   // expanding: keep children and do not restore keys

    cleanKeys(*entity);
    QSet<EntityId> doomed;
    trimStartRec(entities, id, newStart, doomed);
    removeEntities(entities, doomed);
}

void TimelinePanel::trimEnd(QVector<Entity>& entities, EntityId id, int newEnd)
{
    Entity* entity = findEntity(entities, id);
    if (!entity)
        return;
    const int start = clipStartOf(*entity);
    const int oldEnd = clipEndOf(*entity);
    newEnd = std::max(start + 1, newEnd);
    if (m_owner >= 0)
        newEnd = std::min(newEnd, contentEnd());
    setClipRange(*entity, start, newEnd);
    if (newEnd >= oldEnd)
        return;   // expanding: keep children and do not restore keys

    cleanKeys(*entity);
    QSet<EntityId> doomed;
    trimEndRec(entities, id, newEnd, doomed);
    removeEntities(entities, doomed);
}

void TimelinePanel::moveEntityToRow(QVector<Entity>& entities, EntityId id,
                                    int targetRow)
{
    Entity* dragged = findEntity(entities, id);
    if (!dragged)
        return;
    targetRow = std::max(0, targetRow);
    const int owner = dragged->parent;
    const int oldRow = rowOfEntity(*dragged);
    if (oldRow == targetRow)
        return;
    Entity* occupant = nullptr;
    for (Entity& entity : entities)
    {
        if (entity.id == id || entity.parent != owner)
            continue;
        if (rowOfEntity(entity) == targetRow)
        {
            occupant = &entity;
            break;
        }
    }
    if (occupant)
        occupant->data[QStringLiteral("timelineRow")] = oldRow;
    dragged->data[QStringLiteral("timelineRow")] = targetRow;
}

void TimelinePanel::reorderChild(QVector<Entity>& entities, EntityId owner,
                                 EntityId id, int targetRow)
{
    const QVector<EntityId> ids = childIds(entities, owner);
    const int from = ids.indexOf(id);
    if (from < 0)
        return;
    targetRow = std::clamp(targetRow, 0, int(ids.size()) - 1);
    if (from == targetRow)
        return;
    const EntityId targetId = ids[targetRow];
    int nodePos = -1;
    int targetPos = -1;
    for (int i = 0; i < entities.size(); ++i)
    {
        if (entities[i].id == id)
            nodePos = i;
        if (entities[i].id == targetId)
            targetPos = i;
    }
    if (nodePos < 0 || targetPos < 0)
        return;
    Entity entity = entities.takeAt(nodePos);
    if (nodePos < targetPos)
        --targetPos;
    entities.insert(from < targetRow ? targetPos + 1 : targetPos, entity);
}

void TimelinePanel::moveKeys(QVector<Entity>& entities,
                             const QVector<KeyframeSelection>& keys, int delta)
{
    if (keys.isEmpty())
        return;
    int minDelta = -(1 << 30);
    int maxDelta = 1 << 30;
    QHash<EntityId, QSet<int>> framesByEntity;
    for (const KeyframeSelection& selection : keys)
    {
        const Entity* entity = findEntity(entities, selection.entity);
        if (!entity)
            continue;
        const int frame = int(std::lround(selection.frame));
        framesByEntity[selection.entity].insert(frame);
        const int start = clipStartOf(*entity);
        const int end = clipEndOf(*entity);
        bool found = false;
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd();
             ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (int(std::lround(key.frame)) != frame)
                    continue;
                found = true;
                minDelta = std::max(minDelta, start - frame);
                maxDelta = std::min(maxDelta, end - frame);
            }
        }
        if (!found)
            framesByEntity[selection.entity].remove(frame);
    }
    delta = std::clamp(delta, minDelta, maxDelta);
    if (delta == 0)
        return;

    // Move every selected frame simultaneously: remove the original keys
    // first, then insert copies.  Otherwise adjacent selected frames would
    // pick up keys that were just moved into their range.
    for (auto selection = framesByEntity.constBegin();
         selection != framesByEntity.constEnd(); ++selection)
    {
        Entity* entity = findEntity(entities, selection.key());
        if (!entity)
            continue;
        for (auto track = entity->tracks.begin(); track != entity->tracks.end();
             ++track)
        {
            QVector<Keyframe> moved;
            for (const Keyframe& key : track->keys)
            {
                if (selection.value().contains(int(std::lround(key.frame))))
                    moved.append(key);
            }
            if (moved.isEmpty())
                continue;
            auto& trackKeys = track->keys;
            trackKeys.erase(
                std::remove_if(trackKeys.begin(), trackKeys.end(),
                               [&selection](const Keyframe& key) {
                                   return selection.value().contains(
                                       int(std::lround(key.frame)));
                               }),
                trackKeys.end());
            for (Keyframe key : moved)
            {
                key.frame += delta;
                trackKeys.append(key);
            }
            std::sort(trackKeys.begin(), trackKeys.end(),
                      [](const Keyframe& a, const Keyframe& b) {
                          return a.frame < b.frame;
                      });
        }
    }
}

void TimelinePanel::updateDragPreview(const QPointF& pos)
{
    if (sessionMode())
    {
        updateSessionDragPreview(pos);
        return;
    }
    if (m_dragMode == DragMode::None)
        return;
    if (!m_dragging)
    {
        m_dragging = true;
        resetPreview();
    }
    resetPreview();

    if (m_dragMode == DragMode::MoveClip)
    {
        const int delta = frameFromX(int(std::lround(pos.x()))) - m_dragStartFrame;
        applyShift(m_preview, m_dragEntity, delta);
        const int targetRow = dropRowForY(int(std::lround(pos.y())));
        moveEntityToRow(m_preview, m_dragEntity, targetRow);
        m_moved = delta != 0 || targetRow != m_dragStartRow;
    }
    else if (m_dragMode == DragMode::TrimStart)
    {
        Entity* entity = findEntity(m_preview, m_dragEntity);
        if (!entity)
            return;
        int newStart = frameFromX(int(std::lround(pos.x())));
        newStart = std::max({0, contentStart(),
                             std::min(newStart, clipEndOf(*entity) - 1)});
        trimStart(m_preview, m_dragEntity, newStart);
        m_moved = newStart != m_dragClipStart;
    }
    else if (m_dragMode == DragMode::TrimEnd)
    {
        Entity* entity = findEntity(m_preview, m_dragEntity);
        if (!entity)
            return;
        int newEnd = std::max(clipStartOf(*entity) + 1,
                              frameFromX(int(std::lround(pos.x()))));
        if (m_owner >= 0)
            newEnd = std::min(newEnd, contentEnd());
        trimEnd(m_preview, m_dragEntity, newEnd);
        m_moved = newEnd != m_dragClipEnd;
    }
    else if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta =
            frameFromX(int(std::lround(pos.x()))) - m_keyDragStart;
        m_frame = std::max(0, m_keyDragStart + delta);
        emit frameRequested(m_frame);
        moveKeys(m_preview, m_selectedKeyframes, delta);
        m_moved = std::any_of(
            m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
            [this, delta](const KeyframeSelection& selection) {
                const Entity* entity = findEntity(m_before, selection.entity);
                if (!entity)
                    return false;
                for (auto it = entity->tracks.constBegin();
                     it != entity->tracks.constEnd(); ++it)
                    for (const Keyframe& key : it->keys)
                        if (int(std::lround(key.frame))
                            == int(std::lround(selection.frame)))
                            return delta != 0;
                return false;
            });
    }

    update();
    syncPopups();
}

void TimelinePanel::autoScrollTick()
{
    if (!sessionMode() || m_dragMode == DragMode::None)
        return;
    if (m_dragMode == DragMode::Scrub)
    {
        const double rawX = m_lastDragPos.x();
        const int contentRight = width() - kScrollBarSize;
        int dx = 0;
        if (rawX > contentRight - 12)
            dx = 12;
        else if (rawX < nameWidth() + 12)
            dx = -12;
        else
            return;
        m_scrollX = std::max(0, m_scrollX + dx);
        clampScroll();
        // Keep the playhead pinned to the visible edge while the view scrolls.
        setScrubFrame(rawX);
        syncPopups();
        return;
    }
    if (!m_moved)
        return;
    const int contentRight = width() - kScrollBarSize;
    int dx = 0;
    int dy = 0;
    if (m_lastDragPos.x() > contentRight - 24)
        dx = 12;
    else if (m_lastDragPos.x() < nameWidth() + 24)
        dx = -12;
    if (m_lastDragPos.y() > bodyBottom() - 20)
        dy = -12;   // reveal lower rows: the bottom-anchored offset shrinks
    else if (m_lastDragPos.y() < kRulerHeight + 20)
        dy = 12;    // reveal higher rows
    if (dx == 0 && dy == 0)
        return;
    m_scrollX = std::max(0, m_scrollX + dx);
    m_scrollY = std::max(0, m_scrollY + dy);
    // Update before clamping so a growing clip end can grow the scroll range.
    updateDragPreview(m_lastDragPos);
    clampScroll();
}

} // namespace pvf
