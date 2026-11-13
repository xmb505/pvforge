// Canvas-space entities ignore the parent transform in the render path.  These
// cases live in their own executable so Render2Tests.cpp stays below the
// code-size ratchet.

#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QRect>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
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

const QColor kBackground(QStringLiteral("#101014"));

Project makeProject(int width = 100, int height = 100)
{
    Project project;
    project.settings.width = width;
    project.settings.height = height;
    project.settings.background = kBackground.name();
    return project;
}

Entity makeEntity(const Uuid& uuid, const Uuid& parent = Uuid())
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.parentUuid = parent;
    entity.start = 0;
    entity.end = 10;
    return entity;
}

Entity makeVector(const Uuid& uuid, double width, double height,
                  const Uuid& parent = Uuid())
{
    Entity entity = makeEntity(uuid, parent);
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = width;
    entity.content.vector.height = height;
    entity.content.vector.fillColor = QStringLiteral("#ffff0000");
    entity.content.vector.strokeWidth = 0.0;
    return entity;
}

QImage render(const Project& project, const assets::AssetStore& store,
              const Uuid& camera = Uuid())
{
    render2::Options options;
    options.cameraUuid = camera;
    render2::SceneWorld world(project, store);
    anim::Cache cache;
    return render2::renderFrame(project, store, options, world, cache);
}

QRect inkBounds(const QImage& image)
{
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) == kBackground)
                continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

bool nearColor(const QColor& actual, const QColor& expected, int tolerance = 3)
{
    return std::abs(actual.red() - expected.red()) <= tolerance
           && std::abs(actual.green() - expected.green()) <= tolerance
           && std::abs(actual.blue() - expected.blue()) <= tolerance;
}

void testCanvasChildIgnoresParent()
{
    const auto makeScene = [](EntitySpace space) {
        Project project = makeProject(200, 100);

        Entity parent = makeEntity(QStringLiteral("canvas-parent"));
        parent.transform.x = 60.0;
        parent.transform.y = 20.0;
        parent.transform.rotation = 45.0;
        parent.transform.scaleX = 2.0;
        parent.transform.scaleY = 2.0;
        project.entities.insert(parent.uuid, parent);

        Entity child = makeVector(QStringLiteral("canvas-child"), 20, 20,
                                  parent.uuid);
        child.space = space;
        project.entities.insert(child.uuid, child);
        return project;
    };

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());

    const QImage canvasImage = render(makeScene(EntitySpace::Canvas), store);
    const QRect canvasInk = inkBounds(canvasImage);
    CHECK(!canvasInk.isEmpty());
    CHECK(std::abs(canvasInk.center().x() - 100) <= 2);
    CHECK(std::abs(canvasInk.center().y() - 50) <= 2);

    const QImage parentImage = render(makeScene(EntitySpace::Parent), store);
    const QRect parentInk = inkBounds(parentImage);
    CHECK(!parentInk.isEmpty());
    CHECK(std::abs(parentInk.center().x() - 160) <= 2);
    CHECK(std::abs(parentInk.center().y() - 30) <= 2);
}

void testCanvasCameraIgnoresParent()
{
    Project project = makeProject();

    Entity parent = makeEntity(QStringLiteral("camera-parent"));
    parent.transform.x = 50.0;
    project.entities.insert(parent.uuid, parent);

    Entity camera = makeEntity(QStringLiteral("canvas-camera"), parent.uuid);
    camera.space = EntitySpace::Canvas;
    camera.content.kind = ContentKind::Camera;
    project.entities.insert(camera.uuid, camera);

    Entity marker = makeVector(QStringLiteral("marker"), 10, 10);
    project.entities.insert(marker.uuid, marker);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    const QImage image = render(project, store, camera.uuid);

    // The camera must stay at the canvas origin: a parent-following camera
    // would look at (50,0) and leave the marker off-centre.
    CHECK(nearColor(image.pixelColor(50, 50), Qt::red));
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    testCanvasChildIgnoresParent();
    testCanvasCameraIgnoresParent();

    QTextStream(stdout) << QStringLiteral("render2 canvas: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
