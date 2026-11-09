#include "mcp2/Internal.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::mcp2::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxSamples = 10000;

const QStringList kSolverCreateKeys = {
    QStringLiteral("effect"), QStringLiteral("startFrame"),
    QStringLiteral("endFrame"), QStringLiteral("initialVelocity"),
    QStringLiteral("params"),
};

const QStringList kSolverUpdateKeys = {
    QStringLiteral("startFrame"), QStringLiteral("endFrame"),
    QStringLiteral("initialVelocity"), QStringLiteral("params"),
};

std::optional<SolverEffect> effectFromName(const QString& name)
{
    if (name == QLatin1String("gravity"))
        return SolverEffect::Gravity;
    if (name == QLatin1String("orbit"))
        return SolverEffect::Orbit;
    if (name == QLatin1String("spin"))
        return SolverEffect::Spin;
    if (name == QLatin1String("blackhole"))
        return SolverEffect::BlackHole;
    return std::nullopt;
}

bool effectUsesTarget(SolverEffect effect)
{
    return effect != SolverEffect::Spin;
}

bool effectUsesVelocity(SolverEffect effect)
{
    return effect == SolverEffect::Gravity
           || effect == SolverEffect::BlackHole;
}

int segmentEnd(const model::Entity& entity, const model::SolverSegment& segment)
{
    return segment.endFrame < 0 ? entity.duration() - 1 : segment.endFrame;
}

double trackNumber(const model::Entity& entity, const QString& track,
                   int relativeFrame, double fallback)
{
    const auto it = entity.tracks.constFind(track);
    if (it == entity.tracks.constEnd())
        return fallback;
    const auto value = model::evaluateTrack(it.value(), relativeFrame);
    return value ? *value : fallback;
}

double paramValue(const QVariantMap& params, const char* key, double fallback)
{
    const auto it = params.constFind(QString::fromLatin1(key));
    if (it == params.constEnd() || !it->isValid())
        return fallback;
    bool ok = false;
    const double value = it->toDouble(&ok);
    return ok ? value : fallback;
}

solver::Pose startPoseAt(const model::Entity& entity, int relativeFrame)
{
    solver::Pose pose;
    pose.position = QPointF(
        trackNumber(entity, QStringLiteral("x"), relativeFrame,
                    entity.transform.x),
        trackNumber(entity, QStringLiteral("y"), relativeFrame,
                    entity.transform.y));
    pose.rotation = trackNumber(entity, QStringLiteral("rotation"), relativeFrame,
                                entity.transform.rotation);
    return pose;
}

QPointF rotatePoint(QPointF point, double degrees)
{
    if (degrees == 0.0)
        return point;
    const double radians = degrees * kPi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return QPointF(point.x() * cosine - point.y() * sine,
                   point.x() * sine + point.y() * cosine);
}

bool solverReaches(const model::Project& project, const Uuid& from,
                   const Uuid& goal)
{
    QVector<Uuid> pending{from};
    QSet<Uuid> seen;
    while (!pending.isEmpty())
    {
        const Uuid current = pending.takeLast();
        if (current == goal)
            return true;
        if (seen.contains(current))
            continue;
        seen.insert(current);

        const model::Entity* entity = findEntity(project, current);
        if (!entity)
            continue;
        for (const model::SolverSegment& segment : entity->solvers)
        {
            if (!effectUsesTarget(segment.effect))
                continue;
            const QString target =
                segment.params.value(QStringLiteral("targetUuid")).toString();
            if (!target.isEmpty() && !seen.contains(target))
                pending.append(target);
        }
    }
    return false;
}

Result parseNumericParam(const QJsonObject& params, const char* key, double min,
                         double max, bool integer = false,
                         bool openMin = false)
{
    const QJsonValue value = params.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return success();
    if (!value.isDouble())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.%1 必须是数字")
                           .arg(QLatin1String(key)));
    const double number = value.toDouble();
    if (!isFiniteNumber(number))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.%1 必须是有限数")
                           .arg(QLatin1String(key)));
    if (integer && std::floor(number) != number)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.%1 必须是整数")
                           .arg(QLatin1String(key)));
    const bool below = openMin ? number <= min : number < min;
    if (below || number > max)
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), min);
        range.insert(QStringLiteral("max"), max);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        details.insert(QStringLiteral("param"), QLatin1String(key));
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.%1 超出允许范围")
                           .arg(QLatin1String(key)),
                       details);
    }
    return success();
}

