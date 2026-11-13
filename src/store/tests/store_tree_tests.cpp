// v3 root-subtree storage tests. Kept out of store_tests.cpp so that file's
// historical size baseline is not exceeded by the new cases.
#include "store/Store.h"

#include "store_tests.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::store;

namespace {

QString projectPath(QTemporaryDir& temp)
{
    return temp.filePath(QStringLiteral("Demo.pvproj"));
}

model::Entity makeEntity(const QString& name, int start, int end)
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    return entity;
}

model::EntityTreeNode node(model::Entity entity,
                           QVector<model::EntityTreeNode> children = {})
{
    model::EntityTreeNode result;
    result.entity = std::move(entity);
    result.children = std::move(children);
    return result;
}

bool writeText(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size();
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString entityFile(const QString& root, const Uuid& uuid)
{
    return root + QStringLiteral("/entities/") + uuid + QStringLiteral(".json");
}

bool writeTree(const QString& root, const model::EntityTreeNode& tree)
{
    return writeText(entityFile(root, tree.entity.uuid),
                     QString::fromUtf8(QJsonDocument(model::toJson(tree))
                                           .toJson(QJsonDocument::Indented)));
}

std::optional<ProjectStore> createProject(QTemporaryDir& temp, QString* error)
{
    return ProjectStore::create(projectPath(temp), model::ProjectSettings(),
                                error, false);
}

} // namespace

void StoreTests::nestedEntityTreeRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto store = createProject(temp, &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity root = makeEntity(QStringLiteral("根"), 0, 100);
    model::Entity child = makeEntity(QStringLiteral("子"), 10, 50);
    child.parentUuid = root.uuid;
    child.space = model::EntitySpace::Canvas;
    model::Entity grandchild = makeEntity(QStringLiteral("孙"), 10, 50);
    grandchild.parentUuid = child.uuid;
    grandchild.space = model::EntitySpace::Canvas;
    grandchild.followParentRange = true;

    store->mutableProject().entities.insert(root.uuid, root);
    store->mutableProject().entities.insert(child.uuid, child);
    store->mutableProject().entities.insert(grandchild.uuid, grandchild);

    // Saving a leaf must write its root file, not a file named after the leaf.
    QVERIFY2(store->saveEntity(grandchild.uuid, &error), qPrintable(error));
    const QString rootFile = entityFile(projectPath(temp), root.uuid);
    QVERIFY(QFileInfo::exists(rootFile));
    QVERIFY(!QFileInfo::exists(entityFile(projectPath(temp), child.uuid)));
    QVERIFY(!QFileInfo::exists(entityFile(projectPath(temp), grandchild.uuid)));

    const QJsonObject rootJson =
        QJsonDocument::fromJson(readText(rootFile).toUtf8()).object();
    QCOMPARE(rootJson.value(QStringLiteral("version")).toInt(),
             model::kProjectFormatVersion);
    const QJsonArray children = rootJson.value(QStringLiteral("children")).toArray();
    QCOMPARE(children.size(), 1);
    const QJsonObject childJson = children.at(0).toObject();
    QCOMPARE(childJson.value(QStringLiteral("parentUuid")).toString(), root.uuid);
    QCOMPARE(childJson.value(QStringLiteral("space")).toString(),
             QStringLiteral("canvas"));
    const QJsonArray grandchildren =
        childJson.value(QStringLiteral("children")).toArray();
    QCOMPARE(grandchildren.size(), 1);
    QVERIFY(grandchildren.at(0)
                .toObject()
                .value(QStringLiteral("followParentRange"))
                .toBool());

    auto opened = ProjectStore::open(projectPath(temp), &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QCOMPARE(opened->project().entities.size(), 3);
    QCOMPARE(opened->project().entities.value(child.uuid).space,
             model::EntitySpace::Canvas);
    QVERIFY(opened->project().entities.value(grandchild.uuid).followParentRange);
    QCOMPARE(opened->project().entities.value(grandchild.uuid).parentUuid,
             child.uuid);

    // Reparenting a subtree to root must rewrite the old root file as well, or
    // the same UUID would live in two files.
    opened->mutableProject().entities[child.uuid].parentUuid.clear();
    QVERIFY2(opened->saveEntity(child.uuid, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(entityFile(projectPath(temp), child.uuid)));

    const QJsonObject oldRootJson =
        QJsonDocument::fromJson(readText(rootFile).toUtf8()).object();
    QCOMPARE(oldRootJson.value(QStringLiteral("children")).toArray().size(), 0);
    const QJsonObject newRootJson =
        QJsonDocument::fromJson(
            readText(entityFile(projectPath(temp), child.uuid)).toUtf8())
            .object();
    QCOMPARE(newRootJson.value(QStringLiteral("children")).toArray().size(), 1);

    auto reopened = ProjectStore::open(projectPath(temp), &error, false);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->project().entities.size(), 3);
    QVERIFY(reopened->project().entities.value(child.uuid).parentUuid.isEmpty());
    QCOMPARE(reopened->project().entities.value(grandchild.uuid).parentUuid,
             child.uuid);
}

void StoreTests::entityTreeRejectsInconsistentNesting()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto store = createProject(temp, &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    const QString rootPath = projectPath(temp);

    model::Entity root = makeEntity(QStringLiteral("根"), 0, 100);
    model::Entity child = makeEntity(QStringLiteral("子"), 20, 60);
    child.parentUuid = root.uuid;

    // Nested parentUuid must equal the enclosing node.
    model::EntityTreeNode tree = node(root, {node(child)});
    tree.children[0].entity.parentUuid = model::newUuid();
    QVERIFY(writeTree(rootPath, tree));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(child.uuid), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("parentUuid")), qPrintable(error));

    // A child range outside the parent is refused.
    tree.children[0].entity.parentUuid = root.uuid;
    tree.children[0].entity.start = 101;
    tree.children[0].entity.end = 110;
    QVERIFY(writeTree(rootPath, tree));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(child.uuid), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("超出父实体")), qPrintable(error));

    // Canvas space does not relax the timeline constraint.
    tree.children[0].entity.space = model::EntitySpace::Canvas;
    QVERIFY(writeTree(rootPath, tree));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(QStringLiteral("超出父实体")), qPrintable(error));

    // followParentRange=true requires range equality with the direct parent.
    tree.children[0].entity.start = 20;
    tree.children[0].entity.end = 60;
    tree.children[0].entity.followParentRange = true;
    QVERIFY(writeTree(rootPath, tree));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(QStringLiteral("followParentRange")),
             qPrintable(error));

    // A root can never follow a parent.
    tree.children[0].entity.followParentRange = false;
    tree.entity.followParentRange = true;
    QVERIFY(writeTree(rootPath, tree));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(QStringLiteral("根实体")), qPrintable(error));
}

