#include "Anim.h"

#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>

namespace pvf::anim {
namespace {

constexpr QChar kKeySeparator(0x1f);

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
    return effect != SolverEffect::Spin;
}

bool drivesRotation(SolverEffect effect)
{
    return effect == SolverEffect::Spin;
}

QString joinKey(const Uuid& uuid, double frame)
{
    return uuid + kKeySeparator + QString::number(frame, 'g', 17);
}

QString segmentKey(const model::Entity& entity, const model::SolverSegment& segment)
{
    QString id = segment.id;
    if (id.isEmpty())
        id = QStringLiteral("anon-%1").arg(segment.startFrame);
    return entity.uuid + kKeySeparator + id + kKeySeparator
           + QString::number(segment.startFrame) + kKeySeparator
           + QString::number(segment.endFrame) + kKeySeparator
           + QString::number(static_cast<int>(segment.effect));
}

std::optional<double> trackValueIfPresent(const model::Entity& entity,
                                          const QString& name, double relativeFrame,
                                          bool step = false)
{
    const auto it = entity.tracks.constFind(name);
    if (it == entity.tracks.constEnd())
        return std::nullopt;
    return model::evaluateTrack(it.value(), relativeFrame, step);
}

double trackValue(const model::Entity& entity, const QString& name,
                  double relativeFrame, double fallback, bool step = false)
{
    return trackValueIfPresent(entity, name, relativeFrame, step).value_or(fallback);
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
    LocalValues values;
    values.x = trackValue(entity, QStringLiteral("x"), relativeFrame,
                          entity.transform.x);
    values.y = trackValue(entity, QStringLiteral("y"), relativeFrame,
                          entity.transform.y);
    values.rotation = trackValue(entity, QStringLiteral("rotation"), relativeFrame,
                                 entity.transform.rotation);
    values.scaleX = trackValue(entity, QStringLiteral("scaleX"), relativeFrame,
                               entity.transform.scaleX);
    values.scaleY = trackValue(entity, QStringLiteral("scaleY"), relativeFrame,
                               entity.transform.scaleY);
    values.pivotX = trackValue(entity, QStringLiteral("pivotX"), relativeFrame,
                               entity.transform.pivotX);
    values.pivotY = trackValue(entity, QStringLiteral("pivotY"), relativeFrame,
                               entity.transform.pivotY);
    values.opacity = trackValue(entity, QStringLiteral("opacity"), relativeFrame,
                                entity.transform.opacity);

    const double visibleValue = trackValue(
        entity, QStringLiteral("visible"), relativeFrame,
        entity.visible ? 1.0 : 0.0, true);
    values.visible = !entity.hidden && visibleValue != 0.0;
    return values;
}

// Model space is Y-up with the origin at the canvas centre.  Qt's rotate()
// uses the standard mathematical matrix, so a positive angle turns +X towards
// +Y, i.e. counter-clockwise on screen.  Poses stay entirely in model space;
// the renderer performs the one and only world -> screen Y flip when it maps
// to the output image (content-local Y-down is flipped separately at paint
// time).  Never bake a screen flip into this matrix.
Pose composePose(const LocalValues& values, bool inheritVisibility,
                 const Pose* parent)
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
    pose.world = parent ? local * parent->world : local;
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
    solver::Pose pose;
    pose.position = QPointF(
        trackValue(entity, QStringLiteral("x"), relativeFrame, entity.transform.x),
        trackValue(entity, QStringLiteral("y"), relativeFrame, entity.transform.y));
    pose.rotation = trackValue(entity, QStringLiteral("rotation"), relativeFrame,
                               entity.transform.rotation);
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

class EvalContext;

class SolverWorldAdapter final : public solver::World
{
public:
    SolverWorldAdapter(const model::Entity& driven, const anim::World& world,
                       EvalContext& context)
        : m_driven(driven), m_world(world), m_context(context)
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
    EvalContext& m_context;
};

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
                                        const model::SolverSegment& segment);
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
    return m_context.parentWorldOf(m_driven, projectFrame);
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
    m_cache.poses[uuid].insert(projectFrame, pose);
    return pose;
}

