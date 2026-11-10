#include "EditActions.h"

#include "EditorSession.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"

#include <QMessageBox>
#include <QSet>

#include <algorithm>
#include <climits>

namespace pvf {

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString displayName(const model::Entity& entity)
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

void collectSubtree(const model::Project& project, const Uuid& uuid,
                    QVector<Uuid>* out, QSet<Uuid>* visited)
{
    if (visited->contains(uuid))
        return;
    visited->insert(uuid);
    if (!project.entities.contains(uuid))
        return;
    out->append(uuid);
    for (const Uuid& child : model::childrenOf(project, uuid))
        collectSubtree(project, child, out, visited);
}

QVector<Uuid> subtree(const model::Project& project, const Uuid& uuid)
{
    QVector<Uuid> result;
    QSet<Uuid> visited;
    collectSubtree(project, uuid, &result, &visited);
    return result;
}

bool hasAncestorIn(const model::Project& project, const Uuid& uuid,
                   const QVector<Uuid>& ancestors)
{
    Uuid parent = project.entities.value(uuid).parentUuid;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (ancestors.contains(parent))
            return true;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return false;
}

bool isDescendant(const model::Project& project, const Uuid& candidate,
                  const Uuid& ancestor)
{
    if (candidate == ancestor)
        return true;
    Uuid parent = project.entities.value(candidate).parentUuid;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (parent == ancestor)
            return true;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return false;
}

// Roots may contain descendants of another root; only the topmost one moves.
QVector<Uuid> uniqueRoots(const model::Project& project,
                          const QVector<Uuid>& roots, QString* error)
{
    QVector<Uuid> result;
    for (const Uuid& uuid : roots)
    {
        if (!project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return {};
        }
        if (result.contains(uuid) || hasAncestorIn(project, uuid, result))
            continue;
        result.append(uuid);
    }
    if (result.isEmpty() && !roots.isEmpty())
        setError(error, QStringLiteral("没有可编辑的实体"));
    return result;
}

QString lockedSubtreeError(const model::Project& project,
                           const QVector<Uuid>& nodes)
{
    for (const Uuid& uuid : nodes)
    {
        if (next::isEntityLocked(project, uuid))
        {
            return QStringLiteral("实体 %1 或其祖先已锁定，不能编辑")
                .arg(displayName(project.entities.value(uuid)));
        }
    }
    return QString();
}

void addUpsert(QVector<Uuid>* upserts, QSet<Uuid>* seen, const Uuid& uuid)
{
    if (seen->contains(uuid))
        return;
    seen->insert(uuid);
    upserts->append(uuid);
}

int parentStart(const model::Project& project, const model::Entity& entity)
{
    if (entity.parentUuid.isEmpty())
        return 0;
    const auto it = project.entities.constFind(entity.parentUuid);
    return it == project.entities.constEnd() ? 0 : it.value().start;
}

} // namespace

EditActions::EditActions(EditorSession* session)
    : m_session(session)
{
}

bool EditActions::isValid() const
{
    return m_session && m_session->document();
}

bool EditActions::planMove(const QVector<Uuid>& roots, int deltaFrames,
                           const QVector<Uuid>& siblingOrder, EditPlan* plan,
                           QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (roots.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的片段"));
        return false;
    }

    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return false;

    QVector<Uuid> allNodes;
    QSet<Uuid> nodeSet;
    for (const Uuid& root : selected)
    {
        for (const Uuid& uuid : subtree(project, root))
            addUpsert(&allNodes, &nodeSet, uuid);
    }
    const QString locked = lockedSubtreeError(project, allNodes);
    if (!locked.isEmpty())
    {
        setError(error, locked);
        return false;
    }

    for (const Uuid& root : selected)
    {
        const model::Entity& entity = project.entities[root];
        const int lower = parentStart(project, entity);
        if (entity.start + deltaFrames < lower)
        {
            const QString parent =
                entity.parentUuid.isEmpty()
                    ? QStringLiteral("时间轴起点")
                    : displayName(project.entities.value(entity.parentUuid));
            setError(error,
                     QStringLiteral("片段 %1 不能移动到 %2 之前")
                         .arg(displayName(entity), parent));
            return false;
        }
        if (entity.end + deltaFrames <= 0)
        {
            setError(error, QStringLiteral("片段 %1 的结束帧必须大于 0")
                                .arg(displayName(entity)));
            return false;
        }
    }

    model::Project working = project;
    for (const Uuid& uuid : allNodes)
    {
        model::Entity& entity = working.entities[uuid];
        entity.start += deltaFrames;
        entity.end += deltaFrames;
    }

    QVector<Uuid> upserts;
    QSet<Uuid> upsertSet;
    for (const Uuid& uuid : allNodes)
        addUpsert(&upserts, &upsertSet, uuid);

    if (!siblingOrder.isEmpty())
    {
        if (selected.size() != 1)
        {
            setError(error, QStringLiteral("一次只能调整一个片段的行序"));
            return false;
        }
        const Uuid root = selected.first();
        const model::Entity& entity = project.entities[root];
        const QVector<Uuid> siblings =
            model::childrenOf(project, entity.parentUuid);
        QSet<Uuid> expected(siblings.begin(), siblings.end());
        QSet<Uuid> actual(siblingOrder.begin(), siblingOrder.end());
        if (siblings.size() != siblingOrder.size() || expected != actual)
        {
            setError(error, QStringLiteral("行序列表与父级下的片段不一致"));
            return false;
        }
        for (int row = 0; row < siblingOrder.size(); ++row)
        {
            const Uuid uuid = siblingOrder.at(row);
            model::Entity& sibling = working.entities[uuid];
            if (sibling.row == row)
                continue;
            sibling.row = row;
            addUpsert(&upserts, &upsertSet, uuid);
        }
    }

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("移动后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = upserts;
    plan->deletes.clear();
    plan->message = siblingOrder.isEmpty() ? QStringLiteral("移动片段")
                                           : QStringLiteral("调整片段顺序");
    return true;
}

bool EditActions::planTrim(const Uuid& uuid, int newStart, int newEnd,
                           EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }

    model::Project working;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    if (!next::applyEntityRange(m_session->project(), m_session->assets(), uuid,
                                newStart, newEnd, &working, &upserts, &deletes,
                                error))
        return false;

    plan->project = std::move(working);
    plan->upserts = std::move(upserts);
    plan->deletes = std::move(deletes);
    plan->message = QStringLiteral("裁剪片段");
    return true;
}

