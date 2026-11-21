#include "TimelinePanel.h"

#include <QColor>
#include <QPalette>

#include <algorithm>
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

QVector<int> TimelinePanel::snapTargetFrames() const
{
    // Playhead first, then markers.  A marker sitting on the playhead is the
    // same visual line and must not become a second candidate.
    QVector<int> targets;
    if (m_frame >= 0)
        targets.append(m_frame);
    for (const TimelineMarkerView& marker : m_snapshot.markers)
        if (!targets.contains(marker.frame))
            targets.append(marker.frame);
    return targets;
}

TimelinePanel::TimelineSnapResult TimelinePanel::snapTrimStart(
    int proposedStart, int lower, int upper, int mouseX) const
{
    TimelineSnapResult result{proposedStart, -1};
    int bestDistance = timelineSnapPx() + 1;
    for (int line : snapTargetFrames())
    {
        if (line < lower || line > upper)
            continue;
        const int distance = std::abs(mouseX - xFromFrame(line));
        if (distance < bestDistance
            || (distance == bestDistance && result.line >= 0
                && line < result.line))
        {
            bestDistance = distance;
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
    for (int line : snapTargetFrames())
    {
        // The right edge is painted at x(end + 1).  Both closed-range
        // readings are legal candidates:
        //   end == line      the clip includes the target frame and ends there
        //   end == line - 1  the painted edge sits exactly on the target line
        // Order them by the mouse side so equal pixel distances resolve to the
        // edge the user is actually approaching.
        const bool rightOfLine = mouseX >= xFromFrame(line);
        const int candidates[2] = {line, line - 1};
        for (int index = 0; index < 2; ++index)
        {
            const int candidate = candidates[rightOfLine ? index : 1 - index];
            if (candidate < lower || candidate > upper)
                continue;
            const int distance =
                std::abs(mouseX - xFromFrame(candidate + 1));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                result.value = candidate;
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
    for (int line : snapTargetFrames())
    {
        // The left edge is x(start), the right edge is x(end + 1).  The
        // distance that matters is from the *freely dragged* edge to the
        // target line; the candidate delta then parks that edge on the line.
        const int leftDelta = line - startFrame;
        const int rightDelta = line - endExclusive;
        const int deltas[2] = {leftDelta, rightDelta};
        for (int candidate : deltas)
        {
            if (candidate < lowerDelta)
                continue;
            const int freeEdge = candidate == leftDelta
                                     ? startFrame + proposedDelta
                                     : endExclusive + proposedDelta;
            const int distance =
                std::abs(xFromFrame(freeEdge) - xFromFrame(line));
            const int currentBias = std::abs(result.value - proposedDelta);
            const int candidateBias = std::abs(candidate - proposedDelta);
            if (distance < bestDistance
                || (distance == bestDistance && result.line >= 0
                    && candidateBias < currentBias))
            {
                bestDistance = distance;
                result.value = candidate;
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
