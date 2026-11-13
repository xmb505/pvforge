#include "MainWindow.h"

#include "EditorSession.h"
#include "SettingsDialog.h"
#include "core/CheckpointManager.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/history/HistoryPanel.h"
#include "next/settings/SettingsStore.h"
#include "mcp2/HttpServer.h"
#include "model/Project.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CameraMonitor.h"
#include "ui/CollapsibleSection.h"
#include "ui/EnvironmentPanel.h"
#include "ui/McpLogPanel.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

MainWindow::MainWindow(EditorSession* session, QWidget* parent)
    : QMainWindow(parent)
    , m_session(session)
    , m_sessionOwner(session)
{
    buildSessionShell();
}

void MainWindow::buildSessionShell()
{
    resize(1440, 900);
    setMinimumSize(1000, 620);

    // Top split keeps the legacy shell's arrangement: large central surface on
    // the left, project properties on the right.  The asset bin and timeline
    // sit below the editing surface (P5/P2).
    auto* top = new QSplitter(Qt::Horizontal, this);
    top->setObjectName(QStringLiteral("sessionTopSplit"));
    top->setChildrenCollapsible(false);

    // P3a: the old viewport widget is back, drawing the new entity model
    // through render2.  The P1 read-only placeholder page is gone.
    m_viewport = new Viewport(m_session, top);
    m_viewport->setObjectName(QStringLiteral("viewport"));
    m_viewport->setMinimumWidth(480);

    // P4b: the legacy workbench button keeps its place; session mode only
    // exposes the special page (brand wall creation through the new model).
    if (m_session)
    {
        m_workbench = new WorkbenchPanel(m_session, m_viewport);
        m_workbench->setIgnoreMouseWidget(m_viewport->workbenchButton());
        m_workbench->hide();
        connect(m_viewport, &Viewport::workbenchRequested, this,
                &MainWindow::toggleWorkbench);
        connect(m_workbench, &WorkbenchPanel::sessionCreated, this,
                [this](const Uuid& uuid) {
                    if (m_session && m_session->document())
                        m_session->document()->setSelection(uuid);
                    if (m_library)
                        m_library->enqueueMissingVideoExtractions();
                });
    }

    // P3b: the legacy floating monitor, rendering through render2 and the
    // per-project media services.  It starts docked in the viewport corner.
    if (m_session)
    {
        m_monitor = new CameraMonitor(m_session, m_viewport, m_viewport);
        m_monitor->setFrame(m_session->playhead());
        m_monitor->setVisible(Settings::monitorVisible());
    }

    // P6: the old floating MCP log keeps its place beside the camera monitor.
    // It is a child of the viewport (drag/detach like the monitor), never a
    // dock, so the legacy arrangeMcpLog() geometry still applies.
    if (m_session)
    {
        m_mcpLog = new McpLogPanel(m_viewport);
        m_mcpLog->setLoggingEnabled(Settings::mcpLogVisible());
        m_mcpLog->setServerState(false, 0);
        m_mcpLog->hide();
        connect(m_mcpLog, &McpLogPanel::visibilityChanged, this,
                [this](bool visible) {
                    if (m_mcpLogAction)
                    {
                        const QSignalBlocker blocker(m_mcpLogAction);
                        m_mcpLogAction->setChecked(visible);
                    }
                    if (visible)
                        arrangeMcpLog();
                });
        connect(m_mcpLog, &McpLogPanel::entityUuidActivated, this,
                [this](const QString& uuid) {
                    if (m_session && m_session->document())
                        m_session->document()->setSelection(uuid);
                });
        connect(m_mcpLog, &McpLogPanel::frameRequested, this,
                [this](int frame) { sessionGoToFrame(frame); });
        connect(m_mcpLog, &McpLogPanel::stopAiRequested, this, [this] {
            if (m_session && m_session->mcpServer())
                m_session->mcpServer()->stopAi();
        });
        connect(m_mcpLog, &McpLogPanel::resumeAiRequested, this, [this] {
            if (m_session && m_session->mcpServer())
                m_session->mcpServer()->resumeAi();
        });
    }

    m_sessionProps = new ProjectPropertiesPanel(m_session, this);
    m_sessionEnvironment = new EnvironmentPanel(m_session, this);
    m_library = new AssetLibraryPanel(m_session, this);

    // P4a: the legacy entity properties panel comes back next to the project
    // tab. It shows a placeholder until an entity is selected.
    m_props = new PropertiesPanel(m_session, this);
    m_objectTab = new QScrollArea(this);
    m_objectTab->setObjectName(QStringLiteral("objectTab"));
    m_objectTab->setWidgetResizable(true);
    m_objectTab->setFrameShape(QFrame::NoFrame);
    m_objectTab->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_objectTab->setWidget(m_props);

    auto* projectTab = new QWidget(this);
    auto* projectScroll = new QScrollArea(projectTab);
    projectScroll->setObjectName(QStringLiteral("projectScroll"));
    projectScroll->setWidgetResizable(true);
    projectScroll->setFrameShape(QFrame::NoFrame);
    projectScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* projectContent = new QWidget(projectScroll);
    auto* projectLayout = new QVBoxLayout(projectContent);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectLayout->setSpacing(0);

    auto* settingsSection = new CollapsibleSection(
        Lang::t(QStringLiteral("section.projectSettings"), QStringLiteral("项目设置")),
        m_sessionProps, projectContent);
    auto* environmentSection = new CollapsibleSection(
        Lang::t(QStringLiteral("section.environment"), QStringLiteral("环境设置")),
        m_sessionEnvironment, projectContent);
    settingsSection->setExpanded(true);
    environmentSection->setExpanded(false);
    auto* libraryMoved =
        new QLabel(QStringLiteral("素材库已移至时间轴左侧"), projectContent);
    libraryMoved->setObjectName(QStringLiteral("assetMovedLabel"));
    libraryMoved->setContentsMargins(10, 8, 10, 8);
    libraryMoved->setWordWrap(true);
    projectLayout->addWidget(settingsSection);
    projectLayout->addWidget(environmentSection);
    projectLayout->addWidget(libraryMoved);
    projectLayout->addStretch(1);
    projectScroll->setWidget(projectContent);

    auto* projectTabLayout = new QVBoxLayout(projectTab);
    projectTabLayout->setContentsMargins(0, 0, 0, 0);
    projectTabLayout->addWidget(projectScroll);

    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_rightTabs->setMinimumWidth(300);
    m_rightTabs->setMaximumWidth(440);
    m_rightTabs->addTab(projectTab,
                        Lang::t(QStringLiteral("panel.projectTab"),
                                QStringLiteral("项目属性")));
    m_rightTabs->addTab(m_objectTab, QStringLiteral("属性"));

    top->addWidget(m_viewport);
    top->addWidget(m_rightTabs);
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 0);
    top->setSizes({1100, 320});

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Legacy arrangement: the timeline sits below the editing surface, with
    // the asset bin immediately to its left so assets can be dragged in.
    auto* mainSplit = new QSplitter(Qt::Vertical, central);
    mainSplit->setObjectName(QStringLiteral("sessionMainSplit"));
    mainSplit->setChildrenCollapsible(false);
    mainSplit->addWidget(top);

    m_timeline = new TimelinePanel(
        static_cast<EditorSession*>(m_session), mainSplit);
    m_timeline->setObjectName(QStringLiteral("timelinePanel"));

    auto* timelineAssets = new QSplitter(Qt::Horizontal, mainSplit);
    timelineAssets->setObjectName(QStringLiteral("timelineAssetSplit"));
    timelineAssets->setChildrenCollapsible(false);
    m_library->setObjectName(QStringLiteral("timelineAssetPanel"));
    m_library->setCompactMode(true);
    m_library->setMinimumWidth(0);
    timelineAssets->addWidget(m_library);
    timelineAssets->addWidget(m_timeline);
    timelineAssets->setStretchFactor(0, 0);
    timelineAssets->setStretchFactor(1, 1);
    timelineAssets->setSizes({180, 1000});

    mainSplit->addWidget(timelineAssets);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setSizes({640, 220});
    layout->addWidget(mainSplit);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusInfo"));
    statusBar()->addPermanentWidget(m_status);

    // P6: Git history dock.  The panel is the existing next/history widget,
    // styled with the old dark palette and hidden until View -> Git 历史.
    if (m_session)
    {
        m_sessionHistory = new next::HistoryPanel(this);
        m_sessionHistory->setObjectName(QStringLiteral("sessionHistoryPanel"));
        QPalette dark = m_sessionHistory->palette();
        dark.setColor(QPalette::Window, QColor(35, 38, 41));
        dark.setColor(QPalette::WindowText, QColor(239, 240, 241));
        dark.setColor(QPalette::Base, QColor(27, 30, 32));
        dark.setColor(QPalette::AlternateBase, QColor(42, 46, 50));
        dark.setColor(QPalette::Text, QColor(239, 240, 241));
        dark.setColor(QPalette::Button, QColor(42, 46, 50));
        dark.setColor(QPalette::ButtonText, QColor(239, 240, 241));
        dark.setColor(QPalette::Mid, QColor(90, 95, 100));
        dark.setColor(QPalette::Highlight, QColor(61, 174, 233));
        dark.setColor(QPalette::HighlightedText, Qt::white);
        m_sessionHistory->setPalette(dark);
        m_sessionHistory->setAutoFillBackground(true);

        m_sessionHistoryDock = new QDockWidget(QStringLiteral("Git 历史"), this);
        m_sessionHistoryDock->setObjectName(QStringLiteral("sessionHistoryDock"));
        m_sessionHistoryDock->setAllowedAreas(Qt::RightDockWidgetArea
                                              | Qt::BottomDockWidgetArea);
        m_sessionHistoryDock->setFeatures(QDockWidget::DockWidgetClosable
                                          | QDockWidget::DockWidgetMovable
                                          | QDockWidget::DockWidgetFloatable);
        m_sessionHistoryDock->setWidget(m_sessionHistory);
        addDockWidget(Qt::RightDockWidgetArea, m_sessionHistoryDock);
        m_sessionHistoryDock->hide();
        connect(m_sessionHistory, &next::HistoryPanel::refreshRequested, this,
                &MainWindow::refreshSessionHistory);
        connect(m_sessionHistory, &next::HistoryPanel::restoreRequested, this,
                [this](const QString& hash, const QString&) {
                    restoreSessionCommit(hash);
                });
        connect(m_sessionHistoryDock, &QDockWidget::visibilityChanged, this,
                [this](bool visible) {
                    if (m_historyAction)
                    {
                        const QSignalBlocker blocker(m_historyAction);
                        m_historyAction->setChecked(visible);
                    }
                    if (visible)
                        refreshSessionHistory();
                });
    }

    m_mcpStatusButton = new QToolButton(this);
    m_mcpStatusButton->setObjectName(QStringLiteral("mcpStatusButton"));
    m_mcpStatusButton->setAutoRaise(true);
    m_mcpStatusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_mcpStatusButton->setFocusPolicy(Qt::NoFocus);
    connect(m_mcpStatusButton, &QToolButton::clicked, this,
            &MainWindow::showSessionMcpLog);
    if (m_session)
        statusBar()->addPermanentWidget(m_mcpStatusButton);

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::sessionAutoSave);
    updateAutosaveTimer();

    connect(m_sessionProps, &ProjectPropertiesPanel::settingsChanged, this,
            [this] {
                updateWindowTitle();
                if (m_viewport)
                    m_viewport->refreshSession();
                if (m_monitor)
                    m_monitor->setProjectChanged();
                updateSessionStatus();
            });
    connect(m_sessionEnvironment, &EnvironmentPanel::changed, this,
            [this] {
                if (m_viewport)
                    m_viewport->refreshSession();
                if (m_monitor)
                    m_monitor->setProjectChanged();
                updateSessionStatus();
            });
    if (m_props)
    {
        connect(m_props, &PropertiesPanel::sessionEditFailed, this,
                [this](const QString& message) {
                    statusBar()->showMessage(
                        QStringLiteral("编辑失败：%1").arg(message), 5000);
                });
        connect(m_props, &PropertiesPanel::sessionReplaceSourceRequested, this,
                &MainWindow::replaceSessionAssetFromDialog);
    }
    if (m_library)
    {
        connect(m_library, &AssetLibraryPanel::statusMessage, this,
                [this](const QString& message) {
                    statusBar()->showMessage(message, 5000);
                });
        connect(m_library, &AssetLibraryPanel::assetsChanged, this, [this] {
            if (m_viewport)
                m_viewport->refreshSession();
            if (m_monitor)
                m_monitor->setProjectChanged();
        });
    }

    if (m_session && m_session->document())
    {
        auto* document = m_session->document();
        connect(document, &next::Document::changed, this, [this] {
            if (m_monitor)
                m_monitor->setProjectChanged();
            refreshSession();
        });
        connect(document, &next::Document::pendingCommitChanged, this,
                [this](bool) {
                    updateWindowTitle();
                    refreshSession();
                });
        connect(document, &next::Document::commitFailed, this,
                [this](const QString& message) {
                    statusBar()->showMessage(
                        QStringLiteral("提交失败：%1").arg(message), 5000);
                });
        connect(document, &next::Document::playheadChanged, this, [this] {
            if (!m_session)
                return;
            m_frame = m_session->playhead();
            if (m_timeline)
                m_timeline->setCurrentFrame(m_frame);
            if (m_viewport)
                m_viewport->setCurrentFrame(m_frame);
            if (m_monitor)
                m_monitor->setFrame(m_frame);
            updateSessionStatus();
        });
        connect(document, &next::Document::selectionChanged, this, [this] {
            if (!m_session)
                return;
            m_selected = m_timeline
                             ? m_timeline->entityIdForUuid(
                                   m_session->document()->selection())
                             : kInvalidEntity;
            if (m_timeline)
                m_timeline->setSelectedEntity(m_selected);
            if (m_props)
            {
                m_props->setSelectedUuid(m_session->document()->selection());
                if (m_rightTabs && m_objectTab)
                {
                    const int index = m_rightTabs->indexOf(m_objectTab);
                    const Uuid selectedUuid = m_session->document()->selection();
                    const auto it =
                        m_session->project().entities.constFind(selectedUuid);
                    if (index >= 0)
                        m_rightTabs->setTabText(
                            index, it == m_session->project().entities.constEnd()
                                       ? QStringLiteral("属性")
                                       : it.value().name);
                    m_rightTabs->setCurrentWidget(
                        selectedUuid.isEmpty() ? m_rightTabs->widget(0)
                                               : m_objectTab);
                }
            }
            // Selecting a camera makes it the framing camera and asks the
            // monitor to show it, matching the legacy interaction.
            const Uuid selected = m_session->document()->selection();
            m_session->syncSelectionCamera(selected);
            if (m_monitor)
                m_monitor->syncCameraView();
            // The canvas workbench cache does not depend on the selected
            // camera, but the monitor and viewfinder do; repaint both.
            if (m_viewport)
                m_viewport->refreshSession();
            updateSessionStatus();
        });

        if (m_viewport)
        {
            connect(m_viewport, &Viewport::uuidSelected, this,
                    [this](const Uuid& uuid) {
                        if (m_session && m_session->document())
                            m_session->document()->setSelection(uuid);
                    });
            connect(m_viewport, &Viewport::sessionEditFailed, this,
                    [this](const QString& message) {
                        statusBar()->showMessage(
                            QStringLiteral("编辑失败：%1").arg(message), 5000);
                    });
            if (auto* playback = m_session->playback())
            {
                connect(m_viewport, &Viewport::framePresented, playback,
                        [playback](int) { playback->noteFramePresented(); });
                connect(playback, &next::PlaybackController::playbackStateChanged,
                        this, [this](bool) { updateSessionPlaybackUi(); });
                connect(playback, &next::PlaybackController::stalled, this,
                        [this](const QString& message) {
                            statusBar()->showMessage(message, 5000);
                        });
            }
        }

        if (m_monitor)
        {
            connect(m_viewport, &Viewport::viewChanged, m_monitor,
                    &CameraMonitor::updateFromViewport);
            connect(m_monitor, &CameraMonitor::cameraUuidActivated, this,
                    [this](const Uuid& uuid) {
                        if (!m_session)
                            return;
                        m_session->setMonitorCameraUuid(uuid);
                        if (m_viewport)
                            m_viewport->refreshSession();
                        updateSessionStatus();
                    });
            connect(m_monitor, &CameraMonitor::visibilityChanged, this,
                    [this](bool visible) {
                        if (auto* action = findChild<QAction*>(
                                QStringLiteral("monitorAction")))
                        {
                            QSignalBlocker blocker(action);
                            action->setChecked(visible);
                        }
                    });
            if (auto* playback = m_session->playback())
                connect(playback, &next::PlaybackController::playbackStateChanged,
                        m_monitor, [this](bool playing) {
                            if (m_monitor)
                                m_monitor->setPlaying(playing);
                        });
        }

        if (m_timeline)
        {
            connect(m_timeline, &TimelinePanel::frameRequested, this,
                    [this](int frame) { sessionGoToFrame(frame); });
            connect(m_timeline, &TimelinePanel::entitySelected, this,
                    [this](EntityId id) {
                        if (!m_session || !m_timeline)
                            return;
                        m_selected = id;
                        m_session->document()->setSelection(
                            m_timeline->uuidForEntityId(id));
                        updateSessionStatus();
                    });
            connect(m_timeline, &TimelinePanel::scrubStarted, this, [this] {
                if (m_session && m_session->playback()
                    && m_session->playback()->isPlaying())
                    m_session->playback()->pause();
            });
            connect(m_timeline, &TimelinePanel::editCommitted, this,
                    [this](const QString& text) {
                        statusBar()->showMessage(text, 2000);
                    });
            connect(m_timeline, &TimelinePanel::editFailed, this,
                    [this](const QString& message) {
                        statusBar()->showMessage(
                            QStringLiteral("编辑失败：%1").arg(message), 5000);
                    });
        }

        // Opening a project always starts the viewport at frame 0, even when
        // the project carries a persisted playhead.
        m_session->setPlayhead(0);
        m_frame = m_session->playhead();
        m_selected = m_timeline
                         ? m_timeline->entityIdForUuid(document->selection())
                         : kInvalidEntity;
        if (m_props)
            m_props->setSelectedUuid(document->selection());
    }

    // P6: MCP endpoint, AI lock and history wiring.  The server itself was
    // started by EditorSession when the project opened; this connects its
    // signals to the legacy panels.
    if (m_session)
    {
        if (m_timeline)
        {
            m_session->setViewStateExtras([this](mcp2::ViewState& state) {
                if (m_timeline)
                    state.viewScale = m_timeline->pixelsPerFrame();
            });
        }
        wireSessionMcpServer();
        updateSessionMcpStatus();
        refreshSessionHistory();
    }

    createSessionMenus();
    createSessionToolBar();
    if (m_viewport)
        m_viewport->setAutoKeyEnabled(!m_autoKeyAction
                                      || m_autoKeyAction->isChecked());
    if (m_props)
    {
        m_props->setAutoKey(!m_autoKeyAction || m_autoKeyAction->isChecked());
        if (m_autoKeyAction)
        {
            connect(m_props, &PropertiesPanel::autoKeyToggled, this,
                    [this](bool on) {
                        const QSignalBlocker blocker(m_autoKeyAction);
                        m_autoKeyAction->setChecked(on);
                        if (m_viewport)
                            m_viewport->setAutoKeyEnabled(on);
                    });
        }
    }
    refreshSession();
    updateSessionPlaybackUi();
    updateWindowTitle();
    showSessionOnboardingIfNeeded();
}

