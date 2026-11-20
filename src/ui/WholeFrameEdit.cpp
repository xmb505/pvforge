#include "WholeFrameEdit.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Keyframe.h"

#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace pvf::ui::wholeframe {

namespace {

bool isCommonProperty(const QString& property)
{
    static const QStringList common = {
        QStringLiteral("x"),       QStringLiteral("y"),
        QStringLiteral("rotation"), QStringLiteral("scaleX"),
        QStringLiteral("scaleY"),  QStringLiteral("opacity"),
        QStringLiteral("visible"), QStringLiteral("physicsInteract")};
    return common.contains(property);
}

bool isCommonBool(const QString& property)
{
    return property == QLatin1String("visible")
        || property == QLatin1String("physicsInteract");
}

model::KeyframeSample sampleFromKey(const model::Keyframe& key)
{
    model::KeyframeSample sample;
    sample.x = key.x;
    sample.y = key.y;
    sample.rotation = key.rotation;
    sample.scaleX = key.scaleX;
    sample.scaleY = key.scaleY;
    sample.opacity = key.opacity;
    sample.visible = key.visible;
    sample.physicsInteract = key.physicsInteract;
    sample.content = key.content;
    sample.htmlVars = key.htmlVars;
    return sample;
}

model::Keyframe keyFromSample(const model::KeyframeSample& sample, int frame)
{
    model::Keyframe key;
    key.frame = frame;
    key.x = sample.x;
    key.y = sample.y;
    key.rotation = sample.rotation;
    key.scaleX = sample.scaleX;
    key.scaleY = sample.scaleY;
    key.opacity = sample.opacity;
    key.visible = sample.visible;
    key.physicsInteract = sample.physicsInteract;
    key.content = sample.content;
    key.htmlVars = sample.htmlVars;
    return key;
}

std::optional<solver::Sample> solverSampleAt(EditorSession* session,
                                             const model::Entity& entity,
                                             int frame)
{
    if (!session)
        return std::nullopt;
    const model::SolverSegment* segment = anim::solverAt(entity, frame);
    if (!segment)
        return std::nullopt;
    const solver::Result* result =
        session->solverResultFor(entity.uuid, segment->id);
    if (!result || result->samples.isEmpty())
        return std::nullopt;
    for (const solver::Sample& sample : result->samples)
        if (sample.frame == frame)
            return sample;
    if (frame > result->samples.last().frame)
        return result->samples.last();
    return std::nullopt;
}

} // namespace

bool canWriteKeyAt(const model::Entity& entity, int frame, QString* reason)
{
    if (frame < 0 || frame > entity.duration())
    {
        if (reason)
            *reason = QStringLiteral("播放针不在实体范围内：相对帧 %1，允许 0..%2")
                          .arg(frame)
                          .arg(entity.duration());
        return false;
    }
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.endFrame < 0)
            continue;
        if (frame > solver.startFrame && frame <= solver.endFrame)
        {
            if (reason)
                *reason = QStringLiteral(
                    "第 %1 帧由显式求解段驱动，不能创建普通关键帧")
                              .arg(frame);
            return false;
        }
    }
    return true;
}

model::Keyframe keyAt(const model::Entity& entity, int frame,
                      EditorSession* session)
{
    if (const model::Keyframe* existing =
            model::keyframeAt(entity.keyframes, frame))
    {
        model::Keyframe key = *existing;
        fillHtmlVariableDefaults(key, htmlVariableDefaults(session, entity));
        return key;
    }

    model::KeyframeSample sample = model::entitySampleAt(entity, frame);
    if (const auto pose = solverSampleAt(session, entity, frame))
    {
        sample.x = pose->pose.position.x();
        sample.y = pose->pose.position.y();
        sample.rotation = pose->pose.rotation;
        sample.scaleX *= pose->pose.scale;
        sample.scaleY *= pose->pose.scale;
    }

    model::Keyframe key = keyFromSample(sample, frame);
    fillHtmlVariableDefaults(key, htmlVariableDefaults(session, entity));
    return key;
}

