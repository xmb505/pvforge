// Root-subtree and EntitySpace tests live in their own translation unit so the
// historical model_tests.cpp file-size baseline does not force the new cases
// into an already full file.
#include "model/Model.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

using namespace pvf;
using namespace pvf::model;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const QString& detail,
           const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    qCritical().noquote()
        << QStringLiteral("FAIL %1:%2  %3  %4")
               .arg(QString::fromLatin1(file))
               .arg(line)
               .arg(QString::fromLatin1(expression), detail);
}

#define CHECK(condition) check((condition), #condition, QString(), __FILE__, __LINE__)
#define CHECK_DETAIL(condition, detail) \
    check((condition), #condition, (detail), __FILE__, __LINE__)

Entity makeEntity(int start = 0, int end = 10)
{
    Entity entity;
    entity.uuid = newUuid();
    entity.name = QStringLiteral("实体");
    entity.start = start;
    entity.end = end;
    return entity;
}

EntityTreeNode node(Entity entity, QVector<EntityTreeNode> children = {})
{
    EntityTreeNode result;
    result.entity = std::move(entity);
    result.children = std::move(children);
    return result;
}

void testSpaceAndFollowJsonRoundTrip()
{
    Entity entity = makeEntity(0, 30);
    entity.space = EntitySpace::Canvas;
    entity.followParentRange = false;

    const QJsonObject json = toJson(entity);
    CHECK(json.value(QStringLiteral("space")).toString()
          == QStringLiteral("canvas"));
    CHECK(json.value(QStringLiteral("followParentRange")).isBool());
    CHECK(!json.value(QStringLiteral("followParentRange")).toBool());

    QString error;
    const auto parsed = entityFromJson(json, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
    {
        CHECK(parsed->space == EntitySpace::Canvas);
        CHECK(!parsed->followParentRange);
        CHECK(toJson(*parsed) == json);
    }

    // Parent is the serialized default and reads back unchanged.
    Entity plain = makeEntity();
    const QJsonObject plainJson = toJson(plain);
    CHECK(plainJson.value(QStringLiteral("space")).toString()
          == QStringLiteral("parent"));
    const auto plainParsed = entityFromJson(plainJson, &error);
    CHECK_DETAIL(plainParsed.has_value(), error);
    if (plainParsed)
        CHECK(plainParsed->space == EntitySpace::Parent);

    // Unknown space names are a hard error, not a silent parent fallback.
    QJsonObject bad = plainJson;
    bad.insert(QStringLiteral("space"), QStringLiteral("world"));
    error.clear();
    CHECK(!entityFromJson(bad, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("space")), error);
}

void testNestedTreeJsonRoundTrip()
{
    Entity root = makeEntity(0, 100);
    root.name = QStringLiteral("根");

    Entity child = makeEntity(10, 50);
    child.name = QStringLiteral("子");
    child.parentUuid = root.uuid;
    child.space = EntitySpace::Canvas;

    Entity grandchild = makeEntity(10, 50);
    grandchild.name = QStringLiteral("孙");
    grandchild.parentUuid = child.uuid;
    grandchild.followParentRange = true;
    grandchild.space = EntitySpace::Canvas;

    const EntityTreeNode tree =
        node(root, {node(child, {node(grandchild)})});

    const QJsonObject json = toJson(tree);
    const QJsonArray children = json.value(QStringLiteral("children")).toArray();
    CHECK(children.size() == 1);
    const QJsonObject childJson = children.at(0).toObject();
    CHECK(childJson.value(QStringLiteral("parentUuid")).toString() == root.uuid);
    CHECK(childJson.value(QStringLiteral("space")).toString()
          == QStringLiteral("canvas"));
    CHECK(childJson.value(QStringLiteral("children")).toArray().size() == 1);
    CHECK(childJson.value(QStringLiteral("children"))
              .toArray()
              .at(0)
              .toObject()
              .value(QStringLiteral("followParentRange"))
              .toBool());

    QString error;
    const auto parsed = entityTreeFromJson(json, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (!parsed)
        return;
    CHECK(toJson(*parsed) == json);

    QHash<Uuid, Entity> flat;
    CHECK_DETAIL(flattenEntityTree(*parsed, &flat, &error), error);
    CHECK(flat.size() == 3);
    CHECK(flat.value(child.uuid).space == EntitySpace::Canvas);
    CHECK(flat.value(grandchild.uuid).followParentRange);
    CHECK(flat.value(grandchild.uuid).parentUuid == child.uuid);

    // A subtree is also valid when the leaf omits "children" (lenient reader,
    // same document shape older fixtures use).
    QJsonObject withoutChildren = json;
    withoutChildren.remove(QStringLiteral("children"));
    const auto leaf = entityTreeFromJson(withoutChildren, &error);
    CHECK_DETAIL(leaf.has_value(), error);
    if (leaf)
    {
        QHash<Uuid, Entity> leafFlat;
        CHECK_DETAIL(flattenEntityTree(*leaf, &leafFlat, &error), error);
        CHECK(leafFlat.size() == 1);
    }
}

void testTreeRejectsInconsistentNesting()
{
    Entity root = makeEntity(0, 100);
    Entity child = makeEntity(20, 60);
    child.parentUuid = root.uuid;

    QString error;

    // parentUuid disagrees with the enclosing children array.
    EntityTreeNode tree = node(root, {node(child)});
    tree.children[0].entity.parentUuid = newUuid();
    QHash<Uuid, Entity> flat;
    error.clear();
    CHECK(!flattenEntityTree(tree, &flat, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("parentUuid")), error);
    CHECK_DETAIL(error.contains(child.uuid), error);
    CHECK_DETAIL(error.contains(QStringLiteral("children[0]")), error);

    // Child range outside the direct parent.
    tree = node(root, {node(child)});
    tree.children[0].entity.start = 101;
    tree.children[0].entity.end = 110;
    flat.clear();
    error.clear();
    CHECK(!flattenEntityTree(tree, &flat, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("超出父实体")), error);
    CHECK_DETAIL(error.contains(child.uuid), error);

    // Canvas space still obeys the parent range constraint.
    tree.children[0].entity.space = EntitySpace::Canvas;
    flat.clear();
    error.clear();
    CHECK(!flattenEntityTree(tree, &flat, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("超出父实体")), error);

    // followParentRange=true requires equality with the direct parent.
    tree = node(root, {node(child)});
    tree.children[0].entity.followParentRange = true;
    flat.clear();
    error.clear();
    CHECK(!flattenEntityTree(tree, &flat, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("followParentRange")), error);
    if (tree.children.size() == 1)
    {
        tree.children[0].entity.start = root.start;
        tree.children[0].entity.end = root.end;
        flat.clear();
        error.clear();
        CHECK_DETAIL(flattenEntityTree(tree, &flat, &error), error);

        // But the root itself may never follow a parent.
        tree.entity.followParentRange = true;
        flat.clear();
        error.clear();
        CHECK(!flattenEntityTree(tree, &flat, &error));
        CHECK_DETAIL(error.contains(QStringLiteral("根实体")), error);
    }

    // Duplicate UUID in one tree.
    Entity duplicate = makeEntity(10, 20);
    duplicate.parentUuid = root.uuid;
    duplicate.uuid = root.uuid;
    tree = node(root, {node(duplicate)});
    flat.clear();
    error.clear();
    CHECK(!flattenEntityTree(tree, &flat, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("重复")), error);
    CHECK_DETAIL(error.contains(root.uuid), error);
}

void testProjectFollowValidation()
{
    Project project;
    Entity parent = makeEntity(0, 100);
    Entity child = makeEntity(0, 100);
    child.parentUuid = parent.uuid;
    child.followParentRange = true;
    project.entities.insert(parent.uuid, parent);
    project.entities.insert(child.uuid, child);

    QString error;
    CHECK_DETAIL(validateProject(project, &error), error);

    project.entities[child.uuid].end = 90;
    error.clear();
    CHECK(!validateProject(project, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("followParentRange")), error);
    CHECK_DETAIL(error.contains(child.uuid), error);

    project.entities[child.uuid].end = 100;
    project.entities[child.uuid].followParentRange = false;
    project.entities[parent.uuid].followParentRange = true;
    error.clear();
    CHECK(!validateProject(project, &error));
    CHECK_DETAIL(error.contains(QStringLiteral("根实体")), error);
}

} // namespace

void runEntityTreeTests(int* checks, int* failures)
{
    gChecks = 0;
    gFailures = 0;

    testSpaceAndFollowJsonRoundTrip();
    testNestedTreeJsonRoundTrip();
    testTreeRejectsInconsistentNesting();
    testProjectFollowValidation();

    if (checks)
        *checks += gChecks;
    if (failures)
        *failures += gFailures;
}
