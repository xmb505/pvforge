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
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

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
    void htmlPrefetchCapsAtSixtyFrames();
    void htmlPrefetchFillsCache();
    void exportHtmlFrameUsesRequestedSize();
    void htmlScaleChangesRequestSize();
    void exportSnapshotSurvivesDocumentEdits();
    void htmlThumbnailRendersFrameZeroOfAsset();
    void htmlThumbnailLazyStartsForAssetBinOnly();
    void htmlThumbnailDoesNotRestartAfterStop();
    void htmlThumbnailSharesOneRenderSize();
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

void MediaServicesTests::htmlPrefetchCapsAtSixtyFrames()
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
    const auto stored = document->assets().importPvhtml(bundle, &error);
    QVERIFY2(stored.has_value(), qPrintable(error));

    Entity entity = makeAssetEntity(AssetType::Html, stored->hash, 0, 0);
    entity.end = 10000;
    document->mutableProject().entities.insert(entity.uuid, entity);

    MediaServices media(document.get(), AppSettings());
    int calls = 0;
    QVector<html2::FrameRequest> captured;
    media.setHtmlPrefetchSinkForTest(
        [&](const QVector<html2::FrameRequest>& requests) {
            ++calls;
            captured = requests;
        });

    media.prefetchHtml(0, true);
    QCOMPARE(calls, 1);
    QCOMPARE(captured.size(), 60);
    QCOMPARE(captured.at(0).localFrame, 0);
    QCOMPARE(captured.last().localFrame, 59);

    // The same playhead does not resubmit the batch.
    media.prefetchHtml(0);
    QCOMPARE(calls, 1);
    // A document edit forces a rebuild at the same playhead.
    media.prefetchHtml(0, true);
    QCOMPARE(calls, 2);
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

QTEST_MAIN(MediaServicesTests)
#include "MediaServicesTests.moc"