#include "MainWindow.h"

#include "core/Commands.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "SettingsDialog.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CollapsibleSection.h"
#include "ui/EnvironmentPanel.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WorkbenchPanel.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
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
    setMinimumSize(1000, 620);

    m_history = new History(this);
    m_project->autoKey = Settings::autoKeyDefault();
    m_project->packAssets = Settings::packAssetsDefault();
    m_viewport = new Viewport(m_project, this);
    m_viewport->setSnapPixels(Settings::snapPixels());
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
    connect(m_workbench, &WorkbenchPanel::created, this, &MainWindow::onEntityCreated);
    connect(m_library, &AssetLibraryPanel::entityCreated, this, &MainWindow::onEntityCreated);
    connect(m_timeline, &TimelinePanel::entityFlagChanged, this,
            &MainWindow::onEntityFlagChanged);
    connect(m_timeline, &TimelinePanel::deleteKeyframesRequested, this,
            &MainWindow::onDeleteKeyframesRequested);
    connect(m_timeline, &TimelinePanel::entityRenamed, this, &MainWindow::onEntityRenamed);
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

    setFrame(0);
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
    auto* settingsAction =
        new QAction(Lang::t(QStringLiteral("action.settings"), QStringLiteral("设置…")), this);
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
        {
            m_viewport->setSnapPixels(Settings::snapPixels());
            statusBar()->showMessage(QStringLiteral("设置已保存，语言等改动重启后生效"), 3000);
        }
    });
    editMenu->addAction(settingsAction);

    auto* playMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.play"), Lang::t(QStringLiteral("action.play"), QStringLiteral("播放"))));
    playMenu->addAction(m_playAction);
    playMenu->addAction(m_homeAction);
    playMenu->addAction(m_endAction);

    auto* viewMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.view"), QStringLiteral("视图")));
    viewMenu->addAction(m_fitAction);

    menuBar()->setFixedHeight(menuBar()->sizeHint().height());

    auto* helpMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.help"), QStringLiteral("帮助")));
    helpMenu->addAction(Lang::t(QStringLiteral("action.about"), QStringLiteral("关于 PVForge")), this, [this] {
        QMessageBox::about(this, Lang::t(QStringLiteral("action.about"), QStringLiteral("关于 PVForge")),
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

void MainWindow::deleteSelectedEntity()
{
    if (m_selected == kInvalidEntity)
        return;
    const Entity* e = m_project->entity(m_selected);
    if (!e)
        return;

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
    statusBar()->showMessage(QStringLiteral("已保存"), 2000);
    return true;
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
    if (!e || e->name == name)
        return;
    m_history->push(std::make_unique<RenameEntityCommand>(id, e->name, name), *m_project);
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
    if (!e || e->locked)
        return;

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

    const auto& entities = m_project->entities();
    for (int i = 0; i < int(entities.size()); ++i)
    {
        if (entities[i].id != id)
            continue;
        m_history->push(std::make_unique<AddEntityCommand>(entities[i], i), *m_project);
        return;
    }
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
        records += m_project->removeKeyframesAt(selection.entity, selection.frame);
    if (records.isEmpty())
        return;
    m_history->push(std::make_unique<RemoveKeyframesCommand>(records), *m_project);
    statusBar()->showMessage(QStringLiteral("已删除关键帧"), 2000);
}

void MainWindow::refreshAll()
{
    if (m_selected != kInvalidEntity && !m_project->entity(m_selected))
        setSelectedEntity(kInvalidEntity);
    m_viewport->update();
    m_timeline->update();
    m_props->refresh();
    updateStatus();
}

void MainWindow::togglePlayback()
{
    if (m_playing)
    {
        m_playTimer->stop();
        m_playing = false;
        m_playAction->setText(Lang::t(QStringLiteral("action.play"), QStringLiteral("播放")));
        return;
    }

    if (m_frame >= m_project->timelineLength())
        setFrame(0);

    m_playStart = m_frame;
    m_playClock.restart();
    m_playTimer->start(std::max(1, 1000 / std::max(1, m_project->fps)));
    m_playing = true;
    m_playAction->setText(Lang::t(QStringLiteral("action.pause"), QStringLiteral("暂停")));
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

} // namespace pvf
