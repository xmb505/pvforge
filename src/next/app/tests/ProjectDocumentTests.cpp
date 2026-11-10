#include "next/app/ProjectDocument.h"

#include <QCoreApplication>
#include <QDir>
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
