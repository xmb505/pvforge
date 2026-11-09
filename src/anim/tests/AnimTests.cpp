#include "anim/Anim.h"
#include "model/Model.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <QSet>
#include <QTextStream>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

using namespace pvf;
using namespace pvf::model;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const QString& detail,
           const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3  %4")
                                  .arg(QString::fromLatin1(file))
                                  .arg(line)
                                  .arg(QString::fromLatin1(expression), detail)
                        << '\n';
}

bool nearly(double a, double b, double epsilon)
{
    return std::abs(a - b) <= epsilon;
}

void checkNear(double actual, double expected, double epsilon,
               const char* expression, const char* file, int line)
{
    check(nearly(actual, expected, epsilon), expression,
          QStringLiteral("(got %1, expected %2, eps %3)")
              .arg(actual)
              .arg(expected)
              .arg(epsilon),
          file, line);
}

#define CHECK(condition) check((condition), #condition, QString(), __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, epsilon) \
    checkNear((actual), (expected), (epsilon), #actual " ~= " #expected, __FILE__, __LINE__)

Entity makeEntity(const Uuid& uuid, int start = 0, int end = 100)
{
    Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = start;
    entity.end = end;
    return entity;
}

void setTrack(Entity& entity, const char* name,
              std::initializer_list<std::pair<int, double>> keys)
{
    Track track;
    for (const auto& entry : keys)
    {
        Keyframe keyframe;
        keyframe.frame = entry.first;
        keyframe.value = entry.second;
        track.keys.append(keyframe);
    }
    entity.tracks.insert(QString::fromLatin1(name), track);
}

SolverSegment makeSegment(const Uuid& id, SolverEffect effect, int startFrame,
                          int endFrame, const Uuid& target = Uuid())
{
    SolverSegment segment;
    segment.id = id;
    segment.effect = effect;
    segment.startFrame = startFrame;
    segment.endFrame = endFrame;
    if (!target.isEmpty())
        segment.params.insert(QStringLiteral("targetUuid"), target);
    return segment;
}

void addEntity(Project& project, const Entity& entity)
{
    project.entities.insert(entity.uuid, entity);
}

class MockWorld final : public anim::World
{
public:
    QHash<Uuid, solver::Pose> poses;
    QHash<Uuid, QPainterPath> shapes;
    QSet<Uuid> interactive;
    QVector<Uuid> bodies;
    mutable QSet<int> queriedFrames;
    mutable int poseQueries = 0;

    solver::Pose poseOf(const Uuid& uuid, int projectFrame) const override
    {
        ++poseQueries;
        queriedFrames.insert(projectFrame);
        return poses.value(uuid);
    }

    QPainterPath shapeOf(const Uuid& uuid, int projectFrame) const override
    {
        Q_UNUSED(projectFrame);
        return shapes.value(uuid);
    }

    bool physicsInteract(const Uuid& uuid, int projectFrame) const override
    {
        Q_UNUSED(projectFrame);
        return interactive.contains(uuid);
    }

    QVector<Uuid> physicsEntities(int projectFrame) const override
    {
        Q_UNUSED(projectFrame);
        return bodies;
    }
};

void testNormalTracks()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("normal"));
    setTrack(entity, "x", {{0, 0.0}, {10, 100.0}});
    setTrack(entity, "y", {{0, 0.0}, {20, 50.0}});
    setTrack(entity, "rotation", {{0, 0.0}, {10, 90.0}});
    setTrack(entity, "opacity", {{0, 1.0}, {10, 0.5}});
    addEntity(project, entity);

    Entity staticEntity = makeEntity(QStringLiteral("static"));
    staticEntity.transform.x = 7.0;
    staticEntity.transform.y = 9.0;
    staticEntity.transform.opacity = 0.4;
    addEntity(project, staticEntity);

    MockWorld world;
    const anim::Pose pose = anim::evaluateEntity(project, QStringLiteral("normal"), 5, world);
    const QPointF origin = pose.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(origin.x(), 50.0, 1e-6);
    CHECK_NEAR(origin.y(), 12.5, 1e-6);
    CHECK_NEAR(pose.opacity, 0.75, 1e-9);
    CHECK(pose.visible);

    const QPointF xUnit = pose.world.map(QPointF(1.0, 0.0));
    CHECK_NEAR(xUnit.x() - origin.x(), std::cos(M_PI / 4.0), 1e-9);
    CHECK_NEAR(xUnit.y() - origin.y(), std::sin(M_PI / 4.0), 1e-9);

    const anim::Pose staticPose = anim::evaluateEntity(
        project, QStringLiteral("static"), 42, world);
    const QPointF staticOrigin = staticPose.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(staticOrigin.x(), 7.0, 1e-9);
    CHECK_NEAR(staticOrigin.y(), 9.0, 1e-9);
    CHECK_NEAR(staticPose.opacity, 0.4, 1e-9);
}

