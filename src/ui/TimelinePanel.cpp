#include "TimelinePanel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace pvf {

TimelinePanel::TimelinePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMinimumHeight(140);

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollX = value;
        update();
    });
    updateScrollBar();
}

void TimelinePanel::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;

    // Keep the playhead visible as the open timeline grows.
    const int playhead = int(m_frame * kPixelsPerFrame);
    const int left = m_scrollX;
    const int right = m_scrollX + visibleWidth() - 80;
    if (playhead > right)
        m_scrollX = playhead - visibleWidth() + 80;
    else if (playhead < left)
        m_scrollX = playhead;

    updateScrollBar();
    update();
}

void TimelinePanel::setSelectedEntity(EntityId id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    update();
}

int TimelinePanel::contentWidth() const
{
    // Mine-imator: content width = max(content, playhead) * zoom + view width.
    // The view width term keeps a full screen of ruler visible on an empty
    // project instead of stopping at the last keyframe.
    const int endFrame = std::max(m_project->timelineLength(), m_frame);
    return int(endFrame * kPixelsPerFrame) + visibleWidth();
}

int TimelinePanel::visibleWidth() const
{
    return std::max(1, width() - kNameWidth - kRightPad);
}

int TimelinePanel::xFromFrame(int frame) const
{
    return kNameWidth + int(std::lround(frame * kPixelsPerFrame)) - m_scrollX;
}

int TimelinePanel::frameFromX(int x) const
{
    return int(std::lround((x - kNameWidth + m_scrollX) / kPixelsPerFrame));
}

void TimelinePanel::updateScrollBar()
{
    if (!m_hScroll)
        return;
    const int maximum = std::max(0, contentWidth() - visibleWidth());
    m_scrollX = std::clamp(m_scrollX, 0, maximum);
    const QSignalBlocker blocker(m_hScroll);
    m_hScroll->setRange(0, maximum);
    m_hScroll->setPageStep(visibleWidth());
    m_hScroll->setSingleStep(60);
    m_hScroll->setValue(m_scrollX);
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kHeaderHeight)
        return -1;
    return (y - kHeaderHeight) / kRowHeight;
}

void TimelinePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_hScroll->setGeometry(kNameWidth, height() - kScrollBarHeight, visibleWidth(),
                           kScrollBarHeight);
    updateScrollBar();
}

void TimelinePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int bodyBottom = height() - kScrollBarHeight;
    const int right = width() - kRightPad;

    p.fillRect(rect(), QColor(28, 28, 32));
    p.fillRect(QRect(0, 0, kNameWidth, height()), QColor(34, 34, 39));
    p.fillRect(QRect(0, 0, width(), kHeaderHeight), QColor(38, 38, 44));

    p.setPen(QColor(60, 60, 68));
    p.drawLine(kNameWidth, 0, kNameWidth, bodyBottom);
    p.drawLine(0, kHeaderHeight, width(), kHeaderHeight);

    {
        QFont titleFont = p.font();
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(QColor(215, 215, 225));
        p.drawText(QRect(10, 0, kNameWidth - 20, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("时间轴"));
    }

    // Ruler.
    const int fps = std::max(1, m_project->fps);
    const double ppf = kPixelsPerFrame;
    int step = fps;
    while (step * ppf < 60.0)
        step *= 2;

    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    for (int f = 0; f <= frameFromX(right) + step; f += step)
    {
        const int x = xFromFrame(f);
        if (x > right)
            break;
        if (x < kNameWidth - 10)
            continue;
        p.setPen(QColor(70, 70, 78));
        p.drawLine(x, kHeaderHeight - 6, x, kHeaderHeight);
        p.setPen(QColor(150, 150, 160));
        const double seconds = f / double(fps);
        p.drawText(QRect(x + 4, 2, 80, kHeaderHeight - 4), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::number(seconds, 'g', 4) + QStringLiteral("s"));
    }

    // Entity rows.
    const auto& entities = m_project->entities();
    for (int i = 0; i < entities.size(); ++i)
    {
        const Entity& e = entities[i];
        const int top = kHeaderHeight + i * kRowHeight;
        if (top > bodyBottom)
            break;

        const QRect rowRect(0, top, width(), kRowHeight);
        if (e.id == m_selected)
            p.fillRect(rowRect, QColor(45, 58, 76));
        else if (i % 2)
            p.fillRect(rowRect, QColor(31, 31, 36));

        p.setPen(QColor(210, 210, 218));
        p.drawText(QRect(10, top, kNameWidth - 20, kRowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, e.name);

        QSet<int> frames;
        for (auto it = e.tracks.constBegin(); it != e.tracks.constEnd(); ++it)
            for (const Keyframe& k : it->keys)
                frames.insert(int(std::lround(k.frame)));

        p.setPen(Qt::NoPen);
        for (int f : frames)
        {
            const int x = xFromFrame(f);
            if (x < kNameWidth || x > right)
                continue;
            const int cy = top + kRowHeight / 2;
            const double r = 4.5;
            QPolygonF diamond;
            diamond << QPointF(x, cy - r) << QPointF(x + r, cy) << QPointF(x, cy + r)
                    << QPointF(x - r, cy);
            p.setBrush(e.id == m_selected ? QColor(255, 200, 90) : QColor(200, 160, 80));
            p.drawPolygon(diamond);
        }
    }

    if (entities.isEmpty())
    {
        p.setPen(QColor(140, 140, 150));
        p.drawText(QRect(kNameWidth, kHeaderHeight, visibleWidth(), bodyBottom - kHeaderHeight),
                   Qt::AlignCenter,
                   QStringLiteral("点击左上角工作台创建实体"));
    }

    // Playhead.
    const int px = xFromFrame(m_frame);
    if (px >= kNameWidth - 6 && px <= right + 6)
    {
        p.setPen(QPen(QColor(255, 77, 90), 1.5));
        p.drawLine(px, 0, px, bodyBottom);
        p.setBrush(QColor(255, 77, 90));
        p.setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(px - 6, 0) << QPointF(px + 6, 0) << QPointF(px, 9);
        p.drawPolygon(head);
    }

    // End-of-content marker, only when the project actually has content.
    const int length = m_project->timelineLength();
    if (length > 0)
    {
        const int lx = xFromFrame(length);
        if (lx >= kNameWidth && lx <= right)
        {
            p.setPen(QPen(QColor(120, 120, 135), 1, Qt::DashLine));
            p.drawLine(lx, kHeaderHeight, lx, bodyBottom);
        }
    }

    p.setPen(QColor(70, 70, 78));
    p.drawLine(0, bodyBottom - 1, width(), bodyBottom - 1);
}

void TimelinePanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const int bodyBottom = height() - kScrollBarHeight;
    if (event->position().y() >= bodyBottom)
        return;

    // Name column selects the entity row.
    if (event->position().x() < kNameWidth)
    {
        const int row = rowAt(int(event->position().y()));
        const auto& entities = m_project->entities();
        if (row >= 0 && row < entities.size())
            emit entitySelected(entities[row].id);
        return;
    }

    m_scrubbing = true;
    emit frameRequested(std::max(0, frameFromX(event->position().x())));
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing)
        return;
    emit frameRequested(std::max(0, frameFromX(event->position().x())));
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_scrubbing = false;
}

} // namespace pvf
