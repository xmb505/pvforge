#include "TimelinePanel.h"

#include "app/EditorSession.h"
#include "app/TimelineAdapter.h"
#include "core/Settings.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
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

QString displayName(const model::Entity& entity)
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

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

bool hasAssetMime(const QMimeData* mime)
{
    return mime
           && (mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-template"))
               || mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-html")));
}

QPixmap themedGlyph(const QStringList& names, int size, const QColor& color)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            continue;
        QPixmap pixmap = icon.pixmap(QSize(size, size));
        if (pixmap.isNull())
            continue;
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        return pixmap;
    }
    return QPixmap();
}

const QPixmap& plusGlyphPixmap()
{
    static const QPixmap pixmap =
        themedGlyph({QStringLiteral("list-add"),
                     QStringLiteral("list-add-symbolic")},
                    16, Qt::white);
    return pixmap;
}

} // namespace

// ---------------------------------------------------------------- popup

class ClipPopup : public QWidget
{
public:
    ClipPopup(Project* project, EntityId ownerId, TimelinePanel* source)
        : ClipPopup(project, nullptr, ownerId, source)
    {
    }

    ClipPopup(EntityId ownerId, TimelinePanel* source, TimelineAdapter* adapter)
        : ClipPopup(nullptr, adapter, ownerId, source)
    {
    }

private:
    ClipPopup(Project* project, TimelineAdapter* adapter, EntityId ownerId,
              TimelinePanel* source)
        : QWidget(source->window())
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
        const QPixmap closeGlyph =
            themedGlyph({QStringLiteral("window-close"),
                         QStringLiteral("window-close-symbolic")},
                        16, QColor(232, 234, 240));
        if (!closeGlyph.isNull())
        {
            close->setIcon(QIcon(closeGlyph));
            close->setIconSize(QSize(16, 16));
        }
        else
        {
            close->setText(QStringLiteral("×"));
        }
        close->setToolTip(QStringLiteral("关闭私有时间线"));
        headerLayout->addWidget(close);
        layout->addWidget(header);

        m_panel = adapter ? new TimelinePanel(adapter, this)
                          : new TimelinePanel(project, this);
        m_panel->m_owner = ownerId;
        m_panel->m_popup = true;
        m_panel->m_sourcePanel = source;
        m_panel->m_relativeTime = true;
        m_panel->m_pixelsPerFrame = source->m_pixelsPerFrame;
        m_panel->assignMissingRows();
        m_panel->setEditMode(source->m_editMode);
        if (m_panel->m_unitButton)
            m_panel->m_unitButton->hide();
        if (m_panel->m_modeButton)
            m_panel->m_modeButton->hide();
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
        connect(m_panel, &TimelinePanel::deleteEntityRequested, m_source,
                &TimelinePanel::deleteEntityRequested);
        connect(m_panel, &TimelinePanel::audioMoved, m_source,
                &TimelinePanel::audioMoved);
        connect(m_panel, &TimelinePanel::keyframesMoveRequested, m_source,
                &TimelinePanel::keyframesMoveRequested);
        connect(m_panel, &TimelinePanel::snapshotEditRequested, m_source,
                &TimelinePanel::snapshotEditRequested);
        connect(m_panel, &TimelinePanel::keyframeAddRequested, m_source,
                &TimelinePanel::keyframeAddRequested);
        connect(m_panel, &TimelinePanel::keyframeSelectionChanged, m_source,
                &TimelinePanel::adoptKeyframeSelection);
        connect(m_panel, &TimelinePanel::editCommitted, m_source,
                &TimelinePanel::editCommitted);
        connect(m_panel, &TimelinePanel::editFailed, m_source,
                &TimelinePanel::editFailed);

        syncGeometry();
    }

public:
    void syncGeometry()
    {
        const Entity* entity = m_source->entityById(m_ownerId);
        const QRect clip = m_source->clipRectFor(m_ownerId);
        if (!entity || clip.isEmpty() || m_source->window() == nullptr)
        {
            hide();
            return;
        }

        m_panel->m_pixelsPerFrame = m_source->m_pixelsPerFrame;
        if (m_source->sessionMode())
        {
            m_panel->setReadOnlySnapshot(m_source->m_snapshot);
        }
        else if (m_source->m_dragging)
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
        m_panel->m_pixelsPerFrame = m_source->m_pixelsPerFrame;
        // If the parent clip starts left of the visible main timeline, the
        // popup's left edge is clamped to the viewport. Scroll the popup to
        // the global frame under that edge so rulers, clips and playhead line
        // up with the main timeline instead of restarting at the clip start.
        const int sourceX =
            m_source->mapFrom(host, QPoint(left, clip.top())).x();
        const int sourceFrame = m_source->frameFromX(sourceX);
        const int contentStart = m_panel->contentStart();
        m_panel->m_scrollX = std::max(
            0, int(std::lround((std::max(contentStart, sourceFrame)
                                - contentStart)
                               * m_panel->m_pixelsPerFrame)));
        m_panel->clampScroll();
        show();
        raise();
        m_panel->update();
    }

    TimelinePanel* panel() const { return m_panel; }

private:
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
    initPanel();
    if (!m_project)
        return;
    connect(m_project, &Project::changed, this, [this] {
        assignMissingRows();
        if (m_selected != kInvalidEntity && !m_project->entity(m_selected))
        {
            m_selected = kInvalidEntity;
            m_entitySelection.clear();
        }
        clampScroll();
        syncPopups();
        update();
    });
    assignMissingRows();
}

TimelinePanel::TimelinePanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    if (m_session)
    {
        m_ownedAdapter = std::make_unique<TimelineAdapter>(m_session);
        m_adapter = m_ownedAdapter.get();
    }
    initPanel();
    if (m_session && m_session->document())
    {
        auto* document = m_session->document();
        connect(document, &next::Document::changed, this,
                &TimelinePanel::refreshFromSession);
        connect(document, &next::Document::playheadChanged, this, [this] {
            if (m_session)
                setCurrentFrame(m_session->playhead());
        });
        connect(document, &next::Document::selectionChanged, this, [this] {
            if (m_session && m_adapter && m_session->document())
                setSelectedEntity(m_adapter->idForUuid(
                    m_session->document()->selection()));
        });
    }
    refreshFromSession();
}

TimelinePanel::TimelinePanel(TimelineAdapter* adapter, QWidget* parent)
    : QWidget(parent)
    , m_session(adapter ? adapter->session() : nullptr)
    , m_adapter(adapter)
{
    initPanel();
    if (m_adapter)
        setReadOnlySnapshot(m_adapter->snapshot());
}

