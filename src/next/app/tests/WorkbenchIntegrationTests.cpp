#include "next/app/NextWindow.h"

#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchPanel.h"
#include "next/app/WorkbenchServices.h"
#include "next/viewport/ViewportWidget.h"
#include "workbench/MetaBuilder.h"

#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>
#include <thread>

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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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

Uuid addRectAt(ProjectDocument* document, double x, double y, double width,
               double height, const QString& color)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Rect");
    entity.start = 0;
    entity.end = kLastFrame;
    entity.transform.x = x;
    entity.transform.y = y;
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = width;
    entity.content.vector.height = height;
    entity.content.vector.fillColor = color;

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create rect"), &ignored);
    document->flush(&ignored);
    return uuid;
}

// A black clip driven by a spin solver: the workbench meta has to evaluate the
// segment, but the picture itself is irrelevant to the solve count.
Uuid addBlackSolverRect(ProjectDocument* document, int endFrame)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Black");
    entity.start = 0;
    entity.end = endFrame;
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = 320.0;
    entity.content.vector.height = 180.0;
    entity.content.vector.fillColor = QStringLiteral("#000000");

    SolverSegment spin;
    spin.id = QStringLiteral("spin");
    spin.effect = SolverEffect::Spin;
    spin.startFrame = 0;
    spin.endFrame = -1; // automatic end: covers the whole clip
    spin.params.insert(QStringLiteral("angularVelocity"), 6.0);
    spin.params.insert(QStringLiteral("direction"), 1);
    entity.solvers.append(spin);

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create black solver"),
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

int qoiCount(const QString& root)
{
    int files = 0;
    QDirIterator it(root, {QStringLiteral("*.qoi")}, QDir::Files,
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

Uuid addImageEntity(ProjectDocument* document, const AssetHash& hash)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Image");
    entity.start = 0;
    entity.end = kLastFrame;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Image;
    entity.content.asset.hash = hash;

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create image"),
                           &ignored);
    document->flush(&ignored);
    return uuid;
}

Uuid addImageEntityRange(ProjectDocument* document, const AssetHash& hash,
                         int endFrame)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = QStringLiteral("Image");
    entity.start = 0;
    entity.end = endFrame;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Image;
    entity.content.asset.hash = hash;

    const Uuid uuid = entity.uuid;
    document->mutableProject().entities.insert(uuid, entity);
    QString ignored;
    document->commitEntity(uuid, QStringLiteral("pvf: create image"),
                           &ignored);
    document->flush(&ignored);
    return uuid;
}

AssetHash importProfileImage(ProjectDocument* document, const QString& path,
                             QString* error)
{
    QImage image(64, 36, QImage::Format_ARGB32);
    image.fill(QColor(0, 90, 200));
    if (!image.save(path))
        return {};
    const auto stored = document->assets().importFile(path, error);
    return stored.has_value() ? stored->hash : AssetHash();
}

} // namespace

class WorkbenchIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void openDoesNotScanOrCreateCache();
    void onDemandLookupStoresThenHitsAndEditMisses();
    void vectorFramesAreStoredAsQoi();
    void rasterAssetFramesUseWebp();
    void scrubCachesOnlyVisitedFrames();
    void manualScanFillsCacheThenRescanHits();
    void cacheSurvivesReopen();
    void disabledCacheDoesNotLookupOrStore();
    void htmlMissIsNotCached();
    void cameraSwitchRebuildsCachePerCamera();
    void playbackPausesAndResumesScan();
    void cacheToPlayheadLimitsRange();
    void panelShowsStatsAndClears();
    void expandSettingChangesCacheKey();
    void expandedCanvasCachesOutsideEntity();
    void cameraCacheDoesNotExpand();
    void sharedSolverResultsSkipMetaSolves();
    void playbackRasterStoreIsQueued();
    void playbackViewportQueuesRasterStore();
    void editDiscardsQueuedRasterStores();
    void shutdownDiscardsQueuedRasterStores();
    void writerThreadsRetuneWithoutDropping();
    void writerThreadsSpeedUpTheDrain();
    void waitForPendingStoresDoesNotReenterEventLoop();
};

