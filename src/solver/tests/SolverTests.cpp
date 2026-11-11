#include "solver/Solver.h"

#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace pvf;
using namespace pvf::solver;

namespace {

int failures = 0;

void report(const QString &message)
{
    ++failures;
    QTextStream(stderr) << message << '\n';
}

void check(bool ok, const char *expression, const char *file, int line)
{
    if (!ok)
        report(QStringLiteral("FAIL %1:%2  %3").arg(QString::fromLatin1(file))
                       .arg(line).arg(QString::fromLatin1(expression)));
}

void checkNear(double actual, double expected, double epsilon,
               const char *expression, const char *file, int line)
{
    if (std::abs(actual - expected) > epsilon)
        report(QStringLiteral("FAIL %1:%2  %3  (got %4, expected %5, eps %6)")
                       .arg(QString::fromLatin1(file)).arg(line)
                       .arg(QString::fromLatin1(expression))
                       .arg(actual).arg(expected).arg(epsilon));
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, epsilon) \
    checkNear((actual), (expected), (epsilon), #actual " ~= " #expected, __FILE__, __LINE__)

class MockWorld final : public World
{
public:
    QHash<Uuid, Pose> poses;
    QHash<Uuid, QPainterPath> shapes;
    QSet<Uuid> interactive;
    QVector<Uuid> bodies;
    bool enumerateBodies = true;
    mutable QSet<Uuid> interactQueries;

    Pose poseOf(const Uuid &uuid, int frame) const override
    {
        (void)frame;
        return poses.value(uuid);
    }

    QPainterPath shapeOf(const Uuid &uuid, int frame) const override
    {
        (void)frame;
        return shapes.value(uuid);
    }

    bool physicsInteract(const Uuid &uuid, int frame) const override
    {
        (void)frame;
        interactQueries.insert(uuid);
        return interactive.contains(uuid);
    }

    QVector<Uuid> physicsEntities(int frame) const override
    {
        (void)frame;
        return enumerateBodies ? bodies : QVector<Uuid>{};
    }
};

const Sample *atFrame(const QVector<Sample> &samples, int frame)
{
    if (samples.isEmpty())
        return nullptr;
    const int index = frame - samples.first().frame;
    if (index < 0 || index >= samples.size())
        return nullptr;
    return &samples.at(index);
}

Segment gravitySegment(const QPointF &start, const QPointF &velocity)
{
    Segment segment;
    segment.effect = SolverEffect::Gravity;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("ground");
    segment.startFrame = 0;
    segment.endFrame = -1;
    segment.startPose.position = start;
    segment.initialVelocity = velocity;
    segment.params[QStringLiteral("strength")] = 300.0;
    segment.params[QStringLiteral("stopThreshold")] = 0.0;
    return segment;
}

void testGravityParabola()
{
    MockWorld world;
    const Segment segment = gravitySegment(QPointF(0.0, 0.0), QPointF(60.0, 0.0));
    const Result result = evaluateResult(segment, world, 31, 30);
    const QVector<Sample> &samples = result.samples;

    CHECK(samples.size() == 31);
    // Nothing stops the fall, so automatic mode ends at the clip end and this
    // is not a physical stop.
    CHECK(result.stopFrame == 30);
    CHECK(result.reason == StopReason::ClipEnd);
    const Sample *last = atFrame(samples, 30);
    CHECK(last != nullptr);
    if (last)
    {
        CHECK_NEAR(last->pose.position.x(), 60.0, 1e-6);
        CHECK_NEAR(last->pose.position.y(), -150.0, 1e-6);
        CHECK_NEAR(last->velocity.x(), 60.0, 1e-9);
        CHECK_NEAR(last->velocity.y(), -300.0, 1e-9);
        CHECK(!last->stopped);
    }

    const Sample *mid = atFrame(samples, 15);
    CHECK(mid != nullptr);
    if (mid)
    {
        CHECK_NEAR(mid->pose.position.x(), 30.0, 1e-6);
        CHECK_NEAR(mid->pose.position.y(), -37.5, 1e-6);
        CHECK_NEAR(mid->velocity.x(), 60.0, 1e-9);
        CHECK_NEAR(mid->velocity.y(), -150.0, 1e-9);
    }
}

void testGravityDirectionUsesTarget()
{
    MockWorld world;

    // Solver space is Y-up, so gravity points along the target's local -Y.
    // Rotating the ground 180 degrees therefore pulls the ball up (+Y).
    world.poses.insert(QStringLiteral("ground"), Pose{QPointF(0.0, 0.0), 180.0});
    Segment upward = gravitySegment(QPointF(0.0, 0.0), QPointF(0.0, 0.0));
    const QVector<Sample> upSamples = evaluate(upward, world, 31, 30);
    const Sample *upLast = atFrame(upSamples, 30);
    CHECK(upLast != nullptr);
    if (upLast)
    {
        CHECK_NEAR(upLast->pose.position.x(), 0.0, 1e-6);
        CHECK_NEAR(upLast->pose.position.y(), 150.0, 1e-6);
    }

    // Rotating the driven ball must not influence gravity direction.
    world.poses[QStringLiteral("ground")] = Pose{QPointF(0.0, 0.0), 0.0};
    Segment ballRotated = gravitySegment(QPointF(0.0, 0.0), QPointF(0.0, 0.0));
    ballRotated.startPose.rotation = 180.0;
    const QVector<Sample> downSamples = evaluate(ballRotated, world, 31, 30);
    const Sample *downLast = atFrame(downSamples, 30);
    CHECK(downLast != nullptr);
    if (downLast)
    {
        CHECK_NEAR(downLast->pose.position.x(), 0.0, 1e-6);
        CHECK_NEAR(downLast->pose.position.y(), -150.0, 1e-6);
    }

    // initialVelocity and Sample::velocity are solver-space values; rotating
    // the ball must not rotate them.
    Segment moving = gravitySegment(QPointF(0.0, 0.0), QPointF(100.0, 0.0));
    moving.startPose.rotation = 90.0;
    moving.params[QStringLiteral("strength")] = 0.0;
    const QVector<Sample> movingSamples = evaluate(moving, world, 31, 30);
    const Sample *movingMid = atFrame(movingSamples, 15);
    const Sample *movingLast = atFrame(movingSamples, 30);
    CHECK(movingMid != nullptr);
    CHECK(movingLast != nullptr);
    if (movingMid && movingLast)
    {
        CHECK_NEAR(movingMid->pose.position.x(), 50.0, 1e-6);
        CHECK_NEAR(movingMid->pose.position.y(), 0.0, 1e-6);
        CHECK_NEAR(movingMid->velocity.x(), 100.0, 1e-9);
        CHECK_NEAR(movingMid->velocity.y(), 0.0, 1e-9);
        CHECK_NEAR(movingLast->pose.position.x(), 100.0, 1e-6);
        CHECK_NEAR(movingLast->pose.position.y(), 0.0, 1e-6);
    }
}

void testGravityBounce()
{
    MockWorld world;
    const QPointF start(0.0, 0.0);

    QPainterPath ball;
    ball.addRect(QRectF(-10.0, -10.0, 20.0, 20.0));
    ball.translate(start);
    world.shapes.insert(QStringLiteral("ball"), ball);

    QPainterPath ground;
    // Y-up: the floor is below the ball, occupying y in [-300, -100].
    ground.addRect(QRectF(-500.0, -300.0, 1000.0, 200.0));
    world.shapes.insert(QStringLiteral("ground"), ground);
    world.poses.insert(QStringLiteral("ground"), Pose{QPointF(0.0, 0.0), 0.0});

    // Ball and ground are both physics-interactive, but the ball is the driven
    // entity and must be excluded.  The ground is deliberately not enumerated
    // through physicsEntities(): targetUuid must still participate.
    world.interactive.insert(QStringLiteral("ball"));
    world.interactive.insert(QStringLiteral("ground"));
    world.bodies.append(QStringLiteral("ball"));

    Segment segment;
    segment.effect = SolverEffect::Gravity;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("ground");
    segment.startFrame = 0;
    segment.endFrame = 59;
    segment.startPose.position = start;
    segment.params[QStringLiteral("strength")] = 500.0;
    segment.params[QStringLiteral("elasticity")] = 0.7;
    segment.params[QStringLiteral("friction")] = 0.1;
    segment.params[QStringLiteral("stopThreshold")] = 0.5;

    const QVector<Sample> samples = evaluate(segment, world, 60, 30);
    CHECK(samples.size() == 60);

    bool collided = false;
    bool bounced = false;
    double deepestBottom = 1.0e9;
    for (const Sample &sample : samples)
    {
        collided = collided || sample.collided;
        bounced = bounced || sample.velocity.y() > 1.0;
        deepestBottom = std::min(deepestBottom, sample.pose.position.y() - 10.0);
    }

    CHECK(collided);
    CHECK(bounced);
    CHECK_NEAR(deepestBottom, -100.0, 0.05);
    // The explicit endFrame 59 cuts the range while the ball is still
    // bouncing; that is not a physical stop.
    const Result result = evaluateResult(segment, world, 60, 30);
    CHECK(result.stopFrame == 59);
    CHECK(result.reason == StopReason::ExplicitEnd);

    // Direct evidence for candidate filtering: target was queried, entity was
    // never even checked as a potential collider.
    CHECK(world.interactQueries.contains(QStringLiteral("ground")));
    CHECK(!world.interactQueries.contains(QStringLiteral("ball")));
}

void testGravityStopsOnImpact()
{
    MockWorld world;
    world.enumerateBodies = false;

    QPainterPath ball;
    ball.addRect(QRectF(-10.0, -10.0, 20.0, 20.0));
    world.shapes.insert(QStringLiteral("ball"), ball);
    world.poses.insert(QStringLiteral("ball"), Pose{QPointF(0.0, 0.0), 0.0});

    QPainterPath ground;
    // Y-up: the floor is below the ball, occupying y in [-300, -100].
    ground.addRect(QRectF(-500.0, -300.0, 1000.0, 200.0));
    world.shapes.insert(QStringLiteral("ground"), ground);
    world.poses.insert(QStringLiteral("ground"), Pose{QPointF(0.0, 0.0), 0.0});
    world.interactive.insert(QStringLiteral("ground"));

    Segment segment;
    segment.effect = SolverEffect::Gravity;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("ground");
    segment.startFrame = 0;
    segment.endFrame = -1; // automatic: stop where the ball comes to rest
    segment.startPose.position = QPointF(0.0, 0.0);
    segment.params[QStringLiteral("strength")] = 500.0;
    segment.params[QStringLiteral("elasticity")] = 0.0;
    segment.params[QStringLiteral("friction")] = 0.5;
    segment.params[QStringLiteral("stopThreshold")] = 10.0;

    const Result result = evaluateResult(segment, world, 60, 30);
    const QVector<Sample> &samples = result.samples;
    CHECK(samples.size() == 60);

    int firstStopped = -1;
    for (int i = 0; i < samples.size(); ++i)
    {
        if (samples.at(i).stopped)
        {
            firstStopped = i;
            break;
        }
    }
    CHECK(firstStopped > 0);
    CHECK(firstStopped >= 0 && firstStopped < samples.size() - 1);
    // Automatic mode reports the physical collision, not the clip end.
    CHECK(result.stopFrame == firstStopped);
    CHECK(result.reason == StopReason::Collision);
    if (firstStopped >= 0)
    {
        const QPointF resting = samples.at(firstStopped).pose.position;
        // Floor top is y = -100; the 20 px ball rests with centre at -90.
        CHECK_NEAR(resting.y(), -90.0, 0.1);
        for (int i = firstStopped; i < samples.size(); ++i)
        {
            CHECK_NEAR(samples.at(i).pose.position.x(), resting.x(), 1e-9);
            CHECK_NEAR(samples.at(i).pose.position.y(), resting.y(), 1e-9);
            CHECK_NEAR(samples.at(i).velocity.x(), 0.0, 1e-12);
            CHECK_NEAR(samples.at(i).velocity.y(), 0.0, 1e-12);
            CHECK(samples.at(i).stopped);
        }
    }
}

void testOrbitPeriod()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("sun"), Pose{QPointF(100.0, 100.0), 0.0});

