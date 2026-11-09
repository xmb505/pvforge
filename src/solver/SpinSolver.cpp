#include "SolverInternal.h"

namespace pvf::solver::detail {

QVector<Sample> evaluateSpin(const Segment &segment, const FrameRange &range)
{
    const double angularVelocity = paramNumber(segment.params, "angularVelocity", 6.0);
    const int direction = paramDirection(segment.params, 1);
    const double degreesPerFrame = angularVelocity * double(direction);

    QVector<Sample> samples;
    samples.reserve(range.count);
    for (int i = 0; i < range.count; ++i)
    {
        const bool last = (i == range.count - 1);
        Sample sample;
        sample.frame = range.startFrame + i;
        sample.pose.position = segment.startPose.position;
        sample.pose.rotation = segment.startPose.rotation
                + degreesPerFrame * double(sample.frame - range.startFrame);
        sample.velocity = QPointF(0.0, 0.0);
        sample.collided = false;
        sample.stopped = last;
        samples.append(sample);
    }
    return samples;
}

} // namespace pvf::solver::detail
