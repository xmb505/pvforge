// Editor-only `hidden` versus render visibility.  These cases live in their
// own executable so the historical Render2Tests.cpp stays below the code-size
// ratchet.

#include "render2/Render.h"

#include "assets/Assets.h"
#include "model/Model.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
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

Project makeProject()
{
    Project project;
    project.settings.width = 100;
    project.settings.height = 100;
    project.settings.background = QStringLiteral("#101014");
    return project;
}

Entity makeVector(const Uuid& uuid, const Uuid& parent = Uuid())
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.parentUuid = parent;
    entity.start = 0;
    entity.end = 10;
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = 100.0;
    entity.content.vector.height = 100.0;
    entity.content.vector.fillColor = QStringLiteral("#ffff0000");
    entity.content.vector.strokeWidth = 0.0;
    return entity;
}

QImage render(const Project& project, const assets::AssetStore& store,
              bool skipHidden)
{
    render2::Options options;
    options.skipHidden = skipHidden;
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

const QColor kBackground(QStringLiteral("#101014"));

void testHiddenEntityStillRenders()
{
    Project project = makeProject();
    Entity hidden = makeVector(QStringLiteral("hidden"));
    hidden.hidden = true;
    project.entities.insert(hidden.uuid, hidden);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    CHECK(nearColor(render(project, store, false).pixelColor(50, 50), Qt::red));
}

void testVisibleTrackStillHides()
{
    Project project = makeProject();
    Entity tracked = makeVector(QStringLiteral("tracked"));
    tracked.hidden = true;
    Track visible;
    visible.keys.append(Keyframe{0, 0.0});
    tracked.tracks.insert(QStringLiteral("visible"), visible);
    project.entities.insert(tracked.uuid, tracked);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    CHECK(nearColor(render(project, store, false).pixelColor(50, 50),
                    kBackground));
}

void testWorkbenchSkipsHidden()
{
    Project project = makeProject();
    Entity hidden = makeVector(QStringLiteral("hidden"));
    hidden.hidden = true;
    project.entities.insert(hidden.uuid, hidden);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    CHECK(nearColor(render(project, store, true).pixelColor(50, 50),
                    kBackground));
}

void testWorkbenchHiddenParentInheritance()
{
    Project project = makeProject();
    Entity parent = makeVector(QStringLiteral("parent"));
    parent.hidden = true;
    project.entities.insert(parent.uuid, parent);
    Entity child = makeVector(QStringLiteral("child"), parent.uuid);
    project.entities.insert(child.uuid, child);

    QTemporaryDir directory;
    assets::AssetStore store(directory.path());
    CHECK(nearColor(render(project, store, true).pixelColor(50, 50),
                    kBackground));

    Entity independent = makeVector(QStringLiteral("independent"), parent.uuid);
    independent.inheritVisibility = false;
    project.entities.insert(independent.uuid, independent);
    CHECK(nearColor(render(project, store, true).pixelColor(50, 50), Qt::red));
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    testHiddenEntityStillRenders();
    testVisibleTrackStillHides();
    testWorkbenchSkipsHidden();
    testWorkbenchHiddenParentInheritance();

    QTextStream(stdout) << QStringLiteral("render2-hidden: %1 checks, %2 failures\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