    Segment segment;
    segment.effect = SolverEffect::Orbit;
    segment.entityUuid = QStringLiteral("planet");
    segment.targetUuid = QStringLiteral("sun");
    segment.startFrame = 0;
    segment.endFrame = 120;
    segment.startPose.position = QPointF(300.0, 100.0);
    segment.startPose.rotation = 15.0;
    segment.params[QStringLiteral("aspect")] = 0.5;
    segment.params[QStringLiteral("angle")] = 0.0;
    segment.params[QStringLiteral("direction")] = 1;
    segment.params[QStringLiteral("periodFrames")] = 60.0;

    const QVector<Sample> samples = evaluate(segment, world, 121, 30);
    CHECK(samples.size() == 121);

    const Sample *start = atFrame(samples, 0);
    const Sample *quarter = atFrame(samples, 15);
    const Sample *half = atFrame(samples, 30);
    const Sample *period = atFrame(samples, 60);
    CHECK(start && quarter && half && period);
    if (start && quarter && half && period)
    {
        CHECK_NEAR(start->pose.position.x(), 300.0, 1e-9);
        CHECK_NEAR(start->pose.position.y(), 100.0, 1e-9);
        // direction +1 is clockwise: a quarter turn from the +X side of the
        // sun lands below it, at the same Y.
        CHECK_NEAR(quarter->pose.position.x(), 100.0, 1e-6);
        CHECK_NEAR(quarter->pose.position.y(), 0.0, 1e-6);
        CHECK_NEAR(half->pose.position.x(), -100.0, 1e-6);
        CHECK_NEAR(half->pose.position.y(), 100.0, 1e-6);
        CHECK_NEAR(period->pose.position.x(), 300.0, 1e-6);
        CHECK_NEAR(period->pose.position.y(), 100.0, 1e-6);
        CHECK_NEAR(period->pose.rotation, 15.0, 1e-12);

        // Solver-space velocity, not rotated into the planet's local axes.
        CHECK_NEAR(quarter->velocity.x(), -627.2, 5.0);
        CHECK_NEAR(quarter->velocity.y(), 16.4, 5.0);
    }
    // Explicit endFrame keeps the solved range even though orbit never stops.
    const Result result = evaluateResult(segment, world, 121, 30);
    CHECK(result.stopFrame == 120);
    CHECK(result.reason == StopReason::ExplicitEnd);
    CHECK(!samples.last().stopped);
    CHECK_NEAR(samples.last().velocity.x(), 0.0, 1e-12);
    CHECK_NEAR(samples.last().velocity.y(), 0.0, 1e-12);
}

