#include "TimelinePanel.h"

#include "app/AppIcon.h"

#include <QLineEdit>
#include <QKeyEvent>
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
    setMouseTracking(true);   // hover row buttons need motion without a press

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

int TimelinePanel::xFromFrame(int frame) const
{
    return kNameWidth + int(std::lround(frame * m_pixelsPerFrame)) - m_scrollX;
}

int TimelinePanel::frameFromX(int x) const
{
    return int(std::lround((x - kNameWidth + m_scrollX) / m_pixelsPerFrame));
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kHeaderHeight)
        return -1;
    return (y - kHeaderHeight) / kRowHeight;
}

EntityId TimelinePanel::entityAtRow(int row) const
{
    const QVector<int> visible = visibleEntities();
    if (row < 0 || row >= int(visible.size()))
        return kInvalidEntity;
    return m_project->entities()[visible[row]].id;
}

QVector<double> TimelinePanel::keyframeFrames(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    QVector<double> frames;
    if (!entity)
        return frames;

    QSet<int> seen;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            const int rounded = int(std::lround(key.frame));
            if (seen.contains(rounded))
                continue;
            seen.insert(rounded);
            frames.append(key.frame);
        }
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

bool TimelinePanel::hitKeyframe(int row, int x, EntityId* outId, double* outFrame) const
{
    const EntityId id = entityAtRow(row);
    if (id == kInvalidEntity)
        return false;

    const double frame = frameFromX(x);
    const double tolerance = std::max(1.0, 6.0 / m_pixelsPerFrame);

    bool found = false;
    double best = 0.0;
    double bestDistance = tolerance;
    for (const double keyFrame : keyframeFrames(id))
    {
        const double distance = std::abs(keyFrame - frame);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = keyFrame;
            found = true;
        }
    }

    if (found)
    {
        if (outId)
            *outId = id;
        if (outFrame)
            *outFrame = best;
    }
    return found;
}

QRect TimelinePanel::rowButtonRect(int row, int index) const
{
    const int top = kHeaderHeight + row * kRowHeight;
    const int right = width() - kRightPad;
    const int x = right - (index + 1) * (kButtonSize + 2);
    return QRect(x, top + (kRowHeight - kButtonSize) / 2, kButtonSize, kButtonSize);
}

