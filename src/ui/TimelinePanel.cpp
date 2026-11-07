#include "TimelinePanel.h"

#include "app/AppIcon.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "ui/EasingChooser.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

// Mine-imator's timeline tick density: the tick interval is measured in
// frames and gets coarser as the timeline zooms out.
void rulerSteps(double pixelsPerFrame, int* tickStep, int* labelStep)
{
    *tickStep = 1;
    *labelStep = 5;
    if (pixelsPerFrame < 5.0)
    {
        *tickStep = 5;
        *labelStep = 10;
    }
    if (pixelsPerFrame < 3.0)
    {
        *tickStep = 10;
        *labelStep = 50;
    }
    if (pixelsPerFrame < 1.0)
    {
        *tickStep = 20;
        *labelStep = 100;
    }
    if (pixelsPerFrame < 0.5)
    {
        *tickStep = 50;
        *labelStep = 200;
    }
}

// Seconds-mode ruler grid. Both intervals are measured in frames:
// `cellFrames` is the small tick spacing and `labelFrames` the spacing of
// numbered ticks. Cells always subdivide whole seconds evenly, so at 24 fps
// a second is 24, 12, 8, 6, 4, 3, 2 or 1 cells depending on the zoom.
struct SecondsGrid
{
    int labelFrames = 1;
    int cellFrames = 1;
};


SecondsGrid secondsGrid(int fps, double pixelsPerFrame)
{
    const int safeFps = std::max(1, fps);
    SecondsGrid grid;

    // Labels at least one second apart, coarser when zoomed out and finer
    // (half/quarter second) when zoomed in.
    int labelFrames = safeFps;
    while (labelFrames * pixelsPerFrame < 48.0)
        labelFrames *= 2;
    while (labelFrames > 1 && labelFrames % 2 == 0
           && labelFrames * pixelsPerFrame > 240.0)
        labelFrames /= 2;
    grid.labelFrames = labelFrames;

    // Pick the smallest whole-second subdivision that is still readable.
    grid.cellFrames = labelFrames;
    for (int step = 1; step <= labelFrames; ++step)
    {
        if (labelFrames % step != 0)
            continue;
        // A cell is either a slice of one second or a whole number of
        // seconds, never an arbitrary frame count.
        if (safeFps % step != 0 && step % safeFps != 0)
            continue;
        if (step * pixelsPerFrame >= 5.0)
        {
            grid.cellFrames = step;
            break;
        }
    }
    return grid;
}

} // namespace

TimelinePanel::TimelinePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);   // hover row buttons need motion without a press

    m_pixelsPerFrame = std::clamp(Settings::timelinePixelsPerFrame(), 0.25, 32.0);
    m_displayFrames = Settings::timelineDisplayFrames();

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(Lang::t(QStringLiteral("search.objects"), QStringLiteral("搜索对象…")));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
        update();
    });

    // Visible unit switch: the ruler is either pure frames or pure seconds.
    m_unitButton = new QToolButton(this);
    m_unitButton->setObjectName(QStringLiteral("timelineUnitButton"));
    m_unitButton->setToolTip(QStringLiteral("切换时间轴刻度单位（帧 / 秒）"));
    m_unitButton->setFocusPolicy(Qt::NoFocus);
    m_unitButton->setText(m_displayFrames ? QStringLiteral("帧") : QStringLiteral("秒"));
    connect(m_unitButton, &QToolButton::clicked, this, [this] {
        setDisplayFrames(!m_displayFrames);
        emit displayFramesChanged(m_displayFrames);
    });

    m_renameEdit = new QLineEdit(this);
    m_renameEdit->setObjectName(QStringLiteral("renameEdit"));
    m_renameEdit->hide();
    m_renameEdit->installEventFilter(this);
    connect(m_renameEdit, &QLineEdit::editingFinished, this, &TimelinePanel::commitRename);

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollX = value;
        update();
    });
    updateScrollBar();

    // Holding an arrow key keeps stepping at the project frame rate, which
    // turns into forward/backward playback. A quick tap moves one frame; the
    // repeat only starts after a short delay so a normal press never runs on.
    m_holdTimer = new QTimer(this);
    m_holdTimer->setTimerType(Qt::PreciseTimer);
    connect(m_holdTimer, &QTimer::timeout, this, [this] {
        if (m_holdDirection != 0)
            stepFrame(m_holdDirection);
    });

    m_holdDelay = new QTimer(this);
    m_holdDelay->setSingleShot(true);
    m_holdDelay->setInterval(400);
    connect(m_holdDelay, &QTimer::timeout, this, [this] {
        if (m_holdDirection == 0)
            return;
        m_holdTimer->setInterval(std::max(1, 1000 / std::max(1, m_project->fps)));
        m_holdTimer->start();
    });
}