void MainWindow::refreshSession()
{
    if (!m_session)
        return;

    if (m_sessionProps)
        m_sessionProps->refresh();
    if (m_sessionEnvironment)
        m_sessionEnvironment->refresh();
    if (m_props)
        m_props->refresh();
    if (m_viewport)
        m_viewport->refreshSession();

    updateSessionStatus();
    updateWindowTitle();

    if (auto* document = m_session->document())
    {
        if (m_undoAction)
            m_undoAction->setEnabled(document->store()->undoDepth() > 0);
        if (m_redoAction)
            m_redoAction->setEnabled(document->store()->redoDepth() > 0);
    }
    updateSessionActionStates();
}

void MainWindow::sessionGoToFrame(int frame)
{
    if (!m_session)
        return;
    const int clamped = std::max(0, frame);
    m_session->setPlayhead(clamped);
    m_frame = clamped;
    if (m_timeline)
        m_timeline->setCurrentFrame(clamped);
    if (m_viewport)
        m_viewport->setCurrentFrame(clamped);
    if (m_monitor)
        m_monitor->setFrame(clamped);
    updateSessionStatus();
}

void MainWindow::updateSessionPlaybackUi()
{
    const bool playing = m_session && m_session->playback()
                         && m_session->playback()->isPlaying();
    if (m_playAction)
        m_playAction->setText(
            playing ? Lang::t(QStringLiteral("action.pause"), QStringLiteral("暂停"))
                    : Lang::t(QStringLiteral("action.play"), QStringLiteral("播放")));
    updateSessionStatus();
}

