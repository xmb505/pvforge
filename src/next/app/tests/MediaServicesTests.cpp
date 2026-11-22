#include "next/app/MediaServices.h"

#include "assets/AssetStore.h"
#include "htmlcommon/HtmlBrowser.h"
#include "html2/HtmlFrameService.h"
#include "next/app/MediaExportSession.h"
#include "next/app/ProjectDocument.h"
#include "video2/VideoFrames.h"
#include "video2/VideoSequenceService.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>
#include <memory>
#include <optional>

using namespace pvf;
using namespace pvf::next;
using namespace pvf::model;

namespace {

bool gitAvailable()
{
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("--version")});
    return process.waitForFinished(5000) && process.exitCode() == 0;
}

bool chromeAvailable()
{
    return !pvf::html::HtmlBrowser::detectExecutable().isEmpty();
}

std::unique_ptr<ProjectDocument> makeDocument(const QString& projectDir,
                                              QString* error)
{
    ProjectSettings settings;
    settings.name = QStringLiteral("Media");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    return ProjectDocument::create(projectDir, settings, error);
}

QString contentFile(const QString& assetDirectory)
{
    const QStringList names = QDir(assetDirectory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (names.isEmpty())
        return {};
    return QDir(assetDirectory).filePath(names.first());
}

QString makeVideo(const QString& output)
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        return {};

    QProcess process;
    process.start(QStringLiteral("ffmpeg"),
                  {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                   QStringLiteral("error"), QStringLiteral("-y"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"),
                   QStringLiteral("-i"),
                   QStringLiteral("testsrc=size=96x54:rate=30:duration=1"),
                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                   QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), output});
    if (!process.waitForFinished(120000))
    {
        process.kill();
        process.waitForFinished();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QFile::exists(output) ? output : QString();
}

QString makePvhtmlBundle(const QString& directory)
{
    if (!QDir().mkpath(directory))
        return {};
    QFile file(QDir(directory).filePath(QStringLiteral("index.html")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(R"(<!doctype html><html><head><style>
html, body { margin: 0; padding: 0; width: 100%; height: 100%;
             background: #ff0000; }
</style></head><body></body></html>)");
    file.close();
    return directory;
}

Entity makeAssetEntity(AssetType type, const AssetHash& hash, int start, int sourceIn)
{
    Entity entity;
    entity.uuid = model::newUuid();
    entity.name = type == AssetType::Html ? QStringLiteral("HTML")
                                          : QStringLiteral("视频");
    entity.start = start;
    entity.end = start + 30;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = hash;
    entity.content.asset.type = type;
    entity.content.asset.sourceIn = sourceIn;
    return entity;
}


bool writeTestFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

// A .pvhtml bundle whose page paints itself from the numeric `progress`
// variable, declared with default 0 in the manifest.  The host feeds the
// value through the pvforgeframe event (detail.vars), so a frame rendered with
// progress 0 is black, 50 is half red and 100 is full red: a keyframe-driven
// variable is visible in the pixels, not just in the request.
QString makeVariableBundle(const QString& directory)
{
    if (!QDir().mkpath(directory))
        return {};
    const QByteArray manifest = R"JSON({
  "entry": "index.html",
  "logicalSize": { "width": 800, "height": 120 },
  "timeDependent": false,
  "variables": { "progress": 0 }
})JSON";
    const QByteArray html = R"HTML(<!doctype html><html><head><style>
html, body { margin: 0; padding: 0; width: 100%; height: 100%;
             background: #000000; overflow: hidden; }
</style></head><body><script>
(function () {
  window.addEventListener("pvforgeframe", function (event) {
    var vars = (event.detail || {}).vars || {};
    var progress = Number(vars.progress || 0);
    var red = Math.round(Math.max(0, Math.min(100, progress)) * 2.55);
    document.body.style.background = "rgb(" + red + ",0,0)";
  });
})();
</script></body></html>)HTML";
    if (!writeTestFile(QDir(directory).filePath(QStringLiteral("pvhtml.json")),
                       manifest))
        return {};
    if (!writeTestFile(QDir(directory).filePath(QStringLiteral("index.html")),
                       html))
        return {};
    return directory;
}

bool sameImage(const QImage& first, const QImage& second)
{
    return !first.isNull() && !second.isNull() && first == second;
}

// Bounded poll for a background thumbnail.  A real browser renders one 240x150
// frame in well under a second; ten seconds means the renderer is unavailable
// on this machine (broken Chrome sandbox, missing GPU driver) rather than slow,
// so the case skips instead of stalling the suite into a ctest timeout.
constexpr int kThumbnailWaitMs = 10000;

QImage waitForThumbnail(MediaServices& media, const AssetHash& hash,
                        const QSize& target, int timeoutMs = kThumbnailWaitMs)
{
    QElapsedTimer wait;
    wait.start();
    QImage image = media.htmlThumbnail(hash, target);
    while (image.isNull() && wait.elapsed() < timeoutMs)
    {
        QTest::qWait(50);
        image = media.htmlThumbnail(hash, target);
    }
    return image;
}

// A .pvhtml bundle plus its import, but deliberately *not* placed on the
// timeline: the asset-bin-only case of the rework ticket (§3).
std::optional<assets::StoredAsset> importPvhtmlOnly(ProjectDocument& document,
                                                    const QString& bundle,
                                                    QString* error)
{
    return document.assets().importPvhtml(bundle, error);
}

} // namespace

class MediaServicesTests : public QObject
{
    Q_OBJECT

private slots:
    void videoProviderMapsProjectFrames();
    void videoProviderWithoutAssetIsEmpty();
    void exportVideoFrameUsesFullResolution();
    void htmlProviderRendersBundle();
    void htmlPrefetchStopsAtVariableChange();
    void htmlPrefetchCoversOneSecondPerActiveEntity();
    void htmlPrefetchScalesWithActiveEntityCount();
    void htmlPrefetchStaticEntityCostsOneRequest();
    void htmlPrefetchCapsAtHardLimit();
    void htmlPrefetchCappedPlanKeepsFillOrder();
    void htmlPrefetchFillsCache();
    void exportHtmlFrameUsesRequestedSize();
    void htmlScaleChangesRequestSize();
    void exportSnapshotSurvivesDocumentEdits();
    void htmlThumbnailRendersFrameZeroOfAsset();
    void htmlThumbnailLazyStartsForAssetBinOnly();
    void htmlThumbnailDoesNotRestartAfterStop();
    void htmlThumbnailSharesOneRenderSize();
    void htmlVarsKeyframesDriveRenderedFrames();
    void htmlProgressBundleFillIsMonotonic();
    void htmlExportFrameMatchesPreview();
};

void MediaServicesTests::videoProviderMapsProjectFrames()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/video.pvproj"), &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString source = makeVideo(temp.path() + QStringLiteral("/clip.mp4"));
    if (source.isEmpty())
        QSKIP("ffmpeg is unavailable");

    const auto stored = document->assets().importFile(source, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    const Entity entity =
        makeAssetEntity(AssetType::Video, stored->hash, 10, 5);
    document->mutableProject().entities.insert(entity.uuid, entity);

    AppSettings settings;
    settings.videoProxyWidth = 64;
    settings.videoMemoryFrames = 8;

    MediaServices media(document.get(), settings);

    // projectFrame 12.5 -> local frame 2 -> source frame sourceIn + 2 = 7.
    const QImage image = media.videoFrame(entity.uuid, 12.5);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 64);
    QVERIFY(media.videoSequenceForTest(stored->hash) != nullptr);

    video2::VideoFrames decoder(64, 8);
    video2::Request request;
    request.path = contentFile(stored->directory);
    request.sourceFrame = 5 + 2;
    request.fps = 30;
    request.proxy = true;
    const QImage expected = decoder.frame(request);
    QVERIFY(!expected.isNull());
    QCOMPARE(image.size(), expected.size());
    QVERIFY(sameImage(image, expected));

    // A proxy width change drops the old sequence/decoder and rebuilds them.
    AppSettings wide = settings;
    wide.videoProxyWidth = 96;
    media.applySettings(wide);
    const QImage fullWidth = media.videoFrame(entity.uuid, 12.5);
    QVERIFY(!fullWidth.isNull());
    QCOMPARE(fullWidth.width(), 96);

    // An unknown entity id keeps the render2 placeholder.
    QVERIFY(media.videoFrame(QStringLiteral("missing-entity"), 12.5).isNull());
}

