#include "Anim.h"

#include <QSet>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>

namespace pvf::anim {
namespace {

constexpr QChar kKeySeparator(0x1f);

std::atomic<int> g_solverEvaluations{0};

// Range end without solving physics: the model's structural end (explicit
// endFrame, or one frame before the next key/segment/clip end).  Used by
// solverAt() (no World available) and as the fallback when a segment cannot
// be evaluated.
int structuralEnd(const model::Entity& entity, const model::SolverSegment& segment)
{
    return model::solverStructuralEnd(entity, segment);
}

bool drivesPosition(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
    case SolverEffect::Orbit:
    case SolverEffect::BlackHole:
    case SolverEffect::Spiral:
        return true;
    case SolverEffect::Spin:
        return false;
    }
    return false;
}

bool drivesRotation(SolverEffect effect)
{
    return effect == SolverEffect::Spin;
}

QString joinKey(const Uuid& uuid, double frame)
{
    return uuid + kKeySeparator + QString::number(frame, 'g', 17);
}

struct LocalValues
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
    double opacity = 1.0;
    bool visible = true;
};

// Solver-free values, used for cycle guards and re-entrancy fallbacks.
LocalValues plainValues(const model::Entity& entity, double relativeFrame)
{
    const model::KeyframeSample sample =
        model::entitySampleAt(entity, relativeFrame);
    LocalValues values;
    values.x = sample.x;
    values.y = sample.y;
    values.rotation = sample.rotation;
    values.scaleX = sample.scaleX;
    values.scaleY = sample.scaleY;
    // Pivot is a definition parameter; it is never keyframed.
    values.pivotX = entity.transform.pivotX;
    values.pivotY = entity.transform.pivotY;
    values.opacity = sample.opacity;
    // `hidden` is an editor-only flag: it affects the workbench canvas and the
    // timeline, never rendering or export.  Only the whole-frame `visible`
    // value decides.
    values.visible = sample.visible;
    return values;
}

// Model space is Y-up with the origin at the canvas centre.  Qt's rotate()
// uses the standard mathematical matrix, so a positive angle turns +X towards
// +Y, i.e. counter-clockwise on screen.  Poses stay entirely in model space;
// the renderer performs the one and only world -> screen Y flip when it maps
// to the output image (content-local Y-down is flipped separately at paint
// time).  Never bake a screen flip into this matrix.
Pose composePose(const LocalValues& values, bool inheritVisibility,
                 const Pose* parent, bool inheritTransform = true)
{
    QTransform local;
    local.translate(values.x, values.y);
    local.translate(values.pivotX, values.pivotY);
    local.rotate(values.rotation);
    local.scale(values.scaleX, values.scaleY);
    local.translate(-values.pivotX, -values.pivotY);

    Pose pose;
    // QTransform's operator* applies the left operand first.  Hierarchy is
    // child-local -> parent, so the product must be local * parent.world.
    // Canvas-space entities keep the local matrix: the parent never moves
    // them, but opacity/visibility still follow the normal inheritance rules.
    if (parent && inheritTransform)
        pose.world = local * parent->world;
    else
        pose.world = local;
    pose.opacity = parent ? parent->opacity * values.opacity : values.opacity;
    pose.visible = values.visible;
    if (parent && !parent->visible && inheritVisibility)
        pose.visible = false;
    return pose;
}

Pose plainPose(const model::Entity& entity, double projectFrame)
{
    return composePose(plainValues(entity, projectFrame - entity.start),
                       entity.inheritVisibility, nullptr);
}

double transformRotation(const QTransform& transform)
{
    return qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
}

bool hasRequiredTarget(const model::Project& project,
                       const model::SolverSegment& segment)
{
    if (segment.effect == SolverEffect::Spin)
        return true;
    const Uuid target = segment.params.value(QStringLiteral("targetUuid")).toString();
    return !target.isEmpty() && project.entities.contains(target);
}

solver::Pose startPoseAt(const model::Entity& entity, int relativeFrame)
{
    const model::KeyframeSample sample =
        model::entitySampleAt(entity, relativeFrame);
    solver::Pose pose;
    pose.position = QPointF(sample.x, sample.y);
    pose.rotation = sample.rotation;
    return pose;
}

