#include "MainWindow.h"

#include "core/Commands.h"
#include "core/Camera.h"
#include "core/Lang.h"
#include "audio/AudioEngine.h"
#include "media/MediaImporter.h"
#include "media/VideoProxy.h"
#include "core/Settings.h"
#include "SettingsDialog.h"
#include "media/VideoPlayer.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CameraMonitor.h"
#include "ui/CollapsibleSection.h"
#include "ui/EnvironmentPanel.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
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

MainWindow::MainWindow(Project* project, QWidget* parent)
    : QMainWindow(parent)
    , m_project(project)
{
    m_project->setParent(this);
    resize(1440, 900);
    setMinimumSize(1000, 620);

    m_history = new History(this);
    m_audioEngine = new AudioEngine(m_project, this);
    m_project->autoKey = Settings::autoKeyDefault();
    m_project->packAssets = Settings::packAssetsDefault();
    m_viewport = new Viewport(m_project, this);
    m_viewport->setSnapPixels(Settings::snapPixels());
    m_viewport->setEdgeCenterSnap(Settings::edgeCenterSnap());
    m_props = new PropertiesPanel(m_project, this);
    m_library = new AssetLibraryPanel(m_project, this);
    m_projectProps = new ProjectPropertiesPanel(m_project, this);
    m_environment = new EnvironmentPanel(m_project, this);
    m_timeline = new TimelinePanel(m_project, this);
    m_workbench = new WorkbenchPanel(m_project, m_viewport);
    m_workbench->setIgnoreMouseWidget(m_viewport->workbenchButton());
    m_workbench->hide();
    m_monitor = new CameraMonitor(m_project, m_viewport, m_viewport);
    m_monitor->setFrame(0);
    m_monitor->setVisible(Settings::monitorVisible());

    // "Project properties" tab: collapsible categories, one scroll column.
    m_projectTab = new QWidget(this);
    auto* projectScroll = new QScrollArea(m_projectTab);
    projectScroll->setObjectName(QStringLiteral("projectScroll"));
    projectScroll->setWidgetResizable(true);
    projectScroll->setFrameShape(QFrame::NoFrame);
    projectScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* projectContent = new QWidget(projectScroll);
    auto* projectLayout = new QVBoxLayout(projectContent);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectLayout->setSpacing(0);

    auto* settingsSection =
        new CollapsibleSection(Lang::t(QStringLiteral("section.projectSettings"), QStringLiteral("项目设置")), m_projectProps, projectContent);
    auto* environmentSection =
        new CollapsibleSection(Lang::t(QStringLiteral("section.environment"), QStringLiteral("环境设置")), m_environment, projectContent);
    auto* librarySection =
        new CollapsibleSection(Lang::t(QStringLiteral("panel.library"), QStringLiteral("素材库")), m_library, projectContent);
    settingsSection->setExpanded(true);
    environmentSection->setExpanded(false);
    librarySection->setExpanded(true);

    projectLayout->addWidget(settingsSection);
    projectLayout->addWidget(environmentSection);
    projectLayout->addWidget(librarySection);
    projectLayout->addStretch(1);
    projectScroll->setWidget(projectContent);

    auto* projectTabLayout = new QVBoxLayout(m_projectTab);
    projectTabLayout->setContentsMargins(0, 0, 0, 0);
    projectTabLayout->addWidget(projectScroll);

    // Entity properties tab, added on demand when something is selected.
    m_objectTab = new QScrollArea(this);
    m_objectTab->setWidgetResizable(true);
    m_objectTab->setFrameShape(QFrame::NoFrame);
    m_objectTab->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_objectTab->setWidget(m_props);
    // Not in any layout until it is added as a tab; without this it sits
    // visible at (0, 0) on top of the menu bar and toolbar.
    m_objectTab->hide();

    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_rightTabs->setMinimumWidth(300);
    m_rightTabs->setMaximumWidth(440);
    m_rightTabs->addTab(m_projectTab, Lang::t(QStringLiteral("panel.projectTab"), QStringLiteral("项目属性")));

    m_viewport->setMinimumWidth(480);

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(m_viewport);
    top->addWidget(m_rightTabs);
    top->setChildrenCollapsible(false);
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 0);
    top->setSizes({1100, 320});

    auto* mainSplit = new QSplitter(Qt::Vertical, this);
    mainSplit->addWidget(top);
    mainSplit->addWidget(m_timeline);
    mainSplit->setChildrenCollapsible(false);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setSizes({640, 220});

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mainSplit);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusInfo"));
    statusBar()->addPermanentWidget(m_status);

    m_importProgress = new QProgressBar(this);
    m_importProgress->setObjectName(QStringLiteral("importProgress"));
    m_importProgress->setRange(0, 0);
    m_importProgress->setFixedWidth(120);
    m_importProgress->setTextVisible(false);
    m_importProgress->hide();
    statusBar()->addPermanentWidget(m_importProgress);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onPlayTick);

    m_scrubTimer = new QTimer(this);
    m_scrubTimer->setSingleShot(true);
    m_scrubTimer->setInterval(60);
    connect(m_scrubTimer, &QTimer::timeout, this, [this] {
        if (m_playing || !Settings::audioScrub())
            return;
        m_audioEngine->scrub(m_frame / double(std::max(1, m_project->fps)));
        m_scrubClock.restart();
    });

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autoSave);
    updateAutosaveTimer();

    connect(m_viewport, &Viewport::entitySelected, this, &MainWindow::setSelectedEntity);
    connect(m_viewport, &Viewport::valueEdited, this, &MainWindow::applyValue);
    connect(m_viewport, &Viewport::positionEdited, this, &MainWindow::applyPosition);
    connect(m_viewport, &Viewport::scaleEdited, this, &MainWindow::applyScale);
    connect(m_viewport, &Viewport::pivotEdited, this, &MainWindow::applyPivot);
    connect(m_viewport, &Viewport::cameraEdited, this, &MainWindow::applyCamera);
    connect(m_viewport, &Viewport::comicTipEdited, this, &MainWindow::applyComicTip);
    connect(m_viewport, &Viewport::workbenchRequested, this, &MainWindow::toggleWorkbench);
    connect(m_viewport, &Viewport::viewChanged, m_monitor,
            &CameraMonitor::updateFromViewport);
    connect(m_monitor, &CameraMonitor::cameraActivated, this,
            &MainWindow::setSelectedEntity);
    connect(m_monitor, &CameraMonitor::visibilityChanged, this, [this](bool visible) {
        if (auto* action = findChild<QAction*>(QStringLiteral("monitorAction")))
        {
            const QSignalBlocker blocker(action);
            action->setChecked(visible);
        }
    });
    connect(m_props, &PropertiesPanel::valueEdited, this, &MainWindow::applyValue);
    connect(m_props, &PropertiesPanel::pivotEdited, this, &MainWindow::applyPivot);
    connect(m_props, &PropertiesPanel::dataEdited, this, &MainWindow::applyEntityData);
    connect(m_props, &PropertiesPanel::keyframeToggled, this,
            &MainWindow::onPropertyKeyframeToggled);
    connect(m_props, &PropertiesPanel::parentEdited, this, &MainWindow::onParentEdited);
    connect(m_props, &PropertiesPanel::keyframeEasingEdited, this,
            &MainWindow::onKeyframeEasingEdited);
    connect(m_timeline, &TimelinePanel::frameRequested, this, &MainWindow::setFrame);
    connect(m_timeline, &TimelinePanel::scrubStarted, this, [this] {
        if (m_playing)
            togglePlayback();
    });
    connect(m_timeline, &TimelinePanel::displayFramesChanged, this,
            [this](bool frames) {
                Settings::setTimelineDisplayFrames(frames);
                if (auto* action = findChild<QAction*>(
                        frames ? QStringLiteral("timelineFramesAction")
                               : QStringLiteral("timelineSecondsAction")))
                    action->setChecked(true);
                updateStatus();
            });
    connect(m_timeline, &TimelinePanel::entitySelected, this, &MainWindow::setSelectedEntity);
    connect(m_library, &AssetLibraryPanel::entityCreated, this, &MainWindow::setSelectedEntity);
    connect(m_projectProps, &ProjectPropertiesPanel::settingsChanged, this, [this] {
        updateWindowTitle();
        setFrame(m_frame);
    });
    connect(m_project, &Project::changed, this, &MainWindow::refreshAll);
    connect(m_workbench, &WorkbenchPanel::created, this, &MainWindow::onEntityCreated);
    connect(m_library, &AssetLibraryPanel::entityCreated, this, &MainWindow::onEntityCreated);
    connect(m_timeline, &TimelinePanel::entityFlagChanged, this,
            &MainWindow::onEntityFlagChanged);
    connect(m_timeline, &TimelinePanel::deleteKeyframesRequested, this,
            &MainWindow::onDeleteKeyframesRequested);
    connect(m_timeline, &TimelinePanel::keyframeEasingRequested, this,
            &MainWindow::onKeyframeEasingRequested);
    connect(m_timeline, &TimelinePanel::entityRenamed, this, &MainWindow::onEntityRenamed);
    connect(m_timeline, &TimelinePanel::replaceSourceRequested, this,
            &MainWindow::replaceSourceFromDialog);
    connect(m_timeline, &TimelinePanel::copyEntityRequested, this,
            &MainWindow::copyEntityToClipboard);
    connect(m_timeline, &TimelinePanel::duplicateEntityRequested, this,
            &MainWindow::duplicateEntity);
    connect(m_timeline, &TimelinePanel::pasteEntityRequested, this,
            &MainWindow::pasteEntity);
    connect(m_timeline, &TimelinePanel::groupIntoFolderRequested, this,
            &MainWindow::groupEntitiesIntoFolder);
    connect(m_props, &PropertiesPanel::replaceSourceRequested, this,
            &MainWindow::replaceSourceFromDialog);
    connect(m_timeline, &TimelinePanel::audioMoved, this, &MainWindow::onAudioMoved);
    connect(m_timeline, &TimelinePanel::entityMoveRequested, this,
            &MainWindow::onEntityMoveRequested);
    connect(m_timeline, &TimelinePanel::entitiesMoveRequested, this,
            &MainWindow::onEntitiesMoveRequested);
    connect(m_timeline, &TimelinePanel::keyframesMoveRequested, this,
            &MainWindow::onKeyframesMoveRequested);
    connect(m_timeline, &TimelinePanel::folderCreateRequested, this,
            &MainWindow::onCreateFolderRequested);
    connect(m_history, &History::changed, this, [this] {
        m_undoAction->setEnabled(m_history->canUndo());
        m_redoAction->setEnabled(m_history->canRedo());
        m_undoAction->setText(m_history->canUndo()
                                  ? QStringLiteral("撤销 ") + m_history->undoText()
                                  : Lang::t(QStringLiteral("action.undo"), QStringLiteral("撤销")));
        m_redoAction->setText(m_history->canRedo()
                                  ? QStringLiteral("重做 ") + m_history->redoText()
                                  : Lang::t(QStringLiteral("action.redo"), QStringLiteral("重做")));
    });

    createActions();
    createMenus();
    createToolBar();

    m_lastScrubFrame = 0;   // don't preview audio just from opening a project
    setFrame(m_project->filePath.isEmpty()
                 ? 0
                 : Settings::projectFrame(m_project->filePath));
    updateWindowTitle();

    m_proxyGenerator = new VideoProxyGenerator(this);
    connect(m_proxyGenerator, &VideoProxyGenerator::started, this,
            [this](const QString& path) {
                statusBar()->showMessage(
                    QStringLiteral("正在后台生成视频预览：%1")
                        .arg(QFileInfo(path).fileName()));
            });
    connect(m_proxyGenerator, &VideoProxyGenerator::finished, this,
            [this](const QString& path, bool success) {
                if (success)
                {
                    // Drop decoders still attached to the original file and
                    // show the current frame from the proxy immediately.
                    m_videoPlayer.clear();
                    setFrame(m_frame);
                    m_viewport->update();
                    statusBar()->showMessage(
                        QStringLiteral("视频预览已就绪：%1")
                            .arg(QFileInfo(path).fileName()),
                        3000);
                }
                else
                {
                    statusBar()->showMessage(
                        QStringLiteral("视频预览生成失败，继续使用原视频"), 3000);
                }
            });
    ensureProxies();
}