void testGravityStopThresholdWithoutCollision()
{
    MockWorld world;
    Segment segment = gravitySegment(QPointF(10.0, 20.0), QPointF(0.0, -1.0));
    segment.params[QStringLiteral("strength")] = 0.0;
    segment.params[QStringLiteral("stopThreshold")] = 2.0;

    const Result result = evaluateResult(segment, world, 40, 30);
    CHECK(result.samples.size() == 40);
    // No acceleration and a negligible speed: physically at rest at frame 1.
    CHECK(result.stopFrame == 1);
    CHECK(result.reason == StopReason::StopThreshold);
    CHECK(result.samples.at(0).stopped == false);
    CHECK(result.samples.at(1).stopped);
    CHECK(result.samples.last().stopped);
    CHECK_NEAR(result.samples.last().pose.position.x(), 10.0, 1e-12);
    CHECK_NEAR(result.samples.last().pose.position.y(), 20.0, 1e-12);
}

void testExplicitEndKeepsRange()
{
    MockWorld world;
    QPainterPath ball;
    ball.addRect(QRectF(-10.0, -10.0, 20.0, 20.0));
    world.shapes.insert(QStringLiteral("ball"), ball);
    QPainterPath ground;
    ground.addRect(QRectF(-500.0, -300.0, 1000.0, 200.0));
    world.shapes.insert(QStringLiteral("ground"), ground);
    world.poses.insert(QStringLiteral("ground"), Pose{QPointF(0.0, 0.0), 0.0});
    world.interactive.insert(QStringLiteral("ground"));

    Segment segment;
    segment.effect = SolverEffect::Gravity;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("ground");
    segment.startFrame = 0;
    segment.endFrame = 40;
    segment.startPose.position = QPointF(0.0, 0.0);
    segment.params[QStringLiteral("strength")] = 500.0;
    segment.params[QStringLiteral("elasticity")] = 0.0;
    segment.params[QStringLiteral("friction")] = 0.5;
    segment.params[QStringLiteral("stopThreshold")] = 10.0;

    const Result result = evaluateResult(segment, world, 100, 30);
    CHECK(result.samples.size() == 41);
    // Explicit override wins over the earlier physical collision.
    CHECK(result.stopFrame == 40);
    CHECK(result.reason == StopReason::ExplicitEnd);
    CHECK(result.samples.last().frame == 40);
    CHECK(result.samples.last().stopped); // physical stop already happened
    // The pose stays frozen at the resting height for the whole explicit tail.
    CHECK_NEAR(result.samples.last().pose.position.y(), -90.0, 0.1);
}

