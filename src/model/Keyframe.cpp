#include "Keyframe.h"

#include "JsonUtil.h"

#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf::model {

bool Keyframe::operator==(const Keyframe& other) const
{
    return frame == other.frame
           && qFuzzyCompare(x + 1.0, other.x + 1.0)
           && qFuzzyCompare(y + 1.0, other.y + 1.0)
           && qFuzzyCompare(rotation + 1.0, other.rotation + 1.0)
           && qFuzzyCompare(scaleX + 1.0, other.scaleX + 1.0)
           && qFuzzyCompare(scaleY + 1.0, other.scaleY + 1.0)
           && qFuzzyCompare(opacity + 1.0, other.opacity + 1.0)
           && visible == other.visible
           && physicsInteract == other.physicsInteract
           && content == other.content
           && htmlVars == other.htmlVars
           && easing == other.easing
           && note == other.note
           && extras == other.extras;
}

namespace {

// Index of the last keyframe whose frame is <= `frame`, or -1 when the query is
// before the first key.  The vector must be sorted by frame.
int lastIndexAtOrBefore(const QVector<Keyframe>& keys, double frame)
{
    int low = 0;
    int high = keys.size();
    while (low < high)
    {
        const int middle = low + (high - low) / 2;
        if (double(keys.at(middle).frame) <= frame)
            low = middle + 1;
        else
            high = middle;
    }
    return low - 1;
}

int insertionIndex(const QVector<Keyframe>& keys, int frame)
{
    int low = 0;
    int high = keys.size();
    while (low < high)
    {
        const int middle = low + (high - low) / 2;
        if (keys.at(middle).frame < frame)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

// Copy of `base` with every numeric key of `override` replacing it.  Non-number
// values are ignored (load-time validation rejects them).
QJsonObject mergeNumbers(const QJsonObject& base, const QJsonObject& override)
{
    QJsonObject result = base;
    for (auto it = override.constBegin(); it != override.constEnd(); ++it)
    {
        if (it.value().isDouble())
            result.insert(it.key(), it.value());
    }
    return result;
}

// Fills keys missing from `right` with the left value: a parameter that exists
// only on the left keyframe is held, not dropped.
void holdMissingNumbers(const QJsonObject& left, QJsonObject* right)
{
    for (auto it = left.constBegin(); it != left.constEnd(); ++it)
    {
        if (!right->contains(it.key()) && it.value().isDouble())
            right->insert(it.key(), it.value());
    }
}

QJsonObject interpolateNumbers(const QJsonObject& left,
                               const QJsonObject& right, double t)
{
    QJsonObject merged = mergeNumbers(left, right);
    for (auto it = merged.begin(); it != merged.end(); ++it)
    {
        if (!it.value().isDouble())
            continue;
        const QJsonValue leftValue = left.value(it.key());
        const QJsonValue rightValue = right.value(it.key());
        if (!leftValue.isDouble() || !rightValue.isDouble())
            continue;
        const double from = leftValue.toDouble();
        const double to = rightValue.toDouble();
        it.value() = from + (to - from) * t;
    }
    return merged;
}

void copyScalars(const Keyframe& source, KeyframeSample* sample)
{
    sample->x = source.x;
    sample->y = source.y;
    sample->rotation = source.rotation;
    sample->scaleX = source.scaleX;
    sample->scaleY = source.scaleY;
    sample->opacity = source.opacity;
    sample->visible = source.visible;
    sample->physicsInteract = source.physicsInteract;
}

} // namespace

KeyframeSample evaluateKeyframes(const QVector<Keyframe>& keyframes,
                                 const KeyframeSample& fallback, double frame)
{
    KeyframeSample sample = fallback;
    if (keyframes.isEmpty())
        return sample;

    const int index = lastIndexAtOrBefore(keyframes, frame);
    if (index < 0)
    {
        // Held constant before the first key.
        const Keyframe& first = keyframes.first();
        copyScalars(first, &sample);
        sample.content = mergeNumbers(fallback.content, first.content);
        sample.htmlVars = mergeNumbers(fallback.htmlVars, first.htmlVars);
        return sample;
    }

    const Keyframe& left = keyframes.at(index);
    copyScalars(left, &sample);
    sample.content = mergeNumbers(fallback.content, left.content);
    sample.htmlVars = mergeNumbers(fallback.htmlVars, left.htmlVars);

    if (double(left.frame) == frame || index + 1 >= keyframes.size())
        return sample;

    const Keyframe& right = keyframes.at(index + 1);
    const int span = right.frame - left.frame;
    if (span <= 0)
        return sample;

    const double raw = (frame - double(left.frame)) / double(span);
    const double eased = easedProgress(left.easing, raw);

    sample.x = left.x + (right.x - left.x) * eased;
    sample.y = left.y + (right.y - left.y) * eased;
    sample.rotation = left.rotation + (right.rotation - left.rotation) * eased;
    sample.scaleX = left.scaleX + (right.scaleX - left.scaleX) * eased;
    sample.scaleY = left.scaleY + (right.scaleY - left.scaleY) * eased;
    sample.opacity = left.opacity + (right.opacity - left.opacity) * eased;

    QJsonObject rightContent = mergeNumbers(fallback.content, right.content);
    holdMissingNumbers(sample.content, &rightContent);
    sample.content = interpolateNumbers(sample.content, rightContent, eased);

    QJsonObject rightVars = mergeNumbers(fallback.htmlVars, right.htmlVars);
    holdMissingNumbers(sample.htmlVars, &rightVars);
    sample.htmlVars = interpolateNumbers(sample.htmlVars, rightVars, eased);
    return sample;
}

const Keyframe* keyframeAt(const QVector<Keyframe>& keyframes, int frame)
{
    int low = 0;
    int high = keyframes.size();
    while (low < high)
    {
        const int middle = low + (high - low) / 2;
        if (keyframes.at(middle).frame < frame)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < keyframes.size() && keyframes.at(low).frame == frame)
        return &keyframes.at(low);
    return nullptr;
}

bool hasKeyframeAt(const QVector<Keyframe>& keyframes, int frame)
{
    return keyframeAt(keyframes, frame) != nullptr;
}

void sortKeyframes(QVector<Keyframe>& keyframes)
{
    std::stable_sort(keyframes.begin(), keyframes.end(),
                     [](const Keyframe& a, const Keyframe& b) {
                         return a.frame < b.frame;
                     });
}

void upsertKeyframe(QVector<Keyframe>& keyframes, const Keyframe& keyframe)
{
    const int index = insertionIndex(keyframes, keyframe.frame);
    if (index < keyframes.size() && keyframes.at(index).frame == keyframe.frame)
        keyframes[index] = keyframe;
    else
        keyframes.insert(index, keyframe);
}

bool removeKeyframe(QVector<Keyframe>& keyframes, int frame)
{
    const int index = insertionIndex(keyframes, frame);
    if (index >= keyframes.size() || keyframes.at(index).frame != frame)
        return false;
    keyframes.remove(index);
    return true;
}

QJsonObject toJson(const Keyframe& keyframe)
{
    QJsonObject object;
    object.insert(QStringLiteral("frame"), keyframe.frame);
    object.insert(QStringLiteral("x"), keyframe.x);
    object.insert(QStringLiteral("y"), keyframe.y);
    object.insert(QStringLiteral("rotation"), keyframe.rotation);
    object.insert(QStringLiteral("scaleX"), keyframe.scaleX);
    object.insert(QStringLiteral("scaleY"), keyframe.scaleY);
    object.insert(QStringLiteral("opacity"), keyframe.opacity);
    object.insert(QStringLiteral("visible"), keyframe.visible);
    object.insert(QStringLiteral("physicsInteract"), keyframe.physicsInteract);
    object.insert(QStringLiteral("content"), keyframe.content);
    object.insert(QStringLiteral("htmlVars"), keyframe.htmlVars);

    const QString type = canonicalEasingName(keyframe.easing.type);
    if (type == QLatin1String("bezier"))
    {
        QJsonObject easing;
        easing.insert(QStringLiteral("type"), type);
        easing.insert(QStringLiteral("inX"), keyframe.easing.inX);
        easing.insert(QStringLiteral("inY"), keyframe.easing.inY);
        easing.insert(QStringLiteral("outX"), keyframe.easing.outX);
        easing.insert(QStringLiteral("outY"), keyframe.easing.outY);
        object.insert(QStringLiteral("easing"), easing);
    }
    else
    {
        object.insert(QStringLiteral("easing"), type);
    }

    if (!keyframe.note.isEmpty())
        object.insert(QStringLiteral("note"), keyframe.note);
    detail::mergeExtras(object, keyframe.extras);
    return object;
}

namespace {

bool readNumberObject(const QJsonObject& object, const QString& label,
                      const QString& path, QJsonObject* out, QString* error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!it.value().isDouble())
        {
            detail::setError(error,
                             QStringLiteral("%1.%2 必须是数值")
                                 .arg(path, it.key()));
            return false;
        }
        if (!std::isfinite(it.value().toDouble()))
        {
            detail::setError(error,
                             QStringLiteral("%1.%2 必须是有限数值")
                                 .arg(path, it.key()));
            return false;
        }
        out->insert(it.key(), it.value());
    }
    Q_UNUSED(label);
    return true;
}

} // namespace

