// Asset sizing/shape tests for render2 (issue #127).
//
// Video/HTML entities are placed by the asset metadata/logical size; decoded
// frames are only textures.  A separate executable keeps Render2Tests.cpp
// inside the code-size ratchet.

#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

using namespace pvf;
using namespace pvf::model;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3")
                                .arg(QString::fromLatin1(file))
                                .arg(line)
                                .arg(QString::fromLatin1(expression))
                        << '\n';
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

Project makeProject(int width, int height)
{
    Project project;
    project.settings.width = width;
    project.settings.height = height;
    project.settings.background = QStringLiteral("#101014");
    return project;
}

Entity makeEntity(const Uuid& uuid)
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = 0;
    entity.end = 10;
    return entity;
}

Entity makeAssetEntity(const Uuid& uuid, AssetType type, const AssetHash& hash)
{
    Entity entity = makeEntity(uuid);
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = type;
    entity.content.asset.hash = hash;
    return entity;
}

bool writeAssetFiles(const QString& projectRoot, const AssetHash& hash,
                     const QString& type, int width, int height)
{
    const QString directory = projectRoot + QStringLiteral("/assets/") + hash;
    if (!QDir().mkpath(directory))
        return false;

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), type);
    metadata.insert(QStringLiteral("name"), QStringLiteral("sizing"));
    metadata.insert(QStringLiteral("width"), width);
    metadata.insert(QStringLiteral("height"), height);

    if (type == QStringLiteral("html"))
    {
        if (!QDir().mkpath(directory + QStringLiteral("/bundle")))
            return false;
        metadata.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        QFile entry(directory + QStringLiteral("/bundle/index.html"));
        if (!entry.open(QIODevice::WriteOnly))
            return false;
        entry.write("<html><body style='background:#00ff00'></body></html>");
        entry.close();
    }
    else
    {
        QFile content(directory + QStringLiteral("/content.bin"));
        if (!content.open(QIODevice::WriteOnly))
            return false;
        content.write("asset-bytes");
        content.close();
    }

    QFile json(directory + QStringLiteral("/asset.json"));
    if (!json.open(QIODevice::WriteOnly))
        return false;
    json.write(QJsonDocument(metadata).toJson());
    json.close();
    return true;
}

QImage solidFrame(int width, int height, const QColor& color)
{
    QImage frame(width, height, QImage::Format_ARGB32_Premultiplied);
    frame.fill(color);
    return frame;
}

QImage renderWithFrame(const Project& project, const assets::AssetStore& store,
                       const QSize& outputSize, const QImage& frame,
                       AssetType type)
{
    render2::Options options;
    options.outputSize = outputSize;
    const render2::FrameProvider provider = [frame](const Uuid&, double) {
        return frame;
    };
    if (type == AssetType::Video)
        options.videoFrame = provider;
    else
        options.htmlFrame = provider;

    render2::SceneWorld world(project, store);
    anim::Cache cache;
    return render2::renderFrame(project, store, options, world, cache);
}

bool nearColor(const QColor& actual, const QColor& expected, int tolerance = 3)
{
    return std::abs(actual.red() - expected.red()) <= tolerance
           && std::abs(actual.green() - expected.green()) <= tolerance
           && std::abs(actual.blue() - expected.blue()) <= tolerance;
}

QRect inkBounds(const QImage& image, const QColor& background)
{
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) == background)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right < left)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