void MainWindow::createActions()
{
    m_undoAction = new QAction(Lang::t(QStringLiteral("action.undo"), QStringLiteral("撤销")), this);
    m_undoAction->setObjectName(QStringLiteral("undoAction"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::onUndo);
    addAction(m_undoAction);

    m_redoAction = new QAction(Lang::t(QStringLiteral("action.redo"), QStringLiteral("重做")), this);
    m_redoAction->setObjectName(QStringLiteral("redoAction"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::onRedo);
    addAction(m_redoAction);

    m_playAction = new QAction(Lang::t(QStringLiteral("action.play"), QStringLiteral("播放")), this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);
    addAction(m_playAction);

    auto* homeAction = new QAction(Lang::t(QStringLiteral("action.home"), QStringLiteral("回到开头")), this);
    homeAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(homeAction, &QAction::triggered, this, [this] { setFrame(0); });
    addAction(homeAction);
    m_homeAction = homeAction;

    auto* endAction = new QAction(Lang::t(QStringLiteral("action.end"), QStringLiteral("跳到结尾")), this);
    endAction->setShortcut(QKeySequence(Qt::Key_End));
    connect(endAction, &QAction::triggered, this, [this] { setFrame(m_project->timelineLength()); });
    addAction(endAction);
    m_endAction = endAction;

    auto* fitAction = new QAction(Lang::t(QStringLiteral("action.fitWindow"), QStringLiteral("适配窗口")), this);
    fitAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(fitAction, &QAction::triggered, this, [this] { m_viewport->resetView(); });
    addAction(fitAction);
    m_fitAction = fitAction;

    m_autoKeyAction = new QAction(Lang::t(QStringLiteral("action.autoKey"), QStringLiteral("自动关键帧")), this);
    m_autoKeyAction->setCheckable(true);
    m_autoKeyAction->setChecked(m_project->autoKey);
    connect(m_autoKeyAction, &QAction::toggled, this, [this](bool on) {
        m_project->autoKey = on;
    });

    m_quitAction = new QAction(Lang::t(QStringLiteral("action.quit"), QStringLiteral("退出")), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
    addAction(m_quitAction);

    m_saveAction = new QAction(Lang::t(QStringLiteral("action.save"), QStringLiteral("保存")), this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    addAction(m_saveAction);

    m_saveAsAction = new QAction(Lang::t(QStringLiteral("action.saveAs"), QStringLiteral("另存为…")), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    addAction(m_saveAsAction);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.file"), QStringLiteral("文件")));
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    auto* assetMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.assets"), QStringLiteral("素材")));
    m_packAssetsAction = new QAction(Lang::t(QStringLiteral("action.packAssets"), QStringLiteral("全部打包到项目")), this);
    m_packAssetsAction->setCheckable(true);
    m_packAssetsAction->setChecked(m_project->packAssets);
    m_packAssetsAction->setToolTip(
        QStringLiteral("开启后，新导入的图片和视频会自动复制进工程文件夹"));
    connect(m_packAssetsAction, &QAction::toggled, this, [this](bool on) {
        m_project->packAssets = on;
        if (!on)
        {
            statusBar()->showMessage(QStringLiteral("已关闭自动打包"), 2000);
            return;
        }
        statusBar()->showMessage(
            m_project->filePath.isEmpty()
                ? QStringLiteral("已开启：请先保存工程，素材才会复制进工程目录")
                : QStringLiteral("已开启：新导入的素材会自动复制进工程目录"),
            4000);
    });
    assetMenu->addAction(m_packAssetsAction);
    assetMenu->addAction(Lang::t(QStringLiteral("action.packAll"), QStringLiteral("立即打包所有素材")), this, &MainWindow::packAllAssets);
    assetMenu->addAction(Lang::t(QStringLiteral("action.openFolder"), QStringLiteral("打开工程文件夹")), this, [this] {
        if (m_project->filePath.isEmpty())
            return;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(m_project->filePath).absolutePath()));
    });

    auto* editMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.edit"), QStringLiteral("编辑")));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_autoKeyAction);
    editMenu->addSeparator();
    auto* deleteAction = new QAction(Lang::t(QStringLiteral("action.deleteEntity"), QStringLiteral("删除实体")), this);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedEntity);
    editMenu->addAction(deleteAction);
    editMenu->addSeparator();
    auto* copyEntityAction = new QAction(QStringLiteral("复制实体"), this);
    copyEntityAction->setShortcut(QKeySequence::Copy);
    connect(copyEntityAction, &QAction::triggered, this, &MainWindow::copySelectedEntity);
    editMenu->addAction(copyEntityAction);
    auto* pasteEntityAction = new QAction(QStringLiteral("粘贴实体"), this);
    pasteEntityAction->setShortcut(QKeySequence::Paste);
    connect(pasteEntityAction, &QAction::triggered, this, &MainWindow::pasteEntity);
    editMenu->addAction(pasteEntityAction);
    auto* duplicateEntityAction = new QAction(QStringLiteral("原地复制"), this);
    duplicateEntityAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(duplicateEntityAction, &QAction::triggered, this,
            [this] { duplicateEntity(m_selected); });
    editMenu->addAction(duplicateEntityAction);
    editMenu->addSeparator();
    auto* settingsAction =
        new QAction(Lang::t(QStringLiteral("action.settings"), QStringLiteral("设置…")), this);
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            m_viewport->setSnapPixels(Settings::snapPixels());
            m_viewport->setEdgeCenterSnap(Settings::edgeCenterSnap());
            updateAutosaveTimer();
            statusBar()->showMessage(QStringLiteral("设置已保存，语言等改动重启后生效"), 3000);
        }
    });
    editMenu->addAction(settingsAction);

    auto* playMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.play"), Lang::t(QStringLiteral("action.play"), QStringLiteral("播放"))));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_homeAction);
    playMenu->addAction(m_endAction);

    auto* viewMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.view"), QStringLiteral("视图")));
    auto* cameraFrameAction = new QAction(
        Lang::t(QStringLiteral("action.cameraFrame"), QStringLiteral("显示摄像机取景框")),
        this);
    cameraFrameAction->setObjectName(QStringLiteral("cameraFrameAction"));
    cameraFrameAction->setCheckable(true);
    cameraFrameAction->setChecked(Settings::showCameraFrame());
    connect(cameraFrameAction, &QAction::toggled, this, [this](bool on) {
        Settings::setShowCameraFrame(on);
        m_viewport->setCameraFrameVisible(on);
    });
    viewMenu->addAction(cameraFrameAction);

    auto* audioScrubAction = new QAction(
        Lang::t(QStringLiteral("action.audioScrub"), QStringLiteral("音频擦洗")), this);
    audioScrubAction->setObjectName(QStringLiteral("audioScrubAction"));
    audioScrubAction->setCheckable(true);
    audioScrubAction->setChecked(Settings::audioScrub());
    audioScrubAction->setToolTip(QStringLiteral("拖动播放头或逐帧时播放短音频"));
    connect(audioScrubAction, &QAction::toggled, this, [this](bool on) {
        Settings::setAudioScrub(on);
        if (!on)
            m_scrubTimer->stop();
    });
    viewMenu->addAction(audioScrubAction);

    auto* monitorAction =
        new QAction(Lang::t(QStringLiteral("action.monitor"), QStringLiteral("摄像机监视器")),
                    this);
    monitorAction->setObjectName(QStringLiteral("monitorAction"));
    monitorAction->setCheckable(true);
    monitorAction->setChecked(Settings::monitorVisible());
    connect(monitorAction, &QAction::toggled, this, [this](bool on) {
        if (m_monitor)
            m_monitor->setVisibleFromMenu(on);
    });
    viewMenu->addAction(monitorAction);
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();

    auto* secondsAction =
        new QAction(Lang::t(QStringLiteral("action.timelineSeconds"),
                            QStringLiteral("时间轴显示秒")),
                    this);
    secondsAction->setObjectName(QStringLiteral("timelineSecondsAction"));
    secondsAction->setCheckable(true);
    secondsAction->setChecked(!Settings::timelineDisplayFrames());

    auto* framesAction =
        new QAction(Lang::t(QStringLiteral("action.timelineFrames"),
                            QStringLiteral("时间轴显示帧")),
                    this);
    framesAction->setObjectName(QStringLiteral("timelineFramesAction"));
    framesAction->setCheckable(true);
    framesAction->setChecked(Settings::timelineDisplayFrames());

    auto* displayGroup = new QActionGroup(this);
    displayGroup->setExclusive(true);
    displayGroup->addAction(secondsAction);
    displayGroup->addAction(framesAction);
    viewMenu->addAction(secondsAction);
    viewMenu->addAction(framesAction);

    auto setDisplayFrames = [this](bool frames) {
        Settings::setTimelineDisplayFrames(frames);
        m_timeline->setDisplayFrames(frames);
        updateStatus();
    };
    connect(secondsAction, &QAction::triggered, this,
            [setDisplayFrames] { setDisplayFrames(false); });
    connect(framesAction, &QAction::triggered, this,
            [setDisplayFrames] { setDisplayFrames(true); });

    menuBar()->setFixedHeight(menuBar()->sizeHint().height());

    auto* helpMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.help"), QStringLiteral("帮助")));
    helpMenu->addAction(Lang::t(QStringLiteral("action.about"), QStringLiteral("关于 PVForge")), this, [this] {
        SettingsDialog dialog(this);
        dialog.showAboutPage();
        dialog.exec();
    });
}