void WorkbenchIntegrationTest::openDoesNotScanOrCreateCache()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString projectDir = temp.path() + QStringLiteral("/open.pvproj");
    auto document = createProject(projectDir, &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    QVERIFY(window.workbenchServicesForTest() != nullptr);

    // Opening only constructs the service.  No debounce, idle timer or
    // document signal may scan; wait past the old 500 ms debounce.
    QTest::qWait(1500);
    QCOMPARE(window.workbenchStatsForTest().entries, 0);
    QVERIFY(!window.prewarmProgressForTest().scanning);
    QVERIFY(window.workbenchCacheDirForTest().isEmpty());
    QVERIFY(!QDir(projectDir + QStringLiteral("/.cache/workbench")).exists());
    QCOMPARE(webpCount(projectDir + QStringLiteral("/.cache/workbench")), 0);
}

void WorkbenchIntegrationTest::onDemandLookupStoresThenHitsAndEditMisses()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString projectDir = temp.path() + QStringLiteral("/demand.pvproj");
    auto document = createProject(projectDir, &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const Uuid red = addRedRect(document.get());

    NextWindow window(std::move(document));
    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);

    const QString cacheRoot = projectDir + QStringLiteral("/.cache/workbench");
    constexpr int frame = 5;

    // A lookup is the only entry point; a miss without a write leaves no
    // directory behind.
    QVERIFY(window.lookupForTest(frame, Uuid()).isNull());
    QVERIFY(!QDir(cacheRoot).exists());
    QCOMPARE(services->stats().entries, 0);

    // Simulate the viewport render that follows the miss, then store it.
    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(255, 0, 0));
    services->store(frame, Uuid(), rendered);

    QVERIFY(QDir(cacheRoot).exists());
    QCOMPARE(services->stats().entries, 1);
    QCOMPARE(services->stats().stores, quint64(1));
    const QImage hit = window.lookupForTest(frame, Uuid());
    QVERIFY(!hit.isNull());
    QCOMPARE(hit.pixelColor(160, 90), QColor(255, 0, 0));
    QCOMPARE(services->stats().hits, quint64(1));

    // Committing an edit changes the frame meta: the same frame must miss.
    QVERIFY(window.document()->project().entities.contains(red));
    Entity moved = window.document()->project().entities.value(red);
    moved.transform.x = 10.0;
    window.document()->mutableProject().entities.insert(red, moved);
    QVERIFY2(window.document()->commitEntity(
                 red, QStringLiteral("pvf: move red"), &error),
             qPrintable(error));
    window.document()->flush(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY(window.lookupForTest(frame, Uuid()).isNull());
    QVERIFY(services->stats().misses >= 1);
}

void WorkbenchIntegrationTest::vectorFramesAreStoredAsQoi()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString projectDir = temp.path() + QStringLiteral("/qoi.pvproj");
    auto document = createProject(projectDir, &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);

    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(255, 0, 0));
    services->store(5, Uuid(), rendered);

    const QString cacheRoot = projectDir + QStringLiteral("/.cache/workbench");
    QVERIFY(!services->cacheDirectory().isEmpty());
    QVERIFY(QFileInfo::exists(
        QDir(services->cacheDirectory()).filePath(QStringLiteral("5.qoi"))));
    QVERIFY(!QFileInfo::exists(
        QDir(services->cacheDirectory()).filePath(QStringLiteral("5.webp"))));
    QCOMPARE(qoiCount(cacheRoot), 1);
    QCOMPARE(webpCount(cacheRoot), 0);

    // The QOI path is lossless: the cached pixel is exactly the stored one.
    const QImage hit = window.lookupForTest(5, Uuid());
    QVERIFY(!hit.isNull());
    QCOMPARE(hit.pixelColor(160, 90), QColor(255, 0, 0));
}

