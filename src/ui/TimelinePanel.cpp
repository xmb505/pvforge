#include "TimelinePanel.h"

#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "app/TimelineAdapter.h"
#include "core/Settings.h"
#include "model/Query.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"
#include "next/timeline/MarkerDialog.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
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

// Mine-imator's frame tick density table.  The tick interval is measured in
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

// Seconds-mode ruler grid.  Both intervals are measured in frames and always
// divide whole seconds evenly for the project frame rate.
struct SecondsGrid
{
    int labelFrames = 1;
    int cellFrames = 1;
};

SecondsGrid secondsGrid(int fps, double pixelsPerFrame)
{
    const int safeFps = std::max(1, fps);
    SecondsGrid grid;

    int labelFrames = safeFps;
    while (labelFrames * pixelsPerFrame < 48.0)
        labelFrames *= 2;
    while (labelFrames > 1 && labelFrames % 2 == 0
           && labelFrames * pixelsPerFrame > 240.0)
        labelFrames /= 2;
    grid.labelFrames = labelFrames;

    grid.cellFrames = labelFrames;
    for (int step = 1; step <= labelFrames; ++step)
    {
        if (labelFrames % step != 0)
            continue;
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

QRect plusButtonRect(const QRect& clip, int visibleRight)
{
    const int left = std::max(0, clip.left());
    const int right = std::min(clip.right(), visibleRight);
    if (right - left < 84)
        return QRect();
    return QRect(right - 22, clip.top() + (clip.height() - 16) / 2, 16, 16);
}

bool hasAssetMime(const QMimeData* mime)
{
    return mime
           && (mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-template"))
               || mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset-html"))
               || mime->hasFormat(
                   QStringLiteral("application/x-pvforge-asset")));
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
            m_source->mapFrom(host,
                              QPoint(left + m_panel->nameWidth(), clip.top()))
                .x();
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
        // The bar is top-anchored; m_scrollY counts upwards from row 0 at the
        // body bottom.
        m_scrollY = std::max(0, m_vScroll->maximum() - value);
        update();
        syncPopups();
    });

    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(30);
    connect(m_autoScroll, &QTimer::timeout, this, [this] { autoScrollTick(); });

    m_displayFrames = Settings::timelineDisplayFrames();

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("timelineSearchEdit"));
    m_search->setPlaceholderText(QStringLiteral("搜索对象…"));
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(QStringLiteral("按名称筛选时间轴对象"));
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
        clampScroll();
        update();
        syncPopups();
    });

    m_unitButton = new QToolButton(this);
    m_unitButton->setObjectName(QStringLiteral("timelineUnitButton"));
    m_unitButton->setText(m_displayFrames ? QStringLiteral("帧")
                                          : QStringLiteral("秒"));
    m_unitButton->setToolTip(QStringLiteral("切换时间轴刻度（秒 / 帧）"));
    m_unitButton->setFixedSize(36, 20);
    m_unitButton->setFocusPolicy(Qt::NoFocus);
    m_unitButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: #2a2c33; color: #d8dce4;"
        " border: 1px solid #4a4d55; border-radius: 0px; padding: 0 2px; }"
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
        " border: 1px solid #4a4d55; border-radius: 0px; padding: 0 4px; }"
        "QToolButton:hover { background: #3c404a; }"));
    connect(m_modeButton, &QToolButton::clicked, this, [this] {
        setEditMode(m_editMode == EditMode::Clips ? EditMode::Keyframes
                                                  : EditMode::Clips);
    });

    // Arrow-key stepping mirrors the old timeline: a tap moves one frame and
    // a hold repeats at the project frame rate after a short delay.
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
        m_holdTimer->setInterval(std::max(1, 1000 / currentFps()));
        m_holdTimer->start();
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
        if (!rowMatchesFilter(entity))
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
    // Bottom-up: row 0 sits on the body bottom, larger rows stack upwards.
    return bodyBottom() - (row + 1) * kRowHeight + 1 + m_scrollY;
}

int TimelinePanel::nameWidth() const
{
    // The main timeline keeps the old 130 px name column; private timeline
    // popups are narrower so they still have room for content.
    return m_popup ? 96 : kNameWidth;
}

QString TimelinePanel::searchFilter() const
{
    return m_search ? m_search->text().trimmed() : QString();
}

bool TimelinePanel::rowMatchesFilter(const Entity& entity) const
{
    const QString filter = searchFilter();
    return filter.isEmpty() || entity.name.contains(filter, Qt::CaseInsensitive);
}

void TimelinePanel::toggleSessionFlag(EntityId id, bool hidden)
{
    if (!m_session || !m_session->editActions())
        return;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* source = sessionEntity(uuid);
    if (source == nullptr)
        return;
    const bool target = hidden ? !source->hidden : !source->locked;
    const QString text = hidden
                             ? (target ? QStringLiteral("隐藏片段")
                                       : QStringLiteral("显示片段"))
                             : (target ? QStringLiteral("锁定片段")
                                       : QStringLiteral("解锁片段"));
    QString error;
    if (!m_session->editActions()->updateEntity(
            uuid, text,
            [hidden, target](model::Entity& entity) {
                if (hidden)
                    entity.hidden = target;
                else
                    entity.locked = target;
                return true;
            },
            &error, true))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    if (!hidden && target)
    {
        m_selectedKeyframes.erase(
            std::remove_if(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                           [id](const KeyframeSelection& selection) {
                               return selection.entity == id;
                           }),
            m_selectedKeyframes.end());
        emit keyframeSelectionChanged(m_selectedKeyframes);
    }
    emit editCommitted(text);
    refreshFromSession();
}

void TimelinePanel::handleRowButton(int row, int index)
{
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || id == kInvalidEntity)
        return;

    if (index == 2)
    {
        // Select every keyframe of this row, like the old timeline button.
        m_selectedKeyframes.clear();
        if (!isEntityLocked(id))
        {
            for (int frame : keyframeFrames(*entity))
                m_selectedKeyframes.append(KeyframeSelection{id, double(frame), QString()});
        }
        selectEntity(id, true);
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();
        return;
    }

    if (sessionMode())
    {
        if (m_editMode == EditMode::Keyframes)
            m_dragKeyTrack.clear();
        toggleSessionFlag(id, index == 0);
        return;
    }

    if (!m_project)
        return;
    const EntityFlag flag =
        index == 0 ? EntityFlag::Hidden : EntityFlag::Locked;
    const bool oldValue = index == 0 ? entity->hidden : entity->locked;
    if (index == 1 && !oldValue)
    {
        m_selectedKeyframes.erase(
            std::remove_if(m_selectedKeyframes.begin(), m_selectedKeyframes.end(),
                           [id](const KeyframeSelection& selection) {
                               return selection.entity == id;
                           }),
            m_selectedKeyframes.end());
    }
    emit entityFlagChanged(id, flag, !oldValue);
    update();
}

