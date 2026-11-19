#include "next/app/ProjectDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

Entity makeEntity(const QString& name, int start, int end)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    return entity;
}

QString entityPath(const ProjectDocument& document, const Uuid& uuid)
{
    return document.root() + QStringLiteral("/entities/") + uuid
           + QStringLiteral(".json");
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int entityJsonInt(const QString& path, const QString& key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    return QJsonDocument::fromJson(file.readAll())
        .object()
        .value(key)
        .toInt(-1);
}

QJsonObject findTreeNode(const QJsonObject& node, const Uuid& uuid)
{
    if (node.value(QStringLiteral("uuid")).toString() == uuid)
        return node;
    for (const QJsonValue& value : node.value(QStringLiteral("children")).toArray())
    {
        const QJsonObject found = findTreeNode(value.toObject(), uuid);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

int treeEntityInt(const QString& path, const Uuid& uuid, const QString& key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    return findTreeNode(QJsonDocument::fromJson(file.readAll()).object(), uuid)
        .value(key)
        .toInt(-1);
}

} // namespace

class ProjectDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void createCommitOpenUndoRedo()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString projectDir = temp.path() + QStringLiteral("/test.pvproj");

        ProjectSettings settings;
        settings.name = QStringLiteral("测试");
        settings.width = 640;
        settings.height = 360;
        settings.fps = 24;

        QString error;
        auto document = ProjectDocument::create(projectDir, settings, &error);
        QVERIFY2(document != nullptr, qPrintable(error));
        QCOMPARE(document->project().settings.width, 640);
        QCOMPARE(document->playhead(), 0);

        QSignalSpy playheadSpy(document.get(), &Document::playheadChanged);
        document->setPlayhead(12);
        QCOMPARE(document->playhead(), 12);
        QCOMPARE(playheadSpy.count(), 1);

        Entity entity = makeEntity(QStringLiteral("球"), 0, 48);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create ball"), &error),
                 qPrintable(error));
        // Human writes are debounced; force the cold backup before undo.
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        document->setSelection(entity.uuid);
        QCOMPARE(document->selection(), entity.uuid);
        QVERIFY(document->project().entities.contains(entity.uuid));

        auto reopened = ProjectDocument::open(projectDir, &error);
        QVERIFY2(reopened != nullptr, qPrintable(error));
        QVERIFY(reopened->project().entities.contains(entity.uuid));
        QCOMPARE(reopened->project().entities.value(entity.uuid).name, QStringLiteral("球"));

        QVERIFY2(document->undo(&error), qPrintable(error));
        QVERIFY(!document->project().entities.contains(entity.uuid));
        QVERIFY(document->selection().isEmpty());

        QVERIFY2(document->redo(&error), qPrintable(error));
        QVERIFY(document->project().entities.contains(entity.uuid));
    }

    void writeLockRejectsEditsUntilReleased()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString projectDir = temp.path() + QStringLiteral("/locked.pvproj");

        QString error;
        auto document = ProjectDocument::create(projectDir, ProjectSettings(),
                                                &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("锁"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        document->setWritesLocked(true);
        QVERIFY(document->writesLocked());

        error.clear();
        QVERIFY(!document->commitEntity(entity.uuid, QStringLiteral("pvf: x"),
                                        &error));
        QVERIFY(error.contains(QStringLiteral("导出")));

        error.clear();
        QVERIFY(!document->commitChanges({entity.uuid}, {},
                                         QStringLiteral("pvf: x"), &error));
        QVERIFY(error.contains(QStringLiteral("导出")));

        error.clear();
        QVERIFY(!document->undo(&error));
        QVERIFY(error.contains(QStringLiteral("导出")));

        // The export finished: the same edit goes through.
        document->setWritesLocked(false);
        error.clear();
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: x"),
                                        &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void playheadRestoredFromProjectState()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString projectDir = temp.path() + QStringLiteral("/playhead.pvproj");

        ProjectSettings settings;
        settings.name = QStringLiteral("playhead");
        QString error;
        {
            auto document = ProjectDocument::create(projectDir, settings, &error);
            QVERIFY2(document != nullptr, qPrintable(error));

            Entity entity = makeEntity(QStringLiteral("range"), 0, 100);
            document->mutableProject().entities.insert(entity.uuid, entity);
            QVERIFY2(document->commitEntity(entity.uuid,
                                            QStringLiteral("pvf: create range"),
                                            &error),
                     qPrintable(error));
            document->flush(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));

            document->setPlayhead(42);
            document->savePlayheadState();
        }

        auto reopened = ProjectDocument::open(projectDir, &error);
        QVERIFY2(reopened != nullptr, qPrintable(error));
        QCOMPARE(reopened->playhead(), 42);
        reopened.reset();

        // The state file lives outside project.json and is clamped to the
        // project content when it is stale.
        const QString statePath =
            projectDir + QStringLiteral("/.pvforge/playhead.json");
        QVERIFY(QFileInfo::exists(statePath));
        QFile state(statePath);
        QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
        state.write(R"({"frame": 9999})");
        state.close();

        auto clamped = ProjectDocument::open(projectDir, &error);
        QVERIFY2(clamped != nullptr, qPrintable(error));
        QCOMPARE(clamped->playhead(), 100);
        clamped.reset();

        // A corrupt file is ignored instead of failing the open.
        QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
        state.write("{not json");
        state.close();

        auto corrupt = ProjectDocument::open(projectDir, &error);
        QVERIFY2(corrupt != nullptr, qPrintable(error));
        QCOMPARE(corrupt->playhead(), 0);
    }

    void commitChangesUpsertsAndDeletesAtomically()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString projectDir = temp.path() + QStringLiteral("/changes.pvproj");

        ProjectSettings settings;
        settings.name = QStringLiteral("changes");
        QString error;
        auto document = ProjectDocument::create(projectDir, settings, &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity keep = makeEntity(QStringLiteral("keep"), 0, 30);
        Entity drop = makeEntity(QStringLiteral("drop"), 0, 30);
        drop.row = 1;
        document->mutableProject().entities.insert(keep.uuid, keep);
        document->mutableProject().entities.insert(drop.uuid, drop);
        QVERIFY2(document->commitEntities({keep.uuid, drop.uuid},
                                          QStringLiteral("pvf: create two"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const int before = document->store()->history(100).size();
        document->mutableProject().entities[keep.uuid].name = QStringLiteral("kept");
        QVERIFY2(document->commitChanges({keep.uuid}, {drop.uuid},
                                         QStringLiteral("pvf: trim"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QCOMPARE(document->store()->history(100).size(), before + 1);
        QVERIFY(document->project().entities.contains(keep.uuid));
        QVERIFY(!document->project().entities.contains(drop.uuid));
        QCOMPARE(document->project().entities.value(keep.uuid).name, QStringLiteral("kept"));
    }

    void humanEditDebouncesIntoOneCommit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/debounce.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        const int before = document->store()->history(100).size();
        Entity entity = makeEntity(QStringLiteral("球"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid,
                                        QStringLiteral("pvf: create ball"), &error),
                 qPrintable(error));
        QVERIFY(document->hasPendingCommit());

        // Not committed immediately.
        QCOMPARE(document->store()->history(100).size(), before);
        QTest::qWait(500);
        QCOMPARE(document->store()->history(100).size(), before);
        QTest::qWait(700);
        QCOMPARE(document->store()->history(100).size(), before + 1);
        // The caller's message is the real commit message, not a generic
        // "human edit" placeholder.
        QCOMPARE(document->store()->history(1).first().message,
                 QStringLiteral("pvf: create ball"));
        QVERIFY(!document->hasPendingCommit());
    }

    void rapidHumanEditsCoalesceIntoOneCommit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/coalesce.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        const int before = document->store()->history(100).size();
        Entity entity = makeEntity(QStringLiteral("v1"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create"), &error),
                 qPrintable(error));

        QTest::qWait(300);
        document->mutableProject().entities[entity.uuid].name = QStringLiteral("v2");
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: rename"), &error),
                 qPrintable(error));

        QTest::qWait(1200);
        QCOMPARE(document->store()->history(100).size(), before + 1);
        QCOMPARE(document->project().entities.value(entity.uuid).name,
                 QStringLiteral("v2"));
    }

    void aiCommitIsImmediateAndNotDebounced()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/ai.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        const int before = document->store()->history(100).size();
        Entity entity = makeEntity(QStringLiteral("AI"), 0, 30);
        document->store()->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->store()->saveEntity(entity.uuid, &error), qPrintable(error));
        QVERIFY2(document->store()->commit(QStringLiteral("pvf: ai create"), &error),
                 qPrintable(error));

        QCOMPARE(document->store()->history(100).size(), before + 1);
        QCOMPARE(document->store()->history(1).first().message,
                 QStringLiteral("pvf: ai create"));
        QVERIFY(!document->hasPendingCommit());

        QTest::qWait(1200);
        QCOMPARE(document->store()->history(100).size(), before + 1);

        // AI commits also land in the shared memory undo stack.
        QVERIFY2(document->undo(&error), qPrintable(error));
        QVERIFY(!document->project().entities.contains(entity.uuid));
        QCOMPARE(document->store()->history(100).size(), before + 1);
        QVERIFY2(document->redo(&error), qPrintable(error));
        QVERIFY(document->project().entities.contains(entity.uuid));
        QCOMPARE(document->store()->history(100).size(), before + 1);
    }

    void undoRedoUseMemoryHistory()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/undo.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("撤销"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const int commits = document->store()->history(100).size();
        QSignalSpy changedSpy(document.get(), &Document::changed);

        QVERIFY2(document->undo(&error), qPrintable(error));
        QVERIFY(!document->project().entities.contains(entity.uuid));
        QCOMPARE(document->store()->history(100).size(), commits);
        QCOMPARE(changedSpy.count(), 1);

        QVERIFY2(document->redo(&error), qPrintable(error));
        QVERIFY(document->project().entities.contains(entity.uuid));
        QCOMPARE(document->store()->history(100).size(), commits);
        QCOMPARE(changedSpy.count(), 2);
    }

    void undoFlushesPendingEditAsFirstLayer()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/flush-undo.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("v1"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid,
                                        QStringLiteral("pvf: create v1"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const int commits = document->store()->history(100).size();

        // The rename is still inside the debounce window when undo arrives.
        document->mutableProject().entities[entity.uuid].name =
            QStringLiteral("v2");
        QVERIFY2(document->commitEntity(entity.uuid,
                                        QStringLiteral("pvf: rename v2"),
                                        &error),
                 qPrintable(error));
        QVERIFY(document->hasPendingCommit());

        QVERIFY2(document->undo(&error), qPrintable(error));
        QVERIFY(!document->hasPendingCommit());
        // Only the rename is undone; the entity itself survives.
        QVERIFY(document->project().entities.contains(entity.uuid));
        QCOMPARE(document->project().entities.value(entity.uuid).name,
                 QStringLiteral("v1"));
        // The pending edit reached git as its own commit before undoing.
        QCOMPARE(document->store()->history(100).size(), commits + 1);
        QCOMPARE(document->store()->history(1).first().message,
                 QStringLiteral("pvf: rename v2"));

        QVERIFY2(document->redo(&error), qPrintable(error));
        QCOMPARE(document->project().entities.value(entity.uuid).name,
                 QStringLiteral("v2"));
        QCOMPARE(document->store()->history(100).size(), commits + 1);
    }

    void invalidChildRangeRejectedWithoutWrite()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/invalid.pvproj"), ProjectSettings(),
            &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString rootPath = entityPath(*document, parent.uuid);
        QCOMPARE(treeEntityInt(rootPath, child.uuid, QStringLiteral("end")), 30);

        document->mutableProject().entities[child.uuid].end = 90;
        error.clear();
        QVERIFY(!document->commitEntity(child.uuid,
                                        QStringLiteral("pvf: break range"),
                                        &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(treeEntityInt(rootPath, child.uuid, QStringLiteral("end")), 30);
        QVERIFY(!document->hasPendingCommit());
    }

    void deleteParentCascadesSubtree()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/delete-parent.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString rootPath = entityPath(*document, parent.uuid);
        QVERIFY(QFile::exists(rootPath));

        QVERIFY2(document->deleteEntities(
                     {parent.uuid}, QStringLiteral("pvf: delete subtree"), &error),
                 qPrintable(error));
        QVERIFY(!document->project().entities.contains(parent.uuid));
        QVERIFY(!document->project().entities.contains(child.uuid));
        QVERIFY(!QFile::exists(rootPath));
    }

    void commitChangesInvalidUpsertLeavesFilesUntouched()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/changes-invalid.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString rootPath = entityPath(*document, parent.uuid);
        const QByteArray before = readBytes(rootPath);
        QVERIFY(!before.isEmpty());

        document->mutableProject().entities[child.uuid].end = 90;
        document->mutableProject().entities[child.uuid].name =
            QStringLiteral("越界");
        error.clear();
        QVERIFY(!document->commitChanges({child.uuid}, {},
                                         QStringLiteral("pvf: break tree"),
                                         &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(rootPath), before);
        QCOMPARE(treeEntityInt(rootPath, child.uuid, QStringLiteral("end")), 30);
    }

    void commitProjectRejectsInconsistentTree()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/project-invalid.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString projectPath =
            document->root() + QStringLiteral("/project.json");
        const QByteArray before = readBytes(projectPath);
        QVERIFY(!before.isEmpty());

        // A caller that deletes only the parent leaves a dangling child.
        document->mutableProject().entities.remove(parent.uuid);
        error.clear();
        QVERIFY(!document->commitProject(QStringLiteral("pvf: delete parent"),
                                         &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(projectPath), before);
    }

    void commitChildOfLockedParentIsRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/locked-upsert.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->mutableProject().entities[parent.uuid].locked = true;
        QVERIFY2(document->commitEntity(parent.uuid,
                                        QStringLiteral("pvf: lock parent"),
                                        &error),
                 qPrintable(error));

        document->mutableProject().entities[child.uuid].name = QStringLiteral("改名");
        error.clear();
        QVERIFY(!document->commitEntity(child.uuid,
                                        QStringLiteral("pvf: rename child"),
                                        &error));
        QVERIFY(error.contains(QStringLiteral("锁定")));
    }

    void deleteChildOfLockedParentIsRejected()
    {        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/locked-delete.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        Entity child = makeEntity(QStringLiteral("子"), 0, 30);
        child.parentUuid = parent.uuid;
        document->mutableProject().entities.insert(parent.uuid, parent);
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntities({parent.uuid, child.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        // Locking the parent after the tree exists exercises the same guard
        // without using an ancestor-locked upsert to create the tree.
        document->mutableProject().entities[parent.uuid].locked = true;
        QVERIFY2(document->commitEntity(parent.uuid,
                                        QStringLiteral("pvf: lock parent"),
                                        &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        error.clear();
        QVERIFY(!document->deleteEntities({child.uuid},
                                          QStringLiteral("pvf: delete child"),
                                          &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document->project().entities.contains(child.uuid));
    }

    void duplicateDeleteIsRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/dup-delete.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("唯一"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create"),
                                        &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // The second occurrence is rejected and nothing is deleted.
        error.clear();
        QVERIFY(!document->deleteEntities({entity.uuid, entity.uuid},
                                          QStringLiteral("pvf: delete twice"),
                                          &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document->project().entities.contains(entity.uuid));
        QVERIFY(QFile::exists(entityPath(*document, entity.uuid)));

        QVERIFY2(document->deleteEntities({entity.uuid},
                                          QStringLiteral("pvf: delete"),
                                          &error),
                 qPrintable(error));
        QVERIFY(!document->project().entities.contains(entity.uuid));
    }

    void deleteBatchRejectsDanglingChild()
    {
        // A delete batch and an edit of one entity both have to end up with a
        // validated project; the delete-vs-copy path must reject the same
        // dangling-parent case as the old copy-based check.
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/batch-delete.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity root = makeEntity(QStringLiteral("根"), 0, 120);
        Entity middle = makeEntity(QStringLiteral("中"), 0, 100);
        middle.parentUuid = root.uuid;
        Entity leaf = makeEntity(QStringLiteral("叶"), 0, 60);
        leaf.parentUuid = middle.uuid;
        document->mutableProject().entities.insert(root.uuid, root);
        document->mutableProject().entities.insert(middle.uuid, middle);
        document->mutableProject().entities.insert(leaf.uuid, leaf);
        QVERIFY2(document->commitEntities({root.uuid, middle.uuid, leaf.uuid},
                                          QStringLiteral("pvf: create tree"),
                                          &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Deleting the middle folder removes its whole subtree in one go.
        QVERIFY2(document->deleteEntities({middle.uuid},
                                          QStringLiteral("pvf: delete subtree"),
                                          &error),
                 qPrintable(error));
        QVERIFY(!document->project().entities.contains(leaf.uuid));
        QVERIFY(!document->project().entities.contains(middle.uuid));
        QVERIFY(document->project().entities.contains(root.uuid));
    }

    void failedReloadSwitchesToReadOnly()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/readonly.pvproj"),
            ProjectSettings(), &error, false);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("球"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid,
                                        QStringLiteral("pvf: create"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Simulate a write that landed on disk but cannot be reloaded.
        const QString projectPath =
            document->root() + QStringLiteral("/project.json");
        const QString backup = projectPath + QStringLiteral(".bak");
        QVERIFY(QFile::rename(projectPath, backup));

        error.clear();
        QVERIFY(!document->notifyExternalChange(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document->store()->readOnly());
        QVERIFY2(document->store()->readOnlyReason().contains(
                     QStringLiteral("只读")),
                 qPrintable(document->store()->readOnlyReason()));

        // Continuing with the old memory would overwrite the disk state, so
        // every write entry point refuses.
        document->mutableProject().entities[entity.uuid].name =
            QStringLiteral("不应写入");
        error.clear();
        QVERIFY(!document->commitEntity(entity.uuid,
                                        QStringLiteral("pvf: should fail"),
                                        &error));
        QVERIFY2(error.contains(QStringLiteral("只读")), qPrintable(error));

        QVERIFY(QFile::rename(backup, projectPath));
    }

    void commitFailurePersistsRecoveryHint()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        const QString projectDir =
            temp.path() + QStringLiteral("/hint.pvproj");
        {
            auto document = ProjectDocument::create(projectDir, ProjectSettings(),
                                                    &error);
            QVERIFY2(document != nullptr, qPrintable(error));

            QDir directory(document->root());
            QVERIFY(directory.rename(QStringLiteral(".git"),
                                     QStringLiteral(".git-disabled")));

            Entity entity = makeEntity(QStringLiteral("失败"), 0, 30);
            document->mutableProject().entities.insert(entity.uuid, entity);
            QVERIFY2(document->commitEntity(entity.uuid,
                                            QStringLiteral("pvf: create"),
                                            &error),
                     qPrintable(error));
            error.clear();
            document->flush(&error);
            QVERIFY(!error.isEmpty());
            QVERIFY(!document->store()->previousCommitFailure().isEmpty());
        }

        // Still no .git: the destructor's last flush fails again and keeps the
        // hint. Restore the repository afterwards and reopen.
        QDir directory(projectDir);
        QVERIFY(directory.rename(QStringLiteral(".git-disabled"),
                                 QStringLiteral(".git")));

        // The marker survives the session so the next open can tell a failed
        // git commit apart from an abnormal exit.
        auto reopened = ProjectDocument::open(projectDir, &error);
        QVERIFY2(reopened != nullptr, qPrintable(error));
        QVERIFY(!reopened->store()->previousCommitFailure().isEmpty());
        QVERIFY2(reopened->store()->previousCommitFailure().contains(
                     QStringLiteral("git")),
                 qPrintable(reopened->store()->previousCommitFailure()));
    }

    void commitFailureIsReportedThroughSignal()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/fail.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        QDir tempDir(document->root());
        QVERIFY(tempDir.rename(QStringLiteral(".git"),
                               QStringLiteral(".git-disabled")));

        QSignalSpy failedSpy(document.get(), &Document::commitFailed);
        Entity entity = makeEntity(QStringLiteral("失败"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        // Saving succeeds; only the debounced git commit can fail.
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create"), &error),
                 qPrintable(error));
        QVERIFY(document->project().entities.contains(entity.uuid));

        error.clear();
        document->flush(&error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(document->hasPendingCommit());

        QVERIFY(tempDir.rename(QStringLiteral(".git-disabled"),
                               QStringLiteral(".git")));
    }
};

QTEST_MAIN(ProjectDocumentTests)
#include "ProjectDocumentTests.moc"