void TimelinePanel::initPanel()
{
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
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

    m_displayFrames = Settings::timelineDisplayFrames();
    m_unitButton = new QToolButton(this);
    m_unitButton->setObjectName(QStringLiteral("timelineUnitButton"));
    m_unitButton->setText(m_displayFrames ? QStringLiteral("帧")
                                          : QStringLiteral("秒"));
    m_unitButton->setToolTip(QStringLiteral("切换时间轴刻度（秒 / 帧）"));
    m_unitButton->setFixedSize(42, 20);
    m_unitButton->setFocusPolicy(Qt::NoFocus);
    m_unitButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: #2a2c33; color: #d8dce4;"
        " border: 1px solid #4a4d55; border-radius: 3px; padding: 0 4px; }"
        "QToolButton:hover { background: #3c404a; }"));
    connect(m_unitButton, &QToolButton::clicked, this,
            [this] { setDisplayFrames(!m_displayFrames); });

    m_modeButton = new QToolButton(this);
    m_modeButton->setObjectName(QStringLiteral("timelineModeButton"));
    m_modeButton->setText(QStringLiteral("片段"));
    m_modeButton->setToolTip(QStringLiteral("切换编辑模式：片段 / 关键帧"));
    m_modeButton->setFixedSize(54, 20);
    m_modeButton->setFocusPolicy(Qt::NoFocus);
    m_modeButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: #2a2c33; color: #d8dce4;"
        " border: 1px solid #4a4d55; border-radius: 3px; padding: 0 4px; }"
        "QToolButton:hover { background: #3c404a; }"));
    connect(m_modeButton, &QToolButton::clicked, this, [this] {
        setEditMode(m_editMode == EditMode::Clips ? EditMode::Keyframes
                                                  : EditMode::Clips);
    });
}

TimelinePanel::~TimelinePanel()
{
    closeAllPopups();
}

const Entity* TimelinePanel::entityById(EntityId id) const
{
    if (sessionMode())
        return findEntity(m_snapshot.entities, id);
    return m_project ? m_project->entity(id) : nullptr;
}

int TimelinePanel::currentFps() const
{
    if (m_project)
        return std::max(1, m_project->fps);
    return std::max(1, m_snapshot.fps);
}

bool TimelinePanel::isEntityLocked(EntityId id) const
{
    if (sessionMode())
    {
        const Entity* entity = entityById(id);
        return entity && entity->locked;
    }
    return m_project && id != kInvalidEntity && m_project->isEffectivelyLocked(id);
}

EntityId TimelinePanel::entityIdForUuid(const Uuid& uuid) const
{
    return m_adapter ? m_adapter->idForUuid(uuid) : kInvalidEntity;
}

Uuid TimelinePanel::uuidForEntityId(EntityId id) const
{
    return m_adapter ? m_adapter->uuidForId(id) : Uuid();
}

void TimelinePanel::refreshFromSession()
{
    if (!m_adapter)
        return;
    m_adapter->refresh();
    setReadOnlySnapshot(m_adapter->snapshot());
    if (m_session)
    {
        if (m_frame != m_session->playhead())
            setCurrentFrame(m_session->playhead());
        if (auto* document = m_session->document())
        {
            const EntityId selected =
                m_adapter->idForUuid(document->selection());
            if (selected != m_selected)
                setSelectedEntity(selected);
        }
    }
    for (ClipPopup* popup : m_popups)
        if (popup && popup->panel())
            popup->panel()->setReadOnlySnapshot(m_snapshot);
    syncPopups();
}

void TimelinePanel::setReadOnlySnapshot(const TimelineSnapshot& snapshot)
{
    if (!sessionMode())
        return;
    m_snapshot = snapshot;
    if (m_selected != kInvalidEntity && !entityById(m_selected))
    {
        m_selected = kInvalidEntity;
        m_entitySelection.clear();
    }
    if (m_owner != kInvalidEntity && !entityById(m_owner))
    {
        hide();
        return;
    }
    clampScroll();
    update();
}

void TimelinePanel::setCurrentFrame(int frame)
{
    m_frame = std::max(0, frame);
    const bool inRange =
        m_owner < 0 || (frame >= contentStart() && frame < contentEnd());
    if (m_dragMode == DragMode::None && inRange)
        ensureFrameVisible(m_frame);
    update();
    for (ClipPopup* popup : m_popups)
        if (popup && popup->panel())
            popup->panel()->setCurrentFrame(frame);
}

void TimelinePanel::setSelectedEntity(EntityId id)
{
    const Entity* entity = entityById(id);
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
        const Entity* parent = entityById(current);
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
    const Entity* entity = entityById(id);
    while (entity && entity->parent != kInvalidEntity && !seen.contains(entity->parent))
    {
        seen.insert(entity->parent);
        ++depth;
        entity = entityById(entity->parent);
    }
    return depth;
}

int TimelinePanel::rowOfEntity(const Entity& entity) const
{
    return entity.data.value(QStringLiteral("timelineRow"), -1).toInt();
}

void TimelinePanel::assignMissingRows()
{
    if (sessionMode() || !m_project)
        return;
    QVector<Entity>& entities = m_project->mutableEntities();
    QSet<int> used;
    for (const Entity& entity : entities)
    {
        if (entity.parent != m_owner)
            continue;
        const int row = rowOfEntity(entity);
        if (row >= 0 && !used.contains(row))
            used.insert(row);
    }
    int next = 0;
    for (Entity& entity : entities)
    {
        if (entity.parent != m_owner)
            continue;
        int row = rowOfEntity(entity);
        if (row >= 0 && used.contains(row))
        {
            // A row can already be owned by an earlier entity after a
            // reparent; keep the first one and move this one out.
            bool duplicate = false;
            for (const Entity& other : entities)
            {
                if (&other == &entity)
                    break;
                if (other.parent == m_owner && rowOfEntity(other) == row)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                continue;
        }
        while (used.contains(next))
            ++next;
        entity.data[QStringLiteral("timelineRow")] = next;
        used.insert(next);
    }
}

QVector<EntityId> TimelinePanel::rowIds() const
{
    QHash<int, EntityId> byRow;
    int maxRow = -1;
    for (const Entity& entity : activeEntities())
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && entityById(entity.parent) == nullptr;
        if (entity.parent != m_owner
            && !(m_owner == kInvalidEntity && dangling))
            continue;
        const int row = rowOfEntity(entity);
        if (row < 0)
            continue;
        byRow.insert(row, entity.id);
        maxRow = std::max(maxRow, row);
    }

    const int bodyHeight = std::max(0, bodyBottom() - kRulerHeight);
    const int pageRows =
        std::max(1, int(std::ceil(bodyHeight / double(kRowHeight))));
    const int count = std::max(maxRow + 1, pageRows + 3);
    QVector<EntityId> rows(count, kInvalidEntity);
    for (auto it = byRow.constBegin(); it != byRow.constEnd(); ++it)
        if (it.key() >= 0 && it.key() < rows.size())
            rows[it.key()] = it.value();
    return rows;
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
    if (m_owner < 0)
        return 0;
    const Entity* ownerEntity = rangeOwner();
    return ownerEntity ? clipStartOf(*ownerEntity) : 0;
}

const QVector<Entity>& TimelinePanel::activeEntities() const
{
    static const QVector<Entity> empty;
    if (sessionMode())
        return m_snapshot.entities;
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
    if (sessionMode() || !m_project)
        return nullptr;
    if (!m_dragging)
        return m_project->entity(id);
    return findEntity(m_preview, id);
}

int TimelinePanel::clipStartOf(const Entity& entity) const
{
    if (sessionMode())
        return std::max(0, entity.data.value(QStringLiteral("clipStart"), 0).toInt());
    if (entity.isAudio())
        return std::max(0, int(std::floor(
                               entity.data.value(QStringLiteral("startFrame"), 0.0)
                                   .toDouble())));
    return std::max(0, entity.data.value(QStringLiteral("clipStart"), 0).toInt());
}

