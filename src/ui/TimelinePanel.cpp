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
#include <QMenu>
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

    m_vScroll = new QScrollBar(Qt::Vertical, this);
    m_vScroll->setObjectName(QStringLiteral("timelineVScroll"));
    connect(m_vScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollY = value;
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
    // A click already updated the multi selection before emitting; do not
    // collapse it when the main window echoes the same id back.
    if (m_selected == id && (id == kInvalidEntity || m_entitySelection.contains(id)))
    {
        update();
        return;
    }
    m_selected = id;
    m_entitySelection.clear();
    if (id != kInvalidEntity)
    {
        m_entitySelection.insert(id);
        m_selectionAnchor = id;
    }
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
    const auto& entities = m_project->entities();
    const QVector<int> order = m_project->entityOrder();
    const QString filter = m_search ? m_search->text().trimmed() : QString();

    QSet<EntityId> searchVisible;
    if (!filter.isEmpty())
    {
        for (int i : order)
        {
            const Entity& e = entities[i];
            if (!e.name.contains(filter, Qt::CaseInsensitive))
                continue;
            searchVisible.insert(e.id);
            // Show the ancestors so the tree context remains visible.
            QSet<EntityId> seen;
            EntityId parent = e.parent;
            while (parent != kInvalidEntity && !seen.contains(parent))
            {
                seen.insert(parent);
                searchVisible.insert(parent);
                const Entity* parentEntity = m_project->entity(parent);
                if (!parentEntity)
                    break;
                parent = parentEntity->parent;
            }
            // A matching folder reveals its whole subtree.
            if (e.isFolder())
            {
                for (int j : order)
                {
                    const EntityId candidate = entities[j].id;
                    if (candidate != e.id
                        && m_project->isDescendantOf(candidate, e.id))
                    {
                        searchVisible.insert(candidate);
                    }
                }
            }
        }
    }

    QVector<int> result;
    for (int i : order)
    {
        const Entity& e = entities[i];
        if (!filter.isEmpty())
        {
            if (!searchVisible.contains(e.id))
                continue;
        }
        else
        {
            bool collapsed = false;
            QSet<EntityId> seen;
            EntityId parent = e.parent;
            while (parent != kInvalidEntity && !seen.contains(parent))
            {
                if (m_collapsedFolders.contains(parent))
                {
                    collapsed = true;
                    break;
                }
                seen.insert(parent);
                const Entity* parentEntity = m_project->entity(parent);
                if (!parentEntity)
                    break;
                parent = parentEntity->parent;
            }
            if (collapsed)
                continue;
        }
        result.append(i);
    }
    return result;
}

void TimelinePanel::updateScrollBar()
{
    if (!m_hScroll)
        return;
    const int maximum = std::max(0, contentWidth() - visibleWidth());
    m_scrollX = std::clamp(m_scrollX, 0, maximum);
    {
        const QSignalBlocker blocker(m_hScroll);
        m_hScroll->setRange(0, maximum);
        m_hScroll->setPageStep(visibleWidth());
        m_hScroll->setSingleStep(60);
        m_hScroll->setValue(m_scrollX);
    }

    if (!m_vScroll)
        return;
    // Entity rows scroll vertically when the list is taller than the body.
    int total = 0;
    const auto& entities = m_project->entities();
    for (int index : visibleEntities())
        total += entities[index].isAudio() ? kAudioRowHeight : kRowHeight;
    const int page = std::max(0, height() - kHeaderHeight - kScrollBarHeight);
    const int rowsMaximum = std::max(0, total - page);
    m_scrollY = std::clamp(m_scrollY, 0, rowsMaximum);
    const QSignalBlocker blocker(m_vScroll);
    m_vScroll->setRange(0, rowsMaximum);
    m_vScroll->setPageStep(page);
    m_vScroll->setSingleStep(kRowHeight);
    m_vScroll->setValue(m_scrollY);
    m_vScroll->setVisible(rowsMaximum > 0);
}

int TimelinePanel::xFromFrame(int frame) const
{
    return kNameWidth + int(std::lround(frame * m_pixelsPerFrame)) - m_scrollX;
}

int TimelinePanel::frameFromX(int x) const
{
    return int(std::lround((x - kNameWidth + m_scrollX) / m_pixelsPerFrame));
}

bool TimelinePanel::verticalScrollVisibleForTest() const
{
    return m_vScroll && m_vScroll->isVisible();
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kHeaderHeight)
        return -1;
    const QVector<int> visible = visibleEntities();
    int top = kHeaderHeight - m_scrollY;
    for (int row = 0; row < visible.size(); ++row)
    {
        const int height = rowHeight(row);
        if (y < top + height)
            return row;
        top += height;
    }
    return int(visible.size());
}

