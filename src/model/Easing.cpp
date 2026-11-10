#include "Easing.h"

#include <algorithm>
#include <cmath>

namespace pvf::model {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kPresetCount = 30;

// Legacy canonical names, kept byte-for-byte compatible with old project JSON.
const char* const kPresetNames[kPresetCount] = {
    "easeinquad",       "easeoutquad",      "easeinoutquad",
    "easeincubic",      "easeoutcubic",     "easeinoutcubic",
    "easeinquart",      "easeoutquart",     "easeinoutquart",
    "easeinquint",      "easeoutquint",     "easeinoutquint",
    "easeinsine",       "easeoutsine",      "easeinoutsine",
    "easeinexpo",       "easeoutexpo",      "easeinoutexpo",
    "easeincirc",       "easeoutcirc",      "easeinoutcirc",
    "easeinelastic",    "easeoutelastic",   "easeinoutelastic",
    "easeinback",       "easeoutback",      "easeinoutback",
    "easeinbounce",     "easeoutbounce",    "easeinoutbounce",
};

struct Alias
{
    const char* alias;
    const char* canonical;
};

const Alias kAliases[] = {
    {"easein", "easeinquad"},
    {"easeout", "easeoutquad"},
    {"easeinout", "easeinoutquad"},
    {"step", "instant"},
};

int presetIndex(const QString& name)
{
    for (int i = 0; i < kPresetCount; ++i)
        if (name == QLatin1String(kPresetNames[i]))
            return i;
    return -1;
}

double bezierAxis(double p1, double p2, double u)
{
    const double v = 1.0 - u;
    return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u;
}

double cubicBezier(double x1, double y1, double x2, double y2, double x)
{
    x1 = std::clamp(x1, 0.0, 1.0);
    x2 = std::clamp(x2, 0.0, 1.0);

    // Bisect the parametric x to find u for the requested x, then sample y.
    double lower = 0.0;
    double upper = 1.0;
    double u = x;
    for (int i = 0; i < 48; ++i)
    {
        u = 0.5 * (lower + upper);
        if (bezierAxis(x1, x2, u) < x)
            lower = u;
        else
            upper = u;
    }
    return bezierAxis(y1, y2, u);
}

} // namespace

const QStringList& easingPresetNames()
{
    static const QStringList names = [] {
        QStringList list;
        list.reserve(kPresetCount);
        for (int i = 0; i < kPresetCount; ++i)
            list.append(QString::fromLatin1(kPresetNames[i]));
        return list;
    }();
    return names;
}

QString canonicalEasingName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("linear");

    const QString lower = trimmed.toLower();
    const int index = presetIndex(lower);
    if (index >= 0)
        return QString::fromLatin1(kPresetNames[index]);

    for (const Alias& alias : kAliases)
        if (lower == QLatin1String(alias.alias))
            return QString::fromLatin1(alias.canonical);

    if (lower == QLatin1String("linear"))
        return QStringLiteral("linear");
    if (lower == QLatin1String("instant"))
        return QStringLiteral("instant");
    if (lower == QLatin1String("bezier"))
        return QStringLiteral("bezier");

    // Unknown names stay as typed; easedProgress falls back to linear.
    return trimmed;
}

bool EasingSpec::operator==(const EasingSpec& other) const
{
    // Compare canonical names so a legacy alias equals its preset.
    return canonicalEasingName(type) == canonicalEasingName(other.type)
           && qFuzzyCompare(inX + 1.0, other.inX + 1.0)
           && qFuzzyCompare(inY + 1.0, other.inY + 1.0)
           && qFuzzyCompare(outX + 1.0, other.outX + 1.0)
           && qFuzzyCompare(outY + 1.0, other.outY + 1.0);
}