void testOrbitDirections()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("sun"), Pose{QPointF(100.0, 100.0), 0.0});

    Segment segment;
    segment.effect = SolverEffect::Orbit;
    segment.entityUuid = QStringLiteral("planet");
    segment.targetUuid = QStringLiteral("sun");
    segment.startFrame = 0;
    segment.endFrame = 60;
    segment.startPose.position = QPointF(300.0, 100.0);
    segment.params[QStringLiteral("aspect")] = 0.5;
    segment.params[QStringLiteral("periodFrames")] = 60.0;

    segment.params[QStringLiteral("direction")] = 1;
    const QVector<Sample> clockwise = evaluate(segment, world, 61, 30);
    const Sample *cwQuarter = atFrame(clockwise, 15);
    CHECK(cwQuarter != nullptr);
    if (cwQuarter)
        CHECK_NEAR(cwQuarter->pose.position.y(), 0.0, 1e-6);

    segment.params[QStringLiteral("direction")] = -1;
    const QVector<Sample> counterClockwise = evaluate(segment, world, 61, 30);
    const Sample *ccwQuarter = atFrame(counterClockwise, 15);
    CHECK(ccwQuarter != nullptr);
    if (ccwQuarter)
        CHECK_NEAR(ccwQuarter->pose.position.y(), 200.0, 1e-6);
}