void testPivotFormula()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("pivot"));
    entity.transform.x = 10.0;
    entity.transform.y = 20.0;
    entity.transform.rotation = 90.0;
    entity.transform.scaleX = 2.0;
    entity.transform.scaleY = 3.0;
    entity.transform.pivotX = 5.0;
    entity.transform.pivotY = 0.0;
    addEntity(project, entity);

    MockWorld world;
    const anim::Pose pose = anim::evaluateEntity(project, entity.uuid, 0, world);
    const QPointF pivotPoint = pose.world.map(QPointF(5.0, 0.0));
    const QPointF offsetPoint = pose.world.map(QPointF(6.0, 0.0));
    CHECK_NEAR(pivotPoint.x(), 15.0, 1e-9);
    CHECK_NEAR(pivotPoint.y(), 20.0, 1e-9);
    CHECK_NEAR(offsetPoint.x(), 15.0, 1e-9);
    CHECK_NEAR(offsetPoint.y(), 22.0, 1e-9);
}

void testGravitySegment()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("falling"), 0, 100);
    setTrack(entity, "x", {{0, 0.0}});
    setTrack(entity, "y", {{0, 0.0}});

    SolverSegment segment = makeSegment(
        QStringLiteral("seg-gravity"), SolverEffect::Gravity, 0, 10,
        QStringLiteral("ground"));
    segment.initialVelocity = QPointF(0.0, 30.0);
    entity.solvers.append(segment);
    addEntity(project, entity);
    addEntity(project, makeEntity(QStringLiteral("ground")));

    MockWorld world;
    world.poses.insert(QStringLiteral("ground"),
                       solver::Pose{QPointF(0.0, 500.0), 0.0});

    const QPointF frame1 = anim::evaluateEntity(project, entity.uuid, 1, world)
                               .world.map(QPointF(0.0, 0.0));
    const double expected = 30.0 / 30.0 + 0.5 * 980.0 / (30.0 * 30.0);
    CHECK_NEAR(frame1.x(), 0.0, 1e-9);
    CHECK_NEAR(frame1.y(), expected, 1e-9);
}

void testSpinSegment()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("spin"), 0, 100);
    setTrack(entity, "rotation", {{0, 10.0}, {20, 100.0}});
    SolverSegment segment = makeSegment(
        QStringLiteral("seg-spin"), SolverEffect::Spin, 0, 10);
    segment.params.insert(QStringLiteral("angularVelocity"), 6.0);
    segment.params.insert(QStringLiteral("direction"), 1);
    entity.solvers.append(segment);
    addEntity(project, entity);

    MockWorld world;
    const auto rotationAt = [&](int frame)
    {
        const anim::Pose pose = anim::evaluateEntity(project, entity.uuid, frame, world);
        return qRadiansToDegrees(std::atan2(pose.world.m12(), pose.world.m11()));
    };

    CHECK_NEAR(rotationAt(5), 40.0, 1e-6);
    CHECK_NEAR(rotationAt(15), 85.0, 1e-6);
    CHECK_NEAR(rotationAt(20), 100.0, 1e-6);
}

