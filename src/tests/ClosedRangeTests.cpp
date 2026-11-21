#include "TestRegistry.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "app/MainWindow.h"
#include "assets/AssetStore.h"
#include "model/Model.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/timeline/EntityRange.h"
#include "render2/Render.h"
#include "ui/TimelinePanel.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

namespace {

using namespace pvf;

int gFailures = 0;

void check(bool condition, const QString& message)
{
    if (condition)
        return;
    ++gFailures;
    qCritical() << "closed-range-test:" << message;
}

model::Entity makeEntity(const QString& name, int start, int end, int row)
{
    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = name;
    entity.start = start;
    entity.end = end;
    entity.row = row;
    entity.content.kind = ContentKind::Vector;
    entity.content.vector.shape = 0;
    entity.content.vector.width = 20.0;
    entity.content.vector.height = 20.0;
    entity.content.vector.fillColor = QStringLiteral("#ff0000");
    entity.content.vector.strokeWidth = 0.0;
    return entity;
}

model::Project makeProject()
{
    model::Project project;
    project.settings.name = QStringLiteral("Closed Range");
    project.settings.width = 100;
    project.settings.height = 100;
    project.settings.fps = 24;
    project.settings.background = QStringLiteral("#101014");
    return project;
}

void testModelRanges()
{
    model::Entity clip = makeEntity(QStringLiteral("clip"), 0, 133, 0);
    check(clip.duration() == 134, QStringLiteral("0–133 duration != 134"));
    QString error;
    check(model::validateEntity(clip, &error),
          QStringLiteral("0–133 entity is invalid: %1").arg(error));

    model::Entity single = makeEntity(QStringLiteral("single"), 10, 10, 0);
    check(single.duration() == 1, QStringLiteral("[10,10] duration != 1"));
    error.clear();
    check(model::validateEntity(single, &error),
          QStringLiteral("[10,10] is invalid: %1").arg(error));

    model::Entity keyAtEnd = clip;
    model::Keyframe key;
    key.frame = 133;
    keyAtEnd.keyframes = {key};
    error.clear();
    check(model::validateEntity(keyAtEnd, &error),
          QStringLiteral("keyframe at duration-1 is invalid: %1").arg(error));
    keyAtEnd.keyframes.first().frame = 134;
    error.clear();
    check(!model::validateEntity(keyAtEnd, &error),
          QStringLiteral("keyframe at duration was accepted"));

    // Adjacent closed clips share no frame; touching clips do.
    model::Project adjacent = makeProject();
    model::Entity left = makeEntity(QStringLiteral("left"), 0, 133, 0);
    model::Entity right = makeEntity(QStringLiteral("right"), 134, 266, 0);
    adjacent.entities.insert(left.uuid, left);
    adjacent.entities.insert(right.uuid, right);
    error.clear();
    check(model::validateProject(adjacent, &error),
          QStringLiteral("[0,133] + [134,266] rejected: %1").arg(error));

    model::Project touching = adjacent;
    touching.entities[right.uuid].start = 133;
    error.clear();
    check(!model::validateProject(touching, &error),
          QStringLiteral("[0,133] + [133,266] overlap was accepted"));

    model::Project child = makeProject();
    model::Entity parent = makeEntity(QStringLiteral("parent"), 0, 133, 0);
    model::Entity inside = makeEntity(QStringLiteral("inside"), 10, 133, 0);
    inside.parentUuid = parent.uuid;
    child.entities.insert(parent.uuid, parent);
    child.entities.insert(inside.uuid, inside);
    error.clear();
    check(model::validateProject(child, &error),
          QStringLiteral("child ending at parent.end is rejected: %1").arg(error));
}

void testRenderVisibility()
{
    model::Project project = makeProject();
    model::Entity clip = makeEntity(QStringLiteral("clip"), 0, 133, 0);
    project.entities.insert(clip.uuid, clip);

    assets::AssetStore assets(QDir::tempPath());
    render2::Options options;
    options.outputSize = QSize(100, 100);

    auto render = [&](double frame) {
        options.projectFrame = frame;
        render2::SceneWorld world(project, assets);
        anim::Cache cache;
        return render2::renderFrame(project, assets, options, world, cache);
    };
    auto isRed = [](const QImage& image) {
        const QColor color = image.pixelColor(image.width() / 2,
                                              image.height() / 2);
        return color.red() > 200 && color.green() < 60 && color.blue() < 60;
    };

    check(isRed(render(133.0)),
          QStringLiteral("0–133 clip is invisible on integer frame 133"));
    check(!isRed(render(134.0)),
          QStringLiteral("0–133 clip is visible on integer frame 134"));
    check(isRed(render(133.5)),
          QStringLiteral("0–133 clip is invisible on fractional frame 133.5"));
    check(isRed(render(133.9)),
          QStringLiteral("0–133 clip is invisible on fractional frame 133.9"));
    check(!isRed(render(134.0)),
          QStringLiteral("0–133 clip is visible on fractional frame 134.0"));
    check(!isRed(render(134.5)),
          QStringLiteral("0–133 clip is visible on fractional frame 134.5"));

    model::Project single = makeProject();
    const model::Entity singleClip =
        makeEntity(QStringLiteral("single"), 10, 10, 0);
    single.entities.insert(singleClip.uuid, singleClip);
    project = single;
    check(isRed(render(10.0)),
          QStringLiteral("[10,10] is invisible on frame 10"));
    check(!isRed(render(11.0)),
          QStringLiteral("[10,10] is visible on frame 11"));
}

void testSplitRanges()
{
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("temporary directory failed"));
    assets::AssetStore assets(temp.path());

