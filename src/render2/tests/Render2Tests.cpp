#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

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
#define CHECK_DETAIL(condition, detail)                                      \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
            QTextStream(stderr) << QStringLiteral("  detail: ") << (detail)  \
                                << '\n';                                     \
        check((condition), #condition, __FILE__, __LINE__);                  \
    } while (0)

Project makeProject(int width = 100, int height = 100)
{
    Project project;
    project.settings.width = width;
    project.settings.height = height;
    project.settings.background = QStringLiteral("#101014");
    return project;
}

Entity makeEntity(const Uuid& uuid, int start = 0, int end = 10,
                  const Uuid& parent = Uuid())
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.parentUuid = parent;
    entity.start = start;
    entity.end = end;
    return entity;
}

Entity makeVector(const Uuid& uuid, double width, double height,
                  const QColor& color, int start = 0, int end = 10,
                  const Uuid& parent = Uuid())
{
    Entity entity = makeEntity(uuid, start, end, parent);
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = width;
    entity.content.vector.height = height;
    entity.content.vector.fillColor = color.name(QColor::HexArgb);
    entity.content.vector.strokeWidth = 0.0;
    return entity;
}

void add(Project& project, const Entity& entity)
{
    project.entities.insert(entity.uuid, entity);
}

QImage render(const Project& project, const assets::AssetStore& store,
              double frame = 0.0, const Uuid& camera = Uuid(),
              const QSize& outputSize = QSize(),
              const QColor& background = QColor())
{
    render2::Options options;
    options.projectFrame = frame;
    options.cameraUuid = camera;
    options.outputSize = outputSize;
    options.background = background;

    render2::SceneWorld world(project, store);
    anim::Cache cache;
    return render2::renderFrame(project, store, options, world, cache);
}

QImage renderWorld(const Project& project, const assets::AssetStore& store,
                   const QRectF& worldRect, bool transparent,
                   const Uuid& camera = Uuid(),
                   const QSize& outputSize = QSize())
{
    render2::Options options;
    options.cameraUuid = camera;
    options.worldRect = worldRect;
    options.transparentOutsideCanvas = transparent;
    options.outputSize = outputSize;

    render2::SceneWorld world(project, store);
    anim::Cache cache;
    return render2::renderFrame(project, store, options, world, cache);
}

QByteArray encodeWebp(const QImage& image)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(80);
    if (!writer.write(image))
        return {};
    return data;
}

bool nearColor(const QColor& actual, const QColor& expected, int tolerance = 3)
{
    return std::abs(actual.red() - expected.red()) <= tolerance
           && std::abs(actual.green() - expected.green()) <= tolerance
           && std::abs(actual.blue() - expected.blue()) <= tolerance;
}

int countInkPixels(const QImage& image, const QColor& background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) != background)
                ++count;
        }
    }
    return count;
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

// Strongest "ink" covered by any pixel: 255 for black on white, 0 for white.
int maxInkStrength(const QImage& image)
{
    int best = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            const int ink = 255 - std::min({color.red(), color.green(),
                                            color.blue()});
            best = std::max(best, ink);
        }
    }
    return best;
}

Track keyedTrack(std::initializer_list<std::pair<int, double>> keys)
{
    Track track;
    for (const auto& key : keys)
        track.keys.append(Keyframe{key.first, key.second, {}, {}});
    return track;
}

Entity makeSpecialEntity(ContentKind kind, const Uuid& uuid)
{
    Entity entity = makeEntity(uuid);
    entity.transform.x = 320.0;
    entity.transform.y = 180.0;

    switch (kind)
    {
    case ContentKind::ComicText:
        entity.content.kind = kind;
        entity.content.comic.text = QStringLiteral("PV");
        entity.content.comic.pixelSize = 56;
        entity.content.comic.color = QStringLiteral("#000000");
        entity.content.comic.shape = QStringLiteral("rect");
        entity.content.comic.boxWidth = 260.0;
        entity.content.comic.boxHeight = 150.0;
        entity.content.comic.cornerRadius = 24.0;
        entity.content.comic.tail = false;
        entity.content.comic.fillColor = QStringLiteral("#ffffff");
        entity.content.comic.bubbleStrokeColor = QStringLiteral("#000000");
        entity.content.comic.bubbleStrokeWidth = 6.0;
        break;
    case ContentKind::RadialLines:
        entity.content.kind = kind;
        entity.content.radial.count = 24;
        entity.content.radial.radius = 130.0;
        entity.content.radial.innerRatio = 0.1;
        entity.content.radial.width = 8.0;
        entity.content.radial.color = QStringLiteral("#ff0000");
        break;
    case ContentKind::FlowLines:
        entity.content.kind = kind;
        entity.content.flow.count = 14;
        entity.content.flow.length = 240.0;
        entity.content.flow.spacing = 20.0;
        entity.content.flow.thickness = 5.0;
        entity.content.flow.area = 280.0;
        entity.content.flow.bend = 40.0;
        entity.content.flow.angle = 10.0;
        entity.content.flow.color = QStringLiteral("#ff0000");
        break;
    case ContentKind::HalftoneDots:
        entity.content.kind = kind;
        entity.content.halftone.layout = QStringLiteral("grid");
        entity.content.halftone.area = 220.0;
        entity.content.halftone.spacing = 14.0;
        entity.content.halftone.dotSize = 5.5;
        entity.content.halftone.minRatio = 0.6;
        entity.content.halftone.color = QStringLiteral("#ff0000");
        break;
    case ContentKind::BrandWall:
        entity.content.kind = kind;
        entity.content.brandWall.logos = {QStringLiteral("PV"),
                                          QStringLiteral("ACME")};
        entity.content.brandWall.columns = 4;
        entity.content.brandWall.rows = 3;
        entity.content.brandWall.spacing = 10.0;
        entity.content.brandWall.logoSize = 70.0;
        entity.content.brandWall.colors = {QStringLiteral("#ff0000"),
                                           QStringLiteral("#00ff00")};
        entity.content.brandWall.alternate = true;
        break;
    default:
        break;
    }
    return entity;
}

int countRedBorderPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() >= 150 && color.green() <= 100
                && color.blue() <= 100 && color.red() - color.green() >= 60)
                ++count;
        }
    }
    return count;
}

bool hasBrightPixel(const QImage& image, const QRect& area, int minimum = 200)
{
    const QRect clipped = area.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
    {
        for (int x = clipped.left(); x <= clipped.right(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() >= minimum && color.green() >= minimum
                && color.blue() >= minimum)
                return true;
        }
    }
    return false;
}