Result parseDirection(const QJsonObject& params)
{
    const QJsonValue value = params.value(QStringLiteral("direction"));
    if (value.isUndefined() || value.isNull())
        return success();
    if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.direction 必须是 1 或 -1"));
    const double number = value.toDouble();
    if (number != 1.0 && number != -1.0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params.direction 必须是 1 或 -1"));
    return success();
}

bool isKnownParam(SolverEffect effect, const QString& key)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return key == QLatin1String("targetUuid")
               || key == QLatin1String("strength")
               || key == QLatin1String("elasticity")
               || key == QLatin1String("friction")
               || key == QLatin1String("stopThreshold");
    case SolverEffect::Orbit:
        return key == QLatin1String("targetUuid")
               || key == QLatin1String("aspect")
               || key == QLatin1String("angle")
               || key == QLatin1String("direction")
               || key == QLatin1String("periodFrames");
    case SolverEffect::Spin:
        return key == QLatin1String("angularVelocity")
               || key == QLatin1String("direction");
    case SolverEffect::BlackHole:
        return key == QLatin1String("targetUuid")
               || key == QLatin1String("simulationSpeed")
               || key == QLatin1String("mass")
               || key == QLatin1String("angularMomentum")
               || key == QLatin1String("eventHorizonRadius")
               || key == QLatin1String("objectRadius")
               || key == QLatin1String("softening")
               || key == QLatin1String("maxSpeed");
    }
    return false;
}

Result validateSolverParams(const model::Project& project,
                            const Uuid& entityUuid, SolverEffect effect,
                            const QJsonObject& params)
{
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        if (!isKnownParam(effect, it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("params 含未知字段 %1").arg(it.key()));
    }

    if (effectUsesTarget(effect))
    {
        const QJsonValue targetValue = params.value(QStringLiteral("targetUuid"));
        if (targetValue.isUndefined() || targetValue.isNull())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("params.targetUuid 必填"));
        if (!targetValue.isString()
            || !model::isValidUuid(targetValue.toString()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("params.targetUuid 必须是合法 UUID"));
        const Uuid target = targetValue.toString();
        if (target == entityUuid)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("求解目标不能是实体自己"));
        if (!findEntity(project, target))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("求解目标实体不存在：%1").arg(target));
        if (solverReaches(project, target, entityUuid))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("求解目标会形成循环依赖"));
    }

    Result result;
    switch (effect)
    {
    case SolverEffect::Gravity:
        if (!(result = parseNumericParam(params, "strength", 0, 1000000)).ok
            || !(result = parseNumericParam(params, "elasticity", 0, 1)).ok
            || !(result = parseNumericParam(params, "friction", 0, 1)).ok
            || !(result = parseNumericParam(params, "stopThreshold", 0, 1000))
                    .ok)
            return result;
        break;
    case SolverEffect::Orbit:
        if (!(result = parseNumericParam(params, "aspect", 0.01, 1)).ok
            || !(result = parseNumericParam(params, "angle", -360, 360)).ok
            || !(result = parseDirection(params)).ok
            || !(result = parseNumericParam(params, "periodFrames", 1, 1000000,
                                            true))
                    .ok)
            return result;
        break;
    case SolverEffect::Spin:
        if (!(result = parseNumericParam(params, "angularVelocity", 0, 360)).ok
            || !(result = parseDirection(params)).ok)
            return result;
        break;
    case SolverEffect::BlackHole:
        if (!(result = parseNumericParam(params, "simulationSpeed", 0.01, 20))
                 .ok
            || !(result = parseNumericParam(params, "mass", 0, 1000000000,
                                            false, true))
                    .ok
            || !(result = parseNumericParam(params, "angularMomentum", -1000000,
                                            1000000))
                    .ok
            || !(result = parseNumericParam(params, "eventHorizonRadius", 0,
                                            1000000))
                    .ok
            || !(result = parseNumericParam(params, "objectRadius", 0, 1000000))
                    .ok
            || !(result = parseNumericParam(params, "softening", 0, 1000000,
                                            false, true))
                    .ok
            || !(result = parseNumericParam(params, "maxSpeed", 0, 1000000,
                                            false, true))
                    .ok)
            return result;
        break;
    }
    return success();
}

