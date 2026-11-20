#include "mcp2/Internal.h"
#include "mcp2/KeyframePayload.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace pvf::mcp2::detail {

namespace {

constexpr int kMaxBatch = 200;
constexpr double kPi = 3.14159265358979323846;

// The whole-frame evaluation used as the base of keyframes.upsert: ordinary
// keyframe interpolation plus the instantaneous pose of an active solver
// segment, so a key created inside an automatic segment naturally ends it.
model::KeyframeSample sampleAt(const model::Project& project,
                               const assets::AssetStore& assets,
                               const model::Entity& entity, int relativeFrame)
{
    model::KeyframeSample sample =
        model::entitySampleAt(entity, relativeFrame);
    if (!anim::solverAt(entity, relativeFrame))
        return sample;

    const int projectFrame = entity.start + relativeFrame;
    render2::SceneWorld world(project, assets);
    anim::Cache cache;
    QTransform local =
        anim::evaluateEntity(project, entity.uuid, projectFrame, world, cache)
            .world;
    if (entity.space == model::EntitySpace::Parent
        && !entity.parentUuid.isEmpty()
        && findEntity(project, entity.parentUuid))
    {
        const QTransform parent =
            anim::evaluateEntity(project, entity.parentUuid, projectFrame,
                                 world, cache)
                .world;
        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (invertible)
            local = local * inverse;
    }

    const double a00 = local.m11();
    const double a01 = local.m21();
    const double a10 = local.m12();
    const double a11 = local.m22();
    const double pivotX = entity.transform.pivotX;
    const double pivotY = entity.transform.pivotY;

    sample.x = local.dx() - pivotX + (a00 * pivotX + a01 * pivotY);
    sample.y = local.dy() - pivotY + (a10 * pivotX + a11 * pivotY);
    sample.rotation = std::atan2(a10, a00) * 180.0 / kPi;
    return sample;
}

model::Keyframe keyframeFromSample(const model::KeyframeSample& sample, int frame)
{
    model::Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.x = sample.x;
    keyframe.y = sample.y;
    keyframe.rotation = sample.rotation;
    keyframe.scaleX = sample.scaleX;
    keyframe.scaleY = sample.scaleY;
    keyframe.opacity = sample.opacity;
    keyframe.visible = sample.visible;
    keyframe.physicsInteract = sample.physicsInteract;
    keyframe.content = sample.content;
    keyframe.htmlVars = sample.htmlVars;
    return keyframe;
}

Result rejectTrackArg(const QJsonObject& args)
{
    if (!args.contains(QStringLiteral("track")))
        return success();
    QJsonObject details;
    details.insert(QStringLiteral("deprecated"), QStringLiteral("track"));
    return failure(QStringLiteral("invalid_argument"),
                   QStringLiteral("track 已废弃：PVForge 使用整帧关键帧，没有"
                                  "单参数轨道"),
                   details);
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

QJsonObject keyframeList(const model::Entity& entity)
{
    QJsonObject data;
    data.insert(QStringLiteral("uuid"), entity.uuid);
    data.insert(QStringLiteral("duration"), entity.duration());
    data.insert(QStringLiteral("count"), entity.keyframes.size());
    data.insert(QStringLiteral("keyframes"), model::toJson(entity.keyframes));
    return data;
}

Result checkFrameValue(int frame, int duration)
{
    if (frame < 0 || frame > duration)
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), 0);
        range.insert(QStringLiteral("max"), duration);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        return failure(
            QStringLiteral("invalid_argument"),
            QStringLiteral("帧号 %1 超出实体范围 0..%2（frame == duration 允许）")
                .arg(frame)
                .arg(duration),
            details);
    }
    return success();
}

Result solverStartConflict(const model::Entity& entity, int frame)
{
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (segment.startFrame != frame)
            continue;
        QJsonObject details;
        details.insert(QStringLiteral("solverId"), segment.id);
        details.insert(QStringLiteral("startFrame"), segment.startFrame);
        return failure(
            QStringLiteral("conflict"),
            QStringLiteral("第 %1 帧是求解段起点，不能删除或移走；求解起点必须"
                           "保留整帧关键帧")
                .arg(frame),
            details);
    }
    return success();
}

