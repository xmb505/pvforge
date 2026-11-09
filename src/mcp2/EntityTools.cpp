#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace pvf::mcp2::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxEntities = 5000;
constexpr int kMaxBatch = 200;
constexpr int kMaxTreeNodes = 200;
constexpr int kMaxDepth = 64;
constexpr int kMaxFrame = 1000000;

const QStringList kCreateKeys = {
    QStringLiteral("name"),      QStringLiteral("parentUuid"),
    QStringLiteral("start"),     QStringLiteral("end"),
    QStringLiteral("row"),       QStringLiteral("content"),
    QStringLiteral("transform"), QStringLiteral("hidden"),
    QStringLiteral("locked"),    QStringLiteral("inheritVisibility"),
    QStringLiteral("visible"),   QStringLiteral("physicsInteract"),
    QStringLiteral("keyframes"), QStringLiteral("solvers"),
};

int assetDurationFrames(const assets::StoredAsset& asset)
{
    return asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
}

Result checkAssetDuration(const assets::AssetStore& assetStore,
                          const model::Content& content, int duration)
{
    if (content.kind != ContentKind::Asset)
        return success();
    if (content.asset.type != AssetType::Video
        && content.asset.type != AssetType::Audio)
        return success();

    const auto asset = assetStore.get(content.asset.hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(content.asset.hash));
    const int frames = assetDurationFrames(*asset);
    if (frames <= 0)
        return success();
    if (content.asset.sourceIn >= frames)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("sourceIn 超出素材长度（%1 帧）").arg(frames));
    if (content.asset.sourceIn + duration > frames)
        return failure(
            QStringLiteral("invalid_argument"),
            QStringLiteral("实体时长 %1 超出素材剩余长度 %2")
                .arg(duration)
                .arg(frames - content.asset.sourceIn));
    return success();
}

Result parseContentForCreate(const assets::AssetStore& assetStore,
                             const QJsonValue& value, model::Content* out)
{
    if (value.isUndefined() || value.isNull())
    {
        *out = model::Content();
        return success();
    }
    if (!value.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("content 必须是对象"));

    QString error;
    const auto parsed = model::contentFromJson(value.toObject(), &error);
    if (!parsed)
        return failure(QStringLiteral("invalid_argument"), error);

    *out = *parsed;
    if (out->kind != ContentKind::Asset)
        return success();

    const auto asset = assetStore.get(out->asset.hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(out->asset.hash));
    out->asset.type = asset->type;
    if (out->asset.sourceIn < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("sourceIn 不能小于 0"));
    if (!isFiniteNumber(out->asset.volume) || out->asset.volume < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("volume 必须是非负有限数"));
    return success();
}

QString defaultNameForContent(const assets::AssetStore& assetStore,
                              const model::Content& content)
{
    switch (content.kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("实体");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("形状");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        if (const auto asset = assetStore.get(content.asset.hash))
            return asset->name;
        return QStringLiteral("素材");
    }
    return QStringLiteral("实体");
}

Result parseKeyframesForCreate(const QJsonValue& value, int duration,
                               model::Entity& entity,
                               const QStringList& allowedTracks)
{
    if (value.isUndefined() || value.isNull())
        return success();
    if (!value.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("keyframes 必须是对象"));

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!allowedTracks.contains(it.key()))
        {
            QJsonObject details;
            details.insert(QStringLiteral("allowedTracks"),
                           QJsonArray::fromStringList(allowedTracks));
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知轨道：%1").arg(it.key()), details);
        }

        if (!it.value().isArray())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("轨道 %1 必须是关键帧数组").arg(it.key()));

        model::Track parsedTrack;
        QSet<int> frames;
        const QJsonArray keyArray = it.value().toArray();
        for (int i = 0; i < keyArray.size(); ++i)
        {
            if (!keyArray.at(i).isObject())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("轨道 %1 的第 %2 个关键帧不是对象")
                                   .arg(it.key())
                                   .arg(i));
            const QJsonObject keyObject = keyArray.at(i).toObject();
            const QJsonValue frameValue = keyObject.value(QStringLiteral("frame"));
            if (!frameValue.isDouble()
                || std::floor(frameValue.toDouble()) != frameValue.toDouble())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("轨道 %1 的关键帧 frame 必须是整数")
                                   .arg(it.key()));
            if (!keyObject.value(QStringLiteral("value")).isDouble())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("轨道 %1 的关键帧 value 必须是数字")
                                   .arg(it.key()));

            QString keyError;
            const auto key = model::keyframeFromJson(keyObject, &keyError);
            if (!key)
                return failure(QStringLiteral("invalid_argument"), keyError);

            model::EasingSpec easing;
            QJsonValue normalizedEasing;
            Result easingError;
            if (!parseEasing(keyObject.value(QStringLiteral("easing")), &easing,
                             &normalizedEasing, &easingError))
                return easingError;
            Q_UNUSED(normalizedEasing);

            model::Keyframe normalized = *key;
            normalized.easing = easing;

            if (normalized.frame < 0 || normalized.frame >= duration)
            {
                QJsonObject range;
                range.insert(QStringLiteral("min"), 0);
                range.insert(QStringLiteral("max"), duration - 1);
                QJsonObject details;
                details.insert(QStringLiteral("track"), it.key());
                details.insert(QStringLiteral("allowedRange"), range);
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("关键帧帧号 %1 超出轨道 %2 的范围 0..%3")
                                   .arg(normalized.frame)
                                   .arg(it.key())
                                   .arg(duration - 1),
                               details);
            }
            if (frames.contains(normalized.frame))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("轨道 %1 的帧 %2 重复")
                                   .arg(it.key())
                                   .arg(normalized.frame));
            frames.insert(normalized.frame);
            if (!isFiniteNumber(normalized.value))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("轨道 %1 的关键帧值非法").arg(it.key()));
            if (normalized.note.size() > 4096)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("关键帧 note 最长 4096 字符"));
            parsedTrack.keys.append(normalized);
        }

        std::stable_sort(parsedTrack.keys.begin(), parsedTrack.keys.end(),
                         [](const model::Keyframe& a, const model::Keyframe& b) {
                             return a.frame < b.frame;
                         });
        entity.tracks.insert(it.key(), parsedTrack);
    }
    return success();
}