void MainWindow::updateSessionStatus()
{
    if (!m_status)
        return;

    if (!m_session)
    {
        m_status->setText(QStringLiteral("没有打开的工程"));
        return;
    }

    const model::ProjectSettings& settings = m_session->settings();
    const model::Project& project = m_session->project();
    int totalEnd = 0;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        if (it->parentUuid.isEmpty())
            totalEnd = std::max(totalEnd, it->end);
    }

    QString selection = QStringLiteral("无");
    if (auto* document = m_session->document())
    {
        const auto found = project.entities.constFind(document->selection());
        if (found != project.entities.constEnd())
            selection = found->name.isEmpty() ? found->uuid : found->name;
    }

    const bool playing = m_session->playback() && m_session->playback()->isPlaying();
    QString aiState;
    if (m_sessionAiStopped)
        aiState = QStringLiteral(" · AI 已急停");
    else if (m_sessionAiBusyWrites > 0)
        aiState = QStringLiteral(" · AI 修改中");
    m_status->setText(
        QStringLiteral("%1 × %2 @ %3 fps · 帧 %4 / 总长 %5 · %6 · 选中 %7%8")
            .arg(settings.width)
            .arg(settings.height)
            .arg(settings.fps)
            .arg(m_session->playhead())
            .arg(totalEnd)
            .arg(playing ? QStringLiteral("播放中") : QStringLiteral("暂停"))
            .arg(selection)
            .arg(aiState));
}

