#include "MainWindow.h"

#include "EditorSession.h"
#include "NewProjectDialog.h"
#include "assets/AssetStore.h"
#include "core/CheckpointManager.h"
#include "core/Lang.h"
#include "core/PerfMonitor.h"
#include "core/Settings.h"
#include "framecache/CacheUsage.h"
#include "htmlcommon/HtmlBrowser.h"
#include "ExportDialog.h"
#include "ExportProgressDialog.h"
#include "SettingsDialog.h"
#include "TestMode.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/core/Document.h"
#include "next/history/HistoryPanel.h"
#include "next/settings/OnboardingDialog.h"
#include "next/settings/SettingsStore.h"
#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"
#include "model/Project.h"
#include "model/Query.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CameraMonitor.h"
#include "ui/CollapsibleSection.h"
#include "ui/EnvironmentPanel.h"
#include "ui/McpLogPanel.h"
#include "ui/PerfMonitorWindow.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"


#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {


} // namespace


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

void MainWindow::createSessionMenus()
{
    auto* fileMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.file"), QStringLiteral("文件")));

    auto* newAction = new QAction(QStringLiteral("新建项目…"), this);
    newAction->setObjectName(QStringLiteral("newProjectAction"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::createSessionProject);
    fileMenu->addAction(newAction);

    auto* openAction = new QAction(QStringLiteral("打开项目…"), this);
    openAction->setObjectName(QStringLiteral("openProjectAction"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openSessionProject);
    fileMenu->addAction(openAction);

    fileMenu->addSeparator();

    m_saveAction =
        new QAction(Lang::t(QStringLiteral("action.save"), QStringLiteral("保存")), this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    addAction(m_saveAction);
    fileMenu->addAction(m_saveAction);

    m_exportAction =
        new QAction(Lang::t(QStringLiteral("action.exportVideo"),
                            QStringLiteral("导出视频…")),
                    this);
    m_exportAction->setObjectName(QStringLiteral("exportAction"));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportVideo);
    addAction(m_exportAction);
    fileMenu->addAction(m_exportAction);

    auto* folderAction =
        new QAction(Lang::t(QStringLiteral("action.openFolder"),
                            QStringLiteral("打开工程文件夹")),
                    this);
    connect(folderAction, &QAction::triggered, this, [this] {
        if (m_session)
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_session->root()));
    });
    fileMenu->addAction(folderAction);

    fileMenu->addSeparator();
    m_quitAction =
        new QAction(Lang::t(QStringLiteral("action.quit"), QStringLiteral("退出")), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
    addAction(m_quitAction);
    fileMenu->addAction(m_quitAction);

    auto* editMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.edit"), QStringLiteral("编辑")));
    m_undoAction =
        new QAction(Lang::t(QStringLiteral("action.undo"), QStringLiteral("撤销")), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, [this] {
        if (!m_session || !m_session->document())
            return;
        QString error;
        if (!m_session->document()->undo(&error))
            statusBar()->showMessage(QStringLiteral("撤销失败：%1").arg(error), 3000);
        refreshSession();
    });
    addAction(m_undoAction);
    editMenu->addAction(m_undoAction);

    m_redoAction =
        new QAction(Lang::t(QStringLiteral("action.redo"), QStringLiteral("重做")), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, [this] {
        if (!m_session || !m_session->document())
            return;
        QString error;
        if (!m_session->document()->redo(&error))
            statusBar()->showMessage(QStringLiteral("重做失败：%1").arg(error), 3000);
        refreshSession();
    });
    addAction(m_redoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();

    m_autoKeyAction =
        new QAction(Lang::t(QStringLiteral("action.autoKey"),
                            QStringLiteral("自动关键帧")),
                    this);
    m_autoKeyAction->setObjectName(QStringLiteral("autoKeyAction"));
    m_autoKeyAction->setCheckable(true);
    m_autoKeyAction->setChecked(Settings::autoKeyDefault());
    connect(m_autoKeyAction, &QAction::toggled, this, [this](bool on) {
        if (m_viewport)
            m_viewport->setAutoKeyEnabled(on);
        if (m_props)
            m_props->setAutoKey(on);
    });
    editMenu->addAction(m_autoKeyAction);
    editMenu->addSeparator();

    m_settingsAction = new QAction(QStringLiteral("设置…"), this);
    m_settingsAction->setObjectName(QStringLiteral("settingsAction"));
    m_settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(m_settingsAction, &QAction::triggered, this,
            &MainWindow::openSessionSettings);
    editMenu->addAction(m_settingsAction);

    auto* playMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.play"), QStringLiteral("播放")));
    m_playAction =
        new QAction(Lang::t(QStringLiteral("action.play"), QStringLiteral("播放")), this);
    m_playAction->setObjectName(QStringLiteral("playAction"));
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);
    addAction(m_playAction);
    playMenu->addAction(m_playAction);

    m_homeAction =
        new QAction(Lang::t(QStringLiteral("action.home"), QStringLiteral("回到开头")),
                    this);
    m_homeAction->setObjectName(QStringLiteral("homeAction"));
    m_homeAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(m_homeAction, &QAction::triggered, this, [this] {
        sessionGoToFrame(0);
    });
    addAction(m_homeAction);
    playMenu->addAction(m_homeAction);

    m_endAction =
        new QAction(Lang::t(QStringLiteral("action.end"), QStringLiteral("跳到结尾")),
                    this);
    m_endAction->setObjectName(QStringLiteral("endAction"));
    m_endAction->setShortcut(QKeySequence(Qt::Key_End));
    connect(m_endAction, &QAction::triggered, this, [this] {
        if (m_session && m_session->playback())
            sessionGoToFrame(m_session->playback()->contentEnd());
    });
    addAction(m_endAction);
    playMenu->addAction(m_endAction);

    auto* stepBackAction = new QAction(QStringLiteral("上一帧"), this);
    stepBackAction->setShortcut(QKeySequence(Qt::Key_Left));
    connect(stepBackAction, &QAction::triggered, this, [this] {
        if (m_session)
            sessionGoToFrame(std::max(0, m_session->playhead() - 1));
    });
    addAction(stepBackAction);
    playMenu->addAction(stepBackAction);

    auto* stepForwardAction = new QAction(QStringLiteral("下一帧"), this);
    stepForwardAction->setShortcut(QKeySequence(Qt::Key_Right));
    connect(stepForwardAction, &QAction::triggered, this, [this] {
        if (m_session)
            sessionGoToFrame(m_session->playhead() + 1);
    });
    addAction(stepForwardAction);
    playMenu->addAction(stepForwardAction);

    m_fitAction =
        new QAction(Lang::t(QStringLiteral("action.fitWindow"),
                            QStringLiteral("适配窗口")),
                    this);
    m_fitAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(m_fitAction, &QAction::triggered, this, [this] {
        if (m_viewport)
            m_viewport->resetView();
    });
    addAction(m_fitAction);
    playMenu->addAction(m_fitAction);

    auto* viewMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.view"), QStringLiteral("视图")));
    auto* cameraFrameAction = new QAction(
        Lang::t(QStringLiteral("action.cameraFrame"),
                QStringLiteral("显示摄像机取景框")),
        this);
    cameraFrameAction->setObjectName(QStringLiteral("cameraFrameAction"));
    cameraFrameAction->setCheckable(true);
    cameraFrameAction->setChecked(Settings::showCameraFrame());
    connect(cameraFrameAction, &QAction::toggled, this, [this](bool on) {
        Settings::setShowCameraFrame(on);
        if (m_viewport)
            m_viewport->setCameraFrameVisible(on);
    });
    viewMenu->addAction(cameraFrameAction);

    auto* monitorAction = new QAction(
        Lang::t(QStringLiteral("action.monitor"), QStringLiteral("摄像机监视器")),
        this);
    monitorAction->setObjectName(QStringLiteral("monitorAction"));
    monitorAction->setCheckable(true);
    monitorAction->setChecked(Settings::monitorVisible());
    connect(monitorAction, &QAction::toggled, this, [this](bool on) {
        if (m_monitor)
            m_monitor->setVisibleFromMenu(on);
    });
    viewMenu->addAction(monitorAction);

    m_mcpLogAction = new QAction(QStringLiteral("MCP 日志"), this);
    m_mcpLogAction->setObjectName(QStringLiteral("mcpLogAction"));
    m_mcpLogAction->setCheckable(true);
    connect(m_mcpLogAction, &QAction::toggled, this, [this](bool on) {
        if (!m_mcpLog)
            return;
        if (on)
            showSessionMcpLog();
        else if (!m_mcpLog->isDetached())
            m_mcpLog->hide();
    });
    viewMenu->addAction(m_mcpLogAction);

    m_historyAction = new QAction(QStringLiteral("Git 历史"), this);
    m_historyAction->setObjectName(QStringLiteral("historyAction"));
    m_historyAction->setCheckable(true);
    connect(m_historyAction, &QAction::toggled, this, [this](bool on) {
        if (!m_sessionHistoryDock)
            return;
        m_sessionHistoryDock->setVisible(on);
        if (on)
        {
            m_sessionHistoryDock->raise();
            refreshSessionHistory();
        }
    });
    viewMenu->addAction(m_historyAction);

    viewMenu->addAction(createPerfMonitorWindowAction());

    viewMenu->addAction(m_fitAction);

    auto* helpMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.help"), QStringLiteral("帮助")));
    helpMenu->addAction(
        Lang::t(QStringLiteral("action.about"), QStringLiteral("关于 PVForge")), this,
        [this] {
            QMessageBox::about(
                this, QStringLiteral("关于 PVForge"),
                QStringLiteral("PVForge %1\n\n旧 UI + 新实体模型后端：目录工程、时间轴、render2 视口、export2 导出、MCP、Git 历史与工作台缓存。")
                    .arg(QStringLiteral(PVFORGE_VERSION)));
        });

    menuBar()->setFixedHeight(menuBar()->sizeHint().height());
}

void MainWindow::createSessionToolBar()
{
    auto* bar = addToolBar(QStringLiteral("播放"));
    bar->setObjectName(QStringLiteral("sessionPlayToolBar"));
    bar->setMovable(false);
    if (m_playAction)
        bar->addAction(m_playAction);
    if (m_homeAction)
        bar->addAction(m_homeAction);
    if (m_endAction)
        bar->addAction(m_endAction);
    if (m_fitAction)
        bar->addAction(m_fitAction);
    if (m_autoKeyAction)
        bar->addAction(m_autoKeyAction);
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

void MainWindow::wireSessionMcpServer()
{
    if (!m_session || !m_session->mcpServer())
    {
        updateSessionMcpStatus();
        return;
    }

    mcp2::HttpServer* server = m_session->mcpServer();
    if (m_wiredSessionMcpServer == server)
        return;
    m_wiredSessionMcpServer = server;

    connect(server, &mcp2::HttpServer::toolCallStarted, this,
            &MainWindow::onSessionMcpToolStarted);
    connect(server, &mcp2::HttpServer::toolCallFinished, this,
            &MainWindow::onSessionMcpToolFinished);
    connect(server, &mcp2::HttpServer::stoppedChanged, this,
            &MainWindow::onSessionMcpStoppedChanged);

    m_sessionAiStopped = server->aiStopped();
    if (m_mcpLog)
    {
        m_mcpLog->setServerState(server->isRunning(), server->port(),
                                 m_session->mcpError());
        m_mcpLog->setWritesEnabled(m_session->document()
                                   && m_session->document()->store()->gitEnabled());
        m_mcpLog->setAiState(m_sessionAiStopped, false,
                             m_session->document()
                                 && m_session->document()->store()->gitEnabled());
        m_mcpLog->addSystemEvent(
            server->isRunning() ? QStringLiteral("MCP 已启动")
                                : QStringLiteral("MCP 启动失败"),
            server->isRunning()
                ? QStringLiteral("127.0.0.1:%1").arg(server->port())
                : m_session->mcpError());
    }
    refreshSessionAiLock();
}

void MainWindow::onSessionMcpToolStarted(const QString& name,
                                         const QString& argsJson, bool write)
{
    m_sessionRunningToolArgs.insert(name, argsJson);
    if (write)
    {
        ++m_sessionRunningWrites[name];
        ++m_sessionAiBusyWrites;
    }

    if (m_mcpLog)
    {
        if (m_mcpLog->loggingEnabled() && !m_mcpLog->isVisible())
            showSessionMcpLog();
        m_mcpLog->addSystemEvent(QStringLiteral("开始：%1").arg(name), argsJson);
    }
    refreshSessionAiLock();
}

void MainWindow::onSessionMcpToolFinished(const QString& name,
                                          const QString& resultJson, qint64 ms,
                                          bool ok)
{
    const QString argsJson = m_sessionRunningToolArgs.take(name);
    const bool write = mcp2::Service::isWriteTool(name);
    if (write)
    {
        auto it = m_sessionRunningWrites.find(name);
        if (it != m_sessionRunningWrites.end())
        {
            if (--it.value() <= 0)
                m_sessionRunningWrites.erase(it);
        }
        if (m_sessionAiBusyWrites > 0)
            --m_sessionAiBusyWrites;
    }

    if (m_mcpLog)
        m_mcpLog->addToolInvocation(name, argsJson, resultJson, ms, ok);

    // Write tools commit straight through the store.  Reload the document from
    // disk first so the old timeline/viewport/properties/asset bin/workbench
    // all see the new bytes.
    if (write)
    {
        refreshSessionAfterExternalChange();
        refreshSessionHistory();
        updateSessionSettingsDialogContext();
    }

    refreshSessionAiLock();
    statusBar()->showMessage(
        ok ? QStringLiteral("AI %1 完成（%2 ms）").arg(name).arg(ms)
           : QStringLiteral("AI %1 失败（%2 ms）").arg(name).arg(ms),
        5000);
}

void MainWindow::onSessionMcpStoppedChanged(bool stopped)
{
    m_sessionAiStopped = stopped;
    if (m_mcpLog)
    {
        m_mcpLog->addSystemEvent(
            stopped ? QStringLiteral("AI 已急停")
                    : QStringLiteral("AI 写入已恢复"));
    }
    refreshSessionAiLock();
    statusBar()->showMessage(
        stopped ? QStringLiteral("AI 已急停：写入暂停，直到点“恢复”")
                : QStringLiteral("AI 写入已恢复"),
        5000);
}

void MainWindow::refreshSessionAiLock()
{
    // An emergency stop deliberately releases human editing even though the
    // in-flight tool is still unwinding; the Service keeps rejecting AI writes
    // until resumeAi().  Without a stop, only a running write locks.
    const bool writing = m_sessionAiBusyWrites > 0 && !m_sessionAiStopped;
    setHumanEditingEnabled(!writing && !m_exportRunning);

    const bool writesEnabled =
        m_session && m_session->document()
        && m_session->document()->store()->gitEnabled();
    if (m_mcpLog)
    {
        m_mcpLog->setWritesEnabled(writesEnabled);
        m_mcpLog->setAiState(m_sessionAiStopped, writing, writesEnabled);
    }
    updateSessionMcpStatus();
    updateSessionStatus();
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

void MainWindow::updateSessionMcpStatus()
{
    if (!m_mcpStatusButton || !m_session)
        return;

    mcp2::HttpServer* server = m_session->mcpServer();
    const bool running = server && server->isRunning();
    const bool writesEnabled =
        m_session->document() && m_session->document()->store()->gitEnabled();
    if (running)
    {
        if (m_sessionAiStopped)
            m_mcpStatusButton->setText(QStringLiteral("MCP 已急停 · 日志"));
        else if (m_sessionAiBusyWrites > 0)
            m_mcpStatusButton->setText(QStringLiteral("MCP AI 修改中 · 日志"));
        else if (!writesEnabled)
            m_mcpStatusButton->setText(QStringLiteral("MCP 只读 · 日志"));
        else
            m_mcpStatusButton->setText(QStringLiteral("MCP 已启用 · 日志"));
        QString tip = QStringLiteral("监听中：%1").arg(server->endpointUrl());
        tip += writesEnabled ? QStringLiteral("\n可写（Git 已启用）")
                             : QStringLiteral("\n只读：Git 未启用");
        if (m_sessionAiStopped)
            tip += QStringLiteral("\n人类已急停，写操作返回 stopped_by_human");
        tip += QStringLiteral("\n点击打开 MCP 日志");
        m_mcpStatusButton->setToolTip(tip);
    }
    else
    {
        const QString error = m_session->mcpError();
        m_mcpStatusButton->setText(error.isEmpty()
                                       ? QStringLiteral("MCP 关闭 · 日志")
                                       : QStringLiteral("MCP 错误 · 日志"));
        m_mcpStatusButton->setToolTip(
            (error.isEmpty() ? QStringLiteral("MCP 未启动")
                             : QStringLiteral("MCP 启动失败：%1").arg(error))
            + QStringLiteral("\n点击打开 MCP 日志"));
    }
}

void MainWindow::showSessionMcpLog()
{
    if (!m_mcpLog)
        return;
    m_mcpLog->showFromMenu();
    arrangeMcpLog();
}

void MainWindow::wireSessionSettingsDialog(SettingsDialog* dialog)
{
    if (!dialog)
        return;

    connect(dialog, &SettingsDialog::settingsApplied, this, [this, dialog] {
        if (!m_session)
            return;
        applySessionSettings(dialog->appSettings());
    });
    connect(dialog, &SettingsDialog::resetMcpTokenRequested, this, [this] {
        if (!m_session)
            return;
        if (m_session->mcpServer())
            m_session->mcpServer()->resetCredentials();
        updateSessionSettingsDialogContext();
        statusBar()->showMessage(
            QStringLiteral("MCP token 已重置；客户端需重新读取 endpoint 文件"),
            8000);
    });
    connect(dialog, &SettingsDialog::clearHtmlCacheRequested, this, [this] {
        if (!m_session)
            return;
        const QString path =
            QDir(m_session->root()).filePath(QStringLiteral(".cache/html"));
        const framecache::ClearResult result = framecache::clearDirectory(
            path, {QStringLiteral("webp")});
        refreshSessionCacheStats();
        if (!result.ok)
        {
            const QString first = result.failedPaths.isEmpty()
                                      ? QStringLiteral("（未知）")
                                      : QDir::toNativeSeparators(
                                            result.failedPaths.first());
            statusBar()->showMessage(
                QStringLiteral(
                    "清理 HTML 缓存失败：%1 个文件未能删除（例：%2），实际释放 %3 MB")
                    .arg(result.failedPaths.size())
                    .arg(first)
                    .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0,
                         'f', 1),
                12000);
            qWarning().noquote()
                << QStringLiteral("html cache clear failed: %1")
                       .arg(result.failedPaths.join(QStringLiteral(", ")));
            return;
        }
        statusBar()->showMessage(
            QStringLiteral("已清理 HTML 缓存：%1 个文件，释放 %2 MB")
                .arg(result.removedFiles)
                .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f',
                     1),
            8000);
    });
    connect(dialog, &SettingsDialog::clearWorkbenchCacheRequested, this,
            [this] { clearSessionWorkbenchCache(); });
    connect(dialog, &SettingsDialog::openCacheFolderRequested, this,
            [this] { openSessionCacheFolder({}); });
    connect(dialog, &SettingsDialog::openHtmlCacheFolderRequested, this,
            [this] { openSessionCacheFolder(QStringLiteral("html")); });
    connect(dialog, &SettingsDialog::rebuildSolveCacheRequested, this, [this] {
        if (!m_session)
            return;
        const bool playing =
            m_session->playback() && m_session->playback()->isPlaying();
        m_session->invalidateAnimCache();
        statusBar()->showMessage(
            playing ? QStringLiteral("播放中：求解缓存将在暂停后重新预计算")
                    : QStringLiteral("求解缓存已清空，正在重新预计算"),
            8000);
    });
    connect(dialog, &SettingsDialog::chromeDetectRequested, this,
            [this, dialog] {
                const QString requested = dialog->appSettings().chromeExecutable;
                QString found = html::HtmlBrowser::detectExecutable(requested);
                if (found.isEmpty())
                    found = html::HtmlBrowser::detectExecutable();
                if (found.isEmpty())
                {
                    statusBar()->showMessage(
                        QStringLiteral("未找到 Chrome/Chromium，请手动指定路径"),
                        8000);
                    return;
                }
                next::AppSettings settings = dialog->appSettings();
                settings.chromeExecutable = found;
                dialog->setAppSettings(settings);
                statusBar()->showMessage(
                    QStringLiteral("已检测到 Chrome：%1").arg(found), 8000);
            });
}

void MainWindow::openSessionSettings()
{
    if (!m_session || !m_session->settingsStore())
        return;
    if (m_sessionSettingsDialog)
    {
        m_sessionSettingsDialog->raise();
        m_sessionSettingsDialog->activateWindow();
        return;
    }

    auto* dialog = new SettingsDialog(m_session->settingsStore(), this);
    dialog->setObjectName(QStringLiteral("settingsDialog"));
    m_sessionSettingsDialog = dialog;
    wireSessionSettingsDialog(dialog);
    updateSessionSettingsDialogContext();
    connect(dialog, &QObject::destroyed, this, [this] {
        m_sessionSettingsDialog = nullptr;
    });

    // Legacy interaction: Edit -> Settings is modal.  Apply still takes effect
    // immediately, without restarting the window.
    dialog->exec();
    applySessionSettings(m_session->settingsStore()->load());
    dialog->deleteLater();
    m_sessionSettingsDialog = nullptr;
}

SettingsDialog* MainWindow::openSessionSettingsDialogForTest()
{
    if (!m_session || !m_session->settingsStore())
        return nullptr;
    if (m_sessionSettingsDialog)
        return m_sessionSettingsDialog;

    auto* dialog = new SettingsDialog(m_session->settingsStore(), this);
    dialog->setObjectName(QStringLiteral("settingsDialogTest"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    m_sessionSettingsDialog = dialog;
    wireSessionSettingsDialog(dialog);
    updateSessionSettingsDialogContext();
    connect(dialog, &QObject::destroyed, this, [this] {
        m_sessionSettingsDialog = nullptr;
    });
    dialog->show();
    return dialog;
}

void MainWindow::updateSessionSettingsDialogContext()
{
    if (!m_session || !m_sessionSettingsDialog)
        return;

    QString repositoryPath = m_session->root();
    QString recentCommit;
    if (m_session->document() && m_session->document()->store()
        && m_session->document()->store()->gitEnabled())
    {
        QString error;
        const QVector<store::GitCommit> history =
            m_session->document()->store()->history(1, &error);
        if (!history.isEmpty())
            recentCommit = QStringLiteral("%1 %2")
                               .arg(history.first().hash.left(7),
                                    history.first().message);
    }
    m_sessionSettingsDialog->setGitInfo(repositoryPath, recentCommit);

    mcp2::HttpServer* server = m_session->mcpServer();
    const bool running = server && server->isRunning();
    m_sessionSettingsDialog->setEndpointPath(
        server ? server->endpointFilePath()
               : SettingsDialog::defaultEndpointPath());
    m_sessionSettingsDialog->setMcpStatus(
        running, server ? server->port() : 0,
        server ? QString::fromLatin1(server->token()) : QString(),
        m_session->mcpError(),
        m_session->document() && m_session->document()->store()->gitEnabled());

    refreshSessionCacheStats();
}

void MainWindow::refreshSessionCacheStats()
{
    if (!m_session || !m_sessionSettingsDialog)
        return;

    const QString workbenchRoot =
        sessionCacheFolderPathForTest(QStringLiteral("workbench"));
    next::WorkbenchServices* workbench = m_session->workbench();
    const framecache::CacheUsage workbenchUsage =
        workbench ? workbench->cacheUsage()
                  : framecache::scanDirectory(
                        workbenchRoot,
                        {QStringLiteral("webp"), QStringLiteral("qoi")});
    m_sessionSettingsDialog->setWorkbenchCacheStats(
        m_session->appSettings().workbenchCacheEnabled, workbenchUsage,
        workbenchRoot);

    const QString htmlRoot = sessionCacheFolderPathForTest(QStringLiteral("html"));
    m_sessionSettingsDialog->setHtmlCacheStats(
        framecache::scanDirectory(htmlRoot, {QStringLiteral("webp")}),
        htmlRoot);
}

void MainWindow::clearSessionWorkbenchCache()
{
    if (!m_session)
        return;

    const QString workbenchRoot =
        sessionCacheFolderPathForTest(QStringLiteral("workbench"));
    next::WorkbenchServices* workbench = m_session->workbench();
    const framecache::ClearResult result =
        workbench ? workbench->clearAll()
                  : framecache::clearDirectory(
                        workbenchRoot,
                        {QStringLiteral("webp"), QStringLiteral("qoi")});
    refreshSessionCacheStats();

    if (!result.ok)
    {
        const QString first = result.failedPaths.isEmpty()
                                  ? QStringLiteral("（未知）")
                                  : QDir::toNativeSeparators(
                                        result.failedPaths.first());
        statusBar()->showMessage(
            QStringLiteral(
                "清空工作台缓存失败：%1 个文件未能删除（例：%2），实际释放 %3 MB")
                .arg(result.failedPaths.size())
                .arg(first)
                .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f',
                     1),
            12000);
        qWarning().noquote()
            << QStringLiteral("workbench cache clear failed: %1")
                   .arg(result.failedPaths.join(QStringLiteral(", ")));
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("已清空工作台缓存：%1 帧，释放 %2 MB")
            .arg(result.removedFrames)
            .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f', 1),
        8000);
}

void MainWindow::openSessionCacheFolder(const QString& subdirectory)
{
    if (!m_session)
        return;
    const QString path = sessionCacheFolderPathForTest(subdirectory);
    if (!QFileInfo::exists(path))
    {
        statusBar()->showMessage(
            QStringLiteral("缓存目录尚不存在：%1").arg(path), 8000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString MainWindow::sessionCacheFolderPathForTest(
    const QString& subdirectory) const
{
    if (!m_session)
        return {};
    QString path = QDir(m_session->root()).filePath(QStringLiteral(".cache"));
    if (!subdirectory.isEmpty())
        path = QDir(path).filePath(subdirectory);
    return QDir::cleanPath(path);
}

void MainWindow::applySessionSettings(const next::AppSettings& settings)
{
    if (!m_session)
        return;

    QString error;
    const bool ok = m_session->applyAppSettings(settings, &error);
    const next::AppSettings actual = m_session->appSettings();
    Q_UNUSED(actual);

    updateAutosaveTimer();
    if (m_mcpLog)
    {
        m_mcpLog->setLoggingEnabled(Settings::mcpLogVisible());
        m_mcpLog->setWritesEnabled(m_session->document()
                                   && m_session->document()->store()->gitEnabled());
    }
    // A port change rebuilds the listener; reconnect the new instance.
    wireSessionMcpServer();
    updateSessionMcpStatus();
    refreshSessionHistory();
    updateSessionSettingsDialogContext();
    if (m_viewport)
        m_viewport->refreshSession();

    if (!error.isEmpty())
    {
        if (m_mcpLog)
            m_mcpLog->addSystemEvent(QStringLiteral("设置应用失败"), error);
        statusBar()->showMessage(QStringLiteral("设置应用失败：%1").arg(error),
                                 8000);
    }
    else if (!m_session->mcpError().isEmpty())
    {
        if (m_mcpLog)
            m_mcpLog->addSystemEvent(QStringLiteral("MCP 启动失败"),
                                     m_session->mcpError());
        statusBar()->showMessage(
            QStringLiteral("设置已保存，但 MCP 启动失败：%1")
                .arg(m_session->mcpError()),
            8000);
    }
    else
    {
        statusBar()->showMessage(ok ? QStringLiteral("设置已应用")
                                    : QStringLiteral("设置已保存"),
                                 3000);
    }
    updateSessionStatus();
}

void MainWindow::refreshSessionHistory()
{
    if (!m_sessionHistory)
        return;

    if (!m_session || !m_session->document())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(QStringLiteral("未打开工程"));
        return;
    }
    store::ProjectStore* projectStore = m_session->document()->store();
    if (!projectStore || !projectStore->gitEnabled())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(QStringLiteral("Git 未启用"));
        return;
    }

    QString error;
    const QVector<store::GitCommit> commits = projectStore->history(200, &error);
    if (!error.isEmpty())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(
            QStringLiteral("读取历史失败：%1").arg(error));
        return;
    }
    m_sessionHistory->setCommits(commits);
    m_sessionHistory->setStatus(
        commits.isEmpty() ? QStringLiteral("还没有提交")
                          : QStringLiteral("%1 个提交").arg(commits.size()));
}

bool MainWindow::restoreSessionCommitInternal(const QString& hash,
                                              QString* error)
{
    if (error)
        error->clear();
    if (!m_session || !m_session->document())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    store::ProjectStore* projectStore = m_session->document()->store();
    if (!projectStore || !projectStore->gitEnabled())
    {
        if (error)
            *error = QStringLiteral("Git 未启用");
        return false;
    }

    QString flushError;
    if (m_session->document()->hasPendingCommit())
        m_session->document()->flush(&flushError);
    if (!flushError.isEmpty())
    {
        if (error)
            *error = flushError;
        return false;
    }
    if (!projectStore->restoreCommit(hash, error))
        return false;

    m_session->document()->reloadAfterHistory(error);
    refreshSessionAfterExternalChange();
    refreshSessionHistory();
    return true;
}

void MainWindow::restoreSessionCommit(const QString& hash)
{
    if (!m_session || !m_session->humanEditingEnabled())
        return;
    if (m_sessionHistory)
        m_sessionHistory->setBusy(true);

    QMessageBox box(QMessageBox::Question, QStringLiteral("恢复 Git 历史"),
                    QStringLiteral("恢复到 %1？会丢弃当前未提交文本。")
                        .arg(hash.left(7)),
                    QMessageBox::NoButton, this);
    QPushButton* restore =
        box.addButton(QStringLiteral("恢复"), QMessageBox::AcceptRole);
    QPushButton* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();

    QString error;
    const bool ok = box.clickedButton() == restore
                    && restoreSessionCommitInternal(hash, &error);
    if (m_sessionHistory)
        m_sessionHistory->setBusy(false);
    if (!ok)
    {
        if (!error.isEmpty())
            statusBar()->showMessage(
                QStringLiteral("恢复失败：%1").arg(error), 8000);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("已恢复到 %1").arg(hash.left(7)), 5000);
    if (m_mcpLog)
        m_mcpLog->addSystemEvent(
            QStringLiteral("恢复历史"),
            QStringLiteral("已恢复到 %1").arg(hash.left(7)));
}

bool MainWindow::restoreSessionCommitForTest(const QString& hash,
                                             QString* error)
{
    return restoreSessionCommitInternal(hash, error);
}

void MainWindow::showSessionOnboardingIfNeeded()
{
    if (!m_session || m_session->appSettings().onboardingDone)
        return;

    // Tests must never block on the first-run dialog; the setting stays
    // untouched so the test can drive it explicitly.
    if (testMode())
        return;

    auto* dialog = new next::OnboardingDialog(m_session->appSettings(), this);
    dialog->setObjectName(QStringLiteral("onboardingDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        next::AppSettings settings = m_session->appSettings();
        settings.gitEnabled = dialog->gitEnabled();
        settings.onboardingDone = true;
        applySessionSettings(settings);
    });
    connect(dialog, &QDialog::rejected, this, [this] {
        next::AppSettings settings = m_session->appSettings();
        settings.onboardingDone = true;
        applySessionSettings(settings);
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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

void MainWindow::triggerSessionAutoSaveForTest()
{
    sessionAutoSave();
}

void MainWindow::showMcpLogForTest()
{
    showSessionMcpLog();
}

QString MainWindow::mcpStatusTextForTest() const
{
    return m_mcpStatusButton ? m_mcpStatusButton->text() : QString();
}

bool MainWindow::humanEditingEnabledForTest() const
{
    return m_session ? m_session->humanEditingEnabled() : true;
}

bool MainWindow::openSessionProjectForTest(const QString& path, QString* error)
{
    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    auto session = openSessionProjectWithRecovery(
        path, useGit, m_crashRecoveryHandler, this, error);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
        return false;
    }
    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
    return true;
}

bool MainWindow::saveSessionProject()
{
    if (!m_session)
        return false;

    QString error;
    m_session->flush(&error);
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return false;
    }
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("已保存"), 2000);
    return true;
}

namespace {

SessionCrashRecoveryChoice askSessionCrashRecovery(
    const QVector<store::DirtyText>& dirty, QWidget* parent)
{
    QStringList lines;
    const int shown = std::min<int>(dirty.size(), 20);
    for (int i = 0; i < shown; ++i)
        lines << QStringLiteral("%1（%2）").arg(dirty.at(i).path, dirty.at(i).status);
    if (dirty.size() > shown)
        lines << QStringLiteral("...等 %1 个文件").arg(dirty.size());

    QMessageBox box(
        QMessageBox::Question, QStringLiteral("发现未提交的文本改动"),
        QStringLiteral("工程中有 %1 个未提交的文本文件，可能来自上次异常退出：\n\n"
                       "%2\n\n请选择如何处理这些改动。")
            .arg(dirty.size())
            .arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::NoButton, parent);
    QPushButton* restore =
        box.addButton(QStringLiteral("恢复并打开"), QMessageBox::AcceptRole);
    QPushButton* discard =
        box.addButton(QStringLiteral("丢弃未提交文本"), QMessageBox::DestructiveRole);
    QPushButton* commit =
        box.addButton(QStringLiteral("提交为恢复点"), QMessageBox::ActionRole);
    QPushButton* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(restore);
    box.setEscapeButton(cancel);
    box.exec();

    if (box.clickedButton() == restore)
        return SessionCrashRecoveryChoice::Restore;
    if (box.clickedButton() == discard)
        return SessionCrashRecoveryChoice::Discard;
    if (box.clickedButton() == commit)
        return SessionCrashRecoveryChoice::Commit;
    return SessionCrashRecoveryChoice::Cancel;
}

} // namespace

std::unique_ptr<EditorSession> openSessionProjectWithRecovery(
    const QString& path, bool useGit, const SessionCrashRecoveryHandler& handler,
    QWidget* parent, QString* error)
{
    auto session = EditorSession::openProject(path, error, useGit);
    if (!session)
        return nullptr;

    const QVector<store::DirtyText> dirty =
        session->document()->store()->dirtyText();
    if (dirty.isEmpty())
        return session;

    const SessionCrashRecoveryChoice choice =
        handler ? handler(dirty) : askSessionCrashRecovery(dirty, parent);
    QString recoveryError;
    switch (choice)
    {
    case SessionCrashRecoveryChoice::Restore:
        // The store already loaded the working tree, so keeping it is the
        // restore action.
        break;
    case SessionCrashRecoveryChoice::Discard:
        if (!session->document()->store()->discardDirtyText(&recoveryError)
            || !session->document()->store()->reloadAfterDiscard(&recoveryError))
        {
            if (error)
                *error = QStringLiteral("丢弃未提交文本失败：%1")
                             .arg(recoveryError.isEmpty()
                                      ? QStringLiteral("未知错误")
                                      : recoveryError);
            return nullptr;
        }
        session->document()->reloadAfterHistory(&recoveryError);
        break;
    case SessionCrashRecoveryChoice::Commit:
        if (!session->document()->store()->commitRecoveredText(&recoveryError))
        {
            if (error)
                *error = QStringLiteral("提交未提交文本失败：%1")
                             .arg(recoveryError.isEmpty()
                                      ? QStringLiteral("未知错误")
                                      : recoveryError);
            return nullptr;
        }
        break;
    case SessionCrashRecoveryChoice::Cancel:
        // Not an error: the caller suppresses an empty error message.
        if (error)
            error->clear();
        return nullptr;
    }
    return session;
}

void MainWindow::openSessionProject()
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(
            QStringLiteral("导出正在进行中，暂时不能切换工程。"), 5000);
        return;
    }

    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("打开项目（选择 .pvproj 工程目录）"), QString(),
        QFileDialog::ShowDirsOnly);
    if (path.isEmpty())
        return;

    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    QString error;
    auto session = openSessionProjectWithRecovery(
        path, useGit, m_crashRecoveryHandler, this, &error);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("无法打开项目"), error);
        return;
    }

    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
}