Result parseInitialVelocity(const QJsonValue& value, QPointF* out)
{
    *out = QPointF(0.0, 0.0);
    if (value.isUndefined() || value.isNull())
        return success();
    if (!value.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("initialVelocity 必须是对象"));
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (it.key() != QLatin1String("x") && it.key() != QLatin1String("y"))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("initialVelocity 含未知字段 %1")
                               .arg(it.key()));
        if (!it.value().isDouble() || !isFiniteNumber(it.value().toDouble()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("initialVelocity.%1 必须是有限数")
                               .arg(it.key()));
    }
    *out = QPointF(object.value(QStringLiteral("x")).toDouble(0.0),
                   object.value(QStringLiteral("y")).toDouble(0.0));
    return success();
}

Result checkStartKeyframes(const model::Entity& entity,
                           const model::SolverSegment& segment)
{
    QStringList missing;
    for (const QString& track : drivenTracks(segment.effect))
    {
        const auto it = entity.tracks.constFind(track);
        bool found = false;
        if (it != entity.tracks.constEnd())
        {
            for (const model::Keyframe& key : it->keys)
            {
                if (key.frame == segment.startFrame)
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            missing << track;
    }
    if (missing.isEmpty())
        return success();
    QJsonObject details;
    details.insert(QStringLiteral("missingTracks"),
                   QJsonArray::fromStringList(missing));
    return failure(QStringLiteral("conflict"),
                   QStringLiteral("求解开始帧缺少被驱动轨道的关键帧"), details);
}

Result checkInteriorKeyframes(const model::Entity& entity,
                              const model::SolverSegment& segment)
{
    const int end = segmentEnd(entity, segment);
    QJsonArray conflicts;
    for (const QString& track : drivenTracks(segment.effect))
    {
        const auto it = entity.tracks.constFind(track);
        if (it == entity.tracks.constEnd())
            continue;
        for (const model::Keyframe& key : it->keys)
        {
            if (key.frame <= segment.startFrame || key.frame > end)
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("track"), track);
            item.insert(QStringLiteral("frame"), key.frame);
            conflicts.append(item);
        }
    }
    if (conflicts.isEmpty())
        return success();
    QJsonObject details;
    details.insert(QStringLiteral("conflictingFrames"), conflicts);
    return failure(QStringLiteral("conflict"),
                   QStringLiteral("求解段区间内仍有被驱动属性的普通关键帧"),
                   details);
}

Result checkOverlap(const model::Entity& entity,
                    const model::SolverSegment& candidate,
                    const Uuid& skipId = QString())
{
    const int start = candidate.startFrame;
    const int end = segmentEnd(entity, candidate);
    QJsonArray conflicts;
    for (const model::SolverSegment& other : entity.solvers)
    {
        if (!skipId.isEmpty() && other.id == skipId)
            continue;
        const int otherStart = other.startFrame;
        const int otherEnd = segmentEnd(entity, other);
        if (start <= otherEnd && otherStart <= end)
        {
            QJsonObject item;
            item.insert(QStringLiteral("solverId"), other.id);
            item.insert(QStringLiteral("startFrame"), otherStart);
            item.insert(QStringLiteral("endFrame"), other.endFrame);
            conflicts.append(item);
        }
    }
    if (conflicts.isEmpty())
        return success();
    QJsonObject details;
    details.insert(QStringLiteral("conflicts"), conflicts);
    return failure(QStringLiteral("conflict"), QStringLiteral("求解段重叠"),
                   details);
}

Result validateSegmentFrames(const model::Entity& entity, int startFrame,
                             int endFrame)
{
    if (startFrame < 0 || startFrame >= entity.duration())
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), 0);
        range.insert(QStringLiteral("max"), entity.duration() - 1);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("startFrame 超出实体范围"), details);
    }
    if (endFrame != -1
        && (endFrame <= startFrame || endFrame >= entity.duration()))
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), startFrame + 1);
        range.insert(QStringLiteral("max"), entity.duration() - 1);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("endFrame 非法（必须为 -1 或位于 %1..%2）")
                           .arg(startFrame + 1)
                           .arg(entity.duration() - 1),
                       details);
    }
    return success();
}