Result createEntityInternal(Tx& tx, const QJsonObject& args,
                            const QSet<Uuid>* parentWhitelist, Uuid* createdUuid)
{
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (!kCreateKeys.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    model::Project& project = tx.p();
    if (project.entities.size() >= kMaxEntities)
        return failure(QStringLiteral("limit_exceeded"),
                       QStringLiteral("工程实体数超过 %1").arg(kMaxEntities));

    Result error;
    int start = 0;
    if (!args.contains(QStringLiteral("start")))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("entity.create 必须显式给 start"));
    if (!readInt(args, "start", &start, &error, true))
        return error;
    if (start < 0 || start > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("start 必须在 0..%1").arg(kMaxFrame));

    Uuid parentUuid;
    if (!readUuid(args, "parentUuid", &parentUuid, &error, false, true))
        return error;
    if (parentWhitelist && !parentUuid.isEmpty()
        && !parentWhitelist->contains(parentUuid))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("createMany 的 parentUuid 必须指向调用前已存在的实体"));

    const model::Entity* parent = nullptr;
    if (!parentUuid.isEmpty())
    {
        parent = findEntity(project, parentUuid);
        if (!parent)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("父实体不存在：%1").arg(parentUuid));
        if (!ensureUnlocked(project, parentUuid, &error))
            return error;
        if (depthOf(project, parentUuid) + 1 > kMaxDepth)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("父子层级超过 %1 层").arg(kMaxDepth));
    }

    model::Content content;
    Result contentResult =
        parseContentForCreate(tx.store->assets(), args.value(QStringLiteral("content")),
                              &content);
    if (!contentResult.ok)
        return contentResult;

    int end = 0;
    if (args.contains(QStringLiteral("end")))
    {
        if (!readInt(args, "end", &end, &error, true))
            return error;
    }
    else
    {
        if (content.kind == ContentKind::Asset
            && (content.asset.type == AssetType::Video
                || content.asset.type == AssetType::Audio))
        {
            const auto asset = tx.store->assets().get(content.asset.hash);
            const int frames = asset ? assetDurationFrames(*asset) : 0;
            if (frames <= content.asset.sourceIn)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("sourceIn 超出素材长度"));
            end = start + (frames - content.asset.sourceIn);
        }
        else
        {
            end = start + std::max(1, project.settings.fps);
        }
    }
    if (end <= start || end > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("end 必须大于 start 且不超过 %1").arg(kMaxFrame));

    if (parent && (start < parent->start || end > parent->end))
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("实体范围 [%1,%2) 超出父实体范围 [%3,%4)")
                           .arg(start)
                           .arg(end)
                           .arg(parent->start)
                           .arg(parent->end));

    contentResult = checkAssetDuration(tx.store->assets(), content, end - start);
    if (!contentResult.ok)
        return contentResult;

    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.parentUuid = parentUuid;
    entity.start = start;
    entity.end = end;
    entity.content = content;

    int row = 0;
    if (args.contains(QStringLiteral("row")))
    {
        if (!readInt(args, "row", &row, &error, true))
            return error;
        if (row < 0 || row > kMaxFrame)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("row 必须 >= 0"));
    }
    else
    {
        row = defaultRow(project, parentUuid, start, end);
    }
    entity.row = row;

    QString name;
    if (!readString(args, "name", &name, &error))
        return error;
    if (name.isEmpty())
        name = defaultNameForContent(tx.store->assets(), content);
    if (name.size() > 256)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("name 最长 256 字符"));
    entity.name = name;

    if (!readBool(args, "hidden", &entity.hidden, &error)
        || !readBool(args, "locked", &entity.locked, &error)
        || !readBool(args, "inheritVisibility", &entity.inheritVisibility, &error)
        || !readBool(args, "visible", &entity.visible, &error)
        || !readBool(args, "physicsInteract", &entity.physicsInteract, &error))
        return error;

    const QJsonValue transformValue = args.value(QStringLiteral("transform"));
    if (!transformValue.isUndefined() && !transformValue.isNull())
    {
        if (!transformValue.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("transform 必须是对象"));
        QString transformError;
        const auto transform =
            model::transformFromJson(transformValue.toObject(), &transformError);
        if (!transform)
            return failure(QStringLiteral("invalid_argument"), transformError);
        entity.transform = *transform;
    }

    entity.tracks.clear();
    Result keyframeResult = parseKeyframesForCreate(
        args.value(QStringLiteral("keyframes")), entity.duration(), entity,
        allowedTracks(project, tx.store->assets(), entity));
    if (!keyframeResult.ok)
        return keyframeResult;

    const QJsonValue solversValue = args.value(QStringLiteral("solvers"));
    if (!solversValue.isUndefined() && !solversValue.isNull())
    {
        if (!solversValue.isArray())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("solvers 必须是数组"));
        const QJsonArray solvers = solversValue.toArray();
        for (int i = 0; i < solvers.size(); ++i)
        {
            if (!solvers.at(i).isObject())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("solvers[%1] 必须是对象").arg(i));
            model::SolverSegment segment;
            Result parsed = parseSolverCreate(project, entity,
                                              solvers.at(i).toObject(), &segment);
            if (!parsed.ok)
            {
                parsed.details.insert(QStringLiteral("index"), i);
                return parsed;
            }
            entity.solvers.append(segment);
        }
    }

    project.entities.insert(entity.uuid, entity);
    tx.touch(entity.uuid);
    *createdUuid = entity.uuid;
    return success();
}

QJsonObject createdEntityData(const Tx& tx, const Uuid& uuid)
{
    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    if (const model::Entity* entity = findEntity(tx.p(), uuid))
        data.insert(QStringLiteral("entity"), entityBrief(*entity));
    return data;
}

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

double trackValueAt(const model::Entity& entity, const QString& track, int frame,
                    double fallback)
{
    const auto it = entity.tracks.constFind(track);
    if (it == entity.tracks.constEnd())
        return fallback;
    const auto value = model::evaluateTrack(it.value(), frame);
    return value ? *value : fallback;
}

