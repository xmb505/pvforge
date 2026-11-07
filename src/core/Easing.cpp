#include "Easing.h"

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

double sampleBezier(double p1, double p3, double u)
{
    const double v = 1.0 - u;
    return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p3 + u * u * u;
}

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

QString easingDisplayName(Easing easing)
{
    switch (easing)
    {
    case Easing::Linear:
        return QStringLiteral("线性");
    case Easing::Instant:
        return QStringLiteral("瞬间（保持）");
    case Easing::Bezier:
        return QStringLiteral("自定义贝塞尔");
    default:
        break;
    }

    const int index = int(easing) - int(Easing::EaseInQuad);
    if (index < 0 || index >= 30)
        return QStringLiteral("线性");

    static const char* const functions[] = {
        "平滑", "标准", "强", "很强", "柔和",
        "指数", "圆弧", "弹性", "回弹", "反弹",
    };
    static const char* const directions[] = {"缓入", "缓出", "缓入缓出"};
    return QString::fromUtf8(directions[index % 3]) + QStringLiteral(" · ")
           + QString::fromUtf8(functions[index / 3]);
}

double bezierValue(double inX, double inY, double outX, double outY, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0 || t >= 1.0)
        return t;

    inX = std::clamp(inX, 0.0, 1.0);
    outX = std::clamp(outX, 0.0, 1.0);

    double lower = 0.0;
    double upper = 1.0;
    double u = 0.5;
    for (int i = 0; i < 30; ++i)
    {
        u = (lower + upper) / 2.0;
        if (sampleBezier(inX, outX, u) < t)
            lower = u;
        else
            upper = u;
    }
    const double v = 1.0 - u;
    return 3.0 * v * v * u * inY + 3.0 * v * u * u * outY + u * u * u;
}

