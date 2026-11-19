// Persistence safety regressions (issues #113 #114 #116 #117). Kept in a
// separate binary so the historical store_tests.cpp stays under its size
// ratchet.
#include "store/Store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::store;

namespace {

bool gitAvailable()
{
    static const bool available = [] {
        QProcess process;
        process.start(QStringLiteral("git"), {QStringLiteral("--version")});
        if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
            return false;
        return process.exitStatus() == QProcess::NormalExit
               && process.exitCode() == 0;
    }();
    return available;
}

struct PathGuard
{
    PathGuard() : oldPath(qgetenv("PATH")) {}
    ~PathGuard() { qputenv("PATH", oldPath); }
    QByteArray oldPath;
};

#define SKIP_WITHOUT_GIT()                                                    \
    do                                                                        \
    {                                                                         \
        if (!gitAvailable())                                                  \
            QSKIP("git 不可用，跳过依赖 git 的测试");                         \
    } while (false)

QString projectPath(QTemporaryDir& temp)
{
    return temp.filePath(QStringLiteral("Demo.pvproj"));
}

model::Entity makeEntity(const QString& name = QStringLiteral("实体"),
                         int start = 0, int end = 30)
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    return entity;
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

} // namespace

class StoreSafetyTests : public QObject
{
    Q_OBJECT

private slots:
    void corruptEntityJson();
    void openRejectsFutureEntityVersion();
    void openRejectsStaleEntityVersion();
    void openStrictRequiredFields();
    void crossRootWriteRollsBackOnFailure();
    void killBetweenWritesRecoversOnOpen();
    void duplicateUuidAcrossFilesIsQuarantined();
    void lockBlocksSecondInstance();
    void externalChangeRefusesOverwrite();
    void readFailureIsNotMissing();
    void assetTrashIsUndoable();
    void failedCreateRemovesPartialProject();
};

void StoreSafetyTests::corruptEntityJson()
{
    SKIP_WITHOUT_GIT();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString uuid = model::newUuid();
    const QString entityFile =
        path + QStringLiteral("/entities/") + uuid + QStringLiteral(".json");

    // A corrupt entity file is isolated instead of making the whole project
    // unopenable: the file is moved to .pvforge/quarantine and the project
    // opens read-only so nothing can overwrite the missing entity.
    QVERIFY(writeText(entityFile, QStringLiteral("{ broken")));
    error.clear();
    auto broken = ProjectStore::open(path, &error);
    QVERIFY2(broken.has_value(), qPrintable(error));
    QVERIFY(broken->readOnly());
    QVERIFY(!broken->readOnlyReason().isEmpty());
    QCOMPARE(broken->project().entities.size(), 0);
    QCOMPARE(broken->quarantinedFiles().size(), 1);
    QVERIFY(!QFileInfo::exists(entityFile));
    QVERIFY(QFileInfo::exists(broken->quarantinedFiles().first().path));
    QVERIFY2(broken->quarantinedFiles().first().reason.contains(uuid),
             qPrintable(broken->quarantinedFiles().first().reason));

    // Writes are refused while the project is read-only.
    model::Entity blocked = makeEntity(QStringLiteral("不可写"));
    broken->mutableProject().entities.insert(blocked.uuid, blocked);
    error.clear();
    QVERIFY(!broken->saveEntity(blocked.uuid, &error));
    QVERIFY2(error.contains(QStringLiteral("只读")), qPrintable(error));
    broken.reset();

    // A file whose root UUID does not match its name is quarantined too.
    model::Entity other = makeEntity();
    QVERIFY(writeText(entityFile,
                      QString::fromUtf8(
                          QJsonDocument(model::toJson(other)).toJson())));
    error.clear();
    auto mismatch = ProjectStore::open(path, &error);
    QVERIFY2(mismatch.has_value(), qPrintable(error));
    QVERIFY(mismatch->readOnly());
    QCOMPARE(mismatch->quarantinedFiles().size(), 1);
    QVERIFY2(mismatch->quarantinedFiles().first().reason.contains(
                 QStringLiteral("不一致")),
             qPrintable(mismatch->quarantinedFiles().first().reason));
    mismatch.reset();

    // A missing required field is also isolated, not fatal.
    QVERIFY(writeText(entityFile, QStringLiteral("{}")));
    error.clear();
    auto missing = ProjectStore::open(path, &error);
    QVERIFY2(missing.has_value(), qPrintable(error));
    QVERIFY(missing->readOnly());
    QCOMPARE(missing->quarantinedFiles().size(), 1);
    QVERIFY(!missing->quarantinedFiles().first().reason.isEmpty());
}

