#include "mcp2/EntityToolsInternal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::mcp2::detail::entity {

namespace {

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

double trackValueAt(const model::Entity& entity, const QString& track, int frame,
                    double fallback)
{
    const auto it = entity.tracks.constFind(track);
    if (it == entity.tracks.constEnd())
        return fallback;
    const auto value = model::evaluateTrack(it.value(), frame);
    return value ? *value : fallback;
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

        Result fieldCheck = validateContentFieldsJson(update, kindName);
        if (!fieldCheck.ok)
            return fieldCheck;

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
        else if (const auto special = specialContentKindFromName(kindName))
            target = *special;
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
            // empty 可以和任意非素材内容互转；text/vector 这组兼容规则不变。
            const bool allowedChange =
                (current == ContentKind::Empty)
                || (isTextual(current) && isTextual(target))
                || (target == ContentKind::Empty);
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
                // Content structs are nested sub-objects (text/vector/... and the
                // special kinds).  Merge a partial patch into the current value
                // instead of replacing the whole struct.
                if (it.value().isObject())
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

} // namespace pvf::mcp2::detail::entity

namespace pvf::mcp2::detail {

using namespace entity;

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
        if (stopRequested(tx))
            return stoppedByHuman();
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
    if (hasStart && hasDelta)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("start 与 delta 只能给一个"));
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

    QSet<Uuid> moved{uuid};
    for (const Uuid& descendant : subtree(tx.p(), uuid))
        moved.insert(descendant);
    const QJsonArray dependencies = dependentSolvers(tx.p(), moved);
    if (!dependencies.isEmpty() && !confirmed(args))
    {
        QJsonObject affectedObject;
        affectedObject.insert(QStringLiteral("solverDependencies"),
                              dependencies);
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affectedObject);
        details.insert(QStringLiteral("hint"),
                       QStringLiteral("先改相关求解段的 targetUuid，或带 confirm:true 继续"));
        return failure(QStringLiteral("confirm_required"),
                       QStringLiteral("换父级会影响引用该实体的求解段，需要 confirm"),
                       details);
    }

    if (mode == QLatin1String("preserveWorld"))
    {
        static const QStringList transformTracks = {
            QStringLiteral("x"),        QStringLiteral("y"),
            QStringLiteral("rotation"), QStringLiteral("scaleX"),
            QStringLiteral("scaleY"),   QStringLiteral("pivotX"),
            QStringLiteral("pivotY"),
        };
        QStringList animatedTracks;
        for (const QString& track : transformTracks)
        {
            const auto it = entity->tracks.constFind(track);
            if (it != entity->tracks.constEnd() && !it.value().keys.isEmpty())
                animatedTracks << track;
        }
        if (!animatedTracks.isEmpty())
        {
            QJsonObject details;
            details.insert(QStringLiteral("tracks"),
                           QJsonArray::fromStringList(animatedTracks));
            details.insert(
                QStringLiteral("hint"),
                QStringLiteral("preserveWorld 只改静态 transform，会与关键帧冲突；"
                               "请先移除这些关键帧，或改用 keepLocal"));
            return failure(
                QStringLiteral("conflict"),
                QStringLiteral("实体有 transform 关键帧，不能用 preserveWorld 换父级"),
                details);
        }
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

} // namespace pvf::mcp2::detail
