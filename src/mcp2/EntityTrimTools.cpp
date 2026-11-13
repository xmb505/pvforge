#include "mcp2/EntityToolsInternal.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QJsonArray>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <optional>

namespace pvf::mcp2::detail::entity {

namespace {

// ---- trim helpers -------------------------------------------------------

int trimTrack(model::Track& track, int deltaStart, int newDuration)
{
    QVector<model::Keyframe> kept;
    int removed = 0;
    for (const model::Keyframe& key : track.keys)
    {
        model::Keyframe shifted = key;
        shifted.frame -= deltaStart;
        if (shifted.frame < 0 || shifted.frame >= newDuration)
        {
            ++removed;
            continue;
        }
        kept.append(shifted);
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const model::Keyframe& a, const model::Keyframe& b) {
                         return a.frame < b.frame;
                     });
    track.keys = kept;
    return removed;
}

// Solver samples drive local x/y (parent space) or rotation.  Evaluation adds
// pivot/scale and every parent transform, so probe with a neutral scale/pivot
// to recover exactly the values the solver wrote into the driven tracks.
struct SolvedPose
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
};

std::optional<SolvedPose> solvedPoseAt(const model::Project& project,
                                       const assets::AssetStore& assets,
                                       const Uuid& uuid, int projectFrame)
{
    model::Project probe = project;
    const auto it = probe.entities.find(uuid);
    if (it == probe.entities.end())
        return std::nullopt;

    model::Entity& entity = it.value();
    entity.transform.scaleX = 1.0;
    entity.transform.scaleY = 1.0;
    entity.transform.pivotX = 0.0;
    entity.transform.pivotY = 0.0;
    entity.tracks.remove(QStringLiteral("scaleX"));
    entity.tracks.remove(QStringLiteral("scaleY"));
    entity.tracks.remove(QStringLiteral("pivotX"));
    entity.tracks.remove(QStringLiteral("pivotY"));

    render2::SceneWorld world(probe, assets);
    QTransform local =
        anim::evaluateEntity(probe, uuid, projectFrame, world).world;
    if (!entity.parentUuid.isEmpty()
        && probe.entities.contains(entity.parentUuid))
    {
        const QTransform parent =
            anim::evaluateEntity(probe, entity.parentUuid, projectFrame, world)
                .world;
        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (invertible)
            local = local * inverse;
    }

    SolvedPose pose;
    pose.x = local.dx();
    pose.y = local.dy();
    pose.rotation = qRadiansToDegrees(std::atan2(local.m12(), local.m11()));
    return pose;
}

QStringList trimSolvers(model::Entity& entity, const model::Entity& old,
                        int deltaStart, int newDuration,
                        const std::optional<SolvedPose>& cutPose)
{
    QStringList removedIds;
    QVector<model::SolverSegment> kept;

    for (const model::SolverSegment& segment : old.solvers)
    {
        const int oldEnd = solverEffectiveEnd(old, segment);
        const int startRel = segment.startFrame - deltaStart;
        const int endRel = oldEnd - deltaStart;

        if (startRel < 0)
        {
            removedIds << segment.id;
            // The new clip start lands inside this segment.  Freeze the pose
            // the solver produced at the cut frame as an ordinary key so the
            // trimmed clip starts exactly where the segment used to be.
            if (endRel >= 0 && cutPose)
            {
                for (const QString& track : drivenTracks(segment.effect))
                {
                    model::Keyframe key;
                    key.frame = 0;
                    if (track == QLatin1String("x"))
                        key.value = cutPose->x;
                    else if (track == QLatin1String("y"))
                        key.value = cutPose->y;
                    else
                        key.value = cutPose->rotation;
                    key.easing.type = QStringLiteral("linear");
                    model::Track target = entity.tracks.value(track);
                    target.keys.erase(
                        std::remove_if(target.keys.begin(), target.keys.end(),
                                       [](const model::Keyframe& existing) {
                                           return existing.frame == 0;
                                       }),
                        target.keys.end());
                    target.keys.prepend(key);
                    entity.tracks.insert(track, target);
                }
            }
            continue;
        }

        model::SolverSegment adjusted = segment;
        adjusted.startFrame = startRel;
        if (segment.endFrame != -1)
            adjusted.endFrame = endRel;
        if (endRel >= newDuration)
        {
            adjusted.endFrame = newDuration - 1;
            if (adjusted.endFrame < adjusted.startFrame)
            {
                removedIds << segment.id;
                continue;
            }
        }
        kept.append(adjusted);
    }

    entity.solvers = kept;
    return removedIds;
}