std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         const ParseOptions& options,
                                         QString* error)
{
    if (!detail::requireField(object, QStringLiteral("frame"), options, error))
        return std::nullopt;

    // A whole-frame keyframe carries every public animation parameter.  Strict
    // opening (project files, MCP payloads) requires all of them; lenient mode
    // keeps defaults so old hand-written fixtures remain inspectable.
    for (const char* field : {"x", "y", "rotation", "scaleX", "scaleY",
                              "opacity", "visible", "physicsInteract"})
    {
        if (!detail::requireField(object, QString::fromLatin1(field), options,
                                  error))
            return std::nullopt;
    }

    Keyframe keyframe;
    keyframe.frame = detail::optionalInt(
        object, QStringLiteral("frame"), keyframe.frame, options);
    keyframe.x = detail::optionalDouble(
        object, QStringLiteral("x"), keyframe.x, options);
    keyframe.y = detail::optionalDouble(
        object, QStringLiteral("y"), keyframe.y, options);
    keyframe.rotation = detail::optionalDouble(
        object, QStringLiteral("rotation"), keyframe.rotation, options);
    keyframe.scaleX = detail::optionalDouble(
        object, QStringLiteral("scaleX"), keyframe.scaleX, options);
    keyframe.scaleY = detail::optionalDouble(
        object, QStringLiteral("scaleY"), keyframe.scaleY, options);
    keyframe.opacity = detail::optionalDouble(
        object, QStringLiteral("opacity"), keyframe.opacity, options);
    keyframe.visible = detail::optionalBool(
        object, QStringLiteral("visible"), keyframe.visible, options);
    keyframe.physicsInteract = detail::optionalBool(
        object, QStringLiteral("physicsInteract"), keyframe.physicsInteract,
        options);
    keyframe.note = detail::optionalString(
        object, QStringLiteral("note"), keyframe.note, options);

    const QJsonValue contentValue = object.value(QStringLiteral("content"));
    if (!contentValue.isUndefined() && !contentValue.isNull())
    {
        if (!contentValue.isObject())
        {
            detail::setError(error,
                             detail::pathOf(options, QStringLiteral("content"))
                                 + QStringLiteral(" 必须是对象"));
            return std::nullopt;
        }
        if (!readNumberObject(contentValue.toObject(), QStringLiteral("内容"),
                              detail::pathOf(options, QStringLiteral("content")),
                              &keyframe.content, error))
            return std::nullopt;
    }
    else
    {
        detail::warnDefaulted(object, QStringLiteral("content"), options);
    }

    const QJsonValue varsValue = object.value(QStringLiteral("htmlVars"));
    if (!varsValue.isUndefined() && !varsValue.isNull())
    {
        if (!varsValue.isObject())
        {
            detail::setError(error,
                             detail::pathOf(options, QStringLiteral("htmlVars"))
                                 + QStringLiteral(" 必须是对象"));
            return std::nullopt;
        }
        if (!readNumberObject(varsValue.toObject(), QStringLiteral("HTML 变量"),
                              detail::pathOf(options, QStringLiteral("htmlVars")),
                              &keyframe.htmlVars, error))
            return std::nullopt;
    }
    else
    {
        detail::warnDefaulted(object, QStringLiteral("htmlVars"), options);
    }

    const QJsonValue easingValue = object.value(QStringLiteral("easing"));
    if (easingValue.isUndefined() || easingValue.isNull())
    {
        detail::warnDefaulted(object, QStringLiteral("easing"), options);
        keyframe.easing = EasingSpec{};
    }
    else if (easingValue.isString())
    {
        keyframe.easing.type = canonicalEasingName(easingValue.toString());
    }
    else if (easingValue.isObject())
    {
        const QJsonObject easingObject = easingValue.toObject();
        keyframe.easing.type = canonicalEasingName(detail::jsonString(
            easingObject, QStringLiteral("type"), QStringLiteral("bezier")));
        keyframe.easing.inX = detail::jsonDouble(
            easingObject, QStringLiteral("inX"), keyframe.easing.inX);
        keyframe.easing.inY = detail::jsonDouble(
            easingObject, QStringLiteral("inY"), keyframe.easing.inY);
        keyframe.easing.outX = detail::jsonDouble(
            easingObject, QStringLiteral("outX"), keyframe.easing.outX);
        keyframe.easing.outY = detail::jsonDouble(
            easingObject, QStringLiteral("outY"), keyframe.easing.outY);
    }
    else
    {
        detail::setError(error, QStringLiteral("关键帧 easing 必须是字符串或对象"));
        return std::nullopt;
    }

    keyframe.extras = detail::extrasFrom(
        object, {QStringLiteral("frame"), QStringLiteral("x"),
                 QStringLiteral("y"), QStringLiteral("rotation"),
                 QStringLiteral("scaleX"), QStringLiteral("scaleY"),
                 QStringLiteral("opacity"), QStringLiteral("visible"),
                 QStringLiteral("physicsInteract"), QStringLiteral("content"),
                 QStringLiteral("htmlVars"), QStringLiteral("easing"),
                 QStringLiteral("note")});
    return keyframe;
}