bool setValue(model::Keyframe& key, const QString& property, double value)
{
    if (property == QLatin1String("x"))
        key.x = value;
    else if (property == QLatin1String("y"))
        key.y = value;
    else if (property == QLatin1String("rotation"))
        key.rotation = value;
    else if (property == QLatin1String("scaleX"))
        key.scaleX = value;
    else if (property == QLatin1String("scaleY"))
        key.scaleY = value;
    else if (property == QLatin1String("opacity"))
        key.opacity = std::clamp(value, 0.0, 1.0);
    else if (property == QLatin1String("visible"))
        key.visible = value != 0.0;
    else if (property == QLatin1String("physicsInteract"))
        key.physicsInteract = value != 0.0;
    else if (!isCommonProperty(property))
    {
        key.content.insert(property, value);
    }
    else
    {
        return false;
    }
    return true;
}

bool setBoolValue(model::Keyframe& key, const QString& property, bool value)
{
    if (property == QLatin1String("visible"))
    {
        key.visible = value;
        return true;
    }
    if (property == QLatin1String("physicsInteract"))
    {
        key.physicsInteract = value;
        return true;
    }
    return false;
}

void setHtmlVar(model::Keyframe& key, const QString& name, double value)
{
    key.htmlVars.insert(name, value);
}

std::optional<double> value(const model::Keyframe& key,
                            const QString& property)
{
    if (property == QLatin1String("x"))
        return key.x;
    if (property == QLatin1String("y"))
        return key.y;
    if (property == QLatin1String("rotation"))
        return key.rotation;
    if (property == QLatin1String("scaleX"))
        return key.scaleX;
    if (property == QLatin1String("scaleY"))
        return key.scaleY;
    if (property == QLatin1String("opacity"))
        return key.opacity;
    if (property == QLatin1String("visible"))
        return key.visible ? 1.0 : 0.0;
    if (property == QLatin1String("physicsInteract"))
        return key.physicsInteract ? 1.0 : 0.0;
    if (key.content.value(property).isDouble())
        return key.content.value(property).toDouble();
    return std::nullopt;
}

bool setStaticValue(model::Entity& entity, const QString& property,
                    double value)
{
    model::Transform& transform = entity.transform;
    if (property == QLatin1String("x"))
    {
        transform.x = value;
        return true;
    }
    if (property == QLatin1String("y"))
    {
        transform.y = value;
        return true;
    }
    if (property == QLatin1String("rotation"))
    {
        transform.rotation = value;
        return true;
    }
    if (property == QLatin1String("scaleX"))
    {
        transform.scaleX = value;
        return true;
    }
    if (property == QLatin1String("scaleY"))
    {
        transform.scaleY = value;
        return true;
    }
    if (property == QLatin1String("opacity"))
    {
        transform.opacity = std::clamp(value, 0.0, 1.0);
        return true;
    }
    if (property == QLatin1String("visible"))
    {
        entity.visible = value != 0.0;
        return true;
    }
    if (property == QLatin1String("physicsInteract"))
    {
        entity.physicsInteract = value != 0.0;
        return true;
    }

    if (!model::isAnimationKey(entity.content, property))
        return false;
    QJsonObject values = model::animationValues(entity.content);
    values.insert(property, value);
    model::applyAnimationValues(values, &entity.content);
    return true;
}

bool setStaticBoolValue(model::Entity& entity, const QString& property,
                        bool value)
{
    if (property == QLatin1String("visible"))
    {
        entity.visible = value;
        return true;
    }
    if (property == QLatin1String("physicsInteract"))
    {
        entity.physicsInteract = value;
        return true;
    }
    return false;
}

QJsonObject htmlVariableDefaults(EditorSession* session,
                                 const model::Entity& entity)
{
    QJsonObject defaults;
    if (!session || entity.content.kind != ContentKind::Asset
        || entity.content.asset.type != AssetType::Html)
        return defaults;
    const auto stored = session->assets().get(entity.content.asset.hash);
    if (!stored)
        return defaults;
    const QJsonObject variables =
        stored->metadata.value(QStringLiteral("variables")).toObject();
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
    {
        QJsonValue defaultValue = it.value();
        if (defaultValue.isObject())
        {
            const QJsonObject descriptor = defaultValue.toObject();
            defaultValue = descriptor.value(QStringLiteral("default"));
            if (!defaultValue.isDouble()
                && descriptor.value(QStringLiteral("type")).toString()
                       != QLatin1String("number"))
                continue;
        }
        if (defaultValue.isDouble())
            defaults.insert(it.key(), defaultValue);
    }
    return defaults;
}

void fillHtmlVariableDefaults(model::Keyframe& key,
                              const QJsonObject& defaults)
{
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it)
        if (!key.htmlVars.contains(it.key()))
            key.htmlVars.insert(it.key(), it.value());
}

} // namespace pvf::ui::wholeframe