Result trimRecursive(Tx& tx, const Uuid& uuid, int newStart, int newEnd,
                     TrimSummary* stats, bool isRoot);

Result trimChildren(Tx& tx, const Uuid& parentUuid, int newStart, int newEnd,
                    TrimSummary* stats)
{
    QVector<Uuid> childIds;
    for (auto it = tx.p().entities.constBegin(); it != tx.p().entities.constEnd();
         ++it)
    {
        if (it.value().parentUuid == parentUuid)
            childIds.append(it.key());
    }
    std::sort(childIds.begin(), childIds.end());

    for (const Uuid& childId : std::as_const(childIds))
    {
        if (stopRequested(tx))
            return stoppedByHuman();
        const model::Entity* child = findEntity(tx.p(), childId);
        if (!child)
            continue;
        const bool childFollows = child->followParentRange;
        // Following children take the parent's full range, never just the
        // intersection.  Forced recursion keeps them even when the parent
        // jumped to a disjoint range.
        const int start = childFollows ? newStart : std::max(child->start, newStart);
        const int end = childFollows ? newEnd : std::min(child->end, newEnd);
        if (start >= end)
        {
            if (childFollows)
                continue; // a parent range is always valid; defensive only
            const QVector<Uuid> descendants = subtree(tx.p(), childId);
            stats->deletedUuids << childId;
            stats->deletedUuids += descendants;
            tx.p().entities.remove(childId);
            for (const Uuid& descendant : descendants)
                tx.p().entities.remove(descendant);
            tx.drop(childId);
            for (const Uuid& descendant : descendants)
                tx.drop(descendant);
            continue;
        }
        if (start != child->start || end != child->end)
        {
            Result result = trimRecursive(tx, childId, start, end, stats, false);
            if (!result.ok)
                return result;
        }
    }
    return success();
}

Result trimRecursive(Tx& tx, const Uuid& uuid, int newStart, int newEnd,
                     TrimSummary* stats, bool isRoot)
{
    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));

    const model::Entity old = *entity;
    const int deltaStart = newStart - old.start;
    if (newEnd - newStart <= 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("裁剪后时长必须大于 0"));

    int adjustedEnd = newEnd;
    int newSourceIn = old.content.asset.sourceIn;
    if (old.content.kind == ContentKind::Asset
        && (old.content.asset.type == AssetType::Video
            || old.content.asset.type == AssetType::Audio))
    {
        newSourceIn += deltaStart;
        if (newSourceIn < 0)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("裁剪后 sourceIn 小于 0"));
        if (const auto asset = tx.store->assets().get(old.content.asset.hash))
        {
            const int frames = assetDurationFrames(*asset);
            if (frames > 0 && newSourceIn >= frames)
                return failure(QStringLiteral("conflict"),
                               QStringLiteral("sourceIn 超出素材长度 %1").arg(frames));
            if (frames > 0 && newSourceIn + (newEnd - newStart) > frames)
            {
                if (isRoot)
                    return failure(
                        QStringLiteral("invalid_argument"),
                        QStringLiteral("裁剪范围超出素材剩余长度 %1")
                            .arg(frames - newSourceIn));
                adjustedEnd = newStart + (frames - newSourceIn);
                if (adjustedEnd <= newStart)
                {
                    const QVector<Uuid> descendants = subtree(tx.p(), uuid);
                    stats->deletedUuids << uuid;
                    stats->deletedUuids += descendants;
                    tx.p().entities.remove(uuid);
                    for (const Uuid& descendant : descendants)
                        tx.p().entities.remove(descendant);
                    tx.drop(uuid);
                    for (const Uuid& descendant : descendants)
                        tx.drop(descendant);
                    return success();
                }
            }
        }
    }

    const int newDuration = adjustedEnd - newStart;
    if (newDuration <= 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("裁剪后时长必须大于 0"));

    // A start trim that lands inside a solver segment freezes the pose the
    // solver produced at the cut frame.  Probe the untouched project first:
    // once the tracks shift, that pose is no longer recoverable.
    std::optional<SolvedPose> cutPose;
    if (deltaStart > 0)
    {
        bool cutsSegment = false;
        for (const model::SolverSegment& segment : old.solvers)
        {
            if (segment.startFrame < deltaStart
                && deltaStart <= solverEffectiveEnd(old, segment))
            {
                cutsSegment = true;
                break;
            }
        }
        if (cutsSegment)
        {
            cutPose = solvedPoseAt(tx.p(), tx.store->assets(), uuid, newStart);
            if (!cutPose)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("实体不存在：%1").arg(uuid));
        }
    }

    if (old.content.kind == ContentKind::Asset)
        entity->content.asset.sourceIn = newSourceIn;

    for (auto it = entity->tracks.begin(); it != entity->tracks.end(); ++it)
        stats->removedKeyframes += trimTrack(it.value(), deltaStart, newDuration);

    stats->removedSolvers +=
        trimSolvers(*entity, old, deltaStart, newDuration, cutPose);

    entity->start = newStart;
    entity->end = adjustedEnd;
    tx.touch(uuid);

    return trimChildren(tx, uuid, newStart, adjustedEnd, stats);
}

} // namespace

} // namespace pvf::mcp2::detail::entity

