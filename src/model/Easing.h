#pragma once

#include <QString>

namespace pvf::model {

// Interpolation of the left keyframe controls the segment to the next one.
// type is one of: linear, easeIn, easeOut, easeInOut, bezier, instant, step.
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

// Evaluates eased progress for t in [0, 1]. Unknown types fall back to linear.
// Bezier uses (0,0) -> (inX,inY) -> (outX,outY) -> (1,1); X handles are
// clamped to [0,1], Y may overshoot.
double easedProgress(const EasingSpec& spec, double t);

} // namespace pvf::model
