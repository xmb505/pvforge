#include "SolverInternal.h"

namespace pvf::solver::detail {

EvalOutcome evaluateSpin(const Segment &segment, const FrameRange &range)
{
    const double angularVelocity = paramNumber(segment.params, "angularVelocity", 6.0);
    const int direction = paramDirection(segment.params, 1);
    // Model space is Y-up: positive rotation is counter-clockwise.  The
    // direction parameter keeps +1 = clockwise on screen, so the sign flips.
    const double degreesPerFrame = -angularVelocity * double(direction);

    EvalOutcome outcome;
    QVector<Sample> &samples = outcome.samples;
    samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose.position = segment.startPose.position;
        sample.pose.rotation = segment.startPose.rotation
                + degreesPerFrame * double(sample.frame - range.startFrame);
        sample.velocity = QPointF(0.0, 0.0);
        sample.collided = false;
        // Spin has no stop threshold; the clip end is not a physical stop.
        sample.stopped = false;
        samples.append(sample);
    }
    return outcome;
}

} // namespace pvf::solver::detail