double easedProgress(const EasingSpec& spec, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    const QString type = canonicalEasingName(spec.type);
    if (type == QLatin1String("linear"))
        return t;
    if (type == QLatin1String("instant"))
        return 0.0;
    if (type == QLatin1String("bezier"))
        return cubicBezier(spec.inX, spec.inY, spec.outX, spec.outY, t);

    const double x = t;
    const double x2 = x * 2.0;
    const double xm1 = x - 1.0;

    switch (presetIndex(type))
    {
    case 0: // easeinquad
        return x * x;
    case 1: // easeoutquad
        return -x * (x - 2.0);
    case 2: // easeinoutquad
        return x2 < 1.0 ? 0.5 * x2 * x2
                        : -0.5 * ((x2 - 1.0) * (x2 - 3.0) - 1.0);
    case 3: // easeincubic
        return x * x * x;
    case 4: // easeoutcubic
        return xm1 * xm1 * xm1 + 1.0;
    case 5: // easeinoutcubic
        return x2 < 1.0 ? 0.5 * x2 * x2 * x2
                        : 0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) + 2.0);
    case 6: // easeinquart
        return x * x * x * x;
    case 7: // easeoutquart
        return -(xm1 * xm1 * xm1 * xm1 - 1.0);
    case 8: // easeinoutquart
        return x2 < 1.0 ? 0.5 * x2 * x2 * x2 * x2
                        : -0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0)
                                  - 2.0);
    case 9: // easeinquint
        return x * x * x * x * x;
    case 10: // easeoutquint
        return xm1 * xm1 * xm1 * xm1 * xm1 + 1.0;
    case 11: // easeinoutquint
        return x2 < 1.0
                   ? 0.5 * x2 * x2 * x2 * x2 * x2
                   : 0.5 * ((x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0) * (x2 - 2.0)
                                * (x2 - 2.0)
                            + 2.0);
    case 12: // easeinsine
        return 1.0 - std::cos(x * kPi / 2.0);
    case 13: // easeoutsine
        return std::sin(x * kPi / 2.0);
    case 14: // easeinoutsine
        return -0.5 * (std::cos(kPi * x) - 1.0);
    case 15: // easeinexpo
        return std::pow(2.0, 10.0 * (x - 1.0));
    case 16: // easeoutexpo
        return -std::pow(2.0, -10.0 * x) + 1.0;
    case 17: // easeinoutexpo
        return x2 < 1.0 ? 0.5 * std::pow(2.0, 10.0 * (x2 - 1.0))
                        : 0.5 * (-std::pow(2.0, -10.0 * (x2 - 1.0)) + 2.0);
    case 18: // easeincirc
        return -(std::sqrt(1.0 - x * x) - 1.0);
    case 19: // easeoutcirc
        return std::sqrt(1.0 - xm1 * xm1);
    case 20: // easeinoutcirc
        return x2 < 1.0
                   ? -0.5 * (std::sqrt(1.0 - x2 * x2) - 1.0)
                   : 0.5 * (std::sqrt(std::max(0.0, 1.0 - (x2 - 2.0) * (x2 - 2.0)))
                            + 1.0);
    case 21: // easeinelastic
    {
        const double p = 0.3;
        const double s = p / (2.0 * kPi) * std::asin(1.0);
        return -(std::pow(2.0, 10.0 * (x - 1.0))
                 * std::sin(((x - 1.0) - s) * (2.0 * kPi) / p));
    }
    case 22: // easeoutelastic
    {
        const double p = 0.3;
        const double s = p / (2.0 * kPi) * std::asin(1.0);
        return std::pow(2.0, -10.0 * x) * std::sin((x - s) * (2.0 * kPi) / p) + 1.0;
    }
    case 23: // easeinoutelastic
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
    case 24: // easeinback
    {
        const double s = 1.70158;
        return x * x * ((s + 1.0) * x - s);
    }
    case 25: // easeoutback
    {
        const double s = 1.70158;
        return xm1 * xm1 * ((s + 1.0) * xm1 + s) + 1.0;
    }
    case 26: // easeinoutback
    {
        const double s = 1.70158 * 1.525;
        if (x2 < 1.0)
            return 0.5 * (x2 * x2 * ((s + 1.0) * x2 - s));
        return 0.5 * ((x2 - 2.0) * (x2 - 2.0) * ((s + 1.0) * (x2 - 2.0) + s) + 2.0);
    }
    case 27: // easeinbounce
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
    case 28: // easeoutbounce
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
    case 29: // easeinoutbounce
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
    default:
        return x;
    }
}

} // namespace pvf::model