int TimelinePanel::clipEndOf(const Entity& entity) const
{
    const int start = clipStartOf(entity);
    if (sessionMode())
    {
        const int stored = entity.data.value(QStringLiteral("clipEnd"), start)
                               .toInt();
        return std::max(start + 1, stored);
    }
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
    if (sessionMode())
        end = std::max(end, m_snapshot.contentEnd);
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
    const Entity* entity = entityById(id);
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
    const Entity* entity = entityById(id);
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
    if (!m_project && !sessionMode())
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
    const int maxY = std::max(0, rows * kRowHeight - bodyHeight);
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

void TimelinePanel::setScrubFrame(double rawX)
{
    // The pointer can leave the widget while scrubbing. Clamp it to the
    // visible track so the playhead line can never disappear outside it.
    const int x = std::clamp(int(std::lround(rawX)), 0, visibleWidth() - 1);
    m_frame = std::max(0, frameFromX(x));
    emit frameRequested(m_frame);
    update();
}

// ---------------------------------------------------------------- editing

void TimelinePanel::beginDrag()
{
    if (!m_project)
        return;
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

const model::Entity* TimelinePanel::sessionEntity(const Uuid& uuid) const
{
    if (!m_session || uuid.isEmpty())
        return nullptr;
    const auto it = m_session->project().entities.constFind(uuid);
    return it == m_session->project().entities.constEnd() ? nullptr : &it.value();
}

void TimelinePanel::beginSessionDrag()
{
    m_sessionBaseSnapshot = m_snapshot;
    m_sessionPlan = EditPlan();
    m_sessionPlanValid = false;
    m_dragging = false;
    m_moved = false;
    if (m_autoScroll)
        m_autoScroll->start(30);
    update();
}

void TimelinePanel::updateSessionDragPreview(const QPointF& pos)
{
    if (m_dragMode != DragMode::MoveClip
        && m_dragMode != DragMode::TrimStart
        && m_dragMode != DragMode::TrimEnd)
        return;
    if (!m_session || !m_session->editActions() || !m_adapter)
        return;
    const Uuid uuid = uuidForEntityId(m_dragEntity);
    if (uuid.isEmpty())
        return;
    EditActions* actions = m_session->editActions();

    EditPlan plan;
    QString error;
    if (m_dragMode == DragMode::MoveClip)
    {
        int delta = frameFromX(int(std::lround(pos.x()))) - m_dragStartFrame;
        const int lower = actions->minMoveDelta({uuid});
        delta = std::max(delta, lower);
        if (!actions->planMove({uuid}, delta, {}, &plan, &error))
            return;
    }
    else
    {
        int newStart = m_dragClipStart;
        int newEnd = m_dragClipEnd;
        if (m_dragMode == DragMode::TrimStart)
        {
            const int lower = actions->minTrimStart(uuid);
            const int upper = m_dragClipEnd - 1;
            if (upper < lower)
                return;
            newStart = std::clamp(frameFromX(int(std::lround(pos.x()))), lower,
                                  upper);
        }
        else
        {
            const int lower = m_dragClipStart + 1;
            const int upper =
                std::max(lower, actions->maxTrimEnd(uuid));
            newEnd = std::clamp(frameFromX(int(std::lround(pos.x()))), lower,
                                upper);
        }
        if (newStart == m_dragClipStart && newEnd == m_dragClipEnd)
        {
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
        if (!actions->planTrim(uuid, newStart, newEnd, &plan, &error))
            return;
    }

    if (plan.isEmpty())
    {
        m_snapshot = m_sessionBaseSnapshot;
        m_sessionPlanValid = false;
        m_moved = false;
        update();
        syncPopups();
        return;
    }

    m_sessionPlan = plan;
    m_sessionPlanValid = true;
    m_moved = true;
    m_snapshot = m_adapter->buildSnapshot(plan.project, m_frame);
    update();
    syncPopups();
}

EntityId TimelinePanel::sessionBaseEntityAtRow(int row) const
{
    if (row < 0)
        return kInvalidEntity;
    for (const Entity& entity : m_sessionBaseSnapshot.entities)
    {
        if (entity.parent != m_owner)
            continue;
        if (rowOfEntity(entity) == row)
            return entity.id;
    }
    return kInvalidEntity;
}

QRect TimelinePanel::sessionBaseClipRect(const Entity& entity) const
{
    const int row = rowOfEntity(entity);
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

bool TimelinePanel::isReparentCandidate(const Uuid& entity,
                                        const Uuid& target) const
{
    if (!m_session || entity.isEmpty() || target.isEmpty() || entity == target)
        return false;
    const model::Project& project = m_session->project();
    const auto source = project.entities.constFind(entity);
    const auto targetIt = project.entities.constFind(target);
    if (source == project.entities.constEnd()
        || targetIt == project.entities.constEnd())
        return false;
    if (target == source.value().parentUuid)
        return false;
    // Cannot parent an entity under its own subtree.
    Uuid parent = target;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (parent == entity)
            return false;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return !next::isEntityLocked(project, target);
}

QVector<Uuid> TimelinePanel::siblingOrderForDrop(const Uuid& entity,
                                                 const Uuid& target,
                                                 bool after) const
{
    QVector<Uuid> order;
    if (!m_session)
        return order;
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(entity);
    if (it == project.entities.constEnd())
        return order;
    order = model::childrenOf(project, it.value().parentUuid);
    order.removeAll(entity);
    const int index = order.indexOf(target);
    if (index < 0)
        return {};
    order.insert(index + (after ? 1 : 0), entity);
    return order;
}

int TimelinePanel::promptReparentMode(const Uuid& target)
{
    const model::Entity* parent = sessionEntity(target);
    const QString name = parent && !parent->name.isEmpty()
                             ? parent->name
                             : QStringLiteral("这个片段");
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("更换父级"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("把片段挂到「%1」的私有时间线？").arg(name));
    box.setInformativeText(
        QStringLiteral("保持世界位置：绝对帧不变，必要时会拒绝。\n"
                       "保持局部坐标：保留相对父级起点的偏移。"));
    auto* keepWorld =
        box.addButton(QStringLiteral("保持世界位置"), QMessageBox::AcceptRole);
    auto* keepLocal =
        box.addButton(QStringLiteral("保持局部坐标"), QMessageBox::AcceptRole);
    auto* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(keepWorld);
    box.exec();
    if (box.clickedButton() == keepWorld)
        return 1;
    if (box.clickedButton() == keepLocal)
        return 2;
    Q_UNUSED(cancel);
    return 0;
}

void TimelinePanel::deleteSelectedEntities()
{
    if (!m_session || !m_session->editActions() || m_selected == kInvalidEntity)
        return;
    const Uuid uuid = uuidForEntityId(m_selected);
    if (uuid.isEmpty())
        return;
    QString error;
    if (!m_session->editActions()->deleteEntities({uuid}, true, this, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("删除片段"));
}

void TimelinePanel::finishSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    const DragMode mode = m_dragMode;
    const EntityId draggedId = m_dragEntity;
    const QPointF releasePos = m_lastDragPos;
    const QVector<Entity> baseEntities = m_sessionBaseSnapshot.entities;

    m_dragging = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;

    if (!m_session || !m_session->editActions() || !m_adapter)
    {
        m_moved = false;
        refreshFromSession();
        return;
    }
    EditActions* actions = m_session->editActions();
    const Uuid uuid = uuidForEntityId(draggedId);
    if (uuid.isEmpty())
    {
        m_moved = false;
        refreshFromSession();
        return;
    }

    if (mode == DragMode::MoveClip)
    {
        int delta = frameFromX(int(std::lround(releasePos.x()))) - m_dragStartFrame;
        delta = std::max(delta, actions->minMoveDelta({uuid}));
        const int baseRow =
            (int(std::lround(releasePos.y())) - kRulerHeight + m_scrollY)
            / kRowHeight;
        const EntityId targetId = sessionBaseEntityAtRow(baseRow);
        const Entity* target = findEntity(baseEntities, targetId);
        const Uuid targetUuid = uuidForEntityId(targetId);
        const Entity* source = findEntity(baseEntities, draggedId);
        const bool targetHit =
            target && sessionBaseClipRect(*target).contains(releasePos.toPoint());
        const bool targetCanParent =
            target
            && (target->data.value(QStringLiteral("contentKind")).toString()
                    == QStringLiteral("empty")
                || !childIds(baseEntities, targetId).isEmpty());
        if (targetHit && targetCanParent && baseRow != m_dragStartRow
            && isReparentCandidate(uuid, targetUuid))
        {
            m_moved = false;
            m_sessionPlanValid = false;
            const int choice = promptReparentMode(targetUuid);
            if (choice != 0)
            {
                QString error;
                if (!actions->reparent(uuid, targetUuid, choice == 1, &error))
                    emit editFailed(error);
                else
                    emit editCommitted(QStringLiteral("更改片段父级"));
            }
            refreshFromSession();
            update();
            syncPopups();
            return;
        }

        if (target && source && baseRow != m_dragStartRow
            && target->parent == source->parent)
        {
            const QRect targetClip = sessionBaseClipRect(*target);
            const bool after =
                releasePos.y() > targetClip.top() + targetClip.height() / 2;
            const QVector<Uuid> order =
                siblingOrderForDrop(uuid, targetUuid, after);
            EditPlan plan;
            QString error;
            if (!order.isEmpty()
                && actions->planMove({uuid}, delta, order, &plan, &error))
            {
                m_sessionPlan = plan;
                m_sessionPlanValid = true;
                m_moved = true;
            }
            else if (!error.isEmpty())
            {
                emit editFailed(error);
            }
        }
    }

    if (m_sessionPlanValid && m_moved)
    {
        QString error;
        if (!actions->apply(m_sessionPlan, &error))
            emit editFailed(error);
        else
            emit editCommitted(m_sessionPlan.message);
    }

    m_sessionPlanValid = false;
    m_sessionPlan = EditPlan();
    m_moved = false;
    refreshFromSession();
    update();
    syncPopups();
}

void TimelinePanel::cancelSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    m_dragging = false;
    m_sessionPlanValid = false;
    m_sessionPlan = EditPlan();
    m_moved = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    refreshFromSession();
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

void TimelinePanel::moveEntityToRow(QVector<Entity>& entities, EntityId id,
                                    int targetRow)
{
    Entity* dragged = findEntity(entities, id);
    if (!dragged)
        return;
    targetRow = std::max(0, targetRow);
    const int owner = dragged->parent;
    const int oldRow = rowOfEntity(*dragged);
    if (oldRow == targetRow)
        return;
    Entity* occupant = nullptr;
    for (Entity& entity : entities)
    {
        if (entity.id == id || entity.parent != owner)
            continue;
        if (rowOfEntity(entity) == targetRow)
        {
            occupant = &entity;
            break;
        }
    }
    if (occupant)
        occupant->data[QStringLiteral("timelineRow")] = oldRow;
    dragged->data[QStringLiteral("timelineRow")] = targetRow;
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
    if (sessionMode())
    {
        updateSessionDragPreview(pos);
        return;
    }
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
        int targetRow = rowAt(int(pos.y()));
        const int rowsCount = int(rowIds().size());
        if (rowsCount > 0)
        {
            if (targetRow < 0)
                targetRow = pos.y() < kRulerHeight ? 0 : rowsCount - 1;
            targetRow = std::clamp(targetRow, 0, rowsCount - 1);
            moveEntityToRow(m_preview, m_dragEntity, targetRow);
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
    if ((!m_project && !sessionMode()) || m_dragMode == DragMode::None)
        return;
    if (m_dragMode == DragMode::Scrub)
    {
        const double rawX = m_lastDragPos.x();
        int dx = 0;
        if (rawX > visibleWidth() - 12)
            dx = 12;
        else if (rawX < 12)
            dx = -12;
        else
            return;
        m_scrollX = std::max(0, m_scrollX + dx);
        clampScroll();
        // Keep the playhead pinned to the visible edge while the view scrolls.
        setScrubFrame(rawX);
        syncPopups();
        return;
    }
    if (!m_moved)
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

    const int fps = currentFps();
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
    if (!m_project && !sessionMode())
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

    const int fps = currentFps();
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

    if (sessionMode())
    {
        for (int i = 0; i < m_snapshot.markers.size(); ++i)
        {
            const QRect marker = markerRect(i);
            if (marker.isEmpty())
                continue;
            const TimelineMarkerView& view = m_snapshot.markers.at(i);
            QColor color = view.color.isValid() ? view.color
                                                : QColor(255, 190, 60);
            QColor lineColor = color;
            lineColor.setAlpha(90);
            p.setPen(QPen(lineColor, 1.0));
            p.drawLine(marker.left() + 3, marker.bottom(), marker.left() + 3,
                       bottom);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(QRectF(marker).adjusted(0.5, 0.5, -0.5, -0.5),
                              3.0, 3.0);
            if (!view.title.isEmpty())
            {
                p.setPen(QColor(24, 26, 30));
                p.drawText(marker.adjusted(6, 0, -3, 0),
                           Qt::AlignLeft | Qt::AlignVCenter, view.title);
            }
        }
    }

    const QVector<EntityId> rows = rowIds();
    p.save();
    p.setClipRect(QRect(0, kRulerHeight, right, bottom - kRulerHeight));
    for (int row = 0; row < rows.size(); ++row)
    {
        const int top = rowTop(row);
        if (top + kRowHeight < kRulerHeight || top > bottom)
            continue;
        const Entity* entity = findEntity(activeEntities(), rows[row]);
        const QRect rowRect(0, top, right, kRowHeight);
        if (entity
            && (entity->id == m_selected
                || m_entitySelection.contains(entity->id)))
            p.fillRect(rowRect, QColor(accent.red(), accent.green(),
                                       accent.blue(), 45));
        else if (row % 2)
            p.fillRect(rowRect, QColor(alt.red(), alt.green(), alt.blue(), 90));
        p.setPen(QColor(mid.red(), mid.green(), mid.blue(), 70));
        p.drawLine(0, top + kRowHeight - 1, right, top + kRowHeight - 1);
        if (!entity)
            continue;

        const QRect clip = clipRect(*entity);
        if (clip.isEmpty())
            continue;
        QColor color = clipColor(entity->type);
        const bool selected =
            entity->id == m_selected
            || m_entitySelection.contains(entity->id);
        const bool hidden = sessionMode() ? entity->hidden
                                          : m_project->isEffectivelyHidden(entity->id);
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
        if (sessionMode() && isEntityLocked(entity->id))
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 150), 1, Qt::DashLine));
            p.drawRoundedRect(QRectF(clip).adjusted(2.5, 2.5, -2.5, -2.5), 4.0,
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
            const QPixmap& glyph = plusGlyphPixmap();
            if (!glyph.isNull())
            {
                p.drawPixmap(plus.topLeft(), glyph);
            }
            else
            {
                const QPointF plusCenter = QRectF(plus).center();
                p.drawLine(QPointF(plusCenter.x() - 4.0, plusCenter.y()),
                           QPointF(plusCenter.x() + 4.0, plusCenter.y()));
                p.drawLine(QPointF(plusCenter.x(), plusCenter.y() - 4.0),
                           QPointF(plusCenter.x(), plusCenter.y() + 4.0));
            }
        }

        const int nameRight = plus.isEmpty() ? clip.right() - 8 : plus.left() - 6;
        const QFontMetrics metrics(p.font());
        QString rangeInfo;
        if (sessionMode())
        {
            const int startFrame = clipStartOf(*entity);
            const int endFrame = clipEndOf(*entity);
            rangeInfo = QStringLiteral("%1–%2 · %3f")
                            .arg(startFrame)
                            .arg(endFrame)
                            .arg(endFrame - startFrame);
            if (entity->isAudio())
            {
                const double seconds = entity->data
                                           .value(QStringLiteral(
                                               "assetDurationSeconds"))
                                           .toDouble();
                if (seconds > 0.0)
                    rangeInfo += QStringLiteral(" · %1s").arg(seconds, 0, 'f', 2);
            }
        }
        const int infoWidth =
            rangeInfo.isEmpty() ? 0 : metrics.horizontalAdvance(rangeInfo) + 10;
        const int nameWidth =
            std::max(0, nameRight - clip.left() - 8 - infoWidth);
        if (nameRight > clip.left() + 12)
        {
            const QColor nameColor =
                color.lightness() > 150 ? QColor(24, 26, 30) : QColor(255, 255, 255);
            const QString name = metrics.elidedText(entity->name, Qt::ElideRight,
                                                    nameWidth);
            const QRect nameRect(clip.left() + 8, clip.top(), nameWidth,
                                 clip.height());
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
        if (!rangeInfo.isEmpty() && nameRight > clip.left() + 12)
        {
            const QRect infoRect(nameRight - infoWidth, clip.top(), infoWidth,
                                 clip.height());
            p.setPen(QColor(255, 255, 255, 205));
            p.drawText(infoRect, Qt::AlignVCenter | Qt::AlignRight, rangeInfo);
        }
        QString badges;
        if (sessionMode())
        {
            if (isEntityLocked(entity->id))
                badges += QStringLiteral("锁");
            if (hidden)
                badges += QStringLiteral("隐");
        }
        if (!badges.isEmpty() && clip.height() >= 20)
        {
            p.setPen(QColor(255, 255, 255, 200));
            p.drawText(QRect(clip.left() + 6, clip.bottom() - 14, 40, 12),
                       Qt::AlignLeft | Qt::AlignVCenter, badges);
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
            const double radius = keySelected ? 5.5 : 4.5;
            QPolygonF diamond;
            diamond << QPointF(x, centerY - radius)
                    << QPointF(x + radius, centerY)
                    << QPointF(x, centerY + radius)
                    << QPointF(x - radius, centerY);
            if (keySelected)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 95));
                p.drawEllipse(QPointF(x, centerY), 9.0, 9.0);
                p.setPen(QPen(Qt::white, 1.5));
                p.setBrush(accent);
            }
            else
            {
                p.setPen(QPen(QColor(14, 16, 20, 210), 1.2));
                p.setBrush(QColor(248, 250, 255));
            }
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

    const bool noEntities =
        std::none_of(rows.begin(), rows.end(),
                     [](EntityId id) { return id != kInvalidEntity; });
    if (noEntities)
    {
        p.setPen(dim);
        p.drawText(QRect(0, kRulerHeight, right, bottom - kRulerHeight),
                   Qt::AlignCenter,
                   m_owner < 0
                       ? QStringLiteral("把素材拖到这里创建 clip")
                       : QStringLiteral("把素材拖到这里创建子 clip"));
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd)
                               .normalized()
                               .intersected(QRectF(0, kRulerHeight, right,
                                                   bottom - kRulerHeight));
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 50));
        p.drawRect(box);
    }

    if (m_dropActive)
    {
        const int dropX = xFromFrame(m_dropFrame);
        if (dropX >= 0 && dropX <= right)
        {
            p.setPen(QPen(accent.lighter(120), 2));
            p.drawLine(dropX, kRulerHeight, dropX, bottom);
            p.setPen(accent.lighter(140));
            p.drawText(QRect(dropX + 4, kRulerHeight + 2, 120, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("放到第 %1 帧").arg(m_dropFrame));
        }
    }

    const bool playheadInOwner =
        m_owner < 0 || (m_frame >= contentStart() && m_frame < contentEnd());
    const int playhead = xFromFrame(m_frame);
    if (playheadInOwner && playhead >= 0 && playhead <= right)
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
    if ((!m_project && !sessionMode()) || event->button() != Qt::LeftButton)
        return;
    setFocus(Qt::MouseFocusReason);
    if (m_popup)
        if (QWidget* popup = parentWidget())
            popup->raise();

    const QPointF pos = event->position();
    if (pos.y() < kRulerHeight)
    {
        m_dragMode = DragMode::Scrub;
        m_lastDragPos = pos;
        m_autoScroll->start(30);
        emit scrubStarted();
        setScrubFrame(pos.x());
        return;
    }

    const int row = rowAt(int(pos.y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    const int pressX = int(std::lround(pos.x()));
    if (!entity)
    {
        m_dragMode = DragMode::PendingSelect;
        m_pressPos = pos;
        m_boxStart = pos;
        m_boxEnd = pos;
        m_pressFrame = std::max(0, frameFromX(pressX));
        m_pressEntity = kInvalidEntity;
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
    if (sessionMode())
    {
        m_dragEntity = id;
        m_dragRow = row;
        m_dragStartFrame = frameFromX(x);
        m_dragStartRow = row;
        m_dragClipStart = clipStartOf(*entity);
        m_dragClipEnd = clipEndOf(*entity);
        m_lastDragPos = pos;
        m_moved = false;

        if (m_editMode == EditMode::Clips && !isEntityLocked(id))
        {
            const bool nearLeft = std::abs(x - clip.left()) <= kTrimGrab;
            const bool nearRight = std::abs(x - clip.right()) <= kTrimGrab;
            if (nearLeft || nearRight)
            {
                selectEntity(id, true);
                m_dragMode = nearLeft ? DragMode::TrimStart : DragMode::TrimEnd;
                beginSessionDrag();
                update();
                return;
            }
            if (clip.contains(pos.toPoint()))
            {
                selectEntity(id, true);
                m_selectedKeyframes.clear();
                emit keyframeSelectionChanged(m_selectedKeyframes);
                m_dragMode = DragMode::MoveClip;
                beginSessionDrag();
                update();
                return;
            }
        }

        m_dragMode = DragMode::PendingSelect;
        m_pressPos = pos;
        m_boxStart = pos;
        m_boxEnd = pos;
        m_pressFrame = std::max(0, m_dragStartFrame);
        m_pressEntity = id;
        update();
        return;
    }
    m_dragEntity = id;
    m_dragRow = row;
    m_dragStartFrame = frameFromX(x);
    m_dragStartRow = row;
    m_dragClipStart = clipStartOf(*entity);
    m_dragClipEnd = clipEndOf(*entity);
    m_lastDragPos = pos;
    m_moved = false;

    // Keyframes win over trim grips in keyframe mode: a key at the clip edge
    // must still be selectable. In clip mode the diamond is display-only.
    const int keyFrame =
        m_editMode == EditMode::Keyframes ? hitKeyframeRow(row, x) : -1;
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

    const bool nearLeft = std::abs(x - clip.left()) <= kTrimGrab;
    const bool nearRight = std::abs(x - clip.right()) <= kTrimGrab;
    if (m_editMode == EditMode::Clips && !entity->isAudio()
        && (nearLeft || nearRight))
    {
        m_dragMode = nearLeft ? DragMode::TrimStart : DragMode::TrimEnd;
        beginDrag();
        m_autoScroll->start(30);
        update();
        return;
    }

    if (clip.contains(pos.toPoint()))
    {
        if (m_editMode == EditMode::Keyframes)
        {
            // In keyframe mode a clip body is a safe place to start a
            // rubber band; a click without movement still selects the row.
            m_dragMode = DragMode::PendingSelect;
            m_pressPos = pos;
            m_boxStart = pos;
            m_boxEnd = pos;
            m_pressFrame = std::max(0, frameFromX(x));
            m_pressEntity = id;
            update();
            return;
        }
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        selectEntity(id, true);
        m_dragMode = DragMode::MoveClip;
        beginDrag();
        m_autoScroll->start(30);
        update();
        return;
    }

    m_dragMode = DragMode::PendingSelect;
    m_pressPos = pos;
    m_boxStart = pos;
    m_boxEnd = pos;
    m_pressFrame = std::max(0, frameFromX(pressX));
    m_pressEntity = id;
    update();
}

void TimelinePanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_project && !sessionMode())
        return;
    if (m_dragMode == DragMode::Scrub)
    {
        m_lastDragPos = event->position();
        setScrubFrame(event->position().x());
        if (event->position().x() > visibleWidth() - 12
            || event->position().x() < 12)
            m_autoScroll->start(30);
        return;
    }

    if (m_dragMode == DragMode::PendingSelect)
    {
        if (sessionMode())
            return;
        const QPointF delta = event->position() - m_pressPos;
        if (std::abs(delta.x()) + std::abs(delta.y()) <= 5.0)
            return;
        m_dragMode = DragMode::BoxSelect;
        m_boxStart = m_pressPos;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        m_boxEnd = event->position();
        update();
        return;
    }

    if (m_dragMode == DragMode::None)
    {
        const int row = rowAt(int(event->position().y()));
        const Entity* entity = entityById(entityAtRow(row));
        m_hoverRow = row;
        m_hoverLeft = false;
        m_hoverRight = false;
        if (entity)
        {
            const QRect clip = clipRect(*entity);
            const int x = int(std::lround(event->position().x()));
            if (sessionMode())
            {
                const int startFrame = clipStartOf(*entity);
                const int endFrame = clipEndOf(*entity);
                QString tip = QStringLiteral("%1\n%2–%3 帧 · 时长 %4 帧")
                                  .arg(entity->name)
                                  .arg(startFrame)
                                  .arg(endFrame)
                                  .arg(endFrame - startFrame);
                if (entity->locked)
                    tip += QStringLiteral("\n已锁定（不能拖动或裁剪）");
                if (entity->hidden)
                    tip += QStringLiteral("\n已隐藏");
                if (entity->isAudio())
                {
                    const QString hash =
                        entity->data.value(QStringLiteral("assetHash")).toString();
                    if (!hash.isEmpty())
                        tip += QStringLiteral("\n素材 %1…").arg(hash.left(12));
                }
                setToolTip(tip);
                if (!entity->locked && m_editMode == EditMode::Clips
                    && (std::abs(x - clip.left()) <= kTrimGrab
                        || std::abs(x - clip.right()) <= kTrimGrab))
                    setCursor(Qt::SizeHorCursor);
                else if (clip.contains(event->position().toPoint()))
                    setCursor(entity->locked ? Qt::ArrowCursor
                                             : Qt::OpenHandCursor);
                else
                    unsetCursor();
                update();
                return;
            }
            m_hoverLeft = !entity->isAudio()
                          && std::abs(x - clip.left()) <= kTrimGrab;
            m_hoverRight = !entity->isAudio()
                           && std::abs(x - clip.right()) <= kTrimGrab;
            if (m_editMode == EditMode::Keyframes
                && hitKeyframeRow(row, x) >= 0)
                setCursor(Qt::PointingHandCursor);
            else if (m_editMode == EditMode::Clips
                     && (m_hoverLeft || m_hoverRight))
                setCursor(Qt::SizeHorCursor);
            else if (m_editMode == EditMode::Clips
                     && clip.contains(event->position().toPoint()))
                setCursor(Qt::OpenHandCursor);
            else
                unsetCursor();
        }
        else
        {
            unsetCursor();
            if (sessionMode())
                setToolTip(QString());
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
    if (m_dragMode == DragMode::PendingSelect)
    {
        m_dragMode = DragMode::None;
        m_frame = std::max(0, m_pressFrame);
        emit frameRequested(m_frame);
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        if (m_pressEntity != kInvalidEntity
            && (sessionMode() || !isEntityLocked(m_pressEntity)))
            selectEntity(m_pressEntity, true);
        update();
        return;
    }
    if (m_dragMode == DragMode::BoxSelect)
    {
        if (sessionMode())
        {
            m_dragMode = DragMode::None;
            return;
        }
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        const QVector<EntityId> rows = rowIds();
        if (m_editMode == EditMode::Keyframes)
        {
            const int firstRow = rowAt(int(box.top()));
            const int lastRow = rowAt(int(box.bottom()));
            const int firstFrame = std::max(0, frameFromX(int(box.left())));
            const int lastFrame = std::max(0, frameFromX(int(box.right())));
            const bool additive = event->modifiers() & Qt::ShiftModifier;
            if (!additive)
                m_selectedKeyframes.clear();
            for (int row = std::max(0, firstRow);
                 row <= std::min(lastRow, int(rows.size()) - 1); ++row)
            {
                const Entity* entity = m_project->entity(rows[row]);
                if (!entity || m_project->isEffectivelyLocked(entity->id))
                    continue;
                for (int frame : keyframeFrames(*entity))
                {
                    if (frame < firstFrame || frame > lastFrame)
                        continue;
                    const KeyframeSelection selection{entity->id, double(frame)};
                    if (std::find(m_selectedKeyframes.begin(),
                                  m_selectedKeyframes.end(), selection)
                        == m_selectedKeyframes.end())
                        m_selectedKeyframes.append(selection);
                }
            }
            if (lastRow >= 0 && lastRow < rows.size())
            {
                const Entity* last = m_project->entity(rows[lastRow]);
                if (last && !m_project->isEffectivelyLocked(last->id))
                    selectEntity(last->id, true);
            }
            emit keyframeSelectionChanged(m_selectedKeyframes);
        }
        else
        {
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            m_entitySelection.clear();
            EntityId lastId = kInvalidEntity;
            for (EntityId rowId : rows)
            {
                const Entity* entity = m_project->entity(rowId);
                if (!entity || m_project->isEffectivelyLocked(entity->id))
                    continue;
                const QRect clip = clipRect(*entity);
                if (clip.isEmpty() || !clip.intersects(box.toRect()))
                    continue;
                m_entitySelection.insert(entity->id);
                lastId = entity->id;
            }
            m_selected = lastId;
            if (lastId != kInvalidEntity && m_entitySelection.size() == 1)
                emit entitySelected(lastId);
            update();
        }
        m_dragMode = DragMode::None;
        return;
    }
    if (m_dragMode == DragMode::Scrub)
    {
        if (m_autoScroll)
            m_autoScroll->stop();
        m_dragMode = DragMode::None;
        return;
    }
    if (sessionMode())
    {
        m_lastDragPos = event->position();
        finishSessionDrag();
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
    // P2b1 does not edit keyframes from the session-backed timeline yet.
    if (sessionMode() || !m_project || event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight)
        return;
    const int row = rowAt(int(event->position().y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || isEntityLocked(id))
        return;
    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->position().toPoint()))
        return;
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(event->position().toPoint()))
        return;

    const int frame =
        std::max(0, frameFromX(int(std::lround(event->position().x()))));
    m_frame = frame;
    emit frameRequested(m_frame);
    selectEntity(id, true);
    emit keyframeAddRequested(id, frame);
    m_selectedKeyframes.clear();
    m_selectedKeyframes.append(KeyframeSelection{id, double(frame)});
    emit keyframeSelectionChanged(m_selectedKeyframes);
    update();
}

void TimelinePanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_project || !hasAssetMime(event->mimeData()))
        return;
    m_dropActive = true;
    m_dropFrame = std::max(contentStart(),
                           frameFromX(int(std::lround(event->position().x()))));
    if (m_owner >= 0)
        m_dropFrame = std::min(m_dropFrame, contentEnd() - 1);
    event->acceptProposedAction();
    update();
}

void TimelinePanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!m_project || !hasAssetMime(event->mimeData()))
        return;
    m_dropActive = true;
    m_dropFrame = std::max(contentStart(),
                           frameFromX(int(std::lround(event->position().x()))));
    if (m_owner >= 0)
        m_dropFrame = std::min(m_dropFrame, contentEnd() - 1);
    event->acceptProposedAction();
    update();
}

void TimelinePanel::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    m_dropActive = false;
    update();
}

void TimelinePanel::dropEvent(QDropEvent* event)
{
    if (!m_project || !hasAssetMime(event->mimeData()))
    {
        m_dropActive = false;
        update();
        return;
    }
    int frame = frameFromX(int(std::lround(event->position().x())));
    frame = std::max(contentStart(), frame);
    if (m_owner >= 0)
        frame = std::min(frame, contentEnd() - 1);
    int row = rowAt(int(std::lround(event->position().y())));
    if (row < 0)
        row = std::max(0, int(rowIds().size()) - 1);
    const EntityId id = dropAsset(*event->mimeData(), frame, row);
    m_dropActive = false;
    update();
    if (id == kInvalidEntity)
        event->ignore();
    else
        event->acceptProposedAction();
}

EntityId TimelinePanel::dropAsset(const QMimeData& mime, int frame, int row)
{
    if (!m_project || !hasAssetMime(&mime))
        return kInvalidEntity;

    frame = std::max(contentStart(), frame);
    if (m_owner >= 0)
        frame = std::min(frame, contentEnd() - 1);

    const bool html = mime.hasFormat(
        QStringLiteral("application/x-pvforge-asset-html"));
    const QVector<Entity> before = m_project->entities();
    Entity* created = nullptr;
    if (html)
    {
        created = m_project->addHtmlLayer(QString::fromUtf8(
            mime.data(QStringLiteral("application/x-pvforge-asset-html"))));
    }
    else
    {
        const int index =
            mime.data(QStringLiteral("application/x-pvforge-asset-template"))
                .toInt();
        const auto& templates = m_project->templates();
        if (index >= 0 && index < templates.size())
            created = m_project->createFromTemplate(templates[index]);
    }
    if (!created)
        return kInvalidEntity;

    const EntityId id = created->id;
    if (m_owner != kInvalidEntity)
        m_project->setEntityParent(id, m_owner);
    Entity* entity = m_project->entity(id);
    if (!entity)
        return kInvalidEntity;

    const int fps = std::max(1, m_project->fps);
    int length = fps;
    if (entity->isAudio())
    {
        const double duration =
            entity->data.value(QStringLiteral("duration"), 0.0).toDouble();
        if (duration > 0.0)
            length = std::max(1, int(std::ceil(duration * fps)));
        entity->data[QStringLiteral("startFrame")] = frame;
    }
    else
    {
        if (entity->type == EntityType::Video)
        {
            const double duration =
                entity->data.value(QStringLiteral("duration"), 0.0).toDouble();
            if (duration > 0.0)
                length = std::max(1, int(std::ceil(duration * fps)));
        }
        entity->data[QStringLiteral("clipStart")] = frame;
        entity->data[QStringLiteral("clipEnd")] = frame + length;
    }

    // A video template also creates its paired audio; keep it on the same
    // drop frame and in the same private timeline.
    QVector<EntityId> pairedIds;
    for (Entity& other : m_project->mutableEntities())
    {
        if (other.id == id)
            continue;
        if (other.data.value(QStringLiteral("pairedVideo")).toInt() == id)
        {
            other.data[QStringLiteral("startFrame")] = frame;
            if (m_owner != kInvalidEntity)
                other.parent = m_owner;
            pairedIds.append(other.id);
        }
    }

    QVector<Entity>& all = m_project->mutableEntities();
    int maxRow = -1;
    for (const Entity& other : all)
        if (other.parent == m_owner)
            maxRow = std::max(maxRow, rowOfEntity(other));
    int targetRow = row >= 0 ? row : maxRow + 1;
    targetRow = std::max(0, targetRow);

    QSet<EntityId> createdIds;
    createdIds.insert(id);
    for (EntityId pairedId : pairedIds)
        createdIds.insert(pairedId);
    auto insertAtRow = [&](EntityId entityId, int desiredRow) {
        Entity* target = m_project->entity(entityId);
        if (!target)
            return;
        for (Entity& other : all)
        {
            if (createdIds.contains(other.id) || other.parent != m_owner)
                continue;
            const int otherRow = rowOfEntity(other);
            if (otherRow >= desiredRow)
                other.data[QStringLiteral("timelineRow")] = otherRow + 1;
        }
        target->data[QStringLiteral("timelineRow")] = desiredRow;
    };
    insertAtRow(id, targetRow);
    for (int i = 0; i < pairedIds.size(); ++i)
        insertAtRow(pairedIds[i], targetRow + 1 + i);

    assignMissingRows();
    m_project->notifyChanged();
    selectEntity(id, true);
    emit snapshotEditRequested(before, m_project->entities(),
                               QStringLiteral("添加素材"));
    return id;
}

