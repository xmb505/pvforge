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
    if (!std::isfinite(turns))
        turns = 3.0;
    double aspect = std::clamp(
            paramNumber(segment.params, "aspect", 1.0), 0.01, 1.0);
    if (!std::isfinite(aspect))
        aspect = 1.0;
    double angle = paramNumber(segment.params, "angle", 0.0);
    if (!std::isfinite(angle))
        angle = 0.0;
    double speedDegPerSec = paramNumber(segment.params, "speedDegPerSec", 0.0);
    if (!std::isfinite(speedDegPerSec) || speedDegPerSec < 0.0)
        speedDegPerSec = 0.0;

    // Spiral direction is its own convention: +1 is counter-clockwise, -1 is
    // clockwise (the orbit/black-hole parameter uses the opposite sign).
    const int direction =
            paramNumber(segment.params, "direction", 1.0) < 0.0 ? -1 : 1;

    const Uuid &centerEntity = segment.targetUuid;
    const Pose centerStart = world.poseOf(centerEntity, range.startFrame);
    if (!std::isfinite(centerStart.position.x())
        || !std::isfinite(centerStart.position.y()))
        return outcome;

    // Build the ellipse in a frame rotated with the center entity, exactly
    // like Orbit, so the authored start position lies on it.
    const double baseRotation = centerStart.rotation + angle;
    const QPointF localStart = rotatePoint(
            segment.startPose.position - centerStart.position, -baseRotation);
    const double u = localStart.x();
    const double v = localStart.y();
    const double semiMajor = std::sqrt(u * u + (v * v) / (aspect * aspect));
    const double semiMinor = aspect * semiMajor;
    if (!std::isfinite(semiMajor) || !std::isfinite(semiMinor))
        return outcome;
    double startPhase = 0.0;
    if (semiMajor > 1e-9 && semiMinor > 1e-9)
        startPhase = std::atan2(v / semiMinor, u / semiMajor);

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
        // The orbit collapses linearly to a point, so the segment always ends
        // exactly on the target center.
        const double shrink = 1.0 - progress;
        const double phase =
                startPhase + double(direction) * totalAngle * progress;
        const QPointF local(semiMajor * shrink * std::cos(phase),
                            semiMinor * shrink * std::sin(phase));
        const Pose center = world.poseOf(centerEntity, frame);
        positions.append(center.position
                         + rotatePoint(local, center.rotation + angle));
    }
    // Keep frame 0 exactly on the authored start pose, even after the
    // atan2/trig round trip.
    positions[0] = segment.startPose.position;
    // ...and the last frame exactly on the center, even after the shrink
    // round trip.  A single-frame segment keeps the frozen start pose.
    if (range.count > 1)
        positions[range.count - 1] =
                world.poseOf(centerEntity, range.startFrame + range.count - 1)
                        .position;

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