    {
        model::Project project = makeProject();
        const model::Entity clip = makeEntity(QStringLiteral("clip"), 0, 133, 0);
        project.entities.insert(clip.uuid, clip);

        model::Project result;
        QVector<Uuid> upserts;
        QVector<Uuid> deletes;
        Uuid rightUuid;
        QString error;
        check(next::splitEntityAt(project, assets, clip.uuid, 50, &result,
                                  &upserts, &deletes, &rightUuid, &error),
              QStringLiteral("split at 50 failed: %1").arg(error));
        const model::Entity left = result.entities.value(clip.uuid);
        const model::Entity right = result.entities.value(rightUuid);
        check(left.start == 0 && left.end == 49,
              QStringLiteral("left half is [%1,%2], expected [0,49]")
                  .arg(left.start)
                  .arg(left.end));
        check(right.start == 50 && right.end == 133,
              QStringLiteral("right half is [%1,%2], expected [50,133]")
                  .arg(right.start)
                  .arg(right.end));
    }

    {
        // A cut at end is legal and yields a single-frame right half.
        model::Project project = makeProject();
        const model::Entity clip = makeEntity(QStringLiteral("clip"), 0, 133, 0);
        project.entities.insert(clip.uuid, clip);

        model::Project result;
        QVector<Uuid> upserts;
        QVector<Uuid> deletes;
        Uuid rightUuid;
        QString error;
        check(next::splitEntityAt(project, assets, clip.uuid, 133, &result,
                                  &upserts, &deletes, &rightUuid, &error),
              QStringLiteral("split at end failed: %1").arg(error));
        check(result.entities.value(clip.uuid).end == 132,
              QStringLiteral("left half at end cut is not [0,132]"));
        const model::Entity right = result.entities.value(rightUuid);
        check(right.start == 133 && right.end == 133,
              QStringLiteral("right half at end cut is not [133,133]"));
    }
}

void testTimelineGeometry()
{
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("temporary directory failed"));
    if (!temp.isValid())
        return;

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("closed-range.pvproj"));
    model::ProjectSettings settings;
    settings.name = QStringLiteral("Closed Range Timeline");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 24;

    QString error;
    auto session = EditorSession::createProject(projectDir, settings, &error);
    check(session != nullptr, QStringLiteral("project creation failed: %1")
                                   .arg(error));
    if (!session)
        return;

    const model::Entity clip = makeEntity(QStringLiteral("clip"), 0, 133, 0);
    const Uuid uuid = clip.uuid;
    session->mutableProject().entities.insert(uuid, clip);
    check(session->document()->commitEntities({uuid},
                                              QStringLiteral("closed range"),
                                              &error),
          QStringLiteral("setup commit failed: %1").arg(error));

    check(session->playback()->contentEnd() == 134,
          QStringLiteral("contentEnd is %1, expected 134")
              .arg(session->playback()->contentEnd()));

    auto* window = new MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1280, 800);
    window->show();
    QApplication::processEvents();

    auto* timeline = window->findChild<TimelinePanel*>();
    check(timeline != nullptr, QStringLiteral("timeline panel missing"));
    if (timeline)
    {
        const EntityId id = timeline->entityIdForUuid(uuid);
        const QRect rect = timeline->clipRectFor(id);
        check(!rect.isEmpty(), QStringLiteral("clip rect is empty"));
        check(rect.right() + 1 == timeline->frameXForTest(134),
              QStringLiteral("clip right edge is %1, expected %2")
                  .arg(rect.right() + 1)
                  .arg(timeline->frameXForTest(134)));
        check(timeline->clipRangeTextForTest(id) == QStringLiteral("0–133 · 134f"),
              QStringLiteral("clip range text is \"%1\"")
                  .arg(timeline->clipRangeTextForTest(id)));
    }

    window->close();
    QApplication::processEvents();
}

int closedRangeTest(QApplication& app)
{
    Q_UNUSED(app);
    gFailures = 0;
    testModelRanges();
    testRenderVisibility();
    testSplitRanges();
    testTimelineGeometry();

    if (gFailures == 0)
        qInfo() << "closed-range-test OK: model/render/split/timeline [start,end]";
    return gFailures == 0 ? 0 : 1;
}

} // namespace

void pvf::tests::registerClosedRangeTests(Registry& registry)
{
    registry.add(QStringLiteral("--closed-range-test"),
                 [](QApplication& app) { return closedRangeTest(app); });
}
