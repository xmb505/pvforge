#include "TimelinePanel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QVector<EntityId> childIds(const QVector<Entity>& entities, EntityId parent)
{
    QVector<EntityId> result;
    for (const Entity& entity : entities)
        if (entity.parent == parent)
            result.append(entity.id);
    return result;
}

Entity* findEntity(QVector<Entity>& entities, EntityId id)
{
    for (Entity& entity : entities)
        if (entity.id == id)
            return &entity;
    return nullptr;
}

const Entity* findEntity(const QVector<Entity>& entities, EntityId id)
{
    for (const Entity& entity : entities)
        if (entity.id == id)
            return &entity;
    return nullptr;
}

void collectSubtree(const QVector<Entity>& entities, EntityId id, QSet<EntityId>& out)
{
    if (out.contains(id))
        return;
    out.insert(id);
    for (const Entity& entity : entities)
        if (entity.parent == id)
            collectSubtree(entities, entity.id, out);
}

QColor clipColor(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QColor(74, 144, 226);
    case EntityType::Video:
        return QColor(123, 97, 255);
    case EntityType::Text:
        return QColor(242, 201, 76);
    case EntityType::Vector:
        return QColor(235, 87, 87);
    case EntityType::Camera:
        return QColor(186, 132, 255);
    case EntityType::Audio:
        return QColor(92, 204, 142);
    case EntityType::Folder:
        return QColor(132, 138, 148);
    case EntityType::ComicText:
        return QColor(242, 153, 74);
    case EntityType::RadialLines:
        return QColor(255, 120, 160);
    case EntityType::FlowLines:
        return QColor(88, 196, 221);
    case EntityType::BrandWall:
        return QColor(180, 160, 255);
    case EntityType::HalftoneDots:
        return QColor(140, 200, 160);
    case EntityType::HtmlLayer:
        return QColor(86, 204, 242);
    }
    return QColor(150, 150, 158);
}

QRect plusButtonRect(const QRect& clip, int visibleRight)
{
    const int left = std::max(0, clip.left());
    const int right = std::min(clip.right(), visibleRight);
    if (right - left < 56)
        return QRect();
    return QRect(right - 24, clip.top() + (clip.height() - 16) / 2, 16, 16);
}

} // namespace

// ---------------------------------------------------------------- popup

class ClipPopup : public QWidget
{
public:
    ClipPopup(Project* project, EntityId ownerId, TimelinePanel* source)
        : QWidget(source->window())
        , m_project(project)
        , m_ownerId(ownerId)
        , m_source(source)
    {
        setObjectName(QStringLiteral("clipPopup"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral(
            "QWidget#clipPopup { background: #1e1f24; border: 1px solid #4a4d55;"
            " border-radius: 6px; }"
            "QWidget#clipPopupHeader { background: #2a2c33; border: none;"
            " border-bottom: 1px solid #3a3d45; border-top-left-radius: 5px;"
            " border-top-right-radius: 5px; }"
            "QWidget#clipPopup QLabel { background: transparent; color: #d8dce4; }"
            "QWidget#clipPopup QToolButton { background: #32353d; color: #e8eaf0;"
            " border: 1px solid #4a4d55; border-radius: 3px; padding: 1px 7px; }"
            "QWidget#clipPopup QToolButton:hover { background: #3c404a; }"));
        setMinimumWidth(160);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("clipPopupHeader"));
        header->setMinimumHeight(28);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(8, 3, 4, 3);
        headerLayout->setSpacing(4);
        m_title = new QLabel(header);
        QFont titleFont = m_title->font();
        titleFont.setBold(true);
        m_title->setFont(titleFont);
        headerLayout->addWidget(m_title, 1);
        m_timeMode = new QToolButton(header);
        m_timeMode->setCheckable(true);
        m_timeMode->setText(QStringLiteral("相对"));
        m_timeMode->setToolTip(QStringLiteral("切换相对 / 绝对时间"));
        headerLayout->addWidget(m_timeMode);
        auto* close = new QToolButton(header);
        close->setText(QStringLiteral("×"));
        close->setToolTip(QStringLiteral("关闭私有时间线"));
        headerLayout->addWidget(close);
        layout->addWidget(header);

        m_panel = new TimelinePanel(project, this);
        m_panel->m_owner = ownerId;
        m_panel->m_popup = true;
        m_panel->m_sourcePanel = source;
        m_panel->m_relativeTime = true;
        m_panel->m_pixelsPerFrame = source->m_pixelsPerFrame;
        layout->addWidget(m_panel, 1);

        connect(m_timeMode, &QToolButton::toggled, this, [this](bool absolute) {
            m_timeMode->setText(absolute ? QStringLiteral("绝对")
                                         : QStringLiteral("相对"));
            m_panel->m_relativeTime = !absolute;
            m_panel->update();
        });
        connect(close, &QToolButton::clicked, this, [this] {
            m_source->removePopup(m_ownerId);
            deleteLater();
        });

        // The popup is its own TimelinePanel. Forward edit/selection signals
        // up the chain so the main panel's MainWindow connections fire once.
        connect(m_panel, &TimelinePanel::frameRequested, m_source,
                &TimelinePanel::frameRequested);
        connect(m_panel, &TimelinePanel::scrubStarted, m_source,
                &TimelinePanel::scrubStarted);
        connect(m_panel, &TimelinePanel::entitySelected, m_source,
                &TimelinePanel::entitySelected);
        connect(m_panel, &TimelinePanel::deleteKeyframesRequested, m_source,
                &TimelinePanel::deleteKeyframesRequested);
        connect(m_panel, &TimelinePanel::audioMoved, m_source,
                &TimelinePanel::audioMoved);
        connect(m_panel, &TimelinePanel::keyframesMoveRequested, m_source,
                &TimelinePanel::keyframesMoveRequested);
        connect(m_panel, &TimelinePanel::snapshotEditRequested, m_source,
                &TimelinePanel::snapshotEditRequested);
        connect(m_panel, &TimelinePanel::keyframeSelectionChanged, m_source,
                &TimelinePanel::adoptKeyframeSelection);

        syncGeometry();
    }

