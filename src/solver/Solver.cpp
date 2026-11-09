#include "Solver.h"
#include "SolverInternal.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pvf::solver {

QVector<Sample> evaluate(const Segment &segment, const World &world,
                         int clipDuration, int fps)
{
    if (segment.entityUuid.isEmpty())
        return {};
    if (segment.effect != SolverEffect::Spin && segment.targetUuid.isEmpty())
        return {};
    if (clipDuration <= 0)
        return {};
    if (segment.startFrame < 0 || segment.startFrame >= clipDuration)
        return {};

    int endFrame = segment.endFrame < 0 ? clipDuration - 1 : segment.endFrame;
    endFrame = std::min(endFrame, clipDuration - 1);
    if (endFrame < segment.startFrame)
        return {};

    const detail::FrameRange range{segment.startFrame, endFrame,
                                   endFrame - segment.startFrame + 1};
    if (range.count == 1)
        return detail::staticSamples(segment, range);

    const int safeFps = std::max(1, fps);
    switch (segment.effect)
    {
    case SolverEffect::Gravity:
        return detail::evaluateGravity(segment, world, range, safeFps);
    case SolverEffect::Orbit:
        return detail::evaluateOrbit(segment, world, range, safeFps);
    case SolverEffect::Spin:
        return detail::evaluateSpin(segment, range);
    case SolverEffect::BlackHole:
        return detail::evaluateBlackHole(segment, world, range, safeFps);
    }
    return detail::staticSamples(segment, range);
}

} // namespace pvf::solver

namespace pvf::solver::detail {

QPointF rotatePoint(const QPointF &point, double degrees)
{
    if (degrees == 0.0)
        return point;
    const double radians = qDegreesToRadians(degrees);
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return QPointF(point.x() * c - point.y() * s,
                   point.x() * s + point.y() * c);
}

double paramNumber(const QVariantMap &params, const char *key, double fallback)
{
    const auto it = params.constFind(QString::fromLatin1(key));
    if (it == params.constEnd() || !it->isValid())
        return fallback;
    bool ok = false;
    const double value = it->toDouble(&ok);
    return ok ? value : fallback;
}

int paramDirection(const QVariantMap &params, int fallback)
{
    const auto it = params.constFind(QStringLiteral("direction"));
    if (it == params.constEnd() || !it->isValid())
        return fallback;

    if (it->typeId() == QMetaType::QString)
    {
        const QString text = it->toString().trimmed().toLower();
        if (text == QStringLiteral("ccw") || text == QStringLiteral("counterclockwise")
            || text == QStringLiteral("counter-clockwise") || text == QStringLiteral("逆")
            || text == QStringLiteral("逆时针"))
            return -1;
        if (text == QStringLiteral("cw") || text == QStringLiteral("clockwise")
            || text == QStringLiteral("顺") || text == QStringLiteral("顺时针"))
            return 1;
    }

    bool ok = false;
    const double value = it->toDouble(&ok);
    if (!ok)
        return fallback;
    return value < 0.0 ? -1 : 1;
}

QVector<Sample> staticSamples(const Segment &segment, const FrameRange &range)
{
    QVector<Sample> samples;
    samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose = segment.startPose;
        sample.velocity = QPointF(0.0, 0.0);
        sample.stopped = (i == range.count - 1);
        samples.append(sample);
    }
    return samples;
}

} // namespace pvf::solver::detail