void WorkbenchIntegrationTest::rasterAssetFramesUseWebp()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString projectDir = temp.path() + QStringLiteral("/raster.pvproj");
    auto document = createProject(projectDir, &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString source = temp.path() + QStringLiteral("/source.png");
    {
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(QColor(0, 0, 255));
        QVERIFY(image.save(source));
    }
    const auto stored = document->assets().importFile(source, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    addImageEntity(document.get(), stored->hash);

    NextWindow window(std::move(document));
    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);
    QVERIFY(services->buildMetaForTest(5).hasRaster);

    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(0, 0, 255));
    services->store(5, Uuid(), rendered);

    const QString cacheRoot = projectDir + QStringLiteral("/.cache/workbench");
    QVERIFY(QFileInfo::exists(
        QDir(services->cacheDirectory()).filePath(QStringLiteral("5.webp"))));
    QVERIFY(!QFileInfo::exists(
        QDir(services->cacheDirectory()).filePath(QStringLiteral("5.qoi"))));
    QCOMPARE(webpCount(cacheRoot), 1);
    QCOMPARE(qoiCount(cacheRoot), 0);

    const QImage hit = window.lookupForTest(5, Uuid());
    QVERIFY(!hit.isNull());
    QCOMPARE(hit.size(), services->targetSize());
    QCOMPARE(services->stats().entries, 1);
}

void WorkbenchIntegrationTest::scrubCachesOnlyVisitedFrames()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    const QString projectDir = temp.path() + QStringLiteral("/scrub.pvproj");
    auto document = createProject(projectDir, &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    window.resize(1280, 800);
    window.show();
    QTest::qWait(100);

    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);
    const QString cacheRoot = projectDir + QStringLiteral("/.cache/workbench");
    QVERIFY(QDir(cacheRoot).exists());

    // Frame 0 was actually painted, so only it may be cached.
    QCOMPARE(services->stats().entries, 1);
    QVERIFY(!window.lookupForTest(0, Uuid()).isNull());
    QVERIFY(window.lookupForTest(4, Uuid()).isNull());
    QVERIFY(window.lookupForTest(5, Uuid()).isNull());
    QVERIFY(window.lookupForTest(6, Uuid()).isNull());

    // Scrub to frame 5 and paint it; no other frame is prefetched.
    window.document()->setPlayhead(5);
    window.viewportForTest()->refresh();
    window.viewportForTest()->repaint();
    QTest::qWait(100);
    QVERIFY(!window.lookupForTest(5, Uuid()).isNull());
    QVERIFY(window.lookupForTest(4, Uuid()).isNull());
    QVERIFY(window.lookupForTest(6, Uuid()).isNull());
    const int visited = services->stats().entries;

    // Idle waiting must not fill anything else.
    QTest::qWait(800);
    QCOMPARE(services->stats().entries, visited);

    // Returning to a visited frame is a hit.
    const quint64 hitsBefore = services->stats().hits;
    window.document()->setPlayhead(5);
    window.viewportForTest()->refresh();
    window.viewportForTest()->repaint();
    QTest::qWait(100);
    QVERIFY(services->stats().hits > hitsBefore);
}

void WorkbenchIntegrationTest::manualScanFillsCacheThenRescanHits()
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

    // The viewport grabs the project canvas live; the workbench cache holds
    // the 50% expanded (320x180) image.
    const QImage frame = window.viewportForTest()->grabFrame();
    QCOMPARE(frame.size(), QSize(320, 180));
    QVERIFY(isRed(frame.pixelColor(160, 90)));
    QCOMPARE(window.workbenchServicesForTest()->expandPercent(), 50);
    QCOMPARE(window.workbenchServicesForTest()->targetSize(), QSize(320, 180));
    QVERIFY(isRed(window.lookupForTest(0, Uuid()).pixelColor(160, 90)));

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
    QCOMPARE(window.workbenchServicesForTest()->expandPercent(), 0);
    QCOMPARE(window.workbenchServicesForTest()->transparentOutsideCanvas(),
             false);
    QCOMPARE(window.workbenchServicesForTest()->targetSize(), QSize(160, 90));
    // Switching camera must not create or scan a cache by itself.
    QVERIFY(window.workbenchCacheDirForTest().isEmpty());

    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    QVERIFY(window.workbenchCacheDirForTest().contains(cameraUuid));
    QCOMPARE(window.workbenchStatsForTest().entries, kLastFrame);
    QVERIFY(!window.lookupForTest(0, cameraUuid).isNull());

    // Back to the whole canvas: a fresh service and directory, and the camera
    // uuid no longer matches.
    combo->setCurrentIndex(0);
    QVERIFY(window.workbenchServicesForTest()->cameraUuid().isEmpty());
    QCOMPARE(window.workbenchServicesForTest()->expandPercent(), 50);
    QCOMPARE(window.workbenchServicesForTest()->targetSize(), QSize(320, 180));
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
    QVERIFY(panel->statusTextForTest().contains(QStringLiteral("扩展 50%")));
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

