#include "mcp2/EntityToolsInternal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace pvf::mcp2::detail::entity {

namespace {

struct DeleteStats
{
    QStringList deletedUuids;
    QStringList orphanedAssets;
    QJsonArray disabledSolvers;
};

void collectDisabledSolvers(Tx& tx, const QSet<Uuid>& removed, DeleteStats* stats)
{
    QVector<Uuid> remaining = tx.p().entities.keys();
    std::sort(remaining.begin(), remaining.end());
    for (const Uuid& uuid : std::as_const(remaining))
    {
        model::Entity* entity = findEntity(tx.p(), uuid);
        if (!entity)
            continue;
        QVector<model::SolverSegment> kept;
        for (const model::SolverSegment& segment : entity->solvers)
        {
            if (segment.effect == SolverEffect::Spin)
            {
                kept.append(segment);
                continue;
            }
            const QString target =
                segment.params.value(QStringLiteral("targetUuid")).toString();
            if (removed.contains(target))
            {
                QJsonObject item;
                item.insert(QStringLiteral("entityUuid"), uuid);
                item.insert(QStringLiteral("solverId"), segment.id);
                item.insert(QStringLiteral("effect"),
                            solverEffectName(segment.effect));
                stats->disabledSolvers.append(item);
                continue;
            }
            kept.append(segment);
        }
        if (kept.size() != entity->solvers.size())
        {
            entity->solvers = kept;
            tx.touch(uuid);
        }
    }
}

void collectOrphanedAssets(Tx& tx, const QSet<Uuid>& removed, DeleteStats* stats)
{
    Q_UNUSED(removed);
    QSet<AssetHash> before;
    for (auto it = tx.backup.entities.constBegin(); it != tx.backup.entities.constEnd();
         ++it)
    {
        if (it.value().content.kind == ContentKind::Asset)
            before.insert(it.value().content.asset.hash);
    }

    QSet<AssetHash> after;
    for (auto it = tx.p().entities.constBegin(); it != tx.p().entities.constEnd();
         ++it)
    {
        if (it.value().content.kind == ContentKind::Asset)
            after.insert(it.value().content.asset.hash);
    }

    QStringList orphans;
    for (const AssetHash& hash : std::as_const(before))
    {
        if (!after.contains(hash))
            orphans << hash;
    }
    std::sort(orphans.begin(), orphans.end());
    stats->orphanedAssets = orphans;
}

Result deleteCore(Tx& tx, const QVector<Uuid>& roots, DeleteStats* stats)
{
    QSet<Uuid> removed;
    for (const Uuid& root : roots)
    {
        if (removed.contains(root))
            continue;
        removed.insert(root);
        const QVector<Uuid> descendants = subtree(tx.p(), root);
        for (const Uuid& descendant : descendants)
            removed.insert(descendant);
    }

    QVector<Uuid> removedUuids(removed.begin(), removed.end());
    std::sort(removedUuids.begin(), removedUuids.end());

    for (const Uuid& uuid : std::as_const(removedUuids))
    {
        Result lockError;
        if (!ensureUnlocked(tx.p(), uuid, &lockError))
            return lockError;
    }

    // Removing a target makes dependent solver segments disappear; those
    // entities are written too, so a locked dependent blocks the delete.
    for (auto it = tx.p().entities.constBegin(); it != tx.p().entities.constEnd();
         ++it)
    {
        if (removed.contains(it.key()))
            continue;
        bool dependent = false;
        for (const model::SolverSegment& segment : it.value().solvers)
        {
            if (segment.effect == SolverEffect::Spin)
                continue;
            if (removed.contains(segment.params
                                     .value(QStringLiteral("targetUuid"))
                                     .toString()))
            {
                dependent = true;
                break;
            }
        }
        if (!dependent)
            continue;
        Result lockError;
        if (!ensureUnlocked(tx.p(), it.key(), &lockError))
            return lockError;
    }

    collectOrphanedAssets(tx, removed, stats);
    collectDisabledSolvers(tx, removed, stats);

    for (const Uuid& uuid : std::as_const(removedUuids))
    {
        if (stopRequested(tx))
            return stoppedByHuman();
        tx.p().entities.remove(uuid);
        tx.drop(uuid);
        stats->deletedUuids << uuid;
    }
    return success();
}

} // namespace

QJsonArray dependentSolvers(const model::Project& project,
                            const QSet<Uuid>& referenced)
{
    QJsonArray dependencies;
    QVector<Uuid> remaining = project.entities.keys();
    std::sort(remaining.begin(), remaining.end());
    for (const Uuid& uuid : std::as_const(remaining))
    {
        if (referenced.contains(uuid))
            continue;
        const model::Entity& entity = project.entities.value(uuid);
        for (const model::SolverSegment& segment : entity.solvers)
        {
            if (segment.effect == SolverEffect::Spin)
                continue;
            const QString target =
                segment.params.value(QStringLiteral("targetUuid")).toString();
            if (!referenced.contains(target))
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("entityUuid"), uuid);
            item.insert(QStringLiteral("solverId"), segment.id);
            item.insert(QStringLiteral("effect"),
                        solverEffectName(segment.effect));
            item.insert(QStringLiteral("targetUuid"), target);
            dependencies.append(item);
        }
    }
    return dependencies;
}

} // namespace pvf::mcp2::detail::entity