int TimelinePanel::rowAt(int y) const
{
    if (y < kRulerHeight || y >= bodyBottom())
        return -1;
    const int row = (bodyBottom() - y + m_scrollY) / kRowHeight;
    return row >= 0 && row < rowIds().size() ? row : -1;
}

int TimelinePanel::dropRowForY(int y) const
{
    const int highest = highestRowForDrop();
    if (y < kRulerHeight)
        return highest + 1;
    if (y >= bodyBottom())
        return 0;
    const int row = std::max(0, (bodyBottom() - y + m_scrollY) / kRowHeight);
    // Empty space above the top clip is unbounded; land right above it.
    return std::min(row, highest + 1);
}

int TimelinePanel::highestRowForDrop() const
{
    // Clamp against the rows at gesture start, otherwise the preview clip
    // would raise its own ceiling every time it moves and run away.
    const QVector<Entity>& entities =
        sessionMode() && m_dragMode == DragMode::MoveClip
            ? m_sessionBaseSnapshot.entities
            : (!sessionMode() && m_dragging ? m_before : activeEntities());
    int highest = -1;
    for (const Entity& entity : entities)
    {
        const bool dangling = entity.parent != kInvalidEntity
                              && entityById(entity.parent) == nullptr;
        if (entity.parent != m_owner
            && !(m_owner == kInvalidEntity && dangling))
            continue;
        const int row = rowOfEntity(entity);
        if (row >= 0)
            highest = std::max(highest, row);
    }
    return highest;
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
    const int raw0 = xFromFrame(start);
    const int raw1 = xFromFrame(end);
    if (raw1 <= nameWidth())
        return QRect();
    const int x0 = std::max(nameWidth(), raw0);
    const int x1 = std::max(x0 + 1, raw1);
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
    return std::max(1, width() - nameWidth() - kScrollBarSize);
}

int TimelinePanel::bodyBottom() const
{
    return std::max(kRulerHeight, height() - kScrollBarSize);
}

int TimelinePanel::timelineLength() const
{
    if (m_project)
        return std::max(1, m_project->timelineLength());
    return std::max(1, contentEnd());
}

int TimelinePanel::xFromFrame(int frame) const
{
    return nameWidth()
           + int(std::lround((frame - contentStart()) * m_pixelsPerFrame
                             - m_scrollX));
}

int TimelinePanel::frameFromX(int x) const
{
    return std::max(0, int(std::lround(
                           contentStart()
                           + (x - nameWidth() + m_scrollX) / m_pixelsPerFrame)));
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
        // Top of the bar = topmost rows, bottom = row 0.
        m_vScroll->setValue(maxY - m_scrollY);
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
    const int left = nameWidth() + 24;
    const int right = width() - kScrollBarSize - 24;
    const int margin = 24;
    if (x < left)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - margin)));
    else if (x > right)
        m_scrollX = std::max(0, int(std::lround(
                                   (frame - contentStart()) * m_pixelsPerFrame
                                   - (right - left))));
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::setScrubFrame(double rawX)
{
    // The pointer can leave the widget while scrubbing. Clamp it to the
    // visible track so the playhead line can never disappear outside it.
    const int x = std::clamp(int(std::lround(rawX)), nameWidth() + 1,
                             width() - kScrollBarSize - 1);
    m_frame = std::max(0, frameFromX(x));
    emit frameRequested(m_frame);
    update();
}

void TimelinePanel::stepFrame(int delta)
{
    int next = m_frame + delta;
    if (next < 0)
    {
        next = 0;
        stopHold();
    }

    const int length = timelineLength();
    const bool loop = m_project ? m_project->loop : true;
    if (delta > 0 && length > 0 && next >= length)
    {
        if (loop)
        {
            next = 0;
        }
        else
        {
            next = length - 1;
            stopHold();
        }
    }

    m_frame = std::max(0, next);
    emit frameRequested(m_frame);
    update();
}

void TimelinePanel::stopHold()
{
    m_holdDirection = 0;
    if (m_holdTimer)
        m_holdTimer->stop();
    if (m_holdDelay)
        m_holdDelay->stop();
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
        && m_dragMode != DragMode::TrimEnd
        && m_dragMode != DragMode::MoveKeyframe)
        return;
    if (!m_session || !m_session->editActions() || !m_adapter)
        return;
    const Uuid uuid = uuidForEntityId(m_dragEntity);
    if (uuid.isEmpty())
        return;
    EditActions* actions = m_session->editActions();

    EditPlan plan;
    QString error;
    if (m_dragMode == DragMode::MoveKeyframe)
    {
        const int delta = frameFromX(int(std::lround(pos.x()))) - m_keyDragStart;
        const int movedFrame = std::max(0, m_keyDragStart + delta);
        if (movedFrame != m_frame)
        {
            m_frame = movedFrame;
            emit frameRequested(m_frame);
        }
        if (delta != 0)
        {
            if (!actions->planMoveKeyframes(selectedKeyRefs(), delta, &plan,
                                            &error))
            {
                m_snapshot = m_sessionBaseSnapshot;
                m_sessionPlanValid = false;
                m_moved = false;
                update();
                syncPopups();
                return;
            }
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
        return;
    }

    if (m_dragMode == DragMode::MoveClip)
    {
        int delta = frameFromX(int(std::lround(pos.x()))) - m_dragStartFrame;
        const int lower = actions->minMoveDelta({uuid});
        delta = std::max(delta, lower);
        const int targetRow = dropRowForY(int(std::lround(pos.y())));
        if (delta == 0 && targetRow == m_dragStartRow)
        {
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
        if (!actions->planMoveToRow(uuid, delta, targetRow, &plan, &error))
        {
            // A rejected plan (parent bounds, lock) restores the base preview;
            // the release handler reports the error once.
            m_snapshot = m_sessionBaseSnapshot;
            m_sessionPlanValid = false;
            m_moved = false;
            update();
            syncPopups();
            return;
        }
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
    const int raw0 = xFromFrame(start);
    const int raw1 = xFromFrame(end);
    if (raw1 <= nameWidth())
        return QRect();
    const int x0 = std::max(nameWidth(), raw0);
    const int x1 = std::max(x0 + 1, raw1);
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

bool TimelinePanel::addKeyframeAt(EntityId id, int frame, const QString& track)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return false;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return false;
    const int relative = frame - entity->start;
    QString error;
    if (!m_session->editActions()->addKeyframe(uuid, track, relative, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return false;
    }
    m_selectedTrack = track;
    m_selectedKeyframes.clear();
    m_selectedKeyframes.append(KeyframeSelection{id, double(frame), track});
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("设置关键帧 %1").arg(track));
    refreshFromSession();
    return true;
}

QStringList TimelinePanel::tracksAtFrame(EntityId id, int frame) const
{
    QStringList result;
    const Entity* entity = entityById(id);
    if (!entity)
        return result;
    for (auto track = entity->tracks.constBegin(); track != entity->tracks.constEnd();
         ++track)
    {
        for (const Keyframe& key : track->keys)
        {
            if (int(std::lround(key.frame)) == frame)
            {
                result.append(track.key());
                break;
            }
        }
    }
    return result;
}

void TimelinePanel::promptTrackAndAddKey(EntityId id, int frame,
                                         const QPoint& globalPos)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const Uuid uuid = uuidForEntityId(id);
    const QStringList tracks = m_session->editActions()->trackNames(uuid);
    if (tracks.isEmpty())
    {
        emit editFailed(QStringLiteral("这个片段没有可打关键帧的轨道"));
        return;
    }

    QMenu menu(this);
    menu.setTitle(QStringLiteral("选择轨道"));
    QHash<QAction*, QString> actions;
    for (const QString& track : tracks)
    {
        QAction* action = menu.addAction(track);
        actions.insert(action, track);
    }
    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    const QString track = actions.value(chosen);
    if (track.isEmpty())
        return;
    m_selectedTrack = track;
    addKeyframeAt(id, frame, track);
}

bool TimelinePanel::keyframeSelectedAt(EntityId id, int frame) const
{
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        if (selection.entity == id
            && int(std::lround(selection.frame)) == frame)
            return true;
    }
    return false;
}