// Applies the partial field set of keyframes.upsert over an evaluated base.
Result applyPartial(const model::Entity& entity, const QJsonObject& partial,
                    model::Keyframe* keyframe)
{
    static const QStringList allowed = {
        QStringLiteral("x"),        QStringLiteral("y"),
        QStringLiteral("rotation"), QStringLiteral("scaleX"),
        QStringLiteral("scaleY"),   QStringLiteral("opacity"),
        QStringLiteral("visible"),  QStringLiteral("physicsInteract"),
        QStringLiteral("content"),  QStringLiteral("htmlVars"),
        QStringLiteral("easing"),   QStringLiteral("note"),
    };
    for (auto it = partial.constBegin(); it != partial.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("partial 不允许字段 %1：整帧关键帧只接受 "
                               "x/y/rotation/scaleX/scaleY/opacity/visible/"
                               "physicsInteract/content/htmlVars/easing/note")
                    .arg(it.key()));
    }

    Result error;
    const struct
    {
        const char* name;
        double model::Keyframe::*member;
    } numbers[] = {
        {"x", &model::Keyframe::x},
        {"y", &model::Keyframe::y},
        {"rotation", &model::Keyframe::rotation},
        {"scaleX", &model::Keyframe::scaleX},
        {"scaleY", &model::Keyframe::scaleY},
        {"opacity", &model::Keyframe::opacity},
    };
    for (const auto& number : numbers)
    {
        if (!partial.contains(QLatin1String(number.name)))
            continue;
        double value = keyframe->*(number.member);
        if (!readDouble(partial, number.name, &value, &error, true))
            return error;
        if (!isFiniteNumber(value))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("partial.%1 必须是有限数")
                               .arg(QLatin1String(number.name)));
        keyframe->*(number.member) = value;
    }
    if (keyframe->opacity < 0.0 || keyframe->opacity > 1.0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("partial.opacity 必须在 0..1 之间"));

    if (partial.contains(QStringLiteral("visible")))
    {
        if (!readBool(partial, "visible", &keyframe->visible, &error, true))
            return error;
    }
    if (partial.contains(QStringLiteral("physicsInteract")))
    {
        if (!readBool(partial, "physicsInteract",
                      &keyframe->physicsInteract, &error, true))
            return error;
    }

    if (partial.contains(QStringLiteral("content")))
    {
        const QJsonValue value = partial.value(QStringLiteral("content"));
        if (!value.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("partial.content 必须是对象"));
        const QStringList animationKeys = model::animationKeys(entity.content);
        const QJsonObject values = value.toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        {
            if (!animationKeys.contains(it.key()))
                return failure(
                    QStringLiteral("invalid_argument"),
                    QStringLiteral("partial.content.%1 不是动画参数：定义参数不能"
                                   "写进关键帧（允许：%2）")
                        .arg(it.key(),
                             animationKeys.isEmpty()
                                 ? QStringLiteral("无")
                                 : animationKeys.join(QStringLiteral(", "))));
            if (!it.value().isDouble() || !isFiniteNumber(it.value().toDouble()))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("partial.content.%1 必须是有限数")
                                   .arg(it.key()));
            keyframe->content.insert(it.key(), it.value());
        }
    }

    if (partial.contains(QStringLiteral("htmlVars")))
    {
        const QJsonValue value = partial.value(QStringLiteral("htmlVars"));
        if (!value.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("partial.htmlVars 必须是对象"));
        const QJsonObject vars = value.toObject();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        {
            if (!it.value().isDouble() || !isFiniteNumber(it.value().toDouble()))
                return failure(
                    QStringLiteral("invalid_argument"),
                    QStringLiteral("partial.htmlVars.%1 必须是有限数（非数字变量"
                                   "不进关键帧）")
                        .arg(it.key()));
            keyframe->htmlVars.insert(it.key(), it.value());
        }
    }

    if (partial.contains(QStringLiteral("easing")))
    {
        QJsonValue normalized;
        if (!parseEasing(partial.value(QStringLiteral("easing")),
                         &keyframe->easing, &normalized, &error))
            return error;
    }

    if (partial.contains(QStringLiteral("note")))
    {
        if (!readString(partial, "note", &keyframe->note, &error, true))
            return error;
        if (keyframe->note.size() > 4096)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("partial.note 最长 4096 字符"));
    }

    QString validationError;
    if (!model::validateKeyframeContent(entity.content, keyframe->content,
                                        &validationError))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("partial.content：%1").arg(
                           validationError));
    if (!model::validateKeyframeHtmlVars(keyframe->htmlVars, &validationError))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("partial.htmlVars：%1").arg(
                           validationError));
    return success();
}

