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

Keyframe wholeKey(int frame, double x = 0.0, double y = 0.0,
                  double rotation = 0.0)
{
    Keyframe key;
    key.frame = frame;
    key.x = x;
    key.y = y;
    key.rotation = rotation;
    return key;
}

// Adds a whole-frame key whose content animation values are set from a list of
// (property, value) pairs.  Definition parameters never enter a keyframe.  The
// other public parameters start from the entity's static state.
Keyframe contentKey(const Entity& entity, int frame,
                    std::initializer_list<std::pair<const char*, double>> values)
{
    const model::KeyframeSample sample = model::staticEntitySample(entity);
    Keyframe key;
    key.frame = frame;
    key.x = sample.x;
    key.y = sample.y;
    key.rotation = sample.rotation;
    key.scaleX = sample.scaleX;
    key.scaleY = sample.scaleY;
    key.opacity = sample.opacity;
    key.visible = sample.visible;
    key.physicsInteract = sample.physicsInteract;
    key.content = model::animationValues(entity.content);
    for (const auto& entry : values)
        key.content.insert(QString::fromLatin1(entry.first), entry.second);
    return key;
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
    rect.transform.x = 0.0; // canvas centre
    rect.transform.y = 0.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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
    parent.transform.x = 20.0;  // screen (70, 30)
    parent.transform.y = 20.0;
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

    // `hidden` is editor-only: the final renderer still draws the entity.
    Project hiddenProject = makeProject(100, 100);
    Entity hidden = makeVector(QStringLiteral("hidden"), 100, 100, Qt::red);
    hidden.hidden = true;
    add(hiddenProject, hidden);
    CHECK(nearColor(render(hiddenProject, store).pixelColor(50, 50), Qt::red));

    // `visible` alone decides whether the final renderer draws it.
    Project invisibleProject = makeProject(100, 100);
    Entity invisible =
        makeVector(QStringLiteral("invisible"), 100, 100, Qt::red);
    invisible.hidden = true;
    invisible.visible = false;
    add(invisibleProject, invisible);
    const QImage image = render(invisibleProject, store);
    CHECK(nearColor(image.pixelColor(50, 50),
                    QColor(QStringLiteral("#101014"))));
}