QVector<KeyRef> TimelinePanel::selectedKeyRefs() const
{
    QVector<KeyRef> result;
    if (!sessionMode() || !m_session)
        return result;
    const model::Project& project = m_session->project();
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        const Uuid uuid = uuidForEntityId(selection.entity);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;
        KeyRef ref;
        ref.entity = uuid;
        ref.track = selection.track;
        ref.frame = int(std::lround(selection.frame)) - it.value().start;
        result.append(ref);
    }
    return result;
}

void TimelinePanel::deleteSelectedKeyframes()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const QVector<KeyRef> refs = selectedKeyRefs();
    if (refs.isEmpty())
        return;
    QString error;
    if (!m_session->editActions()->deleteKeyframes(refs, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_selectedKeyframes.clear();
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("删除关键帧"));
    refreshFromSession();
}

void TimelinePanel::copySelectedKeyframes()
{
    if (!sessionMode() || !m_session)
        return;
    const QVector<KeyRef> refs = selectedKeyRefs();
    if (refs.isEmpty())
        return;

    const model::Project& project = m_session->project();
    QVector<TrackKey> copied;
    QString entity;
    for (const KeyRef& ref : refs)
    {
        if (entity.isEmpty())
            entity = ref.entity;
        else if (entity != ref.entity)
        {
            emit editFailed(QStringLiteral("一次只能复制同一个实体的关键帧"));
            return;
        }
        const auto it = project.entities.constFind(ref.entity);
        if (it == project.entities.constEnd())
            continue;
        const model::Entity& source = it.value();
        auto collect = [&](const QString& trackName, const model::Track& track) {
            for (const model::Keyframe& key : track.keys)
            {
                if (key.frame != ref.frame)
                    continue;
                TrackKey item;
                item.track = trackName;
                item.key = key;
                copied.append(item);
            }
        };
        if (ref.track.isEmpty())
        {
            for (auto track = source.tracks.constBegin();
                 track != source.tracks.constEnd(); ++track)
                collect(track.key(), track.value());
        }
        else
        {
            const auto track = source.tracks.constFind(ref.track);
            if (track != source.tracks.constEnd())
                collect(ref.track, track.value());
        }
    }

    if (copied.isEmpty())
        return;
    m_keyClipboard = copied;
    m_keyClipboardEntity = entity;
    emit editCommitted(QStringLiteral("复制 %1 个关键帧").arg(copied.size()));
}

void TimelinePanel::pasteKeyframes()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    if (m_keyClipboard.isEmpty() || m_keyClipboardEntity.isEmpty())
        return;

    const Uuid target =
        m_selected != kInvalidEntity ? uuidForEntityId(m_selected)
                                     : m_keyClipboardEntity;
    if (target != m_keyClipboardEntity)
    {
        emit editFailed(QStringLiteral("关键帧只能粘贴回原实体"));
        return;
    }
    const auto entityIt = m_session->project().entities.constFind(target);
    if (entityIt == m_session->project().entities.constEnd())
        return;
    const model::Entity& entity = entityIt.value();
    const int duration = entity.duration();
    if (duration <= 0)
        return;

    int first = m_keyClipboard.first().key.frame;
    int last = first;
    for (const TrackKey& item : m_keyClipboard)
    {
        first = std::min(first, item.key.frame);
        last = std::max(last, item.key.frame);
    }
    const int span = last - first;
    if (span >= duration)
    {
        emit editFailed(QStringLiteral("关键帧片段超出实体时长，无法粘贴"));
        return;
    }

    const int relativePlayhead =
        std::clamp(m_session->playhead() - entity.start, 0, duration - 1);
    const int delta =
        std::clamp(relativePlayhead - first, -first, duration - 1 - last);

    QVector<TrackKey> placed;
    placed.reserve(m_keyClipboard.size());
    for (const TrackKey& item : m_keyClipboard)
    {
        TrackKey targetKey = item;
        targetKey.key.frame += delta;
        placed.append(targetKey);
    }

    QString error;
    if (!m_session->editActions()->pasteKeyframes(target, placed, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }

    m_selectedKeyframes.clear();
    QSet<QString> seen;
    const EntityId targetId =
        m_selected != kInvalidEntity ? m_selected : entityIdForUuid(target);
    for (const TrackKey& item : placed)
    {
        const QString key = QString::number(item.key.frame);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        m_selectedKeyframes.append(
            KeyframeSelection{targetId, double(entity.start + item.key.frame),
                              QString()});
    }
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("粘贴关键帧"));
    refreshFromSession();
}

bool TimelinePanel::splitAtPlayhead()
{
    if (!sessionMode() || m_selected == kInvalidEntity)
    {
        emit editFailed(QStringLiteral("没有选中的片段，无法切分"));
        return false;
    }
    return splitEntityAtPlayhead(m_selected);
}

bool TimelinePanel::splitEntityAtPlayhead(EntityId id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return false;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return false;
    const int cut = m_session->playhead();
    if (cut <= entity->start || cut >= entity->end)
    {
        emit editFailed(QStringLiteral("播放头必须位于片段内部才能切分（%1–%2，播放头 %3）")
                            .arg(entity->start)
                            .arg(entity->end)
                            .arg(cut));
        return false;
    }
    QString error;
    if (!m_session->editActions()->splitClip(uuid, cut, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return false;
    }
    emit editCommitted(QStringLiteral("在播放头切分片段"));
    refreshFromSession();
    return true;
}

void TimelinePanel::addMarkerAtPlayhead()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, m_session->playhead());
    marker.title = QStringLiteral("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");
    QString error;
    if (!m_session->editActions()->addMarker(marker, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("添加标记"));
    refreshFromSession();
}

QString TimelinePanel::markerAt(const QPoint& pos) const
{
    if (!sessionMode())
        return QString();
    for (int i = 0; i < m_snapshot.markers.size(); ++i)
    {
        if (markerRect(i).contains(pos))
            return m_snapshot.markers.at(i).id;
    }
    return QString();
}