void StoreSafetyTests::openRejectsFutureEntityVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Entity entity = makeEntity(QStringLiteral("未来实体"));
    QJsonObject entityObject = model::toJson(entity);
    entityObject.insert(QStringLiteral("version"),
                        model::kProjectFormatVersion + 1);
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    QVERIFY(writeText(
        projectFile,
        QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                              .toJson(QJsonDocument::Indented))));
    QVERIFY(writeText(entityFile, entityText));

    // The newer-version file must not be rewritten or lost; it is moved to the
    // quarantine directory and the project opens read-only.
    QString error;
    auto opened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(opened->readOnly());
    QCOMPARE(opened->quarantinedFiles().size(), 1);
    const QuarantinedFile quarantined = opened->quarantinedFiles().first();
    QVERIFY2(quarantined.reason.contains(entity.uuid), qPrintable(quarantined.reason));
    QVERIFY2(quarantined.reason.contains(QStringLiteral("实体版本")),
             qPrintable(quarantined.reason));
    QVERIFY2(quarantined.reason.contains(
                 QString::number(model::kProjectFormatVersion + 1)),
             qPrintable(quarantined.reason));
    QVERIFY2(quarantined.reason.contains(QStringLiteral("不会被修改")),
             qPrintable(quarantined.reason));
    QCOMPARE(readText(quarantined.path), entityText);
    QVERIFY(!QFileInfo::exists(entityFile));
}

void StoreSafetyTests::openRejectsStaleEntityVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    model::Entity entity = makeEntity(QStringLiteral("过旧实体"));
    QJsonObject entityObject = model::toJson(entity);
    entityObject.insert(QStringLiteral("version"), 0);
    const QString entityText =
        QString::fromUtf8(QJsonDocument(entityObject).toJson(QJsonDocument::Indented));

    const QString projectFile = temp.filePath(QStringLiteral("project.json"));
    const QString entityFile = temp.filePath(QStringLiteral("entities/")
                                             + entity.uuid
                                             + QStringLiteral(".json"));
    QVERIFY(writeText(
        projectFile,
        QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                              .toJson(QJsonDocument::Indented))));
    QVERIFY(writeText(entityFile, entityText));

    QString error;
    auto opened = ProjectStore::open(temp.path(), &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
    QVERIFY(opened->readOnly());
    QCOMPARE(opened->quarantinedFiles().size(), 1);
    const QuarantinedFile quarantined = opened->quarantinedFiles().first();
    QVERIFY2(quarantined.reason.contains(entity.uuid), qPrintable(quarantined.reason));
    QVERIFY2(quarantined.reason.contains(QStringLiteral("过旧")),
             qPrintable(quarantined.reason));
    QVERIFY2(quarantined.reason.contains(QStringLiteral("最低支持版本")),
             qPrintable(quarantined.reason));
    QCOMPARE(readText(quarantined.path), entityText);
}

void StoreSafetyTests::openStrictRequiredFields()
{
    // project.json: a required settings field is gone. Without a project there
    // is nothing to open read-only, so this stays fatal.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QJsonObject object = model::toJson(model::Project{});
        object.remove(QStringLiteral("fps"));
        QVERIFY(writeText(
            temp.filePath(QStringLiteral("project.json")),
            QString::fromUtf8(
                QJsonDocument(object).toJson(QJsonDocument::Indented))));

        QString error;
        QVERIFY(!ProjectStore::open(temp.path(), &error, false));
        QVERIFY2(error.contains(QStringLiteral("fps")), qPrintable(error));
    }

    // Entity file: the reason names the missing field; the project still opens
    // read-only with the file quarantined.
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(writeText(
            temp.filePath(QStringLiteral("project.json")),
            QString::fromUtf8(QJsonDocument(model::toJson(model::Project{}))
                                  .toJson(QJsonDocument::Indented))));

        model::Entity entity = makeEntity();
        QJsonObject entityObject = model::toJson(entity);
        entityObject.remove(QStringLiteral("start"));
        const QString entityFile = temp.filePath(
            QStringLiteral("entities/") + entity.uuid + QStringLiteral(".json"));
        QVERIFY(writeText(
            entityFile,
            QString::fromUtf8(
                QJsonDocument(entityObject).toJson(QJsonDocument::Indented))));

        QString error;
        auto opened = ProjectStore::open(temp.path(), &error, false);
        QVERIFY2(opened.has_value(), qPrintable(error));
        QVERIFY(opened->readOnly());
        QCOMPARE(opened->quarantinedFiles().size(), 1);
        const QString reason = opened->quarantinedFiles().first().reason;
        QVERIFY2(reason.contains(QStringLiteral("start")), qPrintable(reason));
        QVERIFY2(reason.contains(entity.uuid), qPrintable(reason));
        QVERIFY(!QFileInfo::exists(entityFile));
    }
}