void MainWindow::setHumanEditingEnabled(bool enabled)
{
    if (m_session)
        m_session->setHumanEditingEnabled(enabled);

    // Playback, view navigation, the camera monitor and the MCP log stay live
    // so the human can stop the AI or inspect the timeline while locked.
    for (QWidget* panel : {static_cast<QWidget*>(m_timeline),
                           static_cast<QWidget*>(m_props),
                           static_cast<QWidget*>(m_library),
                           static_cast<QWidget*>(m_workbench),
                           static_cast<QWidget*>(m_sessionProps),
                           static_cast<QWidget*>(m_sessionEnvironment),
                           static_cast<QWidget*>(m_sessionHistory),
                           static_cast<QWidget*>(m_sessionSettingsDialog)})
    {
        if (panel)
            panel->setEnabled(enabled);
    }
    if (m_viewport)
        m_viewport->setEditingEnabled(enabled);

    updateSessionActionStates();
}

void MainWindow::updateSessionActionStates()
{
    if (!m_session)
        return;

    const bool human = m_session->humanEditingEnabled();
    bool canUndo = false;
    bool canRedo = false;
    if (auto* document = m_session->document())
    {
        canUndo = document->store()->undoDepth() > 0;
        canRedo = document->store()->redoDepth() > 0;
    }
    if (m_undoAction)
        m_undoAction->setEnabled(human && canUndo);
    if (m_redoAction)
        m_redoAction->setEnabled(human && canRedo);
    if (m_exportAction)
        m_exportAction->setEnabled(human && !m_exportRunning);
    for (const QString& name : {QStringLiteral("saveAction"),
                                QStringLiteral("newProjectAction"),
                                QStringLiteral("openProjectAction")})
    {
        if (auto* action = findChild<QAction*>(name))
            action->setEnabled(human && !m_exportRunning);
    }
    if (m_settingsAction)
        m_settingsAction->setEnabled(human && !m_exportRunning);
}