Result readFrames(const QJsonValue& value, int duration, QVector<int>* frames)
{
    if (!value.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frames 必须是整数数组"));
    QSet<int> seen;
    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isDouble() || std::floor(item.toDouble()) != item.toDouble())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("frames 必须是整数数组"));
        const int frame = int(item.toDouble());
        if (frame < 0 || frame > duration)
        {
            QJsonObject range;
            range.insert(QStringLiteral("min"), 0);
            range.insert(QStringLiteral("max"), duration);
            QJsonObject details;
            details.insert(QStringLiteral("allowedRange"), range);
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("帧号 %1 超出实体范围 0..%2")
                               .arg(frame)
                               .arg(duration),
                           details);
        }
        if (seen.contains(frame))
            continue;
        seen.insert(frame);
        frames->append(frame);
    }
    return success();
}

} // namespace

Result keyframesGet(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    const model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    return success(keyframeList(*entity));
}

Result keyframesSet(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    if (!args.contains(QStringLiteral("keyframes")))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("缺少参数 keyframes"));

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;

    QVector<model::Keyframe> keyframes;
    Result parsed = parseWholeFrameKeyframes(
        entity->content, entity->duration(),
        args.value(QStringLiteral("keyframes")), &keyframes);
    if (!parsed.ok)
        return parsed;
    Result solvers = checkKeyframesAgainstSolvers(*entity, keyframes);
    if (!solvers.ok)
        return solvers;

    entity->keyframes = keyframes;
    tx.touch(uuid);
    return tx.finish(keyframeList(*entity),
                     QStringLiteral("pvf: set whole-frame keyframes \"%1\" (%2)")
                         .arg(entity->name)
                         .arg(keyframes.size()));
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

    QSet<Uuid> seen;
    QJsonArray results;
    for (int i = 0; i < items.size(); ++i)
    {
        if (stopRequested(tx))
            return stoppedByHuman();
        if (!items.at(i).isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("items[%1] 必须是对象").arg(i));
        const QJsonObject item = items.at(i).toObject();
        for (auto it = item.constBegin(); it != item.constEnd(); ++it)
        {
            if (it.key() != QLatin1String("uuid")
                && it.key() != QLatin1String("keyframes"))
                return failure(
                    QStringLiteral("invalid_argument"),
                    QStringLiteral("items[%1] 含未知字段 %2（整帧关键帧没有 "
                                   "track）")
                        .arg(i)
                        .arg(it.key()));
        }
        Uuid uuid;
        if (!readUuid(item, "uuid", &uuid, &error, true))
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }
        if (seen.contains(uuid))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("同一批里 uuid 重复：%1").arg(uuid),
                           QJsonObject{{QStringLiteral("index"), i}});
        seen.insert(uuid);
        if (!item.contains(QStringLiteral("keyframes")))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("items[%1] 缺少 keyframes").arg(i));

        model::Entity* entity = writableEntity(tx, uuid, &error);
        if (!entity)
        {
            error.details.insert(QStringLiteral("index"), i);
            return error;
        }
        QVector<model::Keyframe> keyframes;
        Result parsed = parseWholeFrameKeyframes(
            entity->content, entity->duration(),
            item.value(QStringLiteral("keyframes")), &keyframes);
        if (!parsed.ok)
        {
            parsed.details.insert(QStringLiteral("index"), i);
            return parsed;
        }
        Result solvers = checkKeyframesAgainstSolvers(*entity, keyframes);
        if (!solvers.ok)
        {
            solvers.details.insert(QStringLiteral("index"), i);
            return solvers;
        }

        entity->keyframes = keyframes;
        tx.touch(uuid);

        QJsonObject result;
        result.insert(QStringLiteral("uuid"), uuid);
        result.insert(QStringLiteral("count"), keyframes.size());
        results.append(result);
    }

    QJsonObject data;
    data.insert(QStringLiteral("count"), results.size());
    data.insert(QStringLiteral("items"), results);
    return tx.finish(data,
                     QStringLiteral("pvf: set whole-frame keyframes (%1 entities)")
                         .arg(results.size()));
}