// Issue #127: video/HTML entities are placed by the asset metadata/logical
// size.  The decoded frame is only a texture, so a 960 px preview proxy and a
// full-resolution export must paint the same world rectangle.
void testAssetModelSizeIgnoresFramePixels()
{
    const QColor background(QStringLiteral("#101014"));
    const QColor green(0, 255, 0);

    for (const AssetType type : {AssetType::Video, AssetType::Html})
    {
        QTemporaryDir directory;
        const AssetHash hash = type == AssetType::Video
                                   ? AssetHash(64, QLatin1Char('1'))
                                   : AssetHash(64, QLatin1Char('2'));
        CHECK(writeAssetFiles(directory.path(), hash,
                              type == AssetType::Video
                                  ? QStringLiteral("video")
                                  : QStringLiteral("html"),
                              100, 50));

        Project project = makeProject(200, 100);
        const Entity entity = makeAssetEntity(QStringLiteral("asset"), type, hash);
        project.entities.insert(entity.uuid, entity);

        assets::AssetStore store(directory.path());
        CHECK(store.get(hash).has_value());

        // 1x preview: a proxy frame far bigger than the model size.
        const QImage preview =
            renderWithFrame(project, store, QSize(200, 100),
                            solidFrame(160, 80, green), type);
        const QRect previewInk = inkBounds(preview, background);
        // 2x export: the same entity with a full-resolution frame.
        const QImage exported =
            renderWithFrame(project, store, QSize(400, 200),
                            solidFrame(320, 160, green), type);
        const QRect exportInk = inkBounds(exported, background);

        // 100x50 model units at the canvas centre: (50,25)-(149,74) at 1x.
        CHECK(previewInk == QRect(50, 25, 100, 50));
        // The 2x image is exactly twice the preview in both axes.
        CHECK(exportInk == QRect(100, 50, 200, 100));
        CHECK(exportInk.width() == previewInk.width() * 2);
        CHECK(exportInk.height() == previewInk.height() * 2);

        // The collision/selection shape shares the same rectangle.
        render2::SceneWorld world(project, store);
        const QPainterPath shape = world.shapeOf(entity.uuid, 0);
        const QRectF bounds = shape.boundingRect();
        CHECK(std::abs(bounds.left() + 50.0) < 1e-6);
        CHECK(std::abs(bounds.top() + 25.0) < 1e-6);
        CHECK(std::abs(bounds.width() - 100.0) < 1e-6);
        CHECK(std::abs(bounds.height() - 50.0) < 1e-6);
    }
}

// Issue #127: a 4x export must draw the HTML frame at the model size, not at
// the decoded pixel size, so an oversized final frame cannot spill out of the
// canvas.
void testHtml4xExportIsNotCropped()
{
    QTemporaryDir directory;
    const AssetHash hash(64, QLatin1Char('3'));
    CHECK(writeAssetFiles(directory.path(), hash, QStringLiteral("html"), 100,
                          50));

    Project project = makeProject(200, 100);
    const Entity entity =
        makeAssetEntity(QStringLiteral("html"), AssetType::Html, hash);
    project.entities.insert(entity.uuid, entity);

    assets::AssetStore store(directory.path());
    CHECK(store.get(hash).has_value());
    const QColor background(QStringLiteral("#101014"));
    const QColor green(0, 255, 0);

    // 4x export: the final HTML frame is rendered at 4x the logical size.
    const QImage image =
        renderWithFrame(project, store, QSize(800, 400),
                        solidFrame(400, 200, green), AssetType::Html);
    CHECK(inkBounds(image, background) == QRect(200, 100, 400, 200));
    CHECK(nearColor(image.pixelColor(400, 200), green));
    // Outside the model rectangle stays canvas background: nothing was cropped
    // into the edges, and the content did not expand to 4x the model size.
    CHECK(nearColor(image.pixelColor(0, 0), background));
    CHECK(nearColor(image.pixelColor(799, 399), background));
    CHECK(nearColor(image.pixelColor(199, 200), background));
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    const QVector<QPair<QString, void (*)()>> tests = {
        {QStringLiteral("assetModelSizeIgnoresFramePixels"),
         testAssetModelSizeIgnoresFramePixels},
        {QStringLiteral("html4xExportIsNotCropped"), testHtml4xExportIsNotCropped},
    };

    QString requested;
    if (argc > 1)
        requested = QString::fromLocal8Bit(argv[1]);

    int ran = 0;
    for (const auto& entry : tests)
    {
        if (!requested.isEmpty() && entry.first != requested)
            continue;
        entry.second();
        ++ran;
    }

    if (!requested.isEmpty() && ran == 0)
    {
        QTextStream(stderr) << QStringLiteral("Unknown test: %1\n").arg(requested);
        return 2;
    }

    QTextStream(stdout) << QStringLiteral("render2-asset: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
