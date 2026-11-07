#include "TimelinePanel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace pvf {

TimelinePanel::TimelinePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMinimumHeight(140);
}

void TimelinePanel::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    update();
}

void TimelinePanel::setSelectedEntity(EntityId id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    update();
}

int TimelinePanel::xFromFrame(double frame) const
{
    const int left = kNameWidth;
    const int right = std::max(left + 1, width() - kRightPad);
    const double t = std::clamp(frame / std::max(1, m_project->duration), 0.0, 1.0);
    return left + int(std::lround(t * (right - left)));
}

int TimelinePanel::frameFromX(double x) const
{
    const int left = kNameWidth;
    const int right = std::max(left + 1, width() - kRightPad);
    const double t = std::clamp((x - left) / double(right - left), 0.0, 1.0);
    return int(std::lround(t * m_project->duration));
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kHeaderHeight)
        return -1;
    return (y - kHeaderHeight) / kRowHeight;
}

void TimelinePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int right = std::max(kNameWidth + 1, width() - kRightPad);
    const int timeWidth = right - kNameWidth;

    p.fillRect(rect(), QColor(28, 28, 32));
    p.fillRect(QRect(0, 0, kNameWidth, height()), QColor(34, 34, 39));
    p.fillRect(QRect(0, 0, width(), kHeaderHeight), QColor(38, 38, 44));

    p.setPen(QColor(60, 60, 68));
    p.drawLine(kNameWidth, 0, kNameWidth, height());
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
    const double ppf = timeWidth / double(std::max(1, m_project->duration));
    int step = fps;
    while (step * ppf < 60.0 && step < m_project->duration)
        step *= 2;

    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    for (int f = 0; f <= m_project->duration; f += step)
    {
        const int x = xFromFrame(f);
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
        if (top > height())
            break;

        const QRect rowRect(0, top, width(), kRowHeight);
        if (e.id == m_selected)
            p.fillRect(rowRect, QColor(45, 58, 76));
        else if (i % 2)
            p.fillRect(rowRect, QColor(31, 31, 36));

        p.setPen(QColor(210, 210, 218));
        p.drawText(QRect(10, top, kNameWidth - 20, kRowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, e.name);

        // Merged keyframe positions across all tracks.
        QSet<int> frames;
        for (auto it = e.tracks.constBegin(); it != e.tracks.constEnd(); ++it)
            for (const Keyframe& k : it->keys)
                frames.insert(int(std::lround(k.frame)));

        p.setPen(Qt::NoPen);
        for (int f : frames)
        {
            const int x = xFromFrame(f);
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
        p.drawText(QRect(kNameWidth, kHeaderHeight, timeWidth, height() - kHeaderHeight),
                   Qt::AlignCenter,
                   QStringLiteral("导入图片后，修改属性会自动在当前帧打关键帧"));
    }

    // Playhead.
    const int px = xFromFrame(m_frame);
    p.setPen(QPen(QColor(255, 77, 90), 1.5));
    p.drawLine(px, 0, px, height());
    p.setBrush(QColor(255, 77, 90));
    p.setPen(Qt::NoPen);
    QPolygonF head;
    head << QPointF(px - 6, 0) << QPointF(px + 6, 0) << QPointF(px, 9);
    p.drawPolygon(head);

    p.setPen(QColor(70, 70, 78));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void TimelinePanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
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
    emit frameRequested(frameFromX(event->position().x()));
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubbing)
        return;
    emit frameRequested(frameFromX(event->position().x()));
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_scrubbing = false;
}

} // namespace pvf
