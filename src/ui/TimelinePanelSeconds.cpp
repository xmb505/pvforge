// Whole-second grid for the timeline body and ruler.
//
// The grid sits on whole-second frames of the project frame rate (24/48/72 at
// 24 fps, 30/60/90 at 30 fps, ...).  When zooming out, one-second spacing
// would merge into a grey wall, so the step is widened through a fixed ladder
// of second counts.  The step is a pure function so the self-tests can pin the
// ladder without building a window.

#include "TimelinePanel.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QRegion>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace pvf {

namespace {

// One second is the finest spacing; below ~12 px per step the lines would
// blend into a wall, so the ladder widens through these counts.
constexpr int kSecondStepLadder[] = {1, 2, 5, 10, 30, 60};
constexpr double kMinStepPixels = 12.0;
// Once the ladder is exhausted the step keeps doubling.  The cap keeps an
// absurdly small zoom from overflowing the frame index.
constexpr int kMaxSecondStep = 1 << 20;

qint64 ceilDiv(qint64 value, qint64 divisor)
{
    if (divisor <= 0)
        return 0;
    // C++ division truncates towards zero, so a negative quotient is already
    // the ceiling; only a positive remainder needs one more step.
    const qint64 quotient = value / divisor;
    const qint64 remainder = value % divisor;
    return quotient + (remainder > 0 ? 1 : 0);
}

} // namespace

int TimelinePanel::secondGridStep(int fps, double pixelsPerFrame)
{
    const int safeFps = std::max(1, fps);
    const double pixelsPerSecond = safeFps * std::max(0.0, pixelsPerFrame);
    if (!(pixelsPerSecond > 0.0))
        return kMaxSecondStep;

    for (int step : kSecondStepLadder)
        if (pixelsPerSecond * step >= kMinStepPixels)
            return step;

    int step = kSecondStepLadder[std::size(kSecondStepLadder) - 1];
    while (step < kMaxSecondStep && pixelsPerSecond * step < kMinStepPixels)
        step *= 2;
    return step;
}

int TimelinePanel::secondGridFrames(int fps, double pixelsPerFrame)
{
    return secondGridStep(fps, pixelsPerFrame) * std::max(1, fps);
}

QVector<int> TimelinePanel::secondGridFrameList(int fps, double pixelsPerFrame,
                                                int originFrame, int firstFrame,
                                                int lastFrame)
{
    QVector<int> frames;
    const int stepFrames = secondGridFrames(fps, pixelsPerFrame);
    if (stepFrames <= 0 || lastFrame < firstFrame)
        return frames;

    // Align to whole seconds counted from originFrame: project frame 0 for
    // absolute time, the parent clip's start for relative private timelines.
    const qint64 step = stepFrames;
    const qint64 delta = qint64(firstFrame) - qint64(originFrame);
    qint64 frame = qint64(originFrame) + ceilDiv(delta, step) * step;

    for (; frame <= lastFrame; frame += step)
    {
        if (frame >= 0)
            frames.append(int(frame));
        if (frame > std::numeric_limits<int>::max() - step)
            break;
    }
    return frames;
}

int TimelinePanel::secondGridOrigin() const
{
    // A private timeline in relative mode counts seconds from its parent
    // clip's start; everyone else aligns to absolute project seconds.
    return (m_popup && m_relativeTime) ? contentStart() : 0;
}

QColor TimelinePanel::secondGridColor(const QPalette& palette)
{
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    constexpr double kMix = 0.32;
    const auto blend = [kMix](int from, int to) {
        return std::clamp(int(std::lround(from + (to - from) * kMix)), 0, 255);
    };
    return QColor(blend(base.red(), text.red()),
                  blend(base.green(), text.green()),
                  blend(base.blue(), text.blue()));
}

void TimelinePanel::paintSecondGrid(
    QPainter& painter, const QVector<int>& frames,
    const QHash<EntityId, QRect>& clipRects) const
{
    if (frames.isEmpty())
        return;
    const QColor color = secondGridColor(QApplication::palette());
    const int right = width() - kScrollBarSize;
    const int bottom = bodyBottom() - 1;

    // Clip fills are translucent (alpha 200), so a line drawn underneath
    // would stripe through them.  Cut the clip bodies out of the grid's clip
    // region: the grid still paints before the clips, but visually stays
    // behind them and never competes with the clips.
    QRegion region(QRect(nameWidth(), kRulerHeight,
                         std::max(0, right - nameWidth()),
                         std::max(0, bodyBottom() - kRulerHeight)));
    // Clips are inset by three pixels inside their lane; widen by the same
    // amount so the grid does not peek through above/below a clip.
    for (const QRect& clip : clipRects)
        region -= clip.adjusted(0, -3, 0, 3);

    painter.save();
    painter.setClipRegion(region, Qt::IntersectClip);
    painter.setPen(QPen(color, 2));
    for (int frame : frames)
    {
        const int x = xFromFrame(frame);
        if (x < nameWidth() || x > right)
            continue;
        painter.drawLine(x, kRulerHeight, x, bottom);
    }
    painter.restore();
}

} // namespace pvf
