#include "Keyframe.h"

#include "JsonUtil.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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

namespace {

// True when `keys` is already ordered by frame, which is the normal state of
// an edited track (the timeline appends and inserts in order).
bool isFrameOrdered(const QVector<Keyframe>& keys)
{
    for (int i = 1; i < keys.size(); ++i)
    {
        if (keys.at(i).frame < keys.at(i - 1).frame)
            return false;
    }
    return true;
}

// Positions of `keys` ordered by frame, for the rare unsorted track. Only ints
// are sorted, so a badly ordered track never copies Keyframe (whose note and
// extras are refcounted) and never moves them around. `stable_sort` keeps the
// insertion order of keys that share a frame, matching the old sort-a-copy
// behaviour exactly. Thread-local so the buffer is reused across calls and
// every thread gets its own.
const QVector<int>& frameOrder(const QVector<Keyframe>& keys)
{
    thread_local QVector<int> order;
    order.resize(keys.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&keys](int a, int b) {
        return keys.at(a).frame < keys.at(b).frame;
    });
    return order;
}

// Binary search shared by both orders: `at(i)` returns the i-th key in frame
// order. Returns the value the linear scan used to return.
template <typename At>
double evaluateOrdered(const At& at, int count, double lookup, bool step)
{
    if (count == 1)
        return at(0).value;

    // Held constant before the first key and after the last key. A query that
    // lands exactly on the first frame takes the first key's value even when
    // several keys share that frame, which is what the old linear scan did.
    if (lookup <= at(0).frame)
        return at(0).value;
    if (lookup >= at(count - 1).frame)
        return at(count - 1).value;

    // First key whose frame is greater than the query; everything before it
    // holds the value at or before the query. Bounded by the checks above, so
    // a finite query leaves `low` in 1 .. count-1; a NaN query keeps low = 1
    // and interpolates from the first key, exactly like the old scan.
    int low = 1;
    int high = count;
    while (low < high)
    {
        const int middle = low + (high - low) / 2;
        if (at(middle).frame <= lookup)
            low = middle + 1;
        else
            high = middle;
    }

    const Keyframe& left = at(low - 1);
    if (left.frame == lookup)
        return left.value;
    if (step)
        return left.value;

    const Keyframe& right = at(low);
    const int span = right.frame - left.frame;
    if (span <= 0)
        return right.value;

    const double t = (lookup - left.frame) / double(span);
    return left.value + (right.value - left.value) * easedProgress(left.easing, t);
}

} // namespace

std::optional<double> evaluateTrack(const Track& track, double frame, bool step)
{
    const QVector<Keyframe>& keys = track.keys;
    if (keys.isEmpty())
        return std::nullopt;

    // Step tracks only change on integer frames: a fractional frame takes the
    // left value of the integer frame it sits in.
    const double lookup = step ? std::floor(frame) : frame;

    if (isFrameOrdered(keys))
    {
        return evaluateOrdered(
            [&keys](int index) -> const Keyframe& { return keys.at(index); },
            keys.size(), lookup, step);
    }

    const QVector<int>& order = frameOrder(keys);
    return evaluateOrdered(
        [&keys, &order](int index) -> const Keyframe& {
            return keys.at(order.at(index));
        },
        order.size(), lookup, step);
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