void MainWindow::refreshSessionAfterExternalChange()
{
    if (!m_session || !m_session->document())
        return;

    QString error;
    if (!m_session->document()->notifyExternalChange(&error)
        && !error.isEmpty())
    {
        if (m_mcpLog)
            m_mcpLog->addSystemEvent(QStringLiteral("刷新工程失败"), error);
        statusBar()->showMessage(
            QStringLiteral("刷新工程失败：%1").arg(error), 8000);
    }

    // The whole-frame cache key includes the content hash; dropping it forces a
    // rescan on the next paint instead of showing stale pre-rendered frames.
    m_session->invalidateWorkbench();
    if (m_timeline)
        m_timeline->refreshFromSession();
    if (m_library)
        m_library->refresh();
    if (m_props)
        m_props->refresh();
    if (m_sessionProps)
        m_sessionProps->refresh();
    if (m_sessionEnvironment)
        m_sessionEnvironment->refresh();
    if (m_workbench)
        m_workbench->refreshSession();
    if (m_viewport)
        m_viewport->refreshSession();
    if (m_monitor)
        m_monitor->setProjectChanged();
    refreshSession();
}

MainWindow::~MainWindow()
{
    // The export worker borrows MediaServices and a project snapshot; wait for
    // it before the session (and its decoders) are destroyed.
    if (m_exportWatcher && m_exportWatcher->isRunning())
    {
        if (m_exportCancelled)
            m_exportCancelled->store(true);
        m_exportWatcher->waitForFinished();
    }
    if (m_exportTimer)
        m_exportTimer->stop();

}

