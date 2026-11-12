#include "Keyframe.h"

#include "JsonUtil.h"

#include <algorithm>
#include <cmath>

namespace pvf::model {

bool Keyframe::operator==(const Keyframe& other) const
{
    return frame == other.frame
           && qFuzzyCompare(value + 1.0, other.value + 1.0)
           && easing == other.easing
           && note == other.note
           && extras == other.extras;
}

std::optional<double> evaluateTrack(const Track& track, int frame, bool step)
{
    return evaluateTrack(track, double(frame), step);
}

std::optional<double> evaluateTrack(const Track& track, double frame, bool step)
{
    if (track.keys.isEmpty())
        return std::nullopt;

    QVector<Keyframe> sorted = track.keys;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Keyframe& a, const Keyframe& b) {
                         return a.frame < b.frame;
                     });

    // Step tracks only change on integer frames: a fractional frame takes the
    // left value of the integer frame it sits in.
    const double lookup = step ? std::floor(frame) : frame;

    if (lookup <= sorted.first().frame)
        return sorted.first().value;
    if (lookup >= sorted.last().frame)
        return sorted.last().value;

    int index = 0;
    for (int i = 0; i < sorted.size(); ++i)
    {
        if (sorted.at(i).frame <= lookup)
            index = i;
        else
            break;
    }

    const Keyframe& left = sorted.at(index);
    if (left.frame == lookup)
        return left.value;

    const Keyframe& right = sorted.at(index + 1);
    if (step)
        return left.value;

    const int span = right.frame - left.frame;
    if (span <= 0)
        return right.value;

    const double t = (lookup - left.frame) / double(span);
    return left.value + (right.value - left.value) * easedProgress(left.easing, t);
}

std::optional<double> Track::valueAt(int frame, bool step) const
{
    return evaluateTrack(*this, frame, step);
}

QJsonObject toJson(const Keyframe& keyframe)
{
    QJsonObject object;
    object.insert(QStringLiteral("frame"), keyframe.frame);
    object.insert(QStringLiteral("value"), keyframe.value);

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

std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         const ParseOptions& options,
                                         QString* error)
{
    if (!detail::requireField(object, QStringLiteral("frame"), options, error)
        || !detail::requireField(object, QStringLiteral("value"), options,
                                 error))
    {
        return std::nullopt;
    }

    Keyframe keyframe;
    keyframe.frame = detail::optionalInt(
        object, QStringLiteral("frame"), keyframe.frame, options);
    keyframe.value = detail::optionalDouble(
        object, QStringLiteral("value"), keyframe.value, options);
    keyframe.note = detail::optionalString(
        object, QStringLiteral("note"), keyframe.note, options);

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
        object, {QStringLiteral("frame"), QStringLiteral("value"),
                 QStringLiteral("easing"), QStringLiteral("note")});
    return keyframe;
}

std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         QString* error)
{
    return keyframeFromJson(object, ParseOptions{}, error);
}

QJsonArray toJson(const Track& track)
{
    QJsonArray array;
    for (const Keyframe& keyframe : track.keys)
        array.append(toJson(keyframe));
    return array;
}

std::optional<Track> trackFromJson(const QJsonValue& value,
                                   const ParseOptions& options,
                                   QString* error)
{
    if (value.isUndefined() || value.isNull())
    {
        detail::warn(options, QStringLiteral("缺少字段 %1，使用空轨道")
                                  .arg(options.prefix));
        return Track{};
    }

    if (!value.isArray())
    {
        detail::setError(error, QStringLiteral("轨道必须是关键帧数组"));
        return std::nullopt;
    }

    Track track;
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
        track.keys.append(*keyframe);
    }
    return track;
}

std::optional<Track> trackFromJson(const QJsonValue& value, QString* error)
{
    return trackFromJson(value, ParseOptions{}, error);
}

} // namespace pvf::model