void MainWindow::createSessionProject()
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(
            QStringLiteral("导出正在进行中，暂时不能切换工程。"), 5000);
        return;
    }

    NewProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = dialog.projectPath();
    if (QFileInfo::exists(path))
    {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"),
                             QStringLiteral("路径已存在：%1\n\n请换一个项目名称或位置。")
                                 .arg(path));
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    model::ProjectSettings settings;
    settings.name = dialog.projectName();
    settings.width = dialog.videoWidth();
    settings.height = dialog.videoHeight();
    settings.fps = dialog.frameRate();

    QString error;
    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    auto session = EditorSession::createProject(path, settings, &error, useGit);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
        QMessageBox::warning(this, QStringLiteral("无法创建项目"), error);
        return;
    }

    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
}

void MainWindow::showSessionWindow(std::unique_ptr<EditorSession> session)
{
    auto* window = new MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
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



void MainWindow::exportVideo()
{
    if (!m_session || !m_session->document())
        return;
    if (m_exportRunning)
    {
        statusBar()->showMessage(QStringLiteral("导出正在进行中。"), 4000);
        return;
    }

    if (m_playing)
        togglePlayback();
    if (m_session->playback())
        m_session->playback()->pause();

    QString error;
    m_session->flush(&error);
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("提交工程失败：%1").arg(error));
        return;
    }

    ExportDialog dialog(m_session->project(), m_session->activeCameraUuid(),
                        m_session->root(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    startSessionExport(dialog.cameraUuid(), dialog.startFrame(),
                       dialog.endFrame(), dialog.outputPath(),
                       dialog.includeAudio(), dialog.outputFps(),
                       dialog.resolutionScale(), dialog.crf(),
                       dialog.encoder(), dialog.preset(),
                       dialog.overwriteExisting());
}

void MainWindow::startSessionExport(const Uuid& cameraUuid, int startFrame,
                                    int endFrame, const QString& outputPath,
                                    bool includeAudio, int outputFps,
                                    int resolutionScale, int crf,
                                    const QString& encoder,
                                    const QString& preset, bool overwrite)
{
    if (!m_session || !m_session->document() || m_exportRunning)
        return;
    if (outputPath.trimmed().isEmpty())
        return;

    auto* media = m_session->media();
    if (media)
    {
        QString htmlError;
        media->start(&htmlError);
        if (!htmlError.isEmpty())
            qWarning().noquote() << "export HTML service:" << htmlError;
    }

    auto project = std::make_shared<model::Project>(m_session->project());
    auto assets = std::make_shared<assets::AssetStore>(m_session->assets());
    const int projectFps = std::max(1, project->settings.fps);
    const int selectedFps = outputFps > 0 ? outputFps : projectFps;
    const int scale = resolutionScale == 2 || resolutionScale == 4
                          ? resolutionScale
                          : 1;
    // H.264 needs even dimensions; mirror export2's rounding so the HTML final
    // frames are rendered at exactly the encoded size.
    const QSize exportSize(
        std::max(2, (project->settings.width * scale) & ~1),
        std::max(2, (project->settings.height * scale) & ~1));

    export2::Options options;
    options.outputPath = outputPath;
    options.cameraUuid = cameraUuid;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    options.outputSize = QSize(); // project size * resolutionScale
    options.fps = selectedFps;
    options.outputFps = selectedFps;
    options.resolutionScale = scale;
    options.crf = std::clamp(crf, 0, 51);
    options.encoder = encoder;
    options.preset = preset;
    options.includeAudio = includeAudio;
    options.overwrite = overwrite;
    options.videoFrame = [media](const Uuid& entityUuid, double projectFrame) {
        return media ? media->exportVideoFrame(entityUuid, projectFrame)
                     : QImage();
    };
    options.htmlFrame = [media, exportSize](const Uuid& entityUuid,
                                            double projectFrame) {
        return media ? media->exportHtmlFrame(entityUuid, projectFrame,
                                              exportSize)
                     : QImage();
    };

    if (m_exportOptionsProbe)
        m_exportOptionsProbe(options);

    int rangeStart = startFrame;
    int rangeEnd = endFrame;
    if (rangeEnd <= rangeStart)
    {
        const auto camera = project->entities.constFind(cameraUuid);
        if (camera != project->entities.constEnd())
        {
            rangeStart = camera->start;
            rangeEnd = camera->end;
        }
    }
    const double durationSeconds =
        std::max(0, rangeEnd - rangeStart) / double(projectFps);
    const int totalFrames =
        std::max(1, int(std::llround(durationSeconds * double(selectedFps))));

    m_exportCancelled = std::make_shared<std::atomic_bool>(false);
    m_exportFrame = std::make_shared<std::atomic_int>(0);
    m_exportTotal = std::make_shared<std::atomic_int>(totalFrames);
    m_exportEta = std::make_shared<std::atomic<double>>(0.0);

    auto* progress = new ExportProgressDialog(this);
    m_exportProgress = progress;
    progress->setProgress(0, totalFrames, 0.0);
    connect(progress, &ExportProgressDialog::cancelRequested, this, [this] {
        if (m_exportCancelled)
            m_exportCancelled->store(true);
    });
    progress->show();
    // Let the preview window paint before the worker starts decoding.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    auto* watcher = new QFutureWatcher<export2::Result>(this);
    m_exportWatcher = watcher;
    auto* timer = new QTimer(this);
    m_exportTimer = timer;
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!m_exportProgress || !m_exportFrame || !m_exportTotal || !m_exportEta)
            return;
        const int frame = m_exportFrame->load();
        const int total = std::max(1, m_exportTotal->load());
        const double eta = m_exportEta->load();
        m_exportProgress->setProgress(frame, total, eta);
        if (m_exportProgressHandler)
            m_exportProgressHandler(frame, total, eta);
    });
    timer->start();

    connect(watcher, &QFutureWatcher<export2::Result>::finished, this,
            [this, watcher, progress, timer, outputPath] {
                timer->stop();
                timer->deleteLater();
                if (m_exportTimer == timer)
                    m_exportTimer = nullptr;
                m_exportRunning = false;

                const export2::Result result = watcher->result();
                progress->finish();
                progress->deleteLater();
                if (m_exportProgress == progress)
                    m_exportProgress = nullptr;
                watcher->deleteLater();
                if (m_exportWatcher == watcher)
                    m_exportWatcher = nullptr;

                // Restore the panels, but keep an AI write lock if one is
                // still active.
                refreshSessionAiLock();
                refreshSession();

                if (m_exportFinishedHandler)
                    m_exportFinishedHandler(result.ok, result.error);
                if (m_suppressExportCompletionDialog)
                    return;

                if (result.ok)
                {
                    QString text = QStringLiteral("已导出 %1 帧到\n%2")
                                       .arg(result.framesWritten)
                                       .arg(QDir::toNativeSeparators(outputPath));
                    if (!result.warnings.isEmpty())
                        text += QStringLiteral("\n\n") + result.warnings.join('\n');
                    const QString folder = QFileInfo(outputPath).absolutePath();

                    QMessageBox box(QMessageBox::Information,
                                    QStringLiteral("导出完成"), text,
                                    QMessageBox::NoButton, this);
                    QPushButton* openFolderButton =
                        box.addButton(QStringLiteral("打开文件夹"),
                                      QMessageBox::ActionRole);
                    QPushButton* okButton =
                        box.addButton(QStringLiteral("确定"),
                                      QMessageBox::AcceptRole);
                    box.setDefaultButton(okButton);
                    box.exec();
                    if (box.clickedButton() == openFolderButton)
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(folder));
                }
                else if (result.error == QStringLiteral("cancelled"))
                {
                    statusBar()->showMessage(QStringLiteral("导出已取消"),
                                             5000);
                }
                else
                {
                    QMessageBox::warning(
                        this, QStringLiteral("导出失败"),
                        friendlyExportError(result.error));
                }
            });

    m_exportRunning = true;
    setHumanEditingEnabled(false);
    updateSessionActionStates();

    watcher->setFuture(QtConcurrent::run(
        [project, assets, options, media, cancelled = m_exportCancelled,
         frame = m_exportFrame, total = m_exportTotal, eta = m_exportEta] {
            export2::Result result = export2::exportVideo(
                *project, *assets, options,
                [frame, total, eta, cancelled](int done, int all,
                                               double seconds) {
                    frame->store(done);
                    total->store(all);
                    eta->store(seconds);
                    return !cancelled->load();
                });
            // The final HTML browser was created on this worker thread;
            // release it here so it is never destroyed from the UI thread.
            if (media)
                media->stopExportHtml();
            return result;
        }));
}

