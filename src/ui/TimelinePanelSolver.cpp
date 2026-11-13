#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/EditorSession.h"
#include "model/Project.h"
#include "model/Uuid.h"

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QString solverCacheKey(const Uuid& entityUuid, const Uuid& solverId)
{
    return entityUuid + QLatin1Char('/') + solverId;
}

} // namespace

void TimelinePanel::rebuildSolverStopCache()
{
    m_solverStopFrames.clear();
    if (!m_session)
        return;

    const model::Project& project = m_session->project();
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        for (const model::SolverSegment& solver : it->solvers)
        {
            // The session cache is the only source of the band tail.  A cold
            // cache is solved once by solverStopFrame() while paused; during
            // playback it stays read-only.
            const int stop = m_session->solverStopFrame(it.key(), solver.id);
            if (stop < 0)
                continue;
            ++m_solverBandSolveCount;
            m_solverStopFrames.insert(solverCacheKey(it.key(), solver.id), stop);
        }
    }
}

QRect TimelinePanel::solverBandRect(const Uuid& entityUuid,
                                    const Uuid& solverId) const
{
    if (!m_session || entityUuid.isEmpty())
        return {};
    const auto it = m_session->project().entities.constFind(entityUuid);
    if (it == m_session->project().entities.constEnd())
        return {};

    const model::SolverSegment* chosen = nullptr;
    for (const model::SolverSegment& solver : it->solvers)
        if (solver.id == solverId)
        {
            chosen = &solver;
            break;
        }
    if (!chosen)
        return {};

    const int stopFrame =
        m_solverStopFrames.value(solverCacheKey(entityUuid, solverId), -1);
    if (stopFrame < 0)
        return {};

    const EntityId id = entityIdForUuid(entityUuid);
    const int row = rowIds().indexOf(id);
    if (row < 0)
        return {};
    const Entity* legacy = ui::detail::findEntity(activeEntities(), id);
    if (!legacy)
        return {};

    const int clipLeftFrame = std::max(clipStartOf(*legacy), contentStart());
    const int clipRightFrame = std::min(clipEndOf(*legacy), contentEnd());
    const int bandStart =
        std::max(clipLeftFrame, it->start + chosen->startFrame);
    const int bandEnd = std::min(clipRightFrame, it->start + stopFrame);
    if (bandEnd <= bandStart)
        return {};

    const int x0 = std::max(nameWidth(), xFromFrame(bandStart));
    const int x1 = std::max(x0 + 1, xFromFrame(bandEnd));
    const QRect clip = clipRect(*legacy);
    if (clip.isEmpty())
        return {};
    const int left = std::max(clip.left(), x0);
    const int right = std::min(clip.right(), x1);
    if (right <= left)
        return {};
    return QRect(left, clip.top() + 1, right - left, clip.height() - 2);
}

#ifdef PVFORGE_TEST_HOOKS
int TimelinePanel::solverBandStopFrameForTest(const Uuid& entityUuid,
                                               const Uuid& solverId) const
{
    if (!m_session || entityUuid.isEmpty())
        return -1;
    if (!m_session->project().entities.contains(entityUuid))
        return -1;
    // Read-only: an empty cache means "unknown", never "solve now".
    return m_solverStopFrames.value(solverCacheKey(entityUuid, solverId), -1);
}

QRect TimelinePanel::solverBandRectForTest(const Uuid& entityUuid,
                                           const Uuid& solverId) const
{
    return solverBandRect(entityUuid, solverId);
}
#endif

} // namespace pvf