std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         QString* error)
{
    return keyframeFromJson(object, ParseOptions{}, error);
}

QJsonArray toJson(const QVector<Keyframe>& keyframes)
{
    QJsonArray array;
    for (const Keyframe& keyframe : keyframes)
        array.append(toJson(keyframe));
    return array;
}

std::optional<QVector<Keyframe>> keyframesFromJson(
    const QJsonValue& value, const ParseOptions& options,
    const Content* content, int duration, QString* error)
{
    if (value.isUndefined() || value.isNull())
    {
        detail::warn(options, QStringLiteral("缺少字段 %1，使用空关键帧")
                                  .arg(options.prefix));
        return QVector<Keyframe>{};
    }

    if (!value.isArray())
    {
        detail::setError(
            error,
            QStringLiteral("旧分轨工程格式已废弃：%1 必须是整帧关键帧数组")
                .arg(options.prefix.isEmpty() ? QStringLiteral("keyframes")
                                              : options.prefix));
        return std::nullopt;
    }

    QVector<Keyframe> keyframes;
    QSet<int> frames;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i)
    {
        if (!array.at(i).isObject())
        {
            detail::setError(error, QStringLiteral("第 %1 个关键帧不是对象").arg(i));
            return std::nullopt;
        }

        ParseOptions keyOptions = options;
        keyOptions.prefix = options.prefix + QStringLiteral("[%1]").arg(i);
        const auto keyframe =
            keyframeFromJson(array.at(i).toObject(), keyOptions, error);
        if (!keyframe)
            return std::nullopt;

        if (frames.contains(keyframe->frame))
        {
            detail::setError(
                error,
                QStringLiteral("第 %1 帧存在重复的整帧关键帧").arg(keyframe->frame));
            return std::nullopt;
        }
        frames.insert(keyframe->frame);
        keyframes.append(*keyframe);
    }

    sortKeyframes(keyframes);

    if (duration > 0)
    {
        const QString durationError = QStringLiteral("时长 %1").arg(duration);
        for (const Keyframe& keyframe : keyframes)
        {
            if (keyframe.frame < 0 || keyframe.frame >= duration)
            {
                detail::setError(
                    error,
                    QStringLiteral("关键帧帧号 %1 超出允许范围 0..%2（%3）")
                        .arg(keyframe.frame)
                        .arg(duration - 1)
                        .arg(durationError));
                return std::nullopt;
            }
        }
    }

    if (content)
    {
        for (const Keyframe& keyframe : keyframes)
        {
            if (!validateKeyframeContent(*content, keyframe.content, error)
                || !validateKeyframeHtmlVars(keyframe.htmlVars, error))
                return std::nullopt;
        }
    }

    return keyframes;
}

