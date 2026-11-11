#pragma once

#include "common/Types.h"

#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace pvf::solver {

// A pose in the solver's coordinate space.  That space is the model space:
// origin at the canvas centre, +X right, +Y up, positive rotation
// counter-clockwise.  The solver only drives position and rotation;
// scale/opacity/pivot stay outside of this module.
struct Pose
{
    QPointF position;
    double rotation = 0.0;
};

// Read-only view of the project at a given frame.  Poses and shapes must share
// one coordinate space with Segment::startPose.position (the solver space);
// implementations may bake parent transforms into both.  Non-vector entities
// are expected to return their bounding box; vector entities return the real
// path.
//
// Frame arguments stay in the segment's relative frame space (the same numbers
// as Segment::startFrame / Sample::frame).  A World implementation that maps
// relative frames to project frames should add the driven entity's start offset
// internally.
struct World
{
    virtual ~World() = default;

    virtual Pose poseOf(const Uuid &uuid, int frame) const = 0;
    virtual QPainterPath shapeOf(const Uuid &uuid, int frame) const = 0;
    virtual bool physicsInteract(const Uuid &uuid, int frame) const = 0;

    // Return every entity that may participate in collision at `frame`.
    // evaluate() filters the list with physicsInteract(), excludes
    // Segment::entityUuid, and always considers Segment::targetUuid when it is
    // physics-interactive.  Implementations that cannot enumerate entities can
    // leave this empty and instead pass params["collisionUuids"] on a gravity
    // segment.
    virtual QVector<Uuid> physicsEntities(int frame) const
    {
        (void)frame;
        return {};
    }
};

// Solver segments use frames relative to the driven entity's start, matching
// the project model.  endFrame == -1 means "until the last clip frame".
struct Segment
{
    SolverEffect effect = SolverEffect::Gravity;
    Uuid entityUuid;        // driven moving entity; gravity uses its shape and excludes it from collisions
    Uuid targetUuid;        // gravity: ground/direction entity; orbit/blackhole: center; spin ignores
    int startFrame = 0;
    int endFrame = -1;
    Pose startPose;
    QPointF initialVelocity; // solver space, px/second
    QVariantMap params;
};

struct Sample
{
    int frame = 0;
    Pose pose;
    // Solver-space velocity in pixels/second.  Gravity, orbit and black-hole
    // samples carry it; spin samples report zero.
    QPointF velocity;
    bool collided = false;
    bool stopped = false;
};

// Evaluate a segment on a fixed 1/fps grid from startFrame through endFrame
// (inclusive).  entityUuid is required; targetUuid is required for gravity,
// orbit and black-hole (spin ignores it).  Missing required UUIDs return an
// empty vector.  The result is deterministic and contains one sample per frame;
// an invalid/empty range also returns an empty vector.  The last sample always
// has zero velocity and stopped == true ("value holds after the segment").
//
// Parameter keys:
//   Gravity:   strength (px/s^2, default 980), elasticity (0..1, default 0.35),
//              friction (0..1, default 0.2), stopThreshold (px/s, default 2),
//              collisionUuids (QStringList fallback when World::physicsEntities
//              is not implemented).  Acceleration points along the target's
//              local -Y (screen down) and rotates with the target.
//   Orbit:     aspect (minor/major, 0.01..1, default 1), angle (degrees,
//              counter-clockwise, relative to targetUuid's local rotation),
//              direction (+1 clockwise / -1 counter-clockwise),
//              periodFrames (frames per revolution, default 60).  The ellipse
//              passes through startPose.position; semi-axes are derived from
//              that and centered on targetUuid.
//   Spin:      angularVelocity (degrees/frame, default 6), direction (+1
//              clockwise / -1 counter-clockwise).  Output rotation follows the
//              model convention: positive is counter-clockwise.
//   BlackHole: simulationSpeed (time multiplier, default 1), mass (relative,
//              default 1000), angularMomentum (frame-dragging, default 0),
//              eventHorizonRadius (informational, not a hard boundary, default
//              80), objectRadius (default 40), softening (px, default 60),
//              maxSpeed (px/s, default 4000, 0 = unlimited).  Center is
//              targetUuid.
// initialVelocity and Sample::velocity are solver-space px/second; no local
// axis conversion is applied.
QVector<Sample> evaluate(const Segment &segment, const World &world,
                         int clipDuration, int fps);

} // namespace pvf::solver