void MediaServicesTests::videoProviderWithoutAssetIsEmpty()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/missing.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Video, QString(64, QLatin1Char('a')),
                                    0, 0);
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QVERIFY(media.videoFrame(entity.uuid, 0.0).isNull());
    QCOMPARE(media.cacheRoot(), temp.path() + QStringLiteral("/missing.pvproj/.cache"));

    // Wrong asset type is not served by the video provider.
    entity.content.asset.type = AssetType::Image;
    document->mutableProject().entities.insert(entity.uuid, entity);
    QVERIFY(media.videoFrame(entity.uuid, 0.0).isNull());
}

void MediaServicesTests::exportVideoFrameUsesFullResolution()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/export.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString source = makeVideo(temp.path() + QStringLiteral("/clip.mp4"));
    if (source.isEmpty())
        QSKIP("ffmpeg is unavailable");

    const auto stored = document->assets().importFile(source, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    // The test source is 96x54; the preview proxy is capped at 64 px so a full
    // resolution frame is visibly different.
    const Entity entity =
        makeAssetEntity(AssetType::Video, stored->hash, 10, 5);
    document->mutableProject().entities.insert(entity.uuid, entity);

    AppSettings settings;
    settings.videoProxyWidth = 64;
    settings.videoMemoryFrames = 8;
    MediaServices media(document.get(), settings);

    const QImage preview = media.videoFrame(entity.uuid, 12.5);
    QVERIFY(!preview.isNull());
    QCOMPARE(preview.width(), 64);

    // projectFrame 12.5 -> local 2 -> source 5 + 2 = 7, decoded from the
    // original file at its own size.
    const QImage full = media.exportVideoFrame(entity.uuid, 12.5);
    QVERIFY(!full.isNull());
    QCOMPARE(full.size(), QSize(96, 54));

    QVERIFY(media.exportVideoFrame(QStringLiteral("missing-entity"), 12.5).isNull());
}