void testOrbitSegmentMiddle()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("orbit"), 10, 110);
    entity.transform.opacity = 0.9;
    setTrack(entity, "x", {{10, 600.0}});
    setTrack(entity, "y", {{10, 500.0}});

    SolverSegment segment = makeSegment(
        QStringLiteral("seg-orbit"), SolverEffect::Orbit, 10, 40,
        QStringLiteral("target"));
    segment.params.insert(QStringLiteral("periodFrames"), 60.0);
    segment.params.insert(QStringLiteral("aspect"), 1.0);
    segment.params.insert(QStringLiteral("angle"), 0.0);
    segment.params.insert(QStringLiteral("direction"), 1);
    entity.solvers.append(segment);
    addEntity(project, entity);

    Entity target = makeEntity(QStringLiteral("target"));
    target.transform.x = 500.0;
    target.transform.y = 500.0;
    addEntity(project, target);

    MockWorld world;
    world.poses.insert(QStringLiteral("target"),
                       solver::Pose{QPointF(500.0, 500.0), 0.0});

    const model::SolverSegment* active = anim::solverAt(entity, 10);
    CHECK(active != nullptr && active->id == QStringLiteral("seg-orbit"));
    CHECK(anim::solverAt(entity, 40) == active);
    CHECK(anim::solverAt(entity, 41) == nullptr);
    CHECK(anim::solverAt(entity, 9) == nullptr);

    const anim::Pose start = anim::evaluateEntity(
        project, QStringLiteral("orbit"), 20, world);
    const QPointF startOrigin = start.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(startOrigin.x(), 600.0, 1e-6);
    CHECK_NEAR(startOrigin.y(), 500.0, 1e-6);
    CHECK_NEAR(start.opacity, 0.9, 1e-9);

    const anim::Pose pose = anim::evaluateEntity(
        project, QStringLiteral("orbit"), 25, world);
    const QPointF origin = pose.world.map(QPointF(0.0, 0.0));
    const double expectedX = 500.0 + 100.0 * std::cos(M_PI / 6.0);
    const double expectedY = 500.0 + 100.0 * std::sin(M_PI / 6.0);
    CHECK_NEAR(origin.x(), expectedX, 1e-6);
    CHECK_NEAR(origin.y(), expectedY, 1e-6);
    CHECK(world.queriedFrames.contains(25));
}

void testSegmentEndContinuity()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("continuity"), 0, 200);
    setTrack(entity, "x", {{0, 100.0}, {60, 200.0}});

    SolverSegment segment = makeSegment(
        QStringLiteral("seg-continuity"), SolverEffect::Orbit, 0, 30,
        QStringLiteral("origin"));
    segment.params.insert(QStringLiteral("periodFrames"), 60.0);
    entity.solvers.append(segment);
    addEntity(project, entity);

    addEntity(project, makeEntity(QStringLiteral("origin")));

    MockWorld world;
    world.poses.insert(QStringLiteral("origin"),
                       solver::Pose{QPointF(0.0, 0.0), 0.0});

    const double x30 = anim::evaluateEntity(project, QStringLiteral("continuity"), 30, world)
                           .world.map(QPointF(0.0, 0.0)).x();
    const double x31 = anim::evaluateEntity(project, QStringLiteral("continuity"), 31, world)
                           .world.map(QPointF(0.0, 0.0)).x();
    const double x45 = anim::evaluateEntity(project, QStringLiteral("continuity"), 45, world)
                           .world.map(QPointF(0.0, 0.0)).x();
    const double x60 = anim::evaluateEntity(project, QStringLiteral("continuity"), 60, world)
                           .world.map(QPointF(0.0, 0.0)).x();
    const double y31 = anim::evaluateEntity(project, QStringLiteral("continuity"), 31, world)
                           .world.map(QPointF(0.0, 0.0)).y();

    CHECK_NEAR(x30, -100.0, 1e-6);
    CHECK_NEAR(x31, -90.0, 1e-6);
    CHECK_NEAR(x45, 50.0, 1e-6);
    CHECK_NEAR(x60, 200.0, 1e-6);
    CHECK_NEAR(y31, 0.0, 1e-6);
    CHECK_NEAR(x31 - x30, 10.0, 1e-6);
    CHECK(anim::solverAt(entity, 31) == nullptr);
}