double applyEasing(const EasingSpec& spec, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0 || t >= 1.0)
        return t;

    const double x = t;
    const double x2 = x * 2.0;
    const double xm1 = x - 1.0;

    switch (spec.easing)
    {
    case Easing::Linear:
        return x;
    case Easing::Instant:
        return 0.0;
    case Easing::Bezier:
        return bezierValue(spec.inX, spec.inY, spec.outX, spec.outY, x);
    case Easing::EaseInQuad:
        return x * x;
    case Easing::EaseOutQuad:
        return -x * (x - 2.0);
    case Easing::EaseInOutQuad:
        return x2 < 1.0 ? 0.5 * x2 * x2 : -0.5 * ((x2 - 1.0) * (x2 - 3.0) - 1.0);
    case Easing::EaseInCubic:
        return x * x * x;
    case Easing::EaseOutCubic:
        return xm1 * xm1 * xm1 + 1.0;
    case Easing::EaseInOutCubic:
        return x2 < 1.0 ? 0.5 * x2 * x2 * x2
                        : 0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) + 2.0);
    case Easing::EaseInQuart:
        return x * x * x * x;
    case Easing::EaseOutQuart:
        return -(xm1 * xm1 * xm1 * xm1 - 1.0);
    case Easing::EaseInOutQuart:
        return x2 < 1.0 ? 0.5 * x2 * x2 * x2 * x2
                        : -0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0)
                                  - 2.0);
    case Easing::EaseInQuint:
        return x * x * x * x * x;
    case Easing::EaseOutQuint:
        return xm1 * xm1 * xm1 * xm1 * xm1 + 1.0;
    case Easing::EaseInOutQuint:
        return x2 < 1.0
                   ? 0.5 * x2 * x2 * x2 * x2 * x2
                   : 0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0)
                                * (x2 - 2.0)
                            + 2.0);
    case Easing::EaseInSine:
        return 1.0 - std::cos(x * kPi / 2.0);
    case Easing::EaseOutSine:
        return std::sin(x * kPi / 2.0);
    case Easing::EaseInOutSine:
        return -0.5 * (std::cos(kPi * x) - 1.0);
    case Easing::EaseInExpo:
        return std::pow(2.0, 10.0 * (x - 1.0));
    case Easing::EaseOutExpo:
        return -std::pow(2.0, -10.0 * x) + 1.0;
    case Easing::EaseInOutExpo:
        return x2 < 1.0 ? 0.5 * std::pow(2.0, 10.0 * (x2 - 1.0))
                        : 0.5 * (-std::pow(2.0, -10.0 * (x2 - 1.0)) + 2.0);
    case Easing::EaseInCirc:
        return -(std::sqrt(1.0 - x * x) - 1.0);
    case Easing::EaseOutCirc:
        return std::sqrt(1.0 - xm1 * xm1);
    case Easing::EaseInOutCirc:
        return x2 < 1.0
                   ? -0.5 * (std::sqrt(1.0 - x2 * x2) - 1.0)
                   : 0.5 * (std::sqrt(std::max(0.0, 1.0 - (x2 - 2.0) * (x2 - 2.0)))
                            + 1.0);
    case Easing::EaseInElastic:
    {
        const double p = 0.3;
        const double s = p / (2.0 * kPi) * std::asin(1.0);
        return -(std::pow(2.0, 10.0 * (x - 1.0))
                 * std::sin(((x - 1.0) - s) * (2.0 * kPi) / p));
    }
    case Easing::EaseOutElastic:
    {
        const double p = 0.3;
        const double s = p / (2.0 * kPi) * std::asin(1.0);
        return std::pow(2.0, -10.0 * x) * std::sin((x - s) * (2.0 * kPi) / p) + 1.0;
    }
    case Easing::EaseInOutElastic:
    {
        const double p = 0.3 * 1.5;
        const double s = p / (2.0 * kPi) * std::asin(1.0);
        if (x2 < 1.0)
            return -0.5 * (std::pow(2.0, 10.0 * (x2 - 1.0))
                           * std::sin(((x2 - 1.0) - s) * (2.0 * kPi) / p));
        return std::pow(2.0, -10.0 * (x2 - 1.0))
                   * std::sin(((x2 - 1.0) - s) * (2.0 * kPi) / p) * 0.5
               + 1.0;
    }
    case Easing::EaseInBack:
    {
        const double s = 1.70158;
        return x * x * ((s + 1.0) * x - s);
    }
    case Easing::EaseOutBack:
    {
        const double s = 1.70158;
        return xm1 * xm1 * ((s + 1.0) * xm1 + s) + 1.0;
    }
    case Easing::EaseInOutBack:
    {
        const double s = 1.70158 * 1.525;
        if (x2 < 1.0)
            return 0.5 * (x2 * x2 * ((s + 1.0) * x2 - s));
        return 0.5 * ((x2 - 2.0) * (x2 - 2.0) * ((s + 1.0) * (x2 - 2.0) + s) + 2.0);
    }
    case Easing::EaseInBounce:
    {
        const double v = 1.0 - x;
        if (v < 1.0 / 2.75)
            return 1.0 - (7.5625 * v * v);
        if (v < 2.0 / 2.75)
        {
            const double w = v - 1.5 / 2.75;
            return 1.0 - (7.5625 * w * w + 0.75);
        }
        if (v < 2.5 / 2.75)
        {
            const double w = v - 2.25 / 2.75;
            return 1.0 - (7.5625 * w * w + 0.9375);
        }
        const double w = v - 2.625 / 2.75;
        return 1.0 - (7.5625 * w * w + 0.984375);
    }
    case Easing::EaseOutBounce:
    {
        if (x < 1.0 / 2.75)
            return 7.5625 * x * x;
        if (x < 2.0 / 2.75)
        {
            const double w = x - 1.5 / 2.75;
            return 7.5625 * w * w + 0.75;
        }
        if (x < 2.5 / 2.75)
        {
            const double w = x - 2.25 / 2.75;
            return 7.5625 * w * w + 0.9375;
        }
        const double w = x - 2.625 / 2.75;
        return 7.5625 * w * w + 0.984375;
    }
    case Easing::EaseInOutBounce:
    {
        if (x < 0.5)
        {
            const double v = 1.0 - x * 2.0;
            double result = 0.0;
            if (v < 1.0 / 2.75)
                result = 7.5625 * v * v;
            else if (v < 2.0 / 2.75)
            {
                const double w = v - 1.5 / 2.75;
                result = 7.5625 * w * w + 0.75;
            }
            else if (v < 2.5 / 2.75)
            {
                const double w = v - 2.25 / 2.75;
                result = 7.5625 * w * w + 0.9375;
            }
            else
            {
                const double w = v - 2.625 / 2.75;
                result = 7.5625 * w * w + 0.984375;
            }
            return (1.0 - result) * 0.5;
        }

        const double v = x * 2.0 - 1.0;
        double result = 0.0;
        if (v < 1.0 / 2.75)
            result = 7.5625 * v * v;
        else if (v < 2.0 / 2.75)
        {
            const double w = v - 1.5 / 2.75;
            result = 7.5625 * w * w + 0.75;
        }
        else if (v < 2.5 / 2.75)
        {
            const double w = v - 2.25 / 2.75;
            result = 7.5625 * w * w + 0.9375;
        }
        else
        {
            const double w = v - 2.625 / 2.75;
            result = 7.5625 * w * w + 0.984375;
        }
        return result * 0.5 + 0.5;
    }
    }
    return x;
}

} // namespace pvf