    void syncGeometry()
    {
        const Entity* entity = m_project->entity(m_ownerId);
        const QRect clip = m_source->clipRectFor(m_ownerId);
        if (!entity || clip.isEmpty() || m_source->window() == nullptr)
        {
            hide();
            return;
        }

        m_panel->m_pixelsPerFrame = m_source->m_pixelsPerFrame;
        if (m_source->m_dragging)
        {
            m_panel->m_externalPreview = m_source->m_preview;
            m_panel->m_hasExternalPreview = true;
        }
        else
        {
            m_panel->m_hasExternalPreview = false;
            m_panel->m_externalPreview.clear();
        }
        m_title->setText(entity->name + QStringLiteral(" · 私有时间线"));

        QWidget* host = window();
        const int sourceRight =
            m_source->mapTo(host, QPoint(std::max(0, m_source->width()
                                                    - TimelinePanel::kScrollBarSize),
                                        0))
                .x();
        const int left = m_source->mapTo(host, QPoint(std::max(0, clip.left()),
                                                      clip.top()))
                             .x();
        const int right =
            std::min(m_source->mapTo(host, QPoint(clip.right() + 1, 0)).x(),
                     sourceRight);
        const int width = right - left;
        if (width < 90)
        {
            hide();
            return;
        }

        const int rows = std::max(1, m_panel->rowCountForTest());
        int height = 30 + TimelinePanel::kRulerHeight
                     + rows * TimelinePanel::kRowHeight
                     + TimelinePanel::kScrollBarSize;
        height = std::clamp(height, 140, 360);
        int y = m_source->mapTo(host, QPoint(0, clip.top())).y() - height - 6;
        if (y < 0)
            y = m_source->mapTo(host, QPoint(0, clip.bottom())).y() + 6;
        y = std::clamp(y, 0, std::max(0, host->height() - height));

        setGeometry(left, y, width, height);
        m_panel->clampScroll();
        show();
        raise();
        m_panel->update();
    }

    TimelinePanel* panel() const { return m_panel; }

private:
    Project* m_project = nullptr;
    EntityId m_ownerId = kInvalidEntity;
    TimelinePanel* m_source = nullptr;
    TimelinePanel* m_panel = nullptr;
    QLabel* m_title = nullptr;
    QToolButton* m_timeMode = nullptr;
};

// ---------------------------------------------------------------- panel

TimelinePanel::TimelinePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setObjectName(QStringLiteral("timelinePanel"));

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setObjectName(QStringLiteral("timelineHScroll"));
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollX = value;
        update();
        syncPopups();
    });

    m_vScroll = new QScrollBar(Qt::Vertical, this);
    m_vScroll->setObjectName(QStringLiteral("timelineVScroll"));
    connect(m_vScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollY = value;
        update();
        syncPopups();
    });

    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(30);
    connect(m_autoScroll, &QTimer::timeout, this, [this] { autoScrollTick(); });

    if (m_project)
    {
        connect(m_project, &Project::changed, this, [this] {
            if (m_selected != kInvalidEntity && !m_project->entity(m_selected))
            {
                m_selected = kInvalidEntity;
                m_entitySelection.clear();
            }
            clampScroll();
            syncPopups();
            update();
        });
    }
}

TimelinePanel::~TimelinePanel()
{
    closeAllPopups();
}

void TimelinePanel::setCurrentFrame(int frame)
{
    m_frame = std::max(0, frame);
    update();
    for (ClipPopup* popup : m_popups)
        if (popup && popup->panel())
            popup->panel()->setCurrentFrame(frame);
}

void TimelinePanel::setSelectedEntity(EntityId id)
{
    if (!m_project)
        return;
    const Entity* entity = m_project->entity(id);
    if (!entity)
    {
        selectEntity(kInvalidEntity, false);
        return;
    }

    if (m_owner >= 0)
    {
        if (entity->parent == m_owner)
            selectEntity(id, false);
        return;
    }

    if (entity->parent == kInvalidEntity)
    {
        selectEntity(id, false);
        return;
    }

    // Selecting a child from the viewport should reveal the private timelines
    // that contain it, from the root down to its direct parent.
    QVector<EntityId> chain;
    QSet<EntityId> seen;
    EntityId current = entity->parent;
    while (current != kInvalidEntity && !seen.contains(current))
    {
        seen.insert(current);
        chain.prepend(current);
        const Entity* parent = m_project->entity(current);
        current = parent ? parent->parent : kInvalidEntity;
    }

    TimelinePanel* panel = this;
    for (EntityId ownerId : chain)
    {
        panel = panel->openPopup(ownerId);
        if (!panel)
            return;
    }
    panel->selectEntity(id, false);
}

void TimelinePanel::selectEntity(EntityId id, bool emitSignal)
{
    m_selected = id;
    m_entitySelection.clear();
    if (id != kInvalidEntity)
        m_entitySelection.insert(id);
    if (id == kInvalidEntity)
    {
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
    }
    update();
    if (emitSignal)
        emit entitySelected(id);
}

void TimelinePanel::adoptKeyframeSelection(
    const QVector<KeyframeSelection>& selection)
{
    m_selectedKeyframes = selection;
    update();
    if (m_sourcePanel)
        m_sourcePanel->adoptKeyframeSelection(selection);
}

int TimelinePanel::depthOf(EntityId id) const
{
    int depth = 0;
    QSet<EntityId> seen;
    const Entity* entity = m_project ? m_project->entity(id) : nullptr;
    while (entity && entity->parent != kInvalidEntity && !seen.contains(entity->parent))
    {
        seen.insert(entity->parent);
        ++depth;
        entity = m_project->entity(entity->parent);
    }
    return depth;
}

QVector<EntityId> TimelinePanel::rowIds() const
{
    QVector<EntityId> result;
    for (const Entity& entity : activeEntities())
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && m_project->entity(entity.parent) == nullptr;
        if (entity.parent == m_owner
            || (m_owner == kInvalidEntity && dangling))
            result.append(entity.id);
    }
    return result;
}

EntityId TimelinePanel::entityAtRow(int row) const
{
    const QVector<EntityId> ids = rowIds();
    return row >= 0 && row < ids.size() ? ids[row] : kInvalidEntity;
}

int TimelinePanel::rowTop(int row) const
{
    return kRulerHeight + row * kRowHeight - m_scrollY;
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kRulerHeight || y >= bodyBottom())
        return -1;
    const int row = (y - kRulerHeight + m_scrollY) / kRowHeight;
    return row >= 0 && row < rowIds().size() ? row : -1;
}

int TimelinePanel::contentStart() const
{
    if (m_owner < 0 || !m_project)
        return 0;
    const Entity* ownerEntity = rangeOwner();
    return ownerEntity ? clipStartOf(*ownerEntity) : 0;
}

const QVector<Entity>& TimelinePanel::activeEntities() const
{
    static const QVector<Entity> empty;
    if (!m_project)
        return empty;
    if (m_hasExternalPreview)
        return m_externalPreview;
    return m_dragging ? m_preview : m_project->entities();
}

