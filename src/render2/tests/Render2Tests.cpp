#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
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
