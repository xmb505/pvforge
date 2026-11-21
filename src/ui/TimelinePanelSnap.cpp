#include "TimelinePanel.h"

#include <QColor>
#include <QPalette>

#include <algorithm>
#include <climits>
#include <cmath>

namespace pvf {

// AppSettings currently has no timeline snap distance (snapPixels belongs to
// the stage gizmo), so the timeline owns the documented 8 px default.  The
// value is compared against xFromFrame() distances and therefore never scales
// with the zoom level.
int TimelinePanel::timelineSnapPx() const
{
    return std::max(1, kTimelineSnapPx);
}

QVector<TimelinePanel::SnapCandidate> TimelinePanel::snapCandidates() const
{
    // Playhead first, then markers, then the sibling clip edges.  The frame
    // set is de-duplicated while the priority is kept on the surviving entry:
    // the snap routines break pixel-distance ties by that rank instead of by
    // container order.
    QVector<SnapCandidate> candidates;
    QHash<int, int> indexByFrame;
    auto add = [&candidates, &indexByFrame](int frame, int priority) {
        if (frame < 0)
            return;
        const auto it = indexByFrame.constFind(frame);
        if (it == indexByFrame.constEnd())
        {
            indexByFrame.insert(frame, int(candidates.size()));
            candidates.append({frame, priority});
            return;
        }
        SnapCandidate& existing = candidates[it.value()];
        existing.priority = std::min(existing.priority, priority);
    };

    if (m_frame >= 0)
        add(m_frame, 0);
    for (const TimelineMarkerView& marker : m_snapshot.markers)
        add(marker.frame, 1);

    // Same-parent siblings contribute both closed-range edges: the left edge
    // is start, the painted right edge is end + 1.  Read the gesture-start
    // snapshot so a live preview cannot attract a clip towards its own moving
    // position; outside a gesture the current snapshot is already correct.
    const bool dragging = m_dragMode != DragMode::None
                          && !m_sessionBaseSnapshot.entities.isEmpty();
    const QVector<Entity>& siblings =
        dragging ? m_sessionBaseSnapshot.entities : activeEntities();
    for (const Entity& entity : siblings)
    {
        if (entity.parent != m_owner)
            continue;
        if (entity.id == m_dragEntity)
            continue;
        const Uuid uuid = uuidForEntityId(entity.id);
        if (!uuid.isEmpty() && m_dragRoots.contains(uuid))
            continue;
        // The snapshot exposes the closed model range as modelStart/modelEnd
        // with clipStart/clipEnd as the painted fallback.
        const int start =
            entity.data.value(QStringLiteral("modelStart"), clipStartOf(entity))
                .toInt();
        const int end =
            entity.data
                .value(QStringLiteral("modelEnd"), clipEndOf(entity) - 1)
                .toInt();
        add(start, 2);
        add(end + 1, 2);
    }
    return candidates;
}

QVector<int> TimelinePanel::snapTargetFrames() const
{
    QVector<int> targets;
    for (const SnapCandidate& candidate : snapCandidates())
        targets.append(candidate.frame);
    return targets;
}

TimelinePanel::TimelineSnapResult TimelinePanel::snapTrimStart(
    int proposedStart, int lower, int upper, int mouseX) const
{
    TimelineSnapResult result{proposedStart, -1};
    int bestDistance = timelineSnapPx() + 1;
    int bestPriority = INT_MAX;
    for (const SnapCandidate& candidate : snapCandidates())
    {
        const int line = candidate.frame;
        if (line < lower || line > upper)
            continue;
        const int distance = std::abs(mouseX - xFromFrame(line));
        if (distance < bestDistance
            || (distance == bestDistance
                && (candidate.priority < bestPriority
                    || (candidate.priority == bestPriority && result.line >= 0
                        && line < result.line))))
        {
            bestDistance = distance;
            bestPriority = candidate.priority;
            result.value = line;
            result.line = line;
        }
    }
    if (result.line < 0 || bestDistance > timelineSnapPx())
        return {proposedStart, -1};
    return result;
}

TimelinePanel::TimelineSnapResult TimelinePanel::snapTrimEnd(
    int proposedEnd, int lower, int upper, int mouseX) const
{
    TimelineSnapResult result{proposedEnd, -1};
    int bestDistance = timelineSnapPx() + 1;
    int bestPriority = INT_MAX;
    int bestSideRank = INT_MAX;
    for (const SnapCandidate& candidate : snapCandidates())
    {
        const int line = candidate.frame;
        // The right edge is painted at x(end + 1).  Both closed-range
        // readings are legal candidates:
        //   end == line      the clip includes the target frame and ends there
        //   end == line - 1  the painted edge sits exactly on the target line
        // Order them by the mouse side so equal pixel distances resolve to the
        // edge the user is actually approaching.
        const bool rightOfLine = mouseX >= xFromFrame(line);
        const int candidates[2] = {line, line - 1};
        for (int sideRank = 0; sideRank < 2; ++sideRank)
        {
            const int value =
                candidates[rightOfLine ? sideRank : 1 - sideRank];
            if (value < lower || value > upper)
                continue;
            const int distance =
                std::abs(mouseX - xFromFrame(value + 1));
            if (distance < bestDistance
                || (distance == bestDistance
                    && (candidate.priority < bestPriority
                        || (candidate.priority == bestPriority
                            && line == result.line
                            && sideRank < bestSideRank))))
            {
                bestDistance = distance;
                bestPriority = candidate.priority;
                bestSideRank = sideRank;
                result.value = value;
                result.line = line;
            }
        }
    }
    if (result.line < 0 || bestDistance > timelineSnapPx())
        return {proposedEnd, -1};
    return result;
}

TimelinePanel::TimelineSnapResult TimelinePanel::snapMoveDelta(
    int proposedDelta, int lowerDelta, int startFrame, int endExclusive) const
{
    TimelineSnapResult result{proposedDelta, -1};
    int bestDistance = timelineSnapPx() + 1;
    int bestPriority = INT_MAX;
    for (const SnapCandidate& candidate : snapCandidates())
    {
        const int line = candidate.frame;
        // The left edge is x(start), the right edge is x(end + 1).  The
        // distance that matters is from the *freely dragged* edge to the
        // target line; the candidate delta then parks that edge on the line.
        const int leftDelta = line - startFrame;
        const int rightDelta = line - endExclusive;
        const int deltas[2] = {leftDelta, rightDelta};
        for (int candidateDelta : deltas)
        {
            if (candidateDelta < lowerDelta)
                continue;
            const int freeEdge = candidateDelta == leftDelta
                                     ? startFrame + proposedDelta
                                     : endExclusive + proposedDelta;
            const int distance =
                std::abs(xFromFrame(freeEdge) - xFromFrame(line));
            const int currentBias = std::abs(result.value - proposedDelta);
            const int candidateBias = std::abs(candidateDelta - proposedDelta);
            if (distance < bestDistance
                || (distance == bestDistance
                    && (candidate.priority < bestPriority
                        || (candidate.priority == bestPriority
                            && result.line >= 0
                            && candidateBias < currentBias))))
            {
                bestDistance = distance;
                bestPriority = candidate.priority;
                result.value = candidateDelta;
                result.line = line;
            }
        }
    }
    if (result.line < 0 || bestDistance > timelineSnapPx())
        return {proposedDelta, -1};
    return result;
}

TimelinePanel::TimelineSnapResult TimelinePanel::snapMoveForDrag(
    int proposedDelta, int lowerDelta, int deltaRows) const
{
    if (m_dragRoots.size() > 1 || deltaRows != 0 || m_dragSnapDisabled)
        return {proposedDelta, -1};
    return snapMoveDelta(proposedDelta, lowerDelta, m_dragClipStart,
                         m_dragClipEnd + 1);
}

QColor TimelinePanel::snapHighlightColor(const QPalette& palette)
{
    QColor color = palette.color(QPalette::Highlight);
    if (!color.isValid() || color.hsvSaturation() < 20)
        return QColor(255, 196, 64);
    const int hue = color.hsvHue();
    // Rotate the palette accent to its complement so the snap feedback can
    // never be mistaken for the playhead, which keeps the accent unchanged.
    // Saturation/value are lifted until the line is readable on dark clips.
    color.setHsv((hue + 180) % 360, std::max(190, color.hsvSaturation()), 255,
                 255);
    return color;
}

void TimelinePanel::setSnapFrame(int line)
{
    if (m_snapFrame == line)
        return;
    m_snapFrame = line;
    update();
}

} // namespace pvf
