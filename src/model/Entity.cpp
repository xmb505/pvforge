#include "Entity.h"

#include "JsonUtil.h"
#include "Uuid.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf::model {

namespace {

std::optional<SolverEffect> solverEffectFromName(const QString& name)
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

QStringList drivenTracks(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
    case SolverEffect::Orbit:
    case SolverEffect::BlackHole:
        return {QStringLiteral("x"), QStringLiteral("y")};
    case SolverEffect::Spin:
        return {QStringLiteral("rotation")};
    }
    return {};
}

} // namespace

QJsonObject toJson(const SolverSegment& segment)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), segment.id);
    object.insert(QStringLiteral("effect"), solverEffectName(segment.effect));
    object.insert(QStringLiteral("startFrame"), segment.startFrame);
    object.insert(QStringLiteral("endFrame"), segment.endFrame);
    object.insert(QStringLiteral("version"), segment.version);

    QJsonObject velocity;
    velocity.insert(QStringLiteral("x"), segment.initialVelocity.x());
    velocity.insert(QStringLiteral("y"), segment.initialVelocity.y());
    object.insert(QStringLiteral("initialVelocity"), velocity);
    object.insert(QStringLiteral("params"),
                  QJsonObject::fromVariantMap(segment.params));
    return object;
}

std::optional<SolverSegment> solverFromJson(const QJsonObject& object,
                                            QString* error)
{
    SolverSegment segment;
    segment.id = detail::jsonString(object, QStringLiteral("id"));

    const QString effectName = detail::jsonString(
        object, QStringLiteral("effect"), QStringLiteral("gravity"));
    const auto effect = solverEffectFromName(effectName);
    if (!effect)
    {
        detail::setError(error,
                         QStringLiteral("未知求解效果：%1").arg(effectName));
        return std::nullopt;
    }
    segment.effect = *effect;
    segment.startFrame = detail::jsonInt(
        object, QStringLiteral("startFrame"), segment.startFrame);
    segment.endFrame = detail::jsonInt(
        object, QStringLiteral("endFrame"), segment.endFrame);
    segment.version = detail::jsonInt(
        object, QStringLiteral("version"), segment.version);

    const QJsonValue velocityValue = object.value(
        QStringLiteral("initialVelocity"));
    if (!velocityValue.isUndefined() && !velocityValue.isNull())
    {
        if (!velocityValue.isObject())
        {
            detail::setError(error, QStringLiteral("initialVelocity 必须是对象"));
            return std::nullopt;
        }
        const QJsonObject velocity = velocityValue.toObject();
        segment.initialVelocity = QPointF(
            detail::jsonDouble(velocity, QStringLiteral("x"), 0.0),
            detail::jsonDouble(velocity, QStringLiteral("y"), 0.0));
    }

    const QJsonValue paramsValue = object.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined() && !paramsValue.isNull())
    {
        if (!paramsValue.isObject())
        {
            detail::setError(error, QStringLiteral("params 必须是对象"));
            return std::nullopt;
        }
        segment.params = paramsValue.toObject().toVariantMap();
    }

    return segment;
}

QJsonObject toJson(const Entity& entity)
{
    QJsonObject object;
    object.insert(QStringLiteral("uuid"), entity.uuid);
    object.insert(QStringLiteral("name"), entity.name);
    object.insert(QStringLiteral("parentUuid"),
                  entity.parentUuid.isEmpty()
                      ? QJsonValue(QJsonValue::Null)
                      : QJsonValue(entity.parentUuid));
    object.insert(QStringLiteral("start"), entity.start);
    object.insert(QStringLiteral("end"), entity.end);
    object.insert(QStringLiteral("row"), entity.row);
    object.insert(QStringLiteral("hidden"), entity.hidden);
    object.insert(QStringLiteral("locked"), entity.locked);
    object.insert(QStringLiteral("inheritVisibility"), entity.inheritVisibility);
    object.insert(QStringLiteral("visible"), entity.visible);
    object.insert(QStringLiteral("physicsInteract"), entity.physicsInteract);
    object.insert(QStringLiteral("transform"), toJson(entity.transform));

    QJsonObject keyframes;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        keyframes.insert(it.key(), toJson(it.value()));
    object.insert(QStringLiteral("keyframes"), keyframes);

    QJsonArray solvers;
    for (const SolverSegment& segment : entity.solvers)
        solvers.append(toJson(segment));
    object.insert(QStringLiteral("solvers"), solvers);

    object.insert(QStringLiteral("content"), toJson(entity.content));
    return object;
}