void MainWindow::createToolBar()
{
    auto* bar = addToolBar(QStringLiteral("主工具栏"));
    bar->setMovable(false);
    bar->addAction(m_playAction);
    bar->addSeparator();
    bar->addAction(m_autoKeyAction);
    bar->setFixedHeight(bar->sizeHint().height());
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
    const QString status = m_project->modified
                               ? Lang::t(QStringLiteral("title.unsaved"), QStringLiteral("未保存"))
                               : Lang::t(QStringLiteral("title.saved"), QStringLiteral("已保存"));
    setWindowTitle(QStringLiteral("PVForge — %1 [%2]").arg(m_project->name, status));
}

void MainWindow::deleteSelectedEntity()
{
    if (m_selected == kInvalidEntity)
        return;
    const Entity* e = m_project->entity(m_selected);
    if (!e || m_project->isEffectivelyLocked(m_selected))
        return;
    if (m_project->hasChildren(m_selected))
    {
        statusBar()->showMessage(QStringLiteral("先移动或删除子对象，再删除这个父级"), 3000);
        return;
    }

    const Entity copy = *e;
    const int index = m_project->removeEntity(m_selected);
    setSelectedEntity(kInvalidEntity);
    m_history->push(std::make_unique<RemoveEntityCommand>(copy, index), *m_project);
    statusBar()->showMessage(QStringLiteral("已删除实体"), 2000);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete && m_selected != kInvalidEntity)
    {
        deleteSelectedEntity();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::saveProject()
{
    if (m_project->filePath.isEmpty())
        return saveProjectAs();

    QString error;
    if (!m_project->save(m_project->filePath, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return false;
    }
    Settings::setProjectFrame(m_project->filePath, m_frame);
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("已保存"), 2000);
    return true;
}

void MainWindow::autoSave()
{
    if (!Settings::autosaveEnabled() || m_project->filePath.isEmpty()
        || !m_project->modified)
        return;

    QString error;
    if (!m_project->save(m_project->filePath, &error))
    {
        statusBar()->showMessage(
            QStringLiteral("自动保存失败：%1").arg(error), 5000);
        return;
    }
    Settings::setProjectFrame(m_project->filePath, m_frame);
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("已自动保存"), 2000);
}