void TimelinePanel::handleRowButton(int row, int index)
{
    const EntityId id = entityAtRow(row);
    Entity* entity = m_project->entity(id);
    if (!entity)
        return;

    switch (index)
    {
    case 0:   // Hide / show
        emit entityFlagChanged(id, EntityFlag::Hidden, !entity->hidden);
        break;
    case 1:   // Lock / unlock
        emit entityFlagChanged(id, EntityFlag::Locked, !entity->locked);
        break;
    case 2:   // Select every keyframe of the entity
    {
        m_selectedKeyframes.clear();
        for (const double frame : keyframeFrames(id))
            m_selectedKeyframes.append({id, frame});
        update();
        break;
    }
    default:
        break;
    }
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

void TimelinePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_search->setGeometry(8, 2, kNameWidth - 16, kHeaderHeight - 6);
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

    // Ruler.
    const int fps = std::max(1, m_project->fps);
    int step = fps;
    while (step * m_pixelsPerFrame < 60.0)
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

        p.setPen(e.hidden ? QColor(120, 120, 130) : QColor(210, 210, 218));
        p.drawText(QRect(10, top, kNameWidth - 20, kRowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, e.name);

        for (const double keyFrame : keyframeFrames(e.id))
        {
            const int x = xFromFrame(int(std::lround(keyFrame)));
            if (x < kNameWidth || x > right)
                continue;
            const int cy = top + kRowHeight / 2;
            const double r = 4.5;
            QPolygonF diamond;
            diamond << QPointF(x, cy - r) << QPointF(x + r, cy) << QPointF(x, cy + r)
                    << QPointF(x - r, cy);

            const bool isSelected =
                std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                          KeyframeSelection{e.id, keyFrame})
                != m_selectedKeyframes.end();

            p.setPen(Qt::NoPen);
            if (isSelected)
                p.setBrush(QColor(255, 255, 255));
            else if (e.id == m_selected)
                p.setBrush(QColor(255, 200, 90));
            else
                p.setBrush(QColor(200, 160, 80));
            p.drawPolygon(diamond);
        }

        // Row buttons appear on hover, like Mine-imator's timeline list.
        if (row == m_hoverRow)
        {
            makeEyeIcon(e.hidden).paint(&p, rowButtonRect(row, 0));
            makeLockIcon(e.locked).paint(&p, rowButtonRect(row, 1));
            makeKeyframeIcon().paint(&p, rowButtonRect(row, 2));
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

    // Rubber-band selection drawn on top.
    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        p.setPen(QPen(QColor(100, 160, 255), 1, Qt::DashLine));
        p.setBrush(QColor(100, 160, 255, 40));
        p.drawRect(box.intersected(QRectF(kNameWidth, kHeaderHeight, visibleWidth(),
                                          bodyBottom - kHeaderHeight)));
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

    const int x = int(event->position().x());
    const int y = int(event->position().y());

    // Ruler row: drag the playhead.
    if (y < kHeaderHeight)
    {
        if (x >= kNameWidth)
        {
            m_dragMode = DragMode::Scrub;
            emit frameRequested(std::max(0, frameFromX(x)));
        }
        return;
    }

    setFocus(Qt::MouseFocusReason);

    // Hover buttons take priority.
    if (m_hoverRow >= 0)
    {
        for (int index = 0; index < 3; ++index)
        {
            if (rowButtonRect(m_hoverRow, index).contains(x, y))
            {
                handleRowButton(m_hoverRow, index);
                return;
            }
        }
    }

    // Name column: select the entity row and drop keyframe selection.
    if (x < kNameWidth)
    {
        m_selectedKeyframes.clear();
        const EntityId id = entityAtRow(rowAt(y));
        if (id != kInvalidEntity)
            emit entitySelected(id);
        update();
        return;
    }

    // Keyframe dot: toggle the selection without moving the playhead.
    EntityId keyEntity = kInvalidEntity;
    double keyFrame = 0.0;
    if (hitKeyframe(rowAt(y), x, &keyEntity, &keyFrame))
    {
        const KeyframeSelection selection{keyEntity, keyFrame};
        const auto existing =
            std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(), selection);
        if (event->modifiers() & Qt::ShiftModifier)
        {
            if (existing != m_selectedKeyframes.end())
                m_selectedKeyframes.erase(existing);
            else
                m_selectedKeyframes.append(selection);
        }
        else
        {
            m_selectedKeyframes.clear();
            m_selectedKeyframes.append(selection);
        }
        emit entitySelected(keyEntity);
        update();
        return;
    }

    // Empty timeline area: remember the click; release jumps the playhead
    // unless the mouse turns this into a rubber-band selection.
    m_dragMode = DragMode::PendingSelect;
    m_pressPos = event->position();
    m_boxStart = event->position();
    m_boxEnd = event->position();
    m_pressFrame = std::max(0, frameFromX(x));
    m_pressEntity = entityAtRow(rowAt(y));
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    const int x = int(event->position().x());
    const int y = int(event->position().y());

    // Track the hovered row for the inline buttons.
    if (m_dragMode == DragMode::None)
    {
        const int row = (y >= kHeaderHeight) ? rowAt(y) : -1;
        if (row != m_hoverRow)
        {
            m_hoverRow = row;
            update();
        }
    }

    if (m_dragMode == DragMode::Scrub)
    {
        emit frameRequested(std::max(0, frameFromX(x)));
        return;
    }

    if (m_dragMode == DragMode::PendingSelect)
    {
        if ((event->position() - m_pressPos).manhattanLength() <= 5.0)
            return;
        m_dragMode = DragMode::BoxSelect;
        m_boxStart = m_pressPos;
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        m_boxEnd = event->position();
        update();
    }
}

void TimelinePanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !m_selectedKeyframes.isEmpty())
    {
        m_selectedKeyframes.clear();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete && !m_selectedKeyframes.isEmpty())
    {
        emit deleteKeyframesRequested();
        m_selectedKeyframes.clear();
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragMode == DragMode::PendingSelect)
    {
        // Mine-imator deselects everything on a plain click.
        m_selectedKeyframes.clear();
        emit frameRequested(m_pressFrame);
        if (m_pressEntity != kInvalidEntity)
            emit entitySelected(m_pressEntity);
    }
    else if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        const int firstRow = rowAt(int(box.top()));
        const int lastRow = rowAt(int(box.bottom()));
        const int firstFrame = std::max(0, frameFromX(int(box.left())));
        const int lastFrame = std::max(0, frameFromX(int(box.right())));

        const bool additive = event->modifiers() & Qt::ShiftModifier;
        if (!additive)
            m_selectedKeyframes.clear();

        const QVector<int> visible = visibleEntities();
        for (int row = std::max(0, firstRow);
             row <= std::min(lastRow, int(visible.size()) - 1); ++row)
        {
            const EntityId id = m_project->entities()[visible[row]].id;
            for (const double frame : keyframeFrames(id))
            {
                if (frame < firstFrame || frame > lastFrame)
                    continue;
                const KeyframeSelection selection{id, frame};
                if (std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(), selection)
                    == m_selectedKeyframes.end())
                    m_selectedKeyframes.append(selection);
            }
        }

        // Keep the entity selection in sync with the last touched row.
        if (lastRow >= 0 && lastRow < int(visible.size()))
            emit entitySelected(m_project->entities()[visible[lastRow]].id);
        update();
    }

    m_dragMode = DragMode::None;
    update();
}

} // namespace pvf