void TimelinePanel::editMarker(const QString& id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const model::Project& project = m_session->project();
    for (const model::Marker& marker : project.markers)
    {
        if (marker.id != id)
            continue;
        next::MarkerDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("编辑标记"));
        dialog.setMarker(marker);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QString error;
        if (!m_session->editActions()->updateMarker(dialog.marker(), &error))
        {
            if (!error.isEmpty())
                emit editFailed(error);
            return;
        }
        emit editCommitted(QStringLiteral("编辑标记"));
        refreshFromSession();
        return;
    }
}

void TimelinePanel::deleteMarkerWithConfirm(const QString& id)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    QString title;
    for (const model::Marker& marker : m_session->project().markers)
        if (marker.id == id)
            title = marker.title;
    if (title.isEmpty())
        title = id;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除标记"),
        QStringLiteral("确定删除标记「%1」？").arg(title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!m_session->editActions()->deleteMarker(id, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("删除标记"));
    refreshFromSession();
}

void TimelinePanel::beginMarkerDrag(const QString& id, int x)
{
    m_dragMarkerId = id;
    m_dragMode = DragMode::MoveMarker;
    m_markerDragStartFrame = 0;
    for (const TimelineMarkerView& marker : m_snapshot.markers)
        if (marker.id == id)
            m_markerDragStartFrame = marker.frame;
    m_markerDragFrame = m_markerDragStartFrame;
    m_lastDragPos = QPointF(x, kRulerHeight - 8);
    setCursor(Qt::SizeHorCursor);
    update();
}

void TimelinePanel::updateMarkerDrag(int x)
{
    if (m_dragMode != DragMode::MoveMarker || m_dragMarkerId.isEmpty())
        return;
    const int frame = std::max(0, frameFromX(x));
    if (frame == m_markerDragFrame)
        return;
    m_markerDragFrame = frame;
    for (TimelineMarkerView& marker : m_snapshot.markers)
        if (marker.id == m_dragMarkerId)
            marker.frame = frame;
    update();
}

void TimelinePanel::finishMarkerDrag()
{
    const QString id = m_dragMarkerId;
    const int frame = m_markerDragFrame;
    const int start = m_markerDragStartFrame;
    m_dragMarkerId.clear();
    m_dragMode = DragMode::None;
    unsetCursor();
    if (!id.isEmpty() && frame != start && sessionMode() && m_session
        && m_session->editActions())
    {
        QString error;
        if (!m_session->editActions()->moveMarker(id, frame, &error))
        {
            if (!error.isEmpty())
                emit editFailed(error);
        }
        else
        {
            emit editCommitted(QStringLiteral("移动标记"));
        }
    }
    refreshFromSession();
}