void TimelinePanel::setDisplayFrames(bool frames)
{
    m_displayFrames = frames;
    if (m_unitButton)
        m_unitButton->setText(frames ? QStringLiteral("帧") : QStringLiteral("秒"));
    update();
}

void TimelinePanel::setPixelsPerFrameForTest(double pixels)
{
    m_pixelsPerFrame = std::clamp(pixels, 0.25, 32.0);
    updateScrollBar();
    update();
}

int TimelinePanel::rulerTickStepForTest() const
{
    int tickStep = 0;
    int labelStep = 0;
    rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
    return tickStep;
}

int TimelinePanel::rulerLabelStepForTest() const
{
    int tickStep = 0;
    int labelStep = 0;
    rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
    return labelStep;
}

int TimelinePanel::secondsCellStepForTest() const
{
    return secondsGrid(std::max(1, m_project->fps), m_pixelsPerFrame).cellFrames;
}

int TimelinePanel::secondsLabelStepForTest() const
{
    return secondsGrid(std::max(1, m_project->fps), m_pixelsPerFrame).labelFrames;
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
    // Mine-imator keeps these buttons at the end of the name column.
    const int top = kHeaderHeight + row * kRowHeight;
    const int x = kNameWidth - 8 - (index + 1) * (kButtonSize + 2);
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
    const int unitWidth = 40;
    m_search->setGeometry(8, 2, kNameWidth - 16 - unitWidth - 4, kHeaderHeight - 6);
    m_unitButton->setGeometry(kNameWidth - 8 - unitWidth, 2, unitWidth,
                              kHeaderHeight - 6);
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

    const QPalette pal = QApplication::palette();
    const QColor windowColor = pal.color(QPalette::Window);
    const QColor baseColor = pal.color(QPalette::Base);
    const QColor altColor = pal.color(QPalette::AlternateBase);
    const QColor midColor = pal.color(QPalette::Mid);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor dimColor = pal.color(QPalette::PlaceholderText);
    const QColor accentColor = pal.color(QPalette::Highlight);

    p.fillRect(rect(), windowColor);
    p.fillRect(QRect(0, 0, kNameWidth, height()), baseColor);
    p.fillRect(QRect(0, 0, width(), kHeaderHeight), altColor);

    p.setPen(midColor);
    p.drawLine(kNameWidth, 0, kNameWidth, bodyBottom);
    p.drawLine(0, kHeaderHeight, width(), kHeaderHeight);

    // Ruler. Frame mode follows Mine-imator's density table; seconds mode
    // subdivides every second into whole cells derived from the project
    // frame rate: at 24 fps a second is 24, 12, 8, 6, 4, 3, 2 or 1 cells.
    const int fps = std::max(1, m_project->fps);

    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    auto drawTick = [&](int f, int tickHeight, const QColor& color) {
        const int x = xFromFrame(f);
        if (x < kNameWidth || x > right)
            return;
        p.setPen(QPen(color, 1));
        p.drawLine(x, kHeaderHeight - tickHeight, x, kHeaderHeight);
    };
    auto drawLabel = [&](int f, const QString& text, const QColor& color) {
        const int x = xFromFrame(f);
        if (x < kNameWidth - 20 || x > right)
            return;
        p.setPen(color);
        p.drawText(QRect(x + 4, 2, 96, kHeaderHeight - 4),
                   Qt::AlignVCenter | Qt::AlignLeft, text);
    };

    const int leftFrame = std::max(0, frameFromX(kNameWidth));

    if (m_displayFrames)
    {
        int tickStep = 0;
        int labelStep = 0;
        rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);

        const int lastFrame = frameFromX(right) + tickStep;
        const int firstTick = (leftFrame / tickStep) * tickStep;

        // Mine-imator highlights every Nth frame with a longer notch.
        for (int f = firstTick; f <= lastFrame; f += tickStep)
        {
            const bool major = (f % labelStep == 0);
            drawTick(f, major ? 7 : 3, major ? textColor : midColor);
        }
        for (int f = firstTick; f <= lastFrame; f += tickStep)
        {
            if (f % labelStep == 0)
                drawLabel(f, QString::number(f), textColor);
        }
    }
    else
    {
        const SecondsGrid grid = secondsGrid(fps, m_pixelsPerFrame);
        const int lastFrame = frameFromX(right) + grid.cellFrames;
        const int firstCell = (leftFrame / grid.cellFrames) * grid.cellFrames;

        for (int f = firstCell; f <= lastFrame; f += grid.cellFrames)
            drawTick(f, 3, midColor);

        // Whole seconds get a taller notch when the cell grid is finer.
        if (fps * m_pixelsPerFrame >= 8.0)
        {
            const int firstSecond = (leftFrame / fps) * fps;
            for (int f = firstSecond; f <= lastFrame; f += fps)
            {
                if (f % grid.labelFrames != 0)
                    drawTick(f, 6, midColor);
            }
        }

        const int firstLabel = (leftFrame / grid.labelFrames) * grid.labelFrames;
        for (int f = firstLabel; f <= lastFrame; f += grid.labelFrames)
        {
            drawTick(f, 9, textColor);
            const double seconds = f / double(fps);
            const bool whole = std::abs(seconds - std::round(seconds)) < 1e-9;
            const QString text =
                (whole ? QString::number(int(std::round(seconds)))
                       : QString::number(seconds, 'g', 3))
                + QStringLiteral("s");
            drawLabel(f, text, textColor);
        }
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
            p.fillRect(rowRect, QColor(accentColor.red(), accentColor.green(),
                                        accentColor.blue(), 70));
        else if (row % 2)
            p.fillRect(rowRect, QColor(altColor.red(), altColor.green(), altColor.blue(), 120));

        if (e.id != m_renameEntity)
        {
            const QColor nameColor = e.hidden ? dimColor
                                   : e.locked ? dimColor
                                              : textColor;
            p.setPen(nameColor);
            p.drawText(QRect(10, top, kNameWidth - 10 - 3 * (kButtonSize + 2), kRowHeight),
                       Qt::AlignVCenter | Qt::AlignLeft, e.name);
        }

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
                p.setBrush(pal.color(QPalette::HighlightedText));
            else if (e.locked)
                p.setBrush(QColor(accentColor.red(), accentColor.green(),
                                  accentColor.blue(), 90));
            else if (e.id == m_selected)
                p.setBrush(accentColor);
            else
                p.setBrush(QColor(accentColor.red(), accentColor.green(),
                                  accentColor.blue(), 190));
            p.drawPolygon(diamond);

            const EasingSpec ease = keyframeEasingAt(e.id, keyFrame);
            if (ease.easing != Easing::Linear)
            {
                const QColor marker = ease.easing == Easing::Instant
                                          ? QColor(255, 150, 90, 210)
                                          : QColor(accentColor.red(), accentColor.green(),
                                                   accentColor.blue(), 200);
                p.setPen(QPen(marker, 1.5));
                p.drawLine(QPointF(x - 2.5, cy - r - 3.5),
                           QPointF(x + 2.5, cy - r - 3.5));
            }
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
        p.setPen(dimColor);
        p.drawText(QRect(kNameWidth, kHeaderHeight, visibleWidth(), bodyBottom - kHeaderHeight),
                   Qt::AlignCenter,
                   QStringLiteral("点击左上角工作台创建实体"));
    }
    else if (visible.isEmpty())
    {
        p.setPen(dimColor);
        p.drawText(QRect(kNameWidth, kHeaderHeight, visibleWidth(), bodyBottom - kHeaderHeight),
                   Qt::AlignCenter, QStringLiteral("没有匹配的对象"));
    }

    // Playhead.
    const int px = xFromFrame(m_frame);
    if (px >= kNameWidth - 6 && px <= right + 6)
    {
        p.setPen(QPen(accentColor, 1.5));
        p.drawLine(px, 0, px, bodyBottom);
        p.setBrush(accentColor);
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
            p.setPen(QPen(midColor, 1, Qt::DashLine));
            p.drawLine(lx, kHeaderHeight, lx, bodyBottom);
        }
    }

    // Rubber-band selection drawn on top.
    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        p.setPen(QPen(accentColor, 1, Qt::DashLine));
        p.setBrush(QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 50));
        p.drawRect(box.intersected(QRectF(kNameWidth, kHeaderHeight, visibleWidth(),
                                          bodyBottom - kHeaderHeight)));
    }

    p.setPen(midColor);
    p.drawLine(0, bodyBottom - 1, width(), bodyBottom - 1);
}

void TimelinePanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int x = int(event->position().x());
    const int y = int(event->position().y());
    if (x >= kNameWidth || y < kHeaderHeight)
        return;

    const int row = rowAt(y);
    const EntityId id = entityAtRow(row);
    const Entity* entity = m_project->entity(id);
    if (id == kInvalidEntity || !entity)
        return;

    m_renameEntity = id;
    m_renameEdit->setText(entity->name);
    m_renameEdit->setGeometry(6, kHeaderHeight + row * kRowHeight + 2, kNameWidth - 12,
                              kRowHeight - 4);
    m_renameEdit->show();
    m_renameEdit->selectAll();
    m_renameEdit->setFocus();
}

bool TimelinePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_renameEdit && event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
        {
            m_renameEdit->hide();
            m_renameEdit->clearFocus();
            m_renameEntity = kInvalidEntity;
            update();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TimelinePanel::commitRename()
{
    if (m_renameEntity == kInvalidEntity)
        return;

    const EntityId id = m_renameEntity;
    const QString name = m_renameEdit->text().trimmed();
    m_renameEntity = kInvalidEntity;
    m_renameEdit->hide();
    update();

    if (!name.isEmpty())
        emit entityRenamed(id, name);
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
            setFocus(Qt::MouseFocusReason);
            emit scrubStarted();
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
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && event->modifiers() == Qt::NoModifier)
    {
        const int direction = event->key() == Qt::Key_Right ? 1 : -1;

        // Auto-repeat is ignored: the hold timer does the continuous part.
        if (!event->isAutoRepeat())
        {
            emit scrubStarted();
            m_holdDirection = direction;
            stepFrame(direction);
            if (m_holdDirection != 0)
                m_holdDelay->start();
        }
        event->accept();
        return;
    }

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

EasingSpec TimelinePanel::keyframeEasingAt(EntityId id, double frame) const
{
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return EasingSpec();
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (std::abs(key.frame - frame) < 1e-6)
                return key.ease;
        }
    }
    return EasingSpec();
}