void StoreSafetyTests::crossRootWriteRollsBackOnFailure()
{
    // A reparent rewrites two files. If the second staged write fails, the
    // first target must not be touched: no UUID may live in two files.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity first = makeEntity(QStringLiteral("一"), 0, 100);
    model::Entity second = makeEntity(QStringLiteral("二"), 0, 100);
    second.row = 1;
    model::Entity child = makeEntity(QStringLiteral("子"), 10, 20);
    child.parentUuid = first.uuid;
    store->mutableProject().entities.insert(first.uuid, first);
    store->mutableProject().entities.insert(second.uuid, second);
    store->mutableProject().entities.insert(child.uuid, child);
    QVERIFY2(store->saveEntity(first.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(child.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(second.uuid, &error), qPrintable(error));

    const QString firstPath =
        path + QStringLiteral("/entities/") + first.uuid + QStringLiteral(".json");
    const QString secondPath =
        path + QStringLiteral("/entities/") + second.uuid + QStringLiteral(".json");
    const QByteArray firstBytes = readText(firstPath).toUtf8();
    const QByteArray secondBytes = readText(secondPath).toUtf8();

    store->mutableProject().entities[child.uuid].parentUuid = second.uuid;
    store->setWriteFailureForTest(1); // fail while staging the old root file
    error.clear();
    QVERIFY(!store->saveEntity(child.uuid, &error));
    QVERIFY2(error.contains(QStringLiteral("模拟写入失败")), qPrintable(error));
    QCOMPARE(readText(firstPath).toUtf8(), firstBytes);
    QCOMPARE(readText(secondPath).toUtf8(), secondBytes);

    // The transaction did not half-apply: the next attempt succeeds and the
    // old root no longer contains the moved subtree.
    error.clear();
    QVERIFY2(store->saveEntity(child.uuid, &error), qPrintable(error));
    QVERIFY(!readText(firstPath).contains(child.uuid));
    QVERIFY(readText(secondPath).contains(child.uuid));

    store.reset();
    error.clear();
    auto reopened = ProjectStore::open(path, &error, false);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QCOMPARE(reopened->project().entities.value(child.uuid).parentUuid,
             second.uuid);
}

void StoreSafetyTests::killBetweenWritesRecoversOnOpen()
{
    // Simulate the process dying after the new root file landed but before the
    // old root file was rewritten. open() must finish the journal, so the
    // project loads without duplicate UUIDs.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity first = makeEntity(QStringLiteral("一"), 0, 100);
    model::Entity second = makeEntity(QStringLiteral("二"), 0, 100);
    second.row = 1;
    model::Entity child = makeEntity(QStringLiteral("子"), 10, 20);
    child.parentUuid = first.uuid;
    store->mutableProject().entities.insert(first.uuid, first);
    store->mutableProject().entities.insert(second.uuid, second);
    store->mutableProject().entities.insert(child.uuid, child);
    QVERIFY2(store->saveEntity(first.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(child.uuid, &error), qPrintable(error));
    QVERIFY2(store->saveEntity(second.uuid, &error), qPrintable(error));

    store->mutableProject().entities[child.uuid].parentUuid = second.uuid;
    store->setCrashAfterApplyForTest(1);
    error.clear();
    QVERIFY(!store->saveEntity(child.uuid, &error));
    QVERIFY2(error.contains(QStringLiteral("模拟进程")), qPrintable(error));

    // The half-applied state is visible on disk (same UUID in both files), so
    // only the journal can recover it.
    const QString firstPath =
        path + QStringLiteral("/entities/") + first.uuid + QStringLiteral(".json");
    const QString secondPath =
        path + QStringLiteral("/entities/") + second.uuid + QStringLiteral(".json");
    QVERIFY(readText(firstPath).contains(child.uuid));
    QVERIFY(readText(secondPath).contains(child.uuid));

    store.reset();
    error.clear();
    auto reopened = ProjectStore::open(path, &error, false);
    QVERIFY2(reopened.has_value(), qPrintable(error));
    QVERIFY(!reopened->readOnly());
    QCOMPARE(reopened->project().entities.size(), 3);
    QCOMPARE(reopened->project().entities.value(child.uuid).parentUuid,
             second.uuid);
    QVERIFY(!readText(firstPath).contains(child.uuid));
    const QStringList leftover = QDir(path + QStringLiteral("/.pvforge"))
                                     .entryList({QStringLiteral("txn-*")},
                                                QDir::Dirs);
    QVERIFY2(leftover.isEmpty(), qPrintable(leftover.join(',')));
}

void StoreSafetyTests::duplicateUuidAcrossFilesIsQuarantined()
{
    // Two root files claim the same child UUID. The second file is isolated and
    // the project opens read-only instead of being permanently unopenable.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("Duplicates.pvproj"));
    QVERIFY(QDir().mkpath(path + QStringLiteral("/entities")));
    QVERIFY(writeText(path + QStringLiteral("/project.json"),
                      QString::fromUtf8(QJsonDocument(model::toJson(
                                                            model::Project{}))
                                            .toJson(QJsonDocument::Indented))));

    model::Entity first = makeEntity(QStringLiteral("一"), 0, 100);
    model::Entity second = makeEntity(QStringLiteral("二"), 0, 100);
    model::Entity shared = makeEntity(QStringLiteral("共享"), 10, 20);
    shared.parentUuid = first.uuid;

    model::EntityTreeNode sharedNode;
    sharedNode.entity = shared;
    model::EntityTreeNode firstTree;
    firstTree.entity = first;
    firstTree.children.append(sharedNode);

    shared.parentUuid = second.uuid;
    model::EntityTreeNode secondSharedNode;
    secondSharedNode.entity = shared;
    model::EntityTreeNode secondTree;
    secondTree.entity = second;
    secondTree.children.append(secondSharedNode);

    QVERIFY(writeText(path + QStringLiteral("/entities/") + first.uuid
                          + QStringLiteral(".json"),
                      QString::fromUtf8(QJsonDocument(model::toJson(firstTree))
                                            .toJson(QJsonDocument::Indented))));
    QVERIFY(writeText(path + QStringLiteral("/entities/") + second.uuid
                          + QStringLiteral(".json"),
                      QString::fromUtf8(QJsonDocument(model::toJson(secondTree))
                                            .toJson(QJsonDocument::Indented))));

    QString error;
    auto store = ProjectStore::open(path, &error, false);
    QVERIFY2(store.has_value(), qPrintable(error));
    QVERIFY(store->readOnly());
    QCOMPARE(store->quarantinedFiles().size(), 1);
    QVERIFY2(store->quarantinedFiles().first().reason.contains(
                 QStringLiteral("重复")),
             qPrintable(store->quarantinedFiles().first().reason));
    // QDir::Name loads the lexicographically first root file; the other one is
    // the duplicate that gets isolated.
    const QString loadedRoot =
        first.uuid < second.uuid ? first.uuid : second.uuid;
    const QString droppedRoot =
        first.uuid < second.uuid ? second.uuid : first.uuid;
    QVERIFY(store->project().entities.contains(loadedRoot));
    QVERIFY(store->project().entities.contains(shared.uuid));
    QVERIFY(!store->project().entities.contains(droppedRoot));
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/entities/") + loadedRoot
                              + QStringLiteral(".json")));
}

void StoreSafetyTests::lockBlocksSecondInstance()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));
    store.reset();

    // A raw QLockFile on the same path stands in for another PVForge process:
    // the pid is alive, so it is not stale.
    QLockFile foreignLock(path + QStringLiteral("/.pvforge/project.lock"));
    QVERIFY(foreignLock.tryLock(0));

    error.clear();
    auto blocked = ProjectStore::open(path, &error, false);
    QVERIFY(!blocked.has_value());
    QVERIFY2(error.contains(QStringLiteral("另一个 PVForge 实例")),
             qPrintable(error));

    foreignLock.unlock();
    error.clear();
    auto opened = ProjectStore::open(path, &error, false);
    QVERIFY2(opened.has_value(), qPrintable(error));
}

