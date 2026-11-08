#include "WaveformView.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pvf {

WaveformView::WaveformView(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setObjectName(QStringLiteral("waveformView"));
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setObjectName(QStringLiteral("waveScroll"));
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollX = value;
        update();
    });

    m_vScroll = new QScrollBar(Qt::Vertical, this);
    m_vScroll->setObjectName(QStringLiteral("waveVScroll"));
    connect(m_vScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollY = value;
        update();
    });
    updateScrollBars();
}

void WaveformView::setCurrentFrame(int frame)
{
    frame = std::max(0, frame);
    if (m_frame == frame)
        return;
    m_frame = frame;

    const double playhead = m_frame * m_pixelsPerFrame;
    if (playhead > m_scrollX + plotWidth() - 40)
        m_scrollX = int(std::lround(playhead - plotWidth() + 40));
    else if (playhead < m_scrollX)
        m_scrollX = int(std::lround(playhead));
    updateScrollBars();
    update();
}

void WaveformView::refresh()
{
    updateScrollBars();
    update();
}

QVector<WaveformView::Lane> WaveformView::lanes() const
{
    QVector<Lane> result;
    int index = 0;
    for (const Entity& entity : m_project->entities())
    {
        if (!entity.isAudio())
            continue;
        result.append({entity.id, index});
        ++index;
    }
    return result;
}

int WaveformView::laneCountForTest() const
{
    return int(lanes().size());
}

QRect WaveformView::laneRectForTest(int lane) const
{
    return QRect(0, kRulerHeight + lane * kLaneHeight - m_scrollY, width(),
                 kLaneHeight);
}

int WaveformView::plotWidth() const
{
    return std::max(1, width() - kNameWidth - kScrollBarSize);
}

int WaveformView::contentWidth() const
{
    const int end = std::max(m_project->timelineLength(), m_frame);
    return int(end * m_pixelsPerFrame) + plotWidth();
}

void WaveformView::updateScrollBars()
{
    if (!m_hScroll || !m_vScroll)
        return;

    const int horizontalMaximum = std::max(0, contentWidth() - plotWidth());
    m_scrollX = std::clamp(m_scrollX, 0, horizontalMaximum);
    {
        const QSignalBlocker blocker(m_hScroll);
        m_hScroll->setRange(0, horizontalMaximum);
        m_hScroll->setPageStep(plotWidth());
        m_hScroll->setSingleStep(60);
        m_hScroll->setValue(m_scrollX);
        m_hScroll->setVisible(horizontalMaximum > 0);
    }

    const int bodyHeight = std::max(1, height() - kRulerHeight - kScrollBarSize);
    const int laneTotal = int(lanes().size()) * kLaneHeight;
    const int verticalMaximum = std::max(0, laneTotal - bodyHeight);
    m_scrollY = std::clamp(m_scrollY, 0, verticalMaximum);
    {
        const QSignalBlocker blocker(m_vScroll);
        m_vScroll->setRange(0, verticalMaximum);
        m_vScroll->setPageStep(bodyHeight);
        m_vScroll->setSingleStep(kLaneHeight);
        m_vScroll->setValue(m_scrollY);
        m_vScroll->setVisible(verticalMaximum > 0);
    }
}

double WaveformView::frameFromX(double x) const
{
    return (x - kNameWidth + m_scrollX) / m_pixelsPerFrame;
}

double WaveformView::xForFrame(double frame) const
{
    return kNameWidth + frame * m_pixelsPerFrame - m_scrollX;
}

int WaveformView::frameXForTest(double frame) const
{
    return int(std::lround(xForFrame(frame)));
}

int WaveformView::frameFromXForTest(int x) const
{
    return int(std::lround(frameFromX(x)));
}