namespace pvf::mcp2::detail {

using namespace entity;

Result entityDelete(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    if (!findEntity(tx.p(), uuid))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));

    QSet<Uuid> affected{uuid};
    for (const Uuid& descendant : subtree(tx.p(), uuid))
        affected.insert(descendant);

    if (!confirmed(args))
    {
        QJsonObject affectedObject;
        QStringList ids(affected.begin(), affected.end());
        std::sort(ids.begin(), ids.end());
        affectedObject.insert(QStringLiteral("deletedUuids"),
                              QJsonArray::fromStringList(ids));
        const QJsonArray dependencies = dependentSolvers(tx.p(), affected);
        affectedObject.insert(QStringLiteral("solverDependencies"), dependencies);
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affectedObject);
        return failure(QStringLiteral("confirm_required"),
                       dependencies.isEmpty()
                           ? QStringLiteral("删除实体需要 confirm")
                           : QStringLiteral("删除实体会移除依赖求解段，需要 confirm"),
                       details);
    }

    DeleteStats stats;
    Result deleted = deleteCore(tx, {uuid}, &stats);
    if (!deleted.ok)
        return deleted;

    QJsonObject data;
    data.insert(QStringLiteral("deletedUuids"),
                QJsonArray::fromStringList(stats.deletedUuids));
    data.insert(QStringLiteral("orphanedAssets"),
                QJsonArray::fromStringList(stats.orphanedAssets));
    data.insert(QStringLiteral("disabledSolvers"), stats.disabledSolvers);
    return tx.finish(data, QStringLiteral("pvf: delete %1 entities")
                               .arg(stats.deletedUuids.size()));
}

Result entityDeleteMany(Tx& tx, const QJsonObject& args)
{
    Result error;
    const QJsonValue uuidsValue = args.value(QStringLiteral("uuids"));
    if (!uuidsValue.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("uuids 必须是数组"));
    const QJsonArray array = uuidsValue.toArray();
    if (array.isEmpty() || array.size() > kMaxBatch)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("uuids 数量必须在 1..%1").arg(kMaxBatch));

    QStringList missing;
    QVector<Uuid> roots;
    for (const QJsonValue& value : array)
    {
        if (!value.isString() || !model::isValidUuid(value.toString()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("uuids 里存在非法 UUID"));
        const QString uuid = value.toString();
        if (!findEntity(tx.p(), uuid))
        {
            missing << uuid;
            continue;
        }
        if (!roots.contains(uuid))
            roots.append(uuid);
    }
    if (!missing.isEmpty())
    {
        QJsonObject details;
        details.insert(QStringLiteral("missing"), QJsonArray::fromStringList(missing));
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("部分实体不存在"), details);
    }

    QSet<Uuid> affected;
    for (const Uuid& root : std::as_const(roots))
    {
        affected.insert(root);
        for (const Uuid& descendant : subtree(tx.p(), root))
            affected.insert(descendant);
    }

    if (!confirmed(args))
    {
        QStringList ids(affected.begin(), affected.end());
        std::sort(ids.begin(), ids.end());
        QJsonObject affectedObject;
        affectedObject.insert(QStringLiteral("deletedUuids"),
                              QJsonArray::fromStringList(ids));
        const QJsonArray dependencies = dependentSolvers(tx.p(), affected);
        affectedObject.insert(QStringLiteral("solverDependencies"), dependencies);
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affectedObject);
        return failure(QStringLiteral("confirm_required"),
                       dependencies.isEmpty()
                           ? QStringLiteral("批量删除需要 confirm")
                           : QStringLiteral("批量删除会移除依赖求解段，需要 confirm"),
                       details);
    }

    DeleteStats stats;
    Result deleted = deleteCore(tx, roots, &stats);
    if (!deleted.ok)
        return deleted;

    QJsonObject data;
    data.insert(QStringLiteral("deletedUuids"),
                QJsonArray::fromStringList(stats.deletedUuids));
    data.insert(QStringLiteral("orphanedAssets"),
                QJsonArray::fromStringList(stats.orphanedAssets));
    data.insert(QStringLiteral("disabledSolvers"), stats.disabledSolvers);
    return tx.finish(data, QStringLiteral("pvf: delete %1 entities")
                               .arg(stats.deletedUuids.size()));
}

} // namespace pvf::mcp2::detail
