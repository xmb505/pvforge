#pragma once

#include "Easing.h"
#include "JsonUtil.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace pvf::model {

struct Keyframe
{
    int frame = 0;
    double value = 0.0;
    EasingSpec easing;
    QString note;
    // Unknown keys, preserved verbatim across a load/save cycle.
    QJsonObject extras;

    bool operator==(const Keyframe& other) const;
    bool operator!=(const Keyframe& other) const { return !(*this == other); }
};

// Keyframes are stored in entity-relative frames. Evaluation sorts a copy, so
// keys do not have to be kept in order while editing.
struct Track
{
    QVector<Keyframe> keys;

    bool isEmpty() const { return keys.isEmpty(); }
    std::optional<double> valueAt(int frame, bool step = false) const;
    bool operator==(const Track& other) const { return keys == other.keys; }
    bool operator!=(const Track& other) const { return !(*this == other); }
};

// Returns nullopt for an empty track. Values are held constant before the
// first key and after the last key. step=true holds the left value.
//
// Keyframe frames stay integers, but the query may be fractional so exports can
// sample 2x/4x the project fps.  Numeric tracks interpolate between the
// surrounding keys; step tracks (visible/physicsInteract) take the left value of
// floor(frame).
std::optional<double> evaluateTrack(const Track& track, double frame,
                                    bool step = false);
std::optional<double> evaluateTrack(const Track& track, int frame,
                                    bool step = false);

QJsonObject toJson(const Keyframe& keyframe);
std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         const ParseOptions& options,
                                         QString* error = nullptr);
std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         QString* error = nullptr);

QJsonArray toJson(const Track& track);
std::optional<Track> trackFromJson(const QJsonValue& value,
                                   const ParseOptions& options,
                                   QString* error = nullptr);
std::optional<Track> trackFromJson(const QJsonValue& value,
                                   QString* error = nullptr);

} // namespace pvf::model
