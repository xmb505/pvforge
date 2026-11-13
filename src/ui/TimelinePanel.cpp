#include "TimelinePanel.h"

#include "ui/TimelinePanelPopups.h"

#include "app/EditorSession.h"
#include "app/TimelineAdapter.h"
#include "core/Settings.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"

#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pvf {

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
    {
        setReadOnlySnapshot(m_adapter->snapshot());
        rebuildSolverStopCache();
    }
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
        return ui::detail::findEntity(m_snapshot.entities, id);
    return nullptr;
}

int TimelinePanel::currentFps() const
{
    return std::max(1, m_snapshot.fps);
}

bool TimelinePanel::isEntityLocked(EntityId id) const
{
    if (sessionMode())
    {
        const Entity* entity = entityById(id);
        return entity && entity->locked;
    }
    return false;
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
    rebuildSolverStopCache();
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
        {
            popup->panel()->setReadOnlySnapshot(m_snapshot);
            popup->panel()->rebuildSolverStopCache();
        }
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

} // namespace pvf