void MediaServicesTests::htmlProviderRendersBundle()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/html.pvproj"), &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());

    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    const Entity entity =
        makeAssetEntity(AssetType::Html, stored->hash, 10, 0);
    document->mutableProject().entities.insert(entity.uuid, entity);

    AppSettings settings;
    settings.htmlScale = 0.5;
    settings.htmlMaxWidth = 1280;

    MediaServices media(document.get(), settings);
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));
    QVERIFY(media.htmlRunning());

    QImage image;
    QTRY_VERIFY_WITH_TIMEOUT(!(image = media.htmlFrame(entity.uuid, 12.5)).isNull(),
                             120000);
    QCOMPARE(image.size(), media.htmlRequestSizeForTest(entity.uuid));
    // The bundle has no pvhtml.json, so its logical size is the 800x120
    // default; the 0.5 preview clarity yields a 400x60 texture.
    QCOMPARE(image.size(), QSize(400, 60));

    const QColor center = image.pixelColor(image.width() / 2, image.height() / 2);
    QVERIFY2(center.red() > 200 && center.green() < 60 && center.blue() < 60,
             qPrintable(center.name()));
}

void MediaServicesTests::htmlPrefetchStopsAtVariableChange()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/prefetch.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 10, 0);
    entity.end = 200;
    Keyframe speedAt30;
    speedAt30.frame = 30;
    speedAt30.htmlVars.insert(QStringLiteral("speed"), 1.0);
    Keyframe speedAt60;
    speedAt60.frame = 60;
    speedAt60.htmlVars.insert(QStringLiteral("speed"), 2.0);
    entity.keyframes = {speedAt30, speedAt60};
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QVector<html2::FrameRequest> captured;
    media.setHtmlPrefetchSinkForTest(
        [&captured](const QVector<html2::FrameRequest>& requests) {
            captured = requests;
        });

    // The next var.* key is at entity-relative 30, i.e. project frame 40.
    media.prefetchHtml(20, true);
    QCOMPARE(captured.size(), 30);

    // Forward first: project frames 20..39 -> local 10..29.
    QCOMPARE(captured.at(0).localFrame, 10);
    QCOMPARE(captured.at(19).localFrame, 29);
    // Then backwards: local 9 down to 0.
    QCOMPARE(captured.at(20).localFrame, 9);
    QCOMPARE(captured.last().localFrame, 0);
    for (const html2::FrameRequest& request : captured) {
        QVERIFY(request.localFrame < 30);
        QCOMPARE(request.vars.value(QStringLiteral("speed")).toDouble(), 1.0);
    }
}

// Timeline frames of a plan: how many distinct local frames one asset hash
// covers.  A plan is one batch for all active HTML entities, so the coverage
// of the playback window is per entity, not the raw batch size.
int localFrameSpan(const QVector<html2::FrameRequest>& requests,
                   const AssetHash& hash)
{
    int low = std::numeric_limits<int>::max();
    int high = std::numeric_limits<int>::min();
    for (const html2::FrameRequest& request : requests)
    {
        if (request.assetHash != hash)
            continue;
        low = std::min(low, request.localFrame);
        high = std::max(high, request.localFrame);
    }
    return high < low ? 0 : high - low + 1;
}

// `count` time-dependent HTML entities spanning the same timeline range, all
// sharing one imported bundle (identical pixels, distinct entity slots).  The
// bundle must not declare timeDependent=false, so it keeps the default true.
QVector<Entity> makePrefetchEntities(ProjectDocument& document,
                                     const QString& bundle, int count,
                                     int start, int end, QString* error)
{
    QVector<Entity> entities;
    const auto stored = document.assets().importPvhtml(bundle, error);
    if (!stored.has_value())
        return entities;
    for (int i = 0; i < count; ++i)
    {
        Entity entity = makeAssetEntity(AssetType::Html, stored->hash, start, 0);
        entity.end = end;
        document.mutableProject().entities.insert(entity.uuid, entity);
        entities.append(entity);
    }
    return entities;
}

// Imports a bundle whose manifest declares timeDependent=false, then rewrites
// the stored asset metadata to match (importPvhtml reads pvhtml.json, the
// timeline request reads the asset metadata).
bool forceStoredAssetStatic(ProjectDocument& document, const AssetHash& hash)
{
    const QString metadataPath =
        QDir(document.assets().assetsDirectory())
            .filePath(hash + QStringLiteral("/asset.json"));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonObject metadata = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    metadata.insert(QStringLiteral("timeDependent"), false);
    return writeTestFile(metadataPath, QJsonDocument(metadata).toJson());
}

QVector<html2::FrameRequest> capturePrefetch(MediaServices& media, int frame)
{
    QVector<html2::FrameRequest> captured;
    media.setHtmlPrefetchSinkForTest(
        [&captured](const QVector<html2::FrameRequest>& requests) {
            captured = requests;
        });
    media.prefetchHtml(frame, true);
    return captured;
}

// The 11 x 1920x1080 HTML entity project from the playback report: the old
// flat 60-request budget covered 5 timeline frames (0.17 s) at 30 fps and the
// background renderer could never catch up.  One plan must now cover at least
// one second of playback per active entity.
void MediaServicesTests::htmlPrefetchCoversOneSecondPerActiveEntity()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/eleven.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    constexpr int kEntities = 11;
    constexpr int kStart = 100;
    const QVector<Entity> entities = makePrefetchEntities(
        *document, makePvhtmlBundle(temp.path() + QStringLiteral("/source")),
        kEntities, kStart, kStart + 10000, &error);
    QVERIFY2(entities.size() == kEntities, qPrintable(error));

    MediaServices media(document.get(), AppSettings());
    const int fps = document->project().settings.fps;
    const int playhead = kStart + 300;
    const QVector<html2::FrameRequest> plan = capturePrefetch(media, playhead);

    // 11 entity slots, each covering the first second of playback: the old
    // 60-request plan gave one entity 60/kEntities = 5 frames, this one gives
    // it a full fps frames.
    const AssetHash hash = entities.first().content.asset.hash;
    QCOMPARE(plan.size(), kEntities * fps);
    QVERIFY2(localFrameSpan(plan, hash) >= fps,
             qPrintable(QStringLiteral("per-entity span %1 < fps %2")
                            .arg(localFrameSpan(plan, hash))
                            .arg(fps)));
    // Forward first: the playhead frame leads its entity's run...
    QCOMPARE(plan.at(0).localFrame, playhead - kStart);
    // ...and the next entity's second starts right after the first one's.
    QCOMPARE(plan.at(fps).localFrame, playhead - kStart);
    QCOMPARE(plan.last().localFrame, playhead - kStart + fps - 1);
}