int TimelinePanel::rowTop(int row) const
{
    int top = kHeaderHeight - m_scrollY;
    for (int i = 0; i < row; ++i)
        top += rowHeight(i);
    return top;
}

int TimelinePanel::rowHeight(int row) const
{
    const Entity* entity = m_project->entity(entityAtRow(row));
    return entity && entity->isAudio() ? kAudioRowHeight : kRowHeight;
}

EntityId TimelinePanel::entityAtRow(int row) const
{
    const QVector<int> visible = visibleEntities();
    if (row < 0 || row >= int(visible.size()))
        return kInvalidEntity;
    return m_project->entities()[visible[row]].id;
}

int TimelinePanel::depthOf(EntityId id) const
{
    int depth = 0;
    QSet<EntityId> seen;
    EntityId parent = m_project->entity(id) ? m_project->entity(id)->parent
                                            : kInvalidEntity;
    while (parent != kInvalidEntity && !seen.contains(parent) && depth < 64)
    {
        seen.insert(parent);
        const Entity* parentEntity = m_project->entity(parent);
        if (!parentEntity)
            break;
        ++depth;
        parent = parentEntity->parent;
    }
    return depth;
}

QRect TimelinePanel::folderArrowRect(int row) const
{
    const Entity* entity = m_project->entity(entityAtRow(row));
    if (!entity || !entity->isFolder())
        return QRect();
    const int indent = depthOf(entity->id) * 14;
    const int top = rowTop(row);
    const int height = rowHeight(row);
    return QRect(6 + indent, top + (height - 14) / 2, 14, 14);
}

QVector<EntityId> TimelinePanel::rowDragRoots(EntityId clicked) const
{
    QVector<EntityId> roots;
    if (!m_entitySelection.contains(clicked))
    {
        roots.append(clicked);
        return roots;
    }

    // Drag the whole selection, keeping layer order and dropping children of
    // selected folders (the folder carries them).
    for (int index : m_project->entityOrder())
    {
        const Entity& entity = m_project->entities()[index];
        if (!m_entitySelection.contains(entity.id) || entity.isAudio()
            || m_project->isEffectivelyLocked(entity.id))
            continue;
        bool nested = false;
        for (EntityId root : roots)
            if (m_project->isDescendantOf(entity.id, root))
            {
                nested = true;
                break;
            }
        if (!nested)
            roots.append(entity.id);
    }
    if (roots.isEmpty())
        roots.append(clicked);
    return roots;
}

double TimelinePanel::dragFrame(EntityId id, double frame) const
{
    if (m_dragMode != DragMode::MoveKeyframe || m_kfDragDelta == 0)
        return frame;
    const auto selected =
        std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                  KeyframeSelection{id, frame});
    if (selected == m_selectedKeyframes.end())
        return frame;
    return std::max(0.0, frame + m_kfDragDelta);
}

