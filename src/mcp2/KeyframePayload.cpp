#include "mcp2/KeyframePayload.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf::mcp2::detail {

namespace {

const QStringList& keyframeFields()
{
    static const QStringList fields = {
        QStringLiteral("frame"),     QStringLiteral("x"),
        QStringLiteral("y"),         QStringLiteral("rotation"),
        QStringLiteral("scaleX"),    QStringLiteral("scaleY"),
        QStringLiteral("opacity"),   QStringLiteral("visible"),
        QStringLiteral("physicsInteract"), QStringLiteral("content"),
        QStringLiteral("htmlVars"),  QStringLiteral("easing"),
        QStringLiteral("note"),
    };
    return fields;
}

Result unknownField(const QString& label, const QString& field)
{
    QJsonObject details;
    details.insert(QStringLiteral("field"), field);
    details.insert(QStringLiteral("allowedFields"),
                   QJsonArray::fromStringList(keyframeFields()));
    return failure(
        QStringLiteral("invalid_argument"),
        QStringLiteral("%1 含未知字段 %2：整帧关键帧只接受 "
                       "frame/x/y/rotation/scaleX/scaleY/opacity/visible/"
                       "physicsInteract/content/htmlVars/easing/note，"
                       "没有 track 或单参数轨道")
            .arg(label, field),
        details);
}

} // namespace

Result parseWholeFrameKeyframe(const model::Content& content, int duration,
                               const QJsonObject& object,
                               const QString& label, model::Keyframe* out)
{
    if (duration <= 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体时长必须大于 0"));

    Result error;
    model::Keyframe keyframe;
    if (!readInt(object, "frame", &keyframe.frame, &error, true))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.%2").arg(label, error.message));
    if (keyframe.frame < 0 || keyframe.frame >= duration)
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), 0);
        range.insert(QStringLiteral("max"), duration - 1);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.frame 超出实体范围 0..%2")
                           .arg(label)
                           .arg(duration - 1),
                       details);
    }

    const struct
    {
        const char* name;
        double model::Keyframe::*member;
    } numbers[] = {
        {"x", &model::Keyframe::x},
        {"y", &model::Keyframe::y},
        {"rotation", &model::Keyframe::rotation},
        {"scaleX", &model::Keyframe::scaleX},
        {"scaleY", &model::Keyframe::scaleY},
        {"opacity", &model::Keyframe::opacity},
    };
    for (const auto& number : numbers)
    {
        if (!object.contains(QLatin1String(number.name)))
            return failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("%1 缺少动画参数 %2（整帧关键帧必须包含全部动画参数）")
                    .arg(label, QLatin1String(number.name)));
        double value = keyframe.*(number.member);
        if (!readDouble(object, number.name, &value, &error, true))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.%2").arg(label, error.message));
        if (!isFiniteNumber(value))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.%2 必须是有限数")
                               .arg(label, QLatin1String(number.name)));
        keyframe.*(number.member) = value;
    }
    if (keyframe.opacity < 0.0 || keyframe.opacity > 1.0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.opacity 必须在 0..1 之间").arg(label));

    for (const char* name : {"visible", "physicsInteract"})
    {
        if (!object.contains(QLatin1String(name)))
            return failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("%1 缺少动画参数 %2（整帧关键帧必须包含全部动画参数）")
                    .arg(label, QLatin1String(name)));
        bool value = name[0] == 'v' ? keyframe.visible : keyframe.physicsInteract;
        if (!readBool(object, name, &value, &error, true))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.%2").arg(label, error.message));
        if (name[0] == 'v')
            keyframe.visible = value;
        else
            keyframe.physicsInteract = value;
    }

    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!keyframeFields().contains(it.key()))
            return unknownField(label, it.key());
    }

    const QStringList animationKeys = model::animationKeys(content);
    const QJsonValue contentValue = object.value(QStringLiteral("content"));
    if (contentValue.isUndefined() || contentValue.isNull())
    {
        if (!animationKeys.isEmpty())
        {
            QJsonObject details;
            details.insert(QStringLiteral("missingFields"),
                           QJsonArray::fromStringList(animationKeys));
            details.insert(QStringLiteral("requiredFields"),
                           QJsonArray::fromStringList(animationKeys));
            return failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("%1 缺少 content（该内容类型的动画参数：%2）")
                    .arg(label, animationKeys.join(QStringLiteral(", "))),
                details);
        }
    }
    else if (!contentValue.isObject())
    {
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.content 必须是对象").arg(label));
    }
    else
    {
        const QJsonObject values = contentValue.toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        {
            if (!animationKeys.contains(it.key()))
                return failure(
                    QStringLiteral("invalid_argument"),
                    QStringLiteral("%1.content.%2 不是动画参数：定义参数不能写进"
                                   "关键帧（允许：%3）")
                        .arg(label, it.key(),
                             animationKeys.isEmpty()
                                 ? QStringLiteral("无")
                                 : animationKeys.join(QStringLiteral(", "))));
            if (!it.value().isDouble() || !isFiniteNumber(it.value().toDouble()))
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("%1.content.%2 必须是有限数")
                                   .arg(label, it.key()));
            keyframe.content.insert(it.key(), it.value());
        }
        // Report every missing animation parameter at once.  Returning the
        // first one forced text entities into up to seven retries (issue #146
        // P0-2).
        QStringList missing;
        for (const QString& key : animationKeys)
        {
            if (!values.contains(key))
                missing.append(key);
        }
        if (!missing.isEmpty())
        {
            QJsonObject details;
            details.insert(QStringLiteral("missingFields"),
                           QJsonArray::fromStringList(missing));
            details.insert(QStringLiteral("requiredFields"),
                           QJsonArray::fromStringList(animationKeys));
            return failure(
                QStringLiteral("invalid_argument"),
                QStringLiteral("%1.content 缺少动画参数：%2（该内容类型的全部动画"
                               "参数：%3）")
                    .arg(label, missing.join(QStringLiteral(", ")),
                         animationKeys.join(QStringLiteral(", "))),
                details);
        }
    }

    const QJsonValue varsValue = object.value(QStringLiteral("htmlVars"));
    if (!varsValue.isUndefined() && !varsValue.isNull())
    {
        if (!varsValue.isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.htmlVars 必须是对象").arg(label));
        const QJsonObject vars = varsValue.toObject();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        {
            if (!it.value().isDouble() || !isFiniteNumber(it.value().toDouble()))
                return failure(
                    QStringLiteral("invalid_argument"),
                    QStringLiteral("%1.htmlVars.%2 必须是有限数（非数字变量不进"
                                   "关键帧）")
                        .arg(label, it.key()));
            keyframe.htmlVars.insert(it.key(), it.value());
        }
    }

    if (object.contains(QStringLiteral("easing")))
    {
        QJsonValue normalized;
        if (!parseEasing(object.value(QStringLiteral("easing")),
                         &keyframe.easing, &normalized, &error))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1：%2").arg(label, error.message));
    }

    if (object.contains(QStringLiteral("note")))
    {
        QString note;
        if (!readString(object, "note", &note, &error, true))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.%2").arg(label, error.message));
        if (note.size() > 4096)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("%1.note 最长 4096 字符").arg(label));
        keyframe.note = note;
    }

    QString validationError;
    if (!model::validateKeyframeContent(content, keyframe.content,
                                        &validationError))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.content：%2").arg(label,
                                                            validationError));
    if (!model::validateKeyframeHtmlVars(keyframe.htmlVars, &validationError))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("%1.htmlVars：%2").arg(label,
                                                             validationError));

    *out = keyframe;
    return success();
}