bool writeAssetFiles(const QString& projectRoot, const AssetHash& hash,
                     const QString& type, const QByteArray& content,
                     int width = 0, int height = 0)
{
    const QString directory = projectRoot + QStringLiteral("/assets/") + hash;
    if (!QDir().mkpath(directory))
        return false;

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), type);
    metadata.insert(QStringLiteral("name"), QStringLiteral("broken"));
    if (width > 0)
        metadata.insert(QStringLiteral("width"), width);
    if (height > 0)
        metadata.insert(QStringLiteral("height"), height);

    QFile json(directory + QStringLiteral("/asset.json"));
    if (!json.open(QIODevice::WriteOnly))
        return false;
    json.write(QJsonDocument(metadata).toJson());

    QFile file(directory + QStringLiteral("/content.bin"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(content) == content.size();
}

Entity makeAssetEntity(const Uuid& uuid, AssetType type, const AssetHash& hash)
{
    Entity entity = makeEntity(uuid);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = type;
    entity.content.asset.hash = hash;
    return entity;
}

void testRedVectorCenterPixel()
{
    Project project = makeProject(100, 100);
    Entity rect = makeVector(QStringLiteral("rect"), 60, 60, Qt::red);
    rect.transform.x = 50.0;
    rect.transform.y = 50.0;
    add(project, rect);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(image.size() == QSize(100, 100));
    CHECK(nearColor(image.pixelColor(50, 50), Qt::red));
    CHECK(!nearColor(image.pixelColor(1, 1), Qt::red));
}

void testImageAssetOnCanvas()
{
    QTemporaryDir directory;
    QImage source(20, 20, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(0, 255, 0));
    const QString path = directory.path() + QStringLiteral("/picture.png");
    CHECK(source.save(path));

    assets::AssetStore store(directory.path());
    QString error;
    const auto stored = store.importFile(path, &error);
    CHECK(stored.has_value());

    Project project = makeProject(100, 100);
    Entity entity = makeEntity(QStringLiteral("image"));
    entity.transform.x = 50.0;
    entity.transform.y = 50.0;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Image;
    entity.content.asset.hash = stored ? stored->hash : AssetHash();
    add(project, entity);

    const QImage image = render(project, store);
    CHECK(nearColor(image.pixelColor(50, 50), QColor(0, 255, 0)));
    CHECK(!nearColor(image.pixelColor(5, 5), QColor(0, 255, 0)));
}

void testTextIsNotBlank()
{
    Project project = makeProject(200, 100);
    Entity entity = makeEntity(QStringLiteral("text"));
    entity.transform.x = 100.0;
    entity.transform.y = 50.0;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("PV");
    entity.content.text.pixelSize = 48;
    entity.content.text.color = QStringLiteral("#ffffff");
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, 0, Uuid(), QSize(),
                                QColor(Qt::black));

    int painted = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) != QColor(Qt::black))
                ++painted;
        }
    }
    CHECK(painted > 0);
}

void testChildFollowsParent()
{
    Project project = makeProject(100, 100);
    Entity parent = makeEntity(QStringLiteral("parent"));
    parent.transform.x = 70.0;
    parent.transform.y = 30.0;
    add(project, parent);
    add(project, makeVector(QStringLiteral("child"), 10, 10, Qt::red, 0, 10,
                            QStringLiteral("parent")));

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(nearColor(image.pixelColor(70, 30), Qt::red));
    CHECK(nearColor(image.pixelColor(10, 10), QColor(QStringLiteral("#101014"))));
}

void testHiddenAndInvisible()
{
    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    {
        Project project = makeProject(100, 100);
        Entity entity = makeVector(QStringLiteral("hidden"), 100, 100, Qt::red);
        entity.transform.x = 50.0;
        entity.transform.y = 50.0;
        entity.hidden = true;
        add(project, entity);

        const QImage image = render(project, store);
        CHECK(nearColor(image.pixelColor(50, 50),
                        QColor(QStringLiteral("#101014"))));
    }

    {
        Project project = makeProject(100, 100);
        Entity entity = makeVector(QStringLiteral("invisible"), 100, 100, Qt::red);
        entity.transform.x = 50.0;
        entity.transform.y = 50.0;
        entity.visible = false;
        add(project, entity);

        const QImage image = render(project, store);
        CHECK(nearColor(image.pixelColor(50, 50),
                        QColor(QStringLiteral("#101014"))));
    }
}

void testOpacityHalfBlend()
{
    Project project = makeProject(100, 100);
    Entity entity = makeVector(QStringLiteral("half"), 100, 100, Qt::red);
    entity.transform.x = 50.0;
    entity.transform.y = 50.0;
    entity.transform.opacity = 0.5;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, 0, Uuid(), QSize(),
                                QColor(Qt::white));

    const QColor blended = image.pixelColor(50, 50);
    CHECK(blended.red() >= 250);
    CHECK(std::abs(blended.green() - 127) <= 4);
    CHECK(std::abs(blended.blue() - 127) <= 4);
}

void testMissingAssetCheckerboard()
{
    Project project = makeProject(320, 180);
    Entity entity = makeEntity(QStringLiteral("missing"));
    entity.transform.x = 160.0;
    entity.transform.y = 90.0;
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Image;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    const QColor background(QStringLiteral("#101014"));
    const QColor first = image.pixelColor(8, 8);
    const QColor second = image.pixelColor(24, 8);
    CHECK(first != second);
    CHECK(first != background);
    CHECK(second != background);
}

void testMissingImageCard()
{
    const AssetHash hash(64, QLatin1Char('a'));
    Project project = makeProject(320, 180);
    Entity entity = makeAssetEntity(QStringLiteral("offline-image"),
                                    AssetType::Image, hash);
    entity.transform.x = 160.0;
    entity.transform.y = 90.0;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(countRedBorderPixels(image) > 20);
    CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
    CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
          == QStringLiteral("素材丢失"));
}