// Breaks solver -> World -> evaluateEntity recursion when a World
// implementation evaluates entities through this module.
thread_local QSet<QString> gActiveEvaluations;

// Guards solver -> World -> evaluateEntity recursion.  The key deliberately
// omits the frame: a solver sampling its own entity asks for shape/pose at many
// frames, and evaluating each one would restart the solver (frame 59 -> 58 ->
// ... -> exponential).  Any re-entry for the same entity returns a solver-free
// plain pose instead.
QString evaluationKey(const model::Project& project, const Uuid& uuid)
{
    return QString::number(reinterpret_cast<quintptr>(&project)) + kKeySeparator
           + uuid;
}

class ActiveEvaluationGuard
{
public:
    explicit ActiveEvaluationGuard(QString key)
        : m_key(std::move(key))
    {
        gActiveEvaluations.insert(m_key);
    }

    ~ActiveEvaluationGuard() { gActiveEvaluations.remove(m_key); }

private:
    QString m_key;
};

using ParentWorldResolver =
    std::function<QTransform(const model::Entity&, int)>;

class SolverWorldAdapter final : public solver::World
{
public:
    SolverWorldAdapter(const model::Entity& driven, const anim::World& world,
                       ParentWorldResolver parentWorld)
        : m_driven(driven)
        , m_world(world)
        , m_parentWorld(std::move(parentWorld))
    {
    }

    solver::Pose poseOf(const Uuid& uuid, int frame) const override;
    QPainterPath shapeOf(const Uuid& uuid, int frame) const override;
    bool physicsInteract(const Uuid& uuid, int frame) const override;
    QVector<Uuid> physicsEntities(int frame) const override;

private:
    int projectFrameOf(int frame) const { return frame + m_driven.start; }
    QTransform parentWorldOf(int projectFrame) const;

    const model::Entity& m_driven;
    const anim::World& m_world;
    ParentWorldResolver m_parentWorld;
};

// Shared implementation behind anim::solverResultFor() and
// EvalContext::resultFor().  `parentWorld` resolves the driven entity's parent
// world at a project frame; the evaluator passes its visiting-set-aware
// resolver, while top-level callers get the public evaluateEntity() path.
const solver::Result* solverResultForImpl(
    const model::Project& project, const model::Entity& entity,
    const model::SolverSegment& segment, const World& world, Cache& cache,
    const ParentWorldResolver& parentWorld)
{
    const QString key = solverResultKey(entity, segment);
    QHash<QString, solver::Result>* shared = cache.sharedSolverResults;
    if (shared)
    {
        const auto hit = shared->constFind(key);
        if (hit != shared->constEnd())
            return &hit.value();
    }
    const auto cached = cache.solverResults.constFind(key);
    if (cached != cache.solverResults.constEnd())
        return &cached.value();

    // A locked cache is exactly the playback contract: lookup only, never
    // solve.  Callers fall back to ordinary tracks for missing segments.
    if (cache.solverResultsLocked)
        return nullptr;

    solver::Result result;
    if (entity.duration() > 0 && hasRequiredTarget(project, segment))
    {
        solver::Segment request;
        request.effect = segment.effect;
        request.entityUuid = entity.uuid;
        request.targetUuid = segment.params.value(QStringLiteral("targetUuid"))
                                 .toString();
        request.startFrame = segment.startFrame;
        request.endFrame = segment.endFrame;
        request.startPose = startPoseAt(entity, segment.startFrame);
        request.initialVelocity = segment.initialVelocity;
        request.params = segment.params;

        // A top-level call is not inside evaluateEntity(), so it does not get
        // the self-recursion guard from the outer evaluation.  Install the
        // same guard here; a nested call already owns it and must not remove
        // the outer key on return.
        const QString guardKey = evaluationKey(project, entity.uuid);
        const bool ownsGuard = !gActiveEvaluations.contains(guardKey);
        std::optional<ActiveEvaluationGuard> guard;
        if (ownsGuard)
            guard.emplace(guardKey);

        ++g_solverEvaluations;
        SolverWorldAdapter adapter(entity, world, parentWorld);
        result = solver::evaluateResult(request, adapter, entity.duration(),
                                        project.settings.fps);
    }

    const auto inserted = cache.solverResults.insert(key, result);
    if (shared && shared != &cache.solverResults)
        shared->insert(key, result);
    return &inserted.value();
}