QStringList trimSolvers(model::Entity& entity, const model::Entity& old,
                        int deltaStart, int newDuration, bool addFallback)
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
            // The new clip start lands inside this segment (or after it). The
            // solver module is not linked in Phase A, so freeze the segment's
            // initial pose at frame 0 as the normal keyframe value.
            const bool inside = endRel >= 0;
            if (inside && addFallback)
            {
                for (const QString& track : drivenTracks(segment.effect))
                {
                    model::Keyframe key;
                    key.frame = 0;
                    key.value = trackValueAt(old, track, segment.startFrame, 0.0);
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
        const model::Entity* child = findEntity(tx.p(), childId);
        if (!child)
            continue;
        const int start = std::max(child->start, newStart);
        const int end = std::min(child->end, newEnd);
        if (start >= end)
        {
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

    if (old.content.kind == ContentKind::Asset)
        entity->content.asset.sourceIn = newSourceIn;

    for (auto it = entity->tracks.begin(); it != entity->tracks.end(); ++it)
        stats->removedKeyframes += trimTrack(it.value(), deltaStart, newDuration);

    stats->removedSolvers += trimSolvers(*entity, old, deltaStart, newDuration, true);

    entity->start = newStart;
    entity->end = adjustedEnd;
    tx.touch(uuid);

    return trimChildren(tx, uuid, newStart, adjustedEnd, stats);
}

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
        tx.p().entities.remove(uuid);
        tx.drop(uuid);
        stats->deletedUuids << uuid;
    }
    return success();
}

// ---- affine helpers -----------------------------------------------------

struct Affine
{
    double a00 = 1, a01 = 0, a10 = 0, a11 = 1;
    double tx = 0, ty = 0;

    QPointF apply(QPointF point) const
    {
        return QPointF(a00 * point.x() + a01 * point.y() + tx,
                       a10 * point.x() + a11 * point.y() + ty);
    }
};

Affine compose(const Affine& parent, const Affine& child)
{
    Affine result;
    result.a00 = parent.a00 * child.a00 + parent.a01 * child.a10;
    result.a01 = parent.a00 * child.a01 + parent.a01 * child.a11;
    result.a10 = parent.a10 * child.a00 + parent.a11 * child.a10;
    result.a11 = parent.a10 * child.a01 + parent.a11 * child.a11;
    result.tx = parent.a00 * child.tx + parent.a01 * child.ty + parent.tx;
    result.ty = parent.a10 * child.tx + parent.a11 * child.ty + parent.ty;
    return result;
}

Affine inverse(const Affine& affine)
{
    const double det = affine.a00 * affine.a11 - affine.a01 * affine.a10;
    if (std::abs(det) < 1e-12)
        return Affine();
    Affine result;
    result.a00 = affine.a11 / det;
    result.a01 = -affine.a01 / det;
    result.a10 = -affine.a10 / det;
    result.a11 = affine.a00 / det;
    result.tx = -(result.a00 * affine.tx + result.a01 * affine.ty);
    result.ty = -(result.a10 * affine.tx + result.a11 * affine.ty);
    return result;
}

double evaluated(const model::Entity& entity, const QString& track, int frame,
                 double fallback)
{
    return trackValueAt(entity, track, frame - entity.start, fallback);
}

Affine localAffine(const model::Entity& entity, int frame)
{
    const double x = evaluated(entity, QStringLiteral("x"), frame, entity.transform.x);
    const double y = evaluated(entity, QStringLiteral("y"), frame, entity.transform.y);
    const double rotation = evaluated(entity, QStringLiteral("rotation"), frame,
                                      entity.transform.rotation);
    const double scaleX = evaluated(entity, QStringLiteral("scaleX"), frame,
                                    entity.transform.scaleX);
    const double scaleY = evaluated(entity, QStringLiteral("scaleY"), frame,
                                    entity.transform.scaleY);
    const double pivotX = evaluated(entity, QStringLiteral("pivotX"), frame,
                                    entity.transform.pivotX);
    const double pivotY = evaluated(entity, QStringLiteral("pivotY"), frame,
                                    entity.transform.pivotY);

    const double radians = rotation * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    Affine affine;
    affine.a00 = cosine * scaleX;
    affine.a01 = -sine * scaleY;
    affine.a10 = sine * scaleX;
    affine.a11 = cosine * scaleY;
    affine.tx = x + pivotX - (affine.a00 * pivotX + affine.a01 * pivotY);
    affine.ty = y + pivotY - (affine.a10 * pivotX + affine.a11 * pivotY);
    return affine;
}

Affine worldAffine(const model::Project& project, const Uuid& uuid, int frame)
{
    QVector<const model::Entity*> chain;
    Uuid current = uuid;
    QSet<Uuid> visited;
    while (!current.isEmpty() && !visited.contains(current))
    {
        visited.insert(current);
        const model::Entity* entity = findEntity(project, current);
        if (!entity)
            break;
        chain.prepend(entity);
        current = entity->parentUuid;
    }

    Affine result;
    for (const model::Entity* entity : std::as_const(chain))
        result = compose(result, localAffine(*entity, frame));
    return result;
}