void testSpin()
{
    MockWorld world;
    Segment segment;
    segment.effect = SolverEffect::Spin;
    segment.entityUuid = QStringLiteral("wheel");
    segment.startFrame = 0;
    segment.endFrame = 30;
    segment.startPose.position = QPointF(10.0, 20.0);
    segment.startPose.rotation = 10.0;
    segment.params[QStringLiteral("angularVelocity")] = 10.0;
    segment.params[QStringLiteral("direction")] = 1;

    // Spin ignores targetUuid; an empty target must still evaluate.
    const QVector<Sample> samples = evaluate(segment, world, 31, 30);
    CHECK(samples.size() == 31);
    const Sample *start = atFrame(samples, 0);
    const Sample *ten = atFrame(samples, 10);
    const Sample *last = atFrame(samples, 30);
    CHECK(start && ten && last);
    if (start && ten && last)
    {
        CHECK_NEAR(start->pose.rotation, 10.0, 1e-12);
        // direction +1 is clockwise, i.e. negative in Y-up model space.
        CHECK_NEAR(ten->pose.rotation, -90.0, 1e-12);
        CHECK_NEAR(last->pose.rotation, -290.0, 1e-12);
        CHECK_NEAR(last->pose.position.x(), 10.0, 1e-12);
        CHECK_NEAR(last->pose.position.y(), 20.0, 1e-12);
        CHECK_NEAR(last->velocity.x(), 0.0, 1e-12);
        CHECK_NEAR(last->velocity.y(), 0.0, 1e-12);
        // The clip end is not a physical stop.
        CHECK(!last->stopped);
    }

    const Result spinResult = evaluateResult(segment, world, 31, 30);
    CHECK(spinResult.stopFrame == 30);
    CHECK(spinResult.reason == StopReason::ExplicitEnd);

    segment.params[QStringLiteral("direction")] = -1;
    const QVector<Sample> reverse = evaluate(segment, world, 31, 30);
    const Sample *reverseLast = atFrame(reverse, 30);
    CHECK(reverseLast != nullptr);
    if (reverseLast)
        CHECK_NEAR(reverseLast->pose.rotation, 310.0, 1e-12);
}