std::optional<Entity> entityFromJson(const QJsonObject& object, QString* error)
{
    Entity entity;
    entity.uuid = detail::jsonString(object, QStringLiteral("uuid"));
    entity.name = detail::jsonString(object, QStringLiteral("name"));

    const QJsonValue parentValue = object.value(QStringLiteral("parentUuid"));
    if (parentValue.isString())
        entity.parentUuid = parentValue.toString();
    else if (!parentValue.isUndefined() && !parentValue.isNull())
    {
        detail::setError(error, QStringLiteral("parentUuid 必须是 UUID 字符串或 null"));
        return std::nullopt;
    }

    entity.start = detail::jsonInt(object, QStringLiteral("start"), entity.start);
    entity.end = object.contains(QStringLiteral("end"))
                     ? detail::jsonInt(object, QStringLiteral("end"),
                                       entity.start + 1)
                     : entity.start + 1;
    entity.row = detail::jsonInt(object, QStringLiteral("row"), entity.row);
    entity.hidden = detail::jsonBool(
        object, QStringLiteral("hidden"), entity.hidden);
    entity.locked = detail::jsonBool(
        object, QStringLiteral("locked"), entity.locked);
    entity.inheritVisibility = detail::jsonBool(
        object, QStringLiteral("inheritVisibility"), entity.inheritVisibility);
    entity.visible = detail::jsonBool(
        object, QStringLiteral("visible"), entity.visible);
    entity.physicsInteract = detail::jsonBool(
        object, QStringLiteral("physicsInteract"), entity.physicsInteract);

    const QJsonValue transformValue = object.value(QStringLiteral("transform"));
    if (!transformValue.isUndefined() && !transformValue.isNull())
    {
        if (!transformValue.isObject())
        {
            detail::setError(error, QStringLiteral("transform 必须是对象"));
            return std::nullopt;
        }
        const auto transform = transformFromJson(transformValue.toObject(), error);
        if (!transform)
            return std::nullopt;
        entity.transform = *transform;
    }

    const QJsonValue keyframesValue = object.value(QStringLiteral("keyframes"));
    if (!keyframesValue.isUndefined() && !keyframesValue.isNull())
    {
        if (!keyframesValue.isObject())
        {
            detail::setError(error, QStringLiteral("keyframes 必须是对象"));
            return std::nullopt;
        }
        const QJsonObject keyframes = keyframesValue.toObject();
        for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it)
        {
            if (it.key().isEmpty())
            {
                detail::setError(error, QStringLiteral("关键帧轨道名不能为空"));
                return std::nullopt;
            }
            const auto track = trackFromJson(it.value(), error);
            if (!track)
                return std::nullopt;
            entity.tracks.insert(it.key(), *track);
        }
    }

    const QJsonValue contentValue = object.value(QStringLiteral("content"));
    if (!contentValue.isUndefined() && !contentValue.isNull())
    {
        if (!contentValue.isObject())
        {
            detail::setError(error, QStringLiteral("content 必须是对象"));
            return std::nullopt;
        }
        const auto content = contentFromJson(contentValue.toObject(), error);
        if (!content)
            return std::nullopt;
        entity.content = *content;
    }

    const QJsonValue solversValue = object.value(QStringLiteral("solvers"));
    if (!solversValue.isUndefined() && !solversValue.isNull())
    {
        if (!solversValue.isArray())
        {
            detail::setError(error, QStringLiteral("solvers 必须是数组"));
            return std::nullopt;
        }
        const QJsonArray solvers = solversValue.toArray();
        for (int i = 0; i < solvers.size(); ++i)
        {
            if (!solvers.at(i).isObject())
            {
                detail::setError(error,
                                 QStringLiteral("第 %1 个求解段不是对象").arg(i));
                return std::nullopt;
            }
            const auto segment = solverFromJson(solvers.at(i).toObject(), error);
            if (!segment)
                return std::nullopt;
            entity.solvers.append(*segment);
        }
    }

    if (!validateEntity(entity, error))
        return std::nullopt;
    return entity;
}