std::optional<QVector<Keyframe>> keyframesFromJson(const QJsonValue& value,
                                                   QString* error)
{
    return keyframesFromJson(value, ParseOptions{}, nullptr, -1, error);
}

bool validateKeyframes(const QVector<Keyframe>& keyframes, int duration,
                       const Content& content, QString* error)
{
    if (duration <= 0)
    {
        detail::setError(error, QStringLiteral("实体时长必须大于 0"));
        return false;
    }

    int previous = -1;
    bool hasPrevious = false;
    QSet<int> seenFrames;
    for (const Keyframe& keyframe : keyframes)
    {
        if (keyframe.frame < 0 || keyframe.frame >= duration)
        {
            detail::setError(
                error,
                QStringLiteral("关键帧帧号 %1 超出允许范围 0..%2")
                    .arg(keyframe.frame)
                    .arg(duration - 1));
            return false;
        }
        if (seenFrames.contains(keyframe.frame))
        {
            detail::setError(
                error,
                QStringLiteral("第 %1 帧存在重复的整帧关键帧").arg(keyframe.frame));
            return false;
        }
        seenFrames.insert(keyframe.frame);
        if (hasPrevious && keyframe.frame < previous)
        {
            detail::setError(
                error,
                QStringLiteral("整帧关键帧必须按帧号严格升序（%1 在 %2 之后）")
                    .arg(keyframe.frame)
                    .arg(previous));
            return false;
        }
        if (!std::isfinite(keyframe.x) || !std::isfinite(keyframe.y)
            || !std::isfinite(keyframe.rotation)
            || !std::isfinite(keyframe.scaleX)
            || !std::isfinite(keyframe.scaleY)
            || !std::isfinite(keyframe.opacity))
        {
            detail::setError(
                error,
                QStringLiteral("关键帧 %1 的动画数值必须是有限值")
                    .arg(keyframe.frame));
            return false;
        }
        if (!std::isfinite(keyframe.opacity) || keyframe.opacity < 0.0
            || keyframe.opacity > 1.0)
        {
            detail::setError(
                error,
                QStringLiteral("关键帧 %1 的 opacity 必须在 0..1 之间")
                    .arg(keyframe.frame));
            return false;
        }
        if (!validateKeyframeContent(content, keyframe.content, error)
            || !validateKeyframeHtmlVars(keyframe.htmlVars, error))
            return false;

        previous = keyframe.frame;
        hasPrevious = true;
    }
    return true;
}

} // namespace pvf::model
