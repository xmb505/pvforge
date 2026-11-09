#include "next/app/NextWindow.h"

#include "next/app/ProjectDocument.h"
#include "next/viewport/ViewportWidget.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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

std::unique_ptr<NextWindow> makeWindow(QTemporaryDir& temp, QString* error)
{
    ProjectSettings settings;
    settings.name = QStringLiteral("Shell");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 24;

    auto document = ProjectDocument::create(
        temp.path() + QStringLiteral("/shell.pvproj"), settings, error);
    if (!document)
        return nullptr;
    return std::make_unique<NextWindow>(std::move(document));
}

Entity makeCamera(const QString& name)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = 0;
    entity.end = 30;
    entity.content.kind = ContentKind::Camera;
    return entity;
}

} // namespace

class NextWindowTests : public QObject
{
    Q_OBJECT

private slots:
    void shellExposesPanels()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));
        QVERIFY(window->document() != nullptr);

        QVERIFY(window->viewportForTest() != nullptr);
        QVERIFY(window->timelineForTest() != nullptr);
        QVERIFY(window->assetPanelForTest() != nullptr);
        QVERIFY(window->propertiesPanelForTest() != nullptr);
        QVERIFY(window->cameraComboBoxForTest() != nullptr);
        QCOMPARE(window->cameraComboBoxForTest()->count(), 1);
    }

    void playheadSyncsBothWays()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QSpinBox* spin = window->playheadSpinBoxForTest();
        QVERIFY(spin != nullptr);

        spin->setValue(42);
        QCOMPARE(window->document()->playhead(), 42);

        window->document()->setPlayhead(7);
        QCOMPARE(spin->value(), 7);
    }

    void cameraComboListsCameraEntities()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        const Entity camera = makeCamera(QStringLiteral("主镜头"));
        window->document()->mutableProject().entities.insert(camera.uuid, camera);
        QVERIFY2(window->document()->commitEntity(
                     camera.uuid, QStringLiteral("pvf: create camera"), &error),
                 qPrintable(error));

        QComboBox* combo = window->cameraComboBoxForTest();
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemData(1).toString(), camera.uuid);
        QVERIFY(combo->itemText(1).contains(QStringLiteral("主镜头")));
        QVERIFY(combo->itemText(1).contains(camera.uuid.left(8)));

        combo->setCurrentIndex(1);
        QCOMPARE(window->viewportForTest()->cameraUuid(), camera.uuid);

        Entity empty;
        empty.uuid = model::newUuid();
        empty.name = QStringLiteral("空实体");
        empty.start = 0;
        empty.end = 10;
        window->document()->mutableProject().entities.insert(empty.uuid, empty);
        QVERIFY2(window->document()->commitEntity(
                     empty.uuid, QStringLiteral("pvf: create empty"), &error),
                 qPrintable(error));
        QCOMPARE(combo->count(), 2);
    }

    void undoRedoActionsDoNotCrash()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QAction* undo = window->undoActionForTest();
        QAction* redo = window->redoActionForTest();
        QVERIFY(undo != nullptr);
        QVERIFY(redo != nullptr);

        const Entity camera = makeCamera(QStringLiteral("撤销镜头"));
        window->document()->mutableProject().entities.insert(camera.uuid, camera);
        QVERIFY2(window->document()->commitEntity(
                     camera.uuid, QStringLiteral("pvf: create camera"), &error),
                 qPrintable(error));
        QVERIFY(window->document()->project().entities.contains(camera.uuid));

        undo->trigger();
        QCoreApplication::processEvents();
        QVERIFY(!window->document()->project().entities.contains(camera.uuid));

        redo->trigger();
        QCoreApplication::processEvents();
        QVERIFY(window->document()->project().entities.contains(camera.uuid));
    }

    void closingWindowDoesNotCrash()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        window->show();
        QVERIFY(window->close());
        QCoreApplication::processEvents();
    }

    void oldSingleFileProjectRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.path() + QStringLiteral("/old.pvproj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
        file.close();

        NextWindow window(nullptr);
        QString error;
        QVERIFY(!window.openProject(path, &error));
        QVERIFY(error.contains(QStringLiteral("旧工程格式已废弃")));
        QVERIFY(window.document() == nullptr);
    }
};

QTEST_MAIN(NextWindowTests)
#include "NextWindowTests.moc"
