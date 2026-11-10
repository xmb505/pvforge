#include "next/app/NextWindow.h"

#include "assets/AssetStore.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchPanel.h"
#include "next/app/WorkbenchServices.h"
#include "next/viewport/ViewportWidget.h"

#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

constexpr int kLastFrame = 12; // entity end; frames 0..11 are cached

std::unique_ptr<ProjectDocument> createProject(const QString& projectDir,
                                               QString* error)
{
    ProjectSettings settings;
    settings.name = QStringLiteral("Workbench");
    settings.width = 320;
    settings.height = 180;
    settings.fps = 10;
    return ProjectDocument::create(projectDir, settings, error, false);
}

Uuid addRedRect(ProjectDocument* document)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Red");
    entity.start = 0;
    entity.end = kLastFrame;
    entity.transform.x = 160.0;
    entity.transform.y = 90.0;
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = 320.0;
    entity.content.vector.height = 180.0;
    entity.content.vector.fillColor = QStringLiteral("#ff0000");

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create red rect"),
                           &ignored);
    document->flush(&ignored);
    return uuid;
}

Uuid addCamera(ProjectDocument* document)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Camera");
    entity.start = 0;
    entity.end = kLastFrame;
    entity.content.kind = ContentKind::Camera;

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create camera"),
                           &ignored);
    document->flush(&ignored);
    return uuid;
}

bool isRed(const QColor& color)
{
    return color.red() > 200 && color.green() < 60 && color.blue() < 60;
}

int webpCount(const QString& root)
{
    int files = 0;
    QDirIterator it(root, {QStringLiteral("*.webp")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++files;
    }
    return files;
}

// Writes a minimal HTML asset directly into the store so render2 asks the HTML
// provider for a picture without needing Chrome or an import pipeline.
AssetHash writeHtmlAsset(ProjectDocument* document, const QString& name)
{
    const AssetHash hash = QString::fromLatin1(
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    const QString directory =
        document->assets().assetsDirectory() + QLatin1Char('/') + hash;
    if (!QDir().mkpath(directory + QStringLiteral("/bundle")))
        return {};

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), QStringLiteral("html"));
    metadata.insert(QStringLiteral("name"), name);
    metadata.insert(QStringLiteral("width"), 800);
    metadata.insert(QStringLiteral("height"), 120);
    metadata.insert(QStringLiteral("entry"), QStringLiteral("index.html"));

    QFile json(directory + QStringLiteral("/asset.json"));
    if (!json.open(QIODevice::WriteOnly))
        return {};
    json.write(QJsonDocument(metadata).toJson());
    json.close();

    QFile index(directory + QStringLiteral("/bundle/index.html"));
    if (!index.open(QIODevice::WriteOnly))
        return {};
    index.write("<html></html>");
    index.close();
    return hash;
}

Uuid addHtmlEntity(ProjectDocument* document, const AssetHash& hash)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Html");
    entity.start = 0;
    entity.end = kLastFrame;
    entity.transform.x = 160.0;
    entity.transform.y = 90.0;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Html;
    entity.content.asset.hash = hash;

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create html"),
                           &ignored);
    document->flush(&ignored);
    return uuid;
}

} // namespace

class WorkbenchIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void autoScanStartsAfterOpen();
    void prewarmFillsCacheThenRescanHits();
    void cacheSurvivesReopen();
    void disabledCacheDoesNotLookupOrStore();
    void htmlMissIsNotCached();
    void cameraSwitchRebuildsCachePerCamera();
    void playbackPausesAndResumesScan();
    void cacheToPlayheadLimitsRange();
    void panelShowsStatsAndClears();
};

void WorkbenchIntegrationTest::autoScanStartsAfterOpen()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/auto.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    QVERIFY(window.workbenchServicesForTest() != nullptr);

    // No manual kick: the 500 ms debounce after opening must start the scan.
    QTRY_VERIFY_WITH_TIMEOUT(window.workbenchStatsForTest().entries > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    QCOMPARE(window.workbenchStatsForTest().entries, kLastFrame);
}