class SceneWorldAdapter final : public solver::World
{
public:
    SceneWorldAdapter(const model::Entity& driven,
                      const model::Project& project,
                      const render2::SceneWorld& world)
        : m_driven(driven)
        , m_project(project)
        , m_world(world)
    {
    }

    solver::Pose poseOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
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

    QPainterPath shapeOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
        const QPainterPath path = m_world.shapeOf(uuid, projectFrame);
        const QTransform parent = parentWorldOf(projectFrame);
        if (parent.isIdentity())
            return path;

        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        return invertible ? inverse.map(path) : path;
    }

    bool physicsInteract(const Uuid& uuid, int frame) const override
    {
        return m_world.physicsInteract(uuid, frame + m_driven.start);
    }

    QVector<Uuid> physicsEntities(int frame) const override
    {
        return m_world.physicsEntities(frame + m_driven.start);
    }

private:
    static double transformRotation(const QTransform& transform)
    {
        return std::atan2(transform.m12(), transform.m11()) * 180.0 / kPi;
    }

    QTransform parentWorldOf(int projectFrame) const
    {
        if (m_driven.parentUuid.isEmpty()
            || !m_project.entities.contains(m_driven.parentUuid))
            return QTransform();
        return anim::evaluateEntity(m_project, m_driven.parentUuid, projectFrame,
                                    m_world, m_cache)
            .world;
    }

    const model::Entity& m_driven;
    const model::Project& m_project;
    const render2::SceneWorld& m_world;
    mutable anim::Cache m_cache;
};

QJsonObject orbitComputed(const model::Project& project,
                          const assets::AssetStore& assets,
                          const model::Entity& entity,
                          const model::SolverSegment& segment)
{
    const double aspect =
        std::clamp(paramValue(segment.params, "aspect", 1.0), 0.01, 1.0);
    const double angle = paramValue(segment.params, "angle", 0.0);
    const QString target =
        segment.params.value(QStringLiteral("targetUuid")).toString();

    solver::Pose center;
    center.position = QPointF(0.0, 0.0);
    if (!target.isEmpty() && findEntity(project, target))
    {
        render2::SceneWorld world(project, assets);
        SceneWorldAdapter adapter(entity, project, world);
        center = adapter.poseOf(target, segment.startFrame);
    }

    const solver::Pose start = startPoseAt(entity, segment.startFrame);
    const QPointF relative =
        rotatePoint(start.position - center.position, -(center.rotation + angle));
    const double semiMajor =
        std::sqrt(relative.x() * relative.x()
                  + (relative.y() * relative.y()) / (aspect * aspect));
    const double semiMinor = aspect * semiMajor;

    QJsonObject computed;
    computed.insert(QStringLiteral("targetUuid"),
                    target.isEmpty() ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(target));
    computed.insert(QStringLiteral("centerX"), center.position.x());
    computed.insert(QStringLiteral("centerY"), center.position.y());
    computed.insert(QStringLiteral("semiMajor"), semiMajor);
    computed.insert(QStringLiteral("semiMinor"), semiMinor);
    computed.insert(QStringLiteral("aspect"), aspect);
    computed.insert(QStringLiteral("angle"), angle);
    computed.insert(QStringLiteral("periodFrames"),
                    paramValue(segment.params, "periodFrames", 60.0));
    return computed;
}

QJsonObject sampleJson(const solver::Sample& sample)
{
    QJsonObject object;
    object.insert(QStringLiteral("frame"), sample.frame);
    object.insert(QStringLiteral("x"), sample.pose.position.x());
    object.insert(QStringLiteral("y"), sample.pose.position.y());
    object.insert(QStringLiteral("rotation"), sample.pose.rotation);

    QJsonObject velocity;
    velocity.insert(QStringLiteral("x"), sample.velocity.x());
    velocity.insert(QStringLiteral("y"), sample.velocity.y());
    object.insert(QStringLiteral("velocity"), velocity);
    object.insert(QStringLiteral("collided"), sample.collided);
    object.insert(QStringLiteral("stopped"), sample.stopped);
    return object;
}

