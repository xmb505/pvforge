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
        QCOMPARE(document->store()->history(1).first().message,
                 QStringLiteral("pvf: human edit"));
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

    void undoDropsPendingDebounce()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/drop.pvproj"), ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity entity = makeEntity(QStringLiteral("v1"), 0, 30);
        document->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: create"), &error),
                 qPrintable(error));
        document->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const int commits = document->store()->history(100).size();

        // A newer edit is still inside the debounce window when undo arrives.
        document->mutableProject().entities[entity.uuid].name = QStringLiteral("v2");
        QVERIFY2(document->commitEntity(entity.uuid, QStringLiteral("pvf: rename"), &error),
                 qPrintable(error));
        QVERIFY(document->hasPendingCommit());

        QVERIFY2(document->undo(&error), qPrintable(error));
        QVERIFY(!document->hasPendingCommit());
        QVERIFY(!document->project().entities.contains(entity.uuid));

        QTest::qWait(1300);
        QCOMPARE(document->store()->history(100).size(), commits);
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

        const QString childPath = entityPath(*document, child.uuid);
        QCOMPARE(entityJsonInt(childPath, QStringLiteral("end")), 30);

        document->mutableProject().entities[child.uuid].end = 90;
        error.clear();
        QVERIFY(!document->commitEntity(child.uuid,
                                        QStringLiteral("pvf: break range"),
                                        &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(entityJsonInt(childPath, QStringLiteral("end")), 30);
        QVERIFY(!document->hasPendingCommit());
    }

    void deleteParentWithoutCascadeIsRejected()
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

        const int historyBefore = document->store()->history(100).size();
        error.clear();
        QVERIFY(!document->deleteEntities({parent.uuid},
                                          QStringLiteral("pvf: delete parent"),
                                          &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document->project().entities.contains(parent.uuid));
        QVERIFY(document->project().entities.contains(child.uuid));
        QVERIFY(QFile::exists(entityPath(*document, parent.uuid)));
        QVERIFY(QFile::exists(entityPath(*document, child.uuid)));
        QCOMPARE(document->store()->history(100).size(), historyBefore);

        QVERIFY2(document->deleteEntities(
                     {child.uuid, parent.uuid},
                     QStringLiteral("pvf: delete subtree"), &error),
                 qPrintable(error));
        QVERIFY(!document->project().entities.contains(parent.uuid));
        QVERIFY(!document->project().entities.contains(child.uuid));
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

        const QString childPath = entityPath(*document, child.uuid);
        const QByteArray before = readBytes(childPath);
        QVERIFY(!before.isEmpty());

        document->mutableProject().entities[child.uuid].end = 90;
        document->mutableProject().entities[child.uuid].name =
            QStringLiteral("越界");
        error.clear();
        QVERIFY(!document->commitChanges({child.uuid}, {},
                                         QStringLiteral("pvf: break tree"),
                                         &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(childPath), before);
        QCOMPARE(entityJsonInt(childPath, QStringLiteral("end")), 30);
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
        QVERIFY2(document->store()->deleteEntity(parent.uuid, &error),
                 qPrintable(error));
        error.clear();
        QVERIFY(!document->commitProject(QStringLiteral("pvf: delete parent"),
                                         &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readBytes(projectPath), before);
    }

    void deleteChildOfLockedParentIsRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto document = ProjectDocument::create(
            temp.path() + QStringLiteral("/locked-delete.pvproj"),
            ProjectSettings(), &error);
        QVERIFY2(document != nullptr, qPrintable(error));

        Entity parent = makeEntity(QStringLiteral("父"), 0, 60);
        parent.locked = true;
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

        error.clear();
        QVERIFY(!document->deleteEntities({child.uuid},
                                          QStringLiteral("pvf: delete child"),
                                          &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document->project().entities.contains(child.uuid));
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
