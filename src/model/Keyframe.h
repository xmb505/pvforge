#pragma once

#include "Content.h"
#include "Easing.h"
#include "JsonUtil.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace pvf::model {

// One whole-frame keyframe.  A keyframe always carries every animation
// parameter of the entity; there are no per-property tracks.
//
// `frame` is entity-relative and must be 0 .. duration()-1: the closed range
// [start, end] renders every relative frame from 0 through end-start.
struct Keyframe
{
    int frame = 0;

    // Public animation parameters.
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double opacity = 1.0;
    bool visible = true;
    bool physicsInteract = false;

    // Content animation parameters for this entity type.  The schema is fixed
    // in Content.cpp; project JSON never declares types.  Definition
    // parameters never appear here.
    QJsonObject content;

    // Numeric HTML variables declared by the asset's pvhtml.json manifest.
    // Non-number variables stay static and never enter a keyframe.
    QJsonObject htmlVars;

    EasingSpec easing; // this frame -> next frame, shared by the whole frame
    QString note;
    // Unknown keys, preserved verbatim across a load/save cycle.
    QJsonObject extras;

    bool operator==(const Keyframe& other) const;
    bool operator!=(const Keyframe& other) const { return !(*this == other); }
};

// Whole-frame animation values at one entity-relative frame.  `content` and
// `htmlVars` hold numeric values only; definition parameters are not included.
struct KeyframeSample
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double opacity = 1.0;
    bool visible = true;
    bool physicsInteract = false;
    QJsonObject content;
    QJsonObject htmlVars;
};

// Interpolated values at `frame` (may be fractional).  One binary search finds
// the surrounding keyframes and one eased progress value is used for every
// numeric field.  `visible` / `physicsInteract` take the left keyframe's step
// value.  Values are held constant before the first key and after the last; an
// empty vector returns `fallback` unchanged.
//
// `fallback.content` / `fallback.htmlVars` are the static defaults for fields a
// keyframe does not mention.  Missing numeric fields are filled from those
// defaults, so a partially authored keyframe still interpolates every
// parameter.
KeyframeSample evaluateKeyframes(const QVector<Keyframe>& keyframes,
                                 const KeyframeSample& fallback, double frame);

// Exact-frame lookup.  The vector must be sorted by frame; duplicate frames are
// invalid in a loaded project.
const Keyframe* keyframeAt(const QVector<Keyframe>& keyframes, int frame);
bool hasKeyframeAt(const QVector<Keyframe>& keyframes, int frame);

// Editing helpers.  They keep the vector sorted by frame; upsert replaces the
// keyframe at the same frame.  remove returns false when no key was present.
void sortKeyframes(QVector<Keyframe>& keyframes);
void upsertKeyframe(QVector<Keyframe>& keyframes, const Keyframe& keyframe);
bool removeKeyframe(QVector<Keyframe>& keyframes, int frame);

QJsonObject toJson(const Keyframe& keyframe);
std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         const ParseOptions& options,
                                         QString* error = nullptr);
std::optional<Keyframe> keyframeFromJson(const QJsonObject& object,
                                         QString* error = nullptr);

QJsonArray toJson(const QVector<Keyframe>& keyframes);

// Parses the "keyframes" field.  It must be an array; an old per-property
// object (or any non-array value) is rejected with the "旧分轨工程格式已废弃"
// message.  Duplicate frames are rejected.  When `content` is non-null the
// per-type content schema is validated, and a non-negative `duration` checks
// the 0..duration-1 frame range.
std::optional<QVector<Keyframe>> keyframesFromJson(
    const QJsonValue& value, const ParseOptions& options,
    const Content* content, int duration, QString* error = nullptr);
std::optional<QVector<Keyframe>> keyframesFromJson(const QJsonValue& value,
                                                   QString* error = nullptr);

// Frames must be strictly increasing inside 0..duration-1, non-finite animation
// values are rejected, content keys must belong to the entity type's animation
// schema, and htmlVars values must be finite numbers.
bool validateKeyframes(const QVector<Keyframe>& keyframes, int duration,
                       const Content& content, QString* error = nullptr);

} // namespace pvf::model