void WorkbenchIntegrationTest::prewarmFillsCacheThenRescanHits()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/wb.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    QVERIFY(window.workbenchServicesForTest() != nullptr);

    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);

    const WorkbenchPrewarmProgress first = window.prewarmProgressForTest();
    QCOMPARE(first.total, kLastFrame);
    QCOMPARE(first.done, kLastFrame);
    QCOMPARE(first.cached, kLastFrame);
    QCOMPARE(first.skipped, 0);
    QCOMPARE(first.failed, 0);

    const workbench::CacheStats afterFirst = window.workbenchStatsForTest();
    QCOMPARE(afterFirst.entries, kLastFrame);
    QCOMPARE(afterFirst.stores, quint64(kLastFrame));
    QVERIFY(!window.workbenchCacheDirForTest().isEmpty());
    QVERIFY(QDir(window.workbenchCacheDirForTest()).exists());

    // The viewport grabs the cached (50%) frame and scales it while painting.
    const QImage frame = window.viewportForTest()->grabFrame();
    QCOMPARE(frame.size(), QSize(160, 90));
    QVERIFY(isRed(frame.pixelColor(80, 45)));
    QVERIFY(isRed(window.lookupForTest(0, Uuid()).pixelColor(80, 45)));

    // Second pass: meta matches everywhere, nothing is rendered or written.
    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);

    const WorkbenchPrewarmProgress second = window.prewarmProgressForTest();
    QCOMPARE(second.hits, kLastFrame);
    QCOMPARE(second.cached, 0);
    QCOMPARE(window.workbenchStatsForTest().stores, quint64(kLastFrame));
}

void WorkbenchIntegrationTest::cacheSurvivesReopen()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.path() + QStringLiteral("/reopen.pvproj");

    QString error;
    {
        auto document = createProject(path, &error);
        QVERIFY2(document != nullptr, qPrintable(error));
        addRedRect(document.get());

        NextWindow window(std::move(document));
        window.startWorkbenchScanForTest();
        QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning,
                                 15000);
        QCOMPARE(window.prewarmProgressForTest().cached, kLastFrame);
    }

    auto reopened = ProjectDocument::open(path, &error, false);
    QVERIFY2(reopened != nullptr, qPrintable(error));
    NextWindow window(std::move(reopened));
    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);

    const WorkbenchPrewarmProgress progress = window.prewarmProgressForTest();
    QCOMPARE(progress.hits, kLastFrame);
    QCOMPARE(progress.cached, 0);
    QCOMPARE(window.workbenchStatsForTest().entries, kLastFrame);
    QVERIFY(!window.lookupForTest(5, Uuid()).isNull());
}

void WorkbenchIntegrationTest::disabledCacheDoesNotLookupOrStore()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.path() + QStringLiteral("/off.pvproj");

    QString error;
    auto document = createProject(path, &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    AppSettings settings = window.settingsForTest();
    settings.workbenchCacheEnabled = false;
    window.setSettingsForTest(settings);

    QVERIFY(window.workbenchServicesForTest() == nullptr);
    window.startWorkbenchScanForTest(); // no service, must be a no-op
    QTest::qWait(300);

    QCOMPARE(window.workbenchStatsForTest().entries, 0);
    QVERIFY(window.lookupForTest(0, Uuid()).isNull());
    QCOMPARE(webpCount(path + QStringLiteral("/.cache/workbench")), 0);

    // The viewport still renders normally without the hooks.
    QVERIFY(!window.viewportForTest()->grabFrame().isNull());
}