class EvalContext
{
public:
    EvalContext(const model::Project& project, const anim::World& world, Cache& cache)
        : m_project(project), m_world(world), m_cache(cache)
    {
    }

    Pose evaluate(const Uuid& uuid, double projectFrame);
    QTransform parentWorldOf(const model::Entity& entity, double projectFrame);

private:
    Pose localPose(const model::Entity& entity, double projectFrame,
                   const Pose* parent);
    double drivenValue(const model::Entity& entity, const QString& track,
                       double relativeFrame, double fallback);
    // Solver-driven uniform scale at this frame (1 = unchanged).  Only the
    // black hole's optional capture shrink produces a value below 1.
    double drivenScale(const model::Entity& entity, double relativeFrame);

    const model::SolverSegment* activeSegment(const model::Entity& entity,
                                              double relativeFrame,
                                              bool rotationTrack);
    const model::SolverSegment* latestEndedSegment(const model::Entity& entity,
                                                   double relativeFrame,
                                                   bool rotationTrack);

    // Effective end of the segment: the physical stop frame for automatic
    // segments, the explicit endFrame otherwise.  Falls back to the clip end
    // when the segment cannot be solved (missing target, empty solve).
    int endFrameFor(const model::Entity& entity,
                    const model::SolverSegment& segment);

    std::optional<double> anchoredValue(const model::Entity& entity,
                                        const QString& track,
                                        double relativeFrame,
                                        const model::SolverSegment& segment,
                                        double fallback);
    std::optional<solver::Sample> sampleAt(const model::Entity& entity,
                                           const model::SolverSegment& segment,
                                           double relativeFrame);
    const solver::Result* resultFor(const model::Entity& entity,
                                    const model::SolverSegment& segment);

    const model::Project& m_project;
    const anim::World& m_world;
    Cache& m_cache;
    QSet<QString> m_visiting;
};

solver::Pose SolverWorldAdapter::poseOf(const Uuid& uuid, int frame) const
{
    const int projectFrame = projectFrameOf(frame);
    const solver::Pose raw = m_world.poseOf(uuid, projectFrame);
    const QTransform parent = parentWorldOf(projectFrame);
    if (parent.isIdentity())
        return raw;

    bool invertible = false;
    const QTransform inverse = parent.inverted(&invertible);
    if (!invertible)
        return raw;
    return solver::Pose{inverse.map(raw.position),
                        raw.rotation - transformRotation(parent)};
}

QPainterPath SolverWorldAdapter::shapeOf(const Uuid& uuid, int frame) const
{
    const int projectFrame = projectFrameOf(frame);
    const QPainterPath path = m_world.shapeOf(uuid, projectFrame);
    const QTransform parent = parentWorldOf(projectFrame);
    if (parent.isIdentity())
        return path;

    bool invertible = false;
    const QTransform inverse = parent.inverted(&invertible);
    return invertible ? inverse.map(path) : path;
}

bool SolverWorldAdapter::physicsInteract(const Uuid& uuid, int frame) const
{
    return m_world.physicsInteract(uuid, projectFrameOf(frame));
}

QVector<Uuid> SolverWorldAdapter::physicsEntities(int frame) const
{
    return m_world.physicsEntities(projectFrameOf(frame));
}

QTransform SolverWorldAdapter::parentWorldOf(int projectFrame) const
{
    // A canvas-space entity is solved in project canvas coordinates, so the
    // target poses and shapes must not be pushed through the parent transform.
    // Parent-space entities keep the historical behaviour: the solver works in
    // the driven entity's parent space and startPoseAt() reads parent-local
    // x/y/rotation.
    if (m_driven.space == model::EntitySpace::Canvas)
        return QTransform();
    return m_parentWorld(m_driven, projectFrame);
}

