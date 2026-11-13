#include "next/app/MediaServices.h"

#include "assets/AssetStore.h"
#include "htmlcommon/HtmlBrowser.h"
#include "html2/HtmlFrameService.h"
#include "next/app/ProjectDocument.h"
#include "video2/VideoFrames.h"
#include "video2/VideoSequenceService.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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
    if (!media.start(&startError))
        QSKIP("the HTML service could not start");
    QVERIFY(media.htmlRunning());

    QImage image;
    QTRY_VERIFY_WITH_TIMEOUT(!(image = media.htmlFrame(entity.uuid, 12.5)).isNull(),
                             120000);
    QCOMPARE(image.size(), media.htmlRequestSizeForTest());
    QCOMPARE(image.size(), QSize(320, 180));

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
    Track speed;
    speed.keys.append(Keyframe{30, 1.0, EasingSpec(), QString()});
    speed.keys.append(Keyframe{60, 2.0, EasingSpec(), QString()});
    entity.tracks.insert(QStringLiteral("var.speed"), speed);
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
    if (!media.start(&startError))
        QSKIP("the HTML service could not start");

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
    if (!media.start(&startError))
        QSKIP("the HTML service could not start");

    const QImage image =
        media.exportHtmlFrame(entity.uuid, 12.5, QSize(480, 270));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(480, 270));

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

    AppSettings settings;
    settings.htmlScale = 0.25;
    settings.htmlMaxWidth = 2000;
    MediaServices media(document.get(), settings);
    QCOMPARE(media.htmlRequestSizeForTest(), QSize(160, 90));

    settings.htmlScale = 0.5;
    media.applySettings(settings);
    QCOMPARE(media.htmlRequestSizeForTest(), QSize(320, 180));

    // The width cap keeps the project aspect ratio and rounds to integers.
    settings.htmlScale = 1.0;
    settings.htmlMaxWidth = 100;
    media.applySettings(settings);
    QCOMPARE(media.htmlRequestSizeForTest(), QSize(100, 56));
}

QTEST_MAIN(MediaServicesTests)
#include "MediaServicesTests.moc"