void TimelinePanel::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_project && !sessionMode())
        return;
    const int row = rowAt(event->pos().y());
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity)
        return;
    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->pos()))
        return;

    selectEntity(id, true);
    QMenu menu(this);
    QAction* open = menu.addAction(QStringLiteral("打开私有时间线"));
    QAction* remove = nullptr;
    QHash<QAction*, QPair<Uuid, bool>> reparents;
    if (sessionMode())
    {
        remove = menu.addAction(QStringLiteral("删除片段"));
        remove->setEnabled(!isEntityLocked(id));

        const Uuid source = uuidForEntityId(id);
        const model::Project& project = m_session->project();
        QMenu* parentMenu = menu.addMenu(QStringLiteral("更换父级"));
        auto addTarget = [&](const Uuid& target, const QString& label,
                             bool enabled) {
            QMenu* sub = parentMenu->addMenu(label);
            sub->setEnabled(enabled);
            QAction* world = sub->addAction(QStringLiteral("保持世界位置"));
            QAction* local = sub->addAction(QStringLiteral("保持局部坐标"));
            reparents.insert(world, qMakePair(target, true));
            reparents.insert(local, qMakePair(target, false));
        };

        const model::Entity& sourceEntity = project.entities.value(source);
        QVector<Uuid> candidates;
        for (auto it = project.entities.constBegin();
             it != project.entities.constEnd(); ++it)
        {
            if (it.key() == source || it.key() == sourceEntity.parentUuid)
                continue;
            bool descendant = false;
            Uuid parent = it.key();
            QSet<Uuid> seen;
            while (!parent.isEmpty() && !seen.contains(parent))
            {
                if (parent == source)
                {
                    descendant = true;
                    break;
                }
                seen.insert(parent);
                parent = project.entities.value(parent).parentUuid;
            }
            if (!descendant)
                candidates.append(it.key());
        }
        std::sort(candidates.begin(), candidates.end(),
                  [&project](const Uuid& a, const Uuid& b) {
                      const QString an = displayName(project.entities.value(a));
                      const QString bn = displayName(project.entities.value(b));
                      if (an != bn)
                          return an < bn;
                      return a < b;
                  });
        if (!sourceEntity.parentUuid.isEmpty())
            addTarget(Uuid(), QStringLiteral("顶层（无父级）"), true);
        for (const Uuid& candidate : candidates)
        {
            const model::Entity& target = project.entities.value(candidate);
            addTarget(candidate, displayName(target),
                      !next::isEntityLocked(project, candidate));
        }
        if (parentMenu->isEmpty())
            parentMenu->setEnabled(false);
    }
    else
    {
        remove = menu.addAction(QStringLiteral("删除片段"));
        remove->setEnabled(!m_project->isEffectivelyLocked(id));
    }
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == open)
    {
        openPopup(id);
    }
    else if (remove && chosen == remove)
    {
        if (sessionMode())
        {
            deleteSelectedEntities();
            refreshFromSession();
        }
        else
        {
            emit deleteEntityRequested(id);
        }
    }
    else if (reparents.contains(chosen))
    {
        const auto target = reparents.value(chosen);
        QString error;
        if (!m_session->editActions()->reparent(uuidForEntityId(id), target.first,
                                                target.second, &error))
            emit editFailed(error);
        else
            emit editCommitted(QStringLiteral("更改片段父级"));
        refreshFromSession();
    }
    event->accept();
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
        if (sessionMode())
        {
            if (m_selected != kInvalidEntity)
                deleteSelectedEntities();
            return;
        }
        if (!m_selectedKeyframes.isEmpty())
        {
            emit deleteKeyframesRequested();
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            update();
            return;
        }
        if (m_selected != kInvalidEntity)
        {
            emit deleteEntityRequested(m_selected);
            return;
        }
        return;
    }
    if (event->key() == Qt::Key_Escape && m_dragMode != DragMode::None)
    {
        if (sessionMode())
        {
            cancelSessionDrag();
            return;
        }
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
    if (m_unitButton)
    {
        m_unitButton->setGeometry(std::max(4, width() - kScrollBarSize - 46), 3,
                                  42, 20);
        m_unitButton->setVisible(m_owner < 0 && width() > 120);
    }
    if (m_modeButton)
    {
        m_modeButton->setGeometry(std::max(4, width() - kScrollBarSize - 104),
                                  3, 54, 20);
        m_modeButton->setVisible(m_owner < 0 && width() > 220);
    }
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
    if ((!m_project && !sessionMode()) || ownerId == kInvalidEntity
        || ownerId == m_owner)
        return nullptr;
    if (m_popups.contains(ownerId))
    {
        ClipPopup* popup = m_popups.value(ownerId);
        popup->syncGeometry();
        popup->raise();
        return popup->panel();
    }
    const Entity* entity = entityById(ownerId);
    if (!entity)
        return nullptr;
    if (depthOf(ownerId) >= 8)
        return nullptr;
    if (clipRectFor(ownerId).isEmpty())
    {
        ensureFrameVisible(clipStartOf(*entity));
    }
    auto* popup = sessionMode() && m_adapter
                      ? new ClipPopup(ownerId, this, m_adapter)
                      : new ClipPopup(m_project, ownerId, this);
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
        if (!entityById(it.key()) || clipRectFor(it.key()).isEmpty())
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
    if (m_unitButton)
        m_unitButton->setText(frames ? QStringLiteral("帧") : QStringLiteral("秒"));
    update();
    emit displayFramesChanged(frames);
    for (ClipPopup* popup : m_popups)
        if (popup && popup->panel())
            popup->panel()->setDisplayFrames(frames);
}