QTransform EvalContext::parentWorldOf(const model::Entity& entity,
                                      double projectFrame)
{
    if (entity.parentUuid.isEmpty()
        || !m_project.entities.contains(entity.parentUuid))
        return QTransform();
    return evaluate(entity.parentUuid, projectFrame).world;
}

Pose EvalContext::evaluate(const Uuid& uuid, double projectFrame)
{
    const auto entityIt = m_project.entities.constFind(uuid);
    if (entityIt == m_project.entities.constEnd())
        return Pose{};

    const auto cachedEntity = m_cache.poses.constFind(uuid);
    if (cachedEntity != m_cache.poses.constEnd())
    {
        const QHash<double, Pose>& frames = cachedEntity.value();
        const auto cachedFrame = frames.constFind(projectFrame);
        if (cachedFrame != frames.constEnd())
            return cachedFrame.value();
    }

    const QString key = joinKey(uuid, projectFrame);
    if (m_visiting.contains(key))
    {
        // Parent cycle: this node falls back to a root under itself.
        return composePose(plainValues(entityIt.value(),
                                       projectFrame - entityIt->start),
                           entityIt->inheritVisibility, nullptr);
    }

    m_visiting.insert(key);

    Pose parentPose;
    bool hasParent = false;
    const Uuid parentUuid = entityIt->parentUuid;
    if (!parentUuid.isEmpty() && m_project.entities.contains(parentUuid)
        && !m_visiting.contains(joinKey(parentUuid, projectFrame)))
    {
        parentPose = evaluate(parentUuid, projectFrame);
        hasParent = true;
    }

    const Pose pose = localPose(entityIt.value(), projectFrame,
                               hasParent ? &parentPose : nullptr);
    m_visiting.remove(key);

    QHash<double, Pose>& frames = m_cache.poses[uuid];
    frames.insert(projectFrame, pose);
    const int limit = m_cache.maxPosesPerEntity;
    if (limit > 0 && frames.size() > limit)
    {
        // Bounded playback cache: drop the sampled frame farthest from the
        // one just evaluated, so a long play keeps a local window (#132).
        auto farthest = frames.begin();
        double farthestDistance = -1.0;
        for (auto it = frames.begin(); it != frames.end(); ++it)
        {
            if (it.key() == projectFrame)
                continue;
            const double distance = std::abs(it.key() - projectFrame);
            if (distance > farthestDistance)
            {
                farthestDistance = distance;
                farthest = it;
            }
        }
        if (farthest != frames.end() && farthest.key() != projectFrame)
            frames.erase(farthest);
    }
    return pose;
}

Pose EvalContext::localPose(const model::Entity& entity, double projectFrame,
                            const Pose* parent)
{
    const double relativeFrame = projectFrame - entity.start;
    const model::KeyframeSample sample =
        model::entitySampleAt(entity, relativeFrame);

    LocalValues values;
    values.x = drivenValue(entity, QStringLiteral("x"), relativeFrame, sample.x);
    values.y = drivenValue(entity, QStringLiteral("y"), relativeFrame, sample.y);
    values.rotation = drivenValue(entity, QStringLiteral("rotation"),
                                  relativeFrame, sample.rotation);
    values.scaleX = sample.scaleX;
    values.scaleY = sample.scaleY;
    // A black-hole capture shrink multiplies the authored scale instead of
    // replacing it, so the entity keeps its own size when the option is off
    // (issue #52).
    const double solverScale = drivenScale(entity, relativeFrame);
    values.scaleX *= solverScale;
    values.scaleY *= solverScale;
    // Pivot is a definition parameter; it is never keyframed.
    values.pivotX = entity.transform.pivotX;
    values.pivotY = entity.transform.pivotY;
    values.opacity = sample.opacity;
    values.visible = sample.visible;

    return composePose(values, entity.inheritVisibility, parent,
                       entity.space != model::EntitySpace::Canvas);
}

