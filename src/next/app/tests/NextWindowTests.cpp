#include "next/app/NextWindow.h"

#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"
#include "next/app/ExportDialog.h"
#include "next/app/ProjectDocument.h"
#include "next/assets/AssetPanel.h"
#include "next/history/HistoryPanel.h"
#include "next/mcp/McpLogPanel.h"
#include "next/properties/PropertiesPanel.h"
#include "next/settings/OnboardingDialog.h"
#include "next/settings/SettingsDialog.h"
#include "next/timeline/TimelineWidget.h"
#include "next/viewport/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

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

struct CrashFixture
{
    QString projectDir;
    Entity entity;
};

// Creates a project with one committed entity and destroys the document, so
// the caller can simulate a crash by editing the entity file on disk.
bool prepareProjectWithEntity(QTemporaryDir& temp, const QString& name,
                              CrashFixture* fixture, QString* error)
{
    ProjectSettings settings;
    settings.name = name;
    settings.width = 640;
    settings.height = 360;
    settings.fps = 24;

    fixture->projectDir =
        temp.path() + QLatin1Char('/') + name + QStringLiteral(".pvproj");
    auto document = ProjectDocument::create(fixture->projectDir, settings, error);
    if (!document)
        return false;

    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("原始名字");
    entity.start = 0;
    entity.end = 24;
    fixture->entity = entity;
    document->mutableProject().entities.insert(entity.uuid, entity);
    if (!document->commitEntity(entity.uuid,
                                QStringLiteral("pvf: create entity"), error))
        return false;
    document->flush(error);
    return error->isEmpty();
}

bool rewriteEntityName(const QString& projectDir, const Uuid& uuid,
                       const QString& name)
{
    const QString path = projectDir + QStringLiteral("/entities/") + uuid
                         + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    object.insert(QStringLiteral("name"), name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.flush();
}

// Recent projects live in the same PVForge/PVForge QSettings file as the app
// preferences; XDG_CONFIG_HOME isolates it per test binary.
void clearRecentProjectsSetting()
{
    QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
    settings.remove(QStringLiteral("recent/projects"));
    settings.sync();
}

QStringList storedRecentProjects()
{
    QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
    return settings.value(QStringLiteral("recent/projects")).toStringList();
}

void writeRecentProjectsSetting(const QStringList& paths)
{
    QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
    settings.setValue(QStringLiteral("recent/projects"), paths);
    settings.sync();
}

// Creates a project directory on disk and returns its absolute path (empty on
// failure).
QString createRecentProject(const QString& projectDir, const QString& name)
{
    ProjectSettings settings;
    settings.name = name;
    settings.width = 640;
    settings.height = 360;
    settings.fps = 24;

    QString error;
    auto document = ProjectDocument::create(projectDir, settings, &error);
    if (!document)
        return QString();
    return document->root();
}

// Writes an ephemeral-port MCP config so a window never races for the
// production port during tests.
QString writeMcpConfig(QTemporaryDir& temp)
{
    const QString configPath =
        temp.path() + QStringLiteral("/mcp.json");
    QJsonObject config;
    config.insert(QStringLiteral("port"), 0);
    config.insert(QStringLiteral("token"), QStringLiteral("test-token"));
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(QJsonDocument(config).toJson());
    return configPath;
}

} // namespace

class NextWindowTests : public QObject
{
    Q_OBJECT

private slots:
    // QSettings outlives one test function; start every case with no recents,
    // no first-run onboarding dialog and Git enabled, which the crash-recovery
    // and history cases rely on.
    void init()
    {
        clearRecentProjectsSetting();
        QSettings settings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
        settings.setValue(QStringLiteral("app/onboardingDone"), true);
        settings.setValue(QStringLiteral("git/enabled"), true);
        settings.sync();
    }

    void shellExposesPanels()
    {
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
        // Four rapid human operations coalesce into one debounced commit.
        QCOMPARE(commitCount(window.get()), commitsBefore);
        QTRY_COMPARE_WITH_TIMEOUT(commitCount(window.get()), commitsBefore + 1,
                                  3000);
    }

