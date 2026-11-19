#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"
#include "app/EditActionsSupport.h"
#include "app/TimelineAdapter.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"

#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <climits>
#include <cmath>

namespace pvf {

using ui::detail::childIds;
using ui::detail::findEntity;

const model::Entity* TimelinePanel::sessionEntity(const Uuid& uuid) const
{
    if (!m_session || uuid.isEmpty())
        return nullptr;
    const auto it = m_session->project().entities.constFind(uuid);
    return it == m_session->project().entities.constEnd() ? nullptr : &it.value();
}

void TimelinePanel::beginSessionDrag()
{
    m_sessionBaseSnapshot = m_snapshot;
    m_sessionPlan = EditPlan();
    m_sessionPlanValid = false;
    m_dragging = false;
    m_moved = false;
    if (m_autoScroll)
        m_autoScroll->start(30);
    update();
}

void TimelinePanel::updateSessionDragPreview(const QPointF& pos)
{
    if (m_dragMode != DragMode::MoveClip
        && m_dragMode != DragMode::TrimStart
        && m_dragMode != DragMode::TrimEnd
        && m_dragMode != DragMode::MoveKeyframe)
        return;
    if (!m_session || !m_session->editActions() || !m_adapter)
        return;
    const Uuid uuid = uuidForEntityId(m_dragEntity);
    if (uuid.isEmpty())
        return;
    EditActions* actions = m_session->editActions();

    EditPlan plan;
    QString error;
    if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta = frameFromX(int(std::lround(pos.x()))) - m_keyDragStart;
        const int movedFrame = std::max(0, m_keyDragStart + delta);
        if (movedFrame != m_frame)
        {
            m_frame = movedFrame;
            emit frameRequested(m_frame);
        }
        if (delta != 0)
        {
            if (!actions->planMoveKeyframes(selectedKeyRefs(), delta, &plan,
                                            &error))
            {
                m_snapshot = m_sessionBaseSnapshot;
                m_sessionPlanValid = false;
                m_moved = false;
                update();
                syncPopups();
                return;
            }
        }
        if (plan.isEmpty())
        {
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
        m_sessionPlan = plan;
        m_sessionPlanValid = true;
        m_moved = true;
        m_snapshot = m_adapter->buildSnapshot(plan.project, m_frame);
        update();
        syncPopups();
        return;
    }

    if (m_dragMode == DragMode::MoveClip)
    {
        int delta = frameFromX(int(std::lround(pos.x()))) - m_dragStartFrame;
        const QVector<Uuid> clampRoots =
            m_dragRoots.size() > 1 ? m_dragRoots : QVector<Uuid>{uuid};
        const int lower = actions->minMoveDelta(clampRoots);
        delta = std::max(delta, lower);
        const int targetRow = dropRowForY(int(std::lround(pos.y())));
        if (delta == 0 && targetRow == m_dragStartRow)
        {
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
        // A multi-selection moves as one block in time; the pointer row only
        // pins a single dragged clip.  Shared rows may split once the whole
        // block shifts, so validation decides instead of a silent swap.
        const bool multi = m_dragRoots.size() > 1;
        if (multi ? !actions->planMove(m_dragRoots, delta, {}, &plan, &error)
                  : !actions->planMoveToRow(uuid, delta, targetRow, &plan,
                                            &error))
        {
            // A rejected plan (parent bounds, lock) restores the base preview;
            // the release handler reports the error once.
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
    }
    else
    {
        int newStart = m_dragClipStart;
        int newEnd = m_dragClipEnd;
        if (m_dragMode == DragMode::TrimStart)
        {
            const int lower = actions->minTrimStart(uuid);
            const int upper = m_dragClipEnd - 1;
            if (upper < lower)
                return;
            newStart = std::clamp(frameFromX(int(std::lround(pos.x()))), lower,
                                  upper);
        }
        else
        {
            const int lower = m_dragClipStart + 1;
            const int upper =
                std::max(lower, actions->maxTrimEnd(uuid));
            newEnd = std::clamp(frameFromX(int(std::lround(pos.x()))), lower,
                                upper);
        }
        if (newStart == m_dragClipStart && newEnd == m_dragClipEnd)
        {
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
        if (!actions->planTrim(uuid, newStart, newEnd, &plan, &error))
            return;
    }

    if (plan.isEmpty())
    {
        m_snapshot = m_sessionBaseSnapshot;
        m_sessionPlanValid = false;
        m_moved = false;
        update();
        syncPopups();
        return;
    }

    m_sessionPlan = plan;
    m_sessionPlanValid = true;
    m_moved = true;
    m_snapshot = m_adapter->buildSnapshot(plan.project, m_frame);
    update();
    syncPopups();
}

EntityId TimelinePanel::sessionBaseEntityAtRow(int row) const
{
    if (!isValidRow(row))
        return kInvalidEntity;
    for (const Entity& entity : m_sessionBaseSnapshot.entities)
    {
        if (entity.parent != m_owner)
            continue;
        if (rowOfEntity(entity) == row)
            return entity.id;
    }
    return kInvalidEntity;
}

QRect TimelinePanel::sessionBaseClipRect(const Entity& entity) const
{
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

bool TimelinePanel::isReparentCandidate(const Uuid& entity,
                                        const Uuid& target) const
{
    if (!m_session || entity.isEmpty() || target.isEmpty() || entity == target)
        return false;
    const model::Project& project = m_session->project();
    const auto source = project.entities.constFind(entity);
    const auto targetIt = project.entities.constFind(target);
    if (source == project.entities.constEnd()
        || targetIt == project.entities.constEnd())
        return false;
    if (target == source.value().parentUuid)
        return false;
    // Cannot parent an entity under its own subtree.
    Uuid parent = target;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (parent == entity)
            return false;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return !next::isEntityLocked(project, target);
}

int TimelinePanel::promptReparentMode(const Uuid& target)
{
    const model::Entity* parent = sessionEntity(target);
    const QString name = parent && !parent->name.isEmpty()
                             ? parent->name
                             : QStringLiteral("这个片段");
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("更换父级"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("把片段挂到「%1」的私有时间线？").arg(name));
    box.setInformativeText(
        QStringLiteral("保持世界位置：绝对帧不变，必要时会拒绝。\n"
                       "保持局部坐标：保留相对父级起点的偏移。"));
    auto* keepWorld =
        box.addButton(QStringLiteral("保持世界位置"), QMessageBox::AcceptRole);
    auto* keepLocal =
        box.addButton(QStringLiteral("保持局部坐标"), QMessageBox::AcceptRole);
    auto* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(keepWorld);
    box.exec();
    if (box.clickedButton() == keepWorld)
        return 1;
    if (box.clickedButton() == keepLocal)
        return 2;
    Q_UNUSED(cancel);
    return 0;
}

void TimelinePanel::requestDeleteSelection()
{
    if (!m_selectedKeyframes.isEmpty())
        deleteSelectedKeyframes();
    else if (m_selected != kInvalidEntity)
        deleteSelectedEntities();
}

QVector<Uuid> TimelinePanel::selectedUuids() const
{
    QVector<Uuid> uuids;
    for (const Entity& entity : activeEntities())
    {
        if (entity.id != m_selected && !m_entitySelection.contains(entity.id))
            continue;
        const Uuid uuid = uuidForEntityId(entity.id);
        if (!uuid.isEmpty() && !uuids.contains(uuid))
            uuids.append(uuid);
    }
    return uuids;
}

void TimelinePanel::copySelectedClips()
{
    if (!sessionMode() || !m_session)
        return;
    const QVector<Uuid> roots = selectedUuids();
    if (roots.isEmpty())
        return;
    const model::Project& project = m_session->project();
    QVector<model::Entity> clipboard;
    QSet<Uuid> seen;
    for (const Uuid& root : roots)
    {
        for (const Uuid& uuid : edit_detail::subtree(project, root))
        {
            if (seen.contains(uuid))
                continue;
            seen.insert(uuid);
            clipboard.append(project.entities.value(uuid));
        }
    }
    m_clipClipboard = clipboard;
    emit editCommitted(roots.size() > 1
                           ? QStringLiteral("已复制 %1 个片段").arg(roots.size())
                           : QStringLiteral("已复制片段"));
}

void TimelinePanel::pasteClips()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    if (m_clipClipboard.isEmpty())
        return;
    const Uuid parent = m_owner >= 0 ? uuidForEntityId(m_owner) : Uuid();
    int minStart = INT_MAX;
    QSet<Uuid> ids;
    for (const model::Entity& entity : m_clipClipboard)
        ids.insert(entity.uuid);
    for (const model::Entity& entity : m_clipClipboard)
        if (entity.parentUuid.isEmpty() || !ids.contains(entity.parentUuid))
            minStart = std::min(minStart, entity.start);
    const int delta = m_frame - (minStart == INT_MAX ? m_frame : minStart);
    QString error;
    const QVector<Uuid> created =
        m_session->editActions()->pasteEntities(m_clipClipboard, parent, delta,
                                                &error);
    if (created.isEmpty())
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_entitySelection.clear();
    for (const Uuid& uuid : created)
    {
        const EntityId id = entityIdForUuid(uuid);
        if (id != kInvalidEntity)
            m_entitySelection.insert(id);
    }
    if (!created.isEmpty())
        m_selected = entityIdForUuid(created.last());
    emit entitySelected(m_selected);
    emit editCommitted(QStringLiteral("粘贴片段"));
    refreshFromSession();
}

void TimelinePanel::duplicateSelectedClips()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const QVector<Uuid> roots = selectedUuids();
    if (roots.isEmpty())
        return;
    const Uuid parent = m_session->project().entities.value(roots.first()).parentUuid;
    QString error;
    const QVector<Uuid> created = m_session->editActions()->duplicateEntities(
        roots, 0, parent, &error);
    if (created.isEmpty())
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_entitySelection.clear();
    for (const Uuid& uuid : created)
    {
        const EntityId id = entityIdForUuid(uuid);
        if (id != kInvalidEntity)
            m_entitySelection.insert(id);
    }
    m_selected = created.isEmpty() ? kInvalidEntity
                                   : entityIdForUuid(created.last());
    emit entitySelected(m_selected);
    emit editCommitted(QStringLiteral("复制片段"));
    refreshFromSession();
}

void TimelinePanel::groupSelectedClips()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const QVector<Uuid> roots = selectedUuids();
    if (roots.size() < 2)
        return;
    QString error;
    Uuid folder;
    if (!m_session->editActions()->groupIntoFolder(roots, &folder, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_entitySelection.clear();
    m_selected = entityIdForUuid(folder);
    emit entitySelected(m_selected);
    emit editCommitted(QStringLiteral("成组为文件夹"));
    refreshFromSession();
}

void TimelinePanel::deleteSelectedEntities()
{
    if (!m_session || !m_session->editActions())
        return;
    QVector<Uuid> uuids;
    for (EntityId id : m_entitySelection)
    {
        const Uuid uuid = uuidForEntityId(id);
        if (!uuid.isEmpty() && !uuids.contains(uuid))
            uuids.append(uuid);
    }
    if (uuids.isEmpty() && m_selected != kInvalidEntity)
    {
        const Uuid uuid = uuidForEntityId(m_selected);
        if (!uuid.isEmpty())
            uuids.append(uuid);
    }
    if (uuids.isEmpty())
        return;
    QString error;
    if (!m_session->editActions()->deleteEntities(uuids, true, this, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(uuids.size() > 1
                           ? QStringLiteral("删除 %1 个片段").arg(uuids.size())
                           : QStringLiteral("删除片段"));
}

void TimelinePanel::finishSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    const DragMode mode = m_dragMode;
    const EntityId draggedId = m_dragEntity;
    const QVector<Uuid> dragRoots = m_dragRoots;
    const QPointF releasePos = m_lastDragPos;
    // Resolve the drop row before clearing the drag mode: the clamp uses the
    // rows captured when the gesture started.
    const int releaseRow = dropRowForY(int(std::lround(releasePos.y())));
    const QVector<Entity> baseEntities = m_sessionBaseSnapshot.entities;

    m_dragging = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    m_dragRoots.clear();
    m_dragKeyTrack.clear();

    if (!m_session || !m_session->editActions() || !m_adapter)
    {
        m_moved = false;
        refreshFromSession();
        return;
    }
    EditActions* actions = m_session->editActions();
    const Uuid uuid = uuidForEntityId(draggedId);
    if (uuid.isEmpty())
    {
        m_moved = false;
        refreshFromSession();
        return;
    }

    if (mode == DragMode::MoveClip)
    {
        int delta = frameFromX(int(std::lround(releasePos.x()))) - m_dragStartFrame;
        const QVector<Uuid> clampRoots =
            dragRoots.size() > 1 ? dragRoots : QVector<Uuid>{uuid};
        delta = std::max(delta, actions->minMoveDelta(clampRoots));
        const EntityId targetId = sessionBaseEntityAtRow(releaseRow);
        const Entity* target = findEntity(baseEntities, targetId);
        const Uuid targetUuid = uuidForEntityId(targetId);
        const bool targetHit =
            target && sessionBaseClipRect(*target).contains(releasePos.toPoint());
        const bool targetCanParent =
            target
            && (target->data.value(QStringLiteral("contentKind")).toString()
                    == QStringLiteral("empty")
                || !childIds(baseEntities, targetId).isEmpty());
        const bool multi = dragRoots.size() > 1;
        if (!multi && targetHit && targetCanParent
            && releaseRow != m_dragStartRow
            && isReparentCandidate(uuid, targetUuid))
        {
            m_moved = false;
            m_sessionPlanValid = false;
            const int choice = promptReparentMode(targetUuid);
            if (choice != 0)
            {
                QString error;
                if (!actions->reparent(uuid, targetUuid, choice == 1, &error))
                    emit editFailed(error);
                else
                    emit editCommitted(QStringLiteral("更改片段父级"));
            }
            refreshFromSession();
            update();
            syncPopups();
            return;
        }

        // Blank rows are first-class targets for a single clip.  A multi
        // selection moves as one time block and keeps every row.
        if (multi ? delta != 0
                  : (releaseRow != m_dragStartRow || delta != 0))
        {
            EditPlan plan;
            QString error;
            const bool planned =
                multi ? actions->planMove(dragRoots, delta, {}, &plan, &error)
                      : actions->planMoveToRow(uuid, delta, releaseRow, &plan,
                                               &error);
            if (planned)
            {
                m_sessionPlan = plan;
                m_sessionPlanValid = true;
                m_moved = true;
            }
            else if (!error.isEmpty())
            {
                emit editFailed(error);
            }
        }
    }

    if (m_sessionPlanValid && m_moved)
    {
        const int keyDelta = m_sessionPlan.frameDelta;
        QString error;
        if (!actions->apply(m_sessionPlan, &error))
        {
            emit editFailed(error);
        }
        else
        {
            if (mode == DragMode::MoveKeyframe && keyDelta != 0)
            {
                QVector<KeyframeSelection> moved;
                for (KeyframeSelection selection : m_selectedKeyframes)
                {
                    selection.frame += keyDelta;
                    if (!moved.contains(selection))
                        moved.append(selection);
                }
                m_selectedKeyframes = moved;
                emit keyframeSelectionChanged(m_selectedKeyframes);
            }
            else if (mode == DragMode::TrimStart
                     || mode == DragMode::TrimEnd)
            {
                // Trimming keeps the absolute frame of surviving keys but
                // drops the ones the cut removed; the selection must follow.
                QVector<KeyframeSelection> kept;
                for (const KeyframeSelection& selection : m_selectedKeyframes)
                {
                    const Uuid keyUuid = uuidForEntityId(selection.entity);
                    const auto it =
                        m_sessionPlan.project.entities.constFind(keyUuid);
                    if (it == m_sessionPlan.project.entities.constEnd())
                        continue;
                    if (selection.frame >= it->start
                        && selection.frame < it->end)
                        kept.append(selection);
                }
                if (kept.size() != m_selectedKeyframes.size())
                {
                    m_selectedKeyframes = kept;
                    emit keyframeSelectionChanged(m_selectedKeyframes);
                }
            }
            emit editCommitted(m_sessionPlan.message);
        }
    }

    m_sessionPlanValid = false;
    m_sessionPlan = EditPlan();
    m_moved = false;
    refreshFromSession();
    update();
    syncPopups();
}

void TimelinePanel::cancelSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    m_dragging = false;
    m_sessionPlanValid = false;
    m_sessionPlan = EditPlan();
    m_moved = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    m_dragKeyTrack.clear();
    m_dragMarkerId.clear();
    unsetCursor();
    refreshFromSession();
    update();
    syncPopups();
}

void TimelinePanel::updateDragPreview(const QPointF& pos)
{
    if (sessionMode())
        updateSessionDragPreview(pos);
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
