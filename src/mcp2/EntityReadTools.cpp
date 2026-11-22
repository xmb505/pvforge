#include "mcp2/EntityToolsInternal.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>

namespace pvf::mcp2::detail {

using namespace entity;

namespace {

// entity.list appendices shared by the flat and tree modes.
void appendEntityExtras(const model::Entity& entity, bool includeKeyframes,
                        bool includeSolvers, QJsonObject* object)
{
    if (includeKeyframes)
    {
        object->insert(QStringLiteral("keyframes"),
                       model::toJson(entity.keyframes));
    }
    if (includeSolvers)
    {
        QJsonArray solvers;
        for (const model::SolverSegment& segment : entity.solvers)
            solvers.append(model::toJson(segment));
        object->insert(QStringLiteral("solvers"), solvers);
    }
}

} // namespace

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

    int frame = tx.viewState.playhead;
    if (!readInt(args, "frame", &frame, &error))
        return error;
    if (frame < 0 || frame > kMaxFrame)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("frame 必须在 0..%1").arg(kMaxFrame));

    bool includeChildren = false;
    if (!readBool(args, "includeChildren", &includeChildren, &error))
        return error;

    const int relative = frame - entity->start;
    const model::SolverSegment* activeSolver = anim::solverAt(*entity, relative);

    // Evaluate through anim so solver segments (gravity/orbit/spin/blackhole)
    // are included. The pose is world-space; invert the parent transform to
    // report the entity-local values the rest of the API uses.
    render2::SceneWorld world(tx.p(), tx.store->assets());
    anim::Cache cache;
    const anim::Pose pose =
        anim::evaluateEntity(tx.p(), uuid, frame, world, cache);

    QTransform parentWorld;
    if (!entity->parentUuid.isEmpty()
        && findEntity(tx.p(), entity->parentUuid))
    {
        parentWorld =
            anim::evaluateEntity(tx.p(), entity->parentUuid, frame, world, cache)
                .world;
    }
    bool invertible = false;
    const QTransform inverse = parentWorld.inverted(&invertible);
    const QTransform local =
        invertible ? pose.world * inverse : pose.world;

    const double a00 = local.m11();
    const double a01 = local.m21();
    const double a10 = local.m12();
    const double a11 = local.m22();
    const double determinant = a00 * a11 - a01 * a10;
    const double scaleX = std::hypot(a00, a10);
    const double scaleY =
        scaleX > 1e-12 ? determinant / scaleX : std::hypot(a01, a11);
    const double rotation = std::atan2(a10, a00) * 180.0 / kPi;

    const model::KeyframeSample sample = model::entitySampleAt(*entity, relative);
    const double pivotX = entity->transform.pivotX;
    const double pivotY = entity->transform.pivotY;

    QJsonObject evaluated;
    evaluated.insert(
        QStringLiteral("x"),
        local.dx() - pivotX + (a00 * pivotX + a01 * pivotY));
    evaluated.insert(
        QStringLiteral("y"),
        local.dy() - pivotY + (a10 * pivotX + a11 * pivotY));
    evaluated.insert(QStringLiteral("rotation"), rotation);
    evaluated.insert(QStringLiteral("scaleX"), scaleX);
    evaluated.insert(QStringLiteral("scaleY"), scaleY);
    evaluated.insert(QStringLiteral("opacity"), pose.opacity);
    evaluated.insert(QStringLiteral("visible"), pose.visible);
    if (entity->content.kind == ContentKind::Asset
        && entity->content.asset.type == AssetType::Audio)
        evaluated.insert(QStringLiteral("volume"),
                         sample.content.value(QStringLiteral("volume"))
                             .toDouble(entity->content.asset.volume));

    QJsonObject data = model::toJson(*entity);
    data.insert(QStringLiteral("duration"), entity->duration());
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
                        assetJson(tx.p(), *asset));
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
    bool tree = false;
    if (!readBool(args, "includeKeyframes", &includeKeyframes, &error)
        || !readBool(args, "includeSolvers", &includeSolvers, &error)
        || !readBool(args, "tree", &tree, &error))
        return error;

    if (tree)
    {
        const model::ChildrenIndex index(tx.p());
        QSet<Uuid> visited;
        std::function<QJsonObject(const Uuid&)> build =
            [&](const Uuid& uuid) -> QJsonObject {
            visited.insert(uuid);
            const model::Entity* entity = findEntity(tx.p(), uuid);
            QJsonObject object = entityBrief(*entity);
            // "kind" is the content kind name; entityBrief already exposes it
            // as contentKind, the tree contract names it kind.
            object.insert(QStringLiteral("kind"),
                          contentKindName(entity->content.kind));
            appendEntityExtras(*entity, includeKeyframes, includeSolvers,
                               &object);

            QJsonArray children;
            for (const Uuid& childUuid : index.children(uuid))
            {
                // Dirty data may contain a cycle; never recurse into a node
                // that is already on the current path or already emitted.
                if (visited.contains(childUuid))
                    continue;
                children.append(build(childUuid));
            }
            object.insert(QStringLiteral("children"), children);
            return object;
        };

        // An empty parentUuid selects every top-level entity, a concrete one
        // starts from that parent's direct children.
        const QVector<Uuid> roots = index.children(parentUuid);
        QJsonArray array;
        for (const Uuid& rootUuid : roots)
        {
            if (visited.contains(rootUuid))
                continue;
            array.append(build(rootUuid));
        }

        QJsonObject data;
        data.insert(QStringLiteral("entities"), array);
        data.insert(QStringLiteral("page"),
                    pageJson(0, std::max(1, int(roots.size())),
                             int(roots.size())));
        return success(data);
    }

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
        appendEntityExtras(entities.at(i), includeKeyframes, includeSolvers,
                           &object);
        array.append(object);
    }

    QJsonObject data;
    data.insert(QStringLiteral("entities"), array);
    data.insert(QStringLiteral("page"), pageJson(offset, limit, entities.size()));
    return success(data);
}

} // namespace pvf::mcp2::detail