void testBlackHole()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("hole"), Pose{QPointF(0.0, 0.0), 0.0});

    Segment segment;
    segment.effect = SolverEffect::BlackHole;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("hole");
    segment.startFrame = 0;
    segment.endFrame = 60;
    segment.startPose.position = QPointF(300.0, 0.0);
    segment.params[QStringLiteral("simulationSpeed")] = 1.0;
    segment.params[QStringLiteral("mass")] = 5000.0;
    segment.params[QStringLiteral("angularMomentum")] = 0.0;
    segment.params[QStringLiteral("eventHorizonRadius")] = 20.0;
    segment.params[QStringLiteral("objectRadius")] = 5.0;
    segment.params[QStringLiteral("softening")] = 20.0;
    segment.params[QStringLiteral("maxSpeed")] = 0.0;

    segment.initialVelocity = QPointF(0.0, 0.0);
    const QVector<Sample> slow = evaluate(segment, world, 61, 30);
    CHECK(slow.size() == 61);
    double slowMinDistance = 1.0e9;
    for (const Sample &sample : slow)
        slowMinDistance = std::min(slowMinDistance,
                                   std::hypot(sample.pose.position.x(),
                                              sample.pose.position.y()));
    CHECK(slowMinDistance < 290.0);
    // endFrame 60 is explicit; the object has not reached the capture radius.
    CHECK(evaluateResult(segment, world, 61, 30).reason
          == StopReason::ExplicitEnd);

    // Rotating the driven entity must not rotate the solver-space velocity.
    segment.startPose.rotation = 90.0;
    segment.initialVelocity = QPointF(0.0, 3000.0);
    const QVector<Sample> fast = evaluate(segment, world, 61, 30);
    CHECK(fast.size() == 61);
    double fastMaxDistance = 0.0;
    for (const Sample &sample : fast)
        fastMaxDistance = std::max(fastMaxDistance,
                                   std::hypot(sample.pose.position.x(),
                                              sample.pose.position.y()));
    CHECK(fastMaxDistance > 1000.0);

    const Sample *fastEarly = atFrame(fast, 1);
    CHECK(fastEarly != nullptr);
    if (fastEarly)
    {
        CHECK_NEAR(fastEarly->velocity.y(), 3000.0, 20.0);
        CHECK_NEAR(fastEarly->velocity.x(), 0.0, 20.0);
    }

    segment.params[QStringLiteral("maxSpeed")] = 500.0;
    const QVector<Sample> limited = evaluate(segment, world, 61, 30);
    CHECK(limited.size() == 61);
    double limitedMaxSpeed = 0.0;
    for (const Sample &sample : limited)
        limitedMaxSpeed = std::max(limitedMaxSpeed,
                                   std::hypot(sample.velocity.x(),
                                              sample.velocity.y()));
    CHECK(limitedMaxSpeed <= 500.0 + 1e-6);
    CHECK(limitedMaxSpeed > 0.0);
}