void StoreTests::entityTreeRejectsDuplicateUuid()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto store = createProject(temp, &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    const QString rootPath = projectPath(temp);

    model::Entity root = makeEntity(QStringLiteral("根"), 0, 100);
    model::Entity duplicate = makeEntity(QStringLiteral("重复"), 10, 20);
    duplicate.uuid = root.uuid;
    duplicate.parentUuid = root.uuid;

    QVERIFY(writeTree(rootPath, node(root, {node(duplicate)})));
    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(root.uuid), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("重复")), qPrintable(error));
}

void StoreTests::entityTreeRejectsRootFileNameMismatch()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto store = createProject(temp, &error);
    QVERIFY2(store.has_value(), qPrintable(error));
    const QString rootPath = projectPath(temp);

    model::Entity root = makeEntity(QStringLiteral("根"), 0, 100);
    const Uuid wrongFileName = model::newUuid();
    const QString file = entityFile(rootPath, wrongFileName);
    QVERIFY(writeText(file,
                      QString::fromUtf8(QJsonDocument(model::toJson(
                                                            node(root)))
                                            .toJson(QJsonDocument::Indented))));

    error.clear();
    QVERIFY(!ProjectStore::open(rootPath, &error, false));
    QVERIFY2(error.contains(root.uuid), qPrintable(error));
    QVERIFY2(error.contains(wrongFileName), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("文件名")), qPrintable(error));
}

void StoreTests::openRejectsLegacyFlatFormat()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // v2 was the flat one-entity-per-file layout. It is intentionally not
    // imported: opening must fail readably and leave the bytes untouched.
    model::Project project;
    project.settings.name = QStringLiteral("旧工程");
    model::Entity entity = makeEntity(QStringLiteral("旧实体"), 0, 30);
    model::EntityTreeNode tree;
    tree.entity = entity;

    QJsonObject projectObject = model::toJson(project);
    projectObject.insert(QStringLiteral("version"),
                         model::kProjectFormatVersion - 1);
    QJsonObject entityObject = model::toJson(tree);
    entityObject.remove(QStringLiteral("children"));
    entityObject.insert(QStringLiteral("version"),
                        model::kProjectFormatVersion - 1);

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityPath = temp.filePath(
        QStringLiteral("entities/") + entity.uuid + QStringLiteral(".json"));
    const QString projectText =
        QString::fromUtf8(QJsonDocument(projectObject).toJson(QJsonDocument::Indented));
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));
    QVERIFY(writeText(projectFile, projectText));
    QVERIFY(writeText(entityPath, entityText));

    QString error;
    QVERIFY(!ProjectStore::open(temp.path(), &error, false));
    QVERIFY2(error.contains(QStringLiteral("过旧")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("不做导入")), qPrintable(error));
    QCOMPARE(readText(projectFile), projectText);
    QCOMPARE(readText(entityPath), entityText);
}