QJsonObject sampleSummary(const QVector<solver::Sample>& samples)
{
    QJsonObject summary;
    if (samples.isEmpty())
    {
        summary.insert(QStringLiteral("minX"), 0.0);
        summary.insert(QStringLiteral("maxX"), 0.0);
        summary.insert(QStringLiteral("minY"), 0.0);
        summary.insert(QStringLiteral("maxY"), 0.0);
        summary.insert(QStringLiteral("firstContactFrame"),
                       QJsonValue(QJsonValue::Null));
        summary.insert(QStringLiteral("stoppedFrame"),
                       QJsonValue(QJsonValue::Null));
        return summary;
    }

    double minX = samples.first().pose.position.x();
    double maxX = minX;
    double minY = samples.first().pose.position.y();
    double maxY = minY;
    QJsonValue firstContact = QJsonValue(QJsonValue::Null);
    QJsonValue stoppedFrame = QJsonValue(QJsonValue::Null);
    for (const solver::Sample& sample : samples)
    {
        minX = std::min(minX, sample.pose.position.x());
        maxX = std::max(maxX, sample.pose.position.x());
        minY = std::min(minY, sample.pose.position.y());
        maxY = std::max(maxY, sample.pose.position.y());
        if (firstContact.isNull() && sample.collided)
            firstContact = sample.frame;
        if (stoppedFrame.isNull() && sample.stopped)
            stoppedFrame = sample.frame;
    }

    summary.insert(QStringLiteral("minX"), minX);
    summary.insert(QStringLiteral("maxX"), maxX);
    summary.insert(QStringLiteral("minY"), minY);
    summary.insert(QStringLiteral("maxY"), maxY);
    summary.insert(QStringLiteral("firstContactFrame"), firstContact);
    summary.insert(QStringLiteral("stoppedFrame"), stoppedFrame);
    return summary;
}

model::SolverSegment* findSegment(model::Entity& entity, const Uuid& solverId)
{
    for (model::SolverSegment& segment : entity.solvers)
    {
        if (segment.id == solverId)
            return &segment;
    }
    return nullptr;
}

} // namespace

Result parseSolverCreate(const model::Project& project,
                         const model::Entity& entity,
                         const QJsonObject& object,
                         model::SolverSegment* segment)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!kSolverCreateKeys.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    if (entity.duration() <= 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体时长必须大于 0"));

    Result error;
    QString effectName;
    if (!readString(object, "effect", &effectName, &error, true))
        return error;
    const auto effect = effectFromName(effectName);
    if (!effect)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("未知求解效果：%1").arg(effectName));

    int startFrame = 0;
    if (!readInt(object, "startFrame", &startFrame, &error, true))
        return error;
    int endFrame = -1;
    if (!readInt(object, "endFrame", &endFrame, &error))
        return error;

    Result frames = validateSegmentFrames(entity, startFrame, endFrame);
    if (!frames.ok)
        return frames;

    QPointF velocity(0.0, 0.0);
    if (object.contains(QStringLiteral("initialVelocity"))
        && !effectUsesVelocity(*effect))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1 不支持 initialVelocity")
                           .arg(effectName));
    Result velocityResult =
        parseInitialVelocity(object.value(QStringLiteral("initialVelocity")),
                             &velocity);
    if (!velocityResult.ok)
        return velocityResult;

    const QJsonValue paramsValue = object.value(QStringLiteral("params"));
    if (!paramsValue.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("params 必须是对象"));
    const QJsonObject rawParams = paramsValue.toObject();
    QJsonObject params;
    for (auto it = rawParams.constBegin(); it != rawParams.constEnd(); ++it)
    {
        if (!it.value().isNull())
            params.insert(it.key(), it.value());
    }
    Result paramsResult =
        validateSolverParams(project, entity.uuid, *effect, params);
    if (!paramsResult.ok)
        return paramsResult;

    model::SolverSegment candidate;
    candidate.id = model::newUuid();
    candidate.effect = *effect;
    candidate.startFrame = startFrame;
    candidate.endFrame = endFrame;
    candidate.version = 1;
    candidate.initialVelocity = velocity;
    candidate.params = params.toVariantMap();

    Result startKeys = checkStartKeyframes(entity, candidate);
    if (!startKeys.ok)
        return startKeys;
    Result interior = checkInteriorKeyframes(entity, candidate);
    if (!interior.ok)
        return interior;
    Result overlap = checkOverlap(entity, candidate);
    if (!overlap.ok)
        return overlap;

    *segment = candidate;
    return success();
}