const Entity* TimelinePanel::rangeOwner() const
{
    if (m_owner < 0)
        return nullptr;
    return findEntity(activeEntities(), m_owner);
}

Entity* TimelinePanel::activeEntity(EntityId id)
{
    if (!m_project)
        return nullptr;
    if (!m_dragging)
        return m_project->entity(id);
    return findEntity(m_preview, id);
}

int TimelinePanel::clipStartOf(const Entity& entity) const
{
    if (entity.isAudio())
        return std::max(0, int(std::floor(
                               entity.data.value(QStringLiteral("startFrame"), 0.0)
                                   .toDouble())));
    return std::max(0, entity.data.value(QStringLiteral("clipStart"), 0).toInt());
}

int TimelinePanel::clipEndOf(const Entity& entity) const
{
    const int start = clipStartOf(entity);
    if (entity.isAudio()
        && entity.data.contains(QStringLiteral("duration")))
    {
        const double media =
            entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double trimStart =
            entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double trimEnd =
            entity.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
        const double fps = std::max(1, m_project ? m_project->fps : 30);
        const double frames =
            std::max(0.0, (media - trimStart - trimEnd) * fps);
        return start + std::max(1, int(std::ceil(frames)));
    }
    const int stored = entity.data.value(QStringLiteral("clipEnd"), 0).toInt();
    if (stored > start)
        return stored;
    return std::max(start + 1,
                    std::max(180, m_project ? m_project->timelineLength() : 180));
}

int TimelinePanel::contentEnd() const
{
    if (m_owner >= 0)
    {
        const Entity* ownerEntity = rangeOwner();
        if (!ownerEntity)
            return 180;
        return std::max(contentStart() + 1, clipEndOf(*ownerEntity));
    }
    int end = 180;
    for (const Entity& entity : activeEntities())
        end = std::max(end, clipEndOf(entity));
    if (m_project)
        end = std::max(end, m_project->timelineLength());
    return end;
}

QRect TimelinePanel::clipRect(const Entity& entity) const
{
    const QVector<EntityId> ids = rowIds();
    const int row = ids.indexOf(entity.id);
    if (row < 0)
        return QRect();
    const int start = std::max(clipStartOf(entity), contentStart());
    const int end = std::min(clipEndOf(entity), contentEnd());
    if (end <= start)
        return QRect();
    const int x0 = xFromFrame(start);
    const int x1 = xFromFrame(end);
    return QRect(x0, rowTop(row) + 3, std::max(1, x1 - x0), kRowHeight - 6);
}

QRect TimelinePanel::clipRectFor(EntityId id) const
{
    const Entity* entity = findEntity(activeEntities(), id);
    return entity ? clipRect(*entity) : QRect();
}

QVector<int> TimelinePanel::keyframeFrames(const Entity& entity) const
{
    QSet<int> frames;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
        for (const Keyframe& key : it->keys)
            frames.insert(int(std::lround(key.frame)));
    QVector<int> result(frames.begin(), frames.end());
    std::sort(result.begin(), result.end());
    return result;
}

QVector<KeyframeRecord> TimelinePanel::keyframeRecords(EntityId id, int frame) const
{
    QVector<KeyframeRecord> result;
    const Entity* entity = m_project ? m_project->entity(id) : nullptr;
    if (!entity)
        return result;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (int(std::lround(key.frame)) != frame)
                continue;
            KeyframeRecord record;
            record.entity = id;
            record.property = it.key();
            record.frame = key.frame;
            record.value = key.value;
            record.ease = key.ease;
            record.note = key.note;
            result.append(record);
        }
    }
    return result;
}

bool TimelinePanel::keyframeHasNote(EntityId id, double frame) const
{
    const Entity* entity = m_project ? m_project->entity(id) : nullptr;
    if (!entity)
        return false;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        for (const Keyframe& key : it->keys)
            if (std::abs(key.frame - frame) < 0.5 && !key.note.isEmpty())
                return true;
    return false;
}

int TimelinePanel::visibleWidth() const
{
    return std::max(1, width() - kScrollBarSize);
}

int TimelinePanel::bodyBottom() const
{
    return std::max(kRulerHeight, height() - kScrollBarSize);
}

int TimelinePanel::xFromFrame(int frame) const
{
    return int(std::lround((frame - contentStart()) * m_pixelsPerFrame
                           - m_scrollX));
}

int TimelinePanel::frameFromX(int x) const
{
    return int(std::lround(contentStart()
                           + (x + m_scrollX) / m_pixelsPerFrame));
}

void TimelinePanel::clampScroll()
{
    if (!m_project)
        return;
    const int visible = visibleWidth();
    const int tailPad = std::max(400, visible);
    const int contentWidth =
        int((contentEnd() - contentStart()) * m_pixelsPerFrame) + tailPad;
    const int maxScroll = std::max(0, contentWidth - visible);
    m_scrollX = std::clamp(m_scrollX, 0, maxScroll);
    if (m_hScroll)
    {
        const QSignalBlocker blocker(m_hScroll);
        m_hScroll->setRange(0, maxScroll);
        m_hScroll->setPageStep(visible);
        m_hScroll->setSingleStep(60);
        m_hScroll->setValue(m_scrollX);
        m_hScroll->setVisible(true);
    }

    const int rows = int(rowIds().size());
    const int bodyHeight = std::max(0, bodyBottom() - kRulerHeight);
    const int pageRows =
        std::max(1, int(std::ceil(bodyHeight / double(kRowHeight))));
    const int virtualRows = std::max(rows, pageRows) + pageRows;
    const int maxY = std::max(0, virtualRows * kRowHeight - bodyHeight);
    m_scrollY = std::clamp(m_scrollY, 0, maxY);
    if (m_vScroll)
    {
        const QSignalBlocker blocker(m_vScroll);
        m_vScroll->setRange(0, maxY);
        m_vScroll->setPageStep(std::max(1, bodyHeight));
        m_vScroll->setSingleStep(kRowHeight);
        m_vScroll->setValue(m_scrollY);
        m_vScroll->setVisible(true);
    }
}

void TimelinePanel::updateScrollBars()
{
    clampScroll();
}

void TimelinePanel::ensureFrameVisible(int frame)
{
    const int x = xFromFrame(frame);
    const int margin = 24;
    if (x < margin)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - margin)));
    else if (x > visibleWidth() - margin)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - (visibleWidth() - margin))));
    clampScroll();
    update();
    syncPopups();
}

// ---------------------------------------------------------------- editing

void TimelinePanel::beginDrag()
{
    m_before = m_project->entities();
    m_preview = m_before;
    m_dragging = false;
    m_moved = false;
}

