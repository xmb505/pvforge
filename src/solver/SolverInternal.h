#pragma once

#include "Solver.h"

namespace pvf::solver::detail {

struct FrameRange
{
    int startFrame = 0;
    int endFrame = 0;
    int count = 0;
};

// Solver-level outcome before evaluateResult() decides between ClipEnd and
// ExplicitEnd.  stopFrame is the first physical stop, or -1 when the motion
// never stopped and simply ran out of range.
struct EvalOutcome
{
    QVector<Sample> samples;
    int stopFrame = -1;
    StopReason reason = StopReason::None;
};

QPointF rotatePoint(const QPointF &point, double degrees);

double paramNumber(const QVariantMap &params, const char *key, double fallback);
int paramDirection(const QVariantMap &params, int fallback = 1);

EvalOutcome staticSamples(const Segment &segment, const FrameRange &range);

EvalOutcome evaluateGravity(const Segment &segment, const World &world,
                            const FrameRange &range, int fps);
EvalOutcome evaluateOrbit(const Segment &segment, const World &world,
                          const FrameRange &range, int fps);
EvalOutcome evaluateSpin(const Segment &segment, const FrameRange &range);
EvalOutcome evaluateBlackHole(const Segment &segment, const World &world,
                              const FrameRange &range, int fps);
EvalOutcome evaluateSpiral(const Segment &segment, const World &world,
                           const FrameRange &range, int fps);

} // namespace pvf::solver::detail
