#include "SolverInternal.h"

#include <algorithm>
#include <cmath>

namespace pvf::solver::detail {

QVector<Sample> evaluateOrbit(const Segment &segment, const World &world,
                              const FrameRange &range, int fps)
{
    if (segment.targetUuid.isEmpty())
        return {};

    const double aspect = std::clamp(
            paramNumber(segment.params, "aspect", 1.0), 0.01, 1.0);
    const double angle = paramNumber(segment.params, "angle", 0.0);
    const int direction = paramDirection(segment.params, 1);
    const double periodFrames = paramNumber(segment.params, "periodFrames", 60.0);

    const Uuid &centerEntity = segment.targetUuid;

    const Pose centerStart = world.poseOf(centerEntity, range.startFrame);
    const double baseRotation = centerStart.rotation + angle;
    const QPointF relative = rotatePoint(
            segment.startPose.position - centerStart.position, -baseRotation);

    // The ellipse has to pass through the start position.  With b = aspect * a,
    // solving (u/a)^2 + (v/b)^2 = 1 gives a from the start distance.
    const double u = relative.x();
    const double v = relative.y();
    const double semiMajor = std::sqrt(u * u + (v * v) / (aspect * aspect));
    const double semiMinor = aspect * semiMajor;
    double startPhase = 0.0;
    if (semiMajor > 1e-9 && semiMinor > 1e-9)
        startPhase = std::atan2(v / semiMinor, u / semiMajor);

    // Model space is Y-up, so a positive mathematical angle runs
    // counter-clockwise.  The public direction parameter keeps its old
    // meaning: +1 is clockwise on screen, hence the negated phase.
    const double twoPi = 2.0 * std::acos(-1.0);
    const double phasePerFrame = periodFrames > 0.0
            ? -double(direction) * twoPi / periodFrames
            : 0.0;

    QVector<QPointF> positions;
    positions.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        const int frame = range.startFrame + i;
        const double phase = startPhase
                + phasePerFrame * double(frame - range.startFrame);
        const QPointF local(semiMajor * std::cos(phase),
                            semiMinor * std::sin(phase));
        const Pose center = world.poseOf(centerEntity, frame);
        positions.append(center.position
                + rotatePoint(local, center.rotation + angle));
    }
    // Keep frame 0 exactly on the authored start pose, even after the
    // atan2/axis round trip.
    positions[0] = segment.startPose.position;

    QVector<Sample> samples;
    samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        const bool last = (i == range.count - 1);
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose.position = positions.at(i);
        sample.pose.rotation = segment.startPose.rotation;
        sample.collided = false;
        sample.stopped = last;
        if (last)
            sample.velocity = QPointF(0.0, 0.0);
        else
            sample.velocity = (positions.at(i + 1) - positions.at(i))
                    * double(fps);
        samples.append(sample);
    }
    return samples;
}

} // namespace pvf::solver::detail