void MainWindow::updateAutosaveTimer()
{
    if (!m_autosaveTimer)
        return;
    if (!Settings::autosaveEnabled())
    {
        m_autosaveTimer->stop();
        return;
    }
    m_autosaveTimer->start(Settings::autosaveMinutes() * 60 * 1000);
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

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存项目"), m_project->filePath,
        QStringLiteral("PVForge 工程 (*.pvproj)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".pvproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".pvproj");

    QString error;
    if (!m_project->save(path, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return false;
    }
    Settings::setProjectFrame(m_project->filePath, m_frame);
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(path), 3000);
    return true;
}

void MainWindow::packAllAssets()
{
    if (m_project->filePath.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("素材"),
                                 QStringLiteral("请先保存工程，再打包素材。"));
        return;
    }

    int packed = 0;
    for (Entity& entity : m_project->mutableEntities())
    {
        if (entity.sourcePath.isEmpty())
            continue;
        const QString target = m_project->copyIntoProject(entity.sourcePath);
        if (target != entity.sourcePath)
        {
            entity.sourcePath = target;
            ++packed;
        }
    }

    for (int i = 0; i < m_project->templateCount(); ++i)
    {
        Template* tmpl = m_project->templateAt(i);
        if (!tmpl || tmpl->path.isEmpty())
            continue;
        const QString target = m_project->copyIntoProject(tmpl->path);
        if (target != tmpl->path)
        {
            tmpl->path = target;
            ++packed;
        }
    }

    m_project->notifyChanged();
    saveProject();
    statusBar()->showMessage(QStringLiteral("已把 %1 个素材打包进工程目录").arg(packed), 3000);
}

void MainWindow::onEntityRenamed(EntityId id, const QString& name)
{
    const Entity* e = m_project->entity(id);
    if (!e || m_project->isEffectivelyLocked(id) || e->name == name)
        return;
    m_history->push(std::make_unique<RenameEntityCommand>(id, e->name, name), *m_project);
}

void MainWindow::onAudioMoved(EntityId id, double startFrame)
{
    Entity* e = m_project->entity(id);
    if (!e || !e->isAudio() || e->locked)
        return;
    const double oldStart =
        e->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    if (std::abs(oldStart - startFrame) < 1e-9)
        return;
    m_history->push(std::make_unique<SetEntityDataCommand>(
                        id, QStringLiteral("startFrame"), oldStart, startFrame,
                        QStringLiteral("移动音频")),
                    *m_project);
    statusBar()->showMessage(
        QStringLiteral("音频已移动到第 %1 帧").arg(std::lround(startFrame)), 2000);
}

void MainWindow::onParentEdited(EntityId id, EntityId parent)
{
    Entity* e = m_project->entity(id);
    if (!e || e->isAudio() || m_project->isEffectivelyLocked(id)
        || e->parent == parent)
        return;
    if (parent != kInvalidEntity)
    {
        const Entity* newParent = m_project->entity(parent);
        if (!newParent || newParent->isAudio()
            || (e->isCamera() && newParent->isCamera())
            || m_project->isDescendantOf(parent, id))
        {
            return;
        }
    }
    m_history->push(std::make_unique<SetParentCommand>(id, e->parent, parent),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已修改父级坐标系"), 2000);
}

void MainWindow::onEntityMoveRequested(EntityId id, EntityId parent, int index)
{
    Entity* e = m_project->entity(id);
    if (!e || e->isAudio() || m_project->isEffectivelyLocked(id))
        return;
    if (parent != kInvalidEntity)
    {
        const Entity* newParent = m_project->entity(parent);
        if (!newParent || newParent->isAudio()
            || (e->isCamera() && newParent->isCamera())
            || m_project->isDescendantOf(parent, id))
        {
            return;
        }
    }

    const auto& entities = m_project->entities();
    int oldIndex = -1;
    for (int i = 0; i < int(entities.size()); ++i)
        if (entities[i].id == id)
        {
            oldIndex = i;
            break;
        }
    if (oldIndex < 0)
        return;
    if (e->parent == parent && (index == oldIndex || index == oldIndex + 1))
        return;

    m_history->push(std::make_unique<MoveEntityCommand>(id, e->parent, parent,
                                                        oldIndex, index),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已调整图层顺序"), 2000);
}

void MainWindow::onEntitiesMoveRequested(const QVector<EntityId>& ids,
                                         EntityId parent, int index)
{
    QVector<EntityId> roots;
    for (EntityId id : ids)
    {
        const Entity* e = m_project->entity(id);
        if (!e || e->isAudio() || m_project->isEffectivelyLocked(id))
            continue;
        if (parent != kInvalidEntity)
        {
            const Entity* newParent = m_project->entity(parent);
            if (!newParent || newParent->isAudio()
                || (e->isCamera() && newParent->isCamera())
                || parent == id || m_project->isDescendantOf(parent, id))
                continue;
        }
        roots.append(id);
    }
    if (roots.isEmpty())
        return;

    const QVector<Entity> before = m_project->entities();
    if (!m_project->moveEntities(roots, parent, index))
        return;
    m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                        before, m_project->entities(), QStringLiteral("移动实体")),
                    *m_project);
    statusBar()->showMessage(
        QStringLiteral("已移动 %1 个实体").arg(roots.size()), 2000);
}