// Two entities and one entity: the budget follows activeEntities x fps, and an
// entity outside the playhead is excluded from both the count and the fill.
void MediaServicesTests::htmlPrefetchScalesWithActiveEntityCount()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/scale.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const QVector<Entity> entities =
        makePrefetchEntities(*document, bundle, 2, 100, 10000, &error);
    QCOMPARE(entities.size(), 2);

    // A third HTML clip that ends long before the playhead must not add demand.
    Entity past = makeAssetEntity(AssetType::Html, entities.first().content.asset.hash,
                                  0, 0);
    past.end = 50;
    document->mutableProject().entities.insert(past.uuid, past);

    MediaServices media(document.get(), AppSettings());
    const int fps = document->project().settings.fps;

    // Same bundle, two entity slots: the batch is twice a single second.
    const QVector<html2::FrameRequest> two = capturePrefetch(media, 300);
    QCOMPARE(two.size(), 2 * fps);
    QVERIFY(localFrameSpan(two, entities.first().content.asset.hash) >= fps);
    QCOMPARE(two.at(0).localFrame, 200);
    QCOMPARE(two.at(fps).localFrame, 200);

    // Move the playhead past the second entity's end (kept at 10000, so shrink
    // it first): the plan is then one entity's second.
    Entity shortened = entities.at(1);
    shortened.end = 150;
    document->mutableProject().entities.insert(shortened.uuid, shortened);
    const QVector<html2::FrameRequest> one = capturePrefetch(media, 300);
    QCOMPARE(one.size(), fps);
    // One entity's whole second is warmed forward from the playhead, so the run
    // is the playhead frame followed by the rest of that second, not the same
    // frame repeated.
    for (int i = 0; i < one.size(); ++i)
        QCOMPARE(one.at(i).localFrame, 200 + i);
}

// A timeDependent=false layer shares one cache entry for every timeline frame:
// it keeps its single request and does not eat a second of the budget.
void MediaServicesTests::htmlPrefetchStaticEntityCostsOneRequest()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/static.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const QVector<Entity> entities =
        makePrefetchEntities(*document, bundle, 3, 100, 10000, &error);
    QCOMPARE(entities.size(), 3);

    const AssetHash hash = entities.first().content.asset.hash;
    QVERIFY(forceStoredAssetStatic(*document, hash));

    MediaServices media(document.get(), AppSettings());
    const int fps = document->project().settings.fps;
    const QVector<html2::FrameRequest> plan = capturePrefetch(media, 300);

    // 3 static entities -> 3 requests, all at the playhead frame, none cached
    // as time-dependent and no follows-on frames.
    QCOMPARE(plan.size(), 3);
    QVERIFY(plan.size() < fps);
    for (const html2::FrameRequest& request : plan)
    {
        QVERIFY(!request.timeDependent);
        QCOMPARE(request.localFrame, 200);
    }
}

// The hard ceiling keeps a pathological project (or a 60 fps one with many
// layers) from submitting an unbounded batch to the serial prefetch worker.
void MediaServicesTests::htmlPrefetchCapsAtHardLimit()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/cap.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    // 20 entities x 60 fps = 1200 demanded > 600 hard cap.
    const QVector<Entity> entities =
        makePrefetchEntities(*document, bundle, 20, 100, 10000, &error);
    QCOMPARE(entities.size(), 20);
    document->mutableProject().settings.fps = 60;

    MediaServices media(document.get(), AppSettings());
    // Playhead 400: 300 frames remain before the first entity's clip start.
    const QVector<html2::FrameRequest> plan = capturePrefetch(media, 400);
    QCOMPARE(plan.size(), 600);
    QCOMPARE(plan.at(0).localFrame, 300);
    // 600 requests / 20 entity slots = 30 forward frames each (60 fps demands
    // 60), then the budget is gone and no backward fill starts.
    QCOMPARE(plan.at(20).localFrame, 301);
    QCOMPARE(plan.at(599).localFrame, 329);
}

// Truncated to the cap, the plan still fills forward before backward: at the
// cap the playhead frame comes first and the frames just before it come last.
void MediaServicesTests::htmlPrefetchCappedPlanKeepsFillOrder()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/order.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    QVector<Entity> entities =
        makePrefetchEntities(*document, bundle, 1, 100, 10000, &error);
    QCOMPARE(entities.size(), 1);
    document->mutableProject().settings.fps = 60;

    // One entity, no keyframes: forward from the playhead up to the cap (60
    // demanded), then backwards for the remaining 540 frames.
    MediaServices media(document.get(), AppSettings());
    const QVector<html2::FrameRequest> capped = capturePrefetch(media, 700);
    QCOMPARE(capped.size(), 600);
    QCOMPARE(capped.at(0).localFrame, 600);
    // Forward fill covers the first second...
    QCOMPARE(capped.at(59).localFrame, 659);
    // ...and the rest goes backwards, ending just below the playhead.
    QCOMPARE(capped.at(60).localFrame, 599);
    QCOMPARE(capped.at(61).localFrame, 598);
}