void testFractionalTracks()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("fractional"));
    setTrack(entity, "x", {{0, 0.0}, {10, 10.0}});
    addEntity(project, entity);

    Entity stepEntity = makeEntity(QStringLiteral("fractional-visible"));
    setTrack(stepEntity, "visible", {{0, 1.0}, {10, 0.0}});
    addEntity(project, stepEntity);

    MockWorld world;
    const QPointF mid = anim::evaluateEntity(project, entity.uuid, 5.5, world)
                            .world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(mid.x(), 5.5, 1e-9);

    const QPointF quarter =
        anim::evaluateEntity(project, entity.uuid, 2.25, world)
            .world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(quarter.x(), 2.25, 1e-9);

    // Step tracks only change on integer frames.
    CHECK(anim::evaluateEntity(project, stepEntity.uuid, 9.9, world).visible);
    CHECK(!anim::evaluateEntity(project, stepEntity.uuid, 10.0, world).visible);
}

void testFractionalSolverSegment()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("fractional-spin"), 0, 100);
    setTrack(entity, "rotation", {{0, 10.0}, {20, 100.0}});
    SolverSegment segment = makeSegment(
        QStringLiteral("seg-fractional-spin"), SolverEffect::Spin, 0, 10);
    segment.params.insert(QStringLiteral("angularVelocity"), 6.0);
    segment.params.insert(QStringLiteral("direction"), 1);
    entity.solvers.append(segment);
    addEntity(project, entity);

    MockWorld world;
    const auto rotationAt = [&](double frame)
    {
        const anim::Pose pose = anim::evaluateEntity(
            project, entity.uuid, frame, world);
        return qRadiansToDegrees(std::atan2(pose.world.m12(), pose.world.m11()));
    };

    // Samples sit on integer frames; 5.5 interpolates spin samples 5 and 6.
    CHECK_NEAR(rotationAt(5.0), 40.0, 1e-6);
    CHECK_NEAR(rotationAt(6.0), 46.0, 1e-6);
    CHECK_NEAR(rotationAt(5.5), 43.0, 1e-6);
    // After the segment the solved end sample is the implicit left value.
    CHECK_NEAR(rotationAt(10.5), 71.5, 1e-6);

    // Gravity drives x/y; a fractional frame lands between its two samples.
    Project fallProject;
    Entity falling = makeEntity(QStringLiteral("fractional-fall"), 0, 100);
    setTrack(falling, "x", {{0, 0.0}});
    setTrack(falling, "y", {{0, 0.0}});
    SolverSegment gravity = makeSegment(
        QStringLiteral("seg-fractional-gravity"), SolverEffect::Gravity, 0, 10,
        QStringLiteral("ground"));
    gravity.initialVelocity = QPointF(0.0, 30.0);
    falling.solvers.append(gravity);
    addEntity(fallProject, falling);
    addEntity(fallProject, makeEntity(QStringLiteral("ground")));

    MockWorld fallWorld;
    fallWorld.poses.insert(QStringLiteral("ground"),
                           solver::Pose{QPointF(0.0, 500.0), 0.0});
    const auto yAt = [&](double frame)
    {
        return anim::evaluateEntity(fallProject, falling.uuid, frame, fallWorld)
            .world.map(QPointF(0.0, 0.0))
            .y();
    };

    const double y1 = yAt(1.0);
    const double y2 = yAt(2.0);
    const double y15 = yAt(1.5);
    CHECK(y15 > y1);
    CHECK(y15 < y2);
    CHECK_NEAR(y15, (y1 + y2) / 2.0, 1e-9);
}