void TimelinePanel::resetPreview()
{
    m_preview = m_before;
}

void TimelinePanel::endDrag(const QString& text)
{
    if (m_autoScroll)
        m_autoScroll->stop();
    if (m_dragging && m_moved && !m_before.isEmpty())
        emit snapshotEditRequested(m_before, m_preview, text);
    m_dragging = false;
    m_preview.clear();
    m_before.clear();
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    m_moved = false;
    update();
    syncPopups();
}

void TimelinePanel::applyShift(QVector<Entity>& entities, EntityId id, int delta)
{
    QSet<EntityId> ids;
    collectSubtree(entities, id, ids);
    int minStart = 1 << 30;
    for (EntityId entityId : ids)
        if (const Entity* entity = findEntity(entities, entityId))
            minStart = std::min(minStart, clipStartOf(*entity));
    if (minStart == (1 << 30))
        return;
    const int lower = std::max(0, contentStart());
    delta = std::max(delta, lower - minStart);
    if (delta == 0)
        return;
    for (EntityId entityId : ids)
    {
        Entity* entity = findEntity(entities, entityId);
        if (!entity)
            continue;
        setClipRange(*entity, clipStartOf(*entity) + delta,
                     clipEndOf(*entity) + delta);
        for (auto track = entity->tracks.begin(); track != entity->tracks.end();
             ++track)
            for (Keyframe& key : track->keys)
                key.frame += delta;
    }
}

void TimelinePanel::setClipRange(Entity& entity, int start, int end)
{
    start = std::max(0, start);
    if (end <= start)
        end = start + 1;
    if (entity.isAudio())
    {
        const int oldStart = clipStartOf(entity);
        double audioStart =
            entity.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
        audioStart += start - oldStart;
        audioStart = std::max(0.0, audioStart);
        entity.data[QStringLiteral("startFrame")] = audioStart;
        const double media =
            entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
        const double trimStart =
            entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
        const double fps = std::max(1, m_project ? m_project->fps : 30);
        if (media > 0.0)
        {
            double trimEnd =
                media - trimStart - (end - start) / double(fps);
            trimEnd = std::clamp(trimEnd, 0.0, std::max(0.0, media - trimStart));
            entity.data[QStringLiteral("trimEnd")] = trimEnd;
        }
        return;
    }
    entity.data[QStringLiteral("clipStart")] = start;
    entity.data[QStringLiteral("clipEnd")] = end;
}

void TimelinePanel::cleanKeys(Entity& entity)
{
    const int start = clipStartOf(entity);
    const int end = clipEndOf(entity);
    for (auto track = entity.tracks.begin(); track != entity.tracks.end(); ++track)
    {
        auto& keys = track->keys;
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [start, end](const Keyframe& key) {
                                      const int frame = int(std::lround(key.frame));
                                      return frame < start || frame >= end;
                                  }),
                   keys.end());
    }
}

void TimelinePanel::trimStartRec(QVector<Entity>& entities, EntityId parentId,
                                 int limit, QSet<EntityId>& doomed)
{
    for (EntityId childId : childIds(entities, parentId))
    {
        Entity* child = findEntity(entities, childId);
        if (!child)
            continue;
        const int childStart = clipStartOf(*child);
        const int childEnd = clipEndOf(*child);
        if (childEnd <= limit)
        {
            collectSubtree(entities, childId, doomed);
            continue;
        }
        if (childStart < limit)
        {
            setClipRange(*child, limit, childEnd);
            cleanKeys(*child);
        }
        trimStartRec(entities, childId, limit, doomed);
    }
}

void TimelinePanel::trimEndRec(QVector<Entity>& entities, EntityId parentId,
                               int limit, QSet<EntityId>& doomed)
{
    for (EntityId childId : childIds(entities, parentId))
    {
        Entity* child = findEntity(entities, childId);
        if (!child)
            continue;
        const int childStart = clipStartOf(*child);
        const int childEnd = clipEndOf(*child);
        if (childStart >= limit)
        {
            collectSubtree(entities, childId, doomed);
            continue;
        }
        if (childEnd > limit)
        {
            setClipRange(*child, childStart, limit);
            cleanKeys(*child);
        }
        trimEndRec(entities, childId, limit, doomed);
    }
}

void TimelinePanel::removeEntities(QVector<Entity>& entities,
                                   const QSet<EntityId>& doomed)
{
    if (doomed.isEmpty())
        return;
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [&doomed](const Entity& entity) {
                                      return doomed.contains(entity.id);
                                  }),
                   entities.end());
}

void TimelinePanel::trimStart(QVector<Entity>& entities, EntityId id, int newStart)
{
    Entity* entity = findEntity(entities, id);
    if (!entity)
        return;
    const int oldStart = clipStartOf(*entity);
    const int end = clipEndOf(*entity);
    newStart = std::max({0, contentStart(), std::min(newStart, end - 1)});
    setClipRange(*entity, newStart, end);
    if (newStart <= oldStart)
        return;   // expanding: keep children and do not restore keys

    cleanKeys(*entity);
    QSet<EntityId> doomed;
    trimStartRec(entities, id, newStart, doomed);
    removeEntities(entities, doomed);
}

void TimelinePanel::trimEnd(QVector<Entity>& entities, EntityId id, int newEnd)
{
    Entity* entity = findEntity(entities, id);
    if (!entity)
        return;
    const int start = clipStartOf(*entity);
    const int oldEnd = clipEndOf(*entity);
    newEnd = std::max(start + 1, newEnd);
    if (m_owner >= 0)
        newEnd = std::min(newEnd, contentEnd());
    setClipRange(*entity, start, newEnd);
    if (newEnd >= oldEnd)
        return;   // expanding: keep children and do not restore keys

    cleanKeys(*entity);
    QSet<EntityId> doomed;
    trimEndRec(entities, id, newEnd, doomed);
    removeEntities(entities, doomed);
}

void TimelinePanel::reorderChild(QVector<Entity>& entities, EntityId owner,
                                 EntityId id, int targetRow)
{
    const QVector<EntityId> ids = childIds(entities, owner);
    const int from = ids.indexOf(id);
    if (from < 0)
        return;
    targetRow = std::clamp(targetRow, 0, int(ids.size()) - 1);
    if (from == targetRow)
        return;
    const EntityId targetId = ids[targetRow];
    int nodePos = -1;
    int targetPos = -1;
    for (int i = 0; i < entities.size(); ++i)
    {
        if (entities[i].id == id)
            nodePos = i;
        if (entities[i].id == targetId)
            targetPos = i;
    }
    if (nodePos < 0 || targetPos < 0)
        return;
    Entity entity = entities.takeAt(nodePos);
    if (nodePos < targetPos)
        --targetPos;
    entities.insert(from < targetRow ? targetPos + 1 : targetPos, entity);
}