bool validateTrack(const Track& track, int duration, const QString& trackName,
                   QString* error)
{
    if (duration <= 0)
    {
        detail::setError(
            error, QStringLiteral("轨道 %1 所属实体的时长必须大于 0").arg(trackName));
        return false;
    }

    for (const Keyframe& keyframe : track.keys)
    {
        if (keyframe.frame < 0 || keyframe.frame >= duration)
        {
            detail::setError(
                error,
                QStringLiteral("轨道 %1 的关键帧帧号 %2 超出允许范围 0..%3")
                    .arg(trackName)
                    .arg(keyframe.frame)
                    .arg(duration - 1));
            return false;
        }
    }
    return true;
}

bool isLastKeyframeInClip(const Entity& entity, int relativeFrame)
{
    bool found = false;
    for (auto track = entity.tracks.constBegin();
         track != entity.tracks.constEnd(); ++track)
    {
        for (const Keyframe& keyframe : track.value().keys)
        {
            if (keyframe.frame == relativeFrame)
                found = true;
            else if (keyframe.frame > relativeFrame)
                return false;
        }
    }
    return found;
}

int solverStructuralEnd(const Entity& entity, const SolverSegment& segment)
{
    const int lastFrame = entity.duration() - 1;
    if (lastFrame < 0)
        return -1;
    if (segment.endFrame >= 0)
        return std::min(segment.endFrame, lastFrame);

    int end = lastFrame;

    // The first ordinary key created after the solver start is the end
    // marker: the solver stops at N-1 and the key at N belongs to the normal
    // animation that follows.
    for (auto track = entity.tracks.constBegin();
         track != entity.tracks.constEnd(); ++track)
    {
        for (const Keyframe& keyframe : track.value().keys)
            if (keyframe.frame > segment.startFrame)
                end = std::min(end, keyframe.frame - 1);
    }

    // A later segment owns the following frames; automatic segments cannot
    // overlap it even when no key separates them.
    for (const SolverSegment& other : entity.solvers)
    {
        if (other.id == segment.id)
            continue;
        if (other.startFrame > segment.startFrame)
            end = std::min(end, other.startFrame - 1);
    }

    return end;
}