void TimelinePanel::finishSessionDrag()
{
    if (m_autoScroll)
        m_autoScroll->stop();
    const DragMode mode = m_dragMode;
    const EntityId draggedId = m_dragEntity;
    const QPointF releasePos = m_lastDragPos;
    // Resolve the drop row before clearing the drag mode: the clamp uses the
    // rows captured when the gesture started.
    const int releaseRow = dropRowForY(int(std::lround(releasePos.y())));
    const QVector<Entity> baseEntities = m_sessionBaseSnapshot.entities;

    m_dragging = false;
    m_dragMode = DragMode::None;
    m_dragEntity = kInvalidEntity;
    m_dragKeyTrack.clear();

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
        const EntityId targetId = sessionBaseEntityAtRow(releaseRow);
        const Entity* target = findEntity(baseEntities, targetId);
        const Uuid targetUuid = uuidForEntityId(targetId);
        const bool targetHit =
            target && sessionBaseClipRect(*target).contains(releasePos.toPoint());
        const bool targetCanParent =
            target
            && (target->data.value(QStringLiteral("contentKind")).toString()
                    == QStringLiteral("empty")
                || !childIds(baseEntities, targetId).isEmpty());
        if (targetHit && targetCanParent && releaseRow != m_dragStartRow
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

        // Blank rows are first-class targets: the model row is written even
        // when no clip sits there.  Dropping on a clip swaps the two slots.
        if (releaseRow != m_dragStartRow || delta != 0)
        {
            EditPlan plan;
            QString error;
            if (actions->planMoveToRow(uuid, delta, releaseRow, &plan, &error))
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
    m_dragKeyTrack.clear();
    m_dragMarkerId.clear();
    unsetCursor();
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
    QHash<EntityId, QSet<int>> framesByEntity;
    for (const KeyframeSelection& selection : keys)
    {
        const Entity* entity = findEntity(entities, selection.entity);
        if (!entity)
            continue;
        const int frame = int(std::lround(selection.frame));
        framesByEntity[selection.entity].insert(frame);
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
                maxDelta = std::min(maxDelta, end - frame);
            }
        }
        if (!found)
            framesByEntity[selection.entity].remove(frame);
    }
    delta = std::clamp(delta, minDelta, maxDelta);
    if (delta == 0)
        return;

    // Move every selected frame simultaneously: remove the original keys
    // first, then insert copies.  Otherwise adjacent selected frames would
    // pick up keys that were just moved into their range.
    for (auto selection = framesByEntity.constBegin();
         selection != framesByEntity.constEnd(); ++selection)
    {
        Entity* entity = findEntity(entities, selection.key());
        if (!entity)
            continue;
        for (auto track = entity->tracks.begin(); track != entity->tracks.end();
             ++track)
        {
            QVector<Keyframe> moved;
            for (const Keyframe& key : track->keys)
            {
                if (selection.value().contains(int(std::lround(key.frame))))
                    moved.append(key);
            }
            if (moved.isEmpty())
                continue;
            auto& trackKeys = track->keys;
            trackKeys.erase(
                std::remove_if(trackKeys.begin(), trackKeys.end(),
                               [&selection](const Keyframe& key) {
                                   return selection.value().contains(
                                       int(std::lround(key.frame)));
                               }),
                trackKeys.end());
            for (Keyframe key : moved)
            {
                key.frame += delta;
                trackKeys.append(key);
            }
            std::sort(trackKeys.begin(), trackKeys.end(),
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
        const int targetRow = dropRowForY(int(std::lround(pos.y())));
        moveEntityToRow(m_preview, m_dragEntity, targetRow);
        m_moved = delta != 0 || targetRow != m_dragStartRow;
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
        m_frame = std::max(0, m_keyDragStart + delta);
        emit frameRequested(m_frame);
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
        const int contentRight = width() - kScrollBarSize;
        int dx = 0;
        if (rawX > contentRight - 12)
            dx = 12;
        else if (rawX < nameWidth() + 12)
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
    const int contentRight = width() - kScrollBarSize;
    int dx = 0;
    int dy = 0;
    if (m_lastDragPos.x() > contentRight - 24)
        dx = 12;
    else if (m_lastDragPos.x() < nameWidth() + 24)
        dx = -12;
    if (m_lastDragPos.y() > bodyBottom() - 20)
        dy = -12;   // reveal lower rows: the bottom-anchored offset shrinks
    else if (m_lastDragPos.y() < kRulerHeight + 20)
        dy = 12;    // reveal higher rows
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
    p.fillRect(rect(), base);
    p.fillRect(QRect(0, 0, nameWidth(), height()), base);
    p.fillRect(QRect(0, 0, width(), kRulerHeight), alt);

    p.setPen(mid);
    p.drawLine(nameWidth(), 0, nameWidth(), bottom);
    p.drawLine(0, kRulerHeight, width(), kRulerHeight);

    const int fps = currentFps();
    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    auto drawTick = [&](int frame, int tickHeight, const QColor& color) {
        const int x = xFromFrame(frame);
        if (x < nameWidth() || x > right)
            return;
        p.setPen(QPen(color, 1));
        p.drawLine(x, kRulerHeight - tickHeight, x, kRulerHeight);
    };
    auto drawLabel = [&](int frame, const QString& label, const QColor& color) {
        const int x = xFromFrame(frame);
        if (x < nameWidth() - 24 || x > right + 24)
            return;
        // Keep the header mode button from covering a ruler label.
        if (m_modeButton && m_modeButton->isVisible() && x > right - 62)
            return;
        p.setPen(color);
        // Labels sit centred on their tick, not offset to the right.
        p.drawText(QRect(x - 48, 1, 96, kRulerHeight - 5),
                   Qt::AlignHCenter | Qt::AlignVCenter, label);
    };

    const int leftFrame = frameFromX(nameWidth());
    if (m_displayFrames)
    {
        int tickStep = 0;
        int labelStep = 0;
        rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
        const int lastFrame = frameFromX(right) + tickStep;
        const int firstTick =
            ((leftFrame + tickStep - 1) / tickStep) * tickStep;

        for (int frame = firstTick; frame <= lastFrame; frame += tickStep)
        {
            const bool major = frame % labelStep == 0;
            drawTick(frame, major ? 9 : 4, major ? text : mid);
        }
        const int firstLabel =
            ((leftFrame + labelStep - 1) / labelStep) * labelStep;
        for (int frame = firstLabel; frame <= lastFrame; frame += labelStep)
            drawLabel(frame, QString::number(frame), text);
    }
    else
    {
        const SecondsGrid grid = secondsGrid(fps, m_pixelsPerFrame);
        const int lastFrame = frameFromX(right) + grid.cellFrames;
        const int firstCell =
            ((leftFrame + grid.cellFrames - 1) / grid.cellFrames)
            * grid.cellFrames;

        for (int frame = firstCell; frame <= lastFrame; frame += grid.cellFrames)
            drawTick(frame, 4, mid);

        if (fps * m_pixelsPerFrame >= 8.0)
        {
            const int firstSecond = ((leftFrame + fps - 1) / fps) * fps;
            for (int frame = firstSecond; frame <= lastFrame; frame += fps)
            {
                if (frame % grid.labelFrames != 0)
                    drawTick(frame, 7, mid);
            }
        }

        const int firstLabel =
            ((leftFrame + grid.labelFrames - 1) / grid.labelFrames)
            * grid.labelFrames;
        for (int frame = firstLabel; frame <= lastFrame;
             frame += grid.labelFrames)
        {
            drawTick(frame, 10, text);
            const double seconds = frame / double(fps);
            const bool whole = std::abs(seconds - std::round(seconds)) < 1e-9;
            const QString label =
                (whole ? QString::number(int(std::round(seconds)))
                       : QString::number(seconds, 'g', 3))
                + QStringLiteral("s");
            drawLabel(frame, label, text);
        }
    }

    if (sessionMode())
    {
        for (int i = 0; i < m_snapshot.markers.size(); ++i)
        {
            const QRect marker = markerRect(i);
            if (marker.isEmpty() || marker.left() < nameWidth())
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
            p.drawRect(QRectF(marker).adjusted(0.5, 0.5, -0.5, -0.5));
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
        const bool rowSelected =
            entity
            && (entity->id == m_selected
                || m_entitySelection.contains(entity->id));
        if (rowSelected)
            p.fillRect(rowRect, QColor(accent.red(), accent.green(),
                                       accent.blue(), 60));
        else if (row % 2)
            p.fillRect(rowRect, QColor(alt.red(), alt.green(), alt.blue(), 90));
        p.setPen(QColor(mid.red(), mid.green(), mid.blue(), 70));
        p.drawLine(0, top + kRowHeight - 1, right, top + kRowHeight - 1);
        if (!entity)
            continue;

        // Name column: entity name plus the old eye / lock / select-keys
        // buttons.  Names never live inside the clip any more.
        const int depth = depthOf(entity->id);
        const int nameX = 8 + std::min(depth, 4) * 12;
        const int buttonLeft =
            nameWidth() - 6 - 3 * (kButtonSize + kButtonGap);
        const bool hidden = sessionMode()
                                ? entity->hidden
                                : m_project->isEffectivelyHidden(entity->id);
        const bool locked = sessionMode()
                                ? entity->locked
                                : m_project->isEffectivelyLocked(entity->id);
        const QColor nameColor =
            (hidden || locked) ? dim : text;
        p.setPen(nameColor);
        const QFontMetrics nameMetrics(p.font());
        const QString shownName =
            nameMetrics.elidedText(entity->name, Qt::ElideRight,
                                   std::max(8, buttonLeft - 4 - nameX));
        p.drawText(QRect(nameX, top, std::max(8, buttonLeft - 4 - nameX),
                         kRowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, shownName);

        makeEyeIcon(hidden).paint(&p, rowButtonRect(row, 0));
        makeLockIcon(locked).paint(&p, rowButtonRect(row, 1));
        makeKeyframeIcon().paint(&p, rowButtonRect(row, 2));

        p.setPen(QColor(mid.red(), mid.green(), mid.blue(), 90));
        p.drawLine(nameWidth() - 1, top, nameWidth() - 1, top + kRowHeight);

        const QRect clip = clipRect(*entity);
        if (clip.isEmpty())
            continue;
        QColor color = clipColor(entity->type);
        const bool selected = rowSelected;
        const int alpha = hidden ? 80 : (selected ? 230 : 200);
        QColor fill = color;
        fill.setAlpha(alpha);
        p.setPen(QPen(color.darker(170), 1));
        p.setBrush(fill);
        // Flat, square corners: no gradient, rounding or white highlight.
        p.drawRect(clip.adjusted(0, 0, -1, -1));
        if (selected)
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(accent, 2));
            p.drawRect(clip.adjusted(1, 1, -2, -2));
        }
        if (sessionMode() && isEntityLocked(entity->id))
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 150), 1, Qt::DashLine));
            p.drawRect(clip.adjusted(2, 2, -3, -3));
        }

        if (entity->isAudio())
            drawAudioClip(p, *entity, clip);

        const QRect plus = plusButtonRect(clip, right);
        if (!plus.isEmpty())
        {
            p.setPen(QPen(QColor(255, 255, 255, 125), 1));
            p.setBrush(QColor(0, 0, 0, 85));
            p.drawRect(plus);
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

        // Clip body carries time/duration only; the entity name is in the
        // name column.  This keeps short clips readable.
        const int infoRight = plus.isEmpty() ? clip.right() - 6 : plus.left() - 6;
        const QFontMetrics metrics(p.font());
        const int startFrame = clipStartOf(*entity);
        const int endFrame = clipEndOf(*entity);
        QString rangeInfo =
            QStringLiteral("%1–%2 · %3f")
                .arg(startFrame)
                .arg(endFrame)
                .arg(endFrame - startFrame);
        if (entity->isAudio())
        {
            const double seconds =
                entity->data.value(QStringLiteral("assetDurationSeconds"))
                    .toDouble();
            if (seconds > 0.0)
                rangeInfo += QStringLiteral(" · %1s").arg(seconds, 0, 'f', 2);
        }
        if (infoRight > clip.left() + 10)
        {
            const QString shown =
                metrics.elidedText(rangeInfo, Qt::ElideRight,
                                   infoRight - clip.left() - 8);
            p.setPen(QColor(255, 255, 255, 210));
            p.drawText(QRect(clip.left() + 5, clip.top(),
                             std::max(1, infoRight - clip.left() - 6),
                             clip.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, shown);
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
            p.drawText(QRect(clip.left() + 5, clip.bottom() - 13, 40, 11),
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
                keyframeSelectedAt(entity->id, keyFrame);
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
        // activeEntities() is the live preview in both modes, so the marker
        // tracks the row under the pointer while dragging.
        const Entity* dragged = findEntity(activeEntities(), m_dragEntity);
        const QVector<EntityId> ids = rowIds();
        const int row = dragged ? ids.indexOf(dragged->id) : -1;
        if (row >= 0)
        {
            const int y = rowTop(row);
            p.setPen(QPen(accent.lighter(130), 2));
            p.drawLine(nameWidth(), y, right, y);
        }
    }

    const bool noEntities =
        std::none_of(rows.begin(), rows.end(),
                     [](EntityId id) { return id != kInvalidEntity; });
    if (noEntities)
    {
        p.setPen(dim);
        const QString emptyText =
            !searchFilter().isEmpty()
                ? QStringLiteral("没有匹配的对象")
                : (m_owner < 0
                       ? QStringLiteral("把素材拖到这里创建 clip")
                       : QStringLiteral("把素材拖到这里创建子 clip"));
        p.drawText(QRect(nameWidth(), kRulerHeight, right - nameWidth(),
                         bottom - kRulerHeight),
                   Qt::AlignCenter, emptyText);
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd)
                               .normalized()
                               .intersected(QRectF(nameWidth(), kRulerHeight,
                                                   right - nameWidth(),
                                                   bottom - kRulerHeight));
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 50));
        p.drawRect(box);
    }

    if (m_dropActive)
    {
        const int dropX = xFromFrame(m_dropFrame);
        if (dropX >= nameWidth() && dropX <= right)
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
    if (playheadInOwner && playhead >= nameWidth() && playhead <= right)
    {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(playhead, 0, playhead, bottom);
        QPolygonF marker;
        marker << QPointF(playhead - 5, 0) << QPointF(playhead + 5, 0)
               << QPointF(playhead, 8);
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
        if (pos.x() < nameWidth())
            return;
        if (sessionMode())
        {
            const QString markerId = markerAt(pos.toPoint());
            if (!markerId.isEmpty())
            {
                beginMarkerDrag(markerId, int(std::lround(pos.x())));
                return;
            }
        }
        m_dragMode = DragMode::Scrub;
        m_lastDragPos = pos;
        m_autoScroll->start(30);
        emit scrubStarted();
        setScrubFrame(pos.x());
        return;
    }

    const int row = rowAt(int(pos.y()));
    const EntityId id = entityAtRow(row);

    if (pos.x() < nameWidth())
    {
        for (int index = 0; index < 3; ++index)
        {
            if (rowButtonRect(row, index).contains(pos.toPoint()))
            {
                handleRowButton(row, index);
                return;
            }
        }
        const Entity* rowEntity = entityById(id);
        if (rowEntity && !isEntityLocked(id))
        {
            m_selectedKeyframes.clear();
            emit keyframeSelectionChanged(m_selectedKeyframes);
            const bool additive =
                event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            if (additive)
            {
                if (m_entitySelection.contains(id))
                    m_entitySelection.remove(id);
                else
                    m_entitySelection.insert(id);
                m_selected = id;
                emit entitySelected(id);
                update();
            }
            else if (!m_entitySelection.contains(id)
                     || m_entitySelection.size() <= 1)
            {
                selectEntity(id, true);
            }
        }
        update();
        return;
    }

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

        if (!isEntityLocked(id))
        {
            const int keyFrame = hitKeyframeRow(row, x, int(std::lround(pos.y())));
            if (keyFrame >= 0)
            {
                selectEntity(id, true);
                const QStringList frameTracks = tracksAtFrame(id, keyFrame);
                const bool singleTrack =
                    (event->modifiers() & Qt::AltModifier) != 0
                    && !frameTracks.isEmpty();
                QString track;
                if (singleTrack)
                {
                    track = frameTracks.contains(m_selectedTrack)
                                ? m_selectedTrack
                                : frameTracks.first();
                }
                KeyframeSelection selection{id, double(keyFrame), track};
                const bool toggle =
                    event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
                auto it = std::find(m_selectedKeyframes.begin(),
                                    m_selectedKeyframes.end(), selection);
                if (toggle)
                {
                    if (it != m_selectedKeyframes.end())
                        m_selectedKeyframes.erase(it);
                    else
                        m_selectedKeyframes.append(selection);
                }
                else if (it == m_selectedKeyframes.end())
                {
                    m_selectedKeyframes.clear();
                    m_selectedKeyframes.append(selection);
                }
                if (!track.isEmpty())
                    m_selectedTrack = track;
                else if (!frameTracks.isEmpty())
                    m_selectedTrack = frameTracks.first();

                m_dragMode = DragMode::MoveKeyframe;
                m_dragKeyTrack = track;
                m_keyDragStart = keyFrame;
                m_dragStartFrame = keyFrame;
                m_frame = std::max(0, keyFrame);
                emit frameRequested(m_frame);
                emit keyframeSelectionChanged(m_selectedKeyframes);
                beginSessionDrag();
                update();
                return;
            }
        }

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

    // Keyframes win over trim grips in both edit modes: a key at the clip
    // edge must still be selectable, and clicking a diamond alone selects it
    // without requiring the keyframe mode.
    const int keyFrame = isEntityLocked(id)
                             ? -1
                             : hitKeyframeRow(row, x,
                                              int(std::lround(pos.y())));
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        KeyframeSelection selection{id, double(keyFrame)};
        const bool toggle = event->modifiers() & (Qt::ControlModifier
                                                  | Qt::ShiftModifier);
        auto it = std::find(m_selectedKeyframes.begin(),
                            m_selectedKeyframes.end(), selection);
        if (toggle)
        {
            if (it != m_selectedKeyframes.end())
                m_selectedKeyframes.erase(it);
            else
                m_selectedKeyframes.append(selection);
        }
        else if (it == m_selectedKeyframes.end())
        {
            m_selectedKeyframes.clear();
            m_selectedKeyframes.append(selection);
        }
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
    if (m_dragMode == DragMode::MoveMarker)
    {
        updateMarkerDrag(int(std::lround(event->position().x())));
        return;
    }
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
        if (sessionMode() && event->position().y() < kRulerHeight)
        {
            setCursor(markerAt(event->position().toPoint()).isEmpty()
                          ? Qt::ArrowCursor
                          : Qt::SizeHorCursor);
            return;
        }
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
                else if (!entity->locked
                         && hitKeyframeRow(row, x,
                                           int(std::lround(event->position().y())))
                                >= 0)
                    setCursor(Qt::PointingHandCursor);
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
            if (hitKeyframeRow(row, x,
                               int(std::lround(event->position().y())))
                >= 0)
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
    if (m_dragMode == DragMode::MoveMarker)
    {
        finishMarkerDrag();
        return;
    }
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
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        const QVector<EntityId> rows = rowIds();
        // The old timeline's rubber band always picked keyframes, in every
        // edit mode.  Keep that muscle memory instead of making clip mode
        // change what a drag means.
        const int topRow = rowAt(int(box.top()));
        const int bottomRow = rowAt(int(box.bottom()));
        const int firstRow = bottomRow >= 0 ? bottomRow : 0;
        const int lastRow = topRow >= 0 ? topRow : int(rows.size()) - 1;
        const int firstFrame = std::max(0, frameFromX(int(box.left())));
        const int lastFrame = std::max(0, frameFromX(int(box.right())));
        const bool additive = event->modifiers() & Qt::ShiftModifier;
        if (!additive)
            m_selectedKeyframes.clear();
        for (int row = std::max(0, firstRow);
             row <= std::min(lastRow, int(rows.size()) - 1); ++row)
        {
            const Entity* entity = entityById(rows[row]);
            if (!entity || isEntityLocked(entity->id))
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
        // The lowest selected row becomes the active entity.
        if (firstRow >= 0 && firstRow < rows.size())
        {
            const Entity* last = entityById(rows[firstRow]);
            if (last && !isEntityLocked(last->id))
                selectEntity(last->id, true);
        }
        emit keyframeSelectionChanged(m_selectedKeyframes);
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
    if ((!m_project && !sessionMode()) || event->button() != Qt::LeftButton)
        return;
    if (event->position().y() < kRulerHeight)
        return;
    if (event->position().x() < nameWidth())
        return;
    const int row = rowAt(int(event->position().y()));
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity || isEntityLocked(id))
        return;

    const int x = int(std::lround(event->position().x()));
    const int keyFrame =
        hitKeyframeRow(row, x, int(std::lround(event->position().y())));
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(KeyframeSelection{id, double(keyFrame), QString()});
        const QStringList frameTracks = tracksAtFrame(id, keyFrame);
        if (!frameTracks.isEmpty())
            m_selectedTrack = frameTracks.first();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();
        // Easing lives in the Properties keyframe section, not on the clip.
        return;
    }

    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->position().toPoint()))
        return;
    const QRect plus = plusButtonRect(clip, width() - kScrollBarSize);
    if (!plus.isEmpty() && plus.contains(event->position().toPoint()))
        return;

    const int frame = std::max(0, frameFromX(x));
    m_frame = frame;
    emit frameRequested(m_frame);
    selectEntity(id, true);

    if (sessionMode())
    {
        const Uuid uuid = uuidForEntityId(id);
        const QStringList tracks =
            (m_session && m_session->editActions())
                ? m_session->editActions()->trackNames(uuid)
                : QStringList();
        if (m_selectedTrack.isEmpty() || !tracks.contains(m_selectedTrack))
            promptTrackAndAddKey(id, frame, event->globalPosition().toPoint());
        else
            addKeyframeAt(id, frame, m_selectedTrack);
        update();
        return;
    }

    emit keyframeAddRequested(id, frame);
    m_selectedKeyframes.clear();
    m_selectedKeyframes.append(KeyframeSelection{id, double(frame)});
    emit keyframeSelectionChanged(m_selectedKeyframes);
    update();
}

