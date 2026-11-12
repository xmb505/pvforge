#include "SolverInternal.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pvf::solver::detail {

EvalOutcome evaluateSpiral(const Segment &segment, const World &world,
                           const FrameRange &range, int fps)
{
    EvalOutcome outcome;
    if (segment.targetUuid.isEmpty())
        return outcome;

    double turns = paramNumber(segment.params, "turns", 3.0);
    double endRadius = paramNumber(segment.params, "endRadius", 0.0);
    const double speedDegPerSec =
            paramNumber(segment.params, "speedDegPerSec", 0.0);
    if (!std::isfinite(turns))
        turns = 3.0;
    if (!std::isfinite(endRadius))
        endRadius = 0.0;
    endRadius = std::max(0.0, endRadius);

    // Spiral direction is its own convention: +1 is counter-clockwise, -1 is
    // clockwise (the orbit/black-hole parameter uses the opposite sign).
    const int direction =
            paramNumber(segment.params, "direction", 1.0) < 0.0 ? -1 : 1;

    const Uuid &centerEntity = segment.targetUuid;
    const Pose centerStart = world.poseOf(centerEntity, range.startFrame);
    if (!std::isfinite(centerStart.position.x())
        || !std::isfinite(centerStart.position.y()))
        return outcome;

    const QPointF offset = segment.startPose.position - centerStart.position;
    const double startRadius = std::hypot(offset.x(), offset.y());
    if (!std::isfinite(startRadius))
        return outcome;
    const double startAngle = std::atan2(offset.y(), offset.x());

    const double twoPi = 2.0 * std::acos(-1.0);
    const double durationSeconds =
            fps > 0 ? double(range.count - 1) / double(fps) : 0.0;
    // A positive speed overrides the authored turn count for the whole segment.
    const double totalAngle = speedDegPerSec > 0.0 && durationSeconds > 0.0
            ? qDegreesToRadians(speedDegPerSec) * durationSeconds
            : turns * twoPi;
    if (!std::isfinite(totalAngle))
        return outcome;

    const double span = range.count > 1 ? double(range.count - 1) : 0.0;

    QVector<QPointF> positions;
    positions.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        const int frame = range.startFrame + i;
        const double progress = span > 0.0 ? double(i) / span : 0.0;
        const double radius =
                startRadius + (endRadius - startRadius) * progress;
        const double angle = startAngle + double(direction) * totalAngle * progress;
        const Pose center = world.poseOf(centerEntity, frame);
        positions.append(center.position
                         + QPointF(radius * std::cos(angle),
                                   radius * std::sin(angle)));
    }
    // Keep frame 0 exactly on the authored start pose, even after the
    // atan2/trig round trip.
    positions[0] = segment.startPose.position;

    QVector<Sample> &samples = outcome.samples;
    samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        const bool last = (i == range.count - 1);
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose.position = positions.at(i);
        sample.pose.rotation = segment.startPose.rotation;
        sample.collided = false;
        // A spiral is a purely kinematic path: it has no collision, no stop
        // threshold and never reports a physical stop.
        sample.stopped = false;
        if (last)
            sample.velocity = QPointF(0.0, 0.0);
        else
            sample.velocity = (positions.at(i + 1) - positions.at(i))
                    * double(fps);
        samples.append(sample);
    }
    return outcome;
}

} // namespace pvf::solver::detail