void MediaServicesTests::htmlPrefetchFillsCache()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/warm.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 10, 0);
    entity.end = 16;
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));

    media.prefetchHtml(12, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        media.htmlServiceForTest()->stats().prefetched > 0, 120000);

    // The prefetched frame is a cache hit, so the cache-only provider returns
    // it immediately instead of queuing another render.
    QVERIFY(!media.htmlFrame(entity.uuid, 12.0).isNull());
}

void MediaServicesTests::exportHtmlFrameUsesRequestedSize()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/html-export.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());

    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    const Entity entity =
        makeAssetEntity(AssetType::Html, stored->hash, 10, 0);
    document->mutableProject().entities.insert(entity.uuid, entity);

    // The preview request is deliberately much smaller than the export size.
    AppSettings settings;
    settings.htmlScale = 0.25;
    settings.htmlMaxWidth = 1280;

    MediaServices media(document.get(), settings);
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));

    const QImage image =
        media.exportHtmlFrame(entity.uuid, 12.5, QSize(480, 270));
    QVERIFY(!image.isNull());
    // 480x270 over the 640x360 project is a 0.75 pixel density: the 800x120
    // logical HTML frame is rendered at 600x90 and drawn into the same model
    // rectangle as the preview.  2x/4x exports never grow the model size.
    QCOMPARE(image.size(), QSize(600, 90));

    const QColor center = image.pixelColor(image.width() / 2, image.height() / 2);
    QVERIFY2(center.red() > 200 && center.green() < 60 && center.blue() < 60,
             qPrintable(center.name()));
}

void MediaServicesTests::htmlScaleChangesRequestSize()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/scale.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    // The bundle declares no pvhtml.json size, so the logical size is the
    // 800x120 default.  htmlScale is a fraction of that model size, not of the
    // project resolution.
    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    const Entity entity =
        makeAssetEntity(AssetType::Html, stored->hash, 10, 0);
    document->mutableProject().entities.insert(entity.uuid, entity);

    AppSettings settings;
    settings.htmlScale = 0.25;
    settings.htmlMaxWidth = 2000;
    MediaServices media(document.get(), settings);
    QCOMPARE(media.htmlRequestSizeForTest(entity.uuid), QSize(200, 30));

    settings.htmlScale = 0.5;
    media.applySettings(settings);
    QCOMPARE(media.htmlRequestSizeForTest(entity.uuid), QSize(400, 60));

    // The width cap keeps the logical aspect ratio and rounds to integers.
    settings.htmlScale = 1.0;
    settings.htmlMaxWidth = 100;
    media.applySettings(settings);
    QCOMPARE(media.htmlRequestSizeForTest(entity.uuid), QSize(100, 15));
}

void MediaServicesTests::exportSnapshotSurvivesDocumentEdits()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/snapshot.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString source = makeVideo(temp.path() + QStringLiteral("/clip.mp4"));
    if (source.isEmpty())
        QSKIP("ffmpeg is unavailable");
    const auto stored = document->assets().importFile(source, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    const Entity entity =
        makeAssetEntity(AssetType::Video, stored->hash, 10, 5);
    document->mutableProject().entities.insert(entity.uuid, entity);

    // The export worker gets an independent copy: dropping the entity and
    // changing the canvas afterwards must not change its decoded frames
    // (issue #126).
    next::MediaExportSession session(document->project(), document->assets(),
                                     AppSettings(), QSize(640, 360));
    document->mutableProject().entities.clear();
    document->mutableProject().settings.width = 32;

    const QImage frame = session.videoFrame(entity.uuid, 12.5);
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.size(), QSize(96, 54));
    QVERIFY(session.videoFrame(QStringLiteral("gone"), 12.5).isNull());
    session.finish();
}

void MediaServicesTests::htmlThumbnailRendersFrameZeroOfAsset()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/thumb.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    MediaServices media(document.get(), AppSettings());

    // The asset does not have to be on the timeline: the thumbnail is keyed by
    // the asset, rendered at frame 0 with the declared default variables, and
    // scaled into the requested box (48x48 icon and 240x150 card share the
    // 240x36 entry of the 800x120 default logical size).  The first miss lazily
    // starts the HTML service, so on a machine without a working Chrome this
    // call records the failure and keeps returning null.
    const QImage first = media.htmlThumbnail(stored->hash, QSize(48, 48));
    QVERIFY(first.isNull());
    const QImage again = media.htmlThumbnail(stored->hash, QSize(48, 48));
    // Whatever the renderer does, it must never throw an image back from an
    // unfinished request.
    QVERIFY(again.isNull());

    QString startError;
    const bool started = media.start(&startError);
    if (!started)
    {
        // No Chrome: the asset bin keeps its placeholder and the call is
        // side-effect free apart from the recorded failure.
        QVERIFY(media.htmlStartFailed());
        QVERIFY(media.htmlThumbnail(stored->hash, QSize(48, 48)).isNull());
        QSKIP("Chrome/Chromium is not installed");
    }

    // A bounded wait, not the usual two-minute QTRY: a machine where Chrome
    // starts but never produces a frame (broken sandbox, missing GPU driver)
    // must not stall the whole suite into a ctest timeout.
    const QImage thumb = waitForThumbnail(media, stored->hash, QSize(240, 150));
    if (thumb.isNull())
        QSKIP("HTML renderer produced no thumbnail (Chrome unavailable)");
    QCOMPARE(thumb.size(), QSize(240, 36));

    // The 48x48 icon shares the same cache entry: a repaint never queues again.
    QCOMPARE(media.htmlThumbnail(stored->hash, QSize(48, 48)), thumb);

    // Unknown hash / non-HTML / invalid size are no-ops.
    QVERIFY(media.htmlThumbnail(AssetHash(), QSize(48, 48)).isNull());
    QVERIFY(media.htmlThumbnail(stored->hash, QSize()).isNull());
}