void TimelinePanel::moveKeys(QVector<Entity>& entities,
                             const QVector<KeyframeSelection>& keys, int delta)
{
    if (keys.isEmpty())
        return;
    int minDelta = -(1 << 30);
    int maxDelta = 1 << 30;
    for (const KeyframeSelection& selection : keys)
    {
        const Entity* entity = findEntity(entities, selection.entity);
        if (!entity)
            continue;
        const int frame = int(std::lround(selection.frame));
        const int start = clipStartOf(*entity);
        const int end = clipEndOf(*entity);
        bool found = false;
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd();
             ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (int(std::lround(key.frame)) != frame)
                    continue;
                found = true;
                minDelta = std::max(minDelta, start - frame);
                maxDelta = std::min(maxDelta, end - 1 - frame);
            }
        }
        if (!found)
            continue;
    }
    delta = std::clamp(delta, minDelta, maxDelta);
    if (delta == 0)
        return;

    for (const KeyframeSelection& selection : keys)
    {
        Entity* entity = findEntity(entities, selection.entity);
        if (!entity)
            continue;
        const int from = int(std::lround(selection.frame));
        for (auto track = entity->tracks.begin(); track != entity->tracks.end();
             ++track)
        {
            bool changed = false;
            for (Keyframe& key : track->keys)
            {
                if (int(std::lround(key.frame)) == from)
                {
                    key.frame += delta;
                    changed = true;
                }
            }
            if (changed)
                std::sort(track->keys.begin(), track->keys.end(),
                          [](const Keyframe& a, const Keyframe& b) {
                              return a.frame < b.frame;
                          });
        }
    }
}

void TimelinePanel::updateDragPreview(const QPointF& pos)
{
    if (!m_project || m_dragMode == DragMode::None)
        return;
    if (!m_dragging)
    {
        m_dragging = true;
        resetPreview();
    }
    resetPreview();

    if (m_dragMode == DragMode::MoveClip)
    {
        const int delta = frameFromX(int(std::lround(pos.x()))) - m_dragStartFrame;
        applyShift(m_preview, m_dragEntity, delta);
        const QVector<EntityId> rowsNow = childIds(m_preview, m_owner);
        int targetRow = rowAt(int(pos.y()));
        if (!rowsNow.isEmpty())
        {
            if (targetRow < 0)
                targetRow = pos.y() < kRulerHeight ? 0 : int(rowsNow.size()) - 1;
            targetRow = std::clamp(targetRow, 0, int(rowsNow.size()) - 1);
            reorderChild(m_preview, m_owner, m_dragEntity, targetRow);
            m_moved = delta != 0 || targetRow != m_dragStartRow;
        }
        else
        {
            m_moved = delta != 0;
        }
    }
    else if (m_dragMode == DragMode::TrimStart)
    {
        Entity* entity = findEntity(m_preview, m_dragEntity);
        if (!entity)
            return;
        int newStart = frameFromX(int(std::lround(pos.x())));
        newStart = std::max({0, contentStart(),
                             std::min(newStart, clipEndOf(*entity) - 1)});
        trimStart(m_preview, m_dragEntity, newStart);
        m_moved = newStart != m_dragClipStart;
    }
    else if (m_dragMode == DragMode::TrimEnd)
    {
        Entity* entity = findEntity(m_preview, m_dragEntity);
        if (!entity)
            return;
        int newEnd = std::max(clipStartOf(*entity) + 1,
                              frameFromX(int(std::lround(pos.x()))));
        if (m_owner >= 0)
            newEnd = std::min(newEnd, contentEnd());
        trimEnd(m_preview, m_dragEntity, newEnd);
        m_moved = newEnd != m_dragClipEnd;
    }
    else if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta =
            frameFromX(int(std::lround(pos.x()))) - m_keyDragStart;
        moveKeys(m_preview, m_selectedKeyframes, delta);
        m_moved = std::any_of(
            m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
            [this, delta](const KeyframeSelection& selection) {
                const Entity* entity = findEntity(m_before, selection.entity);
                if (!entity)
                    return false;
                for (auto it = entity->tracks.constBegin();
                     it != entity->tracks.constEnd(); ++it)
                    for (const Keyframe& key : it->keys)
                        if (int(std::lround(key.frame))
                            == int(std::lround(selection.frame)))
                            return delta != 0;
                return false;
            });
    }

    update();
    syncPopups();
}

void TimelinePanel::autoScrollTick()
{
    if (!m_project || m_dragMode == DragMode::None
        || m_dragMode == DragMode::Scrub || !m_moved)
        return;
    int dx = 0;
    int dy = 0;
    if (m_lastDragPos.x() > visibleWidth() - 24)
        dx = 12;
    else if (m_lastDragPos.x() < 24)
        dx = -12;
    if (m_lastDragPos.y() > bodyBottom() - 20)
        dy = 12;
    else if (m_lastDragPos.y() < kRulerHeight + 20)
        dy = -12;
    if (dx == 0 && dy == 0)
        return;
    m_scrollX = std::max(0, m_scrollX + dx);
    m_scrollY = std::max(0, m_scrollY + dy);
    // Update before clamping so a growing clip end can grow the scroll range.
    updateDragPreview(m_lastDragPos);
    clampScroll();
}

// ---------------------------------------------------------------- paint

void TimelinePanel::drawAudioClip(QPainter& p, const Entity& entity,
                                  const QRect& clip) const
{
    const int left = std::max(clip.left() + 2, 0);
    const int right = std::min(clip.right() - 2, width() - kScrollBarSize - 1);
    if (right <= left)
        return;
    const int center = clip.center().y();
    if (!entity.audio || entity.audio->peaks.isEmpty())
    {
        p.setPen(QPen(QColor(18, 60, 40, 120), 1.0, Qt::DashLine));
        p.drawLine(left, center, right, center);
        return;
    }

    const int fps = std::max(1, m_project ? m_project->fps : 30);
    const double trimStart =
        entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double x0 = xFromFrame(clipStartOf(entity));
    const QVector<float>& peaks = entity.audio->peaks;
    const int half = std::max(2, clip.height() / 2 - 4);

    p.save();
    p.setClipRect(clip.adjusted(2, 2, -2, -2));
    p.setPen(QPen(QColor(12, 54, 32, 190), 1.0));
    for (int x = left; x <= right; ++x)
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
    p.restore();
}