void TimelinePanel::dragEnterEvent(QDragEnterEvent* event)
{
    if ((!m_project && !sessionMode()) || !hasAssetMime(event->mimeData()))
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
    if ((!m_project && !sessionMode()) || !hasAssetMime(event->mimeData()))
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
    if ((!m_project && !sessionMode()) || !hasAssetMime(event->mimeData()))
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
        row = dropRowForY(int(std::lround(event->position().y())));
    const EntityId id = dropAsset(*event->mimeData(), frame, row);
    m_dropActive = false;
    update();
    if (id == kInvalidEntity)
        event->ignore();
    else
        event->acceptProposedAction();
}

EntityId TimelinePanel::dropSessionAsset(const QMimeData& mime, int frame, int row)
{
    if (!m_session || !m_session->document() || !hasAssetMime(&mime))
        return kInvalidEntity;

    const QString hash =
        QString::fromUtf8(mime.data(QStringLiteral("application/x-pvforge-asset")))
            .trimmed();
    if (hash.isEmpty())
        return kInvalidEntity;

    frame = std::max(contentStart(), frame);
    if (m_owner >= 0)
        frame = std::min(frame, contentEnd() - 1);

    const Uuid parent = m_owner >= 0 ? uuidForEntityId(m_owner) : Uuid();
    QString error;
    const QVector<Uuid> created = m_session->editActions()->addAssetEntities(
        hash, frame, row, parent, &error);
    if (created.isEmpty())
    {
        emit editFailed(error.isEmpty() ? QStringLiteral("无法添加素材") : error);
        return kInvalidEntity;
    }

    m_session->document()->setSelection(created.first());
    const auto asset = m_session->assets().get(hash);
    const QString name = asset && !asset->name.isEmpty() ? asset->name : hash;
    emit editCommitted(QStringLiteral("已添加素材 \"%1\"").arg(name));
    refreshFromSession();
    return entityIdForUuid(created.first());
}