void TimelinePanel::setPixelsPerFrameForTest(double pixels)
{
    m_pixelsPerFrame = std::clamp(pixels, 2.0, 32.0);
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::setEditMode(EditMode mode)
{
    if (m_editMode == mode)
        return;
    m_editMode = mode;
    if (m_modeButton)
        m_modeButton->setText(mode == EditMode::Clips ? QStringLiteral("片段")
                                                      : QStringLiteral("关键帧"));
    if (mode == EditMode::Clips)
    {
        m_selectedKeyframes.clear();
        emit keyframeSelectionChanged(m_selectedKeyframes);
    }
    update();
    for (ClipPopup* popup : m_popups)
        if (popup && popup->panel())
            popup->panel()->setEditMode(mode);
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
    const int fps = currentFps();
    int cell = fps;
    while (cell * m_pixelsPerFrame < 5.0)
        cell *= 2;
    return cell;
}

int TimelinePanel::secondsLabelStepForTest() const
{
    const int fps = currentFps();
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

int TimelinePanel::entityRowCountForTest() const
{
    int count = 0;
    for (EntityId id : rowIds())
        if (id != kInvalidEntity)
            ++count;
    return count;
}

QString TimelinePanel::rowNameForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->name : QString();
}

int TimelinePanel::rowStartForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    return entity->data.value(QStringLiteral("modelStart"),
                              clipStartOf(*entity))
        .toInt();
}