void TimelinePanel::paintEvent(QPaintEvent*)
{
    if (!m_project)
        return;
    clampScroll();

    const QPalette pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor alt = pal.color(QPalette::AlternateBase);
    const QColor text = pal.color(QPalette::Text);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor accent = pal.color(QPalette::Highlight);
    const int right = width() - kScrollBarSize;
    const int bottom = bodyBottom();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), base);
    p.fillRect(QRect(0, 0, width(), kRulerHeight), alt);

    const int fps = std::max(1, m_project->fps);
    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    int labelFrames = fps;
    while (labelFrames * m_pixelsPerFrame < 64.0)
        labelFrames *= 2;
    if (m_displayFrames)
    {
        int step = 1;
        while (step * m_pixelsPerFrame < 64.0)
            step *= 2;
        labelFrames = step;
    }

    const int firstFrame = frameFromX(0);
    const int lastFrame = frameFromX(right) + 1;
    const int firstLabel = (firstFrame / labelFrames) * labelFrames;
    for (int frame = firstLabel; frame <= lastFrame; frame += labelFrames)
    {
        const int x = xFromFrame(frame);
        if (x < -2 || x > right)
            continue;
        p.setPen(QPen(mid, 1.0));
        p.drawLine(x, 0, x, bottom);
        p.setPen(text);
        const int shown = frame - (m_relativeTime ? contentStart() : 0);
        const QString label = m_displayFrames
                                  ? QString::number(shown)
                                  : QString::number(shown / double(fps), 'g', 4)
                                        + QStringLiteral("s");
        p.drawText(QRect(x + 4, 0, 80, kRulerHeight - 3),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    }
    p.setPen(mid);
    p.drawLine(0, kRulerHeight, width(), kRulerHeight);

    const QVector<EntityId> rows = rowIds();
    p.save();
    p.setClipRect(QRect(0, kRulerHeight, right, bottom - kRulerHeight));
    for (int row = 0; row < rows.size(); ++row)
    {
        const Entity* entity = findEntity(activeEntities(), rows[row]);
        if (!entity)
            continue;
        const int top = rowTop(row);
        if (top + kRowHeight < kRulerHeight || top > bottom)
            continue;
        const QRect rowRect(0, top, right, kRowHeight);
        if (entity->id == m_selected)
            p.fillRect(rowRect, QColor(accent.red(), accent.green(),
                                       accent.blue(), 45));
        else if (row % 2)
            p.fillRect(rowRect, QColor(alt.red(), alt.green(), alt.blue(), 90));
        p.setPen(QColor(mid.red(), mid.green(), mid.blue(), 70));
        p.drawLine(0, top + kRowHeight - 1, right, top + kRowHeight - 1);

        const QRect clip = clipRect(*entity);
        if (clip.isEmpty())
            continue;
        QColor color = clipColor(entity->type);
        const bool selected = entity->id == m_selected;
        const bool hidden = m_project->isEffectivelyHidden(entity->id);
        const int alpha = hidden ? 80 : (selected ? 245 : 215);
        QColor topColor = color.lighter(112);
        QColor bottomColor = color.darker(108);
        topColor.setAlpha(alpha);
        bottomColor.setAlpha(alpha);
        QLinearGradient fill(clip.topLeft(), clip.bottomLeft());
        fill.setColorAt(0.0, topColor);
        fill.setColorAt(1.0, bottomColor);
        p.setPen(QPen(color.darker(160), 1));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(clip).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
        p.setPen(QPen(QColor(255, 255, 255, 38), 1));
        p.drawLine(clip.left() + 6, clip.top() + 1, clip.right() - 6,
                   clip.top() + 1);
        if (selected)
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(accent, 2));
            p.drawRoundedRect(QRectF(clip).adjusted(1.5, 1.5, -1.5, -1.5), 4.0,
                              4.0);
        }

        if (entity->isAudio())
            drawAudioClip(p, *entity, clip);

        const QRect plus = plusButtonRect(clip, right);
        if (!plus.isEmpty())
        {
            p.setPen(QPen(QColor(255, 255, 255, 125), 1));
            p.setBrush(QColor(0, 0, 0, 85));
            p.drawRoundedRect(QRectF(plus).adjusted(0.5, 0.5, -0.5, -0.5), 3.0,
                              3.0);
            p.setPen(QPen(Qt::white, 1.5));
            p.drawLine(plus.center().x() - 4, plus.center().y(),
                       plus.center().x() + 4, plus.center().y());
            p.drawLine(plus.center().x(), plus.center().y() - 4,
                       plus.center().x(), plus.center().y() + 4);
        }

        const int nameRight = plus.isEmpty() ? clip.right() - 8 : plus.left() - 6;
        if (nameRight > clip.left() + 12)
        {
            const QColor nameColor =
                color.lightness() > 150 ? QColor(24, 26, 30) : QColor(255, 255, 255);
            const QFontMetrics metrics(p.font());
            const QString name = metrics.elidedText(
                entity->name, Qt::ElideRight,
                std::max(0, nameRight - clip.left() - 8));
            const QRect nameRect(clip.left() + 8, clip.top(),
                                 nameRight - clip.left() - 8, clip.height());
            if (entity->isAudio())
            {
                p.setPen(QColor(0, 0, 0, 160));
                p.drawText(nameRect.translated(1, 1),
                           Qt::AlignVCenter | Qt::AlignLeft, name);
                p.setPen(QColor(255, 255, 255));
            }
            else
            {
                p.setPen(nameColor);
            }
            p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);
        }

        const int centerY = clip.center().y();
        for (int keyFrame : keyframeFrames(*entity))
        {
            if (keyFrame < clipStartOf(*entity) || keyFrame >= clipEndOf(*entity))
                continue;
            const int x = xFromFrame(keyFrame);
            if (x < clip.left() - 6 || x > clip.right() + 6)
                continue;
            const bool keySelected =
                std::find(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                          KeyframeSelection{entity->id, double(keyFrame)})
                != m_selectedKeyframes.end();
            const double radius = 4.5;
            QPolygonF diamond;
            diamond << QPointF(x, centerY - radius)
                    << QPointF(x + radius, centerY)
                    << QPointF(x, centerY + radius)
                    << QPointF(x - radius, centerY);
            p.setPen(QPen(QColor(14, 16, 20, 210), 1.2));
            p.setBrush(keySelected ? QColor(255, 214, 102)
                                   : QColor(248, 250, 255));
            p.drawPolygon(diamond);
        }
    }

    if (m_dragMode == DragMode::MoveClip && m_moved)
    {
        Entity* dragged = findEntity(m_preview, m_dragEntity);
        const QVector<EntityId> ids = rowIds();
        const int row = dragged ? ids.indexOf(dragged->id) : -1;
        if (row >= 0)
        {
            const int y = rowTop(row);
            p.setPen(QPen(accent.lighter(130), 2));
            p.drawLine(0, y, right, y);
        }
    }

    if (rows.isEmpty())
    {
        p.setPen(dim);
        p.drawText(QRect(0, kRulerHeight, right, bottom - kRulerHeight),
                   Qt::AlignCenter,
                   m_owner < 0
                       ? QStringLiteral("主时间线 · 选中片段后点 + 打开私有时间线")
                       : QStringLiteral("私有时间线 · 此片段还没有子实体"));
    }

    const int playhead = xFromFrame(m_frame);
    if (playhead >= 0 && playhead <= right)
    {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(playhead, 0, playhead, bottom);
        QPolygonF marker;
        marker << QPointF(playhead - 4, 0) << QPointF(playhead + 4, 0)
               << QPointF(playhead, 6);
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPolygon(marker);
    }
    p.restore();
}