void testParentTransform()
{
    Project project;
    Entity parent = makeEntity(QStringLiteral("parent"));
    parent.transform.x = 100.0;
    parent.transform.y = 50.0;
    parent.transform.rotation = 90.0;
    parent.transform.scaleX = 2.0;
    parent.transform.scaleY = 2.0;
    parent.transform.opacity = 0.5;
    addEntity(project, parent);

    Entity child = makeEntity(QStringLiteral("child"));
    child.parentUuid = QStringLiteral("parent");
    child.transform.x = 10.0;
    child.transform.opacity = 0.5;
    addEntity(project, child);

    MockWorld world;
    const anim::Pose parentPose = anim::evaluateEntity(
        project, QStringLiteral("parent"), 0, world);
    const QPointF parentOrigin = parentPose.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(parentOrigin.x(), 100.0, 1e-9);
    CHECK_NEAR(parentOrigin.y(), 50.0, 1e-9);

    const anim::Pose childPose = anim::evaluateEntity(
        project, QStringLiteral("child"), 0, world);
    const QPointF origin = childPose.world.map(QPointF(0.0, 0.0));
    const QPointF xUnit = childPose.world.map(QPointF(1.0, 0.0));
    CHECK_NEAR(origin.x(), 100.0, 1e-9);
    CHECK_NEAR(origin.y(), 70.0, 1e-9);
    CHECK_NEAR(xUnit.x() - origin.x(), 0.0, 1e-9);
    CHECK_NEAR(xUnit.y() - origin.y(), 2.0, 1e-9);
    CHECK_NEAR(childPose.opacity, 0.25, 1e-9);
}

void testVisibilityAndOpacity()
{
    Project project;

    Entity parent = makeEntity(QStringLiteral("vp"));
    parent.transform.opacity = 0.5;
    setTrack(parent, "visible", {{0, 1.0}, {10, 0.0}});
    addEntity(project, parent);

    Entity inheriting = makeEntity(QStringLiteral("vi"));
    inheriting.parentUuid = parent.uuid;
    inheriting.transform.opacity = 0.5;
    addEntity(project, inheriting);

    Entity independent = makeEntity(QStringLiteral("vn"));
    independent.parentUuid = parent.uuid;
    independent.inheritVisibility = false;
    addEntity(project, independent);

    Entity grandchild = makeEntity(QStringLiteral("grandchild"));
    grandchild.parentUuid = inheriting.uuid;
    grandchild.transform.opacity = 0.5;
    addEntity(project, grandchild);

    Entity hiddenParent = makeEntity(QStringLiteral("hidden-parent"));
    hiddenParent.hidden = true;
    addEntity(project, hiddenParent);

    Entity hiddenChild = makeEntity(QStringLiteral("hidden-child"));
    hiddenChild.parentUuid = hiddenParent.uuid;
    addEntity(project, hiddenChild);

    Entity independentHiddenChild = makeEntity(QStringLiteral("hidden-child-independent"));
    independentHiddenChild.parentUuid = hiddenParent.uuid;
    independentHiddenChild.inheritVisibility = false;
    addEntity(project, independentHiddenChild);

    Entity hiddenEntity = makeEntity(QStringLiteral("hidden"));
    hiddenEntity.hidden = true;
    addEntity(project, hiddenEntity);

    Entity invisibleEntity = makeEntity(QStringLiteral("invisible"));
    invisibleEntity.visible = false;
    addEntity(project, invisibleEntity);

    Entity stepVisible = makeEntity(QStringLiteral("step-visible"));
    setTrack(stepVisible, "visible", {{0, 1.0}, {10, 0.0}});
    addEntity(project, stepVisible);

    Entity physics = makeEntity(QStringLiteral("physics"));
    setTrack(physics, "physicsInteract", {{0, 1.0}, {10, 0.0}});
    addEntity(project, physics);

    MockWorld world;

    const anim::Pose inheritedAt5 = anim::evaluateEntity(
        project, inheriting.uuid, 5, world);
    CHECK(inheritedAt5.visible);
    CHECK_NEAR(inheritedAt5.opacity, 0.25, 1e-9);

    const anim::Pose grandchildAt5 = anim::evaluateEntity(
        project, grandchild.uuid, 5, world);
    CHECK(grandchildAt5.visible);
    CHECK_NEAR(grandchildAt5.opacity, 0.125, 1e-9);

    CHECK(!anim::evaluateEntity(project, inheriting.uuid, 10, world).visible);
    CHECK(!anim::evaluateEntity(project, grandchild.uuid, 10, world).visible);
    CHECK(anim::evaluateEntity(project, independent.uuid, 10, world).visible);
    CHECK(!anim::evaluateEntity(project, parent.uuid, 10, world).visible);

    CHECK(!anim::evaluateEntity(project, hiddenChild.uuid, 0, world).visible);
    CHECK(anim::evaluateEntity(project, independentHiddenChild.uuid, 0, world).visible);
    CHECK(!anim::evaluateEntity(project, hiddenEntity.uuid, 0, world).visible);
    CHECK(!anim::evaluateEntity(project, invisibleEntity.uuid, 0, world).visible);

    CHECK(anim::evaluateEntity(project, stepVisible.uuid, 9, world).visible);
    CHECK(!anim::evaluateEntity(project, stepVisible.uuid, 10, world).visible);
    CHECK(anim::physicsInteractAt(project, physics.uuid, 9));
    CHECK(!anim::physicsInteractAt(project, physics.uuid, 10));
}

