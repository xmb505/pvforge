#include "TimelinePanel.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pvf {

TimelinePanel::TimelinePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索对象…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
        update();
    });

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
    const int playhead = int(m_frame * m_pixelsPerFrame);
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
    return int(endFrame * m_pixelsPerFrame) + visibleWidth();
}

int TimelinePanel::visibleWidth() const
{
    return std::max(1, width() - kNameWidth - kRightPad);
}

QVector<int> TimelinePanel::visibleEntities() const
{
    QVector<int> result;
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const auto& entities = m_project->entities();
    for (int i = 0; i < entities.size(); ++i)
        if (filter.isEmpty() || entities[i].name.contains(filter, Qt::CaseInsensitive))
            result.append(i);
    return result;
}

int TimelinePanel::xFromFrame(int frame) const
{
    return kNameWidth + int(std::lround(frame * m_pixelsPerFrame)) - m_scrollX;
}

int TimelinePanel::frameFromX(int x) const
{
    return int(std::lround((x - kNameWidth + m_scrollX) / m_pixelsPerFrame));
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
    m_search->setGeometry(8, 2, kNameWidth - 16, kHeaderHeight - 6);
    m_hScroll->setGeometry(kNameWidth, height() - kScrollBarHeight, visibleWidth(),
                           kScrollBarHeight);
    updateScrollBar();
}

void TimelinePanel::zoomAt(double factor, int anchorX)
{
    const double oldZoom = m_pixelsPerFrame;
    double nextZoom = std::clamp(oldZoom * factor, 0.25, 32.0);
    if (nextZoom > 1.0)
        nextZoom = std::round(nextZoom);
    if (std::abs(nextZoom - oldZoom) < 1e-6)
        return;

    // Keep the frame under the anchor at the same screen position.
    const double anchorFrame = (anchorX - kNameWidth + m_scrollX) / oldZoom;
    m_pixelsPerFrame = nextZoom;
    m_scrollX = int(std::lround(anchorFrame * nextZoom - (anchorX - kNameWidth)));
    updateScrollBar();
    update();
}

void TimelinePanel::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    if (event->modifiers() & Qt::ControlModifier)
    {
        zoomAt(std::pow(2.0, steps), int(event->position().x()));
        event->accept();
        return;
    }

    m_scrollX -= int(std::lround(steps * 80));
    updateScrollBar();
    update();
    event->accept();
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

    // Ruler.
    const int fps = std::max(1, m_project->fps);
    const double ppf = m_pixelsPerFrame;
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
    const QVector<int> visible = visibleEntities();
    for (int row = 0; row < visible.size(); ++row)
    {
        const Entity& e = entities[visible[row]];
        const int top = kHeaderHeight + row * kRowHeight;
        if (top > bodyBottom)
            break;

        const QRect rowRect(0, top, width(), kRowHeight);
        if (e.id == m_selected)
            p.fillRect(rowRect, QColor(45, 58, 76));
        else if (row % 2)
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
    else if (visible.isEmpty())
    {
        p.setPen(QColor(140, 140, 150));
        p.drawText(QRect(kNameWidth, kHeaderHeight, visibleWidth(), bodyBottom - kHeaderHeight),
                   Qt::AlignCenter, QStringLiteral("没有匹配的对象"));
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
        setFocus(Qt::MouseFocusReason);
        const QVector<int> visible = visibleEntities();
        if (row >= 0 && row < visible.size())
            emit entitySelected(entities[visible[row]].id);
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