void MediaServicesTests::htmlThumbnailLazyStartsForAssetBinOnly()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/bin-only.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = importPvhtmlOnly(*document, bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    // The whole point of the case: the asset exists only in the asset bin.
    QVERIFY(document->project().entities.isEmpty());

    MediaServices media(document.get(), AppSettings());
    // §3.1: the asset bin counts as HTML content, so the service may start.
    QVERIFY(media.projectHasHtmlForTest());
    QCOMPARE(media.thumbnailPendingForTest(), 0);

    // §3.2: the first miss starts the service on demand and queues the render;
    // a second repaint must not queue the same asset twice.
    const QImage queued = media.htmlThumbnail(stored->hash, QSize(240, 150));
    if (!media.htmlRunning())
    {
        // Real Chrome cannot run here (e.g. the SUID sandbox is broken): the
        // lazy start recorded its failure, so the placeholder stays and the
        // call is a no-op from now on.
        QVERIFY(media.htmlStartFailed());
        QCOMPARE(media.thumbnailPendingForTest(), 0);
        QVERIFY(media.htmlThumbnail(stored->hash, QSize(240, 150)).isNull());
        QCOMPARE(media.thumbnailPendingForTest(), 0);
        QSKIP("Chrome/Chromium cannot start on this machine");
    }

    QVERIFY(queued.isNull());
    QCOMPARE(media.thumbnailPendingForTest(), 1);
    QVERIFY(media.htmlThumbnail(stored->hash, QSize(240, 150)).isNull());
    QCOMPARE(media.thumbnailPendingForTest(), 1);

    const QImage rendered = waitForThumbnail(media, stored->hash, QSize(240, 150));
    if (rendered.isNull())
        QSKIP("HTML renderer produced no thumbnail (Chrome unavailable)");
    // The cache is written before frameReady is queued, so the wait above can
    // return while the clearing slot is still in the event queue: give the
    // queued connection a moment before reading the pending set.
    QTRY_COMPARE_WITH_TIMEOUT(media.thumbnailPendingForTest(), 0, 5000);
    QCOMPARE(media.htmlThumbnail(stored->hash, QSize(48, 48)), rendered);
}

void MediaServicesTests::htmlThumbnailDoesNotRestartAfterStop()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/stopped.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    // Not on the timeline: the only reason the service ever ran is the bin.
    const auto stored = importPvhtmlOnly(*document, bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));
    QVERIFY(document->project().entities.isEmpty());

    MediaServices media(document.get(), AppSettings());
    QString startError;
    const bool started = media.start(&startError);
    if (!started)
    {
        QVERIFY(media.htmlStartFailed());
        QSKIP("Chrome/Chromium cannot start on this machine");
    }
    QVERIFY(media.htmlRunning());

    // Queue one thumbnail but do not wait for it to finish: a finished render
    // lands in the frame cache (memory and, in real runs, disk), and after the
    // restart below a cache hit would legitimately return an image — making the
    // "stopped service stays down and queues nothing" assertions meaningless.
    // This case is about the pending set, not about pixels.
    QVERIFY(media.htmlThumbnail(stored->hash, QSize(240, 150)).isNull());
    QCOMPARE(media.thumbnailPendingForTest(), 1);

    media.stop();
    QVERIFY(!media.htmlRunning());
    // §3.3: the queued thumbnails died with the browsers.
    QCOMPARE(media.thumbnailPendingForTest(), 0);

    // §3.2: an explicit stop() is final for thumbnails — no restart, no queue.
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(media.htmlThumbnail(stored->hash, QSize(240, 150)).isNull());
        QVERIFY(!media.htmlRunning());
        QCOMPARE(media.thumbnailPendingForTest(), 0);
    }

    // An explicit start() (or applySettings() rebuilding the service) brings
    // it back with an empty pending set, so requesting works again.  A frame
    // that was in flight while stop() joined the workers may legitimately have
    // reached the disk tier before the caches were dropped, in which case the
    // first call after the restart is a cache hit instead of a queue: accept
    // both, and only require that the request ends with a usable image.
    QVERIFY2(media.start(&startError), qPrintable(startError));
    QVERIFY(media.htmlRunning());
    QCOMPARE(media.thumbnailPendingForTest(), 0);
    const QImage afterRestart =
        media.htmlThumbnail(stored->hash, QSize(240, 150));
    if (afterRestart.isNull())
    {
        QCOMPARE(media.thumbnailPendingForTest(), 1);
        const QImage rendered = waitForThumbnail(media, stored->hash,
                                                 QSize(240, 150));
        if (rendered.isNull())
            QSKIP("HTML renderer produced no thumbnail (Chrome unavailable)");
        QTRY_COMPARE_WITH_TIMEOUT(media.thumbnailPendingForTest(), 0, 5000);
    }
    else
    {
        QCOMPARE(media.thumbnailPendingForTest(), 0);
    }
    media.stop();
}