EntityId TimelinePanel::dropAsset(const QMimeData& mime, int frame, int row)
{
    if (sessionMode())
        return dropSessionAsset(mime, frame, row);
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

    if (sessionMode() && event->pos().y() < kRulerHeight)
    {
        const QString markerId = markerAt(event->pos());
        QMenu menu(this);
        QAction* add = menu.addAction(QStringLiteral("在播放头添加标记"));
        QAction* edit = nullptr;
        QAction* remove = nullptr;
        if (!markerId.isEmpty())
        {
            menu.addSeparator();
            edit = menu.addAction(QStringLiteral("编辑标记…"));
            remove = menu.addAction(QStringLiteral("删除标记"));
        }
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == add)
            addMarkerAtPlayhead();
        else if (edit && chosen == edit)
            editMarker(markerId);
        else if (remove && chosen == remove)
            deleteMarkerWithConfirm(markerId);
        event->accept();
        return;
    }

    const int row = rowAt(event->pos().y());
    const EntityId id = entityAtRow(row);
    const Entity* entity = entityById(id);
    if (!entity)
        return;

    const int hitX = event->pos().x();
    const int keyFrame = hitKeyframeRow(row, hitX, event->pos().y());
    if (keyFrame >= 0)
    {
        selectEntity(id, true);
        m_selectedKeyframes.clear();
        m_selectedKeyframes.append(KeyframeSelection{id, double(keyFrame), QString()});
        const QStringList frameTracks = tracksAtFrame(id, keyFrame);
        if (!frameTracks.isEmpty())
            m_selectedTrack = frameTracks.first();
        emit keyframeSelectionChanged(m_selectedKeyframes);
        update();

        // Easing is edited in the Properties keyframe section; the clip menu
        // only manages keys.
        QMenu menu(this);
        QAction* copy = menu.addAction(QStringLiteral("复制关键帧"));
        QAction* remove = menu.addAction(QStringLiteral("删除关键帧"));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == copy && sessionMode())
            copySelectedKeyframes();
        else if (chosen == remove && sessionMode())
            deleteSelectedKeyframes();
        event->accept();
        return;
    }

    const QRect clip = clipRect(*entity);
    if (!clip.contains(event->pos()))
        return;

    selectEntity(id, true);
    QMenu menu(this);
    QAction* open = menu.addAction(QStringLiteral("打开私有时间线"));
    QAction* remove = nullptr;
    QAction* split = nullptr;
    QHash<QAction*, QPair<Uuid, bool>> reparents;
    if (sessionMode())
    {
        remove = menu.addAction(QStringLiteral("删除片段"));
        remove->setEnabled(!isEntityLocked(id));
        split = menu.addAction(QStringLiteral("在播放头切分"));
        const int playhead = m_session ? m_session->playhead() : 0;
        const model::Entity* splitEntity = sessionEntity(uuidForEntityId(id));
        split->setEnabled(splitEntity && playhead > splitEntity->start
                          && playhead < splitEntity->end
                          && !isEntityLocked(id));

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
    else if (split && chosen == split)
    {
        splitEntityAtPlayhead(id);
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
        double next = std::clamp(old * std::pow(2.0, steps), 0.25, 32.0);
        if (next > 1.0)
            next = std::round(next);
        if (std::abs(next - old) < 1e-6)
            return;
        const int anchor = frameFromX(int(std::lround(event->position().x())));
        m_pixelsPerFrame = next;
        m_scrollX = std::max(
            0, int(std::lround(nameWidth()
                               + (anchor - contentStart()) * next
                               - event->position().x())));
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
        m_scrollX -= int(std::lround(steps * 120.0));
    }
    else
    {
        // Bottom-up: wheel up reveals higher rows, so the upward offset grows.
        m_scrollY += int(std::lround(steps * kRowHeight));
    }
    clampScroll();
    update();
    syncPopups();
    event->accept();
}

