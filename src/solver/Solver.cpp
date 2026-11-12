#include "Solver.h"
#include "SolverInternal.h"

#include "perf/FrameProfiler.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pvf::solver {

QString stopReasonName(StopReason reason)
{
    switch (reason)
    {
    case StopReason::Collision:
        return QStringLiteral("collision");
    case StopReason::StopThreshold:
        return QStringLiteral("stopThreshold");
    case StopReason::ClipEnd:
        return QStringLiteral("clipEnd");
    case StopReason::ExplicitEnd:
        return QStringLiteral("explicitEnd");
    case StopReason::None:
        break;
    }
    return QStringLiteral("none");
}

Result evaluateResult(const Segment &segment, const World &world,
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

    const bool explicitEnd = segment.endFrame >= 0;
    int endFrame = explicitEnd ? segment.endFrame : clipDuration - 1;
    endFrame = std::min(endFrame, clipDuration - 1);
    if (endFrame < segment.startFrame)
        return {};

    perf::Scope solverScope("物理求解", perf::Category::Solver);
    perf::FrameProfiler::instance().noteSolverCall();

    const detail::FrameRange range{segment.startFrame, endFrame,
                                   endFrame - segment.startFrame + 1};
    detail::EvalOutcome outcome;
    if (range.count == 1)
    {
        outcome = detail::staticSamples(segment, range);
    }
    else
    {
        const int safeFps = std::max(1, fps);
        switch (segment.effect)
        {
        case SolverEffect::Gravity:
            outcome = detail::evaluateGravity(segment, world, range, safeFps);
            break;
        case SolverEffect::Orbit:
            outcome = detail::evaluateOrbit(segment, world, range, safeFps);
            break;
        case SolverEffect::Spin:
            outcome = detail::evaluateSpin(segment, range);
            break;
        case SolverEffect::BlackHole:
            outcome = detail::evaluateBlackHole(segment, world, range, safeFps);
            break;
        case SolverEffect::Spiral:
            outcome = detail::evaluateSpiral(segment, world, range, safeFps);
            break;
        }
    }

    if (outcome.samples.isEmpty())
        return {};

    Result result;
    result.samples = std::move(outcome.samples);
    if (explicitEnd)
    {
        // An explicit endFrame is an advanced override: it keeps the old
        // clipping rule (and the frozen hold) even when the motion physically
        // stopped earlier.  Sample::stopped still reports the real stop.
        result.stopFrame = endFrame;
        result.reason = StopReason::ExplicitEnd;
    }
    else if (outcome.stopFrame >= segment.startFrame)
    {
        result.stopFrame = outcome.stopFrame;
        result.reason = outcome.reason;
    }
    else
    {
        result.stopFrame = endFrame;
        result.reason = StopReason::ClipEnd;
    }
    return result;
}

QVector<Sample> evaluate(const Segment &segment, const World &world,
                         int clipDuration, int fps)
{
    return evaluateResult(segment, world, clipDuration, fps).samples;
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

EvalOutcome staticSamples(const Segment &segment, const FrameRange &range)
{
    EvalOutcome outcome;
    outcome.samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose = segment.startPose;
        sample.velocity = QPointF(0.0, 0.0);
        // A single solved frame is a static pose, not a physical stop; the
        // caller reports ClipEnd/ExplicitEnd in Result::reason.
        sample.stopped = false;
        outcome.samples.append(sample);
    }
    return outcome;
}

} // namespace pvf::solver::detail
