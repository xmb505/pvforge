#pragma once

#include <QString>

namespace pvf {

// Mine-imator's per-keyframe transition set. The transition of the left
// keyframe controls the segment to the next keyframe.
enum class Easing
{
    Linear,
    Instant,
    Bezier,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInQuart,
    EaseOutQuart,
    EaseInOutQuart,
    EaseInQuint,
    EaseOutQuint,
    EaseInOutQuint,
    EaseInSine,
    EaseOutSine,
    EaseInOutSine,
    EaseInExpo,
    EaseOutExpo,
    EaseInOutExpo,
    EaseInCirc,
    EaseOutCirc,
    EaseInOutCirc,
    EaseInElastic,
    EaseOutElastic,
    EaseInOutElastic,
    EaseInBack,
    EaseOutBack,
    EaseInOutBack,
    EaseInBounce,
    EaseOutBounce,
    EaseInOutBounce
};

struct EasingSpec
{
    Easing easing = Easing::Linear;

    // Cubic bezier handles in unit coordinates. P0 is (0,0), P3 is (1,1).
    // X is clamped to [0,1] by the evaluator; Y may overshoot.
    double inX = 1.0;
    double inY = 0.0;
    double outX = 0.0;
    double outY = 1.0;

    bool operator==(const EasingSpec& other) const
    {
        return easing == other.easing && qFuzzyCompare(inX + 1.0, other.inX + 1.0)
               && qFuzzyCompare(inY + 1.0, other.inY + 1.0)
               && qFuzzyCompare(outX + 1.0, other.outX + 1.0)
               && qFuzzyCompare(outY + 1.0, other.outY + 1.0);
    }
    bool operator!=(const EasingSpec& other) const { return !(*this == other); }
};

QString easingName(Easing easing);
Easing easingFromName(const QString& name);

// Friendly label for menus/buttons, e.g. "缓入 · 柔和".
QString easingDisplayName(Easing easing);

// Evaluates the eased progress for t in [0,1]. Bezier uses the four handles.
double applyEasing(const EasingSpec& spec, double t);

// Raw parametric cubic bezier y for the x position t.
double bezierValue(double inX, double inY, double outX, double outY, double t);

} // namespace pvf