Pose EvalContext::localPose(const model::Entity& entity, double projectFrame,
                            const Pose* parent)
{
    const double relativeFrame = projectFrame - entity.start;
    LocalValues values;
    values.x = drivenValue(entity, QStringLiteral("x"), relativeFrame,
                           entity.transform.x);
    values.y = drivenValue(entity, QStringLiteral("y"), relativeFrame,
                           entity.transform.y);
    values.rotation = drivenValue(entity, QStringLiteral("rotation"),
                                  relativeFrame, entity.transform.rotation);
    values.scaleX = trackValue(entity, QStringLiteral("scaleX"), relativeFrame,
                               entity.transform.scaleX);
    values.scaleY = trackValue(entity, QStringLiteral("scaleY"), relativeFrame,
                               entity.transform.scaleY);
    values.pivotX = trackValue(entity, QStringLiteral("pivotX"), relativeFrame,
                               entity.transform.pivotX);
    values.pivotY = trackValue(entity, QStringLiteral("pivotY"), relativeFrame,
                               entity.transform.pivotY);
    values.opacity = trackValue(entity, QStringLiteral("opacity"), relativeFrame,
                                entity.transform.opacity);

    const double visibleValue = trackValue(
        entity, QStringLiteral("visible"), relativeFrame,
        entity.visible ? 1.0 : 0.0, true);
    values.visible = !entity.hidden && visibleValue != 0.0;

    return composePose(values, entity.inheritVisibility, parent);
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
        return trackValue(entity, track, relativeFrame, fallback);
    }

    if (const model::SolverSegment* segment = latestEndedSegment(
            entity, relativeFrame, rotationTrack))
    {
        // The solved end pose is an implicit left key; interpolate from it to
        // the first ordinary key after the segment instead of jumping back.
        if (const auto value = anchoredValue(entity, track, relativeFrame,
                                             *segment))
            return *value;
    }

    return trackValue(entity, track, relativeFrame, fallback);
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
    const model::SolverSegment& segment)
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

    const auto trackIt = entity.tracks.constFind(track);
    if (trackIt == entity.tracks.constEnd())
        return anchor;

    const model::Keyframe* next = nullptr;
    for (const model::Keyframe& key : trackIt->keys)
    {
        if (key.frame > end && (!next || key.frame < next->frame))
            next = &key;
    }
    if (!next)
        return anchor;
    if (relativeFrame >= next->frame)
        return std::nullopt; // ordinary interpolation takes over from here

    const int span = next->frame - end;
    if (span <= 0)
        return next->value;

    // The synthetic anchor has no easing of its own; use a linear ramp.
    const double t = double(relativeFrame - end) / double(span);
    return anchor + (next->value - anchor) * t;
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
    return mixed;
}

const solver::Result* EvalContext::resultFor(
    const model::Entity& entity, const model::SolverSegment& segment)
{
    const QString key = segmentKey(entity, segment);
    const auto cached = m_cache.solverResults.constFind(key);
    if (cached != m_cache.solverResults.constEnd())
        return &cached.value();

    solver::Result result;
    if (entity.duration() > 0 && hasRequiredTarget(m_project, segment))
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

        SolverWorldAdapter adapter(entity, m_world, *this);
        result = solver::evaluateResult(request, adapter, entity.duration(),
                                        m_project.settings.fps);
    }

    const auto inserted = m_cache.solverResults.insert(key, result);
    return &inserted.value();
}

} // namespace

void Cache::clear()
{
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
    const double value = trackValue(
        entity, QStringLiteral("physicsInteract"), projectFrame - entity.start,
        entity.physicsInteract ? 1.0 : 0.0, true);
    return value != 0.0;
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