void TimelinePanel::updateRowDropTarget(const QPointF& pos)
{
    m_dropRow = -1;
    m_dropMode = DropMode::None;
    const int y = int(pos.y());
    const int bodyBottom = height() - kScrollBarHeight;
    if (y < kHeaderHeight || y >= bodyBottom)
    {
        update();
        return;
    }

    const int count = int(visibleEntities().size());
    if (count == 0)
    {
        update();
        return;
    }

    int row = rowAt(y);
    if (row < 0)
        row = 0;
    if (row >= count)
        row = count - 1;
    const Entity* target = m_project->entity(entityAtRow(row));
    if (!target)
    {
        update();
        return;
    }

    // Never drop a folder into itself or one of its descendants.
    bool invalidTarget = m_project->isEffectivelyLocked(target->id);
    for (EntityId draggedId : m_rowDragIds)
        if (target->id == draggedId
            || m_project->isDescendantOf(target->id, draggedId))
        {
            invalidTarget = true;
            break;
        }
    if (invalidTarget)
    {
        update();
        return;
    }

    const int top = rowTop(row);
    const int rowHeightPx = rowHeight(row);
    const bool middle = y >= top + rowHeightPx / 4
                        && y <= top + rowHeightPx * 3 / 4;
    bool canParent = !m_rowDragIds.isEmpty();
    for (EntityId draggedId : m_rowDragIds)
    {
        const Entity* dragged = m_project->entity(draggedId);
        if (!dragged || dragged->isAudio()
            || (target->isCamera() && dragged->isCamera()))
        {
            canParent = false;
            break;
        }
    }
    const bool targetCanParent = target->isFolder() || target->isCamera();
    if (targetCanParent && middle && canParent
        && !m_collapsedFolders.contains(target->id))
    {
        m_dropRow = row;
        m_dropMode = DropMode::Into;
    }
    else
    {
        m_dropRow = row;
        m_dropMode = y < top + rowHeightPx / 2 ? DropMode::Above : DropMode::Below;
    }
    update();
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
    const Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
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
    const int top = rowTop(row);
    const int height = rowHeight(row);
    const int x = kNameWidth - 8 - (index + 1) * (kButtonSize + 2);
    return QRect(x, top + (height - kButtonSize) / 2, kButtonSize, kButtonSize);
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
        if (!entity->locked)
        {
            m_selectedKeyframes.erase(
                std::remove_if(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                               [id](const KeyframeSelection& selection) {
                                   return selection.entity == id;
                               }),
                m_selectedKeyframes.end());
            update();
        }
        emit entityFlagChanged(id, EntityFlag::Locked, !entity->locked);
        break;
    case 2:   // Select every keyframe of the entity
    {
        if (m_project->isEffectivelyLocked(id))
            break;
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
    const double vsteps = event->angleDelta().y() / 120.0;
    const double hsteps = event->angleDelta().x() / 120.0;
    if (vsteps == 0.0 && hsteps == 0.0)
        return;

    if (event->modifiers() & Qt::ControlModifier)
    {
        zoomAt(std::pow(2.0, vsteps != 0.0 ? vsteps : hsteps),
               int(event->position().x()));
        event->accept();
        return;
    }

    // Shift + wheel scrolls the time axis, the plain wheel scrolls the
    // entity rows. Some platforms report Shift as a horizontal delta.
    if ((event->modifiers() & Qt::ShiftModifier) || hsteps != 0.0)
    {
        const double amount = hsteps != 0.0 ? hsteps : vsteps;
        m_scrollX -= int(std::lround(amount * 120));
    }
    else
    {
        m_scrollY -= int(std::lround(vsteps * kRowHeight * 4));
    }
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
    if (m_vScroll)
        m_vScroll->setGeometry(width() - kScrollBarHeight, kHeaderHeight,
                               kScrollBarHeight,
                               height() - kHeaderHeight - kScrollBarHeight);
    updateScrollBar();
}

void TimelinePanel::leaveEvent(QEvent* event)
{
    if (m_dragMode != DragMode::MoveAudio)
        unsetCursor();
    QWidget::leaveEvent(event);
}

bool TimelinePanel::audioClipRange(const Entity& entity, double* left, double* right) const
{
    if (!entity.isAudio())
        return false;
    const double fps = std::max(1, m_project->fps);
    const double startFrame =
        entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    const double trimStart =
        entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd = entity.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double duration =
        entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double clipFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (clipFrames <= 0.0)
        return false;
    const double x0 = kNameWidth + startFrame * m_pixelsPerFrame - m_scrollX;
    *left = x0;
    *right = x0 + clipFrames * m_pixelsPerFrame;
    return true;
}

EntityId TimelinePanel::audioClipAt(int x, int y) const
{
    if (x < kNameWidth || y < kHeaderHeight)
        return kInvalidEntity;
    const Entity* entity = m_project->entity(entityAtRow(rowAt(y)));
    double left = 0.0;
    double right = 0.0;
    if (!entity || !audioClipRange(*entity, &left, &right))
        return kInvalidEntity;
    constexpr double kGrabPadding = 4.0;
    if (x < left - kGrabPadding || x > right + kGrabPadding)
        return kInvalidEntity;
    return entity->id;
}

void TimelinePanel::updateHoverCursor(const QPointF& pos)
{
    if (m_dragMode == DragMode::MoveAudio || m_dragMode == DragMode::MoveRow
        || m_dragMode == DragMode::PendingRowDrag)
    {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const int x = int(pos.x());
    const int y = int(pos.y());
    if (x < kNameWidth)
    {
        const Entity* entity =
            (y >= kHeaderHeight && y < height() - kScrollBarHeight)
                ? m_project->entity(entityAtRow(rowAt(y)))
                : nullptr;
        if (entity && !m_project->isEffectivelyLocked(entity->id))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
        return;
    }
    const Entity* clip =
        (y >= kHeaderHeight && y < height() - kScrollBarHeight)
            ? m_project->entity(audioClipAt(x, y))
            : nullptr;
    if (clip && !m_project->isEffectivelyLocked(clip->id))
    {
        setCursor(Qt::OpenHandCursor);
    }
    else
    {
        unsetCursor();
    }
}

void TimelinePanel::drawAudioWaveform(QPainter& p, const Entity& entity,
                                       const QRect& row, int right)
{
    const double fps = std::max(1, m_project->fps);
    double startFrame =
        entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    if (m_dragMode == DragMode::MoveAudio && entity.id == m_audioDragId)
        startFrame = std::max(0.0, m_audioDragStart + m_audioDragOffset);
    const double trimStart =
        entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd = entity.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double duration =
        entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double clipFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (clipFrames <= 0.0)
        return;

    const double x0 = kNameWidth + startFrame * m_pixelsPerFrame - m_scrollX;
    const double x1 = x0 + clipFrames * m_pixelsPerFrame;
    const int left = std::max(kNameWidth, int(std::ceil(x0)));
    const int rightEdge = std::min(right, int(std::floor(x1)));
    if (rightEdge <= left)
        return;

    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    const bool selected = entity.id == m_selected;
    p.fillRect(QRect(left, row.top() + 1, rightEdge - left, row.height() - 2),
               QColor(accent.red(), accent.green(), accent.blue(),
                      selected ? 70 : 22));

    const int center = row.center().y();
    const int half = std::max(3, row.height() / 2 - 3);

    if (!entity.audio || entity.audio->peaks.isEmpty())
    {
        p.setPen(QPen(QApplication::palette().color(QPalette::PlaceholderText), 1.0,
                      Qt::DashLine));
        p.drawLine(left, center, rightEdge, center);
        p.setPen(QApplication::palette().color(QPalette::PlaceholderText));
        p.drawText(QRect(left, row.top(), rightEdge - left, row.height()),
                   Qt::AlignCenter, QStringLiteral("音频未解码"));
        return;
    }

    const QVector<float>& peaks = entity.audio->peaks;
    p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 210), 1.0));
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

    p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 180), 1.0));
    const int startX = int(std::lround(x0));
    const int endX = int(std::lround(x1));
    if (startX >= kNameWidth && startX <= right)
        p.drawLine(startX, row.top() + 1, startX, row.bottom() - 1);
    if (endX >= kNameWidth && endX <= right)
        p.drawLine(endX, row.top() + 1, endX, row.bottom() - 1);

    // Top and bottom rails make the waveform read as a draggable clip block.
    p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(),
                         selected ? 150 : 90),
                  1.0));
    p.drawLine(left, row.top() + 1, rightEdge, row.top() + 1);
    p.drawLine(left, row.bottom() - 1, rightEdge, row.bottom() - 1);
}

void TimelinePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    updateScrollBar();

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
    p.save();
    p.setClipRect(QRect(0, kHeaderHeight, width(), bodyBottom - kHeaderHeight));
    for (int row = 0; row < visible.size(); ++row)
    {
        const Entity& e = entities[visible[row]];
        const int top = rowTop(row);
        const int height = rowHeight(row);
        if (top > bodyBottom)
            break;

        const QRect rowRect(0, top, width(), height);
        if ((m_entitySelection.contains(e.id)
             || (m_entitySelection.isEmpty() && e.id == m_selected))
            && !e.isAudio())
            p.fillRect(rowRect, QColor(accentColor.red(), accentColor.green(),
                                        accentColor.blue(), 70));
        else if (row % 2)
            p.fillRect(rowRect, QColor(altColor.red(), altColor.green(), altColor.blue(), 120));

        const int depth = depthOf(e.id);
        const int indent = depth * 14;
        const bool effectivelyHidden = m_project->isEffectivelyHidden(e.id);
        const bool effectivelyLocked = m_project->isEffectivelyLocked(e.id);

        // Subtle tree guides make nested layers readable.
        if (depth > 0)
        {
            QColor guide = midColor;
            guide.setAlpha(90);
            p.setPen(QPen(guide, 1));
            for (int level = 0; level < depth; ++level)
            {
                const int gx = 12 + level * 14;
                p.drawLine(gx, top, gx, top + height - 1);
            }
        }

        if (e.isFolder())
        {
            const QRect arrow = folderArrowRect(row);
            const QPointF center = arrow.center();
            QPolygonF triangle;
            if (m_collapsedFolders.contains(e.id))
            {
                triangle << QPointF(center.x() - 3, center.y() - 5)
                         << QPointF(center.x() + 4, center.y())
                         << QPointF(center.x() - 3, center.y() + 5);
            }
            else
            {
                triangle << QPointF(center.x() - 5, center.y() - 3)
                         << QPointF(center.x() + 5, center.y() - 3)
                         << QPointF(center.x(), center.y() + 4);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(effectivelyLocked ? dimColor : textColor);
            p.drawPolygon(triangle);
        }

        if (e.id != m_renameEntity)
        {
            const QColor nameColor =
                (effectivelyHidden || effectivelyLocked) ? dimColor : textColor;
            const int nameX = (e.isFolder() ? 24 : 10) + indent;
            p.setPen(nameColor);
            p.drawText(QRect(nameX, top, kNameWidth - nameX - 3 * (kButtonSize + 2),
                             height),
                       Qt::AlignVCenter | Qt::AlignLeft, e.name);
        }

        if (e.isAudio())
            drawAudioWaveform(p, e, rowRect, right);

        for (const double keyFrame : keyframeFrames(e.id))
        {
            const double shownFrame = dragFrame(e.id, keyFrame);
            const int x = xFromFrame(int(std::lround(shownFrame)));
            if (x < kNameWidth || x > right)
                continue;
            const int cy = top + height / 2;
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
            else if (effectivelyLocked)
                p.setBrush(QColor(accentColor.red(), accentColor.green(),
                                  accentColor.blue(), 90));
            else if (e.id == m_selected)
                p.setBrush(accentColor);
            else
                p.setBrush(QColor(accentColor.red(), accentColor.green(),
                                  accentColor.blue(), 190));
            p.drawPolygon(diamond);

            const EasingSpec ease = keyframeEasingAt(e.id, keyFrame);
            if (ease.easing != Easing::Linear
                && m_project->previousKeyframeFrame(e.id, keyFrame) >= 0.0)
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

    p.restore();

    if (m_dragMode == DragMode::MoveRow && m_dropRow >= 0
        && m_dropMode != DropMode::None)
    {
        const int top = rowTop(m_dropRow);
        const int height = rowHeight(m_dropRow);
        const QColor accent(accentColor.red(), accentColor.green(), accentColor.blue());
        if (m_dropMode == DropMode::Into)
        {
            p.fillRect(QRect(0, top, width(), height),
                       QColor(accent.red(), accent.green(), accent.blue(), 40));
            p.setPen(QPen(accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRect(1, top + 1, width() - 2, height - 2));
        }
        else
        {
            const int y = m_dropMode == DropMode::Above ? top : top + height - 1;
            p.setPen(QPen(accent, 2));
            p.drawLine(0, y, width(), y);
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
    if (id == kInvalidEntity || !entity
        || m_project->isEffectivelyLocked(id))
        return;

    m_renameEntity = id;
    m_renameEdit->setText(entity->name);
    m_renameEdit->setGeometry(6, rowTop(row) + 2, kNameWidth - 12,
                              rowHeight(row) - 4);
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

    // Name column: select the entity row, toggle folders and start a layer
    // drag. Clicking the arrow collapses/expands a folder.
    if (x < kNameWidth)
    {
        const int row = rowAt(y);
        const EntityId id = entityAtRow(row);
        const Entity* entity = m_project->entity(id);
        if (entity && entity->isFolder() && folderArrowRect(row).contains(x, y))
        {
            if (m_collapsedFolders.contains(id))
                m_collapsedFolders.remove(id);
            else
                m_collapsedFolders.insert(id);
            update();
            return;
        }

        m_selectedKeyframes.clear();
        if (entity && !m_project->isEffectivelyLocked(id))
        {
            const bool ctrl = event->modifiers()
                              & (Qt::ControlModifier | Qt::MetaModifier);
            const bool shift = event->modifiers() & Qt::ShiftModifier;
            if (shift && m_selectionAnchor != kInvalidEntity)
            {
                const QVector<int> visible = visibleEntities();
                int anchorRow = -1;
                for (int i = 0; i < int(visible.size()); ++i)
                    if (m_project->entities()[visible[i]].id == m_selectionAnchor)
                        anchorRow = i;
                if (anchorRow >= 0)
                {
                    m_entitySelection.clear();
                    const int low = std::min(anchorRow, row);
                    const int high = std::max(anchorRow, row);
                    for (int i = low; i <= high; ++i)
                        m_entitySelection.insert(m_project->entities()[visible[i]].id);
                }
                m_selected = id;
                emit entitySelected(id);
            }
            else if (ctrl)
            {
                if (m_entitySelection.contains(id))
                {
                    m_entitySelection.remove(id);
                    if (m_selected == id)
                    {
                        m_selected = m_entitySelection.isEmpty()
                                         ? kInvalidEntity
                                         : *m_entitySelection.begin();
                        emit entitySelected(m_selected);
                    }
                }
                else
                {
                    m_entitySelection.insert(id);
                    m_selected = id;
                    m_selectionAnchor = id;
                    emit entitySelected(id);
                }
            }
            else
            {
                // Mine-imator keeps an existing multi selection when a
                // selected row is pressed, so it can be dragged as a group.
                if (!m_entitySelection.contains(id))
                {
                    m_entitySelection.clear();
                    m_entitySelection.insert(id);
                    m_selectionAnchor = id;
                }
                m_selected = id;
                emit entitySelected(id);
            }
            m_rowDragId = id;
            m_rowDragIds = rowDragRoots(id);
            m_pressPos = event->position();
            m_dropRow = -1;
            m_dropMode = DropMode::None;
            m_dragMode = DragMode::PendingRowDrag;
        }
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
        const bool ctrl =
            event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        const bool shift = event->modifiers() & Qt::ShiftModifier;

        if (existing != m_selectedKeyframes.end())
        {
            // Mine-imator: Ctrl removes a selected keyframe, plain press
            // starts moving the whole selection.
            if (ctrl)
            {
                m_selectedKeyframes.erase(existing);
                update();
                return;
            }
        }
        else
        {
            if (!shift)
                m_selectedKeyframes.clear();
            m_selectedKeyframes.append(selection);
        }
        emit entitySelected(keyEntity);

        m_kfDragRecords.clear();
        for (const KeyframeSelection& selected : m_selectedKeyframes)
        {
            if (m_project->isEffectivelyLocked(selected.entity))
                continue;
            const Entity* entity = m_project->entity(selected.entity);
            if (!entity)
                continue;
            for (auto it = entity->tracks.begin(); it != entity->tracks.end(); ++it)
                for (const Keyframe& key : it->keys)
                    if (std::abs(key.frame - selected.frame) < 1e-6)
                        m_kfDragRecords.append({selected.entity, it.key(), key.frame,
                                                key.value, key.ease});
        }
        m_kfDragStartFrame = frameFromX(x);
        m_kfDragDelta = 0;
        m_dragMode = DragMode::MoveKeyframe;
        update();
        return;
    }

    // Audio clip body: drag the clip to a new start frame.
    const EntityId audioId = audioClipAt(x, y);
    Entity* audio = m_project->entity(audioId);
    if (audio)
    {
        m_selectedKeyframes.clear();
        m_pressFrame = std::max(0, frameFromX(x));
        if (!m_project->isEffectivelyLocked(audioId))
        {
            emit entitySelected(audioId);
            m_dragMode = DragMode::MoveAudio;
            m_audioDragId = audioId;
            m_audioDragStart =
                audio->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
            m_audioDragOffset = 0.0;
            m_pressPos = event->position();
            setCursor(Qt::ClosedHandCursor);
        }
        else
        {
            emit scrubStarted();
            emit frameRequested(m_pressFrame);
        }
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
        updateHoverCursor(event->position());
    }

    if (m_dragMode == DragMode::Scrub)
    {
        emit frameRequested(std::max(0, frameFromX(x)));
        return;
    }

    if (m_dragMode == DragMode::PendingRowDrag)
    {
        if ((event->position() - m_pressPos).manhattanLength() <= 4.0)
            return;
        m_dragMode = DragMode::MoveRow;
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta = frameFromX(int(event->position().x())) - m_kfDragStartFrame;
        if (delta != m_kfDragDelta)
        {
            m_kfDragDelta = delta;
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::MoveRow)
    {
        updateRowDropTarget(event->position());
        return;
    }

    if (m_dragMode == DragMode::MoveAudio)
    {
        const double deltaFrames =
            (event->position().x() - m_pressPos.x()) / m_pixelsPerFrame;
        const double start =
            std::max(0.0, m_audioDragStart + std::round(deltaFrames));
        m_audioDragOffset = start - m_audioDragStart;
        update();
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

    double previous = -1.0;
    KeyframeSelection base;
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        const double candidate =
            m_project->previousKeyframeFrame(selection.entity, selection.frame);
        if (candidate >= 0.0)
        {
            previous = candidate;
            base = selection;
            break;
        }
    }

    if (previous < 0.0)
    {
        QMenu menu(this);
        QAction* action =
            menu.addAction(QStringLiteral("首帧没有进入段，请在第二个关键帧上设置"));
        action->setEnabled(false);
        menu.exec(globalPos);
        return;
    }

    const EasingSpec current = keyframeEasingAt(base.entity, base.frame);
    const int fps = std::max(1, m_project->fps);
    const QString title = QStringLiteral("进入本帧：%1s → %2s")
                              .arg(previous / double(fps), 0, 'f', 2)
                              .arg(base.frame / double(fps), 0, 'f', 2);

    EasingSpec chosen;
    if (EasingChooser::choose(this, current, globalPos, &chosen, title))
        emit keyframeEasingRequested(chosen);
}

void TimelinePanel::contextMenuEvent(QContextMenuEvent* event)
{
    const QPoint pos = event->pos();
    if (pos.y() < kHeaderHeight)
        return;

    if (pos.x() >= kNameWidth)
    {
        EntityId keyEntity = kInvalidEntity;
        double keyFrame = 0.0;
        if (hitKeyframe(rowAt(pos.y()), pos.x(), &keyEntity, &keyFrame))
        {
            const KeyframeSelection selection{keyEntity, keyFrame};
            const auto existing =
                std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                          selection);
            if (existing == m_selectedKeyframes.end())
            {
                m_selectedKeyframes.clear();
                m_selectedKeyframes.append(selection);
                emit entitySelected(keyEntity);
                update();
            }

            showKeyframeMenu(event->globalPos());
            event->accept();
            return;
        }
    }

    const int row = rowAt(pos.y());
    const EntityId clickedId = entityAtRow(row);
    const Entity* entity = m_project->entity(clickedId);
    if (entity && !m_entitySelection.contains(clickedId))
    {
        m_entitySelection.clear();
        m_entitySelection.insert(clickedId);
        m_selected = clickedId;
        m_selectionAnchor = clickedId;
        emit entitySelected(clickedId);
        update();
    }
    QMenu menu(this);
    QAction* createInside = nullptr;
    QAction* createSibling = nullptr;
    QAction* rename = nullptr;
    QAction* replaceSource = nullptr;
    QAction* copyEntity = nullptr;
    QAction* duplicateEntity = nullptr;
    QAction* pasteEntity = nullptr;
    QAction* groupFolder = nullptr;
    QAction* toWorld = nullptr;

    if (entity)
    {
        const bool locked = m_project->isEffectivelyLocked(entity->id);
        if (entity->isFolder())
        {
            if (!locked)
                createInside = menu.addAction(QStringLiteral("在此文件夹内新建文件夹"));
            createSibling = menu.addAction(QStringLiteral("新建同级文件夹"));
        }
        else
        {
            createInside = menu.addAction(QStringLiteral("新建文件夹"));
        }
        if (!locked)
            rename = menu.addAction(QStringLiteral("重命名"));
        if (!locked
            && (entity->type == EntityType::Image
                || entity->type == EntityType::Video
                || entity->type == EntityType::Audio))
            replaceSource = menu.addAction(QStringLiteral("更换素材…"));
        if (!locked)
        {
            copyEntity = menu.addAction(QStringLiteral("复制实体"));
            duplicateEntity = menu.addAction(QStringLiteral("原地复制"));
        }
        if (!m_entitySelection.isEmpty())
            groupFolder = menu.addAction(QStringLiteral("组成文件夹"));
        if (entity->parent != kInvalidEntity && !locked)
            toWorld = menu.addAction(QStringLiteral("移到工作台"));
    }
    else
    {
        createInside = menu.addAction(QStringLiteral("新建文件夹"));
    }
    menu.addSeparator();
    pasteEntity = menu.addAction(QStringLiteral("粘贴实体"));

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    if (chosen == createInside)
    {
        EntityId parent = kInvalidEntity;
        if (entity)
            parent = entity->isFolder() ? entity->id : entity->parent;
        if (parent == kInvalidEntity || !m_project->isEffectivelyLocked(parent))
        {
            if (parent != kInvalidEntity)
                m_collapsedFolders.remove(parent);
            emit folderCreateRequested(parent);
            update();
        }
    }
    else if (chosen == createSibling)
    {
        const EntityId parent = entity ? entity->parent : kInvalidEntity;
        if (parent == kInvalidEntity || !m_project->isEffectivelyLocked(parent))
        {
            if (parent != kInvalidEntity)
                m_collapsedFolders.remove(parent);
            emit folderCreateRequested(parent);
            update();
        }
    }
    else if (chosen == rename && entity)
    {
        m_renameEntity = entity->id;
        m_renameEdit->setText(entity->name);
        m_renameEdit->setGeometry(6, rowTop(row) + 2, kNameWidth - 12,
                                  rowHeight(row) - 4);
        m_renameEdit->show();
        m_renameEdit->selectAll();
        m_renameEdit->setFocus();
    }
    else if (chosen == copyEntity && entity)
    {
        emit copyEntityRequested(entity->id);
    }
    else if (chosen == duplicateEntity && entity)
    {
        emit duplicateEntityRequested(entity->id);
    }
    else if (chosen == pasteEntity)
    {
        emit pasteEntityRequested();
    }
    else if (chosen == groupFolder)
    {
        QVector<EntityId> ids;
        for (int index : m_project->entityOrder())
        {
            const EntityId id = m_project->entities()[index].id;
            if (m_entitySelection.contains(id))
                ids.append(id);
        }
        if (!ids.isEmpty())
            emit groupIntoFolderRequested(ids);
    }
    else if (chosen == replaceSource && entity)
    {
        emit replaceSourceRequested(entity->id);
    }
    else if (chosen == toWorld && entity)
    {
        emit entityMoveRequested(entity->id, kInvalidEntity,
                                 int(m_project->entities().size()));
    }
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
        const Entity* pressed = m_project->entity(m_pressEntity);
        if (pressed && !m_project->isEffectivelyLocked(m_pressEntity))
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
            const Entity& entity = m_project->entities()[visible[row]];
            if (m_project->isEffectivelyLocked(entity.id))
                continue;
            for (const double frame : keyframeFrames(entity.id))
            {
                if (frame < firstFrame || frame > lastFrame)
                    continue;
                const KeyframeSelection selection{entity.id, frame};
                if (std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(), selection)
                    == m_selectedKeyframes.end())
                    m_selectedKeyframes.append(selection);
            }
        }

        // Keep the entity selection in sync with the last touched row.
        if (lastRow >= 0 && lastRow < int(visible.size()))
        {
            const Entity& last = m_project->entities()[visible[lastRow]];
            if (!m_project->isEffectivelyLocked(last.id))
                emit entitySelected(last.id);
        }
        update();
    }
    else if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta = m_kfDragDelta;
        m_dragMode = DragMode::None;
        m_kfDragDelta = 0;
        if (delta != 0 && !m_kfDragRecords.isEmpty())
        {
            for (KeyframeSelection& selected : m_selectedKeyframes)
                selected.frame = std::max(0.0, selected.frame + delta);
            emit keyframesMoveRequested(m_kfDragRecords, double(delta));
        }
        m_kfDragRecords.clear();
        update();
    }
    else if (m_dragMode == DragMode::MoveRow)
    {
        EntityId parent = kInvalidEntity;
        int index = -1;
        bool into = false;
        if (m_dropMode != DropMode::None && m_dropRow >= 0)
        {
            const Entity* target = m_project->entity(entityAtRow(m_dropRow));
            if (target)
            {
                const auto& entities = m_project->entities();
                int targetIndex = -1;
                for (int i = 0; i < int(entities.size()); ++i)
                    if (entities[i].id == target->id)
                    {
                        targetIndex = i;
                        break;
                    }
                if (targetIndex >= 0)
                {
                    parent = target->parent;
                    index = targetIndex;
                    if (m_dropMode == DropMode::Into)
                    {
                        parent = target->id;
                        index = int(entities.size());
                        into = true;
                    }
                    else if (m_dropMode == DropMode::Below)
                    {
                        ++index;
                    }
                }
            }
        }

        QVector<EntityId> dragged = m_rowDragIds;
        if (dragged.isEmpty())
            dragged.append(m_rowDragId);

        m_dragMode = DragMode::None;
        m_rowDragId = kInvalidEntity;
        m_rowDragIds.clear();
        m_dropRow = -1;
        m_dropMode = DropMode::None;
        unsetCursor();

        bool valid = index >= 0;
        for (EntityId id : dragged)
            if (parent == id || m_project->isDescendantOf(parent, id))
                valid = false;
        if (valid)
        {
            if (dragged.size() == 1)
            {
                emit entityMoveRequested(dragged.first(), parent, index);
            }
            else
            {
                // moveEntities() takes an index into the list with the moved
                // entities removed; subtract the dragged rows before the
                // target and add one for a drop below it.
                if (!into)
                {
                    const auto& entities = m_project->entities();
                    for (int i = 0; i < std::min(index, int(entities.size())); ++i)
                        if (dragged.contains(entities[i].id))
                            --index;
                }
                emit entitiesMoveRequested(dragged, parent, index);
            }
        }
        updateHoverCursor(event->position());
    }
    else if (m_dragMode == DragMode::MoveAudio)
    {
        const EntityId id = m_audioDragId;
        const double newStart =
            std::max(0.0, m_audioDragStart + m_audioDragOffset);
        const bool changed = std::abs(newStart - m_audioDragStart) > 1e-9;
        const int clickFrame = m_pressFrame;
        m_dragMode = DragMode::None;
        m_audioDragId = kInvalidEntity;
        m_audioDragOffset = 0.0;
        if (changed)
        {
            emit audioMoved(id, newStart);
        }
        else
        {
            // A click without dragging still positions the playhead, as it
            // did before clips became draggable.
            emit scrubStarted();
            emit frameRequested(clickFrame);
        }
        updateHoverCursor(event->position());
    }

    m_dragMode = DragMode::None;
    update();
}

} // namespace pvf