void testMissingVideoAndHtmlCards()
{
    const AssetHash hash(64, QLatin1Char('b'));
    const QVector<AssetType> types = {AssetType::Video, AssetType::Html};

    for (const AssetType type : types)
    {
        Project project = makeProject(320, 180);
        Entity entity = makeAssetEntity(QStringLiteral("offline"), type, hash);
        entity.transform.x = 160.0;
        entity.transform.y = 90.0;
        add(project, entity);

        QTemporaryDir directory;
        assets::AssetStore store(directory.path());

        // A stale provider frame must not hide a genuinely missing asset.
        QImage stale(20, 20, QImage::Format_ARGB32_Premultiplied);
        stale.fill(QColor(0, 255, 0));
        render2::Options options;
        options.videoFrame = [&](const Uuid&, double) { return stale; };
        options.htmlFrame = [&](const Uuid&, double) { return stale; };
        render2::SceneWorld world(project, store);
        anim::Cache cache;
        const QImage image =
            render2::renderFrame(project, store, options, world, cache);

        CHECK(countRedBorderPixels(image) > 20);
        CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
        CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
              == QStringLiteral("素材丢失"));
    }
}

void testPresentAssetKeepsPendingPlaceholder()
{
    QTemporaryDir directory;
    QImage source(20, 20, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(0, 255, 0));
    const QString path = directory.path() + QStringLiteral("/picture.png");
    CHECK(source.save(path));

    assets::AssetStore store(directory.path());
    QString error;
    const auto stored = store.importFile(path, &error);
    CHECK(stored.has_value());
    if (!stored)
        return;

    {
        Project project = makeProject(320, 180);
        Entity entity = makeAssetEntity(QStringLiteral("pending-video"),
                                        AssetType::Video, stored->hash);
        entity.transform.x = 160.0;
        entity.transform.y = 90.0;
        add(project, entity);

        render2::Options options;
        options.videoFrame = [](const Uuid&, double) { return QImage(); };
        render2::SceneWorld world(project, store);
        anim::Cache cache;
        const QImage image =
            render2::renderFrame(project, store, options, world, cache);

        CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
              == QStringLiteral("视频"));
        CHECK(countRedBorderPixels(image) == 0);
        CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
    }

    {
        Project project = makeProject(320, 180);
        Entity entity = makeAssetEntity(QStringLiteral("pending-html"),
                                        AssetType::Html, stored->hash);
        entity.transform.x = 160.0;
        entity.transform.y = 90.0;
        add(project, entity);

        render2::Options options;
        options.htmlFrame = [](const Uuid&, double) { return QImage(); };
        render2::SceneWorld world(project, store);
        anim::Cache cache;
        const QImage image =
            render2::renderFrame(project, store, options, world, cache);

        CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
              == QStringLiteral("HTML"));
        CHECK(countRedBorderPixels(image) == 0);
        CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
    }
}

void testCorruptImageShowsMissingCard()
{
    QTemporaryDir directory;
    const AssetHash hash(64, QLatin1Char('c'));
    CHECK(writeAssetFiles(directory.path(), hash, QStringLiteral("image"),
                          QByteArrayLiteral("not an image"), 200, 100));

    Project project = makeProject(320, 180);
    Entity entity = makeAssetEntity(QStringLiteral("corrupt"),
                                    AssetType::Image, hash);
    entity.transform.x = 160.0;
    entity.transform.y = 90.0;
    add(project, entity);

    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(store.get(hash).has_value());
    CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
          == QStringLiteral("素材丢失"));
    CHECK(countRedBorderPixels(image) > 20);
    CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
}

void testSmallMissingRectDoesNotOverflow()
{
    QTemporaryDir directory;
    const AssetHash hash(64, QLatin1Char('d'));
    CHECK(writeAssetFiles(directory.path(), hash, QStringLiteral("image"),
                          QByteArrayLiteral("not an image"), 60, 30));

    Project project = makeProject(200, 200);
    Entity entity = makeAssetEntity(QStringLiteral("tiny"),
                                    AssetType::Image, hash);
    entity.transform.x = 100.0;
    entity.transform.y = 100.0;
    add(project, entity);

    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(countRedBorderPixels(image) > 10);
    // 60x30 content: only the headline fits.  Nothing bright may leak out.
    const QRect inside(70, 85, 60, 30);
    CHECK(hasBrightPixel(image, inside));
    const QRect outsideLeft(0, 0, 70, 200);
    const QRect outsideRight(130, 0, 70, 200);
    CHECK(!hasBrightPixel(image, outsideLeft));
    CHECK(!hasBrightPixel(image, outsideRight));
}

void testReimportSameHashRestoresImage()
{
    QTemporaryDir directory;
    QImage source(20, 20, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(0, 255, 0));
    const QString path = directory.path() + QStringLiteral("/picture.png");
    CHECK(source.save(path));

    assets::AssetStore store(directory.path());
    QString error;
    const auto stored = store.importFile(path, &error);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(400, 300);
    Entity entity = makeAssetEntity(QStringLiteral("image"),
                                    AssetType::Image, stored->hash);
    entity.transform.x = 200.0;
    entity.transform.y = 150.0;
    add(project, entity);

    const QImage imported = render(project, store);
    CHECK(nearColor(imported.pixelColor(200, 150), QColor(0, 255, 0)));
    CHECK(countRedBorderPixels(imported) == 0);

    CHECK(store.remove(stored->hash));
    const QImage offline = render(project, store);
    CHECK(!nearColor(offline.pixelColor(200, 150), QColor(0, 255, 0)));
    CHECK(countRedBorderPixels(offline) > 20);
    CHECK(render2::placeholderLabelForTest(store, entity.content.asset)
          == QStringLiteral("素材丢失"));

    const auto restored = store.importFile(path, &error);
    CHECK(restored.has_value());
    CHECK(restored && restored->hash == stored->hash);
    const QImage online = render(project, store);
    CHECK(nearColor(online.pixelColor(200, 150), QColor(0, 255, 0)));
    CHECK(countRedBorderPixels(online) == 0);
}

void testCameraCrop()
{
    Project project = makeProject(200, 200);
    add(project, makeVector(QStringLiteral("red"), 40, 40, Qt::red));
    project.entities[QStringLiteral("red")].transform.x = 100.0;
    project.entities[QStringLiteral("red")].transform.y = 100.0;

    add(project, makeVector(QStringLiteral("blue"), 20, 20, Qt::blue));
    project.entities[QStringLiteral("blue")].transform.x = 20.0;
    project.entities[QStringLiteral("blue")].transform.y = 20.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 100.0;
    camera.transform.y = 100.0;
    camera.transform.scaleX = 0.5;
    camera.transform.scaleY = 0.5;
    add(project, camera);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, 0, QStringLiteral("camera"),
                                QSize(100, 100));

    CHECK(nearColor(image.pixelColor(50, 50), Qt::red));
    CHECK(nearColor(image.pixelColor(4, 4),
                    QColor(QStringLiteral("#101014"))));

    int bluePixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.blue() > 200 && color.red() < 80)
                ++bluePixels;
        }
    }
    CHECK(bluePixels == 0);
}

