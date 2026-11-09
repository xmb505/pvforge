#pragma once

#include "Solver.h"

namespace pvf::solver::detail {

struct FrameRange
{
    int startFrame = 0;
    int endFrame = 0;
    int count = 0;
};

QPointF rotatePoint(const QPointF &point, double degrees);

double paramNumber(const QVariantMap &params, const char *key, double fallback);
int paramDirection(const QVariantMap &params, int fallback = 1);

QVector<Sample> staticSamples(const Segment &segment, const FrameRange &range);

QVector<Sample> evaluateGravity(const Segment &segment, const World &world,
                                const FrameRange &range, int fps);
QVector<Sample> evaluateOrbit(const Segment &segment, const World &world,
                              const FrameRange &range, int fps);
QVector<Sample> evaluateSpin(const Segment &segment, const FrameRange &range);
QVector<Sample> evaluateBlackHole(const Segment &segment, const World &world,
                                  const FrameRange &range, int fps);

} // namespace pvf::solver::detail