void MediaServicesTests::htmlThumbnailSharesOneRenderSize()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/size.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    MediaServices media(document.get(), AppSettings());

    // The 48x48 list icon asks first.  The renderer must still be asked for the
    // shared 240x150 base box (800x120 logical -> 240x36), never for 48x48:
    // a per-caller size produced 48x7 for the icon and a second cache entry,
    // so the hover card could not reuse the icon's render (and vice versa).
    Q_UNUSED(media.htmlThumbnail(stored->hash, QSize(48, 48)));
    QCOMPARE(media.lastThumbnailRequestSizeForTest(), QSize(240, 36));

    Q_UNUSED(media.htmlThumbnail(stored->hash, QSize(240, 150)));
    QCOMPARE(media.lastThumbnailRequestSizeForTest(), QSize(240, 36));

    // Still a no-op for the requests that never reach the renderer.
    QVERIFY(media.htmlThumbnail(AssetHash(), QSize(48, 48)).isNull());
    QVERIFY(media.htmlThumbnail(stored->hash, QSize()).isNull());

    // Deliberately no Chrome-dependent assertion: the request size is recorded
    // before any browser start, so this regression guard runs even when the
    // HTML renderer is unavailable.
}


void MediaServicesTests::htmlVarsKeyframesDriveRenderedFrames()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/vars.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle =
        makeVariableBundle(temp.path() + QStringLiteral("/varsource"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 0, 0);
    entity.end = 59;
    // Whole-frame keyframes drive the numeric variable: 0 at the clip start,
    // 100 halfway.  The linear default easing makes frame 25 render at ~50.
    Keyframe atZero;
    atZero.frame = 0;
    atZero.htmlVars.insert(QStringLiteral("progress"), 0.0);
    Keyframe atFifty;
    atFifty.frame = 50;
    atFifty.htmlVars.insert(QStringLiteral("progress"), 100.0);
    entity.keyframes = {atZero, atFifty};
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));
    QVERIFY(media.htmlRunning());

    const auto redAt = [&](int frame) {
        const QImage image = media.htmlFrameBlocking(entity.uuid, frame, 15000);
        if (image.isNull())
            return -1;
        return image.pixelColor(image.width() / 2, image.height() / 2).red();
    };

    const int red0 = redAt(0);
    const int red25 = redAt(25);
    const int red50 = redAt(50);
    QVERIFY2(red0 >= 0 && red25 >= 0 && red50 >= 0,
             qPrintable(QStringLiteral("renderer produced no frame: %1/%2/%3")
                            .arg(red0).arg(red25).arg(red50)));

    // progress 0 -> black body, 100 -> full red body.
    QVERIFY2(red0 < 40, qPrintable(QStringLiteral("frame 0 red=%1, expected "
                                                  "the manifest default 0")
                                       .arg(red0)));
    QVERIFY2(red50 > 215, qPrintable(QStringLiteral("frame 50 red=%1, expected "
                                                    "progress 100")
                                         .arg(red50)));
    // The halfway frame must interpolate, not hold either endpoint: this is
    // what a whole-frame htmlVars keyframe pair is supposed to do.
    QVERIFY2(red25 > red0 + 40 && red25 < red50 - 40,
             qPrintable(QStringLiteral("frame 25 red=%1, not between %2 and %3")
                            .arg(red25).arg(red0).arg(red50)));

    // Seeking back to the same frame renders exactly the same pixels.
    const QImage again = media.htmlFrameBlocking(entity.uuid, 25, 15000);
    QVERIFY(!again.isNull());
    QCOMPARE(again.pixelColor(again.width() / 2, again.height() / 2).red(),
             red25);
}