void testOpacityHalfBlend()
{
    Project project = makeProject(100, 100);
    Entity entity = makeVector(QStringLiteral("half"), 100, 100, Qt::red);
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(countRedBorderPixels(image) > 20);
    CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
    CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
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
        entity.transform.x = 0.0;
        entity.transform.y = 0.0;
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
        CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
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
        entity.transform.x = 0.0;
        entity.transform.y = 0.0;
        add(project, entity);

        render2::Options options;
        options.videoFrame = [](const Uuid&, double) { return QImage(); };
        render2::SceneWorld world(project, store);
        anim::Cache cache;
        const QImage image =
            render2::renderFrame(project, store, options, world, cache);

        CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
              == QStringLiteral("视频"));
        CHECK(countRedBorderPixels(image) == 0);
        CHECK(hasBrightPixel(image, QRect(80, 40, 160, 100)));
    }

    {
        Project project = makeProject(320, 180);
        Entity entity = makeAssetEntity(QStringLiteral("pending-html"),
                                        AssetType::Html, stored->hash);
        entity.transform.x = 0.0;
        entity.transform.y = 0.0;
        add(project, entity);

        render2::Options options;
        options.htmlFrame = [](const Uuid&, double) { return QImage(); };
        render2::SceneWorld world(project, store);
        anim::Cache cache;
        const QImage image =
            render2::renderFrame(project, store, options, world, cache);

        CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    add(project, entity);

    assets::AssetStore store(directory.path());
    const QImage image = render(project, store);

    CHECK(store.get(hash).has_value());
    CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    add(project, entity);

    const QImage imported = render(project, store);
    CHECK(nearColor(imported.pixelColor(200, 150), QColor(0, 255, 0)));
    CHECK(countRedBorderPixels(imported) == 0);

    CHECK(store.remove(stored->hash));
    const QImage offline = render(project, store);
    CHECK(!nearColor(offline.pixelColor(200, 150), QColor(0, 255, 0)));
    CHECK(countRedBorderPixels(offline) > 20);
    CHECK(render2::assetPlaceholderLabel(store, entity.content.asset)
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
    project.entities[QStringLiteral("red")].transform.x = 0.0;
    project.entities[QStringLiteral("red")].transform.y = 0.0;

    add(project, makeVector(QStringLiteral("blue"), 20, 20, Qt::blue));
    project.entities[QStringLiteral("blue")].transform.x = -80.0;
    project.entities[QStringLiteral("blue")].transform.y = -80.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 0.0;
    camera.transform.y = 0.0;
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
    project.entities[QStringLiteral("marker")].transform.x = 20.0;   // screen (120,120)
    project.entities[QStringLiteral("marker")].transform.y = -20.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 0.0;
    camera.transform.y = 0.0;
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
    project.entities[QStringLiteral("marker")].transform.x = -20.0; // screen (30,50)
    project.entities[QStringLiteral("marker")].transform.y = 0.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 0.0;
    camera.transform.y = 0.0;
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
    outside.transform.x = -70.0; // above-left of the 100x100 canvas
    outside.transform.y = 70.0;
    add(project, outside);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QRectF world(-100, -100, 200, 200);
    const QImage image =
        renderWorld(project, store, world, false, Uuid(), QSize(200, 200));

    // Model (-70,+70) maps to output (30,30) after the single Y flip; the
    // canvas centre (0,0) maps to (100,100) and stays the background.
    CHECK(nearColor(image.pixelColor(30, 30), Qt::red));
    CHECK(nearColor(image.pixelColor(100, 100),
                    QColor(QStringLiteral("#101014"))));
    CHECK(nearColor(image.pixelColor(0, 0),
                    QColor(QStringLiteral("#101014"))));
}

void testCameraIgnoresWorldRect()
{
    Project project = makeProject(200, 200);
    add(project, makeVector(QStringLiteral("red"), 40, 40, Qt::red));
    project.entities[QStringLiteral("red")].transform.x = 0.0;
    project.entities[QStringLiteral("red")].transform.y = 0.0;

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.x = 0.0;
    camera.transform.y = 0.0;
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
    outside.transform.x = -70.0;
    outside.transform.y = 70.0;
    add(project, outside);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QRectF world(-100, -100, 200, 200);
    const QImage image =
        renderWorld(project, store, world, true, Uuid(), QSize(200, 200));

    CHECK(image.pixelColor(0, 0).alpha() == 0);
    CHECK(image.pixelColor(199, 199).alpha() == 0);
    CHECK(nearColor(image.pixelColor(100, 100),
                    QColor(QStringLiteral("#101014"))));
    CHECK(image.pixelColor(100, 100).alpha() == 255);
    CHECK(nearColor(image.pixelColor(30, 30), Qt::red));
    CHECK(image.pixelColor(30, 30).alpha() == 255);

    const QImage opaque =
        renderWorld(project, store, world, false, Uuid(), QSize(200, 200));
    CHECK(opaque.pixelColor(0, 0).alpha() == 255);
}

void testTransparentWebpOverheadIsSmall()
{
    Project project = makeProject(100, 100);
    Entity centre = makeVector(QStringLiteral("centre"), 60, 60, Qt::red);
    centre.transform.x = 0.0;
    centre.transform.y = 0.0;
    add(project, centre);
    Entity outside = makeVector(QStringLiteral("outside"), 20, 20, Qt::blue);
    outside.transform.x = -150.0;
    outside.transform.y = 150.0;
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
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    add(project, entity);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    render2::SceneWorld world(project, store);

    const solver::Pose pose = world.poseOf(QStringLiteral("shape"), 0);
    CHECK(std::abs(pose.position.x()) < 1e-6);
    CHECK(std::abs(pose.position.y()) < 1e-6);
    CHECK(std::abs(pose.rotation) < 1e-6);

    const QPainterPath shape = world.shapeOf(QStringLiteral("shape"), 0);
    CHECK(shape.contains(QPointF(0.0, 0.0)));
    CHECK(!shape.contains(QPointF(50.0, 0.0)));
}

void testSolverSelfShapeTerminates()
{
    Project project = makeProject(100, 100);
    Entity ball = makeVector(QStringLiteral("ball"), 10, 10, Qt::red);
    ball.start = 0;
    ball.end = 60;
    ball.transform.x = 0.0;
    ball.transform.y = -40.0;
    ball.keyframes = {wholeKey(0, 0.0, -40.0)};

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
    rect.transform.y = 0.0;
    rect.keyframes = {wholeKey(0, -50.0, 0.0), wholeKey(10, 50.0, 0.0)};
    add(project, rect);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage at5 = render(project, store, 5.0);
    const QImage at55 = render(project, store, 5.5);
    const QImage at6 = render(project, store, 6.0);

    // 4x4 rect: model x=0 at frame 5, +5 at 5.5, +10 at 6; screen x is
    // 50 + model x because the canvas centre is the origin.
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
    text.keyframes = {
        contentKey(text, 0,
                   {{"pixelSize", 20.0}, {"lineHeight", 1.0},
                    {"letterSpacing", 0.0}, {"padding", 2.0},
                    {"boxWidth", 100.0}, {"boxHeight", 50.0},
                    {"strokeWidth", 1.0}}),
        contentKey(text, 100,
                   {{"pixelSize", 120.0}, {"lineHeight", 2.0},
                    {"letterSpacing", 10.0}, {"padding", 22.0},
                    {"boxWidth", 300.0}, {"boxHeight", 150.0},
                    {"strokeWidth", 3.0}}),
    };

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
    vector.keyframes = {
        contentKey(vector, 0,
                   {{"width", 20.0}, {"height", 10.0},
                    {"cornerRadius", 0.0}, {"strokeWidth", 0.0}}),
        contentKey(vector, 10,
                   {{"width", 220.0}, {"height", 110.0},
                    {"cornerRadius", 40.0}, {"strokeWidth", 8.0}}),
    };

    const model::Content vectorAt = render2::animatedContent(vector, 5.0);
    CHECK(vectorAt.kind == ContentKind::Vector);
    CHECK(std::abs(vectorAt.vector.width - 120.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.height - 60.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.cornerRadius - 20.0) < 1e-9);
    CHECK(std::abs(vectorAt.vector.strokeWidth - 4.0) < 1e-9);
    CHECK(vectorAt.vector.fillColor == vector.content.vector.fillColor);
}

void testContentFallback()
{
    Entity text = makeEntity(QStringLiteral("text"), 0, 100);
    text.content.kind = ContentKind::Text;
    text.content.text.pixelSize = 42;
    text.content.text.lineHeight = 1.25;
    text.content.text.padding = 7.0;

    // No keyframes: every field keeps its static value.
    const model::Content plain = render2::animatedContent(text, 999.0);
    CHECK(plain.text.pixelSize == 42);
    CHECK(std::abs(plain.text.lineHeight - 1.25) < 1e-9);
    CHECK(std::abs(plain.text.padding - 7.0) < 1e-9);

    // Whole-frame keys override the static value at the relative frame.
    text.keyframes = {
        contentKey(text, 0, {{"pixelSize", 3.0}}),
        contentKey(text, 10, {{"pixelSize", 13.0}}),
    };
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

void testTextPixelSizeRenders()
{
    Project project = makeProject(300, 300);
    Entity entity = makeEntity(QStringLiteral("text"), 0, 200);
    entity.transform.x = 0.0;
    entity.transform.y = 0.0;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("PV");
    entity.content.text.pixelSize = 100; // ignored while keyframes exist
    entity.content.text.bold = true;
    entity.content.text.color = QStringLiteral("#ffffff");
    entity.content.text.padding = 0.0;
    entity.keyframes = {
        contentKey(entity, 0, {{"pixelSize", 0.0}}),
        contentKey(entity, 100, {{"pixelSize", 100.0}}),
    };
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

void testWorldConventionOriginAndRotation()
{
    // Model space: centre (0,0), +X right, +Y up, positive rotation CCW.
    // Screen pixels: x = 100 + modelX, y = 100 - modelY for a 200x200 canvas.
    struct Case
    {
        double rotation;
        QPoint redPixel;  // child at local (+40, 0)
        QPoint bluePixel; // child at local (0, +40)
    };
    const Case cases[] = {
        {0.0, QPoint(140, 100), QPoint(100, 60)},
        {90.0, QPoint(100, 60), QPoint(60, 100)},
        {180.0, QPoint(60, 100), QPoint(100, 140)},
        {-90.0, QPoint(100, 140), QPoint(140, 100)},
    };

    for (const Case& entry : cases)
    {
        Project project = makeProject(200, 200);
        Entity parent = makeEntity(QStringLiteral("parent"));
        parent.transform.rotation = entry.rotation;
        add(project, parent);

        Entity red = makeVector(QStringLiteral("red"), 10, 10, Qt::red, 0, 10,
                               QStringLiteral("parent"));
        red.transform.x = 40.0;
        add(project, red);
        Entity blue = makeVector(QStringLiteral("blue"), 10, 10, Qt::blue, 0, 10,
                                 QStringLiteral("parent"));
        blue.transform.y = 40.0;
        add(project, blue);

        QTemporaryDir directory;
        assets::AssetStore store(directory.path());
        const QImage image = render(project, store, 0, Uuid(), QSize(),
                                    QColor(Qt::black));

        CHECK_DETAIL(nearColor(image.pixelColor(entry.redPixel), Qt::red),
                     QStringLiteral("red at rotation %1").arg(entry.rotation));
        CHECK_DETAIL(nearColor(image.pixelColor(entry.bluePixel), Qt::blue),
                     QStringLiteral("blue at rotation %1").arg(entry.rotation));
        CHECK_DETAIL(nearColor(image.pixelColor(100, 100), Qt::black),
                     QStringLiteral("origin empty at rotation %1")
                         .arg(entry.rotation));
    }
}

void testImageOrientationIsUpright()
{
    QTemporaryDir directory;
    QImage source(20, 20, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::blue);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 20; ++x)
            source.setPixelColor(x, y, Qt::red);
    const QString path = directory.path() + QStringLiteral("/upright.png");
    CHECK(source.save(path));

    assets::AssetStore store(directory.path());
    QString error;
    const auto stored = store.importFile(path, &error);
    CHECK(stored.has_value());
    if (!stored)
        return;

    Project project = makeProject(100, 100);
    Entity entity = makeEntity(QStringLiteral("image"));
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.type = AssetType::Image;
    entity.content.asset.hash = stored->hash;
    add(project, entity);

    const QImage image = render(project, store, 0, Uuid(), QSize(),
                                QColor(Qt::black));
    // The content-local flip cancels the world -> screen flip: image row 0
    // still lands at the top of the screen.
    CHECK(nearColor(image.pixelColor(50, 45), Qt::red));
    CHECK(nearColor(image.pixelColor(50, 55), Qt::blue));
}

void testCameraOrientationMatchesCanvas()
{
    Project project = makeProject(200, 200);
    Entity marker = makeVector(QStringLiteral("marker"), 20, 20, Qt::red);
    marker.transform.y = 40.0; // above the canvas centre
    add(project, marker);

    Entity camera = makeEntity(QStringLiteral("camera"));
    camera.content.kind = ContentKind::Camera;
    camera.transform.scaleX = 0.5;
    camera.transform.scaleY = 0.5;
    add(project, camera);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage canvas = render(project, store, 0, Uuid(), QSize(200, 200));
    CHECK(nearColor(canvas.pixelColor(100, 60), Qt::red));

    const QImage throughCamera =
        render(project, store, 0, QStringLiteral("camera"), QSize(100, 100));
    CHECK(nearColor(throughCamera.pixelColor(50, 10), Qt::red));
    CHECK(!nearColor(throughCamera.pixelColor(50, 90), Qt::red));

    // A +90 degree camera is counter-clockwise, so the world appears rotated
    // clockwise: a marker to the right of the camera lands below its centre.
    Project rotated = project;
    rotated.entities[QStringLiteral("camera")].transform.rotation = 90.0;
    rotated.entities[QStringLiteral("marker")].transform.x = 40.0;
    rotated.entities[QStringLiteral("marker")].transform.y = 0.0;
    const QImage rotatedCamera =
        render(rotated, store, 0, QStringLiteral("camera"), QSize(100, 100));
    CHECK(nearColor(rotatedCamera.pixelColor(50, 90), Qt::red));
    CHECK(!nearColor(rotatedCamera.pixelColor(50, 10), Qt::red));
}

void testCollisionShapeUsesModelYUp()
{
    Project project = makeProject(100, 100);
    Entity triangle = makeVector(QStringLiteral("triangle"), 40, 20, Qt::red);
    triangle.content.vector.shape = 4; // triangle pointing up in content space
    add(project, triangle);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    render2::SceneWorld world(project, store);
    const QPainterPath shape = world.shapeOf(QStringLiteral("triangle"), 0);

    // After the content-local flip the tip is at +Y in model space.
    CHECK(shape.contains(QPointF(0.0, 8.0)));
    CHECK(!shape.contains(QPointF(15.0, 8.0)));
    CHECK(shape.contains(QPointF(0.0, -9.0)));
}

void testVectorContentAnimationRenders()
{
    Project project = makeProject(300, 300);
    const QColor background(QStringLiteral("#101014"));

    Entity bar = makeEntity(QStringLiteral("bar"), 0, 200);
    bar.transform.x = 0.0;   // screen x = 150
    bar.transform.y = 50.0;  // screen y = 100
    bar.content.kind = ContentKind::Vector;
    bar.content.vector.shape = 0;
    bar.content.vector.width = 100.0; // ignored while the track has keys
    bar.content.vector.height = 40.0;
    bar.content.vector.cornerRadius = 0.0;
    bar.content.vector.fillColor = QStringLiteral("#ff0000");
    bar.content.vector.strokeWidth = 0.0;
    bar.keyframes = {
        contentKey(bar, 0, {{"width", 20.0}}),
        contentKey(bar, 100, {{"width", 220.0}}),
    };
    add(project, bar);

    Entity corner = makeEntity(QStringLiteral("corner"), 0, 200);
    corner.transform.x = 0.0;    // screen x = 150
    corner.transform.y = -70.0;  // screen y = 220
    corner.content.kind = ContentKind::Vector;
    corner.content.vector.shape = 3; // rounded rect: uses cornerRadius
    corner.content.vector.width = 80.0;
    corner.content.vector.height = 80.0;
    corner.content.vector.cornerRadius = 0.0;
    corner.content.vector.fillColor = QStringLiteral("#0000ff");
    corner.content.vector.strokeWidth = 0.0;
    corner.keyframes = {
        contentKey(corner, 0, {{"cornerRadius", 0.0}}),
        contentKey(corner, 100, {{"cornerRadius", 40.0}}),
    };
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
        {QStringLiteral("animatedContentFieldMapping"),
         testAnimatedContentFieldMapping},
        {QStringLiteral("contentFallback"), testContentFallback},
        {QStringLiteral("textPixelSizeRenders"), testTextPixelSizeRenders},
        {QStringLiteral("worldConventionOriginAndRotation"),
         testWorldConventionOriginAndRotation},
        {QStringLiteral("imageOrientationIsUpright"),
         testImageOrientationIsUpright},
        {QStringLiteral("cameraOrientationMatchesCanvas"),
         testCameraOrientationMatchesCanvas},
        {QStringLiteral("collisionShapeUsesModelYUp"),
         testCollisionShapeUsesModelYUp},
        {QStringLiteral("vectorContentAnimationRenders"),
         testVectorContentAnimationRenders},
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