void MainWindow::onKeyframesMoveRequested(const QVector<KeyframeRecord>& records,
                                          double delta)
{
    if (records.isEmpty() || std::abs(delta) < 1e-9)
        return;
    const QVector<Entity> before = m_project->entities();
    m_project->moveKeyframes(records, delta);
    m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                        before, m_project->entities(), QStringLiteral("移动关键帧")),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已移动关键帧"), 2000);
}

void MainWindow::onCreateFolderRequested(EntityId parent)
{
    if (parent != kInvalidEntity)
    {
        const Entity* parentEntity = m_project->entity(parent);
        if (!parentEntity || m_project->isEffectivelyLocked(parent))
            return;
    }
    Entity* folder = m_project->addFolder(QString(), parent);
    if (folder)
        onEntityCreated(folder->id);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_project->modified)
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("关闭项目"),
            QStringLiteral("“%1”有未保存的修改，要保存吗？").arg(m_project->name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

        if (answer == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save && !saveProject())
        {
            event->ignore();
            return;
        }
    }
    // Remember the playhead even when the project itself is unchanged.
    Settings::setProjectFrame(m_project->filePath, m_frame);
    event->accept();
}

EntityId MainWindow::importImageFile(const QString& path)
{
    Entity* e = m_project->addImage(path);
    if (!e)
        return kInvalidEntity;

    const EntityId id = e->id;
    onEntityCreated(id);
    return id;
}

void MainWindow::setFrame(int frame)
{
    m_frame = std::max(0, frame);
    m_videoPlayer.update(*m_project, m_frame);
    m_viewport->setCurrentFrame(m_frame);
    m_timeline->setCurrentFrame(m_frame);
    m_props->setFrame(m_frame);
    if (m_monitor)
        m_monitor->setFrame(m_frame);
    updateStatus();

    if (!m_playing && Settings::audioScrub() && m_frame != m_lastScrubFrame)
    {
        m_lastScrubFrame = m_frame;
        const qint64 elapsed =
            m_scrubClock.isValid() ? m_scrubClock.elapsed() : 1000;
        if (elapsed >= 60)
        {
            m_audioEngine->scrub(m_frame / double(std::max(1, m_project->fps)));
            m_scrubClock.restart();
        }
        else
        {
            m_scrubTimer->start(int(60 - elapsed));
        }
    }
}

void MainWindow::setSelectedEntity(EntityId id)
{
    Entity* selected = m_project->entity(id);
    if (selected && m_project->isEffectivelyLocked(id))
    {
        id = kInvalidEntity;
        selected = nullptr;
    }
    if (selected && selected->isCamera()
        && !m_project->isEffectivelyHidden(id))
    {
        // Selecting a camera makes it the active framing camera and asks the
        // monitor to show it.
        m_project->activeCamera = id;
        m_project->monitorView = id;
        if (m_monitor)
            m_monitor->syncCameraView();
    }

    if (m_selected == id)
        return;

    m_selected = id;
    m_viewport->setSelectedEntity(id);
    m_props->setEntity(id);
    m_timeline->setSelectedEntity(id);

    // Mine-imator opens an entity property tab when something is selected
    // and closes it when the selection is cleared.
    if (id != kInvalidEntity)
    {
        const Entity* entity = m_project->entity(id);
        const QString tabName = entity ? entity->name : QStringLiteral("属性");
        const int index = m_rightTabs->indexOf(m_objectTab);
        if (index < 0)
            m_rightTabs->addTab(m_objectTab, tabName);
        else
            m_rightTabs->setTabText(index, tabName);
        m_rightTabs->setCurrentWidget(m_objectTab);
    }
    else
    {
        const int index = m_rightTabs->indexOf(m_objectTab);
        if (index >= 0)
        {
            m_rightTabs->removeTab(index);
            m_rightTabs->setCurrentWidget(m_projectTab);
        }
    }
}

void MainWindow::applyValue(EntityId id, const QString& prop, double value)
{
    Entity* e = m_project->entity(id);
    if (!e || e->locked)
        return;

    if (e->isCamera())
    {
        Camera::State state = Camera::state(*m_project, id, m_frame);
        double x = state.center.x();
        double y = state.center.y();
        double scale = state.scale;
        double rotation = state.rotation;
        if (prop == QStringLiteral("x"))
            x = value;
        else if (prop == QStringLiteral("y"))
            y = value;
        else if (prop == QStringLiteral("scaleX"))
            scale = std::clamp(value, Camera::kMinScale, Camera::kMaxScale);
        else if (prop == QStringLiteral("rotation"))
            rotation = value;
        else
            return;
        const QPointF center = Camera::clampCenter(*m_project, scale, QPointF(x, y));
        applyCamera(id, center.x(), center.y(), scale, rotation);
        return;
    }

    // Auto-key on by default. With auto-key off, existing animated properties
    // still get edited at the current frame so the viewport stays truthful.
    const bool keyframe = m_project->autoKey || e->tracks.contains(prop);
    const double oldValue = m_project->value(id, prop, m_frame);
    if (qFuzzyCompare(oldValue + 1.0, value + 1.0))
        return;

    m_project->setValue(id, prop, m_frame, value, keyframe);
    m_history->push(std::make_unique<SetValueCommand>(id, prop, m_frame, oldValue, value,
                                                       keyframe),
                    *m_project);
}

void MainWindow::applyPosition(EntityId id, double x, double y)
{
    Entity* e = m_project->entity(id);
    if (!e || e->locked)
        return;

    const double oldX = m_project->value(id, QStringLiteral("x"), m_frame);
    const double oldY = m_project->value(id, QStringLiteral("y"), m_frame);
    if (qFuzzyCompare(oldX + 1.0, x + 1.0) && qFuzzyCompare(oldY + 1.0, y + 1.0))
        return;

    const bool keyframe = m_project->autoKey || e->tracks.contains(QStringLiteral("x"))
                          || e->tracks.contains(QStringLiteral("y"));
    m_project->setValue(id, QStringLiteral("x"), m_frame, x, keyframe);
    m_project->setValue(id, QStringLiteral("y"), m_frame, y, keyframe);
    m_history->push(std::make_unique<SetPositionCommand>(id, m_frame, oldX, oldY, x, y,
                                                         keyframe),
                    *m_project);
}

void MainWindow::applyScale(EntityId id, double sx, double sy, double x, double y)
{
    Entity* e = m_project->entity(id);
    if (!e || e->locked)
        return;

    const double oldSx = m_project->value(id, QStringLiteral("scaleX"), m_frame);
    const double oldSy = m_project->value(id, QStringLiteral("scaleY"), m_frame);
    const double oldX = m_project->value(id, QStringLiteral("x"), m_frame);
    const double oldY = m_project->value(id, QStringLiteral("y"), m_frame);
    if (qFuzzyCompare(oldSx + 1.0, sx + 1.0) && qFuzzyCompare(oldSy + 1.0, sy + 1.0)
        && qFuzzyCompare(oldX + 1.0, x + 1.0) && qFuzzyCompare(oldY + 1.0, y + 1.0))
        return;

    const bool keyframe = m_project->autoKey
                          || e->tracks.contains(QStringLiteral("scaleX"))
                          || e->tracks.contains(QStringLiteral("scaleY"));
    // Apply first: merged history entries skip redo.
    m_project->setValue(id, QStringLiteral("scaleX"), m_frame, sx, keyframe);
    m_project->setValue(id, QStringLiteral("scaleY"), m_frame, sy, keyframe);
    m_project->setValue(id, QStringLiteral("x"), m_frame, x, keyframe);
    m_project->setValue(id, QStringLiteral("y"), m_frame, y, keyframe);
    m_history->push(std::make_unique<SetScaleCommand>(id, m_frame, oldSx, oldSy, oldX,
                                                       oldY, sx, sy, x, y, keyframe),
                    *m_project);
}

