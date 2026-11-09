#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf::mcp2::detail {

namespace {

constexpr int kMaxBatch = 200;

bool isTrackAllowed(const model::Project& project,
                    const assets::AssetStore& assetStore,
                    const model::Entity& entity, const QString& track,
                    Result* error)
{
    const QStringList tracks = allowedTracks(project, assetStore, entity);
    if (tracks.contains(track))
        return true;
    QJsonObject details;
    details.insert(QStringLiteral("allowedTracks"),
                   QJsonArray::fromStringList(tracks));
    *error = failure(QStringLiteral("invalid_argument"),
                     QStringLiteral("未知轨道：%1").arg(track), details);
    return false;
}

Result parseKeys(const QJsonValue& value, int duration, const QString& track,
                 model::Track* out)
{
    if (!value.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("keys 必须是数组"));
    const QJsonArray array = value.toArray();

    model::Track parsed;
    QSet<int> frames;
    for (int i = 0; i < array.size(); ++i)
    {
        if (!array.at(i).isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("keys[%1] 必须是对象").arg(i));
        const QJsonObject keyObject = array.at(i).toObject();
        const QJsonValue frameValue = keyObject.value(QStringLiteral("frame"));
        if (!frameValue.isDouble()
            || std::floor(frameValue.toDouble()) != frameValue.toDouble())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("keys[%1].frame 必须是整数").arg(i));
        if (!keyObject.value(QStringLiteral("value")).isDouble())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("keys[%1].value 必须是数字").arg(i));

        QString keyError;
        const auto key = model::keyframeFromJson(keyObject, &keyError);
        if (!key)
            return failure(QStringLiteral("invalid_argument"), keyError);

        model::EasingSpec easing;
        QJsonValue normalizedEasing;
        Result easingError;
        if (!parseEasing(keyObject.value(QStringLiteral("easing")), &easing,
                         &normalizedEasing, &easingError))
        {
            easingError.details.insert(QStringLiteral("index"), i);
            return easingError;
        }
        Q_UNUSED(normalizedEasing);
        model::Keyframe normalized = *key;
        normalized.easing = easing;

        if (normalized.frame < 0 || normalized.frame >= duration)
        {
            QJsonObject range;
            range.insert(QStringLiteral("min"), 0);
            range.insert(QStringLiteral("max"), duration - 1);
            QJsonObject details;
            details.insert(QStringLiteral("track"), track);
            details.insert(QStringLiteral("allowedRange"), range);
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("关键帧帧号 %1 超出轨道 %2 的范围 0..%3")
                               .arg(normalized.frame)
                               .arg(track)
                               .arg(duration - 1),
                           details);
        }
        if (frames.contains(normalized.frame))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("轨道 %1 的帧 %2 重复")
                               .arg(track)
                               .arg(normalized.frame));
        frames.insert(normalized.frame);
        if (!isFiniteNumber(normalized.value))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("轨道 %1 的关键帧值非法").arg(track));
        if (normalized.note.size() > 4096)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("关键帧 note 最长 4096 字符"));
        parsed.keys.append(normalized);
    }

    std::stable_sort(parsed.keys.begin(), parsed.keys.end(),
                     [](const model::Keyframe& a, const model::Keyframe& b) {
                         return a.frame < b.frame;
                     });
    *out = parsed;
    return success();
}

QJsonArray solverConflicts(const model::Entity& entity, const QString& track,
                           const QVector<int>& frames = {})
{
    QJsonArray conflicts;
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (!drivenTracks(segment.effect).contains(track))
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("solverId"), segment.id);
        item.insert(QStringLiteral("startFrame"), segment.startFrame);
        item.insert(QStringLiteral("endFrame"), segment.endFrame);
        conflicts.append(item);
    }
    Q_UNUSED(frames);
    return conflicts;
}

Result solverConflict(const model::Entity& entity, const QString& track,
                      const QVector<int>& frames = {})
{
    QJsonObject details;
    details.insert(QStringLiteral("conflicts"),
                   solverConflicts(entity, track, frames));
    if (!frames.isEmpty())
    {
        QJsonArray framesArray;
        for (int frame : frames)
            framesArray.append(frame);
        details.insert(QStringLiteral("conflictingFrames"), framesArray);
    }
    return failure(QStringLiteral("conflict"),
                   QStringLiteral("轨道 %1 被求解段驱动，不能写普通关键帧")
                       .arg(track),
                   details);
}

Result checkSetSolverRules(const model::Entity& entity, const QString& track,
                           const model::Track& keys)
{
    QVector<int> interior;
    bool requiresStart = false;
    bool hasStart = false;

    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (!drivenTracks(segment.effect).contains(track))
            continue;
        requiresStart = true;
        const int end = solverEffectiveEnd(entity, segment);
        for (const model::Keyframe& key : keys.keys)
        {
            if (key.frame == segment.startFrame)
                hasStart = true;
            else if (key.frame > segment.startFrame && key.frame <= end)
                interior << key.frame;
        }
    }

    if (!interior.isEmpty())
        return solverConflict(entity, track, interior);
    if (requiresStart && !hasStart)
        return solverConflict(entity, track);
    return success();
}