double EvalContext::drivenValue(const model::Entity& entity,
                                const QString& track, double relativeFrame,
                                double fallback)
{
    const bool rotationTrack = track == QStringLiteral("rotation");
    if (const model::SolverSegment* segment = activeSegment(
            entity, relativeFrame, rotationTrack))
    {
        if (const auto sample = sampleAt(entity, *segment, relativeFrame))
        {
            if (track == QStringLiteral("x"))
                return sample->pose.position.x();
            if (track == QStringLiteral("y"))
                return sample->pose.position.y();
            return sample->pose.rotation;
        }
        // Missing target / empty solve: behave as if the segment did not exist.
        return fallback;
    }

    if (const model::SolverSegment* segment = latestEndedSegment(
            entity, relativeFrame, rotationTrack))
    {
        // The solved end pose is an implicit left key; interpolate from it to
        // the first ordinary key after the segment instead of jumping back.
        if (const auto value = anchoredValue(entity, track, relativeFrame,
                                             *segment, fallback))
            return *value;
    }

    return fallback;
}

double EvalContext::drivenScale(const model::Entity& entity,
                                double relativeFrame)
{
    // Only a black-hole segment with captureShrink changes the entity scale;
    // every other effect leaves the authored scale untouched (issue #52).
    const auto scaleOf = [this, &entity](
                             const model::SolverSegment& segment,
                             double frame) -> std::optional<double> {
        if (segment.effect != SolverEffect::BlackHole)
            return std::nullopt;
        const auto sample = sampleAt(entity, segment, frame);
        if (!sample || !std::isfinite(sample->pose.scale))
            return std::nullopt;
        return std::clamp(sample->pose.scale, 0.0, 1.0);
    };

    if (const model::SolverSegment* segment =
            activeSegment(entity, relativeFrame, false))
    {
        if (const auto value = scaleOf(*segment, relativeFrame))
            return *value;
        return 1.0;
    }
    // After a capture the entity is gone: hold the end scale instead of
    // snapping back to the authored size.
    if (const model::SolverSegment* segment =
            latestEndedSegment(entity, relativeFrame, false))
    {
        const int end = endFrameFor(entity, *segment);
        if (end >= segment->startFrame)
        {
            if (const auto value = scaleOf(*segment, double(end)))
                return *value;
        }
    }
    return 1.0;
}

const model::SolverSegment* EvalContext::activeSegment(
    const model::Entity& entity, double relativeFrame, bool rotationTrack)
{
    if (entity.duration() <= 0)
        return nullptr;

    const model::SolverSegment* best = nullptr;
    for (const model::SolverSegment& segment : entity.solvers)
    {
        const bool drives = rotationTrack ? drivesRotation(segment.effect)
                                          : drivesPosition(segment.effect);
        if (!drives || segment.startFrame < 0
            || segment.startFrame >= entity.duration())
            continue;
        if (relativeFrame < segment.startFrame)
            continue;

        const int end = endFrameFor(entity, segment);
        if (end < segment.startFrame || relativeFrame > end)
            continue;

        if (!best || segment.startFrame > best->startFrame)
            best = &segment;
    }
    return best;
}

const model::SolverSegment* EvalContext::latestEndedSegment(
    const model::Entity& entity, double relativeFrame, bool rotationTrack)
{
    if (entity.duration() <= 0)
        return nullptr;

    const model::SolverSegment* best = nullptr;
    for (const model::SolverSegment& segment : entity.solvers)
    {
        const bool drives = rotationTrack ? drivesRotation(segment.effect)
                                          : drivesPosition(segment.effect);
        if (!drives || segment.startFrame < 0
            || segment.startFrame >= entity.duration())
            continue;
        // A segment that starts at or after this frame cannot have ended yet;
        // skip it before solving (automatic ends need a full evaluation).
        if (segment.startFrame >= relativeFrame)
            continue;

        const int end = endFrameFor(entity, segment);
        if (end < segment.startFrame || end >= relativeFrame)
            continue;

        if (!best || segment.startFrame > best->startFrame)
            best = &segment;
    }
    return best;
}