void WorkbenchIntegrationTest::expandSettingChangesCacheKey()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/expand.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());

    NextWindow window(std::move(document));
    WorkbenchServices* first = window.workbenchServicesForTest();
    QVERIFY(first != nullptr);
    QCOMPARE(first->expandPercent(), 50);
    QCOMPARE(first->targetSize(), QSize(320, 180));

    // Creating the service does not create files; an on-demand store does.
    QVERIFY(first->cacheDirectory().isEmpty());
    QImage image(320, 180, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    first->store(0, Uuid(), image);
    const QString directory50 = first->cacheDirectory();
    QVERIFY(!directory50.isEmpty());

    AppSettings settings = window.settingsForTest();
    settings.workbenchCacheExpandPercent = 25;
    window.setSettingsForTest(settings);

    WorkbenchServices* second = window.workbenchServicesForTest();
    QVERIFY(second != nullptr);
    QCOMPARE(second->expandPercent(), 25);
    QCOMPARE(second->targetSize(), QSize(240, 135));
    second->store(0, Uuid(), image);
    QVERIFY(!second->cacheDirectory().isEmpty());
    QVERIFY(second->cacheDirectory() != directory50);
}

void WorkbenchIntegrationTest::expandedCanvasCachesOutsideEntity()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/outside.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());
    addRectAt(document.get(), -200.0, -120.0, 20.0, 20.0,
              QStringLiteral("#0000ff"));

    NextWindow window(std::move(document));
    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);

    const QImage cached = window.lookupForTest(0, Uuid());
    QVERIFY(!cached.isNull());
    QCOMPARE(cached.size(), QSize(320, 180));

    // Expanded world (-320,-180,640,360) at 50%: world (-200,-120) lands on
    // (60,150), while the extended corner is transparent.
    const QColor outside = cached.pixelColor(60, 150);
    QVERIFY(outside.blue() > 200 && outside.red() < 60);
    QCOMPARE(outside.alpha(), 255);
    QCOMPARE(cached.pixelColor(0, 0).alpha(), 0);
    QVERIFY(isRed(cached.pixelColor(160, 90)));
    QCOMPARE(cached.pixelColor(160, 90).alpha(), 255);
}

void WorkbenchIntegrationTest::cameraCacheDoesNotExpand()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/cam-flat.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addRedRect(document.get());
    const Uuid cameraUuid = addCamera(document.get());

    NextWindow window(std::move(document));
    QComboBox* combo = window.cameraComboBoxForTest();
    QVERIFY(combo != nullptr && combo->count() == 2);
    combo->setCurrentIndex(1);

    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);
    QCOMPARE(services->cameraUuid(), cameraUuid);
    QCOMPARE(services->expandPercent(), 0);
    QCOMPARE(services->transparentOutsideCanvas(), false);
    QCOMPARE(services->targetSize(), QSize(160, 90));

    window.startWorkbenchScanForTest();
    QTRY_VERIFY_WITH_TIMEOUT(!window.prewarmProgressForTest().scanning, 15000);
    const QImage cached = window.lookupForTest(0, cameraUuid);
    QCOMPARE(cached.size(), QSize(160, 90));
    QVERIFY(isRed(cached.pixelColor(80, 45)));
    QCOMPARE(cached.pixelColor(0, 0).alpha(), 255);
}

