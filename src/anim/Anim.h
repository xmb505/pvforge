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
    // Render visibility: the step-evaluated `visible` track plus inherited
    // parent visibility.  `hidden` is editor-only and never enters this value.
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
    // Internal storage, exposed for simple lifetime management.  Frames are
    // doubles so fractional export sampling (2x/4x project fps) can be cached
    // next to integer project frames.
    QHash<Uuid, QHash<double, Pose>> poses; // entity uuid -> project frame -> pose
    QHash<QString, solver::Result> solverResults; // segment signature

    // Optional long-lived store owned by EditorSession.  When set, a result is
    // looked up here before the local map and every newly solved segment is
    // written to both.  Poses stay local: only solver results are shared, so a
    // playback cache does not grow with the number of painted frames.  The
    // owner must clear it after any project edit; clear()/clearEntity() here
    // never touch it.
    QHash<QString, solver::Result> *sharedSolverResults = nullptr;

    // While set, a missing segment is never solved on the calling path: the
    // evaluator falls back to ordinary tracks and solverResultFor() returns
    // nullptr.  Playback sets this so a cold cache can never stall a frame.
    bool solverResultsLocked = false;

    void clear();
    void clearEntity(const Uuid &uuid);
};

// Returns the cached result for one solver segment, solving the whole segment
// exactly once on a miss.  A hit checks the shared store first, then this
// cache.  Returns nullptr when the cache is locked and the segment is missing.
// The pointer stays valid until the cache is cleared or a result is inserted.
const solver::Result *solverResultFor(const model::Project &project,
                                      const model::Entity &entity,
                                      const model::SolverSegment &segment,
                                      const World &world, Cache &cache);

// Lookup only: never solves and never inserts.  Read-only previews use this so
// they can share solved samples without seeding the cache from another World.
const solver::Result *cachedSolverResultFor(const model::Entity &entity,
                                            const model::SolverSegment &segment,
                                            const Cache &cache);

// Stable key for one solved segment: entity id + segment id + start/end frame
// + effect.  Shared caches and result lookups must build the same key.
QString solverResultKey(const model::Entity &entity,
                        const model::SolverSegment &segment);

// Diagnostic counter: number of real solver::evaluateResult() calls in this
// process.  A shared-cache hit does not increment it, so tests can assert that
// playback only solves each segment once.
int solverEvaluationCount();

// Returns the solver segment structurally covering entityRelativeFrame, or
// nullptr.  The covered range is [startFrame, endFrame] with endFrame == -1
// treated as the last frame of the entity clip.  Frames are relative to
// entity.start.  When invalid data overlaps, the segment with the latest start
// frame wins.  The pointer is owned by entity.solvers and is invalidated when
// that vector detaches or changes.
//
// This helper has no World, so it cannot know where a physics segment really
// stops.  Use solver::evaluateResult()/Result::stopFrame (or evaluateEntity,
// which already honours it) for the physical end; timeline code should draw
// the band up to that stop frame.
const model::SolverSegment *solverAt(const model::Entity &entity,
                                     int entityRelativeFrame);

// Step-evaluated physicsInteract value (0/1), independent of visibility.
bool physicsInteractAt(const model::Project &project, const Uuid &entityUuid,
                       int projectFrame);

// Solver segments drive x/y (gravity, orbit, black hole) or rotation (spin).
// Every other track stays on ordinary keyframes.  With endFrame == -1 the
// evaluator uses the first physical stop/collision (or the clip end when the
// motion never stops); endFrame >= 0 keeps the explicit range.  After a
// segment ends, its solved stop pose acts as an implicit left key so the next
// ordinary key does not jump back to the pre-segment value.
//
// The double overload samples at fractional project frames (export 2x/4x fps).
// Ordinary tracks interpolate between keys; inside a solver segment the
// neighbouring integer solver samples are interpolated (position and rotation).
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    double projectFrame, const World &world);
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    int projectFrame, const World &world);

// Same evaluation with a caller-owned cache.  Use this in render loops.
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    double projectFrame, const World &world, Cache &cache);
Pose evaluateEntity(const model::Project &project, const Uuid &entityUuid,
                    int projectFrame, const World &world, Cache &cache);

} // namespace pvf::anim
