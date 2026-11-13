#include "TimelineWidgetInternal.h"

#include "next/core/Document.h"
#include "next/core/EntityLock.h"

#include <algorithm>

namespace pvf::next::detail {

bool entityIsLocked(const Document* document, const Uuid& uuid)
{
    return document && isEntityLocked(document->project(), uuid);
}

// Solver segments drive x/y for gravity/orbit/blackhole and rotation for
// spin. Inside a segment (startFrame, effectiveEnd] normal keys are forbidden;
// the start frame itself holds the segment's initial key and stays editable.
bool trackBlockedAt(const model::Entity& entity, const QString& track, int frame)
{
    const int duration = entity.duration();
    if (duration <= 0)
        return false;

    for (const auto& solver : entity.solvers)
    {
        const int last = solver.endFrame < 0
                             ? duration - 1
                             : std::min(duration - 1, solver.endFrame);
        if (frame <= solver.startFrame || frame > last)
            continue;

        const bool positionDriven = solver.effect == SolverEffect::Gravity
                                    || solver.effect == SolverEffect::Orbit
                                    || solver.effect == SolverEffect::BlackHole
                                    || solver.effect == SolverEffect::Spiral;
        if (positionDriven
            && (track == QStringLiteral("x") || track == QStringLiteral("y")))
            return true;
        if (solver.effect == SolverEffect::Spin
            && track == QStringLiteral("rotation"))
            return true;
    }
    return false;
}

} // namespace pvf::next::detail