namespace pvf::mcp2::detail {

using namespace entity;

Result trimEntityRange(Tx& tx, const Uuid& uuid, int newStart, int newEnd,
                       TrimSummary* summary)
{
    return trimRecursive(tx, uuid, newStart, newEnd, summary, true);
}

Result entityTrim(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    if (!ensureUnlocked(tx.p(), uuid, &error))
        return error;

    const bool hasStart = args.contains(QStringLiteral("start"));
    const bool hasEnd = args.contains(QStringLiteral("end"));
    if (!hasStart && !hasEnd)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("start 和 end 至少给一个"));

    int newStart = entity->start;
    int newEnd = entity->end;
    if (hasStart && !readInt(args, "start", &newStart, &error, true))
        return error;
    if (hasEnd && !readInt(args, "end", &newEnd, &error, true))
        return error;
    if (newStart < 0 || newEnd > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("裁剪范围必须在 0..%1").arg(kMaxFrame));
    if (newEnd <= newStart)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("裁剪后 end 必须大于 start"));

    if (entity->followParentRange)
    {
        const model::Entity* followParent = findEntity(tx.p(), entity->parentUuid);
        if (!followParent || newStart != followParent->start
            || newEnd != followParent->end)
            return failure(
                QStringLiteral("conflict"),
                QStringLiteral("实体跟随父区间，不能单独裁剪；请调整父实体"));
    }

    if (!entity->parentUuid.isEmpty())
    {
        const model::Entity* parent = findEntity(tx.p(), entity->parentUuid);
        if (parent && (newStart < parent->start || newEnd > parent->end))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("裁剪范围超出父实体范围"));
    }

    const QVector<Uuid> subtreeIds = subtree(tx.p(), uuid);
    for (const Uuid& childId : std::as_const(subtreeIds))
    {
        if (!ensureUnlocked(tx.p(), childId, &error))
            return error;
    }

    TrimSummary stats;
    Result trimmed = trimRecursive(tx, uuid, newStart, newEnd, &stats, true);
    if (!trimmed.ok)
        return trimmed;

    if (!stats.deletedUuids.isEmpty() && !confirmed(args))
    {
        QJsonObject affected;
        affected.insert(QStringLiteral("deletedUuids"),
                        QJsonArray::fromStringList(stats.deletedUuids));
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affected);
        return failure(QStringLiteral("confirm_required"),
                       QStringLiteral("裁剪会删除子实体，需要 confirm"), details);
    }

    const model::Entity* trimmedEntity = findEntity(tx.p(), uuid);
    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("start"), trimmedEntity->start);
    data.insert(QStringLiteral("end"), trimmedEntity->end);
    data.insert(QStringLiteral("duration"), trimmedEntity->duration());
    data.insert(QStringLiteral("sourceIn"),
                trimmedEntity->content.kind == ContentKind::Asset
                    ? trimmedEntity->content.asset.sourceIn
                    : 0);
    data.insert(QStringLiteral("removedKeyframes"), stats.removedKeyframes);
    data.insert(QStringLiteral("removedSolvers"),
                QJsonArray::fromStringList(stats.removedSolvers));
    data.insert(QStringLiteral("deletedUuids"),
                QJsonArray::fromStringList(stats.deletedUuids));
    return tx.finish(data,
                     QStringLiteral("pvf: trim entity \"%1\" [%2,%3)")
                         .arg(trimmedEntity->name)
                         .arg(trimmedEntity->start)
                         .arg(trimmedEntity->end));
}

} // namespace pvf::mcp2::detail