void WaveformView::setPixelsPerFrameForTest(double pixels)
{
    m_pixelsPerFrame = std::clamp(pixels, 0.25, 64.0);
    updateScrollBars();
    update();
}

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor alt = pal.color(QPalette::AlternateBase);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::Text);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor accent = pal.color(QPalette::Highlight);

    const int right = width() - kScrollBarSize;
    const int bodyBottom = height() - kScrollBarSize;

    p.fillRect(rect(), base);
    p.fillRect(QRect(0, 0, width(), kRulerHeight), alt);
    p.fillRect(QRect(0, 0, kNameWidth, height()), alt);

    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    // Time ruler, same whole-second stepping as the timeline.
    const int fps = std::max(1, m_project->fps);
    int labelSeconds = 1;
    while (labelSeconds * fps * m_pixelsPerFrame < 90.0)
        labelSeconds *= 2;
    const int labelFrames = labelSeconds * fps;
    const double leftFrame = std::max(0.0, frameFromX(kNameWidth));
    for (int f = int(leftFrame / labelFrames) * labelFrames;; f += labelFrames)
    {
        const double x = xForFrame(f);
        if (x > right)
            break;
        if (x < kNameWidth - 2)
            continue;
        p.setPen(QPen(mid, 1.0));
        p.drawLine(QPointF(x, 0), QPointF(x, bodyBottom));
        p.setPen(text);
        p.drawText(QRectF(x + 3.0, 0.0, 80.0, kRulerHeight - 3.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(f / double(fps), 'g', 4) + QStringLiteral("s"));
    }

    p.setPen(mid);
    p.drawLine(kNameWidth, 0, kNameWidth, bodyBottom);
    p.drawLine(0, kRulerHeight, width(), kRulerHeight);

    const QVector<Lane> audioLanes = lanes();
    const double fpsScale = std::max(1, m_project->fps);

    p.save();
    p.setClipRect(QRect(kNameWidth, kRulerHeight, right - kNameWidth,
                        bodyBottom - kRulerHeight));
    for (int laneIndex = 0; laneIndex < audioLanes.size(); ++laneIndex)
    {
        const int top = kRulerHeight + laneIndex * kLaneHeight - m_scrollY;
        if (top + kLaneHeight <= kRulerHeight || top >= bodyBottom)
            continue;
        const Entity* entity = m_project->entity(audioLanes[laneIndex].id);
        if (!entity)
            continue;

        if (laneIndex % 2)
            p.fillRect(QRect(kNameWidth, top, right - kNameWidth, kLaneHeight),
                       QColor(alt.red(), alt.green(), alt.blue(), 120));

        const double fps = fpsScale;
        const double startFrame =
            entity->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        const double trimStart =
            entity->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            entity->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        const double duration =
            entity->data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double clipFrames =
            std::max(0.0, (duration - trimStart - trimEnd) * fps);
        if (clipFrames <= 0.0)
            continue;

        const double x0 = kNameWidth + startFrame * m_pixelsPerFrame - m_scrollX;
        const double x1 = x0 + clipFrames * m_pixelsPerFrame;
        const int left = std::max(kNameWidth, int(std::ceil(x0)));
        const int rightEdge = std::min(right, int(std::floor(x1)));
        if (rightEdge > left)
        {
            p.fillRect(QRect(left, top + 1, rightEdge - left, kLaneHeight - 2),
                       QColor(accent.red(), accent.green(), accent.blue(), 22));

            const int center = top + kLaneHeight / 2;
            const int half = std::max(4, kLaneHeight / 2 - 10);
            if (!entity->audio || entity->audio->peaks.isEmpty())
            {
                p.setPen(QPen(dim, 1.0, Qt::DashLine));
                p.drawLine(left, center, rightEdge, center);
                p.setPen(dim);
                p.drawText(QRect(left, top, rightEdge - left, kLaneHeight),
                           Qt::AlignCenter, QStringLiteral("音频未解码"));
            }
            else
            {
                const QVector<float>& peaks = entity->audio->peaks;
                p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(),
                                     210),
                              1.0));
                for (int x = left; x < rightEdge; ++x)
                {
                    const double local0 = (x - x0) / m_pixelsPerFrame;
                    const double local1 = (x + 1 - x0) / m_pixelsPerFrame;
                    const double source0 = trimStart + local0 / fps;
                    const double source1 = trimStart + local1 / fps;
                    int i0 = int(std::floor(source0 * 1000.0));
                    int i1 = int(std::ceil(source1 * 1000.0));
                    if (i1 <= 0 || i0 >= peaks.size())
                        continue;
                    i0 = std::clamp(i0, 0, int(peaks.size()) - 1);
                    i1 = std::clamp(i1, i0 + 1, int(peaks.size()));
                    float peak = 0.0f;
                    for (int i = i0; i < i1; ++i)
                        peak = std::max(peak, peaks[i]);
                    const int amplitude = int(std::lround(peak * half));
                    p.drawLine(x, center - amplitude, x, center + amplitude);
                }
            }

            p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 140),
                          1.0));
            p.drawLine(left, top + 1, left, top + kLaneHeight - 2);
            p.drawLine(rightEdge, top + 1, rightEdge, top + kLaneHeight - 2);
        }

        p.setPen(QPen(mid, 1.0));
        p.drawLine(kNameWidth, top + kLaneHeight, right, top + kLaneHeight);
    }
    p.restore();

    // Lane labels.
    p.save();
    p.setClipRect(QRect(0, kRulerHeight, kNameWidth, bodyBottom - kRulerHeight));
    for (int laneIndex = 0; laneIndex < audioLanes.size(); ++laneIndex)
    {
        const int top = kRulerHeight + laneIndex * kLaneHeight - m_scrollY;
        if (top + kLaneHeight <= kRulerHeight || top >= bodyBottom)
            continue;
        const Entity* entity = m_project->entity(audioLanes[laneIndex].id);
        if (!entity)
            continue;
        p.setPen(text);
        p.drawText(QRect(8, top + 6, kNameWidth - 16, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, entity->name);
        const double duration =
            entity->data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double startFrame =
            entity->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        p.setPen(dim);
        p.drawText(QRect(8, top + 28, kNameWidth - 16, 20),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("开始 %1 帧 · %2 秒")
                       .arg(int(std::lround(startFrame)))
                       .arg(duration, 0, 'f', 2));
    }
    p.restore();

    // Playhead.
    const double playhead = xForFrame(m_frame);
    if (playhead >= kNameWidth - 6 && playhead <= right + 6)
    {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(playhead, 0), QPointF(playhead, bodyBottom));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(playhead - 6.0, 0.0) << QPointF(playhead + 6.0, 0.0)
             << QPointF(playhead, 9.0);
        p.drawPolygon(head);
    }

    if (audioLanes.isEmpty())
    {
        p.setPen(dim);
        p.drawText(QRect(kNameWidth, kRulerHeight, right - kNameWidth,
                         bodyBottom - kRulerHeight),
                   Qt::AlignCenter,
                   QStringLiteral("还没有音频对象\n从工作台或素材库导入音频"));
    }

    // TODO: beat markers (tap tempo / imported cue points) go on top of the
    // waveform once the rest of the audio editing loop settles.
}