void MediaServicesTests::htmlProgressBundleFillIsMonotonic()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/progress.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    // The real asset used for the progress-bar experiment: transparent page,
    // 60%-wide track at the bottom, fill painted as a teal->light-blue gradient
    // whose width is driven by the `progress` variable.
    const QString bundle = QFINDTESTDATA("data/progress-bar");
    QVERIFY2(!bundle.isEmpty(), "progress-bar test data is missing");
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 0, 0);
    entity.end = 99;
    Keyframe atStart;
    atStart.frame = 0;
    atStart.htmlVars.insert(QStringLiteral("progress"), 0.0);
    Keyframe atEnd;
    atEnd.frame = 99;
    atEnd.htmlVars.insert(QStringLiteral("progress"), 100.0);
    entity.keyframes = {atStart, atEnd};
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));
    QVERIFY(media.htmlRunning());

    // Count the gradient-coloured pixels: everything with meaningful alpha and
    // a green/blue tint.  The translucent black track (alpha ~31), the dark
    // label and the white label shadow all stay outside this predicate.
    const auto fillPixels = [&](int frame) {
        const QImage image = media.htmlFrameBlocking(entity.uuid, frame, 15000);
        if (image.isNull())
            return -1;
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor color = image.pixelColor(x, y);
                if (color.alpha() < 60)
                    continue;
                if (color.blue() > 140 && color.green() > 140
                    && color.red() < 170)
                    ++count;
            }
        }
        return count;
    };

    const int frames[] = {0, 25, 50, 75, 99};
    int filled[5] = {-1, -1, -1, -1, -1};
    for (int i = 0; i < 5; ++i)
        filled[i] = fillPixels(frames[i]);
    for (int i = 0; i < 5; ++i)
    {
        QVERIFY2(filled[i] >= 0,
                 qPrintable(QStringLiteral("frame %1 rendered no image")
                                .arg(frames[i])));
    }
    // progress 0 -> empty bar, 100 -> full bar.
    QVERIFY2(filled[4] > filled[0] + 500,
             qPrintable(QStringLiteral("frame 99 fill=%1, frame 0 fill=%2: "
                                       "the bar never grew")
                            .arg(filled[4]).arg(filled[0])));
    QVERIFY2(filled[0] * 20 < filled[4],
             qPrintable(QStringLiteral("frame 0 fill=%1 is not close to empty")
                            .arg(filled[0])));
    // Monotonic growth through the intermediate keyframe samples.
    for (int i = 1; i < 5; ++i)
    {
        QVERIFY2(filled[i] >= filled[i - 1],
                 qPrintable(QStringLiteral("fill shrank: frame %1 fill=%2, "
                                           "frame %3 fill=%4")
                                .arg(frames[i]).arg(filled[i])
                                .arg(frames[i - 1]).arg(filled[i - 1])));
    }
    // The halfway frame must be visibly between the endpoints, not a step.
    QVERIFY2(filled[2] > filled[0] + (filled[4] - filled[0]) / 5,
             qPrintable(QStringLiteral("frame 50 fill=%1 sits too close to "
                                       "the empty bar (%2)")
                            .arg(filled[2]).arg(filled[0])));
    QVERIFY2(filled[2] < filled[4] - (filled[4] - filled[0]) / 5,
             qPrintable(QStringLiteral("frame 50 fill=%1 sits too close to "
                                       "the full bar (%2)")
                            .arg(filled[2]).arg(filled[4])));

    // Same frame twice -> identical pixels.
    QCOMPARE(fillPixels(50), filled[2]);
}


void MediaServicesTests::htmlExportFrameMatchesPreview()
{
    if (!gitAvailable())
        QSKIP("git is unavailable");
    if (!chromeAvailable())
        QSKIP("Chrome/Chromium is not installed");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString error;
    auto document = makeDocument(temp.path() + QStringLiteral("/parity.pvproj"),
                                 &error);
    QVERIFY2(document != nullptr, qPrintable(error));

    const QString bundle = makePvhtmlBundle(temp.path() + QStringLiteral("/source"));
    QVERIFY(!bundle.isEmpty());
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 0, 0);
    entity.end = 29;
    // A non-trivial transform: both paths must place and scale the layer the
    // same way, so a mismatch in either would show up in the pixel diff.
    entity.transform.x = 37.0;
    entity.transform.y = -11.0;
    entity.transform.scaleX = 1.2;
    entity.transform.scaleY = 0.8;
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    QString startError;
    QVERIFY2(media.start(&startError), qPrintable(startError));
    QVERIFY(media.htmlRunning());

    const int frame = 12;
    const QImage preview = media.htmlFrameBlocking(entity.uuid, frame, 15000);
    QVERIFY2(!preview.isNull(), "preview renderer produced no frame");

    // Export path: final (third) browser.  exportPixelSize is the encoded
    // canvas, so a 2x canvas doubles the layer's logical size (800x120 -> the
    // request size below); the composition must stay identical to the preview.
    const QSize canvas(2 * document->project().settings.width,
                       2 * document->project().settings.height);
    const QImage exported = media.exportHtmlFrame(entity.uuid, frame, canvas);
    QVERIFY2(!exported.isNull(), "export renderer produced no frame");
    media.stopExportHtml();

    QCOMPARE(preview.size(), media.htmlRequestSizeForTest(entity.uuid));
    QCOMPARE(exported.size(), QSize(1600, 240));

    // Compare at the preview resolution: the export's extra sharpness is not a
    // composition difference.
    const QImage scaled = exported.scaled(preview.size(), Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
    qint64 channelDiff = 0;
    qint64 samples = 0;
    int maxDiff = 0;
    int overTolerance = 0;
    for (int y = 0; y < preview.height(); ++y)
    {
        for (int x = 0; x < preview.width(); ++x)
        {
            const QColor a = preview.pixelColor(x, y);
            const QColor b = scaled.pixelColor(x, y);
            const int dr = std::abs(a.red() - b.red());
            const int dg = std::abs(a.green() - b.green());
            const int db = std::abs(a.blue() - b.blue());
            channelDiff += dr + dg + db;
            samples += 3;
            maxDiff = std::max({maxDiff, dr, dg, db});
            if (dr > 16 || dg > 16 || db > 16)
                ++overTolerance;
        }
    }
    const qint64 pixels = qint64(preview.width()) * preview.height();
    const double meanDiff = double(channelDiff) / double(samples);
    // A few pixels along the layer edge legitimately differ (the low-res
    // texture is upscaled by the viewport, the export is rendered sharp), so
    // allow a small band: mean channel error under 8 and at most 2% of pixels
    // off by more than 16.
    QVERIFY2(meanDiff < 8.0,
             qPrintable(QStringLiteral("mean channel diff %1 (max %2)")
                            .arg(meanDiff).arg(maxDiff)));
    QVERIFY2(overTolerance * 100 <= pixels * 2,
             qPrintable(QStringLiteral("%1 of %2 pixels differ by more than 16")
                            .arg(overTolerance).arg(pixels)));
}

QTEST_MAIN(MediaServicesTests)
#include "MediaServicesTests.moc"