void testMissingParentAndCycles()
{
    Project project;

    Entity orphan = makeEntity(QStringLiteral("orphan"));
    orphan.parentUuid = QStringLiteral("missing-parent");
    orphan.transform.x = 5.0;
    orphan.transform.y = 7.0;
    addEntity(project, orphan);

    Entity a = makeEntity(QStringLiteral("cycle-a"));
    a.parentUuid = QStringLiteral("cycle-b");
    a.transform.x = 1.0;
    addEntity(project, a);

    Entity b = makeEntity(QStringLiteral("cycle-b"));
    b.parentUuid = QStringLiteral("cycle-a");
    b.transform.x = 1.0;
    addEntity(project, b);

    Entity self = makeEntity(QStringLiteral("self-parent"));
    self.parentUuid = self.uuid;
    self.transform.x = 3.0;
    addEntity(project, self);

    MockWorld world;
    const QPointF orphanOrigin = anim::evaluateEntity(project, orphan.uuid, 0, world)
                                     .world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(orphanOrigin.x(), 5.0, 1e-9);
    CHECK_NEAR(orphanOrigin.y(), 7.0, 1e-9);

    const QPointF cycleOrigin = anim::evaluateEntity(project, a.uuid, 0, world)
                                    .world.map(QPointF(0.0, 0.0));
    CHECK(std::isfinite(cycleOrigin.x()));
    CHECK(std::isfinite(cycleOrigin.y()));
    CHECK_NEAR(cycleOrigin.x(), 2.0, 1e-9);

    const QPointF selfOrigin = anim::evaluateEntity(project, self.uuid, 0, world)
                                   .world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(selfOrigin.x(), 3.0, 1e-9);

    CHECK(anim::evaluateEntity(project, QStringLiteral("does-not-exist"), 0, world)
              .world.isIdentity());
}