void TimelinePanel::showKeyframeMenu(const QPoint& globalPos)
{
    if (m_selectedKeyframes.isEmpty())
        return;

    const EasingSpec current = keyframeEasingAt(
        m_selectedKeyframes.first().entity, m_selectedKeyframes.first().frame);

    EasingSpec chosen;
    if (EasingChooser::choose(this, current, globalPos, &chosen))
        emit keyframeEasingRequested(chosen);
}

void TimelinePanel::contextMenuEvent(QContextMenuEvent* event)
{
    const QPoint pos = event->pos();
    if (pos.y() < kHeaderHeight || pos.x() < kNameWidth)
        return;

    EntityId entity = kInvalidEntity;
    double frame = 0.0;
    if (!hitKeyframe(rowAt(pos.y()), pos.x(), &entity, &frame))
        return;

    const KeyframeSelection selection{entity, frame};
    const auto existing =
        std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(), selection);
    if (existing == m_selectedKeyframes.end())
    {
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(selection);
        emit entitySelected(entity);
        update();
    }

    showKeyframeMenu(event->globalPos());
    event->accept();
}

void TimelinePanel::keyReleaseEvent(QKeyEvent* event){
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && !event->isAutoRepeat())
    {
        stopHold();
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void TimelinePanel::focusOutEvent(QFocusEvent* event)
{
    // A key release can be lost when the focus moves mid-hold.
    stopHold();
    QWidget::focusOutEvent(event);
}

void TimelinePanel::stepFrame(int delta)
{
    int next = m_frame + delta;

    if (next < 0)
    {
        next = 0;
        stopHold();
    }

    const int length = m_project->timelineLength();
    if (delta > 0 && length > 0 && next >= length)
    {
        if (m_project->loop)
        {
            next = 0;
        }
        else
        {
            next = length;
            stopHold();
        }
    }

    emit frameRequested(next);
}

void TimelinePanel::stopHold()
{
    m_holdDirection = 0;
    m_holdTimer->stop();
    m_holdDelay->stop();
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragMode == DragMode::PendingSelect)
    {
        // Mine-imator deselects everything on a plain click.
        m_selectedKeyframes.clear();
        emit scrubStarted();
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