bool validateSolvers(const Entity& entity, QString* error)
{
    const int duration = entity.duration();
    if (duration <= 0)
    {
        detail::setError(error, QStringLiteral("实体时长必须大于 0"));
        return false;
    }

    QSet<Uuid> ids;
    QVector<int> effectiveEnds;
    effectiveEnds.reserve(entity.solvers.size());

    for (int i = 0; i < entity.solvers.size(); ++i)
    {
        const SolverSegment& segment = entity.solvers.at(i);
        const QString label = QStringLiteral("实体 %1 的第 %2 个求解段")
                                  .arg(entity.uuid)
                                  .arg(i);

        if (!isValidUuid(segment.id))
        {
            detail::setError(error, label + QStringLiteral("的 id 不是合法 UUID"));
            return false;
        }
        if (ids.contains(segment.id))
        {
            detail::setError(error, label + QStringLiteral("的 id 重复"));
            return false;
        }
        ids.insert(segment.id);

        if (segment.startFrame < 0 || segment.startFrame >= duration)
        {
            detail::setError(
                error,
                label
                    + QStringLiteral("起始帧 %1 超出允许范围 0..%2")
                          .arg(segment.startFrame)
                          .arg(duration - 1));
            return false;
        }

        if (segment.endFrame != -1
            && (segment.endFrame < segment.startFrame
                || segment.endFrame >= duration))
        {
            detail::setError(
                error,
                label
                    + QStringLiteral("结束帧 %1 非法（必须为 -1 或位于 %2..%3）")
                          .arg(segment.endFrame)
                          .arg(segment.startFrame)
                          .arg(duration - 1));
            return false;
        }

        // Automatic segments end one frame before the next key/segment;
        // explicit endFrame is an advanced override and is used verbatim.
        const int effectiveEnd = solverStructuralEnd(entity, segment);
        if (effectiveEnd < segment.startFrame)
        {
            detail::setError(
                error,
                label + QStringLiteral("的结构结束帧早于起始帧"));
            return false;
        }
        effectiveEnds.append(effectiveEnd);

        for (const QString& trackName : drivenTracks(segment.effect))
        {
            const auto track = entity.tracks.constFind(trackName);
            if (track == entity.tracks.constEnd())
                continue;

            for (const Keyframe& keyframe : track.value().keys)
            {
                if (keyframe.frame > segment.startFrame
                    && keyframe.frame <= effectiveEnd)
                {
                    detail::setError(
                        error,
                        label + QStringLiteral("（%1）驱动属性 %2，但轨道在该段内仍有关键帧 %3")
                                    .arg(solverEffectName(segment.effect))
                                    .arg(trackName)
                                    .arg(keyframe.frame));
                    return false;
                }
            }
        }
    }

    for (int i = 0; i < entity.solvers.size(); ++i)
    {
        for (int j = i + 1; j < entity.solvers.size(); ++j)
        {
            const int aStart = entity.solvers.at(i).startFrame;
            const int aEnd = effectiveEnds.at(i);
            const int bStart = entity.solvers.at(j).startFrame;
            const int bEnd = effectiveEnds.at(j);
            if (aStart <= bEnd && bStart <= aEnd)
            {
                detail::setError(
                    error,
                    QStringLiteral("实体 %1 的求解段 %2 与 %3 重叠")
                        .arg(entity.uuid)
                        .arg(i)
                        .arg(j));
                return false;
            }
        }
    }
    return true;
}

bool validateEntity(const Entity& entity, QString* error)
{
    if (!isValidUuid(entity.uuid))
    {
        detail::setError(
            error,
            entity.uuid.isEmpty()
                ? QStringLiteral("实体 UUID 不能为空")
                : QStringLiteral("实体 UUID 格式非法：%1").arg(entity.uuid));
        return false;
    }

    if (entity.start < 0)
    {
        detail::setError(
            error,
            QStringLiteral("实体 %1 的 start 不能小于 0").arg(entity.uuid));
        return false;
    }

    if (entity.end <= entity.start)
    {
        detail::setError(
            error,
            QStringLiteral("实体 %1 的 end 必须大于 start").arg(entity.uuid));
        return false;
    }

    if (entity.row < 0)
    {
        detail::setError(
            error,
            QStringLiteral("实体 %1 的 row 不能小于 0").arg(entity.uuid));
        return false;
    }

    if (!std::isfinite(entity.transform.opacity)
        || entity.transform.opacity < 0.0
        || entity.transform.opacity > 1.0)
    {
        detail::setError(
            error,
            QStringLiteral("实体 %1 的 transform.opacity 必须在 0..1 之间"
                           "（当前 %2）")
                .arg(entity.uuid)
                .arg(entity.transform.opacity));
        return false;
    }

    QString contentError;
    if (!validateContent(entity.content, &contentError))
    {
        detail::setError(
            error,
            QStringLiteral("实体 %1 的内容非法：%2")
                .arg(entity.uuid, contentError));
        return false;
    }

    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        if (!validateTrack(it.value(), entity.duration(), it.key(), error))
            return false;
    }

    return validateSolvers(entity, error);
}

} // namespace pvf::model