void TimelinePanel::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers plain =
        event->modifiers() & ~Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && plain == Qt::NoModifier)
    {
        const int direction = event->key() == Qt::Key_Right ? 1 : -1;
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
    if (sessionMode())
    {
        if (event->key() == Qt::Key_Escape && m_dragMode != DragMode::None)
        {
            cancelSessionDrag();
            return;
        }
        if (event->key() == Qt::Key_Delete
            || event->key() == Qt::Key_Backspace)
        {
            if (!m_selectedKeyframes.isEmpty())
                deleteSelectedKeyframes();
            else if (m_selected != kInvalidEntity)
                deleteSelectedEntities();
            return;
        }
        if (plain == Qt::ControlModifier && event->key() == Qt::Key_C)
        {
            copySelectedKeyframes();
            return;
        }
        if (plain == Qt::ControlModifier && event->key() == Qt::Key_V)
        {
            pasteKeyframes();
            return;
        }
        if (plain == Qt::NoModifier && event->key() == Qt::Key_S)
        {
            splitAtPlayhead();
            return;
        }
        if (plain == Qt::NoModifier && event->key() == Qt::Key_M)
        {
            addMarkerAtPlayhead();
            return;
        }
    }

    if (event->key() == Qt::Key_Delete)
    {
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

void TimelinePanel::keyReleaseEvent(QKeyEvent* event)
{
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

void TimelinePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
        m_hScroll->setGeometry(nameWidth(), height() - kScrollBarSize,
                               visibleWidth(), kScrollBarSize);
    if (m_vScroll)
        m_vScroll->setGeometry(width() - kScrollBarSize, kRulerHeight,
                               kScrollBarSize,
                               std::max(0, height() - kRulerHeight
                                               - kScrollBarSize));
    if (m_search)
    {
        const int unitSpace = 36 + 6;
        const int searchWidth =
            std::max(24, nameWidth() - 8 - unitSpace - 6);
        m_search->setGeometry(8, 3, searchWidth, kRulerHeight - 7);
        m_search->setVisible(nameWidth() >= 110 && width() > 220);
    }
    if (m_unitButton)
    {
        m_unitButton->setGeometry(nameWidth() - 8 - 36, 3, 36,
                                  kRulerHeight - 7);
        m_unitButton->setVisible(m_owner < 0 && width() > 200);
    }
    if (m_modeButton)
    {
        m_modeButton->setGeometry(std::max(nameWidth() + 4,
                                           width() - kScrollBarSize - 58),
                                  3, 54, kRulerHeight - 7);
        m_modeButton->setVisible(m_owner < 0 && width() > 280);
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
    m_pixelsPerFrame = std::clamp(pixels, 0.25, 32.0);
    clampScroll();
    update();
    syncPopups();
}

void TimelinePanel::setSelectedTrack(const QString& track)
{
    m_selectedTrack = track;
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
    return secondsGrid(currentFps(), m_pixelsPerFrame).cellFrames;
}

int TimelinePanel::secondsLabelStepForTest() const
{
    return secondsGrid(currentFps(), m_pixelsPerFrame).labelFrames;
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
    if (x < nameWidth() || x > right)
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
    if (index < 0 || index > 2)
        return QRect();
    const int top = rowTop(row);
    const int x = nameWidth() - 6 - (index + 1) * (kButtonSize + kButtonGap);
    return QRect(x, top + (kRowHeight - kButtonSize) / 2, kButtonSize,
                 kButtonSize);
}

bool TimelinePanel::verticalScrollVisibleForTest() const
{
    return m_vScroll && m_vScroll->isVisible();
}

int TimelinePanel::hitKeyframeRow(int row, int x, int y) const
{
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    // A keyframe hit must land on the diamond band, not only near its X.
    // This stops right-clicking a clip body from opening keyframe actions.
    const QRect clip = clipRect(*entity);
    if (clip.isEmpty() || std::abs(y - clip.center().y()) > 9)
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
