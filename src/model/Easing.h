#pragma once

#include <QString>
#include <QStringList>

namespace pvf::model {

// Interpolation of the left keyframe controls the segment to the next one.
//
// Canonical type names (stable JSON format):
//   linear, instant, bezier,
//   easeinquad, easeoutquad, easeinoutquad,
//   easeincubic, easeoutcubic, easeinoutcubic,
//   easeinquart, easeoutquart, easeinoutquart,
//   easeinquint, easeoutquint, easeinoutquint,
//   easeinsine, easeoutsine, easeinoutsine,
//   easeinexpo, easeoutexpo, easeinoutexpo,
//   easeincirc, easeoutcirc, easeinoutcirc,
//   easeinelastic, easeoutelastic, easeinoutelastic,
//   easeinback, easeoutback, easeinoutback,
//   easeinbounce, easeoutbounce, easeinoutbounce.
//
// Reads also accept the legacy camelCase aliases easeIn/easeOut/easeInOut
// (quadratic curves), step (same as instant), and any casing of the names
// above; serialisation always writes the canonical lowercase name.
struct EasingSpec
{
    QString type = QStringLiteral("linear");
    double inX = 0.42;
    double inY = 0.0;
    double outX = 0.58;
    double outY = 1.0;

    bool operator==(const EasingSpec& other) const;
    bool operator!=(const EasingSpec& other) const { return !(*this == other); }
};

// The 30 preset curves in stable (in/out/in-out per family) order. Does not
// include linear, instant or bezier.
const QStringList& easingPresetNames();

// Canonical lowercase name for any accepted alias. Empty maps to "linear";
// unknown names are returned trimmed and unchanged (they evaluate as linear).
QString canonicalEasingName(const QString& name);

// Evaluates eased progress for t in [0, 1]. Unknown types fall back to linear.
// Elastic, back and bounce may leave [0, 1] between the endpoints, matching
// the legacy implementation. Bezier uses (0,0) -> (inX,inY) -> (outX,outY)
// -> (1,1); X handles are clamped to [0,1], Y may overshoot.
double easedProgress(const EasingSpec& spec, double t);

} // namespace pvf::model
