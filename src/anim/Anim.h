#pragma once

#include "common/Types.h"
#include "model/Model.h"
#include "solver/Solver.h"

#include <QHash>
#include <QPainterPath>
#include <QTransform>
#include <QVector>

namespace pvf::anim {

// Final pose of one entity at one project frame.  `world` maps the entity's
// local content coordinates into project canvas coordinates and already
// includes every parent transform.
struct Pose
{
    QTransform world;
    double opacity = 1.0;
    bool visible = true;
};

// Read-only project view used while solving physics segments.  Unlike
// solver::World these callbacks always receive project frames, never entity or
// segment relative frames.  Poses and shapes are in project canvas space; the
// anim solver adapter converts them into the driven entity's parent space so
// they share Segment::startPose's coordinate system.
struct World
{
    virtual ~World() = default;

    virtual solver::Pose poseOf(const Uuid &uuid, int projectFrame) const = 0;
    virtual QPainterPath shapeOf(const Uuid &uuid, int projectFrame) const = 0;
    virtual bool physicsInteract(const Uuid &uuid, int projectFrame) const = 0;
    virtual QVector<Uuid> physicsEntities(int projectFrame) const = 0;
};

// Optional memoisation.  Clear it whenever the project or the World content
// changes.  Within one call the evaluator always uses a local cache, so
// repeated parents are never evaluated exponentially.  clearEntity() drops
// only that entity's direct entries; use clear() after a parent edit.
struct Cache
{
    // Internal storage, exposed for simple lifetime management.
    QHash<Uuid, QHash<int, Pose>> poses; // entity uuid -> project frame -> pose
    QHash<QString, QVector<solver::Sample>> solverSamples; // segment signature

    void clear();
    void clearEntity(const Uuid &uuid);
};

// Returns the solver segment covering entityRelativeFrame, or nullptr.  A
// segment endFrame of -1 means the last frame of the entity clip.  Frames are
// relative to entity.start.  When invalid data overlaps, the segment with the
// latest start frame wins.  The pointer is owned by entity.solvers and is
// invalidated when that vector detaches or changes.
const model::SolverSegment *solverAt(const model::Entity &entity,
                                     int entityRelativeFrame);

// Step-evaluated physicsInteract value (0/1), independent of visibility.
bool physicsInteractAt(const model::Project &project, const Uuid &entityUuid,
                       int projectFrame);

// Solver segments drive x/y (gravity, orbit, black hole) or rotation (spin).
// Every other track stays on ordinary keyframes.  After a segment ends, its
// solved end pose acts as an implicit left key so the next ordinary key does
// not jump back to the pre-segment value.
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    int projectFrame, const World &world);

// Same evaluation with a caller-owned cache.  Use this in render loops.
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    int projectFrame, const World &world, Cache &cache);

} // namespace pvf::anim