Result applyEntityPatch(Tx& tx, model::Entity& entity, const QJsonObject& patch,
                        QStringList* changed)
{
    static const QStringList allowed = {
        QStringLiteral("name"),        QStringLiteral("row"),
        QStringLiteral("hidden"),      QStringLiteral("locked"),
        QStringLiteral("inheritVisibility"), QStringLiteral("visible"),
        QStringLiteral("physicsInteract"),   QStringLiteral("transform"),
        QStringLiteral("content"),
    };
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("patch 不允许字段 %1").arg(it.key()));
    }

    Result error;
    if (patch.contains(QStringLiteral("name")))
    {
        QString name;
        if (!readString(patch, "name", &name, &error, true))
            return error;
        if (name.isEmpty() || name.size() > 256)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("name 必须是 1..256 字符"));
        if (name != entity.name)
        {
            entity.name = name;
            changed->append(QStringLiteral("name"));
        }
    }

    if (patch.contains(QStringLiteral("row")))
    {
        int row = entity.row;
        if (!readInt(patch, "row", &row, &error, true))
            return error;
        if (row < 0 || row > kMaxFrame)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("row 必须 >= 0"));
        if (row != entity.row)
        {
            entity.row = row;
            changed->append(QStringLiteral("row"));
        }
    }

    const struct
    {
        const char* key;
        bool model::Entity::*member;
        const char* label;
    } flags[] = {
        {"hidden", &model::Entity::hidden, "hidden"},
        {"inheritVisibility", &model::Entity::inheritVisibility, "inheritVisibility"},
        {"visible", &model::Entity::visible, "visible"},
        {"physicsInteract", &model::Entity::physicsInteract, "physicsInteract"},
    };
    for (const auto& flag : flags)
    {
        if (!patch.contains(QLatin1String(flag.key)))
            continue;
        bool value = entity.*(flag.member);
        if (!readBool(patch, flag.key, &value, &error, true))
            return error;
        if (value != entity.*(flag.member))
        {
            entity.*(flag.member) = value;
            changed->append(QLatin1String(flag.label));
        }
    }

    if (patch.contains(QStringLiteral("locked")))
    {
        bool locked = entity.locked;
        if (!readBool(patch, "locked", &locked, &error, true))
            return error;
        if (!locked)
            return failure(QStringLiteral("permission_denied"),
                           QStringLiteral("MCP 不能解锁实体"));
        if (!entity.locked)
        {
            entity.locked = true;
            changed->append(QStringLiteral("locked"));
        }
    }

    if (patch.contains(QStringLiteral("transform")))
    {
        const QJsonValue value = patch.value(QStringLiteral("transform"));
        if (!value.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("transform 必须是对象"));
        QJsonObject merged = model::toJson(entity.transform);
        const QJsonObject update = value.toObject();
        for (auto it = update.constBegin(); it != update.constEnd(); ++it)
        {
            if (!isFiniteNumber(it.value().toDouble()) || !it.value().isDouble())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("transform.%1 必须是有限数").arg(it.key()));
            merged.insert(it.key(), it.value());
            changed->append(QStringLiteral("transform.") + it.key());
        }
        QString transformError;
        const auto transform = model::transformFromJson(merged, &transformError);
        if (!transform)
            return failure(QStringLiteral("invalid_argument"), transformError);
        entity.transform = *transform;
    }

    if (patch.contains(QStringLiteral("content")))
    {
        const QJsonValue value = patch.value(QStringLiteral("content"));
        if (!value.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("content 必须是对象"));
        const QJsonObject update = value.toObject();
        QString kindName =
            update.value(QStringLiteral("kind")).toString(contentKindName(entity.content.kind));
        const QJsonValue kindValue = update.value(QStringLiteral("kind"));
        if (!kindValue.isUndefined() && !kindValue.isString())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("content.kind 必须是字符串"));

        const ContentKind current = entity.content.kind;
        const auto isTextual = [](ContentKind kind) {
            return kind == ContentKind::Text || kind == ContentKind::Vector;
        };
        ContentKind target = current;
        if (kindName == QLatin1String("empty"))
            target = ContentKind::Empty;
        else if (kindName == QLatin1String("text"))
            target = ContentKind::Text;
        else if (kindName == QLatin1String("vector"))
            target = ContentKind::Vector;
        else if (kindName == QLatin1String("camera"))
            target = ContentKind::Camera;
        else if (kindName == QLatin1String("asset"))
            target = ContentKind::Asset;
        else
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知内容类型：%1").arg(kindName));

        if (current == ContentKind::Asset && target != ContentKind::Asset)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("素材内容请先用 asset.unassign 清空"));
        if (target == ContentKind::Asset && current != ContentKind::Asset)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("换素材请用 asset.assign"));
        if (target != current)
        {
            const bool allowedChange =
                (current == ContentKind::Empty)
                || (isTextual(current) && isTextual(target))
                || (current == ContentKind::Camera
                    && target == ContentKind::Empty);
            if (!allowedChange)
                return failure(QStringLiteral("conflict"),
                               QStringLiteral("不允许从 %1 切换到 %2")
                                   .arg(contentKindName(current),
                                        contentKindName(target)));
            entity.content.kind = target;
            kindName = contentKindName(target);
            changed->append(QStringLiteral("content.kind"));
        }

        if (target == ContentKind::Asset)
        {
            if (update.contains(QStringLiteral("assetHash"))
                || update.contains(QStringLiteral("assetType")))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("assetHash/assetType 不能通过 entity.update 修改"));
            const QJsonValue assetValue = update.value(QStringLiteral("asset"));
            if (!assetValue.isUndefined() && !assetValue.isNull())
            {
                if (!assetValue.isObject())
                    return failure(QStringLiteral("invalid_argument"),
                                   QStringLiteral("content.asset 必须是对象"));
                const QJsonObject asset = assetValue.toObject();
                if (asset.contains(QStringLiteral("sourceIn")))
                {
                    int sourceIn = entity.content.asset.sourceIn;
                    if (!readInt(asset, "sourceIn", &sourceIn, &error, true))
                        return error;
                    if (sourceIn < 0)
                        return failure(QStringLiteral("invalid_argument"),
                                       QStringLiteral("sourceIn 不能小于 0"));
                    if (const auto stored =
                            tx.store->assets().get(entity.content.asset.hash))
                    {
                        const int frames = assetDurationFrames(*stored);
                        if (frames > 0
                            && sourceIn + entity.duration() > frames)
                            return failure(
                                QStringLiteral("conflict"),
                                QStringLiteral("sourceIn + 时长超出素材长度 %1")
                                    .arg(frames));
                    }
                    if (sourceIn != entity.content.asset.sourceIn)
                    {
                        entity.content.asset.sourceIn = sourceIn;
                        changed->append(QStringLiteral("content.asset.sourceIn"));
                    }
                }
                if (asset.contains(QStringLiteral("volume")))
                {
                    double volume = entity.content.asset.volume;
                    if (!readDouble(asset, "volume", &volume, &error, true))
                        return error;
                    if (volume < 0)
                        return failure(QStringLiteral("invalid_argument"),
                                       QStringLiteral("volume 不能小于 0"));
                    if (volume != entity.content.asset.volume)
                    {
                        entity.content.asset.volume = volume;
                        changed->append(QStringLiteral("content.asset.volume"));
                    }
                }
            }
        }
        else
        {
            QJsonObject merged = model::toJson(entity.content);
            merged.insert(QStringLiteral("kind"), kindName);
            for (auto it = update.constBegin(); it != update.constEnd(); ++it)
            {
                if (it.key() == QLatin1String("kind"))
                    continue;
                if ((it.key() == QLatin1String("text")
                     || it.key() == QLatin1String("vector")
                     || it.key() == QLatin1String("camera"))
                    && it.value().isObject())
                {
                    QJsonObject nested = merged.value(it.key()).toObject();
                    const QJsonObject patchObject = it.value().toObject();
                    for (auto nestedIt = patchObject.constBegin();
                         nestedIt != patchObject.constEnd(); ++nestedIt)
                        nested.insert(nestedIt.key(), nestedIt.value());
                    merged.insert(it.key(), nested);
                }
                else
                {
                    merged.insert(it.key(), it.value());
                }
                changed->append(QStringLiteral("content.") + it.key());
            }
            QString contentError;
            const auto content = model::contentFromJson(merged, &contentError);
            if (!content)
                return failure(QStringLiteral("invalid_argument"), contentError);
            entity.content = *content;
        }
    }

    return success();
}

} // namespace