int TimelinePanel::rowEndForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    return entity->data.value(QStringLiteral("modelEnd"), clipEndOf(*entity))
        .toInt();
}

bool TimelinePanel::rowIsAudioForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity && entity->isAudio();
}

QString TimelinePanel::rowAssetHashForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->data.value(QStringLiteral("assetHash")).toString()
                  : QString();
}

double TimelinePanel::rowAssetSecondsForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->data
                        .value(QStringLiteral("assetDurationSeconds"))
                        .toDouble()
                  : 0.0;
}

int TimelinePanel::markerFrameForTest(int index) const
{
    return index >= 0 && index < m_snapshot.markers.size()
               ? m_snapshot.markers.at(index).frame
               : -1;
}

QRect TimelinePanel::markerRect(int index) const
{
    if (index < 0 || index >= m_snapshot.markers.size())
        return QRect();
    const TimelineMarkerView& marker = m_snapshot.markers.at(index);
    if (marker.frame < contentStart() || marker.frame > contentEnd())
        return QRect();
    const int x = xFromFrame(marker.frame);
    const int right = width() - kScrollBarSize;
    if (x < 0 || x > right)
        return QRect();
    const QFontMetrics metrics(font());
    const int markerWidth = std::max(
        10, std::min(140, metrics.horizontalAdvance(marker.title) + 12));
    return QRect(x + 1, kRulerHeight - 14, markerWidth, 12);
}

int TimelinePanel::rowForEntityForTest(EntityId id) const
{
    const Entity* entity = entityById(id);
    return entity ? rowOfEntity(*entity) : -1;
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
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    int best = -1;
    int bestDistance = 10;
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
    const Entity* entity = entityById(entityAtRow(row));
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