bool EditActions::planReparent(const Uuid& uuid, const Uuid& newParent,
                               bool keepWorld, EditPlan* plan,
                               QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    const model::Entity& entity = it.value();
    if (!newParent.isEmpty() && !project.entities.contains(newParent))
    {
        setError(error, QStringLiteral("父级实体不存在：%1").arg(newParent));
        return false;
    }
    if (newParent == entity.parentUuid)
    {
        plan->project = project;
        plan->upserts.clear();
        plan->deletes.clear();
        plan->message = QStringLiteral("更改父级");
        return true;
    }
    if (!newParent.isEmpty() && isDescendant(project, newParent, uuid))
    {
        setError(error, QStringLiteral("不能把片段 %1 挂到它自己或它的子级下")
                            .arg(displayName(entity)));
        return false;
    }

    const QVector<Uuid> nodes = subtree(project, uuid);
    const QString locked = lockedSubtreeError(project, nodes);
    if (!locked.isEmpty())
    {
        setError(error, locked);
        return false;
    }
    if (!newParent.isEmpty() && next::isEntityLocked(project, newParent))
    {
        setError(error, QStringLiteral("新父级 %1 或其祖先已锁定")
                            .arg(displayName(project.entities.value(newParent))));
        return false;
    }

    const int oldParentStart = parentStart(project, entity);
    int newParentStart = 0;
    if (!newParent.isEmpty())
        newParentStart = project.entities.value(newParent).start;
    const int delta = keepWorld ? 0 : newParentStart - oldParentStart;
    const int newStart = entity.start + delta;
    const int newEnd = entity.end + delta;
    if (newStart < 0)
    {
        setError(error, QStringLiteral("片段 %1 的新开始帧为 %2")
                            .arg(displayName(entity))
                            .arg(newStart));
        return false;
    }
    if (!newParent.isEmpty())
    {
        const model::Entity& parent = project.entities.value(newParent);
        if (newStart < parent.start || newEnd > parent.end)
        {
            setError(error,
                     QStringLiteral("%1后，片段 %2 的范围 [%3, %4) 超出新父级 %5 的 [%6, %7)")
                         .arg(keepWorld ? QStringLiteral("保持世界位置")
                                        : QStringLiteral("保持局部坐标"))
                         .arg(displayName(entity))
                         .arg(newStart)
                         .arg(newEnd)
                         .arg(displayName(parent))
                         .arg(parent.start)
                         .arg(parent.end));
            return false;
        }
    }

    model::Project working = project;
    QVector<Uuid> upserts;
    QSet<Uuid> upsertSet;
    for (const Uuid& node : nodes)
    {
        model::Entity& child = working.entities[node];
        child.start += delta;
        child.end += delta;
        addUpsert(&upserts, &upsertSet, node);
    }

    model::Entity& moved = working.entities[uuid];
    moved.parentUuid = newParent;
    int maxRow = -1;
    for (const Uuid& sibling : model::childrenOf(project, newParent))
    {
        if (sibling == uuid)
            continue;
        maxRow = std::max(maxRow, project.entities.value(sibling).row);
    }
    moved.row = maxRow + 1;

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("更改父级后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = upserts;
    plan->deletes.clear();
    plan->message = QStringLiteral("更改父级");
    return true;
}

bool EditActions::planDelete(const QVector<Uuid>& roots, EditPlan* plan,
                             QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (roots.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的片段"));
        return false;
    }

    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return false;

    QVector<Uuid> deletes;
    QSet<Uuid> seen;
    for (const Uuid& root : selected)
    {
        for (const Uuid& uuid : subtree(project, root))
        {
            const QString locked = lockedSubtreeError(project, {uuid});
            if (!locked.isEmpty())
            {
                setError(error, locked);
                return false;
            }
            addUpsert(&deletes, &seen, uuid);
        }
    }

    model::Project working = project;
    for (const Uuid& uuid : deletes)
        working.entities.remove(uuid);

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("删除后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts.clear();
    plan->deletes = deletes;
    plan->message = QStringLiteral("删除片段");
    return true;
}

int EditActions::minMoveDelta(const QVector<Uuid>& roots) const
{
    if (!isValid())
        return 0;
    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, nullptr);
    int minimum = INT_MIN;
    for (const Uuid& root : selected)
    {
        const model::Entity& entity = project.entities[root];
        minimum = std::max(minimum, parentStart(project, entity) - entity.start);
    }
    return minimum == INT_MIN ? 0 : std::min(0, minimum);
}

int EditActions::minTrimStart(const Uuid& uuid) const
{
    if (!isValid())
        return 0;
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return 0;
    const model::Entity& entity = it.value();
    int minimum = parentStart(project, entity);
    if (entity.content.kind == ContentKind::Asset
        && (entity.content.asset.type == AssetType::Video
            || entity.content.asset.type == AssetType::Audio))
    {
        minimum = std::max(minimum,
                           entity.start - entity.content.asset.sourceIn);
    }
    return minimum;
}

int EditActions::maxTrimEnd(const Uuid& uuid) const
{
    if (!isValid())
        return std::numeric_limits<int>::max();
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return std::numeric_limits<int>::max();
    const model::Entity& entity = it.value();
    int maximum = std::numeric_limits<int>::max();
    if (!entity.parentUuid.isEmpty())
    {
        const auto parent = project.entities.constFind(entity.parentUuid);
        if (parent != project.entities.constEnd())
            maximum = std::min(maximum, parent.value().end);
    }
    const int frames = next::timedAssetDurationFrames(m_session->assets(), entity);
    if (frames > 0)
    {
        maximum = std::min(
            maximum, entity.start + std::max(0, frames - entity.content.asset.sourceIn));
    }
    return maximum;
}

bool EditActions::moveClips(const QVector<Uuid>& roots, int deltaFrames,
                            QString* error)
{
    return moveClips(roots, deltaFrames, {}, error);
}

bool EditActions::moveClips(const QVector<Uuid>& roots, int deltaFrames,
                            const QVector<Uuid>& siblingOrder, QString* error)
{
    EditPlan plan;
    if (!planMove(roots, deltaFrames, siblingOrder, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::trimClip(const Uuid& uuid, int newStart, int newEnd,
                           QString* error)
{
    EditPlan plan;
    if (!planTrim(uuid, newStart, newEnd, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::reparent(const Uuid& uuid, const Uuid& newParent,
                           bool keepWorld, QString* error)
{
    EditPlan plan;
    if (!planReparent(uuid, newParent, keepWorld, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::deleteEntities(const QVector<Uuid>& roots, bool confirm,
                                 QWidget* parent, QString* error)
{
    EditPlan plan;
    if (!planDelete(roots, &plan, error))
        return false;
    if (confirm)
    {
        const auto answer = QMessageBox::question(
            parent, QStringLiteral("删除片段"),
            QStringLiteral("删除 %1 个片段及其子片段？\n\n此操作可用撤销恢复。")
                .arg(plan.deletes.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            if (error)
                error->clear();
            return false;
        }
    }
    return apply(plan, error);
}

bool EditActions::apply(const EditPlan& plan, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (plan.isEmpty())
        return true;

    next::ProjectDocument* document = m_session->document();
    const model::Project before = document->project();
    for (const Uuid& uuid : plan.upserts)
    {
        if (!plan.project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("编辑结果缺少实体：%1").arg(uuid));
            return false;
        }
    }

    model::Project& live = document->mutableProject();
    live = before;
    for (const Uuid& uuid : plan.upserts)
        live.entities.insert(uuid, plan.project.entities.value(uuid));

    const bool ok =
        plan.upserts.isEmpty()
            ? document->deleteEntities(plan.deletes, plan.message, error)
            : document->commitChanges(plan.upserts, plan.deletes, plan.message,
                                      error);
    if (!ok)
    {
        live = before;
        return false;
    }
    return true;
}

} // namespace pvf