void MainWindow::applyComicTip(EntityId id, double tipX, double tipY)
{
    QVariantMap data;
    data.insert(QStringLiteral("bubbleTipX"), tipX);
    data.insert(QStringLiteral("bubbleTipY"), tipY);
    applyEntityData(id, data);
}

void MainWindow::replaceSourceFromDialog(EntityId id)
{
    const Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
        return;

    const EntityType expected = entity->type;
    QString filter;
    switch (expected)
    {
    case EntityType::Image:
        filter = QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.svg)");
        break;
    case EntityType::Video:
        filter = QStringLiteral("视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v)");
        break;
    case EntityType::Audio:
        filter = QStringLiteral("音频 (*.wav *.mp3 *.flac *.ogg *.opus *.m4a *.aac)");
        break;
    default:
        return;
    }

    const QString file =
        AssetLibraryPanel::pickMedia(this, m_project, expected, filter);
    if (file.isEmpty())
        return;
    const QString directory = m_project->assetDirectory();
    auto* watcher = new QFutureWatcher<MediaImportResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, id, expected] {
                const MediaImportResult result = watcher->result();
                watcher->deleteLater();
                hideImportBusy();
                if (!result.error.isEmpty())
                {
                    QMessageBox::warning(this, QStringLiteral("更换素材失败"),
                                         result.error);
                    return;
                }
                if (result.type != expected)
                {
                    QMessageBox::warning(this, QStringLiteral("更换素材失败"),
                                         QStringLiteral("素材类型不匹配"));
                    return;
                }
                const Entity* before = m_project->entity(id);
                if (!before)
                    return;

                QVector<Entity> beforeEntities;
                beforeEntities.append(*before);
                for (const Entity& other : m_project->entities())
                    if (other.type == EntityType::Audio
                        && other.data.value(QStringLiteral("pairedVideo")).toInt() == id)
                        beforeEntities.append(other);

                QString error;
                const QVector<EntityId> changed =
                    m_project->replaceEntitySource(id, result, &error);
                if (!error.isEmpty())
                {
                    QMessageBox::warning(this, QStringLiteral("更换素材失败"), error);
                    return;
                }
                QVector<Entity> afterEntities;
                for (EntityId changedId : changed)
                    if (const Entity* current = m_project->entity(changedId))
                        afterEntities.append(*current);
                m_history->push(std::make_unique<ReplaceEntitySourcesCommand>(
                                    beforeEntities, afterEntities),
                                *m_project);
                statusBar()->showMessage(QStringLiteral("已更换素材"), 2000);
            });
    showImportBusy(1);
    watcher->setFuture(QtConcurrent::run(
        [file, directory] { return processMediaFile(file, directory); }));
}

MainWindow::EntityClipboard MainWindow::buildEntityClip(EntityId id) const
{
    EntityClipboard clip;
    const Entity* root = m_project->entity(id);
    if (!root)
        return clip;

    clip.rootId = id;
    QSet<EntityId> seen;
    std::function<void(EntityId)> add = [&](EntityId current) {
        if (seen.contains(current))
            return;
        seen.insert(current);
        const Entity* entity = m_project->entity(current);
        if (!entity)
            return;
        clip.entries.append({*entity, entity->parent});
        for (const Entity& child : m_project->entities())
            if (child.parent == current)
                add(child.id);
    };
    add(id);

    if (root->type == EntityType::Video)
    {
        for (const Entity& other : m_project->entities())
            if (other.type == EntityType::Audio
                && other.data.value(QStringLiteral("pairedVideo")).toInt() == id)
            {
                clip.hasPairedAudio = true;
                clip.pairedAudio = other;
                break;
            }
    }
    return clip;
}

void MainWindow::copyEntityToClipboard(EntityId id)
{
    if (!m_project->entity(id))
        return;
    m_entityClip = std::make_unique<EntityClipboard>(buildEntityClip(id));
    statusBar()->showMessage(QStringLiteral("已复制实体"), 2000);
}

void MainWindow::pasteEntity()
{
    if (!m_entityClip || m_entityClip->entries.isEmpty())
    {
        statusBar()->showMessage(QStringLiteral("剪贴板里没有实体"), 2000);
        return;
    }
    pasteEntityClip(*m_entityClip);
}

void MainWindow::copySelectedEntity()
{
    if (m_selected != kInvalidEntity)
        copyEntityToClipboard(m_selected);
}

void MainWindow::duplicateEntity(EntityId id)
{
    if (!m_project->entity(id))
        return;
    EntityClipboard clip = buildEntityClip(id);
    pasteEntityClip(clip);
}

void MainWindow::pasteEntityClip(EntityClipboard& clip)
{
    if (clip.entries.isEmpty())
        return;

    QHash<EntityId, EntityId> idMap;
    EntityId next = m_project->nextId();
    for (const EntityClipboardEntry& entry : clip.entries)
        idMap.insert(entry.entity.id, next++);

    EntityId rootParent = clip.entries.first().parent;
    if (rootParent != kInvalidEntity && !m_project->entity(rootParent))
        rootParent = kInvalidEntity;

    int insertAt = int(m_project->entities().size());
    for (int i = 0; i < int(m_project->entities().size()); ++i)
        if (m_project->entities()[i].id == clip.rootId)
        {
            insertAt = i + 1;
            break;
        }

    QVector<Entity> added;
    QVector<int> indices;
    for (int i = 0; i < int(clip.entries.size()); ++i)
    {
        Entity copy = clip.entries[i].entity;
        copy.id = idMap.value(copy.id);
        copy.parent = i == 0
                          ? rootParent
                          : idMap.value(clip.entries[i].parent, kInvalidEntity);
        added.append(copy);
        indices.append(insertAt + i);
        m_project->insertEntity(copy, insertAt + i);
    }

    if (clip.hasPairedAudio)
    {
        Entity audio = clip.pairedAudio;
        audio.id = m_project->nextId();
        audio.parent = kInvalidEntity;
        audio.data[QStringLiteral("pairedVideo")] = idMap.value(clip.rootId);
        added.append(audio);
        indices.append(insertAt + int(clip.entries.size()));
        m_project->insertEntity(audio, insertAt + int(clip.entries.size()));
    }

    setSelectedEntity(idMap.value(clip.rootId));
    m_history->push(std::make_unique<AddEntitiesCommand>(added, indices), *m_project);
    statusBar()->showMessage(QStringLiteral("已粘贴实体"), 2000);
}