int EvalContext::endFrameFor(const model::Entity& entity,
                             const model::SolverSegment& segment)
{
    const int lastFrame = entity.duration() - 1;
    if (lastFrame < 0)
        return -1;
    if (segment.endFrame >= 0)
        return std::min(segment.endFrame, lastFrame);

    // The structural end follows the first key created after the solver
    // start; the physical stop can only make the segment shorter.
    int end = model::solverStructuralEnd(entity, segment);
    const solver::Result* result = resultFor(entity, segment);
    if (result && result->stopFrame >= segment.startFrame)
        end = std::min(end, result->stopFrame);
    return end;
}

std::optional<double> EvalContext::anchoredValue(
    const model::Entity& entity, const QString& track, double relativeFrame,
    const model::SolverSegment& segment, double fallback)
{
    const int end = endFrameFor(entity, segment);
    if (end < segment.startFrame)
        return std::nullopt;

    const auto sample = sampleAt(entity, segment, end);
    if (!sample)
        return std::nullopt;

    double anchor = 0.0;
    if (track == QStringLiteral("x"))
        anchor = sample->pose.position.x();
    else if (track == QStringLiteral("y"))
        anchor = sample->pose.position.y();
    else
        anchor = sample->pose.rotation;

    // The first whole-frame key after the solver end is the interpolation
    // target.
    const model::Keyframe* next = nullptr;
    for (const model::Keyframe& key : entity.keyframes)
    {
        if (key.frame > end)
        {
            next = &key;
            break;
        }
    }
    if (!next)
        return anchor;
    if (relativeFrame >= next->frame)
        return std::nullopt; // ordinary interpolation takes over from here

    double target = fallback;
    if (track == QStringLiteral("x"))
        target = next->x;
    else if (track == QStringLiteral("y"))
        target = next->y;
    else
        target = next->rotation;

    const int span = next->frame - end;
    if (span <= 0)
        return target;

    // The synthetic anchor has no easing of its own; use a linear ramp.
    const double t = double(relativeFrame - end) / double(span);
    return anchor + (target - anchor) * t;
}

std::optional<solver::Sample> EvalContext::sampleAt(
    const model::Entity& entity, const model::SolverSegment& segment,
    double relativeFrame)
{
    const solver::Result* result = resultFor(entity, segment);
    if (!result || result->samples.isEmpty())
        return std::nullopt;
    const QVector<solver::Sample>& samples = result->samples;

    // Solver samples live on integer frames.  Fractional project frames
    // interpolate the two neighbouring samples (position and rotation only;
    // velocity/collision flags keep the left sample).
    const double local = relativeFrame - double(segment.startFrame);
    if (local < 0.0 || local > double(samples.size() - 1))
        return std::nullopt;

    const int lower = int(std::floor(local));
    const int upper = int(std::ceil(local));
    const solver::Sample& left = samples.at(lower);
    if (upper == lower)
        return left;

    const solver::Sample& right = samples.at(upper);
    solver::Sample mixed = left;
    const double t = local - double(lower);
    mixed.pose.position =
        left.pose.position + (right.pose.position - left.pose.position) * t;
    mixed.pose.rotation =
        left.pose.rotation + (right.pose.rotation - left.pose.rotation) * t;
    mixed.pose.scale =
        left.pose.scale + (right.pose.scale - left.pose.scale) * t;
    return mixed;
}

const solver::Result* EvalContext::resultFor(
    const model::Entity& entity, const model::SolverSegment& segment)
{
    return solverResultForImpl(
        m_project, entity, segment, m_world, m_cache,
        [this](const model::Entity& driven, int projectFrame) {
            return parentWorldOf(driven, projectFrame);
        });
}

} // namespace

const solver::Result* solverResultFor(const model::Project& project,
                                      const model::Entity& entity,
                                      const model::SolverSegment& segment,
                                      const World& world, Cache& cache)
{
    return solverResultForImpl(
        project, entity, segment, world, cache,
        [&project, &world, &cache](const model::Entity& driven, int frame) {
            if (driven.parentUuid.isEmpty()
                || !project.entities.contains(driven.parentUuid))
                return QTransform();
            return evaluateEntity(project, driven.parentUuid, frame, world, cache)
                .world;
        });
}