// ---------------------------------------------------------------- mouse

void TimelinePanel::mousePressEvent(QMouseEvent* event)
{
    if (!m_project || event->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    if (m_popup)
        if (QWidget* popup = parentWidget())
            popup->raise();

    const QPointF pos = event->position();
    if (pos.y() < kRulerHeight)
    {
        m_dragMode = DragMode::Scrub;
        emit scrubStarted();
        m_frame = std::max(0, frameFromX(int(std::lround(pos.x()))));
        emit frameRequested(m_frame);
        update();
        return;
    }

    const int row = rowAt(int(pos.y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = m_project->entity(id);
    if (!entity)
    {
        m_dragMode = DragMode::Scrub;
        emit scrubStarted();
        m_frame = std::max(0, frameFromX(int(std::lround(pos.x()))));
        emit frameRequested(m_frame);
        update();
        return;
    }

    const QRect clip = clipRect(*entity);
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(pos.toPoint()))
    {
        openPopup(id);
        return;
    }

    const int x = int(std::lround(pos.x()));
    const bool nearLeft = std::abs(x - clip.left()) <= kTrimGrab;
    const bool nearRight = std::abs(x - clip.right()) <= kTrimGrab;
    m_dragEntity = id;
    m_dragRow = row;
    m_dragStartFrame = frameFromX(x);
    m_dragStartRow = row;
    m_dragClipStart = clipStartOf(*entity);
    m_dragClipEnd = clipEndOf(*entity);
    m_lastDragPos = pos;
    m_moved = false;

    if (!entity->isAudio() && (nearLeft || nearRight))
    {
        m_dragMode = nearLeft ? DragMode::TrimStart : DragMode::TrimEnd;
        beginDrag();
        m_autoScroll->start(30);
        update();
        return;
    }

    const int keyFrame = hitKeyframeRow(row, x);
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        KeyframeSelection selection{id, double(keyFrame)};
        const bool toggle = event->modifiers() & (Qt::ControlModifier
                                                  | Qt::ShiftModifier);
        if (!toggle)
            m_selectedKeyframes.clear();
        auto it = std::find(m_selectedKeyframes.begin(),
                            m_selectedKeyframes.end(), selection);
        if (toggle && it != m_selectedKeyframes.end())
            m_selectedKeyframes.erase(it);
        else if (it == m_selectedKeyframes.end())
            m_selectedKeyframes.append(selection);
        m_dragMode = DragMode::MoveKeyframe;
        m_keyDragStart = keyFrame;
        m_dragStartFrame = keyFrame;
        m_frame = std::max(0, keyFrame);
        emit frameRequested(m_frame);
        emit keyframeSelectionChanged(m_selectedKeyframes);
        beginDrag();
        m_autoScroll->start(30);
        update();
        return;
    }

    if (clip.contains(pos.toPoint()))
    {
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        selectEntity(id, true);
        m_dragMode = DragMode::MoveClip;
        beginDrag();
        m_autoScroll->start(30);
        update();
        return;
    }

    m_frame = std::max(0, frameFromX(x));
    emit frameRequested(m_frame);
    m_selectedKeyframes.clear();
    emit keyframeSelectionChanged(m_selectedKeyframes);
    update();
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_project)
        return;
    if (m_dragMode == DragMode::Scrub)
    {
        m_frame = std::max(0, frameFromX(int(std::lround(event->position().x()))));
        emit frameRequested(m_frame);
        update();
        return;
    }

    if (m_dragMode == DragMode::None)
    {
        const int row = rowAt(int(event->position().y()));
        const Entity* entity =
            m_project->entity(entityAtRow(row));
        m_hoverRow = row;
        m_hoverLeft = false;
        m_hoverRight = false;
        if (entity)
        {
            const QRect clip = clipRect(*entity);
            const int x = int(std::lround(event->position().x()));
            m_hoverLeft = !entity->isAudio()
                          && std::abs(x - clip.left()) <= kTrimGrab;
            m_hoverRight = !entity->isAudio()
                           && std::abs(x - clip.right()) <= kTrimGrab;
            if (m_hoverLeft || m_hoverRight)
                setCursor(Qt::SizeHorCursor);
            else if (clip.contains(event->position().toPoint()))
                setCursor(Qt::OpenHandCursor);
            else
                unsetCursor();
        }
        else
        {
            unsetCursor();
        }
        update();
        return;
    }

    m_lastDragPos = event->position();
    if (m_dragMode != DragMode::Scrub
        && (event->position().x() > visibleWidth() - 24
            || event->position().x() < 24
            || event->position().y() > bodyBottom() - 20
            || event->position().y() < kRulerHeight + 20))
    {
        m_autoScroll->start(30);
    }
    updateDragPreview(event->position());
}

void TimelinePanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_dragMode == DragMode::Scrub)
    {
        m_dragMode = DragMode::None;
        return;
    }
    QString text = QStringLiteral("移动片段");
    if (m_dragMode == DragMode::TrimStart || m_dragMode == DragMode::TrimEnd)
        text = QStringLiteral("裁剪片段");
    else if (m_dragMode == DragMode::MoveKeyframe)
        text = QStringLiteral("移动关键帧");
    endDrag(text);
}

void TimelinePanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_project || event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight)
        return;
    const int row = rowAt(int(event->position().y()));
    const EntityId id = entityAtRow(row);
    if (id == kInvalidEntity)
        return;
    m_frame = std::max(0, frameFromX(int(std::lround(event->position().x()))));
    emit frameRequested(m_frame);
    update();
}