Result solverCreate(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid entityUuid;
    if (!readUuid(args, "entityUuid", &entityUuid, &error, true))
        return error;
    model::Entity* entity = findEntity(tx.p(), entityUuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(entityUuid));
    if (!ensureUnlocked(tx.p(), entityUuid, &error))
        return error;

    QJsonObject payload = args;
    payload.remove(QStringLiteral("entityUuid"));

    model::SolverSegment segment;
    Result parsed = parseSolverCreate(tx.p(), *entity, payload, &segment);
    if (!parsed.ok)
        return parsed;

    entity->solvers.append(segment);
    tx.touch(entityUuid);

    QJsonObject data;
    data.insert(QStringLiteral("solver"), model::toJson(segment));
    return tx.finish(data, QStringLiteral("pvf: create solver \"%1\" %2")
                               .arg(entity->name,
                                    solverEffectName(segment.effect)));
}

Result solverUpdate(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid entityUuid;
    Uuid solverId;
    if (!readUuid(args, "entityUuid", &entityUuid, &error, true)
        || !readUuid(args, "solverId", &solverId, &error, true))
        return error;
    model::Entity* entity = findEntity(tx.p(), entityUuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(entityUuid));
    if (!ensureUnlocked(tx.p(), entityUuid, &error))
        return error;

    model::SolverSegment* existing = findSegment(*entity, solverId);
    if (!existing)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("求解段不存在：%1").arg(solverId));

    const QJsonValue patchValue = args.value(QStringLiteral("patch"));
    if (!patchValue.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("patch 必须是对象"));
    const QJsonObject patch = patchValue.toObject();
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (!kSolverUpdateKeys.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("patch 不允许字段 %1").arg(it.key()));
    }

    model::SolverSegment updated = *existing;
    if (patch.contains(QStringLiteral("startFrame")))
    {
        if (!readInt(patch, "startFrame", &updated.startFrame, &error, true))
            return error;
    }
    if (patch.contains(QStringLiteral("endFrame")))
    {
        if (!readInt(patch, "endFrame", &updated.endFrame, &error, true))
            return error;
    }
    Result frames = validateSegmentFrames(*entity, updated.startFrame,
                                          updated.endFrame);
    if (!frames.ok)
        return frames;

    if (patch.contains(QStringLiteral("initialVelocity")))
    {
        if (!effectUsesVelocity(updated.effect))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1 不支持 initialVelocity")
                               .arg(solverEffectName(updated.effect)));
        Result velocityResult = parseInitialVelocity(
            patch.value(QStringLiteral("initialVelocity")),
            &updated.initialVelocity);
        if (!velocityResult.ok)
            return velocityResult;
    }

    QJsonObject params = QJsonObject::fromVariantMap(updated.params);
    if (patch.contains(QStringLiteral("params")))
    {
        const QJsonValue paramsValue = patch.value(QStringLiteral("params"));
        if (!paramsValue.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("patch.params 必须是对象"));
        const QJsonObject update = paramsValue.toObject();
        for (auto it = update.constBegin(); it != update.constEnd(); ++it)
        {
            if (!isKnownParam(updated.effect, it.key()))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("params 含未知字段 %1")
                                   .arg(it.key()));
            if (it.value().isNull())
                params.remove(it.key());
            else
                params.insert(it.key(), it.value());
        }
    }
    Result paramsResult = validateSolverParams(tx.p(), entityUuid,
                                               updated.effect, params);
    if (!paramsResult.ok)
        return paramsResult;
    updated.params = params.toVariantMap();

    Result startKeys = checkStartKeyframes(*entity, updated);
    if (!startKeys.ok)
        return startKeys;
    Result interior = checkInteriorKeyframes(*entity, updated);
    if (!interior.ok)
        return interior;
    Result overlap = checkOverlap(*entity, updated, solverId);
    if (!overlap.ok)
        return overlap;

    *existing = updated;
    tx.touch(entityUuid);

    QJsonObject data;
    data.insert(QStringLiteral("solver"), model::toJson(updated));
    return tx.finish(data, QStringLiteral("pvf: update solver \"%1\" %2")
                               .arg(entity->name,
                                    solverEffectName(updated.effect)));
}