Result trimEntityRange(Tx& tx, const Uuid& uuid, int newStart, int newEnd,
                       TrimSummary* summary)
{
    return trimRecursive(tx, uuid, newStart, newEnd, summary, true);
}

Result entityCreate(Tx& tx, const QJsonObject& args)
{
    Uuid uuid;
    Result result = createEntityInternal(tx, args, nullptr, &uuid);
    if (!result.ok)
        return result;
    return tx.finish(createdEntityData(tx, uuid),
                     QStringLiteral("pvf: create entity \"%1\"")
                         .arg(findEntity(tx.p(), uuid)->name));
}

Result entityCreateMany(Tx& tx, const QJsonObject& args)
{
    Result error;
    const QJsonValue entitiesValue = args.value(QStringLiteral("entities"));
    if (!entitiesValue.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("entities 必须是数组"));
    const QJsonArray entities = entitiesValue.toArray();
    if (entities.isEmpty() || entities.size() > kMaxBatch)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("entities 数量必须在 1..%1").arg(kMaxBatch));

    QSet<Uuid> existing;
    for (auto it = tx.p().entities.constBegin(); it != tx.p().entities.constEnd();
         ++it)
        existing.insert(it.key());

    QStringList uuids;
    QJsonArray created;
    for (int i = 0; i < entities.size(); ++i)
    {
        if (!entities.at(i).isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("entities[%1] 必须是对象").arg(i));
        Uuid uuid;
        Result result = createEntityInternal(tx, entities.at(i).toObject(),
                                             &existing, &uuid);
        if (!result.ok)
        {
            result.details.insert(QStringLiteral("index"), i);
            return result;
        }
        uuids << uuid;
        QJsonObject item;
        item.insert(QStringLiteral("uuid"), uuid);
        item.insert(QStringLiteral("name"),
                    findEntity(tx.p(), uuid)->name);
        created.append(item);
    }

    QJsonObject data;
    data.insert(QStringLiteral("uuids"), QJsonArray::fromStringList(uuids));
    data.insert(QStringLiteral("created"), created);
    return tx.finish(data, QStringLiteral("pvf: create %1 entities").arg(uuids.size()));
}

Result entityCreateTree(Tx& tx, const QJsonObject& args)
{
    Result error;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (it.key() != QLatin1String("parentUuid")
            && it.key() != QLatin1String("root"))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }
    Uuid parentUuid;
    if (!readUuid(args, "parentUuid", &parentUuid, &error, false, true))
        return error;
    const QJsonValue rootValue = args.value(QStringLiteral("root"));
    if (!rootValue.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("root 必须是对象"));
    if (!parentUuid.isEmpty() && !findEntity(tx.p(), parentUuid))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("父实体不存在：%1").arg(parentUuid));

    QVector<QJsonObject> mapping;
    QStringList uuids;

    std::function<Result(const QJsonObject&, const Uuid&, const QString&, int)> build;
    build = [&](const QJsonObject& node, const Uuid& parent, const QString& path,
                int depth) -> Result {
        if (depth > kMaxDepth)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("树深度超过 %1").arg(kMaxDepth));
        if (uuids.size() >= kMaxTreeNodes)
            return failure(QStringLiteral("limit_exceeded"),
                           QStringLiteral("单棵树最多 %1 个节点").arg(kMaxTreeNodes));
        if (node.contains(QStringLiteral("children")))
        {
            if (!node.value(QStringLiteral("children")).isArray())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("children 必须是数组"));
        }

        QJsonObject nodeArgs = node;
        nodeArgs.remove(QStringLiteral("children"));
        nodeArgs.insert(QStringLiteral("parentUuid"),
                        parent.isEmpty() ? QJsonValue(QJsonValue::Null)
                                         : QJsonValue(parent));

        Uuid uuid;
        Result result = createEntityInternal(tx, nodeArgs, nullptr, &uuid);
        if (!result.ok)
            return result;

        uuids << uuid;
        QJsonObject mapItem;
        mapItem.insert(QStringLiteral("path"), path);
        mapItem.insert(QStringLiteral("uuid"), uuid);
        mapping.append(mapItem);

        const QJsonArray children =
            node.value(QStringLiteral("children")).toArray();
        for (int i = 0; i < children.size(); ++i)
        {
            if (!children.at(i).isObject())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("children[%1] 必须是对象").arg(i));
            const model::Entity* parentEntity = findEntity(tx.p(), uuid);
            QJsonObject child = children.at(i).toObject();
            if (!child.contains(QStringLiteral("start")) && parentEntity)
                child.insert(QStringLiteral("start"), parentEntity->start);
            if (!child.contains(QStringLiteral("end")) && parentEntity)
                child.insert(QStringLiteral("end"), parentEntity->end);
            Result childResult =
                build(child, uuid, path + QStringLiteral(".") + QString::number(i),
                      depth + 1);
            if (!childResult.ok)
                return childResult;
        }
        return success();
    };

    const Uuid rootParent = parentUuid;
    Result built = build(rootValue.toObject(), rootParent, QStringLiteral("0"), 0);
    if (!built.ok)
        return built;

    QJsonArray mappingArray;
    for (const QJsonObject& item : std::as_const(mapping))
        mappingArray.append(item);

    QJsonObject data;
    data.insert(QStringLiteral("rootUuid"), uuids.first());
    data.insert(QStringLiteral("uuids"), QJsonArray::fromStringList(uuids));
    data.insert(QStringLiteral("mapping"), mappingArray);
    data.insert(QStringLiteral("entityCount"), uuids.size());

    const model::Entity* rootEntity = findEntity(tx.p(), uuids.first());
    return tx.finish(data, QStringLiteral("pvf: create tree \"%1\" (%2 entities)")
                               .arg(rootEntity ? rootEntity->name : QString(),
                                    QString::number(uuids.size())));
}

