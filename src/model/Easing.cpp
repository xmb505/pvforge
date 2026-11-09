#include "Easing.h"

#include <algorithm>

namespace pvf::model {

namespace {

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

bool EasingSpec::operator==(const EasingSpec& other) const
{
    return type == other.type
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

    const QString type = spec.type.trimmed();
    if (type.isEmpty() || type == QLatin1String("linear"))
        return t;
    if (type == QLatin1String("easeIn"))
        return t * t;
    if (type == QLatin1String("easeOut"))
        return t * (2.0 - t);
    if (type == QLatin1String("easeInOut"))
    {
        return t < 0.5 ? 2.0 * t * t
                       : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
    }
    if (type == QLatin1String("bezier"))
        return cubicBezier(spec.inX, spec.inY, spec.outX, spec.outY, t);
    if (type == QLatin1String("instant") || type == QLatin1String("step"))
        return 0.0;
    return t;
}

} // namespace pvf::model
