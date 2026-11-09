#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
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
        {QStringLiteral("cameraCrop"), testCameraCrop},
        {QStringLiteral("cameraKeepsCanvasCoordinates"),
         testCameraKeepsCanvasCoordinates},
        {QStringLiteral("cameraNegativeScaleFlips"),
         testCameraNegativeScaleFlips},
        {QStringLiteral("worldPoseAndShape"), testWorldPoseAndShape},
        {QStringLiteral("fractionalProjectFrame"), testFractionalProjectFrame},
        {QStringLiteral("frameProviderReceivesFractionalFrame"),
         testFrameProviderReceivesFractionalFrame},
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