    void createEntityRowsAndPrivateParent()
    {
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

    void createEntityKeepsSaveWhenCommitFails()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        ProjectDocument* document = window->document();
        document->setPlayhead(5);
        QVERIFY(document->selection().isEmpty());

        QDir root(document->root());
        QVERIFY(root.rename(QStringLiteral(".git"),
                            QStringLiteral(".git-disabled")));

        QSignalSpy failedSpy(document, &Document::commitFailed);
        window->createEntity(ContentKind::Empty);

        // The entity is saved; only the git cold backup fails later.
        QCOMPARE(document->project().entities.size(), 1);
        QVERIFY(!document->selection().isEmpty());
        QCOMPARE(document->playhead(), 5);

        error.clear();
        document->flush(&error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(document->hasPendingCommit());

        QVERIFY(root.rename(QStringLiteral(".git-disabled"),
                            QStringLiteral(".git")));
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

        QComboBox* resolution = dialog.resolutionComboBoxForTest();
        QVERIFY(resolution != nullptr);
        QCOMPARE(resolution->count(), 3);
        QCOMPARE(dialog.resolutionScale(), 1);
        QCOMPARE(resolution->currentText(), QStringLiteral("640 × 360（1x）"));
        resolution->setCurrentIndex(2);
        QCOMPARE(dialog.resolutionScale(), 4);
        QCOMPARE(resolution->currentText(), QStringLiteral("2560 × 1440（4x）"));

        QComboBox* fps = dialog.fpsComboBoxForTest();
        QVERIFY(fps != nullptr);
        QCOMPARE(fps->count(), 3);
        QCOMPARE(dialog.outputFps(), 24);
        QCOMPARE(fps->currentText(), QStringLiteral("24（1x）"));
        fps->setCurrentIndex(1);
        QCOMPARE(dialog.outputFps(), 48);
        QCOMPARE(fps->currentText(), QStringLiteral("48（2x）"));
        fps->setCurrentIndex(2);
        QCOMPARE(dialog.outputFps(), 96);
        QCOMPARE(fps->currentText(), QStringLiteral("96（4x）"));

        // A 1280x720 project at 4x is 5120x2880: the dialog asks before
        // exporting.  The test hook skips the modal warning.
        Project wide = window->document()->project();
        wide.settings.width = 1280;
        wide.settings.height = 720;
        ExportDialog wideDialog(wide, cameraUuid);
        wideDialog.setConfirmLargeExportForTest(true);
        wideDialog.resolutionComboBoxForTest()->setCurrentIndex(2);
        QCOMPARE(wideDialog.resolutionScale(), 4);
        auto* buttons = wideDialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons != nullptr);
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),
                          Qt::LeftButton);
        QCOMPARE(wideDialog.result(), int(QDialog::Accepted));
    }

    void mcpServerStartsAndReportsViewState()
    {
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

    void gitDisabledProjectOpensReadOnly()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        ProjectSettings settings;
        settings.name = QStringLiteral("Plain");
        settings.width = 640;
        settings.height = 360;
        settings.fps = 24;

        const QString projectDir =
            temp.path() + QStringLiteral("/plain.pvproj");
        QString error;
        auto document =
            ProjectDocument::create(projectDir, settings, &error, false);
        QVERIFY2(document != nullptr, qPrintable(error));
        QVERIFY(!document->store()->gitEnabled());

        const QString configPath = writeMcpConfig(temp);
        QVERIFY(!configPath.isEmpty());

        NextWindow window(nullptr);
        window.setMcpConfigPathForTest(configPath);
        QVERIFY2(window.openProject(projectDir, &error), qPrintable(error));

        QVERIFY(!window.document()->store()->gitEnabled());
        QVERIFY(window.mcpServiceForTest() != nullptr);
        QVERIFY(!window.mcpServiceForTest()->writesEnabled());
        QVERIFY(window.mcpServerForTest() != nullptr);
        QVERIFY(window.mcpServerForTest()->isRunning());
        const QJsonArray tools =
            window.mcpServiceForTest()->listTools()
                .value(QStringLiteral("tools")).toArray();
        QCOMPARE(tools.size(), 46);

        const QJsonObject create = window.mcpServiceForTest()->execute(
            QStringLiteral("entity.create"),
            QJsonObject{{QStringLiteral("name"), QStringLiteral("标题")}});
        QVERIFY(!create.value(QStringLiteral("ok")).toBool());
        const QJsonObject createError =
            create.value(QStringLiteral("error")).toObject();
        QCOMPARE(createError.value(QStringLiteral("code")).toString(),
                 QStringLiteral("permission_denied"));
        QCOMPARE(createError.value(QStringLiteral("details")).toObject()
                     .value(QStringLiteral("reason")).toString(),
                 QStringLiteral("git_disabled"));

        const QJsonObject get = window.mcpServiceForTest()->execute(
            QStringLiteral("project.get"), {});
        QVERIFY(get.value(QStringLiteral("ok")).toBool());

        HistoryPanel* history = window.historyPanelForTest();
        QVERIFY(history != nullptr);
        QCOMPARE(history->rowCountForTest(), 0);
        auto* status =
            history->findChild<QLabel*>(QStringLiteral("historyStatusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->text(), QStringLiteral("Git 未启用"));
    }

    void settingsToggleEnablesAndDisablesGit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        {
            QSettings settings(QStringLiteral("PVForge"),
                               QStringLiteral("PVForge"));
            settings.setValue(QStringLiteral("git/enabled"), false);
            settings.sync();
        }

        ProjectSettings projectSettings;
        projectSettings.name = QStringLiteral("Toggle");
        projectSettings.width = 640;
        projectSettings.height = 360;
        projectSettings.fps = 24;

        QString error;
        const QString projectDir =
            temp.path() + QStringLiteral("/toggle.pvproj");
        auto document =
            ProjectDocument::create(projectDir, projectSettings, &error, false);
        QVERIFY2(document != nullptr, qPrintable(error));
        QVERIFY(!document->store()->gitEnabled());

        NextWindow window(std::move(document));
        QVERIFY(window.mcpServiceForTest() != nullptr);
        QVERIFY(!window.mcpServiceForTest()->writesEnabled());

        AppSettings settings = window.settingsForTest();
        settings.onboardingDone = true;
        settings.gitEnabled = true;
        window.setSettingsForTest(settings);

        QVERIFY(window.document()->store()->gitEnabled());
        QVERIFY(window.mcpServiceForTest()->writesEnabled());
        HistoryPanel* history = window.historyPanelForTest();
        QVERIFY(history != nullptr);
        QVERIFY(history->rowCountForTest() > 0);
        auto* status =
            history->findChild<QLabel*>(QStringLiteral("historyStatusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(!status->text().isEmpty());

        settings.gitEnabled = false;
        window.setSettingsForTest(settings);
        QVERIFY(!window.document()->store()->gitEnabled());
        QVERIFY(!window.mcpServiceForTest()->writesEnabled());
        QCOMPARE(history->rowCountForTest(), 0);
        QCOMPARE(status->text(), QStringLiteral("Git 未启用"));
    }

    void historyDockShowsCommits()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QVERIFY(window->historyDockForTest() != nullptr);
        QVERIFY(window->historyPanelForTest() != nullptr);
        QVERIFY(window->historyActionForTest() != nullptr);
        QVERIFY(window->historyDockForTest()->isHidden());
        QVERIFY(window->historyPanelForTest()->rowCountForTest() > 0);
    }

    void historyRestoreReturnsToCommit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QVector<store::GitCommit> commits =
            window->document()->store()->history(200, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!commits.isEmpty());
        const QString initial = commits.last().hash;

        window->createEntity(ContentKind::Empty);
        window->document()->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(window->document()->project().entities.size(), 1);
        const int afterEdit =
            window->document()->store()->history(200, &error).size();
        QVERIFY(afterEdit > commits.size());

        QVERIFY2(window->restoreCommitForTest(initial, &error),
                 qPrintable(error));
        QCOMPARE(window->document()->project().entities.size(), 0);
        QCOMPARE(window->document()->store()->history(200, &error).size(),
                 afterEdit + 1);
        QVERIFY(window->historyPanelForTest()->rowCountForTest() > 0);
    }

    void onboardingSkippedWhenDone()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        QVERIFY(!window->onboardingShownForTest());
        QVERIFY(window->findChild<OnboardingDialog*>() == nullptr);
    }

    void onboardingPromptOnceSavesChoice()
    {
        {
            QSettings settings(QStringLiteral("PVForge"),
                               QStringLiteral("PVForge"));
            settings.setValue(QStringLiteral("app/onboardingDone"), false);
            settings.setValue(QStringLiteral("git/enabled"), false);
            settings.sync();
        }

        NextWindow window(nullptr);
        QVERIFY(window.onboardingShownForTest());
        auto* dialog = window.findChild<OnboardingDialog*>();
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->gitEnabled());

        auto* start =
            dialog->findChild<QPushButton*>(QStringLiteral("onboardingStart"));
        QVERIFY(start != nullptr);
        start->click();

        QVERIFY(window.settingsForTest().onboardingDone);
        QVERIFY(!window.settingsForTest().gitEnabled);
        {
            QSettings settings(QStringLiteral("PVForge"),
                               QStringLiteral("PVForge"));
            QVERIFY(settings.value(QStringLiteral("app/onboardingDone")).toBool());
        }

        // The next launch does not ask again.
        NextWindow second(nullptr);
        QVERIFY(!second.onboardingShownForTest());
        QVERIFY(second.findChild<OnboardingDialog*>() == nullptr);
    }

    void exportOpenFolderHandlerIsUsed()
    {
        NextWindow window(nullptr);
        QString opened;
        window.setOpenFolderHandlerForTest(
            [&opened](const QString& path) { opened = path; });
        window.openFolderForTest(QStringLiteral("/tmp/pvforge-export"));
        QCOMPARE(opened, QStringLiteral("/tmp/pvforge-export"));
    }

    void playbackEmptyProjectIsNoOp()
    {
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
        window->document()->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        window->document()->setSelection(parent.uuid);
        QVERIFY(window->deleteActionForTest()->isEnabled());

        const int commitsBefore = commitCount(window.get());
        QVERIFY2(window->deleteSelectedEntityForTest(true, &error),
                 qPrintable(error));
        window->document()->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!window->document()->project().entities.contains(parent.uuid));
        QVERIFY(!window->document()->project().entities.contains(child.uuid));
        QVERIFY(window->document()->selection().isEmpty());
        QCOMPARE(commitCount(window.get()), commitsBefore + 1);
    }

    void deleteLeafRemovesOnlyItself()
    {
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
        window->document()->flush(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
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
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        window->show();
        QVERIFY(window->close());
        QCoreApplication::processEvents();
    }

    void switchingProjectFlushesPendingCommit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        const QString oldDir = window->document()->root();
        const int before = commitCount(window.get());

        window->createEntity(ContentKind::Text);
        QVERIFY(window->document()->hasPendingCommit());
        QCOMPARE(commitCount(window.get()), before);

        ProjectSettings settings;
        settings.name = QStringLiteral("Second");
        auto second = ProjectDocument::create(
            temp.path() + QStringLiteral("/second.pvproj"), settings, &error);
        QVERIFY2(second != nullptr, qPrintable(error));
        window->setDocument(std::move(second));

        auto reopened = store::ProjectStore::open(oldDir, &error);
        QVERIFY2(reopened.has_value(), qPrintable(error));
        QCOMPARE(reopened->history(100).size(), before + 1);
        QVERIFY(!reopened->project().entities.isEmpty());
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

    void cleanProjectSkipsCrashRecoveryHandler()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("clean"),
                                          &fixture, &error),
                 qPrintable(error));

        NextWindow window(nullptr);
        int calls = 0;
        window.setCrashRecoveryHandlerForTest(
            [&calls](const QVector<store::DirtyText>&) {
                ++calls;
                return CrashRecoveryChoice::Restore;
            });

        QVERIFY2(window.openProject(fixture.projectDir, &error),
                 qPrintable(error));
        QCOMPARE(calls, 0);
        QVERIFY(window.document() != nullptr);
        QVERIFY(window.document()->store()->dirtyText().isEmpty());
    }

    void crashRecoveryRestoreOpensDirtyDiskContent()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("restore"),
                                          &fixture, &error),
                 qPrintable(error));
        QVERIFY(rewriteEntityName(fixture.projectDir, fixture.entity.uuid,
                                  QStringLiteral("崩溃后的名字")));

        const QString expectedPath =
            QStringLiteral("entities/") + fixture.entity.uuid
            + QStringLiteral(".json");

        NextWindow window(nullptr);
        QVector<store::DirtyText> seen;
        window.setCrashRecoveryHandlerForTest(
            [&seen](const QVector<store::DirtyText>& dirty) {
                seen = dirty;
                return CrashRecoveryChoice::Restore;
            });

        QVERIFY2(window.openProject(fixture.projectDir, &error),
                 qPrintable(error));
        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen.first().path, expectedPath);
        QCOMPARE(seen.first().status, QStringLiteral("modified"));
        QVERIFY(window.document() != nullptr);
        QCOMPARE(window.document()
                     ->project()
                     .entities.value(fixture.entity.uuid)
                     .name,
                 QStringLiteral("崩溃后的名字"));
        QCOMPARE(window.document()->store()->dirtyText().size(), 1);
    }

    void crashRecoveryDiscardRestoresCommittedText()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("discard"),
                                          &fixture, &error),
                 qPrintable(error));
        QVERIFY(rewriteEntityName(fixture.projectDir, fixture.entity.uuid,
                                  QStringLiteral("要丢弃的名字")));

        NextWindow window(nullptr);
        window.setCrashRecoveryHandlerForTest(
            [](const QVector<store::DirtyText>&) {
                return CrashRecoveryChoice::Discard;
            });

        QVERIFY2(window.openProject(fixture.projectDir, &error),
                 qPrintable(error));
        QVERIFY(window.document() != nullptr);
        QCOMPARE(window.document()
                     ->project()
                     .entities.value(fixture.entity.uuid)
                     .name,
                 QStringLiteral("原始名字"));
        QVERIFY(window.document()->store()->dirtyText().isEmpty());
    }

    void crashRecoveryCommitCreatesRecoverCommit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("commit"),
                                          &fixture, &error),
                 qPrintable(error));
        QVERIFY(rewriteEntityName(fixture.projectDir, fixture.entity.uuid,
                                  QStringLiteral("恢复点名字")));

        NextWindow window(nullptr);
        window.setCrashRecoveryHandlerForTest(
            [](const QVector<store::DirtyText>&) {
                return CrashRecoveryChoice::Commit;
            });

        QVERIFY2(window.openProject(fixture.projectDir, &error),
                 qPrintable(error));
        QVERIFY(window.document() != nullptr);
        QCOMPARE(window.document()
                     ->project()
                     .entities.value(fixture.entity.uuid)
                     .name,
                 QStringLiteral("恢复点名字"));
        QVERIFY(window.document()->store()->dirtyText().isEmpty());

        const QVector<store::GitCommit> history =
            window.document()->store()->history(1, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!history.isEmpty());
        QCOMPARE(history.first().message, QStringLiteral("pvf: recover"));
    }

    void crashRecoveryCancelLeavesProjectClosed()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("cancel"),
                                          &fixture, &error),
                 qPrintable(error));
        QVERIFY(rewriteEntityName(fixture.projectDir, fixture.entity.uuid,
                                  QStringLiteral("取消的名字")));

        NextWindow window(nullptr);
        int calls = 0;
        window.setCrashRecoveryHandlerForTest(
            [&calls](const QVector<store::DirtyText>&) {
                ++calls;
                return CrashRecoveryChoice::Cancel;
            });

        error.clear();
        QVERIFY(!window.openProject(fixture.projectDir, &error));
        QCOMPARE(calls, 1);
        QVERIFY(window.document() == nullptr);
        QVERIFY(error.isEmpty() || error == QStringLiteral("已取消"));
    }

    void videoExtractionUsesProxyWidthSetting()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CrashFixture fixture;
        QString error;
        QVERIFY2(prepareProjectWithEntity(temp, QStringLiteral("extraction"),
                                          &fixture, &error),
                 qPrintable(error));

        NextWindow window(nullptr);
        AppSettings settings = window.settingsForTest();
        settings.videoProxyWidth = 320;
        window.setSettingsForTest(settings);

        QVERIFY2(window.openProject(fixture.projectDir, &error),
                 qPrintable(error));
        QVERIFY(window.assetPanelForTest() != nullptr);
        QCOMPARE(window.assetPanelForTest()->videoExtractionWidth(), 320);

        // Applying a new width updates the option for later imports.
        settings.videoProxyWidth = 1280;
        window.setSettingsForTest(settings);
        QCOMPARE(window.assetPanelForTest()->videoExtractionWidth(), 1280);
    }

    void settingsDialogAppliesAndPersists()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));

        const AppSettings original = window->settingsForTest();
        QAction* settingsAction = window->settingsActionForTest();
        QVERIFY(settingsAction != nullptr);
        settingsAction->trigger();

        QTRY_VERIFY(window->settingsDialogForTest() != nullptr);
        SettingsDialog* dialog = window->settingsDialogForTest();
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->isModal());

        auto* delay = dialog->findChild<QSpinBox*>(QStringLiteral("humanCommitDelayMs"));
        QVERIFY(delay != nullptr);
        const int wanted =
            original.humanCommitDelayMs == 2500 ? 2300 : 2500;
        delay->setValue(wanted);

        auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("applyButton"));
        QVERIFY(apply != nullptr);
        apply->click();

        QCOMPARE(window->settingsForTest().humanCommitDelayMs, wanted);
        QCOMPARE(window->document()->humanCommitDelayMs(), wanted);

        // Put the shared QSettings file back so other test cases keep the
        // documented 1 s debounce.
        delay->setValue(original.humanCommitDelayMs);
        apply->click();
        QCOMPARE(window->settingsForTest().humanCommitDelayMs,
                 original.humanCommitDelayMs);

        dialog->close();
        QTRY_VERIFY(window->settingsDialogForTest() == nullptr);
    }

    void mcpLogDockTracksToolCallsAndEmergencyStop()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        auto window = makeWindow(temp, &error);
        QVERIFY2(window != nullptr, qPrintable(error));
        window->show();

        McpLogPanel* panel = window->mcpLogPanelForTest();
        QDockWidget* dock = window->mcpLogDockForTest();
        QAction* logAction = window->mcpLogActionForTest();
        mcp2::HttpServer* server = window->mcpServerForTest();
        QVERIFY(panel != nullptr);
        QVERIFY(dock != nullptr);
        QVERIFY(logAction != nullptr);
        QVERIFY(server != nullptr);
        QVERIFY(dock->isHidden());

        const int before = panel->entryCountForTest();

        // A write tool locks every editing panel except the log itself.
        server->toolCallStarted(QStringLiteral("entity.create"),
                                QStringLiteral("{\"name\":\"标题\"}"), true);
        QVERIFY(!window->assetPanelForTest()->isEnabled());
        QVERIFY(!window->timelineForTest()->isEnabled());
        QVERIFY(!window->propertiesPanelForTest()->isEnabled());
        QVERIFY(!window->viewportForTest()->isEnabled());
        QVERIFY(panel->isEnabled());
        QVERIFY(!dock->isHidden());
        QVERIFY(logAction->isChecked());
        auto* statusAi = window->findChild<QLabel*>(QStringLiteral("statusAi"));
        QVERIFY(statusAi != nullptr);
        QVERIFY(statusAi->isVisible());
        QCOMPARE(statusAi->text(), QStringLiteral("AI 修改中…"));

        server->toolCallFinished(QStringLiteral("entity.create"),
                                 QStringLiteral("{\"ok\":true}"), 12, true);
        QVERIFY(window->assetPanelForTest()->isEnabled());
        QVERIFY(window->timelineForTest()->isEnabled());
        QVERIFY(window->propertiesPanelForTest()->isEnabled());
        QVERIFY(window->viewportForTest()->isEnabled());
        QVERIFY(statusAi->isHidden());
        QVERIFY(panel->entryCountForTest() > before);

        // Read-only tools never lock the editor.
        server->toolCallStarted(QStringLiteral("view.state"),
                                QStringLiteral("{}"), false);
        QVERIFY(window->viewportForTest()->isEnabled());
        server->toolCallFinished(QStringLiteral("view.state"),
                                 QStringLiteral("{\"ok\":true}"), 1, true);

        // The panel buttons are wired to the transport's emergency stop.
        auto* stopButton = panel->findChild<QPushButton*>(QStringLiteral("mcpLogStopButton"));
        auto* resumeButton = panel->findChild<QPushButton*>(QStringLiteral("mcpLogResumeButton"));
        QVERIFY(stopButton != nullptr);
        QVERIFY(resumeButton != nullptr);

        stopButton->click();
        QVERIFY(server->aiStopped());
        QVERIFY(!window->viewportForTest()->isEnabled());
        QVERIFY(statusAi->isVisible());
        QCOMPARE(statusAi->text(), QStringLiteral("AI 已急停"));

        // stop wins over a finished call: the editor stays locked.
        server->toolCallStarted(QStringLiteral("entity.update"),
                                QStringLiteral("{}"), true);
        server->toolCallFinished(QStringLiteral("entity.update"),
                                 QStringLiteral("{\"ok\":false}"), 2, false);
        QVERIFY(!window->viewportForTest()->isEnabled());

        resumeButton->click();
        QVERIFY(!server->aiStopped());
        QVERIFY(window->assetPanelForTest()->isEnabled());
        QVERIFY(window->timelineForTest()->isEnabled());
        QVERIFY(window->propertiesPanelForTest()->isEnabled());
        QVERIFY(window->viewportForTest()->isEnabled());
        QVERIFY(statusAi->isHidden());
    }

    void recentProjectsStartEmpty()
    {
        NextWindow window(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 0);

        auto* empty =
            window.findChild<QLabel*>(QStringLiteral("recentProjectsEmpty"));
        QVERIFY(empty != nullptr);
        QCOMPARE(empty->text(), QStringLiteral("暂无最近项目"));
        QVERIFY(storedRecentProjects().isEmpty());
    }

    void recentProjectsRecordOpenAndCreate()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString opened = createRecentProject(
            temp.path() + QStringLiteral("/opened.pvproj"),
            QStringLiteral("打开的"));
        QVERIFY(!opened.isEmpty());

        NextWindow window(nullptr);
        QString error;
        QVERIFY2(window.openProject(opened, &error), qPrintable(error));
        QCOMPARE(window.recentProjectCountForTest(), 1);
        QCOMPARE(window.recentProjectForTest(0), opened);
        QCOMPARE(storedRecentProjects(), QStringList{opened});

        // chooseNewProject() also ends in setDocument(); drive that seam
        // directly so no modal new-project dialog is needed.
        const QString createdDir =
            temp.path() + QStringLiteral("/created.pvproj");
        ProjectSettings settings;
        settings.name = QStringLiteral("新建的");
        auto created = ProjectDocument::create(createdDir, settings, &error);
        QVERIFY2(created != nullptr, qPrintable(error));
        window.setDocument(std::move(created));

        QCOMPARE(window.recentProjectCountForTest(), 2);
        QCOMPARE(window.recentProjectForTest(0),
                 QFileInfo(createdDir).absoluteFilePath());
        QCOMPARE(window.recentProjectForTest(1), opened);
    }

    void recentProjectsOrderDedupeAndLimit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QStringList dirs;
        for (int i = 0; i < 11; ++i) {
            const QString dir = createRecentProject(
                temp.path() + QStringLiteral("/p%1.pvproj").arg(i),
                QStringLiteral("P%1").arg(i));
            QVERIFY(!dir.isEmpty());
            dirs.append(dir);
        }

        NextWindow window(nullptr);
        QString error;
        for (const QString& dir : dirs)
            QVERIFY2(window.openProject(dir, &error), qPrintable(error));

        QCOMPARE(window.recentProjectCountForTest(), 10);
        QCOMPARE(window.recentProjectForTest(0), dirs.last());
        QCOMPARE(window.recentProjectForTest(9), dirs.at(1));
        QVERIFY(!storedRecentProjects().contains(dirs.first()));

        // Re-opening an older project moves it to the front, no duplicates.
        QVERIFY2(window.openProject(dirs.at(5), &error), qPrintable(error));
        QCOMPARE(window.recentProjectCountForTest(), 10);
        QCOMPARE(window.recentProjectForTest(0), dirs.at(5));
        QCOMPARE(window.recentProjectForTest(1), dirs.last());

        const QStringList stored = storedRecentProjects();
        QCOMPARE(stored.size(), 10);
        QCOMPARE(QSet<QString>(stored.begin(), stored.end()).size(),
                 stored.size());
    }

    void recentProjectsPruneMissingPaths()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString kept = createRecentProject(
            temp.path() + QStringLiteral("/kept.pvproj"),
            QStringLiteral("保留"));
        QVERIFY(!kept.isEmpty());
        const QString missing =
            temp.path() + QStringLiteral("/missing.pvproj");

        // Missing paths are dropped before the welcome page is built.
        writeRecentProjectsSetting({missing, kept, kept});
        NextWindow window(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 1);
        QCOMPARE(window.recentProjectForTest(0), kept);
        QCOMPARE(storedRecentProjects(), QStringList{kept});

        // A project deleted after being recorded is dropped on failed open.
        QString error;
        QVERIFY2(window.openProject(kept, &error), qPrintable(error));
        QCOMPARE(window.recentProjectCountForTest(), 1);

        QVERIFY(QDir(kept).removeRecursively());
        QVERIFY(!window.openProject(kept, &error));
        QVERIFY(error.contains(QStringLiteral("路径不存在")));
        QCOMPARE(window.recentProjectCountForTest(), 0);
        QVERIFY(storedRecentProjects().isEmpty());
    }

    void recentProjectsDoubleClickOpens()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString dir = createRecentProject(
            temp.path() + QStringLiteral("/named.pvproj"),
            QStringLiteral("双击打开"));
        QVERIFY(!dir.isEmpty());

        writeRecentProjectsSetting({dir});
        NextWindow window(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 1);

        auto* row =
            window.findChild<QWidget*>(QStringLiteral("recentProjectRow"));
        QVERIFY(row != nullptr);
        auto* name =
            row->findChild<QLabel*>(QStringLiteral("recentProjectName"));
        QVERIFY(name != nullptr);
        QCOMPARE(name->text(), QStringLiteral("双击打开"));
        auto* pathLabel =
            row->findChild<QLabel*>(QStringLiteral("recentProjectPath"));
        QVERIFY(pathLabel != nullptr);
        QCOMPARE(pathLabel->toolTip(), QDir::toNativeSeparators(dir));
        QVERIFY(row->findChild<QPushButton*>(
                    QStringLiteral("recentProjectOpenButton"))
                != nullptr);
        QVERIFY(row->findChild<QPushButton*>(
                    QStringLiteral("recentProjectRemoveButton"))
                != nullptr);

        QTest::mouseDClick(row, Qt::LeftButton);
        QTRY_VERIFY(window.document() != nullptr);
        QCOMPARE(window.document()->root(), dir);

        // Back to the welcome page; the "打开" button takes the same path.
        window.setDocument(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 1);
        auto* openButton = window.findChild<QPushButton*>(
            QStringLiteral("recentProjectOpenButton"));
        QVERIFY(openButton != nullptr);
        openButton->click();
        QTRY_VERIFY(window.document() != nullptr);
        QCOMPARE(window.document()->root(), dir);

        // ... and so does the test accessor.
        window.setDocument(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 1);
        window.openRecentForTest(0);
        QVERIFY(window.document() != nullptr);
        QCOMPARE(window.document()->root(), dir);
    }

    void recentProjectsRemoveButtonDropsEntry()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString dir = createRecentProject(
            temp.path() + QStringLiteral("/removable.pvproj"),
            QStringLiteral("可移除"));
        QVERIFY(!dir.isEmpty());

        writeRecentProjectsSetting({dir});
        NextWindow window(nullptr);
        QCOMPARE(window.recentProjectCountForTest(), 1);
        QVERIFY(window.findChild<QWidget*>(
                    QStringLiteral("recentProjectsEmpty"))
                == nullptr);

        auto* removeButton = window.findChild<QPushButton*>(
            QStringLiteral("recentProjectRemoveButton"));
        QVERIFY(removeButton != nullptr);
        removeButton->click();

        QTRY_COMPARE(window.recentProjectCountForTest(), 0);
        QTRY_VERIFY(window.findChild<QWidget*>(
                        QStringLiteral("recentProjectsEmpty"))
                    != nullptr);
        QVERIFY(storedRecentProjects().isEmpty());
    }

    void appWordingHasNoGitLfs()
    {
        const QDir sourceDir(QStringLiteral(PVF_NEXT_APP_SOURCE_DIR));
        QVERIFY(sourceDir.exists());
        const QStringList files =
            sourceDir.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                                QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString& name : files) {
            QFile file(sourceDir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            QVERIFY2(!text.contains(QStringLiteral("Git LFS"), Qt::CaseInsensitive),
                     qPrintable(name));
            QVERIFY2(!text.contains(QStringLiteral("git-lfs"), Qt::CaseInsensitive),
                     qPrintable(name));
        }
    }
};

QTEST_MAIN(NextWindowTests)
#include "NextWindowTests.moc"