void testBlackHoleAngularMomentum()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("hole"), Pose{QPointF(0.0, 0.0), 0.0});

    Segment segment;
    segment.effect = SolverEffect::BlackHole;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("hole");
    segment.startFrame = 0;
    segment.endFrame = 60;
    segment.startPose.position = QPointF(300.0, 0.0);
    segment.params[QStringLiteral("mass")] = 5000.0;
    segment.params[QStringLiteral("softening")] = 20.0;
    segment.params[QStringLiteral("objectRadius")] = 5.0;
    segment.params[QStringLiteral("maxSpeed")] = 4000.0;
    segment.params[QStringLiteral("angularMomentum")] = 1.0e6;

    const QVector<Sample> samples = evaluate(segment, world, 61, 30);
    CHECK(samples.size() == 61);
    bool offAxis = false;
    for (const Sample &sample : samples)
        offAxis = offAxis || std::abs(sample.pose.position.y()) > 1.0;
    CHECK(offAxis);
    CHECK(evaluateResult(segment, world, 61, 30).reason
          == StopReason::ExplicitEnd);
}

void testAutomaticOrbitAndSpinRunToClipEnd()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("sun"), Pose{QPointF(0.0, 0.0), 0.0});

    Segment orbit;
    orbit.effect = SolverEffect::Orbit;
    orbit.entityUuid = QStringLiteral("planet");
    orbit.targetUuid = QStringLiteral("sun");
    orbit.startFrame = 5;
    orbit.endFrame = -1;
    orbit.startPose.position = QPointF(100.0, 0.0);
    const Result orbitResult = evaluateResult(orbit, world, 40, 30);
    CHECK(orbitResult.stopFrame == 39);
    CHECK(orbitResult.reason == StopReason::ClipEnd);
    CHECK(orbitResult.samples.size() == 35);
    for (const Sample &sample : orbitResult.samples)
        CHECK(!sample.stopped);

    Segment spin;
    spin.effect = SolverEffect::Spin;
    spin.entityUuid = QStringLiteral("wheel");
    spin.startFrame = 0;
    spin.endFrame = -1;
    const Result spinResult = evaluateResult(spin, world, 20, 30);
    CHECK(spinResult.stopFrame == 19);
    CHECK(spinResult.reason == StopReason::ClipEnd);
    for (const Sample &sample : spinResult.samples)
        CHECK(!sample.stopped);
}

void testBlackHoleCaptureStopsOnHorizon()
{
    MockWorld world;
    world.poses.insert(QStringLiteral("hole"), Pose{QPointF(0.0, 0.0), 0.0});

    Segment segment;
    segment.effect = SolverEffect::BlackHole;
    segment.entityUuid = QStringLiteral("ball");
    segment.targetUuid = QStringLiteral("hole");
    segment.startFrame = 0;
    segment.endFrame = -1;
    segment.startPose.position = QPointF(150.0, 0.0);
    segment.params[QStringLiteral("mass")] = 5000.0;
    segment.params[QStringLiteral("eventHorizonRadius")] = 20.0;
    segment.params[QStringLiteral("objectRadius")] = 5.0;
    segment.params[QStringLiteral("softening")] = 20.0;

    const Result result = evaluateResult(segment, world, 120, 30);
    CHECK(result.samples.size() == 120);
    CHECK(result.reason == StopReason::Collision);
    CHECK(result.stopFrame > 0);
    CHECK(result.stopFrame < 119);
    if (result.stopFrame >= 0)
    {
        const Sample &captured = result.samples.at(result.stopFrame);
        CHECK(captured.collided);
        CHECK(captured.stopped);
        CHECK_NEAR(std::hypot(captured.pose.position.x(),
                              captured.pose.position.y()),
                   25.0, 1e-6);
    }
}

