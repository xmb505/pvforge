#include "MainWindow.h"

#include "ui/AssetLibraryPanel.h"
#include "ui/CollapsibleSection.h"
#include "ui/EnvironmentPanel.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

MainWindow::MainWindow(Project* project, QWidget* parent)
    : QMainWindow(parent)
    , m_project(project)
{
    m_project->setParent(this);
    setWindowTitle(QStringLiteral("PVForge — %1").arg(m_project->name));
    resize(1440, 900);
    setMinimumSize(640, 400);

    m_viewport = new Viewport(m_project, this);
    m_props = new PropertiesPanel(m_project, this);
    m_library = new AssetLibraryPanel(m_project, this);
    m_projectProps = new ProjectPropertiesPanel(m_project, this);
    m_environment = new EnvironmentPanel(m_project, this);
    m_timeline = new TimelinePanel(m_project, this);
    m_workbench = new WorkbenchPanel(m_project, m_viewport);
    m_workbench->setIgnoreMouseWidget(m_viewport->workbenchButton());
    m_workbench->hide();

    // "Project properties" tab: collapsible categories, one scroll column.
    m_projectTab = new QWidget(this);
    auto* projectScroll = new QScrollArea(m_projectTab);
    projectScroll->setWidgetResizable(true);
    projectScroll->setFrameShape(QFrame::NoFrame);
    projectScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* projectContent = new QWidget(projectScroll);
    auto* projectLayout = new QVBoxLayout(projectContent);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectLayout->setSpacing(0);

    auto* settingsSection =
        new CollapsibleSection(QStringLiteral("项目设置"), m_projectProps, projectContent);
    auto* environmentSection =
        new CollapsibleSection(QStringLiteral("环境设置"), m_environment, projectContent);
    auto* librarySection =
        new CollapsibleSection(QStringLiteral("素材库"), m_library, projectContent);
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

    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_rightTabs->setMinimumWidth(300);
    m_rightTabs->setMaximumWidth(440);
    m_rightTabs->addTab(m_projectTab, QStringLiteral("项目属性"));

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
    statusBar()->addPermanentWidget(m_status);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onPlayTick);

    connect(m_viewport, &Viewport::entitySelected, this, &MainWindow::setSelectedEntity);
    connect(m_viewport, &Viewport::valueEdited, this, &MainWindow::applyValue);
    connect(m_viewport, &Viewport::workbenchRequested, this, &MainWindow::toggleWorkbench);
    connect(m_props, &PropertiesPanel::valueEdited, this, &MainWindow::applyValue);
    connect(m_timeline, &TimelinePanel::frameRequested, this, &MainWindow::setFrame);
    connect(m_timeline, &TimelinePanel::entitySelected, this, &MainWindow::setSelectedEntity);
    connect(m_library, &AssetLibraryPanel::entityCreated, this, &MainWindow::setSelectedEntity);
    connect(m_projectProps, &ProjectPropertiesPanel::settingsChanged, this, [this] {
        setWindowTitle(QStringLiteral("PVForge — %1").arg(m_project->name));
        setFrame(m_frame);
    });
    connect(m_project, &Project::changed, this, &MainWindow::refreshAll);
    connect(m_workbench, &WorkbenchPanel::created, this,
            [this](EntityId id) { setSelectedEntity(id); });

    createActions();
    createMenus();
    createToolBar();

    setFrame(0);
}

