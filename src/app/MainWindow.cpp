#include "MainWindow.h"

#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QAction>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pvf {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PVForge — 未命名工程"));
    resize(1440, 900);
    setMinimumSize(1000, 620);

    m_project = new Project(this);
    m_viewport = new Viewport(m_project, this);
    m_props = new PropertiesPanel(m_project, this);
    m_timeline = new TimelinePanel(m_project, this);
    m_workbench = new WorkbenchPanel(m_project, m_viewport);
    m_workbench->setIgnoreMouseWidget(m_viewport->workbenchButton());
    m_workbench->hide();

    m_props->setMinimumWidth(220);
    m_props->setMaximumWidth(380);
    m_viewport->setMinimumWidth(480);

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(m_viewport);
    top->addWidget(m_props);
    top->setChildrenCollapsible(false);
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 0);
    top->setSizes({1100, 280});

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
    connect(endAction, &QAction::triggered, this, [this] { setFrame(m_project->duration); });
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
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(m_quitAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("编辑"));
    editMenu->addAction(m_autoKeyAction);

    auto* playMenu = menuBar()->addMenu(QStringLiteral("播放"));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_homeAction);
    playMenu->addAction(m_endAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(m_fitAction);

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
    m_frame = std::clamp(frame, 0, m_project->duration);
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

    if (m_frame >= m_project->duration)
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

    if (f > m_project->duration)
    {
        if (m_project->loop)
        {
            m_playStart = 0;
            m_playClock.restart();
            f = 0;
        }
        else
        {
            setFrame(m_project->duration);
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
                          .arg(m_project->duration)
                          .arg(m_project->fps)
                          .arg(m_project->width)
                          .arg(m_project->height));
}

} // namespace pvf