// The workbench builds a frame meta even when the image cache hits.  With the
// session's shared solver map attached, 50 different frames must solve the one
// spin segment exactly once, and the meta bytes must not depend on the cache.
void WorkbenchIntegrationTest::sharedSolverResultsSkipMetaSolves()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/solver.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    addBlackSolverRect(document.get(), 200);

    WorkbenchServices::Providers providers;
    WorkbenchServices sharedService(document.get(), AppSettings{}, Uuid(),
                                    providers);
    QHash<QString, solver::Result> shared;
    sharedService.setSolverResults(&shared);

    const int sharedBefore = anim::solverEvaluationCount();
    workbench::FrameMeta injected;
    for (int frame = 0; frame < 50; ++frame)
    {
        const workbench::FrameMeta meta =
            sharedService.buildMetaForTest(frame);
        if (frame == 25)
            injected = meta;
    }
    QCOMPARE(anim::solverEvaluationCount(), sharedBefore + 1);
    QCOMPARE(shared.size(), 1);

    // Without the shared table the same 5 frames solve every time, so the
    // counter assertion above cannot pass vacuously.
    WorkbenchServices plainService(document.get(), AppSettings{}, Uuid(),
                                   providers);
    const int plainBefore = anim::solverEvaluationCount();
    for (int frame = 0; frame < 5; ++frame)
        plainService.buildMetaForTest(frame);
    QCOMPARE(anim::solverEvaluationCount(), plainBefore + 5);

    // The cache only changes where the pose comes from, not the serialised
    // bytes: frame 25 must have the same sha1 with and without the shared map.
    const workbench::FrameMeta plain = plainService.buildMetaForTest(25);
    QCOMPARE(plain.sha1, injected.sha1);
    QCOMPARE(plain.bytes, injected.bytes);
    QCOMPARE(anim::solverEvaluationCount(), plainBefore + 6);
}

// Playback raster misses must not encode or write on the paint thread.  The
// queue is bounded, drops the oldest pending frame, and the serial writer
// fills the cache with whatever survived.
void WorkbenchIntegrationTest::playbackRasterStoreIsQueued()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/async.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), AppSettings{}, Uuid(),
                               providers);
    QVERIFY(services.cacheValid());
    QVERIFY(services.buildMetaForTest(5).hasRaster);

    QImage rendered(1280, 720, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(0, 90, 200));

    services.setPlaybackActive(true);
    QVERIFY(services.playbackActive());

    QElapsedTimer timer;
    timer.start();
    constexpr int kFrames = 120;
    for (int frame = 0; frame < kFrames; ++frame)
        services.store(frame, Uuid(), rendered);
    const double uiMeanMs =
        double(timer.nsecsElapsed()) / 1.0e6 / double(kFrames);

    // No event loop ran during the loop, so the writer could not drain the
    // queue: the bound and its oldest-first drops are deterministic.
    QCOMPARE(services.pendingStores(), 64);
    QCOMPARE(services.droppedStores(), quint64(kFrames - 64));
    QCOMPARE(services.lastDroppedFrame(), kFrames - 65);
    QVERIFY2(uiMeanMs < 10.0,
             qPrintable(QStringLiteral("store enqueue averaged %1 ms; WebP "
                                       "encoding is still on the UI thread")
                            .arg(uiMeanMs, 0, 'f', 2)));

    QVERIFY(services.waitForPendingStores(30000));
    QTest::qWait(50);
    QCOMPARE(services.pendingStores(), 0);
    QCOMPARE(services.asyncStoresWritten(), quint64(64));
    QCOMPARE(services.workerEncodes(), quint64(64));
    QCOMPARE(services.stats().entries, 64);
    QCOMPARE(services.stats().pendingStores, 0);

    // The newest frames survived the drop, so returning to the playhead after
    // playback is a hit.
    QVERIFY(!services.lookupForTest(kFrames - 1, Uuid()).isNull());
    QVERIFY(services.lookupForTest(0, Uuid()).isNull());
    QVERIFY(services.workerEncodeMeanMs() > 0.0);

    services.setPlaybackActive(false);
}