void WaveformView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
        m_hScroll->setGeometry(kNameWidth, height() - kScrollBarSize,
                               std::max(1, width() - kNameWidth - kScrollBarSize),
                               kScrollBarSize);
    if (m_vScroll)
        m_vScroll->setGeometry(width() - kScrollBarSize, kRulerHeight,
                               kScrollBarSize,
                               std::max(1, height() - kRulerHeight - kScrollBarSize));
    updateScrollBars();
}

void WaveformView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight || event->position().x() < kNameWidth)
        return;
    setFocus(Qt::MouseFocusReason);
    m_scrubbing = true;
    emit frameRequested(
        std::max(0, int(std::lround(frameFromX(event->position().x())))));
}

void WaveformView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing)
        return;
    emit frameRequested(
        std::max(0, int(std::lround(frameFromX(event->position().x())))));
}

void WaveformView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_scrubbing = false;
}

void WaveformView::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    if (event->modifiers() & Qt::ControlModifier)
    {
        const double oldZoom = m_pixelsPerFrame;
        double next = std::clamp(oldZoom * std::pow(2.0, steps), 0.25, 64.0);
        if (next > 1.0)
            next = std::round(next);
        if (std::abs(next - oldZoom) < 1e-6)
            return;
        const double anchor = frameFromX(event->position().x());
        m_pixelsPerFrame = next;
        m_scrollX = int(std::lround(anchor * next
                                    - (event->position().x() - kNameWidth)));
    }
    else if (event->modifiers() & Qt::ShiftModifier || !m_vScroll->isVisible())
    {
        m_scrollX -= int(std::lround(steps * 120.0));
    }
    else
    {
        m_scrollY -= int(std::lround(steps * kLaneHeight));
    }

    updateScrollBars();
    update();
    event->accept();
}

} // namespace pvf