void MainWindow::createActions()
{
    m_playAction = new QAction(QStringLiteral("播放"), this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);
    addAction(m_playAction);

    auto* homeAction = new QAction(QStringLiteral("回到开头"), this);
    homeAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(homeAction, &QAction::triggered, this, [this] { setFrame(0); });
    addAction(homeAction);
    m_homeAction = homeAction;

    auto* endAction = new QAction(QStringLiteral("跳到结尾"), this);
    endAction->setShortcut(QKeySequence(Qt::Key_End));
    connect(endAction, &QAction::triggered, this, [this] { setFrame(m_project->timelineLength()); });
    addAction(endAction);
    m_endAction = endAction;

    auto* fitAction = new QAction(QStringLiteral("适配窗口"), this);
    fitAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(fitAction, &QAction::triggered, this, [this] { m_viewport->resetView(); });
    addAction(fitAction);
    m_fitAction = fitAction;

    m_autoKeyAction = new QAction(QStringLiteral("自动关键帧"), this);
    m_autoKeyAction->setCheckable(true);
    m_autoKeyAction->setChecked(m_project->autoKey);
    connect(m_autoKeyAction, &QAction::toggled, this, [this](bool on) {
        m_project->autoKey = on;
    });

    m_quitAction = new QAction(QStringLiteral("退出"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
    addAction(m_quitAction);

    m_saveAction = new QAction(QStringLiteral("保存"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    addAction(m_saveAction);

    m_saveAsAction = new QAction(QStringLiteral("另存为…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    addAction(m_saveAsAction);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("编辑"));
    editMenu->addAction(m_autoKeyAction);

    auto* playMenu = menuBar()->addMenu(QStringLiteral("播放"));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_homeAction);
    playMenu->addAction(m_endAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(m_fitAction);

    menuBar()->setFixedHeight(menuBar()->sizeHint().height());

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    helpMenu->addAction(QStringLiteral("关于 PVForge"), this, [this] {
        QMessageBox::about(this, QStringLiteral("关于 PVForge"),
                           QStringLiteral("PVForge 0.1\n\n点击视口左上角的工作台按钮创建"
                                          "图片 / 视频 / 文字 / 矢量。\n"
                                          "拖动摆位，空格播放。"));
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

void MainWindow::saveProject()
{
    if (m_project->filePath.isEmpty())
    {
        saveProjectAs();
        return;
    }

    QString error;
    if (!m_project->save(m_project->filePath, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已保存"), 2000);
}

void MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存项目"), m_project->filePath,
        QStringLiteral("PVForge 工程 (*.pvproj)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".pvproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".pvproj");

    QString error;
    if (!m_project->save(path, &error))
    {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(path), 3000);
}

EntityId MainWindow::importImageFile(const QString& path)
{
    Entity* e = m_project->addImage(path);
    if (!e)
        return kInvalidEntity;

    const EntityId id = e->id;
    setSelectedEntity(id);
    return id;
}

void MainWindow::setFrame(int frame)
{
    m_frame = std::max(0, frame);
    m_viewport->setCurrentFrame(m_frame);
    m_timeline->setCurrentFrame(m_frame);
    m_props->setFrame(m_frame);
    updateStatus();
}

void MainWindow::setSelectedEntity(EntityId id)
{
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
    if (!e)
        return;

    // Auto-key on by default. With auto-key off, existing animated properties
    // still get edited at the current frame so the viewport stays truthful.
    const bool keyframe = m_project->autoKey || e->tracks.contains(prop);
    m_project->setValue(id, prop, m_frame, value, keyframe);
}

void MainWindow::refreshAll()
{
    m_viewport->update();
    m_timeline->update();
    m_props->refresh();
}

void MainWindow::togglePlayback()
{
    if (m_playing)
    {
        m_playTimer->stop();
        m_playing = false;
        m_playAction->setText(QStringLiteral("播放"));
        return;
    }

    if (m_frame >= m_project->timelineLength())
        setFrame(0);

    m_playStart = m_frame;
    m_playClock.restart();
    m_playTimer->start(std::max(1, 1000 / std::max(1, m_project->fps)));
    m_playing = true;
    m_playAction->setText(QStringLiteral("暂停"));
}

void MainWindow::onPlayTick()
{
    const double seconds = m_playClock.elapsed() / 1000.0;
    int f = m_playStart + int(seconds * m_project->fps);

    const int length = m_project->timelineLength();
    if (length > 0 && f > length)
    {
        if (m_project->loop)
        {
            m_playStart = 0;
            m_playClock.restart();
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
    m_status->setText(QStringLiteral("帧 %1 / %2 · %3 fps · %4×%5")
                          .arg(m_frame)
                          .arg(m_project->timelineLength())
                          .arg(m_project->fps)
                          .arg(m_project->width)
                          .arg(m_project->height));
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Squeezed windows collapse the side panel and timeline instead of
    // letting their children overlap the toolbar.
    const bool showSide = width() >= 900;
    const bool showTimeline = height() >= 480;
    if (m_rightTabs)
        m_rightTabs->setVisible(showSide);
    if (m_timeline)
        m_timeline->setVisible(showTimeline);
    if (m_viewport)
        m_viewport->setMinimumWidth(showSide ? 480 : 0);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (width() < minimumWidth() || height() < minimumHeight())
        resize(std::max(width(), minimumWidth()), std::max(height(), minimumHeight()));
}

} // namespace pvf
