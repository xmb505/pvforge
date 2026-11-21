#include "mcp2/EntityToolsInternal.h"
#include "mcp2/KeyframePayload.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace pvf::mcp2::detail::entity {

namespace {

const QStringList kCreateKeys = {
    QStringLiteral("name"),      QStringLiteral("parentUuid"),
    QStringLiteral("space"),     QStringLiteral("followParentRange"),
    QStringLiteral("start"),     QStringLiteral("end"),
    QStringLiteral("row"),       QStringLiteral("content"),
    QStringLiteral("transform"), QStringLiteral("hidden"),
    QStringLiteral("locked"),    QStringLiteral("inheritVisibility"),
    QStringLiteral("visible"),   QStringLiteral("physicsInteract"),
    QStringLiteral("keyframes"), QStringLiteral("solvers"),
};

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
            end = start + (frames - content.asset.sourceIn) - 1;
        }
        else
        {
            // One second: fps frames means end = start + fps - 1.
            end = start + std::max(1, project.settings.fps) - 1;
        }
    }
    if (end < start || end > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("end 必须不小于 start 且不超过 %1").arg(kMaxFrame));

    if (parent && (start < parent->start || end > parent->end))
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("实体范围 [%1,%2] 超出父实体范围 [%3,%4]")
                           .arg(start)
                           .arg(end)
                           .arg(parent->start)
                           .arg(parent->end));

    contentResult = checkAssetDuration(tx.store->assets(), content,
                                       end - start + 1);
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
        || !readBool(args, "physicsInteract", &entity.physicsInteract, &error)
        || !readBool(args, "followParentRange", &entity.followParentRange,
                     &error))
        return error;

    QString spaceName = QStringLiteral("parent");
    if (!readString(args, "space", &spaceName, &error))
        return error;
    const auto space = model::entitySpaceFromName(spaceName);
    if (!space)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("space 必须是 parent 或 canvas"));
    entity.space = *space;

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

    entity.keyframes.clear();
    Result keyframeResult = parseWholeFrameKeyframes(
        entity.content, entity.duration(),
        args.value(QStringLiteral("keyframes")), &entity.keyframes);
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

} // namespace

} // namespace pvf::mcp2::detail::entity

namespace pvf::mcp2::detail {

using namespace entity;

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
        if (stopRequested(tx))
            return stoppedByHuman();
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
        if (stopRequested(tx))
            return stoppedByHuman();
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

} // namespace pvf::mcp2::detail