Result keyframesUpsert(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    int frame = 0;
    if (!readInt(args, "frame", &frame, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (Result range = checkFrameValue(frame, entity->duration()); !range.ok)
        return range;
    if (const model::SolverSegment* segment = blockingSolver(*entity, frame))
    {
        QJsonObject details;
        details.insert(QStringLiteral("solverId"), segment->id);
        details.insert(QStringLiteral("startFrame"), segment->startFrame);
        details.insert(QStringLiteral("endFrame"), segment->endFrame);
        return failure(
            QStringLiteral("conflict"),
            QStringLiteral("第 %1 帧由显式求解段驱动（%2..%3，含结束帧），不能"
                           "创建普通整帧关键帧")
                .arg(frame)
                .arg(segment->startFrame)
                .arg(segment->endFrame),
            details);
    }

    QJsonObject partial;
    if (args.contains(QStringLiteral("partial")))
    {
        const QJsonValue value = args.value(QStringLiteral("partial"));
        if (!value.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("partial 必须是对象"));
        partial = value.toObject();
    }

    model::Keyframe keyframe =
        keyframeFromSample(sampleAt(tx.p(), tx.store->assets(), *entity, frame),
                           frame);
    if (const model::Keyframe* existing =
            model::keyframeAt(entity->keyframes, frame))
    {
        keyframe.easing = existing->easing;
        keyframe.note = existing->note;
        keyframe.extras = existing->extras;
    }

    Result applied = applyPartial(*entity, partial, &keyframe);
    if (!applied.ok)
        return applied;

    model::upsertKeyframe(entity->keyframes, keyframe);
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("frame"), frame);
    data.insert(QStringLiteral("keyframe"), model::toJson(keyframe));
    data.insert(QStringLiteral("count"), entity->keyframes.size());
    return tx.finish(data,
                     QStringLiteral("pvf: upsert whole-frame keyframe \"%1\" @%2")
                         .arg(entity->name)
                         .arg(frame));
}

Result keyframesRemove(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    if (!args.contains(QStringLiteral("frames")))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("缺少参数 frames"));

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;

    QVector<int> frames;
    Result parsed = readFrames(args.value(QStringLiteral("frames")),
                               entity->duration(), &frames);
    if (!parsed.ok)
        return parsed;
    for (int frame : frames)
    {
        Result conflict = solverStartConflict(*entity, frame);
        if (!conflict.ok)
            return conflict;
    }

    int removed = 0;
    for (int frame : frames)
    {
        if (model::removeKeyframe(entity->keyframes, frame))
            ++removed;
    }
    if (removed > 0)
        tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("requested"), frames.size());
    data.insert(QStringLiteral("removed"), removed);
    data.insert(QStringLiteral("count"), entity->keyframes.size());
    data.insert(QStringLiteral("keyframes"), model::toJson(entity->keyframes));
    return tx.finish(data,
                     QStringLiteral("pvf: remove whole-frame keyframes \"%1\" (%2)")
                         .arg(entity->name)
                         .arg(removed));
}