void WorkbenchIntegrationTest::playbackViewportQueuesRasterStore()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/vpasync.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    NextWindow window(std::move(document));
    window.resize(1280, 800);
    window.show();
    QTest::qWait(100);

    WorkbenchServices* services = window.workbenchServicesForTest();
    QVERIFY(services != nullptr);
    QVERIFY(services->buildMetaForTest(5).hasRaster);

    window.document()->setPlayhead(5);
    window.togglePlaybackForTest();
    QVERIFY(window.isPlayingForTest());
    QVERIFY(services->playbackActive());

    const quint64 storesBefore = services->stats().stores;
    const int pendingBefore = services->pendingStores();

    // Paint one raster miss without pumping the event loop afterwards: the
    // viewport must already have the picture and the store must be queued.
    window.viewportForTest()->refresh();
    window.viewportForTest()->repaint();
    QVERIFY2(services->pendingStores() > pendingBefore,
             "raster playback miss did not enter the writer queue");
    QCOMPARE(services->stats().stores, storesBefore);

    QVERIFY(services->waitForPendingStores(30000));
    QVERIFY(!window.lookupForTest(5, Uuid()).isNull());
    QVERIFY(services->asyncStoresWritten() > 0);

    if (window.isPlayingForTest())
        window.togglePlaybackForTest();
}

void WorkbenchIntegrationTest::editDiscardsQueuedRasterStores()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/edit.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    const Uuid image = addImageEntityRange(document.get(), hash, 400);

    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), AppSettings{}, Uuid(),
                               providers);
    QVERIFY(services.cacheValid());

    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(0, 90, 200));
    services.setPlaybackActive(true);
    for (int frame = 0; frame < 5; ++frame)
        services.store(frame, Uuid(), rendered);
    QCOMPARE(services.pendingStores(), 5);

    // Commit an edit without pumping events.  Document::changed clears the
    // queue; the running encode may finish but must never write its old frame.
    Entity moved = document->project().entities.value(image);
    moved.transform.x = 12.0;
    document->mutableProject().entities.insert(image, moved);
    QVERIFY2(document->commitEntity(image, QStringLiteral("pvf: move image"),
                                    &error),
             qPrintable(error));
    document->flush(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(services.pendingStores(), 0);

    QVERIFY(services.waitForPendingStores(5000));
    QTest::qWait(100);
    QCOMPARE(services.asyncStoresWritten(), quint64(0));
    QCOMPARE(services.stats().entries, 0);
    QVERIFY(services.lookupForTest(0, Uuid()).isNull());
}

void WorkbenchIntegrationTest::shutdownDiscardsQueuedRasterStores()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/close.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    workbench::Options options;
    {
        WorkbenchServices::Providers providers;
        WorkbenchServices services(document.get(), AppSettings{}, Uuid(),
                                   providers);
        QVERIFY(services.cacheValid());
        options = services.options();
        QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
        rendered.fill(QColor(0, 90, 200));
        services.setPlaybackActive(true);
        for (int frame = 0; frame < 5; ++frame)
            services.store(frame, Uuid(), rendered);
        QCOMPARE(services.pendingStores(), 5);
    } // destructor waits for the worker and discards the queued frames

    workbench::Cache reopened(
        QDir(document->root()).filePath(QStringLiteral(".cache/workbench")),
        QString(), options);
    QVERIFY2(reopened.isValid(), qPrintable(reopened.error()));
    QCOMPARE(reopened.cachedFrames().size(), 0);
}