void MainWindow::groupEntitiesIntoFolder(const QVector<EntityId>& ids)
{
    if (ids.isEmpty())
        return;

    // A folder and its child cannot both move into the new folder; keep the
    // outermost selection.
    QVector<EntityId> roots;
    for (EntityId id : ids)
    {
        if (!m_project->entity(id))
            continue;
        bool nested = false;
        for (EntityId other : ids)
        {
            if (other == id || !m_project->entity(other))
                continue;
            if (m_project->isDescendantOf(id, other))
            {
                nested = true;
                break;
            }
        }
        if (!nested)
            roots.append(id);
    }
    if (roots.isEmpty())
        return;

    EntityId parent = kInvalidEntity;
    bool first = true;
    for (EntityId id : roots)
    {
        const Entity* entity = m_project->entity(id);
        if (!entity)
            continue;
        if (first)
        {
            parent = entity->parent;
            first = false;
        }
        else if (entity->parent != parent)
        {
            parent = kInvalidEntity;
            break;
        }
    }

    QVector<EntityId> ordered;
    for (int index : m_project->entityOrder())
    {
        const EntityId id = m_project->entities()[index].id;
        if (roots.contains(id))
            ordered.append(id);
    }

    const QVector<Entity> before = m_project->entities();
    Entity* folder = m_project->addFolder(QString(), parent);
    if (!folder)
        return;
    const EntityId folderId = folder->id;
    for (EntityId id : ordered)
        m_project->moveEntity(id, folderId, int(m_project->entities().size()));

    setSelectedEntity(folderId);
    m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                        before, m_project->entities(), QStringLiteral("组成文件夹")),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已组成文件夹"), 2000);
}

void MainWindow::onPropertyKeyframeToggled(EntityId id, const QString& prop,
                                           double frame, bool remove)
{
    const Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
        return;
    const double value = m_project->value(id, prop, frame);
    m_history->push(std::make_unique<SetKeyframeCommand>(id, prop, frame, value,
                                                         !remove),
                    *m_project);
}

void MainWindow::applyEntityData(EntityId id, const QVariantMap& data)
{
    const Entity* entity = m_project->entity(id);
    if (!entity || data.isEmpty())
        return;

    QVariantMap before;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it)
        before.insert(it.key(), entity->data.value(it.key()));

    // Apply first: merged history entries skip redo.
    m_project->setEntityDataMap(id, data);

    m_history->push(std::make_unique<SetEntityDataMapCommand>(
                        id, before, data, QStringLiteral("修改属性")),
                    *m_project);
}

void MainWindow::applyPivot(EntityId id, double ax, double ay)
{    Entity* e = m_project->entity(id);
    if (!e || e->locked)
        return;

    const double oldAx = m_project->value(id, QStringLiteral("rotPointX"), m_frame);
    const double oldAy = m_project->value(id, QStringLiteral("rotPointY"), m_frame);
    if (qFuzzyCompare(oldAx + 1.0, ax + 1.0) && qFuzzyCompare(oldAy + 1.0, ay + 1.0))
        return;

    const double oldX = m_project->value(id, QStringLiteral("x"), m_frame);
    const double oldY = m_project->value(id, QStringLiteral("y"), m_frame);
    const double rotation = m_project->value(id, QStringLiteral("rotation"), m_frame);
    const double radians = rotation * M_PI / 180.0;
    const double dax = ax - oldAx;
    const double day = ay - oldAy;
    const double dx = dax * std::cos(radians) - day * std::sin(radians);
    const double dy = dax * std::sin(radians) + day * std::cos(radians);

    // Apply first: merged history entries skip redo.
    m_project->setValue(id, QStringLiteral("rotPointX"), m_frame, ax, true);
    m_project->setValue(id, QStringLiteral("rotPointY"), m_frame, ay, true);
    m_project->setValue(id, QStringLiteral("x"), m_frame, oldX + dx, true);
    m_project->setValue(id, QStringLiteral("y"), m_frame, oldY + dy, true);

    m_history->push(std::make_unique<SetPivotCommand>(id, oldAx, oldAy, oldX, oldY, ax, ay,
                                                      oldX + dx, oldY + dy, m_frame),
                    *m_project);
}

void MainWindow::applyCamera(EntityId id, double x, double y, double scale,
                             double rotation)
{
    Entity* e = m_project->entity(id);
    if (!e || !e->isCamera() || e->locked)
        return;

    const double oldX = m_project->value(id, QStringLiteral("x"), m_frame);
    const double oldY = m_project->value(id, QStringLiteral("y"), m_frame);
    const double oldScale = m_project->value(id, QStringLiteral("scaleX"), m_frame);
    const double oldRotation = m_project->value(id, QStringLiteral("rotation"), m_frame);
    if (qFuzzyCompare(oldX + 1.0, x + 1.0) && qFuzzyCompare(oldY + 1.0, y + 1.0)
        && qFuzzyCompare(oldScale + 1.0, scale + 1.0)
        && qFuzzyCompare(oldRotation + 1.0, rotation + 1.0))
        return;

    const bool keyframe = m_project->autoKey
                          || e->tracks.contains(QStringLiteral("x"))
                          || e->tracks.contains(QStringLiteral("y"))
                          || e->tracks.contains(QStringLiteral("scaleX"))
                          || e->tracks.contains(QStringLiteral("scaleY"))
                          || e->tracks.contains(QStringLiteral("rotation"));

    // Apply first: merged history entries skip redo.
    m_project->setValue(id, QStringLiteral("x"), m_frame, x, keyframe);
    m_project->setValue(id, QStringLiteral("y"), m_frame, y, keyframe);
    m_project->setValue(id, QStringLiteral("scaleX"), m_frame, scale, keyframe);
    m_project->setValue(id, QStringLiteral("scaleY"), m_frame, scale, keyframe);
    m_project->setValue(id, QStringLiteral("rotation"), m_frame, rotation, keyframe);

    m_history->push(std::make_unique<SetCameraCommand>(
                        id, m_frame, oldX, oldY, oldScale, oldRotation, x, y, scale,
                        rotation, keyframe),
                    *m_project);
}

void MainWindow::showImportBusy(int count)
{
    statusBar()->showMessage(QStringLiteral("正在导入 %1 个素材…").arg(count));
    m_importProgress->show();
}

void MainWindow::hideImportBusy()
{
    m_importProgress->hide();
    statusBar()->showMessage(QStringLiteral("导入完成"), 2500);
}

void MainWindow::importMediaFiles(
    const QStringList& files, std::function<void(const QVector<MediaImportResult>&)> done)
{
    if (files.isEmpty())
        return;

    const QString directory = m_project->assetDirectory();
    auto* watcher = new QFutureWatcher<QVector<MediaImportResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, done] {
        const QVector<MediaImportResult> results = watcher->result();
        watcher->deleteLater();
        hideImportBusy();
        if (done)
            done(results);
    });

    showImportBusy(int(files.size()));
    watcher->setFuture(QtConcurrent::run(
        [files, directory] { return processMediaFiles(files, directory); }));
}

void MainWindow::onUndo()
{
    m_history->undo(*m_project);
}

void MainWindow::onRedo()
{
    m_history->redo(*m_project);
}