Result parseWholeFrameKeyframes(const model::Content& content, int duration,
                                const QJsonValue& value,
                                QVector<model::Keyframe>* out)
{
    out->clear();
    if (value.isUndefined() || value.isNull())
        return success();
    if (!value.isArray())
        return failure(
            QStringLiteral("invalid_argument"),
            QStringLiteral("旧分轨工程格式已废弃：keyframes 必须是整帧关键帧数组，"
                           "没有 track/单参数轨道"));

    const QJsonArray array = value.toArray();
    QSet<int> frames;
    for (int i = 0; i < array.size(); ++i)
    {
        if (!array.at(i).isObject())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("keyframes[%1] 必须是对象").arg(i));
        model::Keyframe keyframe;
        Result parsed = parseWholeFrameKeyframe(
            content, duration, array.at(i).toObject(),
            QStringLiteral("keyframes[%1]").arg(i), &keyframe);
        if (!parsed.ok)
            return parsed;
        if (frames.contains(keyframe.frame))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("第 %1 帧存在重复的整帧关键帧")
                               .arg(keyframe.frame));
        frames.insert(keyframe.frame);
        out->append(keyframe);
    }

    model::sortKeyframes(*out);
    return success();
}

const model::SolverSegment* blockingSolver(const model::Entity& entity,
                                           int frame)
{
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (segment.endFrame < 0)
            continue;
        if (frame > segment.startFrame && frame <= segment.endFrame)
            return &segment;
    }
    return nullptr;
}

bool isSolvedFrame(const model::Entity& entity, int frame)
{
    return blockingSolver(entity, frame) != nullptr;
}

Result checkKeyframesAgainstSolvers(const model::Entity& entity,
                                    const QVector<model::Keyframe>& keyframes)
{
    for (const model::SolverSegment& segment : entity.solvers)
    {
        if (!model::hasKeyframeAt(keyframes, segment.startFrame))
        {
            QJsonObject details;
            details.insert(QStringLiteral("solverId"), segment.id);
            details.insert(QStringLiteral("startFrame"), segment.startFrame);
            return failure(
                QStringLiteral("conflict"),
                QStringLiteral("求解段起点第 %1 帧缺少整帧关键帧；求解起点必须"
                               "是一个普通整帧关键帧")
                    .arg(segment.startFrame),
                details);
        }

        if (segment.endFrame < 0)
            continue;

        QJsonArray conflicts;
        for (const model::Keyframe& keyframe : keyframes)
        {
            if (keyframe.frame <= segment.startFrame
                || keyframe.frame > segment.endFrame)
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("solverId"), segment.id);
            item.insert(QStringLiteral("frame"), keyframe.frame);
            item.insert(QStringLiteral("startFrame"), segment.startFrame);
            item.insert(QStringLiteral("endFrame"), segment.endFrame);
            conflicts.append(item);
        }
        if (!conflicts.isEmpty())
        {
            QJsonObject details;
            details.insert(QStringLiteral("conflictingFrames"), conflicts);
            return failure(
                QStringLiteral("conflict"),
                QStringLiteral("求解段 %1..%2 覆盖的帧（含结束帧）不能有普通"
                               "整帧关键帧")
                    .arg(segment.startFrame)
                    .arg(segment.endFrame),
                details);
        }
    }
    return success();
}

} // namespace pvf::mcp2::detail