void testMissingTargetFallsBack()
{
    Project project;

    Entity noTarget = makeEntity(QStringLiteral("no-target"));
    setTrack(noTarget, "x", {{0, 0.0}, {20, 100.0}});
    setTrack(noTarget, "y", {{0, 0.0}, {20, 100.0}});
    noTarget.solvers.append(makeSegment(QStringLiteral("seg-no-target"),
                                        SolverEffect::Orbit, 5, 15));
    addEntity(project, noTarget);

    Entity dangling = makeEntity(QStringLiteral("dangling-target"));
    setTrack(dangling, "x", {{0, 0.0}, {20, 100.0}});
    setTrack(dangling, "y", {{0, 0.0}, {20, 100.0}});
    dangling.solvers.append(makeSegment(QStringLiteral("seg-dangling"),
                                        SolverEffect::Orbit, 5, 15,
                                        QStringLiteral("ghost")));
    addEntity(project, dangling);

    MockWorld world;

    CHECK(anim::solverAt(noTarget, 10) != nullptr);
    CHECK(anim::solverAt(dangling, 10) != nullptr);

    const anim::Pose noTargetPose = anim::evaluateEntity(
        project, noTarget.uuid, 10, world);
    const QPointF noTargetOrigin = noTargetPose.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(noTargetOrigin.x(), 50.0, 1e-9);
    CHECK_NEAR(noTargetOrigin.y(), 50.0, 1e-9);

    const anim::Pose danglingPose = anim::evaluateEntity(
        project, dangling.uuid, 10, world);
    const QPointF danglingOrigin = danglingPose.world.map(QPointF(0.0, 0.0));
    CHECK_NEAR(danglingOrigin.x(), 50.0, 1e-9);
    CHECK_NEAR(danglingOrigin.y(), 50.0, 1e-9);
}

void testSolverAtOpenEnded()
{
    Entity entity = makeEntity(QStringLiteral("open-ended"), 5, 25);
    entity.solvers.append(makeSegment(QStringLiteral("seg-open"),
                                      SolverEffect::Spin, 3, -1));

    CHECK(anim::solverAt(entity, 2) == nullptr);
    CHECK(anim::solverAt(entity, 3) != nullptr);
    CHECK(anim::solverAt(entity, 19) != nullptr);
    CHECK(anim::solverAt(entity, 20) == nullptr);
}

void testCache()
{
    Project project;
    Entity entity = makeEntity(QStringLiteral("cached"), 0, 100);
    setTrack(entity, "x", {{0, 100.0}});
    entity.solvers.append(makeSegment(QStringLiteral("seg-cache"),
                                      SolverEffect::Orbit, 0, 30,
                                      QStringLiteral("origin")));
    entity.solvers.first().params.insert(QStringLiteral("periodFrames"), 60.0);
    addEntity(project, entity);

    addEntity(project, makeEntity(QStringLiteral("origin")));

    MockWorld world;
    world.poses.insert(QStringLiteral("origin"),
                       solver::Pose{QPointF(0.0, 0.0), 0.0});

    anim::Cache cache;
    const anim::Pose first = anim::evaluateEntity(project, entity.uuid, 15, world, cache);
    const int queriesAfterFirst = world.poseQueries;
    CHECK(queriesAfterFirst > 0);

    const anim::Pose second = anim::evaluateEntity(project, entity.uuid, 15, world, cache);
    CHECK(world.poseQueries == queriesAfterFirst);
    CHECK_NEAR(second.world.map(QPointF(0.0, 0.0)).x(),
               first.world.map(QPointF(0.0, 0.0)).x(), 1e-12);

    cache.clearEntity(entity.uuid);
    anim::evaluateEntity(project, entity.uuid, 15, world, cache);
    CHECK(world.poseQueries > queriesAfterFirst);

    cache.clear();
    CHECK(cache.poses.isEmpty());
    CHECK(cache.solverSamples.isEmpty());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testNormalTracks();
    testFractionalTracks();
    testPivotFormula();
    testGravitySegment();
    testSpinSegment();
    testFractionalSolverSegment();
    testOrbitSegmentMiddle();
    testSegmentEndContinuity();
    testParentTransform();
    testVisibilityAndOpacity();
    testMissingParentAndCycles();
    testMissingTargetFallsBack();
    testSolverAtOpenEnded();
    testCache();

    QTextStream(stdout) << QStringLiteral("%1 checks, %2 failure(s)\n")
                               .arg(gChecks)
                               .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