void MainWindow::onEntityCreated(EntityId id)
{
    setSelectedEntity(id);
    if (const Entity* created = m_project->entity(id))
    {
        if (created->isCamera())
            m_monitor->setVisibleFromMenu(true);
    }

    const auto& entities = m_project->entities();
    int videoIndex = -1;
    for (int i = 0; i < int(entities.size()); ++i)
    {
        if (entities[i].id == id)
        {
            videoIndex = i;
            break;
        }
    }
    if (videoIndex < 0)
        return;

    // A video with an extracted track creates its paired audio at the same
    // time; keep both in one undo step.
    QVector<Entity> created;
    QVector<int> indices;
    created.append(entities[videoIndex]);
    indices.append(videoIndex);
    for (int i = 0; i < int(entities.size()); ++i)
    {
        const Entity& e = entities[i];
        if (e.type == EntityType::Audio
            && e.data.value(QStringLiteral("pairedVideo")).toInt() == id)
        {
            created.append(e);
            indices.append(i);
        }
    }
    m_history->push(std::make_unique<AddEntitiesCommand>(created, indices), *m_project);
}

void MainWindow::onEntityFlagChanged(EntityId id, EntityFlag flag, bool value)
{
    const Entity* e = m_project->entity(id);
    if (!e)
        return;
    const bool oldValue = (flag == EntityFlag::Hidden) ? e->hidden : e->locked;
    if (oldValue == value)
        return;
    m_history->push(std::make_unique<SetEntityFlagCommand>(id, flag, oldValue, value),
                    *m_project);
}

void MainWindow::onDeleteKeyframesRequested()
{
    QVector<KeyframeRecord> records;
    for (const KeyframeSelection& selection : m_timeline->selectedKeyframes())
    {
        const Entity* entity = m_project->entity(selection.entity);
        if (!entity || m_project->isEffectivelyLocked(selection.entity))
            continue;
        records += m_project->removeKeyframesAt(selection.entity, selection.frame);
    }
    if (records.isEmpty())
        return;
    m_history->push(std::make_unique<RemoveKeyframesCommand>(records), *m_project);
    statusBar()->showMessage(QStringLiteral("已删除关键帧"), 2000);
}

void MainWindow::onKeyframeEasingRequested(const EasingSpec& spec)
{
    QVector<KeyframeEasingRecord> records;
    for (const KeyframeSelection& selection : m_timeline->selectedKeyframes())
    {
        Entity* entity = m_project->entity(selection.entity);
        if (!entity || entity->locked)
            continue;
        for (auto it = entity->tracks.begin(); it != entity->tracks.end(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - selection.frame) > 1e-6 || key.ease == spec)
                    continue;
                KeyframeEasingRecord record;
                record.entity = selection.entity;
                record.property = it.key();
                record.frame = key.frame;
                record.oldEase = key.ease;
                record.newEase = spec;
                records.append(record);
            }
        }
    }
    if (records.isEmpty())
        return;

    // Apply first: merged history entries skip redo.
    for (const KeyframeEasingRecord& record : records)
        m_project->setKeyframeEasing(record.entity, record.property, record.frame,
                                     record.newEase);
    m_history->push(std::make_unique<SetKeyframeEasingCommand>(records), *m_project);
    m_timeline->update();
    statusBar()->showMessage(QStringLiteral("已更新关键帧缓动"), 2000);
}

void MainWindow::onKeyframeEasingEdited(EntityId id, int frame, const EasingSpec& spec)
{
    Entity* entity = m_project->entity(id);
    if (!entity || entity->locked)
        return;

    QVector<KeyframeEasingRecord> records;
    for (auto it = entity->tracks.begin(); it != entity->tracks.end(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (std::abs(key.frame - frame) > 1e-6 || key.ease == spec)
                continue;
            KeyframeEasingRecord record;
            record.entity = id;
            record.property = it.key();
            record.frame = key.frame;
            record.oldEase = key.ease;
            record.newEase = spec;
            records.append(record);
        }
    }
    if (records.isEmpty())
        return;

    for (const KeyframeEasingRecord& record : records)
        m_project->setKeyframeEasing(record.entity, record.property, record.frame,
                                     record.newEase);
    m_history->push(std::make_unique<SetKeyframeEasingCommand>(records), *m_project);
    m_timeline->update();
    statusBar()->showMessage(QStringLiteral("已更新关键帧缓动"), 2000);
}

void MainWindow::refreshAll()
{
    if (m_selected != kInvalidEntity)
    {
        const Entity* selected = m_project->entity(m_selected);
        if (!selected || m_project->isEffectivelyLocked(m_selected))
            setSelectedEntity(kInvalidEntity);
    }
    m_videoPlayer.update(*m_project, m_frame);
    ensureProxies();
    m_viewport->update();
    m_timeline->update();
    m_props->refresh();
    if (m_monitor)
        m_monitor->setProjectChanged();
    updateStatus();
    updateWindowTitle();
}

void MainWindow::ensureProxies()
{
    if (!m_proxyGenerator)
        return;
    for (const Template& tmpl : m_project->templates())
    {
        if (tmpl.type == EntityType::Video && !tmpl.path.isEmpty())
            m_proxyGenerator->ensure(tmpl.path);
    }
    for (const Entity& entity : m_project->entities())
    {
        if (entity.type == EntityType::Video && !entity.sourcePath.isEmpty())
            m_proxyGenerator->ensure(entity.sourcePath);
    }
}

void MainWindow::togglePlayback()
{
    if (m_playing)
    {
        m_playTimer->stop();
        m_scrubTimer->stop();
        m_audioEngine->stop();
        m_playing = false;
        m_lastScrubFrame = m_frame;
        if (m_monitor)
            m_monitor->setPlaying(false);
        m_playAction->setText(Lang::t(QStringLiteral("action.play"), QStringLiteral("播放")));
        return;
    }

    if (m_frame >= m_project->timelineLength())
        setFrame(0);

    m_scrubTimer->stop();
    m_playStart = m_frame;
    m_playing = true;
    m_audioEngine->play(m_frame);
    m_playClock.restart();
    m_playTimer->start(std::max(1, 1000 / std::max(1, m_project->fps)));
    if (m_monitor)
        m_monitor->setPlaying(true);
    m_playAction->setText(Lang::t(QStringLiteral("action.pause"), QStringLiteral("暂停")));
}

void MainWindow::onPlayTick()
{
    const int fps = std::max(1, m_project->fps);
    int f = 0;
    if (m_audioEngine->isRunning())
    {
        f = int(std::lround(m_audioEngine->positionSeconds() * fps));
    }
    else
    {
        const double seconds = m_playClock.elapsed() / 1000.0;
        f = m_playStart + int(seconds * fps);
    }

    const int length = m_project->timelineLength();
    if (length > 0 && f > length)
    {
        if (m_project->loop)
        {
            m_playStart = 0;
            m_playClock.restart();
            m_audioEngine->play(0);
            f = 0;
        }
        else
        {
            setFrame(length);
            togglePlayback();
            return;
        }
    }
    setFrame(f);
}

void MainWindow::updateStatus()
{
    const int fps = std::max(1, m_project->fps);
    const int length = m_project->timelineLength();
    const QString common = QStringLiteral(" · %1 fps · %2×%3")
                               .arg(fps)
                               .arg(m_project->width)
                               .arg(m_project->height);

    if (Settings::timelineDisplayFrames())
    {
        m_status->setText(QStringLiteral("帧 %1 / %2").arg(m_frame).arg(length)
                          + common);
    }
    else
    {
        m_status->setText(QStringLiteral("%1s / %2s")
                              .arg(m_frame / double(fps), 0, 'f', 2)
                              .arg(length / double(fps), 0, 'f', 2)
                          + common);
    }
}

} // namespace pvf
