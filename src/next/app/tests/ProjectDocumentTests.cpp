#include "next/app/ProjectDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

bool gitLfsAvailable()
{
    QProcess process;
    process.start(QStringLiteral("git-lfs"), {QStringLiteral("version")});
    return process.waitForFinished(5000) && process.exitCode() == 0;
}

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
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

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
};

QTEST_MAIN(ProjectDocumentTests)
#include "ProjectDocumentTests.moc"