model::Entity* writableEntity(Tx& tx, const Uuid& uuid, Result* error)
{
    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("实体不存在：%1").arg(uuid));
        return nullptr;
    }
    if (!ensureUnlocked(tx.p(), uuid, error))
        return nullptr;
    return entity;
}

} // namespace

Result keyframesGet(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    const model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));

    QString track;
    const bool hasTrack = args.contains(QStringLiteral("track"));
    if (!readString(args, "track", &track, &error))
        return error;
    if (hasTrack && !isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("duration"), entity->duration());
    if (hasTrack)
    {
        data.insert(QStringLiteral("track"), track);
        const auto it = entity->tracks.constFind(track);
        data.insert(QStringLiteral("keys"), it == entity->tracks.constEnd()
                                                ? QJsonArray()
                                                : model::toJson(it.value()));
    }
    else
    {
        QJsonObject tracks;
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd();
             ++it)
            tracks.insert(it.key(), model::toJson(it.value()));
        data.insert(QStringLiteral("tracks"), tracks);
    }
    return success(data);
}

Result keyframesSet(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    QString track;
    if (!readString(args, "track", &track, &error, true))
        return error;
    if (track.isEmpty())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("track 不能为空"));

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;

    model::Track parsed;
    Result keysResult =
        parseKeys(args.value(QStringLiteral("keys")), entity->duration(), track, &parsed);
    if (!keysResult.ok)
        return keysResult;

    Result solverResult = checkSetSolverRules(*entity, track, parsed);
    if (!solverResult.ok)
        return solverResult;

    entity->tracks.insert(track, parsed);
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("track"), track);
    data.insert(QStringLiteral("count"), parsed.keys.size());
    data.insert(QStringLiteral("keys"), model::toJson(parsed));
    return tx.finish(data, QStringLiteral("pvf: set keyframes \"%1\" %2")
                               .arg(entity->name, track));
}

Result keyframesSetMany(Tx& tx, const QJsonObject& args)
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

    QSet<QString> seen;
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
        QString track;
        if (!readString(item, "track", &track, &error, true))
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }
        const QString pair = uuid + QLatin1Char('/') + track;
        if (seen.contains(pair))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("同一批里 (uuid, track) 重复：%1").arg(pair),
                           QJsonObject{{QStringLiteral("index"), i}});
        seen.insert(pair);

        model::Entity* entity = writableEntity(tx, uuid, &error);
        if (!entity)
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }
        if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }

        model::Track parsed;
        Result keysResult = parseKeys(item.value(QStringLiteral("keys")),
                                      entity->duration(), track, &parsed);
        if (!keysResult.ok)
        {
            keysResult.details.insert(QStringLiteral("index"), i);
            return keysResult;
        }
        Result solverResult = checkSetSolverRules(*entity, track, parsed);
        if (!solverResult.ok)
        {
            solverResult.details.insert(QStringLiteral("index"), i);
            return solverResult;
        }

        entity->tracks.insert(track, parsed);
        tx.touch(uuid);

        QJsonObject result;
        result.insert(QStringLiteral("uuid"), uuid);
        result.insert(QStringLiteral("track"), track);
        result.insert(QStringLiteral("count"), parsed.keys.size());
        results.append(result);
    }

    QJsonObject data;
    data.insert(QStringLiteral("count"), results.size());
    data.insert(QStringLiteral("items"), results);
    return tx.finish(data, QStringLiteral("pvf: set keyframes (%1 tracks)")
                               .arg(results.size()));
}

Result keyframesRemove(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    QString track;
    if (!readString(args, "track", &track, &error, true))
        return error;
    int frame = 0;
    if (!readInt(args, "frame", &frame, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;
    if (frame < 0 || frame >= entity->duration())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 超出范围 0..%1")
                           .arg(entity->duration() - 1));

    for (const model::SolverSegment& segment : entity->solvers)
    {
        if (drivenTracks(segment.effect).contains(track)
            && segment.startFrame == frame)
            return solverConflict(*entity, track, {frame});
    }

    int removed = 0;
    auto it = entity->tracks.find(track);
    if (it != entity->tracks.end())
    {
        auto& keys = it.value().keys;
        const auto before = keys.size();
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [frame](const model::Keyframe& key) {
                                      return key.frame == frame;
                                  }),
                   keys.end());
        removed = before - keys.size();
        if (removed > 0)
            tx.touch(uuid);
    }

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("track"), track);
    data.insert(QStringLiteral("frame"), frame);
    data.insert(QStringLiteral("removed"), removed);
    return tx.finish(data, QStringLiteral("pvf: remove keyframe \"%1\" %2@%3")
                               .arg(entity->name, track)
                               .arg(frame));
}

