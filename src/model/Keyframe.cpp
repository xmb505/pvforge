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
           && note == other.note;
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

    if (keyframe.easing.type == QLatin1String("bezier"))
    {
        QJsonObject easing;
        easing.insert(QStringLiteral("type"), keyframe.easing.type);
        easing.insert(QStringLiteral("inX"), keyframe.easing.inX);
        easing.insert(QStringLiteral("inY"), keyframe.easing.inY);
        easing.insert(QStringLiteral("outX"), keyframe.easing.outX);
        easing.insert(QStringLiteral("outY"), keyframe.easing.outY);
        object.insert(QStringLiteral("easing"), easing);
    }
    else
    {
        const QString type = keyframe.easing.type.isEmpty()
                                 ? QStringLiteral("linear")
                                 : keyframe.easing.type;
        object.insert(QStringLiteral("easing"), type);
    }

    if (!keyframe.note.isEmpty())
        object.insert(QStringLiteral("note"), keyframe.note);
    return object;
}

std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         QString* error)
{
    Keyframe keyframe;
    keyframe.frame = detail::jsonInt(object, QStringLiteral("frame"), 0);
    keyframe.value = detail::jsonDouble(object, QStringLiteral("value"), 0.0);
    keyframe.note = detail::jsonString(object, QStringLiteral("note"));

    const QJsonValue easingValue = object.value(QStringLiteral("easing"));
    if (easingValue.isUndefined() || easingValue.isNull())
    {
        keyframe.easing = EasingSpec{};
    }
    else if (easingValue.isString())
    {
        keyframe.easing.type = easingValue.toString();
    }
    else if (easingValue.isObject())
    {
        const QJsonObject easingObject = easingValue.toObject();
        keyframe.easing.type = detail::jsonString(
            easingObject, QStringLiteral("type"), QStringLiteral("bezier"));
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

    return keyframe;
}

QJsonArray toJson(const Track& track)
{
    QJsonArray array;
    for (const Keyframe& keyframe : track.keys)
        array.append(toJson(keyframe));
    return array;
}

std::optional<Track> trackFromJson(const QJsonValue& value, QString* error)
{
    if (value.isUndefined() || value.isNull())
        return Track{};

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

        const auto keyframe = keyframeFromJson(array.at(i).toObject(), error);
        if (!keyframe)
            return std::nullopt;
        track.keys.append(*keyframe);
    }
    return track;
}

} // namespace pvf::model