void WorkbenchIntegrationTest::htmlMissIsNotCached()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/html.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash =
        writeHtmlAsset(document.get(), QStringLiteral("Html"));
    QVERIFY(!hash.isEmpty());
    addHtmlEntity(document.get(), hash);

    // No HTML provider: render2 would draw its placeholder, which must never
    // become a cached "final" frame.
    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), AppSettings{}, Uuid(),
                               providers);
    QVERIFY(services.cacheValid());

    services.startScanNow();
    QTRY_VERIFY_WITH_TIMEOUT(!services.prewarmProgressForTest().scanning,
                             15000);

    const WorkbenchPrewarmProgress progress = services.prewarmProgressForTest();
    QCOMPARE(progress.total, kLastFrame);
    QCOMPARE(progress.skipped, kLastFrame);
    QCOMPARE(progress.cached, 0);
    QCOMPARE(services.stats().entries, 0);
}

void WorkbenchIntegrationTest::cameraSwitchRebuildsCachePerCamera()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/cam.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());
    const Uuid cameraUuid = addCamera(document.get());

    NextWindow window(std::move(document));
    QComboBox* combo = window.cameraComboBoxForTest();
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 2);

    combo->setCurrentIndex(1);
    QCOMPARE(window.workbenchServicesForTest()->cameraUuid(), cameraUuid);
    QVERIFY(window.workbenchCacheDirForTest().contains(cameraUuid));

    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    QCOMPARE(window.workbenchStatsForTest().entries, kLastFrame);
    QVERIFY(!window.lookupForTest(0, cameraUuid).isNull());

    // Back to the whole canvas: a fresh service and directory, and the camera
    // uuid no longer matches.
    combo->setCurrentIndex(0);
    QVERIFY(window.workbenchServicesForTest()->cameraUuid().isEmpty());
    QVERIFY(window.lookupForTest(0, cameraUuid).isNull());
}

void WorkbenchIntegrationTest::playbackPausesAndResumesScan()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/play.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    window.startWorkbenchScanForTest();
    QVERIFY(window.prewarmProgressForTest().scanning);

    window.togglePlaybackForTest();
    QVERIFY(window.isPlayingForTest());
    QVERIFY(window.prewarmProgressForTest().paused);

    const int held = window.prewarmProgressForTest().done;
    QTest::qWait(150);
    QCOMPARE(window.prewarmProgressForTest().done, held);

    window.togglePlaybackForTest();
    QVERIFY(!window.isPlayingForTest());
    QVERIFY(!window.prewarmProgressForTest().paused);
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    QCOMPARE(window.workbenchStatsForTest().entries, kLastFrame);
}

void WorkbenchIntegrationTest::cacheToPlayheadLimitsRange()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/head.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    window.document()->setPlayhead(5);
    QAction* action = window.cacheToPlayheadActionForTest();
    QVERIFY(action != nullptr);
    action->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    QCOMPARE(window.prewarmProgressForTest().total, 6);
    QCOMPARE(window.workbenchStatsForTest().entries, 6);
}

void WorkbenchIntegrationTest::panelShowsStatsAndClears()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/panel.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);

    WorkbenchPanel* panel = window.workbenchPanelForTest();
    QVERIFY(panel != nullptr);
    QCOMPARE(panel->servicesForTest(), window.workbenchServicesForTest());
    panel->refreshForTest();
    QVERIFY(panel->statusTextForTest().contains(QStringLiteral("比例 50%")));
    QVERIFY(panel->statsTextForTest().contains(QStringLiteral("已缓存")));
    QVERIFY(panel->statsTextForTest().contains(
        QString::number(kLastFrame)));
    QVERIFY(panel->rescanButtonForTest()->isEnabled());
    QVERIFY(panel->clearButtonForTest()->isEnabled());

    panel->clearButtonForTest()->click();
    QCOMPARE(window.workbenchStatsForTest().entries, 0);
    QVERIFY(panel->statsTextForTest().contains(QStringLiteral("已缓存 0")));

    // The menu entries exist and are wired.
    QVERIFY(window.cacheAllActionForTest() != nullptr);
    QVERIFY(window.clearWorkbenchCacheActionForTest() != nullptr);
    QVERIFY(window.workbenchPanelActionForTest() != nullptr);
}

QTEST_MAIN(WorkbenchIntegrationTest)
#include "WorkbenchIntegrationTests.moc"