Result keyframesMove(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
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
    if (Result range = checkFrameValue(fromFrame, entity->duration()); !range.ok)
        return range;
    if (Result range = checkFrameValue(toFrame, entity->duration()); !range.ok)
        return range;

    model::Keyframe* source = const_cast<model::Keyframe*>(
        model::keyframeAt(entity->keyframes, fromFrame));
    if (!source)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("第 %1 帧没有关键帧").arg(fromFrame));

    if (fromFrame == toFrame)
    {
        QJsonObject data;
        data.insert(QStringLiteral("uuid"), uuid);
        data.insert(QStringLiteral("fromFrame"), fromFrame);
        data.insert(QStringLiteral("toFrame"), toFrame);
        data.insert(QStringLiteral("moved"), false);
        data.insert(QStringLiteral("replaced"), false);
        return tx.finish(data,
                         QStringLiteral("pvf: move whole-frame keyframe \"%1\"")
                             .arg(entity->name));
    }

    Result startConflict = solverStartConflict(*entity, fromFrame);
    if (!startConflict.ok)
        return startConflict;
    if (const model::SolverSegment* segment = blockingSolver(*entity, toFrame))
    {
        QJsonObject details;
        details.insert(QStringLiteral("solverId"), segment->id);
        details.insert(QStringLiteral("startFrame"), segment->startFrame);
        details.insert(QStringLiteral("endFrame"), segment->endFrame);
        return failure(
            QStringLiteral("conflict"),
            QStringLiteral("目标帧 %1 由显式求解段驱动（%2..%3），不能写入普通"
                           "整帧关键帧")
                .arg(toFrame)
                .arg(segment->startFrame)
                .arg(segment->endFrame),
            details);
    }

    const bool replaced = model::hasKeyframeAt(entity->keyframes, toFrame);
    if (replaced && !overwrite)
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("目标帧 %1 已有关键帧").arg(toFrame));

    model::Keyframe movedKey = *source;
    movedKey.frame = toFrame;
    model::removeKeyframe(entity->keyframes, fromFrame);
    model::removeKeyframe(entity->keyframes, toFrame);
    model::upsertKeyframe(entity->keyframes, movedKey);
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("fromFrame"), fromFrame);
    data.insert(QStringLiteral("toFrame"), toFrame);
    data.insert(QStringLiteral("moved"), true);
    data.insert(QStringLiteral("replaced"), replaced);
    data.insert(QStringLiteral("keyframes"), model::toJson(entity->keyframes));
    return tx.finish(data,
                     QStringLiteral("pvf: move whole-frame keyframe \"%1\" %2->%3")
                         .arg(entity->name)
                         .arg(fromFrame)
                         .arg(toFrame));
}

Result keyframesClear(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (!entity->solvers.isEmpty())
    {
        QJsonArray startFrames;
        for (const model::SolverSegment& segment : entity->solvers)
            startFrames.append(segment.startFrame);
        QJsonObject details;
        details.insert(QStringLiteral("solverStartFrames"), startFrames);
        return failure(
            QStringLiteral("conflict"),
            QStringLiteral("实体有求解段，求解起点必须保留整帧关键帧；请先删除"
                           "求解段再清空"),
            details);
    }

    const int removed = entity->keyframes.size();
    if (removed > 0)
    {
        entity->keyframes.clear();
        tx.touch(uuid);
    }

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("removed"), removed);
    return tx.finish(data,
                     QStringLiteral("pvf: clear whole-frame keyframes \"%1\"")
                         .arg(entity->name));
}

Result keyframesSetEasing(Tx& tx, const QJsonObject& args)
{
    Result error;
    if (Result track = rejectTrackArg(args); !track.ok)
        return track;
    Uuid uuid;
    if (!readUuid(args, "uuid", &uuid, &error, true))
        return error;
    int frame = 0;
    if (!readInt(args, "frame", &frame, &error, true))
        return error;

    model::Entity* entity = writableEntity(tx, uuid, &error);
    if (!entity)
        return error;
    if (Result range = checkFrameValue(frame, entity->duration()); !range.ok)
        return range;

    model::Keyframe* key = const_cast<model::Keyframe*>(
        model::keyframeAt(entity->keyframes, frame));
    if (!key)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("第 %1 帧没有关键帧").arg(frame));

    model::EasingSpec easing;
    QJsonValue normalized;
    if (!parseEasing(args.value(QStringLiteral("easing")), &easing, &normalized,
                     &error))
        return error;

    key->easing = easing;
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("uuid"), uuid);
    data.insert(QStringLiteral("frame"), frame);
    data.insert(QStringLiteral("easing"), normalized);
    return tx.finish(data,
                     QStringLiteral("pvf: set whole-frame easing \"%1\" @%2")
                         .arg(entity->name)
                         .arg(frame));
}

} // namespace pvf::mcp2::detail