void testCameraKeepsCanvasCoordinates()
{
    Project project = makeProject(200, 200);
    add(project, makeVector(QStringLiteral("marker"), 20, 20, Qt::red));
    project.entities[QStringLiteral("marker")].transform.x = 120.0;
    project.entities[QStringLiteral("marker")].transform.y = 120.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 100.0;
    camera.transform.y = 100.0;
    camera.transform.scaleX = 0.5;
    camera.transform.scaleY = 0.5;
    add(project, camera);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, 0, QStringLiteral("camera"),
                                QSize(100, 100));

    CHECK(nearColor(image.pixelColor(70, 70), Qt::red));
    CHECK(nearColor(image.pixelColor(50, 50),
                    QColor(QStringLiteral("#101014"))));
    CHECK(nearColor(image.pixelColor(20, 20),
                    QColor(QStringLiteral("#101014"))));
}

void testCameraNegativeScaleFlips()
{
    Project project = makeProject(100, 100);
    add(project, makeVector(QStringLiteral("marker"), 20, 20, Qt::red));
    project.entities[QStringLiteral("marker")].transform.x = 30.0;
    project.entities[QStringLiteral("marker")].transform.y = 50.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 50.0;
    camera.transform.y = 50.0;
    camera.transform.scaleX = -1.0;
    camera.transform.scaleY = 1.0;
    add(project, camera);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, 0, QStringLiteral("camera"),
                                QSize(100, 100));

    CHECK(nearColor(image.pixelColor(70, 50), Qt::red));
    CHECK(nearColor(image.pixelColor(30, 50),
                    QColor(QStringLiteral("#101014"))));
}

void testWorldRectRendersOutsideCanvas()
{
    Project project = makeProject(100, 100);
    Entity outside = makeVector(QStringLiteral("outside"), 20, 20, Qt::red);
    outside.transform.x = -50.0;
    outside.transform.y = -50.0;
    add(project, outside);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QRectF world(-100, -100, 300, 300);
    const QImage image =
        renderWorld(project, store, world, false, Uuid(), QSize(300, 300));

    // World (-50,-50) maps to output (50,50); the canvas centre stays the
    // project background.
    CHECK(nearColor(image.pixelColor(50, 50), Qt::red));
    CHECK(nearColor(image.pixelColor(150, 150),
                    QColor(QStringLiteral("#101014"))));
    CHECK(nearColor(image.pixelColor(0, 0),
                    QColor(QStringLiteral("#101014"))));
}

void testCameraIgnoresWorldRect()
{
    Project project = makeProject(200, 200);
    add(project, makeVector(QStringLiteral("red"), 40, 40, Qt::red));
    project.entities[QStringLiteral("red")].transform.x = 100.0;
    project.entities[QStringLiteral("red")].transform.y = 100.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 100.0;
    camera.transform.y = 100.0;
    camera.transform.scaleX = 0.5;
    camera.transform.scaleY = 0.5;
    add(project, camera);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage baseline =
        render(project, store, 0, QStringLiteral("camera"), QSize(100, 100));
    const QImage withWorld =
        renderWorld(project, store, QRectF(-100, -100, 300, 300), true,
                    QStringLiteral("camera"), QSize(100, 100));

    CHECK(baseline == withWorld);
    CHECK(withWorld.pixelColor(0, 0).alpha() == 255);
    CHECK(nearColor(withWorld.pixelColor(50, 50), Qt::red));
}

void testTransparentOutsideCanvas()
{
    Project project = makeProject(100, 100);
    Entity outside = makeVector(QStringLiteral("outside"), 20, 20, Qt::red);
    outside.transform.x = -50.0;
    outside.transform.y = -50.0;
    add(project, outside);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QRectF world(-100, -100, 300, 300);
    const QImage image =
        renderWorld(project, store, world, true, Uuid(), QSize(300, 300));

    CHECK(image.pixelColor(0, 0).alpha() == 0);
    CHECK(image.pixelColor(299, 299).alpha() == 0);
    CHECK(nearColor(image.pixelColor(150, 150),
                    QColor(QStringLiteral("#101014"))));
    CHECK(image.pixelColor(150, 150).alpha() == 255);
    CHECK(nearColor(image.pixelColor(50, 50), Qt::red));
    CHECK(image.pixelColor(50, 50).alpha() == 255);

    const QImage opaque =
        renderWorld(project, store, world, false, Uuid(), QSize(300, 300));
    CHECK(opaque.pixelColor(0, 0).alpha() == 255);
}

void testTransparentWebpOverheadIsSmall()
{
    Project project = makeProject(100, 100);
    Entity centre = makeVector(QStringLiteral("centre"), 60, 60, Qt::red);
    centre.transform.x = 50.0;
    centre.transform.y = 50.0;
    add(project, centre);
    Entity outside = makeVector(QStringLiteral("outside"), 20, 20, Qt::blue);
    outside.transform.x = -50.0;
    outside.transform.y = -50.0;
    add(project, outside);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QRectF world(-200, -200, 500, 500);
    const QImage opaque =
        renderWorld(project, store, world, false, Uuid(), QSize(500, 500));
    const QImage transparent =
        renderWorld(project, store, world, true, Uuid(), QSize(500, 500));

    const QByteArray opaqueBytes = encodeWebp(opaque);
    const QByteArray transparentBytes = encodeWebp(transparent);
    CHECK(!opaqueBytes.isEmpty());
    CHECK(!transparentBytes.isEmpty());
    // WebP spends a tiny constant on the alpha plane.  The project background
    // is a flat colour that lossy WebP already compresses to almost nothing,
    // so the transparent file is not guaranteed to be globally smaller; the
    // point here is that a 25x larger world does not multiply its size.
    CHECK_DETAIL(transparentBytes.size() < opaqueBytes.size() + 4096,
                 QStringLiteral("transparent %1 bytes, opaque %2 bytes")
                     .arg(transparentBytes.size())
                     .arg(opaqueBytes.size()));
}