Result entityGet(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    const model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));

    int frame = entity->start;
    if (!readInt(args, "frame", &frame, &error))
        return error;
    if (frame < 0 || frame > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 必须在 0..%1").arg(kMaxFrame));

    bool includeChildren = false;
    if (!readBool(args, "includeChildren", &includeChildren, &error))
        return error;

    const int relative = frame - entity->start;
    const model::SolverSegment* activeSolver = nullptr;
    for (const model::SolverSegment& segment : entity->solvers)
    {
        if (relative >= segment.startFrame
            && relative <= solverEffectiveEnd(*entity, segment))
        {
            activeSolver = &segment;
            break;
        }
    }

    const auto valueOf = [&](const QString& track, double fallback,
                             bool step = false) {
        const auto it = entity->tracks.constFind(track);
        if (it == entity->tracks.constEnd())
            return fallback;
        const int evaluateAt =
            activeSolver && drivenTracks(activeSolver->effect).contains(track)
                ? activeSolver->startFrame
                : relative;
        const auto value = model::evaluateTrack(it.value(), evaluateAt, step);
        return value ? *value : fallback;
    };

    QJsonObject evaluated;
    evaluated.insert(QStringLiteral("x"),
                     valueOf(QStringLiteral("x"), entity->transform.x));
    evaluated.insert(QStringLiteral("y"),
                     valueOf(QStringLiteral("y"), entity->transform.y));
    evaluated.insert(QStringLiteral("rotation"),
                     valueOf(QStringLiteral("rotation"), entity->transform.rotation));
    evaluated.insert(QStringLiteral("scaleX"),
                     valueOf(QStringLiteral("scaleX"), entity->transform.scaleX));
    evaluated.insert(QStringLiteral("scaleY"),
                     valueOf(QStringLiteral("scaleY"), entity->transform.scaleY));
    evaluated.insert(QStringLiteral("opacity"),
                     valueOf(QStringLiteral("opacity"), entity->transform.opacity));
    evaluated.insert(QStringLiteral("visible"),
                     valueOf(QStringLiteral("visible"), entity->visible ? 1.0 : 0.0,
                             true)
                         >= 0.5);
    if (entity->content.kind == ContentKind::Asset
        && entity->content.asset.type == AssetType::Audio)
        evaluated.insert(QStringLiteral("volume"),
                         valueOf(QStringLiteral("volume"),
                                 entity->content.asset.volume));

    QJsonObject data = entityFull(*entity);
    data.insert(QStringLiteral("evaluated"), evaluated);

    QJsonArray solvers;
    for (const model::SolverSegment& segment : entity->solvers)
        solvers.append(model::toJson(segment));
    data.insert(QStringLiteral("solvers"), solvers);
    if (activeSolver)
        data.insert(QStringLiteral("activeSolverId"), activeSolver->id);

    if (includeChildren)
    {
        QJsonArray children;
        const QVector<Uuid> ids = model::childrenOf(tx.p(), uuid);
        for (const Uuid& childId : ids)
        {
            if (const model::Entity* child = findEntity(tx.p(), childId))
                children.append(entityBrief(*child));
        }
        data.insert(QStringLiteral("children"), children);
    }

    if (entity->content.kind == ContentKind::Asset)
    {
        if (const auto asset = tx.store->assets().get(entity->content.asset.hash))
            data.insert(QStringLiteral("asset"),
                        assetJson(tx.store->assets(), tx.p(), *asset));
        else
            data.insert(QStringLiteral("assetMissing"), true);
    }

    return success(data);
}

Result entityList(Tx& tx, const QJsonObject& args)
{
    Result error;
    int offset = 0;
    int limit = 100;
    if (!readInt(args, "offset", &offset, &error)
        || !readInt(args, "limit", &limit, &error))
        return error;
    if (offset < 0 || limit < 1 || limit > 500)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("分页参数非法（limit 1..500）"));

    const bool filterParent = args.contains(QStringLiteral("parentUuid"));
    Uuid parentUuid;
    if (!readUuid(args, "parentUuid", &parentUuid, &error, false, true))
        return error;
    if (filterParent && !parentUuid.isEmpty() && !findEntity(tx.p(), parentUuid))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("父实体不存在：%1").arg(parentUuid));

    bool includeKeyframes = false;
    bool includeSolvers = false;
    if (!readBool(args, "includeKeyframes", &includeKeyframes, &error)
        || !readBool(args, "includeSolvers", &includeSolvers, &error))
        return error;

    QVector<model::Entity> entities;
    for (auto it = tx.p().entities.constBegin(); it != tx.p().entities.constEnd();
         ++it)
    {
        if (filterParent && it.value().parentUuid != parentUuid)
            continue;
        entities.append(it.value());
    }
    std::sort(entities.begin(), entities.end(),
              [](const model::Entity& a, const model::Entity& b) {
                  if (a.row != b.row)
                      return a.row < b.row;
                  if (a.start != b.start)
                      return a.start < b.start;
                  return a.uuid < b.uuid;
              });

    QJsonArray array;
    for (int i = offset; i < entities.size() && i < offset + limit; ++i)
    {
        QJsonObject object = entityBrief(entities.at(i));
        if (includeKeyframes)
        {
            QJsonObject keyframes;
            for (auto it = entities.at(i).tracks.constBegin();
                 it != entities.at(i).tracks.constEnd(); ++it)
                keyframes.insert(it.key(), model::toJson(it.value()));
            object.insert(QStringLiteral("keyframes"), keyframes);
        }
        if (includeSolvers)
        {
            QJsonArray solvers;
            for (const model::SolverSegment& segment : entities.at(i).solvers)
                solvers.append(model::toJson(segment));
            object.insert(QStringLiteral("solvers"), solvers);
        }
        array.append(object);
    }

    QJsonObject data;
    data.insert(QStringLiteral("entities"), array);
    data.insert(QStringLiteral("page"), pageJson(offset, limit, entities.size()));
    return success(data);
}

Result entityUpdate(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    const QJsonValue patchValue = args.value(QStringLiteral("patch"));
    if (!patchValue.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("patch 必须是对象"));

    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    if (!ensureUnlocked(tx.p(), uuid, &error))
        return error;

    QStringList changed;
    Result applied = applyEntityPatch(tx, *entity, patchValue.toObject(), &changed);
    if (!applied.ok)
        return applied;

    if (!changed.isEmpty())
        tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("changed"), QJsonArray::fromStringList(changed));
    data.insert(QStringLiteral("entity"), entityBrief(*entity));
    return tx.finish(data, QStringLiteral("pvf: update entity \"%1\"")
                               .arg(entity->name));
}