Result solverDelete(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid entityUuid;
    Uuid solverId;
    if (!readUuid(args, "entityUuid", &entityUuid, &error, true)
        || !readUuid(args, "solverId", &solverId, &error, true))
        return error;
    model::Entity* entity = findEntity(tx.p(), entityUuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(entityUuid));
    if (!ensureUnlocked(tx.p(), entityUuid, &error))
        return error;

    int index = -1;
    for (int i = 0; i < entity->solvers.size(); ++i)
    {
        if (entity->solvers.at(i).id == solverId)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("求解段不存在：%1").arg(solverId));

    const QString effect = solverEffectName(entity->solvers.at(index).effect);
    entity->solvers.removeAt(index);
    tx.touch(entityUuid);

    QJsonObject data;
    data.insert(QStringLiteral("deleted"), true);
    data.insert(QStringLiteral("solverId"), solverId);
    return tx.finish(data, QStringLiteral("pvf: delete solver \"%1\" %2")
                               .arg(entity->name, effect));
}

Result solverList(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid entityUuid;
    if (!readUuid(args, "entityUuid", &entityUuid, &error, true))
        return error;
    const model::Entity* entity = findEntity(tx.p(), entityUuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(entityUuid));

    QVector<model::SolverSegment> sorted = entity->solvers;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const model::SolverSegment& a,
                        const model::SolverSegment& b) {
                         return a.startFrame < b.startFrame;
                     });

    QJsonArray solvers;
    for (const model::SolverSegment& segment : std::as_const(sorted))
    {
        QJsonObject object = model::toJson(segment);
        if (segment.effect == SolverEffect::Orbit)
            object.insert(QStringLiteral("computed"),
                          orbitComputed(tx.p(), tx.store->assets(), *entity,
                                        segment));
        solvers.append(object);
    }

    QJsonObject data;
    data.insert(QStringLiteral("entityUuid"), entityUuid);
    data.insert(QStringLiteral("duration"), entity->duration());
    data.insert(QStringLiteral("solvers"), solvers);
    return success(data);
}