void testWorldPoseAndShape()
{
    Project project = makeProject(100, 100);
    Entity entity = makeVector(QStringLiteral("shape"), 20, 10, Qt::red);
    entity.transform.x = 50.0;
    entity.transform.y = 50.0;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    render2::SceneWorld world(project, store);

    const solver::Pose pose = world.poseOf(QStringLiteral("shape"), 0);
    CHECK(std::abs(pose.position.x() - 50.0) < 1e-6);
    CHECK(std::abs(pose.position.y() - 50.0) < 1e-6);
    CHECK(std::abs(pose.rotation) < 1e-6);

    const QPainterPath shape = world.shapeOf(QStringLiteral("shape"), 0);
    CHECK(shape.contains(QPointF(50.0, 50.0)));
    CHECK(!shape.contains(QPointF(0.0, 0.0)));
}

void testSolverSelfShapeTerminates()
{
    Project project = makeProject(100, 100);
    Entity ball = makeVector(QStringLiteral("ball"), 10, 10, Qt::red);
    ball.start = 0;
    ball.end = 60;
    ball.transform.x = 50.0;
    ball.transform.y = 10.0;
    Track xTrack;
    xTrack.keys = {{0, 50.0, {}, {}}};
    ball.tracks.insert(QStringLiteral("x"), xTrack);
    Track yTrack;
    yTrack.keys = {{0, 10.0, {}, {}}};
    ball.tracks.insert(QStringLiteral("y"), yTrack);

    SolverSegment gravity;
    gravity.id = QStringLiteral("gravity");
    gravity.effect = SolverEffect::Gravity;
    gravity.startFrame = 0;
    gravity.endFrame = 59;
    gravity.params.insert(QStringLiteral("targetUuid"),
                          QStringLiteral("ground"));
    ball.solvers.append(gravity);
    add(project, ball);

    Entity ground = makeEntity(QStringLiteral("ground"), 0, 60);
    add(project, ground);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    render2::SceneWorld world(project, store);

    // Before the entity-keyed recursion guard, sampling the ball's own shape
    // restarted the solver at every frame (59 -> 58 -> ...), which hung the UI.
    const solver::Pose pose = world.poseOf(QStringLiteral("ball"), 50);
    CHECK(std::isfinite(pose.position.x()));
    CHECK(std::isfinite(pose.position.y()));
    const QPainterPath shape = world.shapeOf(QStringLiteral("ball"), 50);
    CHECK(!shape.isEmpty());

    const QImage image = render(project, store, 50.0);
    CHECK(!image.isNull());
}

void testFractionalProjectFrame()
{
    Project project = makeProject(100, 100);
    Entity rect = makeVector(QStringLiteral("moving"), 4, 4, Qt::red);
    rect.transform.y = 50.0;
    Track xTrack;
    Keyframe startKey;
    startKey.frame = 0;
    startKey.value = 0.0;
    Keyframe endKey;
    endKey.frame = 10;
    endKey.value = 100.0;
    xTrack.keys = {startKey, endKey};
    rect.tracks.insert(QStringLiteral("x"), xTrack);
    add(project, rect);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage at5 = render(project, store, 5.0);
    const QImage at55 = render(project, store, 5.5);
    const QImage at6 = render(project, store, 6.0);

    // 4x4 rect: x=50 at frame 5, x=55 at 5.5, x=60 at 6.
    CHECK(nearColor(at5.pixelColor(50, 50), Qt::red));
    CHECK(nearColor(at6.pixelColor(60, 50), Qt::red));
    CHECK(nearColor(at55.pixelColor(55, 50), Qt::red));
    CHECK(!nearColor(at55.pixelColor(50, 50), Qt::red));
    CHECK(!nearColor(at5.pixelColor(55, 50), Qt::red));
}

void testFrameProviderReceivesFractionalFrame()
{
    Project project = makeProject(100, 100);
    Entity video = makeEntity(QStringLiteral("video"));
    video.content.kind = ContentKind::Asset;
    video.content.asset.type = AssetType::Video;
    add(project, video);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    render2::Options options;
    options.projectFrame = 5.5;
    double received = -1.0;
    options.videoFrame = [&](const Uuid&, double projectFrame) {
        received = projectFrame;
        return QImage();
    };

    render2::SceneWorld world(project, store);
    anim::Cache cache;
    render2::renderFrame(project, store, options, world, cache);
    CHECK(std::abs(received - 5.5) < 1e-12);
}

void testSpecialContentRender()
{
    const QVector<QPair<ContentKind, QString>> kinds = {
        {ContentKind::ComicText, QStringLiteral("comicText")},
        {ContentKind::RadialLines, QStringLiteral("radialLines")},
        {ContentKind::FlowLines, QStringLiteral("flowLines")},
        {ContentKind::HalftoneDots, QStringLiteral("halftoneDots")},
        {ContentKind::BrandWall, QStringLiteral("brandWall")},
    };

    for (const auto& entry : kinds)
    {
        Project project = makeProject(640, 360);
        add(project, makeSpecialEntity(entry.first, QStringLiteral("special")));

        QTemporaryDir directory;
        assets::AssetStore store(directory.path());
        const QImage image =
            render(project, store, 0, Uuid(), QSize(), QColor(Qt::white));

        const QString name = entry.second;
        const QRect bounds = inkBounds(image, Qt::white);
        CHECK_DETAIL(!bounds.isEmpty(), name);
        CHECK_DETAIL(countInkPixels(image, Qt::white) > 100, name);
        CHECK_DETAIL(bounds.width() >= 80, name);
        CHECK_DETAIL(bounds.height() >= 40, name);
        CHECK_DETAIL(bounds.width() <= 600, name);
        CHECK_DETAIL(bounds.height() <= 340, name);
        CHECK_DETAIL(bounds.intersects(QRect(280, 140, 80, 80)), name);

        if (entry.first == ContentKind::ComicText
            || entry.first == ContentKind::RadialLines)
        {
            CHECK_DETAIL(std::abs(bounds.center().x() - 320) <= 8, name);
            CHECK_DETAIL(std::abs(bounds.center().y() - 180) <= 8, name);
        }
        else if (entry.first == ContentKind::HalftoneDots)
        {
            CHECK_DETAIL(std::abs(bounds.center().x() - 320) <= 15, name);
            CHECK_DETAIL(std::abs(bounds.center().y() - 180) <= 15, name);
        }
    }
}

