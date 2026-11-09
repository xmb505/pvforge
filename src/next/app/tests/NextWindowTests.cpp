#include "next/app/NextWindow.h"

#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"
#include "next/app/ExportDialog.h"
#include "next/app/ProjectDocument.h"
#include "next/properties/PropertiesPanel.h"
#include "next/timeline/TimelineWidget.h"
#include "next/viewport/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
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

int commitCount(NextWindow* window)
{
    return window->document()->store()->history(10000).size();
}

void dismissNextMessageBox()
{
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            box->accept();
            return;
        }
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                box->accept();
        }
    });
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

    void createEntityActionsPickDefaults()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();
        document->setPlayhead(10);
        const int commitsBefore = commitCount(window.get());

        QAction* emptyAction = window->createEntityActionForTest(ContentKind::Empty);
        QVERIFY(emptyAction != nullptr);
        QVERIFY(emptyAction->isEnabled());
        emptyAction->trigger();

        QCOMPARE(document->project().entities.size(), 1);
        const Uuid emptyUuid = document->selection();
        QVERIFY(!emptyUuid.isEmpty());
        QVERIFY(document->project().entities.contains(emptyUuid));
        const Entity empty = document->project().entities.value(emptyUuid);
        QCOMPARE(empty.name, QStringLiteral("空实体"));
        QCOMPARE(empty.content.kind, ContentKind::Empty);
        QCOMPARE(empty.start, 10);
        QCOMPARE(empty.end, 34);
        QCOMPARE(empty.row, 0);
        QVERIFY(empty.parentUuid.isEmpty());

        QAction* textAction = window->createEntityActionForTest(ContentKind::Text);
        QVERIFY(textAction != nullptr);
        textAction->trigger();
        const Entity text = document->project().entities.value(document->selection());
        QCOMPARE(text.name, QStringLiteral("文字"));
        QCOMPARE(text.content.kind, ContentKind::Text);
        QCOMPARE(text.content.text.text, QStringLiteral("文字"));
        QCOMPARE(text.content.text.pixelSize, 96);
        QCOMPARE(text.content.text.color, QStringLiteral("#ffffff"));
        QCOMPARE(text.content.text.align, int(Qt::AlignCenter));
        QCOMPARE(text.row, 1);

        QAction* vectorAction = window->createEntityActionForTest(ContentKind::Vector);
        QVERIFY(vectorAction != nullptr);
        vectorAction->trigger();
        const Entity vector = document->project().entities.value(document->selection());
        QCOMPARE(vector.name, QStringLiteral("矢量"));
        QCOMPARE(vector.content.kind, ContentKind::Vector);
        QCOMPARE(vector.content.vector.shape, 0);
        QCOMPARE(vector.content.vector.width, 240.0);
        QCOMPARE(vector.content.vector.height, 120.0);
        QCOMPARE(vector.content.vector.cornerRadius, 8.0);
        QCOMPARE(vector.content.vector.fillColor, QStringLiteral("#ffffff"));
        QCOMPARE(vector.row, 2);

        QAction* cameraAction = window->createEntityActionForTest(ContentKind::Camera);
        QVERIFY(cameraAction != nullptr);
        cameraAction->trigger();
        const Entity camera = document->project().entities.value(document->selection());
        QCOMPARE(camera.name, QStringLiteral("摄像机"));
        QCOMPARE(camera.content.kind, ContentKind::Camera);
        QCOMPARE(camera.row, 3);

        QCOMPARE(document->project().entities.size(), 4);
        QCOMPARE(commitCount(window.get()), commitsBefore + 4);
    }

    void createEntityRowsAndPrivateParent()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();
        document->setPlayhead(0);

        window->createEntity(ContentKind::Empty);
        const Uuid parentUuid = document->selection();
        QVERIFY(!parentUuid.isEmpty());
        QCOMPARE(document->project().entities.value(parentUuid).row, 0);

        window->createEntity(ContentKind::Text);
        const Entity sibling =
            document->project().entities.value(document->selection());
        QCOMPARE(sibling.row, 1);
        QVERIFY(sibling.parentUuid.isEmpty());
        QCOMPARE(sibling.start, 0);
        QCOMPARE(sibling.end, 24);

        window->timelineForTest()->setParentUuid(parentUuid);
        window->createEntity(ContentKind::Camera);
        const Entity child =
            document->project().entities.value(document->selection());
        QCOMPARE(child.parentUuid, parentUuid);
        QCOMPARE(child.row, 0);
        QCOMPARE(child.start, 0);
        QCOMPARE(child.end, 24);
    }

    void createEntityClampsToPrivateParent()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();

        Entity parent;
        parent.uuid = model::newUuid();
        parent.name = QStringLiteral("父");
        parent.start = 10;
        parent.end = 20;
        document->mutableProject().entities.insert(parent.uuid, parent);
        QVERIFY2(document->commitEntity(parent.uuid, QStringLiteral("pvf: create parent"),
                                        &error),
                 qPrintable(error));

        window->timelineForTest()->setParentUuid(parent.uuid);

        document->setPlayhead(30);
        window->createEntity(ContentKind::Text);
        const Entity clampedHigh =
            document->project().entities.value(document->selection());
        QCOMPARE(clampedHigh.parentUuid, parent.uuid);
        QCOMPARE(clampedHigh.start, 19);
        QCOMPARE(clampedHigh.end, 20);

        document->setPlayhead(5);
        window->createEntity(ContentKind::Text);
        const Entity clampedLow =
            document->project().entities.value(document->selection());
        QCOMPARE(clampedLow.parentUuid, parent.uuid);
        QCOMPARE(clampedLow.start, 10);
        QCOMPARE(clampedLow.end, 20);

        Entity oneFrame;
        oneFrame.uuid = model::newUuid();
        oneFrame.name = QStringLiteral("单帧父");
        oneFrame.start = 10;
        oneFrame.end = 11;
        document->mutableProject().entities.insert(oneFrame.uuid, oneFrame);
        QVERIFY2(document->commitEntity(oneFrame.uuid,
                                        QStringLiteral("pvf: create one-frame parent"),
                                        &error),
                 qPrintable(error));

        window->timelineForTest()->setParentUuid(oneFrame.uuid);
        document->setPlayhead(10);
        window->createEntity(ContentKind::Empty);
        const Entity oneFrameChild =
            document->project().entities.value(document->selection());
        QCOMPARE(oneFrameChild.parentUuid, oneFrame.uuid);
        QCOMPARE(oneFrameChild.start, 10);
        QCOMPARE(oneFrameChild.end, 11);
    }

    void createEntityRollsBackWhenCommitFails()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();
        document->setPlayhead(5);
        const Uuid previousSelection = document->selection();
        const int previousEntityCount = document->project().entities.size();
        QVERIFY(previousSelection.isEmpty());

        QVERIFY(QDir(document->root()).rename(QStringLiteral(".git"),
                                              QStringLiteral(".git-disabled")));

        dismissNextMessageBox();
        window->createEntity(ContentKind::Empty);

        QCOMPARE(document->project().entities.size(), previousEntityCount);
        QVERIFY(document->selection().isEmpty());
        QCOMPARE(document->playhead(), 5);
    }

    void createActionsDisabledWithoutDocument()
    {
        NextWindow window(nullptr);
        for (ContentKind kind : {ContentKind::Empty, ContentKind::Text,
                                 ContentKind::Vector, ContentKind::Camera}) {
            QAction* action = window.createEntityActionForTest(kind);
            QVERIFY(action != nullptr);
            QVERIFY(!action->isEnabled());
        }
    }

    void privateTimelineOverlayOpensAndCloses()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();

        Entity parent;
        parent.uuid = model::newUuid();
        parent.name = QStringLiteral("父");
        parent.start = 0;
        parent.end = 24;
        document->mutableProject().entities.insert(parent.uuid, parent);
        QVERIFY2(document->commitEntity(parent.uuid, QStringLiteral("pvf: create parent"),
                                        &error),
                 qPrintable(error));

        Entity child;
        child.uuid = model::newUuid();
        child.name = QStringLiteral("子");
        child.parentUuid = parent.uuid;
        child.start = 0;
        child.end = 24;
        document->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(document->commitEntity(child.uuid, QStringLiteral("pvf: create child"),
                                        &error),
                 qPrintable(error));

        QVERIFY(window->timelineForTest()->parentUuid().isEmpty());

        window->openPrivateTimelineForTest(parent.uuid);
        QCoreApplication::processEvents();

        QCOMPARE(window->privateTimelineCountForTest(), 1);
        QDialog* dialog = window->privateTimelineDialogForTest(parent.uuid);
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->windowTitle().contains(QStringLiteral("私有时间线")));
        QVERIFY(dialog->windowTitle().contains(QStringLiteral("父")));

        auto* overlay = dialog->findChild<TimelineWidget*>();
        QVERIFY(overlay != nullptr);
        QCOMPARE(overlay->parentUuid(), parent.uuid);
        QVERIFY(window->timelineForTest()->parentUuid().isEmpty());

        window->openPrivateTimelineForTest(parent.uuid);
        QCOMPARE(window->privateTimelineCountForTest(), 1);
        QCOMPARE(window->privateTimelineDialogForTest(parent.uuid), dialog);

        QVERIFY(overlay->visibleEntities().contains(child.uuid));
        const QRect childBar = overlay->entityBarRect(child.uuid);
        QVERIFY(childBar.isValid());
        QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, childBar.center());
        QCOMPARE(document->selection(), child.uuid);

        QVERIFY2(document->store()->deleteEntity(parent.uuid, &error), qPrintable(error));
        QVERIFY2(document->commitProject(QStringLiteral("pvf: delete parent"), &error),
                 qPrintable(error));
        QVERIFY(!dialog->isVisible());
        QTRY_COMPARE(window->privateTimelineCountForTest(), 0);
        QVERIFY(window->privateTimelineDialogForTest(parent.uuid) == nullptr);
        QVERIFY(window->timelineForTest()->parentUuid().isEmpty());
    }

    void exportActionAndDialog()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QAction* exportAction = window->exportActionForTest();
        QVERIFY(exportAction != nullptr);
        QVERIFY(!exportAction->isEnabled());

        window->createEntity(ContentKind::Camera);
        QVERIFY(exportAction->isEnabled());

        const Uuid cameraUuid = window->document()->selection();
        QVERIFY(!cameraUuid.isEmpty());
        Entity camera = window->document()->project().entities.value(cameraUuid);
        camera.start = 12;
        camera.end = 48;
        window->document()->mutableProject().entities.insert(cameraUuid, camera);
        QVERIFY2(window->document()->commitEntity(
                     cameraUuid, QStringLiteral("pvf: trim camera"), &error),
                 qPrintable(error));

        ExportDialog dialog(window->document()->project(), cameraUuid);
        QCOMPARE(dialog.cameraUuid(), cameraUuid);
        QCOMPARE(dialog.startFrame(), 12);
        QCOMPARE(dialog.endFrame(), 48);
        QVERIFY(dialog.includeAudio());
        QVERIFY(dialog.cameraComboBoxForTest() != nullptr);
        QCOMPARE(dialog.cameraComboBoxForTest()->count(), 1);
    }

    void mcpServerStartsAndReportsViewState()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString projectDir = temp.path() + QStringLiteral("/mcp.pvproj");
        QString error;

        {
            ProjectSettings settings;
            settings.name = QStringLiteral("MCP");
            settings.width = 640;
            settings.height = 360;
            settings.fps = 24;
            auto document = ProjectDocument::create(projectDir, settings, &error);
            QVERIFY2(document != nullptr, qPrintable(error));
        }

        const QString configPath = temp.path() + QStringLiteral("/mcp.json");
        QJsonObject config;
        config.insert(QStringLiteral("port"), 0);
        config.insert(QStringLiteral("token"), QStringLiteral("test-token"));
        QFile configFile(configPath);
        QVERIFY(configFile.open(QIODevice::WriteOnly));
        configFile.write(QJsonDocument(config).toJson());
        configFile.close();

        NextWindow window(nullptr);
        window.setMcpConfigPathForTest(configPath);
        QVERIFY2(window.openProject(projectDir, &error), qPrintable(error));

        QVERIFY(window.mcpServiceForTest() != nullptr);
        mcp2::HttpServer* server = window.mcpServerForTest();
        QVERIFY(server != nullptr);
        QVERIFY(server->isRunning());
        QVERIFY(server->port() > 0);
        QCOMPARE(server->token(), QByteArray("test-token"));

        window.document()->setPlayhead(9);
        window.createEntity(ContentKind::Camera);
        const Uuid selected = window.document()->selection();
        QVERIFY(!selected.isEmpty());

        const QJsonObject result =
            window.mcpServiceForTest()->execute(QStringLiteral("view.state"), {});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("playhead")).toInt(), 9);
        QCOMPARE(data.value(QStringLiteral("selection")).toString(), selected);
        QVERIFY(data.value(QStringLiteral("timelineParent")).toString().isEmpty());
        QVERIFY(data.value(QStringLiteral("cameraUuid")).toString().isEmpty());

        QPointer<mcp2::HttpServer> oldServer(server);
        ProjectSettings secondSettings;
        secondSettings.name = QStringLiteral("MCP2");
        auto secondDocument = ProjectDocument::create(
            temp.path() + QStringLiteral("/mcp2.pvproj"), secondSettings, &error);
        QVERIFY2(secondDocument != nullptr, qPrintable(error));
        window.setDocument(std::move(secondDocument));
        QVERIFY(oldServer.isNull());
        QVERIFY(window.mcpServiceForTest() != nullptr);
        QVERIFY(window.mcpServerForTest() != nullptr);
        QVERIFY(window.mcpServerForTest()->isRunning());

        window.setDocument(nullptr);
        QVERIFY(window.mcpServerForTest() == nullptr);
        QVERIFY(window.mcpServiceForTest() == nullptr);
    }

    void playbackEmptyProjectIsNoOp()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QCOMPARE(window->document()->playhead(), 0);
        window->togglePlaybackForTest();
        QVERIFY(!window->isPlayingForTest());
        QTest::qWait(100);
        QCOMPARE(window->document()->playhead(), 0);
    }

    void playbackAdvancesAndPauses()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        Entity entity;
        entity.uuid = model::newUuid();
        entity.name = QStringLiteral("十帧");
        entity.start = 0;
        entity.end = 10;
        window->document()->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(window->document()->commitEntity(
                     entity.uuid, QStringLiteral("pvf: create ten frames"), &error),
                 qPrintable(error));

        window->document()->setPlayhead(0);
        window->togglePlaybackForTest();
        QVERIFY(window->isPlayingForTest());

        QTest::qWait(300);
        const int advanced = window->document()->playhead();
        QVERIFY(advanced > 0);
        QVERIFY(advanced <= 10);

        window->togglePlaybackForTest();
        QVERIFY(!window->isPlayingForTest());
        const int paused = window->document()->playhead();
        QTest::qWait(150);
        QCOMPARE(window->document()->playhead(), paused);
    }

    void playbackStopsAtContentEnd()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        Entity entity;
        entity.uuid = model::newUuid();
        entity.name = QStringLiteral("末帧");
        entity.start = 0;
        entity.end = 10;
        window->document()->mutableProject().entities.insert(entity.uuid, entity);
        QVERIFY2(window->document()->commitEntity(
                     entity.uuid, QStringLiteral("pvf: create end marker"), &error),
                 qPrintable(error));

        window->document()->setPlayhead(0);
        window->togglePlaybackForTest();
        QVERIFY(window->isPlayingForTest());
        QTRY_VERIFY(!window->isPlayingForTest());
        QCOMPARE(window->document()->playhead(), 10);
    }

    void deleteCascadesSelectedSubtreeInOneCommit()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        Entity parent;
        parent.uuid = model::newUuid();
        parent.name = QStringLiteral("父");
        parent.start = 0;
        parent.end = 60;

        Entity child;
        child.uuid = model::newUuid();
        child.name = QStringLiteral("子");
        child.parentUuid = parent.uuid;
        child.start = 0;
        child.end = 30;

        window->document()->mutableProject().entities.insert(parent.uuid, parent);
        window->document()->mutableProject().entities.insert(child.uuid, child);
        QVERIFY2(window->document()->commitEntities(
                     {parent.uuid, child.uuid}, QStringLiteral("pvf: create tree"),
                     &error),
                 qPrintable(error));

        window->document()->setSelection(parent.uuid);
        QVERIFY(window->deleteActionForTest()->isEnabled());

        const int commitsBefore = commitCount(window.get());
        QVERIFY2(window->deleteSelectedEntityForTest(true, &error),
                 qPrintable(error));
        QVERIFY(!window->document()->project().entities.contains(parent.uuid));
        QVERIFY(!window->document()->project().entities.contains(child.uuid));
        QVERIFY(window->document()->selection().isEmpty());
        QCOMPARE(commitCount(window.get()), commitsBefore + 1);
    }

    void deleteLeafRemovesOnlyItself()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        Entity leaf;
        leaf.uuid = model::newUuid();
        leaf.name = QStringLiteral("叶");
        leaf.start = 0;
        leaf.end = 10;
        window->document()->mutableProject().entities.insert(leaf.uuid, leaf);
        QVERIFY2(window->document()->commitEntity(
                     leaf.uuid, QStringLiteral("pvf: create leaf"), &error),
                 qPrintable(error));

        window->document()->setSelection(leaf.uuid);
        QVERIFY2(window->deleteSelectedEntityForTest(false, &error),
                 qPrintable(error));
        QVERIFY(!window->document()->project().entities.contains(leaf.uuid));
        QVERIFY(window->document()->selection().isEmpty());
    }

    void deleteLockedEntityIsRejected()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        Entity locked;
        locked.uuid = model::newUuid();
        locked.name = QStringLiteral("锁定的");
        locked.start = 0;
        locked.end = 10;
        locked.locked = true;
        window->document()->mutableProject().entities.insert(locked.uuid, locked);
        QVERIFY2(window->document()->commitEntity(
                     locked.uuid, QStringLiteral("pvf: create locked"), &error),
                 qPrintable(error));

        window->document()->setSelection(locked.uuid);
        QVERIFY(!window->deleteActionForTest()->isEnabled());

        QString deleteError;
        QVERIFY(!window->deleteSelectedEntityForTest(true, &deleteError));
        QVERIFY(!deleteError.isEmpty());
        QVERIFY(window->document()->project().entities.contains(locked.uuid));
    }

    void autoKeyDefaultsOnAndResetsAfterProjectSwitch()
    {
        if (!gitLfsAvailable())
            QSKIP("git-lfs is not available; store creation requires it");

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QCheckBox* check = window->autoKeyCheckBoxForTest();
        QVERIFY(check != nullptr);
        QVERIFY(check->isChecked());
        QVERIFY(window->propertiesPanelForTest()->autoKey());

        check->setChecked(false);
        QVERIFY(!window->propertiesPanelForTest()->autoKey());

        ProjectSettings settings;
        settings.name = QStringLiteral("Switch");
        auto second = ProjectDocument::create(
            temp.path() + QStringLiteral("/switch.pvproj"), settings, &error);
        QVERIFY2(second != nullptr, qPrintable(error));
        window->setDocument(std::move(second));

        QVERIFY(window->autoKeyCheckBoxForTest()->isChecked());
        QVERIFY(window->propertiesPanelForTest()->autoKey());
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