void WorkbenchIntegrationTest::writerThreadsRetuneWithoutDropping()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/workers.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    AppSettings settings;
    settings.workbenchCacheWriterThreads = 1;
    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), settings, Uuid(), providers);
    QVERIFY(services.cacheValid());
    QCOMPARE(services.writerThreads(), 1);

    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(0, 90, 200));
    services.setPlaybackActive(true);
    for (int frame = 0; frame < 8; ++frame)
        services.store(frame, Uuid(), rendered);
    QCOMPARE(services.pendingStores(), 8);
    QCOMPARE(services.droppedStores(), quint64(0));

    // Raising the count mid-flight only adds slots: every queued frame is
    // still there and gets written.
    services.setWriterThreads(3);
    QCOMPARE(services.writerThreads(), 3);
    QCOMPARE(services.pendingStores(), 8);
    QCOMPARE(services.droppedStores(), quint64(0));
    QVERIFY(services.waitForPendingStores(30000));
    QCOMPARE(services.pendingStores(), 0);
    QCOMPARE(services.asyncStoresWritten(), quint64(8));

    // Out-of-range values clamp to the supported 1..4 range.
    services.setWriterThreads(0);
    QCOMPARE(services.writerThreads(), 1);
    services.setWriterThreads(9);
    QCOMPARE(services.writerThreads(), 4);
    services.setWriterThreads(-4);
    QCOMPARE(services.writerThreads(), 1);

    services.setPlaybackActive(false);
}

void WorkbenchIntegrationTest::writerThreadsSpeedUpTheDrain()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/speed.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), AppSettings{}, Uuid(),
                               providers);
    QVERIFY(services.cacheValid());
    QImage rendered(1280, 720, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&rendered);
        QLinearGradient gradient(0, 0, 1280, 720);
        gradient.setColorAt(0.0, QColor(240, 80, 40));
        gradient.setColorAt(1.0, QColor(30, 60, 230));
        painter.fillRect(rendered.rect(), gradient);
    }
    services.setPlaybackActive(true);

    const auto drain = [&services, &rendered](int first, int count) {
        QElapsedTimer timer;
        timer.start();
        for (int frame = 0; frame < count; ++frame)
            services.store(first + frame, Uuid(), rendered);
        const bool drained = services.waitForPendingStores(120000);
        const double ms = double(timer.nsecsElapsed()) / 1.0e6;
        return drained ? ms : -1.0;
    };

    // One warm-up frame so plugin/driver pages are resident in both runs.
    services.setWriterThreads(1);
    QVERIFY(drain(100, 1) > 0.0);

    services.setWriterThreads(1);
    const double oneThread = drain(200, 12);
    QVERIFY2(oneThread > 0.0, "single-thread drain did not finish");
    services.setWriterThreads(3);
    const double threeThreads = drain(300, 12);
    QVERIFY2(threeThreads > 0.0, "three-thread drain did not finish");
    services.setWriterThreads(4);
    const double fourThreads = drain(400, 12);
    QVERIFY2(fourThreads > 0.0, "four-thread drain did not finish");

    QVERIFY2(threeThreads < oneThread * 0.9,
             qPrintable(QStringLiteral("writers did not overlap: 1 thread %1 "
                                       "ms, 3 threads %2 ms")
                            .arg(oneThread, 0, 'f', 1)
                            .arg(threeThreads, 0, 'f', 1)));
    // A fourth writer must at least not be slower than a single one; whether it
    // beats three is machine dependent and reported by --cache-miss-profile.
    QVERIFY2(fourThreads < oneThread,
             qPrintable(QStringLiteral("four writers slower than one: 1 "
                                       "thread %1 ms, 4 threads %2 ms")
                            .arg(oneThread, 0, 'f', 1)
                            .arg(fourThreads, 0, 'f', 1)));
    QCOMPARE(services.droppedStores(), quint64(0));
    services.setPlaybackActive(false);
}