void MainWindow::cancelSessionExportForTest()
{
    if (m_exportCancelled)
        m_exportCancelled->store(true);
}

void MainWindow::startSessionExportForTest(
    const Uuid& cameraUuid, int startFrame, int endFrame,
    const QString& outputPath, bool includeAudio, int outputFps,
    int resolutionScale, int crf, const QString& encoder,
    const QString& preset, bool overwrite)
{
    startSessionExport(cameraUuid, startFrame, endFrame, outputPath, includeAudio,
                       outputFps, resolutionScale, crf, encoder, preset,
                       overwrite);
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


void MainWindow::replaceSessionAssetFromDialog(const QString& uuid)
{
    if (!m_session || !m_session->document())
        return;

    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return;

    const model::Content& content = it.value().content;
    if (content.kind != ContentKind::Asset && content.kind != ContentKind::Empty)
    {
        const QString message =
            content.kind == ContentKind::Camera
                ? QStringLiteral("摄像机不能更换素材")
                : QStringLiteral("生成内容实体不能直接换素材，请先清空内容");
        QMessageBox::warning(this, QStringLiteral("更换素材失败"), message);
        return;
    }

    const AssetType currentType =
        content.kind == ContentKind::Asset ? content.asset.type : AssetType::Image;
    const AssetHash currentHash =
        content.kind == ContentKind::Asset ? content.asset.hash : AssetHash();
    const QString hash = AssetLibraryPanel::pickAsset(
        this, m_session, content.kind, currentType, currentHash);
    if (hash.isEmpty())
        return;

    QString error;
    if (!m_session->editActions()->replaceAsset(uuid, hash, &error))
    {
        QMessageBox::warning(this, QStringLiteral("更换素材失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已更换素材"), 3000);
    if (m_library)
        m_library->enqueueMissingVideoExtractions();
    if (m_viewport)
        m_viewport->refreshSession();
    if (m_monitor)
        m_monitor->setProjectChanged();
}

bool MainWindow::replaceAssetForTest(const QString& uuid, const QString& hash,
                                     QString* error)
{
    if (!m_session || !m_session->editActions())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    return m_session->editActions()->replaceAsset(uuid, hash, error);
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


QAction* MainWindow::createPerfMonitorWindowAction()
{
    auto* action = new QAction(QStringLiteral("性能监视"), this);
    action->setObjectName(QStringLiteral("perfMonitorWindowAction"));
    action->setCheckable(true);
    connect(action, &QAction::toggled, this,
            &MainWindow::showPerfMonitorWindow);
    return action;
}

void MainWindow::showPerfMonitorWindow(bool show)
{
    if (!show)
    {
        if (m_perfWindow)
            m_perfWindow->hide();
        return;
    }

    if (!m_perfWindow)
    {
        m_perfWindow = new PerfMonitorWindow(this);
        connect(m_perfWindow, &PerfMonitorWindow::closed, this, [this] {
            if (auto* action = findChild<QAction*>(
                    QStringLiteral("perfMonitorWindowAction")))
            {
                const QSignalBlocker blocker(action);
                action->setChecked(false);
            }
        });
    }

    if (m_session)
        m_perfWindow->setCacheRoot(
            QDir(m_session->root()).filePath(QStringLiteral(".cache/workbench")));
    else
        m_perfWindow->setCacheRoot(QString());
    m_perfWindow->show();
    m_perfWindow->raise();
    m_perfWindow->activateWindow();
}


void MainWindow::arrangeMcpLog()
{
    if (!m_mcpLog || !m_monitor || !m_viewport || !m_mcpLog->isVisible()
        || m_mcpLog->isDetached())
        return;

    const QSize viewportSize = m_viewport->size();
    QSize logSize = m_mcpLog->size();
    if (logSize.width() < 240 || logSize.height() < 120)
        logSize = QSize(420, 280);
    const int gap = 8;
    const int margin = 12;
    QRect target;

    if (m_monitor->isVisible() && !m_monitor->isDetached())
    {
        const QRect monitor = m_monitor->geometry();
        if (monitor.left() - gap - logSize.width() >= margin)
        {
            target = QRect(monitor.left() - gap - logSize.width(), monitor.top(),
                           logSize.width(),
                           std::max(logSize.height(), monitor.height()));
        }
        else if (monitor.right() + gap + logSize.width() + margin
                 <= viewportSize.width())
        {
            target = QRect(monitor.right() + gap, monitor.top(), logSize.width(),
                           std::max(logSize.height(), monitor.height()));
        }
        else
        {
            target = QRect(monitor.left(),
                           std::max(margin,
                                    monitor.top() - gap - logSize.height()),
                           logSize.width(), logSize.height());
        }
    }
    else
    {
        target = QRect(viewportSize.width() - logSize.width() - margin, margin,
                       logSize.width(), logSize.height());
    }

    target.moveLeft(std::clamp(target.left(), 0,
                              std::max(0, viewportSize.width() - target.width())));
    target.moveTop(std::clamp(target.top(), 0,
                             std::max(0, viewportSize.height() - target.height())));
    m_mcpLog->setGeometry(target);
    m_mcpLog->raise();
}


} // namespace pvf