void TimelinePanel::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double old = m_pixelsPerFrame;
        double next = std::clamp(old * std::pow(2.0, steps), 2.0, 32.0);
        if (next > 4.0)
            next = std::round(next);
        if (std::abs(next - old) < 1e-6)
            return;
        const int anchor = frameFromX(int(std::lround(event->position().x())));
        m_pixelsPerFrame = next;
        m_scrollX = std::max(0, int(std::lround(
                                   (anchor - contentStart()) * next
                                   - event->position().x())));
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
        m_scrollX -= int(std::lround(steps * 120.0));
    }
    else
    {
        m_scrollY -= int(std::lround(steps * kRowHeight));
    }
    clampScroll();
    update();
    syncPopups();
    event->accept();
}

void TimelinePanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete)
    {
        emit deleteKeyframesRequested();
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_dragMode != DragMode::None)
    {
        if (m_autoScroll)
            m_autoScroll->stop();
        m_dragging = false;
        m_preview.clear();
        m_before.clear();
        m_dragMode = DragMode::None;
        m_moved = false;
        update();
        syncPopups();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TimelinePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
        m_hScroll->setGeometry(0, height() - kScrollBarSize,
                               std::max(0, width() - kScrollBarSize),
                               kScrollBarSize);
    if (m_vScroll)
        m_vScroll->setGeometry(width() - kScrollBarSize, kRulerHeight,
                               kScrollBarSize,
                               std::max(0, height() - kRulerHeight
                                               - kScrollBarSize));
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::leaveEvent(QEvent* event)
{
    if (m_dragMode == DragMode::None)
        unsetCursor();
    QWidget::leaveEvent(event);
}

bool TimelinePanel::eventFilter(QObject* watched, QEvent* event)
{
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- popups

TimelinePanel* TimelinePanel::openPopup(EntityId ownerId)
{
    if (!m_project || ownerId == kInvalidEntity || ownerId == m_owner)
        return nullptr;
    if (m_popups.contains(ownerId))
    {
        ClipPopup* popup = m_popups.value(ownerId);
        popup->syncGeometry();
        popup->raise();
        return popup->panel();
    }
    const Entity* entity = m_project->entity(ownerId);
    if (!entity)
        return nullptr;
    if (depthOf(ownerId) >= 8)
        return nullptr;
    if (clipRectFor(ownerId).isEmpty())
    {
        ensureFrameVisible(clipStartOf(*entity));
    }
    auto* popup = new ClipPopup(m_project, ownerId, this);
    m_popups.insert(ownerId, popup);
    popup->panel()->setCurrentFrame(m_frame);
    popup->syncGeometry();
    return popup->panel();
}

void TimelinePanel::closeAllPopups()
{
    const auto popups = m_popups;
    m_popups.clear();
    for (ClipPopup* popup : popups)
    {
        if (!popup)
            continue;
        if (popup->panel())
            popup->panel()->closeAllPopups();
        popup->hide();
        popup->deleteLater();
    }
}

void TimelinePanel::syncPopups()
{
    for (auto it = m_popups.begin(); it != m_popups.end(); ++it)
    {
        ClipPopup* popup = it.value();
        if (!popup)
            continue;
        if (!m_project->entity(it.key()) || clipRectFor(it.key()).isEmpty())
        {
            popup->hide();
            continue;
        }
        popup->syncGeometry();
        if (popup->panel())
            popup->panel()->syncPopups();
    }
}

void TimelinePanel::removePopup(EntityId ownerId)
{
    m_popups.remove(ownerId);
}

// ---------------------------------------------------------------- API bits

void TimelinePanel::setDisplayFrames(bool frames)
{
    if (m_displayFrames == frames)
        return;
    m_displayFrames = frames;
    update();
    emit displayFramesChanged(frames);
}

void TimelinePanel::setPixelsPerFrameForTest(double pixels)
{
    m_pixelsPerFrame = std::clamp(pixels, 2.0, 32.0);
    clampScroll();
    update();
    syncPopups();
}

int TimelinePanel::rulerTickStepForTest() const
{
    if (m_pixelsPerFrame < 0.5)
        return 50;
    if (m_pixelsPerFrame < 1.0)
        return 20;
    if (m_pixelsPerFrame < 3.0)
        return 10;
    if (m_pixelsPerFrame < 5.0)
        return 5;
    return 1;
}

int TimelinePanel::rulerLabelStepForTest() const
{
    if (m_pixelsPerFrame < 0.5)
        return 200;
    if (m_pixelsPerFrame < 1.0)
        return 100;
    if (m_pixelsPerFrame < 3.0)
        return 50;
    if (m_pixelsPerFrame < 5.0)
        return 10;
    return 5;
}

int TimelinePanel::secondsCellStepForTest() const
{
    const int fps = std::max(1, m_project ? m_project->fps : 30);
    int cell = fps;
    while (cell * m_pixelsPerFrame < 5.0)
        cell *= 2;
    return cell;
}

int TimelinePanel::secondsLabelStepForTest() const
{
    const int fps = std::max(1, m_project ? m_project->fps : 30);
    int label = fps;
    while (label * m_pixelsPerFrame < 48.0)
        label *= 2;
    while (label > 1 && label % 2 == 0 && label * m_pixelsPerFrame > 240.0)
        label /= 2;
    return label;
}

EntityId TimelinePanel::rowEntityForTest(int row) const
{
    return entityAtRow(row);
}

QRect TimelinePanel::rowButtonRect(int row, int index) const
{
    Q_UNUSED(row);
    Q_UNUSED(index);
    return QRect();
}

bool TimelinePanel::verticalScrollVisibleForTest() const
{
    return m_vScroll && m_vScroll->isVisible();
}

int TimelinePanel::hitKeyframeRow(int row, int x) const
{
    const Entity* entity = m_project->entity(entityAtRow(row));
    if (!entity)
        return -1;
    int best = -1;
    int bestDistance = 7;
    for (int frame : keyframeFrames(*entity))
    {
        const int distance = std::abs(x - xFromFrame(frame));
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = frame;
        }
    }
    return best;
}

EntityId TimelinePanel::hitClipRow(int row, int x) const
{
    const Entity* entity = m_project->entity(entityAtRow(row));
    if (!entity)
        return kInvalidEntity;
    const QRect clip = clipRect(*entity);
    return clip.contains(x, clip.center().y()) ? entity->id : kInvalidEntity;
}

void TimelinePanel::syncPopupGeometry()
{
    syncPopups();
}

} // namespace pvf