void testSpecialContentOpacity()
{
    const QVector<QPair<ContentKind, QString>> kinds = {
        {ContentKind::ComicText, QStringLiteral("comicText")},
        {ContentKind::RadialLines, QStringLiteral("radialLines")},
        {ContentKind::FlowLines, QStringLiteral("flowLines")},
        {ContentKind::HalftoneDots, QStringLiteral("halftoneDots")},
        {ContentKind::BrandWall, QStringLiteral("brandWall")},
    };

    for (const auto& entry : kinds)
    {
        QTemporaryDir directory;
        assets::AssetStore store(directory.path());

        const auto renderOpacity = [&](double opacity) {
            Project project = makeProject(640, 360);
            Entity entity = makeSpecialEntity(entry.first,
                                              QStringLiteral("special"));
            entity.transform.opacity = opacity;
            add(project, entity);
            return render(project, store, 0, Uuid(), QSize(),
                          QColor(Qt::white));
        };

        const QImage opaque = renderOpacity(1.0);
        const QImage half = renderOpacity(0.5);
        const QImage transparent = renderOpacity(0.0);

        const QString name = entry.second;
        CHECK_DETAIL(countInkPixels(transparent, Qt::white) == 0, name);
        CHECK_DETAIL(maxInkStrength(opaque) > maxInkStrength(half) + 60, name);
    }
}

void testSpecialContentFollowsParent()
{
    Project project = makeProject(400, 300);

    Entity parent = makeEntity(QStringLiteral("parent"));
    parent.transform.x = 200.0;
    parent.transform.y = 150.0;
    add(project, parent);

    Entity child =
        makeSpecialEntity(ContentKind::RadialLines, QStringLiteral("child"));
    child.parentUuid = parent.uuid;
    child.transform.x = 0.0;
    child.transform.y = 0.0;
    add(project, child);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image =
        render(project, store, 0, Uuid(), QSize(), QColor(Qt::white));

    const QRect bounds = inkBounds(image, Qt::white);
    CHECK(!bounds.isEmpty());
    CHECK(std::abs(bounds.center().x() - 200) <= 8);
    CHECK(std::abs(bounds.center().y() - 150) <= 8);
}

void testBrandWallLayout()
{
    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const auto describe = [](const QRectF& rect) {
        return QStringLiteral("%1x%2 at %3,%4")
            .arg(rect.width())
            .arg(rect.height())
            .arg(rect.x())
            .arg(rect.y());
    };

    const auto makeWall = [](int columns, int rows, bool alternate,
                             double angle, const QString& background,
                             const QStringList& logos) {
        Project project = makeProject(400, 300);
        Entity entity = makeEntity(QStringLiteral("wall"));
        entity.transform.x = 200.0;
        entity.transform.y = 150.0;
        entity.content.kind = ContentKind::BrandWall;
        BrandWallContent& wall = entity.content.brandWall;
        wall.logos = logos;
        wall.columns = columns;
        wall.rows = rows;
        wall.spacing = 10.0;
        wall.logoSize = 50.0;
        wall.colors = {QStringLiteral("#ff0000"), QStringLiteral("#00ff00"),
                       QStringLiteral("#0000ff")};
        wall.alternate = alternate;
        wall.angle = angle;
        wall.background = background;
        add(project, entity);
        return project;
    };

    // 3x3 wall of 50px tiles with 10px grout: 170x170 centred at (200,150).
    const QImage rows = render(
        makeWall(3, 3, false, 0.0, QString(), {}), store, 0, Uuid(), QSize(),
        QColor(Qt::white));
    const QRect bounds = inkBounds(rows, Qt::white);
    CHECK_DETAIL(std::abs(bounds.width() - 170) <= 2, describe(bounds));
    CHECK_DETAIL(std::abs(bounds.height() - 170) <= 2, describe(bounds));
    CHECK_DETAIL(std::abs(bounds.center().x() - 200) <= 2, describe(bounds));
    CHECK_DETAIL(std::abs(bounds.center().y() - 150) <= 2, describe(bounds));
    CHECK(countInkPixels(rows, Qt::white) > 20000);

    // Transparent background leaves the grout white.
    CHECK(nearColor(rows.pixelColor(170, 90), Qt::white));
    // Slot at row 1, column 0: row-major is red, checkerboard is green.
    CHECK(nearColor(rows.pixelColor(120, 140), QColor(Qt::red)));

    const QImage alternating = render(
        makeWall(3, 3, true, 0.0, QString(), {}), store, 0, Uuid(), QSize(),
        QColor(Qt::white));
    CHECK(nearColor(alternating.pixelColor(120, 140), QColor(Qt::green)));

    // An explicit background fills the grout.
    const QImage filled = render(
        makeWall(3, 3, false, 0.0, QStringLiteral("#000000"), {}), store, 0,
        Uuid(), QSize(), QColor(Qt::white));
    CHECK(nearColor(filled.pixelColor(170, 90), QColor(Qt::black)));

    // A rotated 4x2 wall: 230x110 becomes 110x230.
    const Project rotated =
        makeWall(4, 2, false, 90.0, QString(), {QStringLiteral("PV")});
    const QImage rotatedImage =
        render(rotated, store, 0, Uuid(), QSize(), QColor(Qt::white));
    const QRect rotatedBounds = inkBounds(rotatedImage, Qt::white);
    CHECK_DETAIL(std::abs(rotatedBounds.width() - 110) <= 3,
                 describe(rotatedBounds));
    CHECK_DETAIL(std::abs(rotatedBounds.height() - 230) <= 3,
                 describe(rotatedBounds));

    render2::SceneWorld world(rotated, store);
    const QRectF shape = world.shapeOf(QStringLiteral("wall"), 0).boundingRect();
    CHECK_DETAIL(std::abs(shape.width() - 110.0) <= 0.5, describe(shape));
    CHECK_DETAIL(std::abs(shape.height() - 230.0) <= 0.5, describe(shape));
}