Result keyframesMove(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    QString track;
    if (!readString(args, "track", &track, &error, true))
        return error;
    int fromFrame = 0;
    int toFrame = 0;
    if (!readInt(args, "fromFrame", &fromFrame, &error, true)
        || !readInt(args, "toFrame", &toFrame, &error, true))
        return error;
    bool overwrite = false;
    if (!readBool(args, "overwrite", &overwrite, &error))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;
    if (fromFrame < 0 || fromFrame >= entity->duration()
        || toFrame < 0 || toFrame >= entity->duration())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("帧号超出范围 0..%1")
                           .arg(entity->duration() - 1));

    auto it = entity->tracks.find(track);
    if (it == entity->tracks.end())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("轨道 %1 没有关键帧").arg(track));

    auto& keys = it.value().keys;
    auto source = std::find_if(keys.begin(), keys.end(),
                               [fromFrame](const model::Keyframe& key) {
                                   return key.frame == fromFrame;
                               });
    if (source == keys.end())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("帧 %1 没有关键帧").arg(fromFrame));

    if (fromFrame == toFrame)
    {
        QJsonObject data;
        data.insert(QStringLiteral("uuid"), uuid);
        data.insert(QStringLiteral("track"), track);
        data.insert(QStringLiteral("fromFrame"), fromFrame);
        data.insert(QStringLiteral("toFrame"), toFrame);
        data.insert(QStringLiteral("moved"), false);
        data.insert(QStringLiteral("replaced"), false);
        return tx.finish(data, QStringLiteral("pvf: move keyframe \"%1\" %2")
                                   .arg(entity->name, track));
    }

    for (const model::SolverSegment& segment : entity->solvers)
    {
        if (!drivenTracks(segment.effect).contains(track))
            continue;
        const int end = solverEffectiveEnd(*entity, segment);
        if (fromFrame == segment.startFrame)
            return solverConflict(*entity, track, {fromFrame});
        if (toFrame > segment.startFrame && toFrame <= end)
            return solverConflict(*entity, track, {toFrame});
    }

    const bool replaced =
        std::any_of(keys.constBegin(), keys.constEnd(),
                    [toFrame](const model::Keyframe& key) {
                        return key.frame == toFrame;
                    });
    if (replaced && !overwrite)
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("目标帧 %1 已有关键帧").arg(toFrame));

    model::Keyframe movedKey = *source;
    movedKey.frame = toFrame;
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [fromFrame, toFrame](const model::Keyframe& key) {
                                  return key.frame == fromFrame
                                         || (key.frame == toFrame);
                              }),
               keys.end());
    keys.append(movedKey);
    std::stable_sort(keys.begin(), keys.end(),
                     [](const model::Keyframe& a, const model::Keyframe& b) {
                         return a.frame < b.frame;
                     });
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("track"), track);
    data.insert(QStringLiteral("fromFrame"), fromFrame);
    data.insert(QStringLiteral("toFrame"), toFrame);
    data.insert(QStringLiteral("moved"), true);
    data.insert(QStringLiteral("replaced"), replaced);
    return tx.finish(data, QStringLiteral("pvf: move keyframe \"%1\" %2 %3->%4")
                               .arg(entity->name, track)
                               .arg(fromFrame)
                               .arg(toFrame));
}

Result keyframesClear(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    QString track;
    if (!readString(args, "track", &track, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;

    for (const model::SolverSegment& segment : entity->solvers)
    {
        if (drivenTracks(segment.effect).contains(track))
            return solverConflict(*entity, track);
    }

    int removed = 0;
    auto it = entity->tracks.find(track);
    if (it != entity->tracks.end())
    {
        removed = it.value().keys.size();
        entity->tracks.erase(it);
        tx.touch(uuid);
    }

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("track"), track);
    data.insert(QStringLiteral("removed"), removed);
    return tx.finish(data, QStringLiteral("pvf: clear keyframes \"%1\" %2")
                               .arg(entity->name, track));
}

Result keyframesSetEasing(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    QString track;
    if (!readString(args, "track", &track, &error, true))
        return error;
    int frame = 0;
    if (!readInt(args, "frame", &frame, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!isTrackAllowed(tx.p(), tx.store->assets(), *entity, track, &error))
        return error;
    if (frame < 0 || frame >= entity->duration())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 超出范围 0..%1")
                           .arg(entity->duration() - 1));

    auto it = entity->tracks.find(track);
    if (it == entity->tracks.end())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("轨道 %1 没有关键帧").arg(track));
    auto key = std::find_if(it.value().keys.begin(), it.value().keys.end(),
                            [frame](const model::Keyframe& candidate) {
                                return candidate.frame == frame;
                            });
    if (key == it.value().keys.end())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("帧 %1 没有关键帧").arg(frame));

    model::EasingSpec easing;
    QJsonValue normalized;
    if (!parseEasing(args.value(QStringLiteral("easing")), &easing, &normalized,
                     &error))
        return error;

    key->easing = easing;
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("track"), track);
    data.insert(QStringLiteral("frame"), frame);
    data.insert(QStringLiteral("easing"), normalized);
    return tx.finish(data, QStringLiteral("pvf: set easing \"%1\" %2@%3")
                               .arg(entity->name, track)
                               .arg(frame));
}

} // namespace pvf::mcp2::detail
