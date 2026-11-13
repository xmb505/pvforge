#include "MainWindow.h"

#include "EditorSession.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "next/app/ProjectDocument.h"
#include "next/app/PlaybackController.h"
#include "ui/CameraMonitor.h"
#include "ui/McpLogPanel.h"
#include "ui/PerfMonitorWindow.h"
#include "ui/PropertiesPanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"
#include "ui/panels/WorkbenchPanelHost.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

#include <algorithm>

namespace pvf {

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

    // Workbench panel: show/hide plus dock/floating mode.  The same actions
    // are added to the panel so the shortcuts keep working while it is a
    // separate native window.
    auto* workbenchAction =
        new QAction(Lang::t(QStringLiteral("panel.workbench"),
                            QStringLiteral("工作台")),
                    this);
    workbenchAction->setObjectName(QStringLiteral("workbenchAction"));
    workbenchAction->setCheckable(true);
    workbenchAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    workbenchAction->setChecked(m_workbench && m_workbench->isVisible());
    connect(workbenchAction, &QAction::toggled, this, [this](bool on) {
        if (!m_workbenchHost)
            return;
        if (on)
            m_workbenchHost->showPanel();
        else
            m_workbenchHost->hidePanel();
    });
    if (m_workbenchHost)
    {
        connect(m_workbenchHost, &WorkbenchPanelHost::visibleChanged, this,
                [workbenchAction](bool visible) {
                    const QSignalBlocker blocker(workbenchAction);
                    workbenchAction->setChecked(visible);
                });
    }
    addAction(workbenchAction);
    viewMenu->addAction(workbenchAction);

    auto* workbenchFloatAction =
        new QAction(QStringLiteral("浮出工作台"), this);
    workbenchFloatAction->setObjectName(QStringLiteral("workbenchFloatAction"));
    workbenchFloatAction->setCheckable(true);
    workbenchFloatAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    workbenchFloatAction->setChecked(m_workbenchHost
                                     && m_workbenchHost->isFloating());
    workbenchFloatAction->setText(m_workbenchHost
                                          && m_workbenchHost->isFloating()
                                      ? QStringLiteral("停靠工作台")
                                      : QStringLiteral("浮出工作台"));
    connect(workbenchFloatAction, &QAction::toggled, this, [this](bool on) {
        if (m_workbenchHost)
            m_workbenchHost->setFloating(on);
    });
    if (m_workbenchHost)
    {
        connect(m_workbenchHost, &WorkbenchPanelHost::floatingChanged, this,
                [workbenchFloatAction](bool floating) {
                    const QSignalBlocker blocker(workbenchFloatAction);
                    workbenchFloatAction->setChecked(floating);
                    workbenchFloatAction->setText(
                        floating ? QStringLiteral("停靠工作台")
                                 : QStringLiteral("浮出工作台"));
                });
    }
    addAction(workbenchFloatAction);
    viewMenu->addAction(workbenchFloatAction);
    if (m_workbench)
    {
        m_workbench->addAction(workbenchAction);
        m_workbench->addAction(workbenchFloatAction);
    }

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