void testAnimatedContentFieldMapping()
{
    Entity text = makeEntity(QStringLiteral("text"), 100, 400);
    text.content.kind = ContentKind::Text;
    text.content.text.text = QStringLiteral("PV");
    text.content.text.pixelSize = 10;
    text.content.text.lineHeight = 1.0;
    text.content.text.letterSpacing = 0.0;
    text.content.text.padding = 2.0;
    text.content.text.boxWidth = 100.0;
    text.content.text.boxHeight = 50.0;
    text.content.text.strokeWidth = 1.0;
    text.tracks.insert(QStringLiteral("fontSize"),
                       keyedTrack({{0, 20.0}, {100, 120.0}}));
    text.tracks.insert(QStringLiteral("lineHeight"),
                       keyedTrack({{0, 1.0}, {100, 2.0}}));
    text.tracks.insert(QStringLiteral("letterSpacing"),
                       keyedTrack({{0, 0.0}, {100, 10.0}}));
    text.tracks.insert(QStringLiteral("padding"),
                       keyedTrack({{0, 2.0}, {100, 22.0}}));
    text.tracks.insert(QStringLiteral("boxWidth"),
                       keyedTrack({{0, 100.0}, {100, 300.0}}));
    text.tracks.insert(QStringLiteral("boxHeight"),
                       keyedTrack({{0, 50.0}, {100, 150.0}}));
    text.tracks.insert(QStringLiteral("strokeWidth"),
                       keyedTrack({{0, 1.0}, {100, 3.0}}));

    // Keys are entity-relative: project frame 150 is relative frame 50.
    const model::Content animated = render2::animatedContent(text, 150.0);
    CHECK(animated.kind == ContentKind::Text);
    CHECK(animated.text.pixelSize == 70);
    CHECK(std::abs(animated.text.lineHeight - 1.5) < 1e-9);
    CHECK(std::abs(animated.text.letterSpacing - 5.0) < 1e-9);
    CHECK(std::abs(animated.text.padding - 12.0) < 1e-9);
    CHECK(std::abs(animated.text.boxWidth - 200.0) < 1e-9);
    CHECK(std::abs(animated.text.boxHeight - 100.0) < 1e-9);
    CHECK(std::abs(animated.text.strokeWidth - 2.0) < 1e-9);
    CHECK(animated.text.text == QStringLiteral("PV"));

    Entity vector = makeEntity(QStringLiteral("vector"), 0, 100);
    vector.content.kind = ContentKind::Vector;
    vector.content.vector.width = 100.0;
    vector.content.vector.height = 50.0;
    vector.content.vector.cornerRadius = 10.0;
    vector.content.vector.strokeWidth = 2.0;
    vector.tracks.insert(QStringLiteral("width"),
                         keyedTrack({{0, 20.0}, {10, 220.0}}));
    vector.tracks.insert(QStringLiteral("height"),
                         keyedTrack({{0, 10.0}, {10, 110.0}}));
    vector.tracks.insert(QStringLiteral("cornerRadius"),
                         keyedTrack({{0, 0.0}, {10, 40.0}}));
    vector.tracks.insert(QStringLiteral("strokeWidth"),
                         keyedTrack({{0, 0.0}, {10, 8.0}}));

    const model::Content vectorAt = render2::animatedContent(vector, 5.0);
    CHECK(vectorAt.kind == ContentKind::Vector);
    CHECK(std::abs(vectorAt.vector.width - 120.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.height - 60.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.cornerRadius - 20.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.strokeWidth - 4.0) < 1e-9);
    CHECK(vectorAt.vector.fillColor == vector.content.vector.fillColor);
}

void testContentTrackFallback()
{
    Entity text = makeEntity(QStringLiteral("text"), 0, 100);
    text.content.kind = ContentKind::Text;
    text.content.text.pixelSize = 42;
    text.content.text.lineHeight = 1.25;
    text.content.text.padding = 7.0;

    // No track at all: every field keeps its static value.
    const model::Content plain = render2::animatedContent(text, 999.0);
    CHECK(plain.text.pixelSize == 42);
    CHECK(std::abs(plain.text.lineHeight - 1.25) < 1e-9);
    CHECK(std::abs(plain.text.padding - 7.0) < 1e-9);

    // An empty track (created before its first key) also falls back.
    text.tracks.insert(QStringLiteral("fontSize"), Track());
    const model::Content empty = render2::animatedContent(text, 50.0);
    CHECK(empty.text.pixelSize == 42);

    // A keyed track overrides the static value at the relative frame.
    text.tracks.insert(QStringLiteral("fontSize"),
                       keyedTrack({{0, 3.0}, {10, 13.0}}));
    const model::Content keyed = render2::animatedContent(text, 5.0);
    CHECK(keyed.text.pixelSize == 8);
    CHECK(std::abs(keyed.text.padding - 7.0) < 1e-9);

    Entity vector = makeEntity(QStringLiteral("vector"), 0, 100);
    vector.content.kind = ContentKind::Vector;
    vector.content.vector.width = 300.0;
    vector.content.vector.height = 200.0;
    const model::Content staticVector = render2::animatedContent(vector, 5.0);
    CHECK(std::abs(staticVector.vector.width - 300.0) < 1e-9);
    CHECK(std::abs(staticVector.vector.height - 200.0) < 1e-9);
}

void testTextFontSizeTrackRenders()
{
    Project project = makeProject(300, 300);
    Entity entity = makeEntity(QStringLiteral("text"), 0, 200);
    entity.transform.x = 150.0;
    entity.transform.y = 150.0;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("PV");
    entity.content.text.pixelSize = 100; // ignored while the track has keys
    entity.content.text.bold = true;
    entity.content.text.color = QStringLiteral("#ffffff");
    entity.content.text.padding = 0.0;
    entity.tracks.insert(QStringLiteral("fontSize"),
                         keyedTrack({{0, 0.0}, {100, 100.0}}));
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage at0 =
        render(project, store, 0.0, Uuid(), QSize(), QColor(Qt::black));
    const QImage at50 =
        render(project, store, 50.0, Uuid(), QSize(), QColor(Qt::black));
    const QImage at100 =
        render(project, store, 100.0, Uuid(), QSize(), QColor(Qt::black));

    const QRect bounds0 = inkBounds(at0, Qt::black);
    const QRect bounds50 = inkBounds(at50, Qt::black);
    const QRect bounds100 = inkBounds(at100, Qt::black);

    CHECK_DETAIL(bounds50.height() > bounds0.height(),
                 QStringLiteral("heights %1 / %2")
                     .arg(bounds0.height())
                     .arg(bounds50.height()));
    CHECK_DETAIL(bounds100.height() > bounds50.height() + 20,
                 QStringLiteral("heights %1 / %2")
                     .arg(bounds50.height())
                     .arg(bounds100.height()));
    CHECK(countInkPixels(at100, Qt::black) > countInkPixels(at50, Qt::black));
    CHECK(countInkPixels(at50, Qt::black) > countInkPixels(at0, Qt::black));

    render2::SceneWorld world(project, store);
    const QRectF shape0 = world.shapeOf(QStringLiteral("text"), 0).boundingRect();
    const QRectF shape50 = world.shapeOf(QStringLiteral("text"), 50).boundingRect();
    const QRectF shape100 =
        world.shapeOf(QStringLiteral("text"), 100).boundingRect();
    CHECK(shape50.height() > shape0.height());
    CHECK(shape100.height() > shape50.height());
}