Result entityUpdateMany(Tx& tx, const QJsonObject& args)
{
    Result error;
    const QJsonValue itemsValue = args.value(QStringLiteral("items"));
    if (!itemsValue.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("items 必须是数组"));
    const QJsonArray items = itemsValue.toArray();
    if (items.isEmpty() || items.size() > kMaxBatch)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("items 数量必须在 1..%1").arg(kMaxBatch));

    QJsonArray results;
    for (int i = 0; i < items.size(); ++i)
    {
        if (!items.at(i).isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("items[%1] 必须是对象").arg(i));
        const QJsonObject item = items.at(i).toObject();
        Uuid uuid;
        if (!readUuid(item, "uuid", &uuid, &error, true))
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }
        const QJsonValue patchValue = item.value(QStringLiteral("patch"));
        if (!patchValue.isObject())
        {
            error = failure(QStringLiteral("invalid_argument"),
                            QStringLiteral("items[%1].patch 必须是对象").arg(i));
            return error;
        }
        model::Entity* entity = findEntity(tx.p(), uuid);
        if (!entity)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("实体不存在：%1").arg(uuid),
                           QJsonObject{{QStringLiteral("index"), i}});
        if (!ensureUnlocked(tx.p(), uuid, &error))
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }

        QStringList changed;
        Result applied = applyEntityPatch(tx, *entity, patchValue.toObject(), &changed);
        if (!applied.ok)
        {
            applied.details.insert(QStringLiteral("index"), i);
            return applied;
        }
        tx.touch(uuid);

        QJsonObject result;
        result.insert(QStringLiteral("uuid"), uuid);
        result.insert(QStringLiteral("changed"), QJsonArray::fromStringList(changed));
        results.append(result);
    }

    QJsonObject data;
    data.insert(QStringLiteral("count"), results.size());
    data.insert(QStringLiteral("items"), results);
    return tx.finish(data, QStringLiteral("pvf: update %1 entities").arg(results.size()));
}

Result entityMove(Tx& tx, const QJsonObject& args)
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
    const bool hasDelta = args.contains(QStringLiteral("delta"));
    const bool hasRow = args.contains(QStringLiteral("row"));
    if (hasStart == hasDelta)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("start 与 delta 必须且只能给一个"));
    if (!hasStart && !hasDelta && !hasRow)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("至少要修改 start/delta 或 row"));

    int delta = 0;
    if (hasStart)
    {
        int start = 0;
        if (!readInt(args, "start", &start, &error, true))
            return error;
        delta = start - entity->start;
    }
    else if (hasDelta)
    {
        if (!readInt(args, "delta", &delta, &error, true))
            return error;
    }

    int row = entity->row;
    if (hasRow && !readInt(args, "row", &row, &error, true))
        return error;
    if (row < 0 || row > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("row 必须 >= 0"));

    const int newStart = entity->start + delta;
    const int newEnd = entity->end + delta;
    if (newStart < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("移动后 start 不能小于 0"));

    if (!entity->parentUuid.isEmpty())
    {
        const model::Entity* parent = findEntity(tx.p(), entity->parentUuid);
        if (parent && (newStart < parent->start || newEnd > parent->end))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("移动后范围超出父实体范围"));
    }

    QVector<Uuid> subtreeIds = subtree(tx.p(), uuid);
    for (const Uuid& childId : std::as_const(subtreeIds))
    {
        if (!ensureUnlocked(tx.p(), childId, &error))
            return error;
    }

    QSet<Uuid> moved;
    entity->start = newStart;
    entity->end = newEnd;
    entity->row = row;
    tx.touch(uuid);
    moved.insert(uuid);

    for (const Uuid& childId : std::as_const(subtreeIds))
    {
        model::Entity* child = findEntity(tx.p(), childId);
        if (!child)
            continue;
        if (child->start + delta < 0)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("移动后子实体 start 小于 0"));
        child->start += delta;
        child->end += delta;
        tx.touch(childId);
        moved.insert(childId);
    }

    QVector<Uuid> movedIds(moved.begin(), moved.end());
    std::sort(movedIds.begin(), movedIds.end());
    movedIds.removeAll(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("delta"), delta);
    data.insert(QStringLiteral("start"), entity->start);
    data.insert(QStringLiteral("end"), entity->end);
    data.insert(QStringLiteral("row"), entity->row);
    data.insert(QStringLiteral("childrenMoved"),
                QJsonArray::fromStringList(movedIds));
    return tx.finish(data, QStringLiteral("pvf: move entity \"%1\" %2f")
                               .arg(entity->name)
                               .arg(delta));
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

Result entityReparent(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    if (!args.contains(QStringLiteral("parentUuid")))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("缺少参数 parentUuid"));
    Uuid newParent;
    if (!readUuid(args, "parentUuid", &newParent, &error, true, true))
        return error;

    QString mode;
    if (!readString(args, "mode", &mode, &error, true))
        return error;
    if (mode != QLatin1String("preserveWorld") && mode != QLatin1String("keepLocal"))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("mode 必须是 preserveWorld 或 keepLocal"));

    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    if (!ensureUnlocked(tx.p(), uuid, &error))
        return error;

    model::Entity* parent = nullptr;
    if (!newParent.isEmpty())
    {
        if (newParent == uuid)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("实体不能成为自己的父级"));
        parent = findEntity(tx.p(), newParent);
        if (!parent)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("新父实体不存在：%1").arg(newParent));
        if (subtree(tx.p(), uuid).contains(newParent))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("不能把实体挂到自己的后代下"));
        if (!ensureUnlocked(tx.p(), newParent, &error))
            return error;
        if (depthOf(tx.p(), newParent) + 1 > kMaxDepth)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("父子层级超过 %1 层").arg(kMaxDepth));
    }

    if (parent && (entity->start < parent->start || entity->end > parent->end))
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("实体范围超出新父实体范围"));

    int row = 0;
    if (args.contains(QStringLiteral("row")))
    {
        if (!readInt(args, "row", &row, &error, true))
            return error;
        if (row < 0)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("row 必须 >= 0"));
    }
    else
    {
        row = defaultRow(tx.p(), newParent, entity->start, entity->end);
    }

    if (mode == QLatin1String("preserveWorld"))
    {
        const Affine oldWorld = worldAffine(tx.p(), uuid, entity->start);
        const Affine parentWorld =
            newParent.isEmpty() ? Affine() : worldAffine(tx.p(), newParent, entity->start);
        const Affine local = compose(inverse(parentWorld), oldWorld);

        const double determinant =
            local.a00 * local.a11 - local.a01 * local.a10;
        double scaleX = std::hypot(local.a00, local.a10);
        double scaleY =
            scaleX > 1e-12 ? determinant / scaleX : std::hypot(local.a01, local.a11);
        const double rotation = std::atan2(local.a10, local.a00) * 180.0 / kPi;

        const double pivotX = evaluated(*entity, QStringLiteral("pivotX"),
                                        entity->start, entity->transform.pivotX);
        const double pivotY = evaluated(*entity, QStringLiteral("pivotY"),
                                        entity->start, entity->transform.pivotY);

        entity->transform.rotation = rotation;
        entity->transform.scaleX = scaleX;
        entity->transform.scaleY = scaleY;
        entity->transform.pivotX = pivotX;
        entity->transform.pivotY = pivotY;
        entity->transform.x = local.tx - pivotX
                              + (local.a00 * pivotX + local.a01 * pivotY);
        entity->transform.y = local.ty - pivotY
                              + (local.a10 * pivotX + local.a11 * pivotY);
    }

    entity->parentUuid = newParent;
    entity->row = row;
    tx.touch(uuid);

    QJsonObject transform = model::toJson(entity->transform);
    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("parentUuid"),
                newParent.isEmpty() ? QJsonValue(QJsonValue::Null)
                                    : QJsonValue(newParent));
    data.insert(QStringLiteral("mode"), mode);
    data.insert(QStringLiteral("row"), row);
    data.insert(QStringLiteral("transform"), transform);
    return tx.finish(data, QStringLiteral("pvf: reparent entity \"%1\"")
                               .arg(entity->name));
}

