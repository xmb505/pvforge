#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"
#include "app/TimelineAdapter.h"
#include "model/Project.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"

#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
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
        const int lower = actions->minMoveDelta({uuid});
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
        if (!actions->planMoveToRow(uuid, delta, targetRow, &plan, &error))
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
    if (row < 0)
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
    if (row < 0)
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

void TimelinePanel::deleteSelectedEntities()
{
    if (!m_session || !m_session->editActions() || m_selected == kInvalidEntity)
        return;
    const Uuid uuid = uuidForEntityId(m_selected);
    if (uuid.isEmpty())
        return;
    QString error;
    if (!m_session->editActions()->deleteEntities({uuid}, true, this, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("删除片段"));
}

void TimelinePanel::finishSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    const DragMode mode = m_dragMode;
    const EntityId draggedId = m_dragEntity;
    const QPointF releasePos = m_lastDragPos;
    // Resolve the drop row before clearing the drag mode: the clamp uses the
    // rows captured when the gesture started.
    const int releaseRow = dropRowForY(int(std::lround(releasePos.y())));
    const QVector<Entity> baseEntities = m_sessionBaseSnapshot.entities;

    m_dragging = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
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
        delta = std::max(delta, actions->minMoveDelta({uuid}));
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
        if (targetHit && targetCanParent && releaseRow != m_dragStartRow
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

        // Blank rows are first-class targets: the model row is written even
        // when no clip sits there.  Dropping on a clip swaps the two slots.
        if (releaseRow != m_dragStartRow || delta != 0)
        {
            EditPlan plan;
            QString error;
            if (actions->planMoveToRow(uuid, delta, releaseRow, &plan, &error))
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
        QString error;
        if (!actions->apply(m_sessionPlan, &error))
            emit editFailed(error);
        else
            emit editCommitted(m_sessionPlan.message);
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

} // namespace pvf
