#include "Easing.h"

namespace pvf {

namespace {

struct EasingName
{
    Easing easing;
    const char* name;
};

const EasingName kNames[] = {
    {Easing::Linear, "linear"},
    {Easing::Instant, "instant"},
    {Easing::Bezier, "bezier"},
    {Easing::EaseInQuad, "easeinquad"},
    {Easing::EaseOutQuad, "easeoutquad"},
    {Easing::EaseInOutQuad, "easeinoutquad"},
    {Easing::EaseInCubic, "easeincubic"},
    {Easing::EaseOutCubic, "easeoutcubic"},
    {Easing::EaseInOutCubic, "easeinoutcubic"},
    {Easing::EaseInQuart, "easeinquart"},
    {Easing::EaseOutQuart, "easeoutquart"},
    {Easing::EaseInOutQuart, "easeinoutquart"},
    {Easing::EaseInQuint, "easeinquint"},
    {Easing::EaseOutQuint, "easeoutquint"},
    {Easing::EaseInOutQuint, "easeinoutquint"},
    {Easing::EaseInSine, "easeinsine"},
    {Easing::EaseOutSine, "easeoutsine"},
    {Easing::EaseInOutSine, "easeinoutsine"},
    {Easing::EaseInExpo, "easeinexpo"},
    {Easing::EaseOutExpo, "easeoutexpo"},
    {Easing::EaseInOutExpo, "easeinoutexpo"},
    {Easing::EaseInCirc, "easeincirc"},
    {Easing::EaseOutCirc, "easeoutcirc"},
    {Easing::EaseInOutCirc, "easeinoutcirc"},
    {Easing::EaseInElastic, "easeinelastic"},
    {Easing::EaseOutElastic, "easeoutelastic"},
    {Easing::EaseInOutElastic, "easeinoutelastic"},
    {Easing::EaseInBack, "easeinback"},
    {Easing::EaseOutBack, "easeoutback"},
    {Easing::EaseInOutBack, "easeinoutback"},
    {Easing::EaseInBounce, "easeinbounce"},
    {Easing::EaseOutBounce, "easeoutbounce"},
    {Easing::EaseInOutBounce, "easeinoutbounce"},
};

} // namespace

QString easingName(Easing easing)
{
    for (const EasingName& entry : kNames)
        if (entry.easing == easing)
            return QString::fromLatin1(entry.name);
    return QStringLiteral("linear");
}

Easing easingFromName(const QString& name)
{
    for (const EasingName& entry : kNames)
        if (name == QLatin1String(entry.name))
            return entry.easing;
    return Easing::Linear;
}

} // namespace pvf