void StoreSafetyTests::externalChangeRefusesOverwrite()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("原名"), 0, 30);
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));

    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    const QString external =
        QString::fromUtf8(QJsonDocument(model::toJson(entity))
                              .toJson(QJsonDocument::Indented))
            .replace(QStringLiteral("原名"), QStringLiteral("外部改名"));
    QVERIFY(writeText(entityPath, external));

    store->mutableProject().entities[entity.uuid].name =
        QStringLiteral("本地改名");
    error.clear();
    QVERIFY(!store->saveEntity(entity.uuid, &error));
    QVERIFY2(error.contains(QStringLiteral("外部程序修改")), qPrintable(error));
    QCOMPARE(readText(entityPath), external);
}

void StoreSafetyTests::readFailureIsNotMissing()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    model::Entity entity = makeEntity(QStringLiteral("原名"), 0, 30);
    store->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY2(store->saveEntity(entity.uuid, &error), qPrintable(error));
    QVERIFY2(store->commit(QStringLiteral("pvf: create"), &error),
             qPrintable(error));

    const QString entityPath =
        path + QStringLiteral("/entities/") + entity.uuid + QStringLiteral(".json");
    const QByteArray original = readText(entityPath).toUtf8();
    QVERIFY(QFile::setPermissions(entityPath, QFileDevice::WriteOwner));

    // An unreadable file is not "the file does not exist": the snapshot must
    // not turn into a delete operation.
    store->mutableProject().entities[entity.uuid].name =
        QStringLiteral("本地改名");
    error.clear();
    QVERIFY(!store->saveEntity(entity.uuid, &error));
    QVERIFY2(error.contains(QStringLiteral("无法读取")), qPrintable(error));

    error.clear();
    QVERIFY(!store->undo(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo::exists(entityPath));

    QVERIFY(QFile::setPermissions(entityPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(readText(entityPath).toUtf8(), original);
}

void StoreSafetyTests::assetTrashIsUndoable()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = projectPath(temp);

    QString error;
    auto store = ProjectStore::create(path, model::ProjectSettings(), &error,
                                      false);
    QVERIFY2(store.has_value(), qPrintable(error));

    const QString imagePath = temp.filePath(QStringLiteral("pixel.png"));
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    const auto asset = store->assets().importFile(imagePath, &error);
    QVERIFY2(asset.has_value(), qPrintable(error));
    const QString hash = asset->hash;

    model::Entity reference = makeEntity(QStringLiteral("引用"), 0, 30);
    reference.content.kind = ContentKind::Asset;
    reference.content.asset.hash = hash;
    reference.content.asset.type = AssetType::Image;
    store->mutableProject().entities.insert(reference.uuid, reference);
    QVERIFY2(store->saveEntity(reference.uuid, &error), qPrintable(error));
    QCOMPARE(store->entitiesReferencingAsset(hash),
             QStringList{reference.uuid});

    QVERIFY2(store->trashAsset(hash, &error), qPrintable(error));
    QVERIFY(!store->assets().get(hash).has_value());
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.trash/") + hash
                              + QStringLiteral("/asset.json")));

    QVERIFY2(store->commit(QStringLiteral("pvf: delete asset"), &error),
             qPrintable(error));
    QCOMPARE(store->undoDepth(), 1);

    error.clear();
    QVERIFY2(store->undo(&error), qPrintable(error));
    QVERIFY2(store->assets().get(hash).has_value(), qPrintable(error));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral("/.trash/") + hash));

    error.clear();
    QVERIFY2(store->redo(&error), qPrintable(error));
    QVERIFY(!store->assets().get(hash).has_value());
    QVERIFY(QFileInfo::exists(path + QStringLiteral("/.trash/") + hash));
}