void MainWindow::toggleWorkbench()
{
    if (m_workbench->isVisible())
    {
        m_workbench->hidePanel();
        return;
    }

    const QPoint anchor =
        m_viewport->workbenchButton()->geometry().bottomLeft() + QPoint(0, 8);
    m_workbench->showAt(anchor);
}

void MainWindow::updateWindowTitle()
{
    if (m_session)
    {
        const bool pending = m_session->document()
                             && m_session->document()->hasPendingCommit();
        setWindowTitle(QStringLiteral("PVForge — %1%2")
                           .arg(m_session->name(),
                                pending ? QStringLiteral(" *") : QString()));
        return;
    }
}


void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (m_session)
    {
        // P2a has no session-level delete; the timeline is read-only.
        QMainWindow::keyPressEvent(event);
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::saveProject()
{
    if (m_session)
        return saveSessionProject();
    return false;
}

void MainWindow::autoSave()
{
    if (m_session)
    {
        sessionAutoSave();
        return;
    }
}

void MainWindow::updateAutosaveTimer()
{
    if (!m_autosaveTimer)
        return;
    if (m_session)
    {
        const int minutes =
            std::clamp(m_session->appSettings().autoSaveMinutes, 1, 60);
        m_autosaveTimer->start(minutes * 60 * 1000);
        return;
    }
    m_autosaveTimer->stop();
}

void MainWindow::autoSaveNowForTest()
{
    autoSave();
}

void MainWindow::triggerSessionAutoSaveForTest()
{
    sessionAutoSave();
}

void MainWindow::sessionAutoSave()
{
    if (!m_session || !m_session->document())
        return;
    if (!m_session->document()->hasPendingCommit())
        return;

    QString error;
    m_session->flush(&error);
    if (!error.isEmpty())
    {
        statusBar()->showMessage(
            QStringLiteral("自动保存失败：%1").arg(error), 8000);
        return;
    }
    m_session->savePlayhead();
    statusBar()->showMessage(QStringLiteral("已自动保存"), 3000);
}

bool MainWindow::humanEditingEnabledForTest() const
{
    return m_session ? m_session->humanEditingEnabled() : true;
}

void MainWindow::reloadAutosaveSettingsForTest()
{
    updateAutosaveTimer();
}

bool MainWindow::autosaveActiveForTest() const
{
    return m_autosaveTimer && m_autosaveTimer->isActive();
}

int MainWindow::autosaveIntervalForTest() const
{
    return m_autosaveTimer ? m_autosaveTimer->interval() : 0;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(
            QStringLiteral("导出正在进行中，请先取消或等待完成。"), 5000);
        event->ignore();
        return;
    }

    if (m_session)
    {
        // Edits are already on disk; flush only the debounced git commit and
        // persist the playhead next to the project.
        if (m_session->playback())
            m_session->playback()->pause();
        QString error;
        m_session->flush(&error);
        if (!error.isEmpty())
            statusBar()->showMessage(
                QStringLiteral("关闭时提交失败：%1").arg(error), 5000);
        // Free the fixed MCP port before a project switch constructs the next
        // session (the new listener binds the same configured port).
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
        event->accept();
        return;
    }

    event->accept();
}


void MainWindow::setFrame(int frame)
{
    if (m_session)
    {
        sessionGoToFrame(frame);
        return;
    }
}


void MainWindow::refreshAll()
{
    if (m_session)
    {
        refreshSession();
        return;
    }
}


void MainWindow::togglePlayback()
{
    if (m_session)
    {
        auto* playback = m_session->playback();
        if (!playback)
            return;
        if (playback->isPlaying())
        {
            playback->pause();
        }
        else
        {
            if (m_session->playhead() >= playback->contentEnd())
                sessionGoToFrame(0);
            playback->play();
        }
        updateSessionPlaybackUi();
        return;
    }
}

} // namespace pvf