Result solverEvaluate(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid entityUuid;
    if (!readUuid(args, "entityUuid", &entityUuid, &error, true))
        return error;
    const model::Entity* entity = findEntity(tx.p(), entityUuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(entityUuid));

    Uuid solverId;
    if (!readUuid(args, "solverId", &solverId, &error))
        return error;

    QVector<model::SolverSegment> selected;
    if (!solverId.isEmpty())
    {
        const model::SolverSegment* segment = nullptr;
        for (const model::SolverSegment& candidate : entity->solvers)
        {
            if (candidate.id == solverId)
            {
                segment = &candidate;
                break;
            }
        }
        if (!segment)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("求解段不存在：%1").arg(solverId));
        selected.append(*segment);
    }
    else
    {
        selected = entity->solvers;
        std::stable_sort(selected.begin(), selected.end(),
                         [](const model::SolverSegment& a,
                            const model::SolverSegment& b) {
                             return a.startFrame < b.startFrame;
                         });
    }

    QVector<int> requestedFrames;
    const QJsonValue framesValue = args.value(QStringLiteral("frames"));
    if (!framesValue.isUndefined() && !framesValue.isNull())
    {
        if (!framesValue.isArray())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("frames 必须是数组"));
        for (const QJsonValue& value : framesValue.toArray())
        {
            if (!value.isDouble()
                || std::floor(value.toDouble()) != value.toDouble())
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("frames 必须是整数数组"));
            const int frame = int(value.toDouble());
            if (frame < 0 || frame >= entity->duration())
            {
                QJsonObject range;
                range.insert(QStringLiteral("min"), 0);
                range.insert(QStringLiteral("max"), entity->duration() - 1);
                QJsonObject details;
                details.insert(QStringLiteral("allowedRange"), range);
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("帧号越界"), details);
            }
            bool member = false;
            for (const model::SolverSegment& segment : std::as_const(selected))
            {
                if (frame >= segment.startFrame
                    && frame <= segmentEnd(*entity, segment))
                {
                    member = true;
                    break;
                }
            }
            if (!member)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("帧 %1 不在任何求解段内")
                                   .arg(frame));
            requestedFrames.append(frame);
        }
        std::sort(requestedFrames.begin(), requestedFrames.end());
        requestedFrames.erase(
            std::unique(requestedFrames.begin(), requestedFrames.end()),
            requestedFrames.end());
        if (requestedFrames.size() > kMaxSamples)
        {
            QJsonObject details;
            details.insert(QStringLiteral("maxSamples"), kMaxSamples);
            return failure(QStringLiteral("limit_exceeded"),
                           QStringLiteral("采样超过 %1 帧").arg(kMaxSamples),
                           details);
        }
    }
    else
    {
        qint64 total = 0;
        for (const model::SolverSegment& segment : std::as_const(selected))
            total += segmentEnd(*entity, segment) - segment.startFrame + 1;
        if (total > kMaxSamples)
        {
            QJsonObject details;
            details.insert(QStringLiteral("maxSamples"), kMaxSamples);
            return failure(QStringLiteral("limit_exceeded"),
                           QStringLiteral("采样超过 %1 帧，请显式给 frames")
                               .arg(kMaxSamples),
                           details);
        }
    }

    QJsonArray results;
    for (const model::SolverSegment& segment : std::as_const(selected))
    {
        Result startKeys = checkStartKeyframes(*entity, segment);
        if (!startKeys.ok)
            return startKeys;

        solver::Segment request;
        request.effect = segment.effect;
        request.entityUuid = entity->uuid;
        request.targetUuid =
            segment.params.value(QStringLiteral("targetUuid")).toString();
        request.startFrame = segment.startFrame;
        request.endFrame = segment.endFrame;
        request.startPose = startPoseAt(*entity, segment.startFrame);
        request.initialVelocity = segment.initialVelocity;
        request.params = segment.params;

        if (effectUsesTarget(segment.effect)
            && (request.targetUuid.isEmpty()
                || !findEntity(tx.p(), request.targetUuid)))
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("求解目标实体不存在"));

        render2::SceneWorld world(tx.p(), tx.store->assets());
        SceneWorldAdapter adapter(*entity, tx.p(), world);
        QVector<solver::Sample> samples =
            solver::evaluate(request, adapter, entity->duration(),
                             tx.p().settings.fps);
        if (samples.isEmpty())
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("求解段无法解算，请检查起始关键帧与参数"));

        QVector<solver::Sample> output;
        if (requestedFrames.isEmpty())
        {
            output = samples;
        }
        else
        {
            for (const solver::Sample& sample : samples)
            {
                if (std::binary_search(requestedFrames.begin(),
                                       requestedFrames.end(), sample.frame))
                    output.append(sample);
            }
        }

        QJsonArray sampleArray;
        for (const solver::Sample& sample : std::as_const(output))
            sampleArray.append(sampleJson(sample));

        QJsonObject summary = sampleSummary(output);
        if (segment.effect == SolverEffect::Orbit)
            summary.insert(QStringLiteral("computed"),
                           orbitComputed(tx.p(), tx.store->assets(), *entity,
                                         segment));

        QJsonObject result;
        result.insert(QStringLiteral("solverId"), segment.id);
        result.insert(QStringLiteral("effect"),
                      solverEffectName(segment.effect));
        result.insert(QStringLiteral("samples"), sampleArray);
        result.insert(QStringLiteral("summary"), summary);
        results.append(result);
    }

    QJsonObject data;
    data.insert(QStringLiteral("entityUuid"), entityUuid);
    data.insert(QStringLiteral("framesRelativeTo"),
                QStringLiteral("entityStart"));
    data.insert(QStringLiteral("results"), results);
    return success(data);
}

} // namespace pvf::mcp2::detail