void testVectorContentTracksRender()
{
    Project project = makeProject(300, 300);
    const QColor background(QStringLiteral("#101014"));

    Entity bar = makeEntity(QStringLiteral("bar"), 0, 200);
    bar.transform.x = 150.0;
    bar.transform.y = 100.0;
    bar.content.kind = ContentKind::Vector;
    bar.content.vector.shape = 0;
    bar.content.vector.width = 100.0; // ignored while the track has keys
    bar.content.vector.height = 40.0;
    bar.content.vector.cornerRadius = 0.0;
    bar.content.vector.fillColor = QStringLiteral("#ff0000");
    bar.content.vector.strokeWidth = 0.0;
    bar.tracks.insert(QStringLiteral("width"),
                      keyedTrack({{0, 20.0}, {100, 220.0}}));
    add(project, bar);

    Entity corner = makeEntity(QStringLiteral("corner"), 0, 200);
    corner.transform.x = 150.0;
    corner.transform.y = 220.0;
    corner.content.kind = ContentKind::Vector;
    corner.content.vector.shape = 3; // rounded rect: uses cornerRadius
    corner.content.vector.width = 80.0;
    corner.content.vector.height = 80.0;
    corner.content.vector.cornerRadius = 0.0;
    corner.content.vector.fillColor = QStringLiteral("#0000ff");
    corner.content.vector.strokeWidth = 0.0;
    corner.tracks.insert(QStringLiteral("cornerRadius"),
                         keyedTrack({{0, 0.0}, {100, 40.0}}));
    add(project, corner);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage at0 = render(project, store, 0.0);
    const QImage at100 = render(project, store, 100.0);

    // 20px wide at frame 0, 220px wide at frame 100: the row through the bar
    // starts and stops at different places.
    CHECK(!nearColor(at0.pixelColor(50, 100), Qt::red));
    CHECK(nearColor(at0.pixelColor(150, 100), Qt::red));
    CHECK(nearColor(at100.pixelColor(50, 100), Qt::red));
    CHECK(nearColor(at100.pixelColor(150, 100), Qt::red));

    // 80x80 blue square: (112,182) is near its top-left corner.  A 40px radius
    // turns the square into an ellipse, so the corner pixel empties out.
    CHECK(nearColor(at0.pixelColor(112, 182), QColor(Qt::blue)));
    CHECK(nearColor(at100.pixelColor(112, 182), background));

    render2::SceneWorld world(project, store);
    const QRectF shape0 = world.shapeOf(QStringLiteral("bar"), 0).boundingRect();
    const QRectF shape100 =
        world.shapeOf(QStringLiteral("bar"), 100).boundingRect();
    CHECK_DETAIL(shape100.width() > shape0.width() + 100,
                 QStringLiteral("widths %1 / %2")
                     .arg(shape0.width())
                     .arg(shape100.width()));
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QVector<QPair<QString, void (*)()>> tests = {
        {QStringLiteral("redVectorCenterPixel"), testRedVectorCenterPixel},
        {QStringLiteral("imageAssetOnCanvas"), testImageAssetOnCanvas},
        {QStringLiteral("textIsNotBlank"), testTextIsNotBlank},
        {QStringLiteral("childFollowsParent"), testChildFollowsParent},
        {QStringLiteral("hiddenAndInvisible"), testHiddenAndInvisible},
        {QStringLiteral("opacityHalfBlend"), testOpacityHalfBlend},
        {QStringLiteral("missingAssetCheckerboard"), testMissingAssetCheckerboard},
        {QStringLiteral("missingImageCard"), testMissingImageCard},
        {QStringLiteral("missingVideoAndHtmlCards"), testMissingVideoAndHtmlCards},
        {QStringLiteral("presentAssetKeepsPendingPlaceholder"),
         testPresentAssetKeepsPendingPlaceholder},
        {QStringLiteral("corruptImageShowsMissingCard"),
         testCorruptImageShowsMissingCard},
        {QStringLiteral("smallMissingRectDoesNotOverflow"),
         testSmallMissingRectDoesNotOverflow},
        {QStringLiteral("reimportSameHashRestoresImage"),
         testReimportSameHashRestoresImage},
        {QStringLiteral("cameraCrop"), testCameraCrop},
        {QStringLiteral("cameraKeepsCanvasCoordinates"),
         testCameraKeepsCanvasCoordinates},
        {QStringLiteral("cameraNegativeScaleFlips"),
         testCameraNegativeScaleFlips},
        {QStringLiteral("worldRectRendersOutsideCanvas"),
         testWorldRectRendersOutsideCanvas},
        {QStringLiteral("cameraIgnoresWorldRect"), testCameraIgnoresWorldRect},
        {QStringLiteral("transparentOutsideCanvas"),
         testTransparentOutsideCanvas},
        {QStringLiteral("transparentWebpOverheadIsSmall"),
         testTransparentWebpOverheadIsSmall},
        {QStringLiteral("worldPoseAndShape"), testWorldPoseAndShape},
        {QStringLiteral("solverSelfShapeTerminates"),
         testSolverSelfShapeTerminates},
        {QStringLiteral("fractionalProjectFrame"), testFractionalProjectFrame},
        {QStringLiteral("frameProviderReceivesFractionalFrame"),
         testFrameProviderReceivesFractionalFrame},
        {QStringLiteral("specialContentRender"), testSpecialContentRender},
        {QStringLiteral("specialContentOpacity"), testSpecialContentOpacity},
        {QStringLiteral("specialContentFollowsParent"),
         testSpecialContentFollowsParent},
        {QStringLiteral("brandWallLayout"), testBrandWallLayout},
        {QStringLiteral("animatedContentFieldMapping"),
         testAnimatedContentFieldMapping},
        {QStringLiteral("contentTrackFallback"), testContentTrackFallback},
        {QStringLiteral("textFontSizeTrackRenders"),
         testTextFontSizeTrackRenders},
        {QStringLiteral("vectorContentTracksRender"),
         testVectorContentTracksRender},
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

    QTextStream(stdout) << QStringLiteral("render2: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