Result entityDuplicate(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    const model::Entity* source = findEntity(tx.p(), uuid);
    if (!source)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));

    bool deep = true;
    if (!readBool(args, "deep", &deep, &error))
        return error;
    int offsetFrames = 0;
    if (!readInt(args, "offsetFrames", &offsetFrames, &error))
        return error;

    Uuid targetParent = source->parentUuid;
    const bool hasParent = args.contains(QStringLiteral("parentUuid"));
    if (!readUuid(args, "parentUuid", &targetParent, &error, false, true))
        return error;
    if (hasParent && !targetParent.isEmpty())
    {
        if (!findEntity(tx.p(), targetParent))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("目标父实体不存在：%1").arg(targetParent));
        if (!ensureUnlocked(tx.p(), targetParent, &error))
            return error;
    }
    if (!targetParent.isEmpty())
    {
        const model::Entity* parent = findEntity(tx.p(), targetParent);
        if (parent && !ensureUnlocked(tx.p(), parent->uuid, &error))
            return error;
    }

    QVector<Uuid> nodes{uuid};
    if (deep)
        nodes += subtree(tx.p(), uuid);
    if (nodes.size() > kMaxBatch)
        return failure(QStringLiteral("limit_exceeded"),
                       QStringLiteral("单次最多复制 %1 个实体").arg(kMaxBatch));
    if (tx.p().entities.size() + nodes.size() > kMaxEntities)
        return failure(QStringLiteral("limit_exceeded"),
                       QStringLiteral("工程实体数超过 %1").arg(kMaxEntities));

    QHash<Uuid, Uuid> idMap;
    for (const Uuid& node : std::as_const(nodes))
        idMap.insert(node, model::newUuid());

    QString name = source->name + QStringLiteral(" 副本");
    if (!readString(args, "name", &name, &error))
        return error;
    if (name.isEmpty() || name.size() > 256)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("name 必须是 1..256 字符"));

    int rootRow = 0;
    const bool hasRow = args.contains(QStringLiteral("row"));
    if (hasRow && !readInt(args, "row", &rootRow, &error, true))
        return error;
    if (!hasRow)
        rootRow = defaultRow(tx.p(), targetParent, source->start + offsetFrames,
                             source->end + offsetFrames);
    if (rootRow < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("row 必须 >= 0"));

    QJsonArray uuidMap;
    for (const Uuid& node : std::as_const(nodes))
    {
        const model::Entity* original = findEntity(tx.p(), node);
        if (!original)
            continue;
        model::Entity copy = *original;
        copy.uuid = idMap.value(node);
        if (node == uuid)
            copy.parentUuid = targetParent;
        else
            copy.parentUuid = idMap.value(original->parentUuid);
        copy.start = original->start + offsetFrames;
        copy.end = original->end + offsetFrames;
        copy.row = node == uuid ? rootRow
                                : defaultRow(tx.p(), copy.parentUuid, copy.start,
                                             copy.end);
        copy.name = node == uuid ? name : original->name;

        if (copy.start < 0)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("复制后 start 不能小于 0"));
        if (!(node == uuid) && copy.parentUuid.isEmpty())
            return failure(QStringLiteral("internal_error"),
                           QStringLiteral("复制子树时丢失父级映射"));

        const model::Entity* newParent =
            copy.parentUuid.isEmpty() ? nullptr : findEntity(tx.p(), copy.parentUuid);
        if (!copy.parentUuid.isEmpty() && !newParent)
            return failure(QStringLiteral("internal_error"),
                           QStringLiteral("复制目标父级不存在"));
        if (newParent
            && (copy.start < newParent->start || copy.end > newParent->end))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("复制后实体范围超出父实体范围"));

        for (model::SolverSegment& segment : copy.solvers)
            segment.id = model::newUuid();

        tx.p().entities.insert(copy.uuid, copy);
        tx.touch(copy.uuid);

        QJsonObject mapItem;
        mapItem.insert(QStringLiteral("from"), node);
        mapItem.insert(QStringLiteral("to"), copy.uuid);
        uuidMap.append(mapItem);
    }

    QJsonObject data;
    data.insert(QStringLiteral("sourceUuid"), uuid);
    data.insert(QStringLiteral("rootUuid"), idMap.value(uuid));
    data.insert(QStringLiteral("uuidMap"), uuidMap);
    data.insert(QStringLiteral("entityCount"), uuidMap.size());
    return tx.finish(data, QStringLiteral("pvf: duplicate entity \"%1\"")
                               .arg(source->name));
}

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
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affectedObject);
        return failure(QStringLiteral("confirm_required"),
                       QStringLiteral("删除实体需要 confirm"), details);
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
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affectedObject);
        return failure(QStringLiteral("confirm_required"),
                       QStringLiteral("批量删除需要 confirm"), details);
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