void StoreSafetyTests::failedCreateRemovesPartialProject()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("bin"))));

    // Fake git: enough for the version probe and init, but commit fails. The
    // half-created project directory must not survive.
    const QString fakeGit = temp.filePath(QStringLiteral("bin/git"));
    QVERIFY(writeText(fakeGit,
                      QStringLiteral("#!/bin/sh\n"
                                     "root=.\n"
                                     "prev=\n"
                                     "for arg in \"$@\"; do\n"
                                     "  [ \"$prev\" = \"-C\" ] && root=\"$arg\"\n"
                                     "  prev=\"$arg\"\n"
                                     "done\n"
                                     "case \"$*\" in\n"
                                     "  *--version*) echo 'git version 2.40.0'; exit 0;;\n"
                                     "  *init*) /bin/mkdir -p \"$root/.git\"; exit 0;;\n"
                                     "  *config*) exit 0;;\n"
                                     "  *add*) exit 0;;\n"
                                     "  *commit*) echo 'boom' >&2; exit 1;;\n"
                                     "esac\n"
                                     "exit 1\n")));
    QVERIFY(QFile::setPermissions(
        fakeGit, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                     | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                     | QFileDevice::ExeGroup | QFileDevice::ReadOther
                     | QFileDevice::ExeOther));

    PathGuard guard;
    QVERIFY(qputenv("PATH", temp.filePath(QStringLiteral("bin")).toUtf8()));

    const QString path = temp.filePath(QStringLiteral("Broken.pvproj"));
    QString error;
    auto created = ProjectStore::create(path, model::ProjectSettings(), &error);
    QVERIFY(!created.has_value());
    QVERIFY2(error.contains(QStringLiteral("commit")), qPrintable(error));
    QVERIFY(!QFileInfo::exists(path));
}

QTEST_GUILESS_MAIN(StoreSafetyTests)
#include "store_safety_tests.moc"