// waitForPendingStores() must drain the writer queue without running a nested
// event loop: only the service's own completion callbacks are delivered, so a
// due zero-interval timer stays unfired, and no frame is dispatched to the pool
// twice while several writers run concurrently.
void WorkbenchIntegrationTest::waitForPendingStoresDoesNotReenterEventLoop()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = createProject(temp.path() + QStringLiteral("/wait.pvproj"),
                                  &error);
    QVERIFY2(document != nullptr, qPrintable(error));
    const AssetHash hash = importProfileImage(
        document.get(), temp.path() + QStringLiteral("/photo.png"), &error);
    QVERIFY2(!hash.isEmpty(), qPrintable(error));
    addImageEntityRange(document.get(), hash, 400);

    AppSettings settings;
    settings.workbenchCacheWriterThreads = 3;
    WorkbenchServices::Providers providers;
    WorkbenchServices services(document.get(), settings, Uuid(), providers);
    QVERIFY(services.cacheValid());

    QImage rendered(320, 180, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(QColor(0, 90, 200));
    services.setPlaybackActive(true);

    constexpr int kFrames = 24;
    for (int frame = 0; frame < kFrames; ++frame)
        services.store(frame, Uuid(), rendered);
    QCOMPARE(services.pendingStores(), kFrames);

    // A timer that is due the moment the loop runs: a nested event loop in the
    // wait would fire it, sendPostedEvents(this, QEvent::MetaCall) must not.
    QTimer timer;
    timer.setInterval(0);
    int timerHits = 0;
    connect(&timer, &QTimer::timeout, &timer, [&timerHits] { ++timerHits; });
    timer.start();
    QCOMPARE(timerHits, 0);

    QVERIFY(services.waitForPendingStores(30000));
    // The old processEvents(AllEvents, 10) loop fired this timer; the new wait
    // delivers only this object's meta-calls.
    QCOMPARE(timerHits, 0);
    QCOMPARE(services.pendingStores(), 0);
    // Every queued frame was encoded exactly once: no duplicate dispatch even
    // though a stale scheduleStoreDispatch() call may still be posted.
    QCOMPARE(services.workerEncodes(), quint64(kFrames));
    QCOMPARE(services.asyncStoresWritten(), quint64(kFrames));
    QCOMPARE(services.stats().entries, kFrames);
    QCOMPARE(services.droppedStores(), quint64(0));

    // The wait still ran the completions: the cached frames are real hits.
    QVERIFY(!services.lookupForTest(kFrames - 1, Uuid()).isNull());

    // An empty queue returns immediately instead of waiting for the timeout.
    QElapsedTimer empty;
    empty.start();
    QVERIFY(services.waitForPendingStores(30000));
    QVERIFY(empty.elapsed() < 1000);

    // A zero timeout with encodes still queued reports failure instead of
    // spinning or blocking.
    for (int frame = 0; frame < 4; ++frame)
        services.store(100 + frame, Uuid(), rendered);
    QCOMPARE(services.pendingStores(), 4);
    QVERIFY(!services.waitForPendingStores(0));
    QVERIFY(services.waitForPendingStores(30000));
    QCOMPARE(services.pendingStores(), 0);
    QCOMPARE(services.workerEncodes(), quint64(kFrames + 4));
    QCOMPARE(services.asyncStoresWritten(), quint64(kFrames + 4));

    // Concurrent enqueue while waiting: a second thread posts store() calls to
    // this object's event queue.  waitForPendingStores() has to deliver them,
    // dispatch them to the free writers and still encode every frame exactly
    // once.
    constexpr int kPostedFrames = 8;
    std::thread producer([&services, rendered] {
        for (int frame = 0; frame < kPostedFrames; ++frame)
        {
            QMetaObject::invokeMethod(
                &services,
                [&services, frame, rendered] {
                    services.store(200 + frame, Uuid(), rendered);
                },
                Qt::QueuedConnection);
        }
    });
    producer.join();

    QVERIFY(services.waitForPendingStores(30000));
    QCOMPARE(services.pendingStores(), 0);
    QCOMPARE(services.workerEncodes(), quint64(kFrames + 4 + kPostedFrames));
    QCOMPARE(services.asyncStoresWritten(),
             quint64(kFrames + 4 + kPostedFrames));
    QCOMPARE(services.stats().entries, kFrames + 4 + kPostedFrames);
    QVERIFY(!services.lookupForTest(200, Uuid()).isNull());

    // The timer was only held back by the wait, not disabled.
    QTRY_VERIFY(timerHits > 0);
    services.setPlaybackActive(false);
}

QTEST_MAIN(WorkbenchIntegrationTest)
#include "WorkbenchIntegrationTests.moc"