const solver::Result* cachedSolverResultFor(const model::Entity& entity,
                                            const model::SolverSegment& segment,
                                            const Cache& cache)
{
    const QString key = solverResultKey(entity, segment);
    if (cache.sharedSolverResults)
    {
        const auto hit = cache.sharedSolverResults->constFind(key);
        if (hit != cache.sharedSolverResults->constEnd())
            return &hit.value();
    }
    const auto cached = cache.solverResults.constFind(key);
    return cached == cache.solverResults.constEnd() ? nullptr : &cached.value();
}

QString solverResultKey(const model::Entity& entity,
                        const model::SolverSegment& segment)
{
    QString id = segment.id;
    if (id.isEmpty())
        id = QStringLiteral("anon-%1").arg(segment.startFrame);
    return entity.uuid + kKeySeparator + id + kKeySeparator
           + QString::number(segment.startFrame) + kKeySeparator
           + QString::number(segment.endFrame) + kKeySeparator
           + QString::number(static_cast<int>(segment.effect)) + kKeySeparator
           + QString::number(static_cast<int>(entity.space));
}

int solverEvaluationCount()
{
    return g_solverEvaluations.load(std::memory_order_relaxed);
}

void Cache::clear()
{
    // The borrowed sharedSolverResults map belongs to its owner and is never
    // cleared here.
    poses.clear();
    solverResults.clear();
}

void Cache::clearEntity(const Uuid& uuid)
{
    poses.remove(uuid);
    const QString prefix = uuid + kKeySeparator;
    for (auto it = solverResults.begin(); it != solverResults.end();)
    {
        if (it.key().startsWith(prefix))
            it = solverResults.erase(it);
        else
            ++it;
    }
}

const model::SolverSegment* solverAt(const model::Entity& entity,
                                     int entityRelativeFrame)
{
    if (entity.duration() <= 0)
        return nullptr;

    const model::SolverSegment* best = nullptr;
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (segment.startFrame < 0 || segment.startFrame >= entity.duration())
            continue;

        const int end = structuralEnd(entity, segment);
        if (end < segment.startFrame || entityRelativeFrame < segment.startFrame
            || entityRelativeFrame > end)
            continue;

        if (!best || segment.startFrame > best->startFrame)
            best = &segment;
    }
    return best;
}

bool physicsInteractAt(const model::Project& project, const Uuid& entityUuid,
                       int projectFrame)
{
    const auto it = project.entities.constFind(entityUuid);
    if (it == project.entities.constEnd())
        return false;

    const model::Entity& entity = it.value();
    return model::entitySampleAt(entity, projectFrame - entity.start)
        .physicsInteract;
}

Pose evaluateEntity(const model::Project& project, const Uuid& entityUuid,
                    int projectFrame, const World& world)
{
    return evaluateEntity(project, entityUuid, double(projectFrame), world);
}

Pose evaluateEntity(const model::Project& project, const Uuid& entityUuid,
                    double projectFrame, const World& world)
{
    Cache cache;
    return evaluateEntity(project, entityUuid, projectFrame, world, cache);
}

Pose evaluateEntity(const model::Project& project, const Uuid& entityUuid,
                    int projectFrame, const World& world, Cache& cache)
{
    return evaluateEntity(project, entityUuid, double(projectFrame), world,
                          cache);
}

Pose evaluateEntity(const model::Project& project, const Uuid& entityUuid,
                    double projectFrame, const World& world, Cache& cache)
{
    const auto it = project.entities.constFind(entityUuid);
    if (it == project.entities.constEnd())
        return Pose{};

    const QString key = evaluationKey(project, entityUuid);
    if (gActiveEvaluations.contains(key))
    {
        // A World callback asked us to evaluate the entity that is currently
        // being solved.  Return a solver-free local pose instead of recursing.
        return plainPose(it.value(), projectFrame);
    }

    const ActiveEvaluationGuard guard(key);
    EvalContext context(project, world, cache);
    return context.evaluate(entityUuid, projectFrame);
}

} // namespace pvf::anim