void testEmptyAndSingleFrame()
{
    MockWorld world;
    Segment segment;
    segment.effect = SolverEffect::Gravity;
    segment.entityUuid = QStringLiteral("box");
    segment.targetUuid = QStringLiteral("ground");
    segment.startPose.position = QPointF(5.0, 6.0);

    segment.startFrame = 10;
    segment.endFrame = 5;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    segment.startFrame = 30;
    segment.endFrame = -1;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    segment.startFrame = -1;
    segment.endFrame = -1;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    segment.startFrame = 5;
    segment.endFrame = 5;
    const QVector<Sample> single = evaluate(segment, world, 30, 30);
    CHECK(single.size() == 1);
    if (single.size() == 1)
    {
        CHECK(single.first().frame == 5);
        CHECK_NEAR(single.first().pose.position.x(), 5.0, 1e-12);
        CHECK_NEAR(single.first().pose.position.y(), 6.0, 1e-12);
        CHECK_NEAR(single.first().velocity.x(), 0.0, 1e-12);
        CHECK_NEAR(single.first().velocity.y(), 0.0, 1e-12);
        // A single solved frame is static, not a physical stop.
        CHECK(!single.first().stopped);
    }
    const Result singleResult = evaluateResult(segment, world, 30, 30);
    CHECK(singleResult.stopFrame == 5);
    CHECK(singleResult.reason == StopReason::ExplicitEnd);

    segment.startFrame = 9;
    segment.endFrame = -1;
    const QVector<Sample> lastFrame = evaluate(segment, world, 10, 30);
    CHECK(lastFrame.size() == 1);
    if (lastFrame.size() == 1)
        CHECK(lastFrame.first().frame == 9);

    segment.startFrame = 0;
    segment.endFrame = 999;
    const QVector<Sample> clipped = evaluate(segment, world, 10, 30);
    CHECK(clipped.size() == 10);
    if (!clipped.isEmpty())
        CHECK(clipped.last().frame == 9);

    // Missing targetUuid is invalid for gravity/orbit/blackhole.
    segment.effect = SolverEffect::Gravity;
    segment.startFrame = 0;
    segment.endFrame = 5;
    segment.targetUuid.clear();
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    segment.effect = SolverEffect::Orbit;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    segment.effect = SolverEffect::BlackHole;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    // Missing entityUuid is always invalid.
    segment.entityUuid.clear();
    segment.targetUuid = QStringLiteral("ground");
    segment.effect = SolverEffect::Gravity;
    CHECK(evaluate(segment, world, 30, 30).isEmpty());

    // Spin ignores targetUuid and only needs entityUuid.
    segment.entityUuid = QStringLiteral("box");
    segment.targetUuid.clear();
    segment.effect = SolverEffect::Spin;
    const QVector<Sample> spin = evaluate(segment, world, 30, 30);
    CHECK(spin.size() == 6);
    if (!spin.isEmpty())
        CHECK(!spin.last().stopped);
}

} // namespace

int main()
{
    int testCases = 0;
    const auto run = [&testCases](const char *name, void (*function)())
    {
        ++testCases;
        QTextStream(stdout) << "[" << name << "]\n";
        function();
    };

    run("gravity parabola", testGravityParabola);
    run("gravity direction uses target", testGravityDirectionUsesTarget);
    run("gravity bounce", testGravityBounce);
    run("gravity stop threshold", testGravityStopsOnImpact);
    run("orbit period", testOrbitPeriod);
    run("orbit directions", testOrbitDirections);
    run("spin", testSpin);
    run("black hole", testBlackHole);
    run("black hole angular momentum", testBlackHoleAngularMomentum);
    run("gravity stop threshold without collision",
        testGravityStopThresholdWithoutCollision);
    run("explicit end keeps range", testExplicitEndKeepsRange);
    run("automatic orbit and spin run to clip end",
        testAutomaticOrbitAndSpinRunToClipEnd);
    run("black hole capture stops on horizon",
        testBlackHoleCaptureStopsOnHorizon);
    run("empty and single frame", testEmptyAndSingleFrame);

    if (failures == 0)
    {
        QTextStream(stdout) << testCases << " solver test cases passed\n";
        return 0;
    }
    QTextStream(stderr) << failures << " solver check(s) failed in "
                        << testCases << " test case(s)\n";
    return 1;
}
