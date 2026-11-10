#include "MainWindow.h"

#include "EditorSession.h"
#include "NewProjectDialog.h"
#include "core/Commands.h"
#include "core/Camera.h"
#include "core/CheckpointManager.h"
#include "core/Lang.h"
#include "core/PerfMonitor.h"
#include "audio/AudioEngine.h"
#include "media/MediaImporter.h"
#include "media/VideoProxy.h"
#include "core/Settings.h"
#include "html/HtmlBrowser.h"
#include "html/HtmlRenderer.h"
#include "ExportDialog.h"
#include "ExportProgressDialog.h"
#include "SettingsDialog.h"
#include "media/VideoPlayer.h"
#include "mcp/McpCommandService.h"
#include "mcp/McpServer.h"
#include "render/CameraRenderer.h"
#include "render/VideoExporter.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "model/Project.h"
#include "model/Query.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CameraMonitor.h"
#include "ui/CollapsibleSection.h"
#include "ui/CurveEditor.h"
#include "ui/EnvironmentPanel.h"
#include "ui/McpLogPanel.h"
#include "ui/ProjectPropertiesPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/Viewport.h"
#include "ui/WaveformView.h"
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

class SceneDialog : public QDialog
{
public:
    SceneDialog(Project* project, const Scene* scene, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_project(project)
    {
        setWindowTitle(scene ? QStringLiteral("编辑场景") : QStringLiteral("新建场景"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        layout->addLayout(form);

        m_name = new QLineEdit(this);
        m_name->setText(scene ? scene->name
                              : QStringLiteral("场景 %1")
                                    .arg(project->scenes().size() + 1));
        form->addRow(QStringLiteral("名称"), m_name);

        const int maximum = std::max(project->timelineLength(),
                                     scene ? scene->endFrame : 0);
        m_start = new QSpinBox(this);
        m_start->setRange(0, maximum);
        m_end = new QSpinBox(this);
        m_end->setRange(0, maximum);
        form->addRow(QStringLiteral("起始帧"), m_start);
        form->addRow(QStringLiteral("结束帧"), m_end);

        m_color = scene ? scene->color : QColor(90, 170, 255);
        m_colorButton = new QPushButton(this);
        m_colorButton->setObjectName(QStringLiteral("sceneColorButton"));
        form->addRow(QStringLiteral("颜色"), m_colorButton);
        connect(m_colorButton, &QPushButton::clicked, this, [this] {
            const QColor chosen =
                QColorDialog::getColor(m_color, this, QStringLiteral("场景颜色"));
            if (chosen.isValid())
            {
                m_color = chosen;
                updateColorButton();
            }
        });
        updateColorButton();

        m_note = new QPlainTextEdit(this);
        m_note->setMaximumHeight(90);
        m_note->setPlaceholderText(QStringLiteral("这一段是什么"));
        form->addRow(QStringLiteral("备注"), m_note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &SceneDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        if (scene)
        {
            m_editId = scene->id;
            m_start->setValue(scene->startFrame);
            m_end->setValue(scene->endFrame);
            m_note->setPlainText(scene->note);
        }
        else
        {
            m_start->setValue(0);
            m_end->setValue(maximum);
        }
    }

    QString name() const { return m_name->text().trimmed(); }
    int startFrame() const { return m_start->value(); }
    int endFrame() const { return m_end->value(); }
    QColor color() const { return m_color; }
    QString note() const { return m_note->toPlainText(); }

    void accept() override
    {
        QString error;
        if (!validate(&error))
        {
            QMessageBox::warning(this, QStringLiteral("场景"), error);
            return;
        }
        QDialog::accept();
    }

private:
    bool validate(QString* error) const
    {
        if (name().isEmpty())
        {
            *error = QStringLiteral("场景名称不能为空");
            return false;
        }
        if (startFrame() > endFrame())
        {
            *error = QStringLiteral("场景结束帧不能早于起始帧");
            return false;
        }
        if (endFrame() > m_project->timelineLength())
        {
            *error = QStringLiteral("场景结束帧 %1 超出时间轴长度 %2")
                         .arg(endFrame())
                         .arg(m_project->timelineLength());
            return false;
        }
        for (const Scene& scene : m_project->scenes())
        {
            if (!m_editId.isEmpty() && scene.id == m_editId)
                continue;
            if (startFrame() <= scene.endFrame && scene.startFrame <= endFrame())
            {
                *error = QStringLiteral("场景区间与「%1」重叠").arg(scene.name);
                return false;
            }
        }
        return true;
    }

    void updateColorButton()
    {
        m_colorButton->setText(m_color.name());
        const bool dark = m_color.lightness() < 140;
        m_colorButton->setStyleSheet(
            QStringLiteral("background:%1;color:%2;")
                .arg(m_color.name(), dark ? QStringLiteral("white")
                                          : QStringLiteral("black")));
    }

    Project* m_project = nullptr;
    QLineEdit* m_name = nullptr;
    QSpinBox* m_start = nullptr;
    QSpinBox* m_end = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QColor m_color;
    QString m_editId;
};

class AnnotationDialog : public QDialog
{
public:
    AnnotationDialog(int frame, const Annotation* annotation,
                     QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(annotation ? QStringLiteral("编辑注释")
                                  : QStringLiteral("新建注释"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        layout->addLayout(form);

        m_title = new QLineEdit(this);
        m_title->setText(annotation ? annotation->title : QString());
        m_title->setPlaceholderText(QStringLiteral("这一帧发生了什么"));
        form->addRow(QStringLiteral("标题"), m_title);

        m_frame = new QSpinBox(this);
        m_frame->setRange(0, 1000000);
        m_frame->setValue(annotation ? annotation->frame : std::max(0, frame));
        form->addRow(QStringLiteral("帧"), m_frame);

        m_tags = new QLineEdit(this);
        m_tags->setPlaceholderText(QStringLiteral("字幕, 场景, 副歌, 镜头"));
        if (annotation)
            m_tags->setText(annotation->tags.join(QStringLiteral(", ")));
        form->addRow(QStringLiteral("标签"), m_tags);

        m_color = annotation ? annotation->color : QColor(255, 190, 60);
        m_colorButton = new QPushButton(this);
        m_colorButton->setObjectName(QStringLiteral("annotationColorButton"));
        form->addRow(QStringLiteral("颜色"), m_colorButton);
        connect(m_colorButton, &QPushButton::clicked, this, [this] {
            const QColor chosen =
                QColorDialog::getColor(m_color, this, QStringLiteral("注释颜色"));
            if (chosen.isValid())
            {
                m_color = chosen;
                updateColorButton();
            }
        });
        updateColorButton();

        m_note = new QPlainTextEdit(this);
        m_note->setPlaceholderText(QStringLiteral("Markdown / 纯文本备注"));
        m_note->setMinimumHeight(120);
        if (annotation)
            m_note->setPlainText(annotation->note);
        form->addRow(QStringLiteral("备注"), m_note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QString title() const { return m_title->text().trimmed(); }
    int frame() const { return m_frame->value(); }
    QStringList tags() const
    {
        QStringList result;
        for (QString tag : m_tags->text().split(QRegularExpression(
                 QStringLiteral("[,，]")), Qt::SkipEmptyParts))
        {
            tag = tag.trimmed();
            if (!tag.isEmpty() && !result.contains(tag))
                result.append(tag);
        }
        return result;
    }
    QColor color() const { return m_color; }
    QString note() const { return m_note->toPlainText(); }

private:
    void updateColorButton()
    {
        m_colorButton->setText(m_color.name());
        const bool dark = m_color.lightness() < 140;
        m_colorButton->setStyleSheet(
            QStringLiteral("background:%1;color:%2;")
                .arg(m_color.name(), dark ? QStringLiteral("white")
                                          : QStringLiteral("black")));
    }

    QLineEdit* m_title = nullptr;
    QSpinBox* m_frame = nullptr;
    QLineEdit* m_tags = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QColor m_color;
};

// Content-addressed assets never change, but packAssets can be off and the
// user may edit the source file in place. The stamp keeps the parse cache and
// preview cache key honest without hashing the document every frame.
QString htmlFileStamp(const QString& path)
{
    const QFileInfo info(path);
    return QStringLiteral("%1|%2")
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

} // namespace

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
    m_curve = new CurveEditor(m_project, m_history, this);
    m_wave = new WaveformView(m_project, this);
    // No curve/waveform tabs in the clip timeline. Keep the objects for their
    // existing signal wiring, but never let them float over the viewport.
    m_curve->hide();
    m_wave->hide();
    m_workbench = new WorkbenchPanel(m_project, m_viewport);
    m_workbench->setIgnoreMouseWidget(m_viewport->workbenchButton());
    m_workbench->hide();
    m_monitor = new CameraMonitor(m_project, m_viewport, m_viewport);
    m_monitor->setFrame(0);
    m_monitor->setVisible(Settings::monitorVisible());

    m_mcpServer = new McpServer(m_project, this);
    m_mcpServer->setObjectName(QStringLiteral("mcpServer"));
    m_mcpLog = new McpLogPanel(m_viewport);
    m_mcpLog->setLoggingEnabled(Settings::mcpLogVisible());
    m_mcpLog->setServerState(false, 0);
    // A plain child widget becomes visible with its parent; the log stays
    // hidden until the first tool call or an explicit status-bar click.
    m_mcpLog->hide();
    connect(m_mcpServer, &McpServer::toolInvoked, m_mcpLog,
            &McpLogPanel::addToolInvocation);
    connect(m_mcpServer, &McpServer::notificationReceived, this,
            [this](const QString& method, const QString& paramsJson) {
                if (!m_mcpLog)
                    return;
                m_mcpLog->addSystemEvent(
                    QStringLiteral("通知：%1").arg(method),
                    paramsJson.isEmpty() ? QStringLiteral("—") : paramsJson);
            });
    connect(m_mcpLog, &McpLogPanel::entityActivated, this,
            &MainWindow::setSelectedEntity);
    connect(m_mcpLog, &McpLogPanel::frameRequested, this, &MainWindow::setFrame);
    connect(m_mcpLog, &McpLogPanel::stopAiRequested, this,
            &MainWindow::stopMcpWrites);

    // One headless browser is shared by every HTML layer; renderers are
    // created lazily per layer and never outlive the browser.
    m_htmlBrowser = new html::HtmlBrowser(this);
    m_htmlPreviewTimer = new QTimer(this);
    m_htmlPreviewTimer->setSingleShot(true);
    connect(m_htmlPreviewTimer, &QTimer::timeout, this,
            &MainWindow::requestHtmlPreviews);

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
    settingsSection->setExpanded(true);
    environmentSection->setExpanded(false);

    projectLayout->addWidget(settingsSection);
    projectLayout->addWidget(environmentSection);
    projectLayout->addStretch(1);
    projectScroll->setWidget(projectContent);

    auto* projectTabLayout = new QVBoxLayout(m_projectTab);
    projectTabLayout->setContentsMargins(0, 0, 0, 0);
    projectTabLayout->addWidget(projectScroll);

    // Entity properties tab, added on demand when something is selected.
    m_objectTab = new QScrollArea(this);
    m_objectTab->setObjectName(QStringLiteral("objectTab"));
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

    // Asset box on the left of the clip timeline, like the prototype.
    auto* bottom = new QSplitter(Qt::Horizontal, this);
    auto* libraryHost = new QWidget(bottom);
    libraryHost->setObjectName(QStringLiteral("libraryHost"));
    auto* libraryLayout = new QVBoxLayout(libraryHost);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(0);
    libraryLayout->addWidget(m_library);
    libraryHost->setMinimumWidth(240);
    libraryHost->setMaximumWidth(420);
    bottom->addWidget(libraryHost);
    bottom->addWidget(m_timeline);
    bottom->setChildrenCollapsible(false);
    bottom->setStretchFactor(0, 0);
    bottom->setStretchFactor(1, 1);
    bottom->setSizes({300, 1100});
    mainSplit->addWidget(bottom);

    // The clip timeline is the only timeline surface. The curve editor and
    // waveform view stay constructed for their existing signal wiring, but
    // they are no longer tabs in the main layout.
    mainSplit->setChildrenCollapsible(false);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setSizes({520, 380});

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mainSplit);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusInfo"));
    statusBar()->addPermanentWidget(m_status);

    m_mcpStatusButton = new QToolButton(this);
    m_mcpStatusButton->setObjectName(QStringLiteral("mcpStatusButton"));
    m_mcpStatusButton->setAutoRaise(true);
    m_mcpStatusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_mcpStatusButton->setFocusPolicy(Qt::NoFocus);
    connect(m_mcpStatusButton, &QToolButton::clicked, this, [this] {
        if (m_mcpLog)
            m_mcpLog->showFromMenu();
    });
    statusBar()->addPermanentWidget(m_mcpStatusButton);
    updateMcpStatus();

    m_importProgress = new QProgressBar(this);
    m_importProgress->setObjectName(QStringLiteral("importProgress"));
    m_importProgress->setRange(0, 0);
    m_importProgress->setFixedWidth(120);
    m_importProgress->setTextVisible(false);
    m_importProgress->hide();
    statusBar()->addPermanentWidget(m_importProgress);

    m_perfLabel = new QLabel(this);
    m_perfLabel->setObjectName(QStringLiteral("perfInfo"));
    m_perfLabel->setToolTip(QStringLiteral(
        "播放 FPS、每帧耗时、场景/视口/时间轴/视频解码耗时；视图 → 性能监视"));
    m_perfLabel->hide();
    statusBar()->addPermanentWidget(m_perfLabel);

    m_perfTimer = new QTimer(this);
    m_perfTimer->setInterval(500);
    connect(m_perfTimer, &QTimer::timeout, this,
            &MainWindow::updatePerfLabel);
    PerfMonitor::instance().setEnabled(Settings::perfMonitor());
    if (Settings::perfMonitor())
    {
        m_perfLabel->show();
        m_perfTimer->start();
    }

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
        arrangeMcpLog();
    });
    connect(m_mcpLog, &McpLogPanel::visibilityChanged, this,
            [this](bool visible) {
                if (auto* action =
                        findChild<QAction*>(QStringLiteral("mcpLogAction")))
                {
                    const QSignalBlocker blocker(action);
                    action->setChecked(visible);
                }
                if (visible)
                    arrangeMcpLog();
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
    connect(m_curve, &CurveEditor::frameRequested, this, &MainWindow::setFrame);
    connect(m_curve, &CurveEditor::statusMessage, this, [this](const QString& text) {
        statusBar()->showMessage(text, 2000);
    });
    connect(m_wave, &WaveformView::frameRequested, this, &MainWindow::setFrame);
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
    connect(m_timeline, &TimelinePanel::deleteEntityRequested, this,
            [this](EntityId id) {
                if (id == kInvalidEntity)
                    return;
                setSelectedEntity(id);
                deleteSelectedEntity();
            });
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
    connect(m_timeline, &TimelinePanel::snapshotEditRequested, this,
            [this](const QVector<Entity>& before, const QVector<Entity>& after,
                   const QString& text) {
                m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                                    before, after, text),
                                *m_project);
            });
    connect(m_timeline, &TimelinePanel::keyframeAddRequested, this,
            [this](EntityId id, int frame) {
                const Entity* entity = m_project->entity(id);
                if (!entity || m_project->isEffectivelyLocked(id))
                    return;
                const bool camera = entity->isCamera();
                const QVector<Entity> before = m_project->entities();
                const QStringList properties = {
                    QStringLiteral("x"),        QStringLiteral("y"),
                    QStringLiteral("rotation"), QStringLiteral("scaleX"),
                    QStringLiteral("scaleY"),   QStringLiteral("opacity")};
                for (const QString& property : properties)
                {
                    if (camera && property == QStringLiteral("opacity"))
                        continue;
                    m_project->setValue(id, property, frame,
                                        m_project->value(id, property, frame),
                                        true);
                }
                m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                                    before, m_project->entities(),
                                    QStringLiteral("打关键帧")),
                                *m_project);
                statusBar()->showMessage(
                    QStringLiteral("已在第 %1 帧打关键帧").arg(frame), 2000);
            });
    connect(m_timeline, &TimelinePanel::folderCreateRequested, this,
            &MainWindow::onCreateFolderRequested);
    connect(m_timeline, &TimelinePanel::sceneCreateRequested, this,
            [this] { showSceneDialog(); });
    connect(m_timeline, &TimelinePanel::sceneEditRequested, this,
            [this](const QString& id) { showSceneDialog(id); });
    connect(m_timeline, &TimelinePanel::sceneDeleteRequested, this,
            &MainWindow::commitSceneRemove);
    connect(m_timeline, &TimelinePanel::sceneRetireRequested, this,
            &MainWindow::commitSceneRetired);
    connect(m_timeline, &TimelinePanel::annotationCreateRequested, this,
            [this](int frame) { showAnnotationDialog(QString(), frame); });
    connect(m_timeline, &TimelinePanel::annotationEditRequested, this,
            [this](const QString& id) { showAnnotationDialog(id); });
    connect(m_timeline, &TimelinePanel::annotationDeleteRequested, this,
            &MainWindow::commitAnnotationRemove);
    connect(m_timeline, &TimelinePanel::keyframeNoteRequested, this,
            &MainWindow::editKeyframeNote);
    connect(m_curve, &CurveEditor::keyframeNoteRequested, this,
            &MainWindow::editKeyframeNote);
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
    // the left, project properties on the right.  Timeline/asset panels are
    // intentionally absent until P2/P5.
    auto* top = new QSplitter(Qt::Horizontal, this);
    top->setObjectName(QStringLiteral("sessionTopSplit"));
    top->setChildrenCollapsible(false);

    // P3a: the old viewport widget is back, drawing the new entity model
    // through render2.  The P1 read-only placeholder page is gone.
    m_viewport = new Viewport(m_session, top);
    m_viewport->setObjectName(QStringLiteral("viewport"));
    m_viewport->setMinimumWidth(480);

    // P3b: the legacy floating monitor, rendering through render2 and the
    // per-project media services.  It starts docked in the viewport corner.
    if (m_session)
    {
        m_monitor = new CameraMonitor(m_session, m_viewport, m_viewport);
        m_monitor->setFrame(m_session->playhead());
        m_monitor->setVisible(Settings::monitorVisible());
    }

    m_sessionProps = new ProjectPropertiesPanel(m_session, this);
    m_sessionEnvironment = new EnvironmentPanel(m_session, this);

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
    projectLayout->addWidget(settingsSection);
    projectLayout->addWidget(environmentSection);
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

    top->addWidget(m_viewport);
    top->addWidget(m_rightTabs);
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 0);
    top->setSizes({1100, 320});

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Legacy arrangement: the editing surface sits above the timeline.  P2a
    // brings the old TimelinePanel back below the placeholder; the right-hand
    // project properties tab is unchanged.
    auto* mainSplit = new QSplitter(Qt::Vertical, central);
    mainSplit->setObjectName(QStringLiteral("sessionMainSplit"));
    mainSplit->setChildrenCollapsible(false);
    mainSplit->addWidget(top);
    m_timeline = new TimelinePanel(
        static_cast<EditorSession*>(m_session), mainSplit);
    m_timeline->setObjectName(QStringLiteral("timelinePanel"));
    mainSplit->addWidget(m_timeline);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setSizes({620, 280});
    layout->addWidget(mainSplit);
    setCentralWidget(central);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusInfo"));
    statusBar()->addPermanentWidget(m_status);

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
    }

    createSessionMenus();
    createSessionToolBar();
    if (m_viewport)
        m_viewport->setAutoKeyEnabled(!m_autoKeyAction
                                      || m_autoKeyAction->isChecked());
    refreshSession();
    updateSessionPlaybackUi();
    updateWindowTitle();
}

void MainWindow::createSessionMenus()
{
    auto* fileMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.file"), QStringLiteral("文件")));

    auto* newAction = new QAction(QStringLiteral("新建项目…"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::createSessionProject);
    fileMenu->addAction(newAction);

    auto* openAction = new QAction(QStringLiteral("打开项目…"), this);
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
    });
    editMenu->addAction(m_autoKeyAction);

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
    viewMenu->addAction(m_fitAction);

    auto* helpMenu =
        menuBar()->addMenu(Lang::t(QStringLiteral("menu.help"), QStringLiteral("帮助")));
    helpMenu->addAction(
        Lang::t(QStringLiteral("action.about"), QStringLiteral("关于 PVForge")), this,
        [this] {
            QMessageBox::about(
                this, QStringLiteral("关于 PVForge"),
                QStringLiteral("PVForge %1\n\n旧 UI 接入 P3a：目录工程、工程属性、可编辑时间轴、render2 视口、播放与工作台缓存已接通。")
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
    m_status->setText(
        QStringLiteral("%1 × %2 @ %3 fps · 帧 %4 / 总长 %5 · %6 · 选中 %7")
            .arg(settings.width)
            .arg(settings.height)
            .arg(settings.fps)
            .arg(m_session->playhead())
            .arg(totalEnd)
            .arg(playing ? QStringLiteral("播放中") : QStringLiteral("暂停"))
            .arg(selection));
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

void MainWindow::openSessionProject()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("打开项目（选择 .pvproj 工程目录）"), QString(),
        QFileDialog::ShowDirsOnly);
    if (path.isEmpty())
        return;

    QString error;
    auto session = EditorSession::openProject(path, &error);
    if (!session)
    {
        QMessageBox::warning(this, QStringLiteral("无法打开项目"), error);
        return;
    }

    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
}

void MainWindow::createSessionProject()
{
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
    auto session = EditorSession::createProject(path, settings, &error);
    if (!session)
    {
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
    // Stop MCP before the HTTP server and log panel children are destroyed;
    // this also removes the endpoint file if the window bypassed closeEvent.
    if (m_mcpServer)
    {
        disarmMcpWrites();
        m_mcpServer->stop();
    }
    if (m_exporter)
    {
        m_exporter->disconnect();
        m_exporter->cancel();
    }
    // Renderers hold a raw pointer to the shared browser, so destroy them
    // before it. Deleting the browser closes the headless process and its
    // pages.
    if (m_htmlPreviewTimer)
        m_htmlPreviewTimer->stop();
    for (html::HtmlRenderer* renderer : m_htmlRenderers)
    {
        if (renderer)
        {
            renderer->cancel(QStringLiteral("window closed"));
            delete renderer;
        }
    }
    m_htmlRenderers.clear();
    if (m_htmlBrowser)
    {
        m_htmlBrowser->stop();
        delete m_htmlBrowser;
        m_htmlBrowser = nullptr;
    }
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

    m_exportAction = new QAction(
        Lang::t(QStringLiteral("action.exportVideo"), QStringLiteral("导出视频…")), this);
    m_exportAction->setObjectName(QStringLiteral("exportAction"));
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportVideo);
    addAction(m_exportAction);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(Lang::t(QStringLiteral("menu.file"), QStringLiteral("文件")));
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAction);
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
        SettingsDialog dialog(this, m_mcpServer);
        wireMcpDialog(&dialog);
        if (dialog.exec() == QDialog::Accepted)
        {
            m_viewport->setSnapPixels(Settings::snapPixels());
            m_viewport->setEdgeCenterSnap(Settings::edgeCenterSnap());
            updateAutosaveTimer();
            if (m_mcpLog)
                m_mcpLog->setLoggingEnabled(Settings::mcpLogVisible());
            updateMcpStatus();
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
    auto* mcpLogAction = new QAction(
        Lang::t(QStringLiteral("action.mcpLog"), QStringLiteral("MCP 日志")), this);
    mcpLogAction->setObjectName(QStringLiteral("mcpLogAction"));
    mcpLogAction->setCheckable(true);
    mcpLogAction->setChecked(m_mcpLog && m_mcpLog->isVisible());
    connect(mcpLogAction, &QAction::toggled, this, [this](bool on) {
        if (!m_mcpLog)
            return;
        if (on)
            m_mcpLog->showFromMenu();
        else
            m_mcpLog->hide();
    });
    viewMenu->addAction(mcpLogAction);
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

    viewMenu->addSeparator();
    m_perfAction = new QAction(QStringLiteral("性能监视"), this);
    m_perfAction->setObjectName(QStringLiteral("perfMonitorAction"));
    m_perfAction->setCheckable(true);
    m_perfAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    m_perfAction->setChecked(Settings::perfMonitor());
    connect(m_perfAction, &QAction::toggled, this, [this](bool on) {
        Settings::setPerfMonitor(on);
        PerfMonitor::instance().setEnabled(on);
        m_perfLabel->setVisible(on);
        if (on)
        {
            updatePerfLabel();
            m_perfTimer->start();
        }
        else
        {
            m_perfTimer->stop();
        }
    });
    viewMenu->addAction(m_perfAction);

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
    if (m_session)
    {
        const bool pending = m_session->document()
                             && m_session->document()->hasPendingCommit();
        setWindowTitle(QStringLiteral("PVForge — %1%2")
                           .arg(m_session->name(),
                                pending ? QStringLiteral(" *") : QString()));
        return;
    }
    if (!m_project)
    {
        setWindowTitle(QStringLiteral("PVForge"));
        return;
    }

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
    if (m_session)
    {
        // P2a has no session-level delete; the timeline is read-only.
        QMainWindow::keyPressEvent(event);
        return;
    }
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
    if (m_session)
        return saveSessionProject();
    if (!m_project)
        return false;

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

void MainWindow::scheduleHtmlPreview(bool immediate)
{
    if (!m_htmlPreviewTimer || m_htmlExporting)
        return;
    if (htmlLayersForFrame(m_frame).isEmpty())
        return;

    int delay = 0;
    if (m_playing && !immediate && m_htmlPreviewClock.isValid())
        delay = std::max(0, 66 - int(m_htmlPreviewClock.elapsed()));
    m_htmlPreviewTimer->start(delay);
}

void MainWindow::requestHtmlPreviews()
{
    if (m_htmlExporting)
        return;

    const QVector<EntityId> layers = htmlLayersForFrame(m_frame);
    if (layers.isEmpty())
        return;

    m_htmlPreviewClock.start();
    if (!m_htmlBrowser->isRunning())
    {
        const QString executable = html::HtmlBrowser::detectExecutable(
            m_htmlBrowser->requestedExecutable());
        if (executable.isEmpty())
        {
            for (EntityId id : layers)
                if (Entity* entity = m_project->entity(id))
                {
                    entity->htmlImage = QImage();
                    entity->htmlError = QStringLiteral(
                        "未找到 Chrome/Edge/Chromium；请安装浏览器后重启预览");
                    entity->pixmap = QPixmap();
                    entity->thumbnail = QImage();
                }
            m_viewport->update();
            if (m_monitor)
                m_monitor->update();
            return;
        }
        if (m_htmlBrowserRetryClock.isValid()
            && m_htmlBrowserRetryClock.elapsed() < 3000)
            return;
    }

    const QSize size = htmlPreviewSize();
    for (EntityId id : layers)
    {
        Entity* entity = m_project->entity(id);
        if (!entity || entity->type != EntityType::HtmlLayer)
            continue;

        html::HtmlRenderer* renderer = htmlRendererFor(id);
        if (!renderer)
            continue;

        QString assetError;
        const html::HtmlAsset asset = htmlAssetFor(*entity, &assetError);
        if (!assetError.isEmpty())
        {
            applyHtmlFrame(id, ++m_htmlTokens[id], QString(), QImage(), assetError);
            continue;
        }

        QHash<QString, double> vars;
        for (const html::HtmlComponent& component : asset.components)
            for (const html::HtmlVariable& variable : component.variables)
            {
                const QString key =
                    component.id + QLatin1Char('.') + variable.name;
                vars.insert(key, m_project->value(
                                     id, QStringLiteral("var.") + key, m_frame));
            }

        const bool timeDependent = entity->htmlTimeDependent();
        QString cacheKey = QStringLiteral("%1|%2|%3x%4|%5")
                               .arg(entity->htmlAssetPath())
                               .arg(htmlFileStamp(entity->htmlAssetPath()))
                               .arg(size.width())
                               .arg(size.height())
                               .arg(timeDependent ? m_frame : 0);
        QList<QString> varNames = vars.keys();
        std::sort(varNames.begin(), varNames.end());
        for (const QString& key : varNames)
            cacheKey += QStringLiteral("|%1=%2").arg(key).arg(vars.value(key), 0, 'g', 17);
        if (!entity->htmlImage.isNull()
            && m_htmlCacheKeys.value(id) == cacheKey)
            continue;
        // One capture per layer at a time. During playback a slower browser
        // must finish the current frame instead of being superseded every
        // tick; the next tick will pick up the newest playhead.
        if (m_htmlInFlight.contains(id))
            continue;

        html::FrameRequest request;
        request.asset = asset;
        request.pageSize = size;
        request.vars = vars;
        request.frame = m_frame;
        request.time = m_frame / double(std::max(1, m_project->fps));
        request.deterministic = true;
        request.timeoutMs = 1200;

        const quint64 token = ++m_htmlTokens[id];
        m_htmlInFlight.insert(id);
        renderer->requestFrame(
            request, [this, id, token, cacheKey](const QImage& image,
                                                 const QString& error) {
                applyHtmlFrame(id, token, cacheKey, image, error);
            });
    }
}

void MainWindow::cleanupHtmlRenderers()
{
    for (auto it = m_htmlRenderers.begin(); it != m_htmlRenderers.end();)
    {
        Entity* entity = m_project->entity(it.key());
        if (entity && entity->type == EntityType::HtmlLayer)
        {
            ++it;
            continue;
        }
        html::HtmlRenderer* renderer = it.value();
        if (renderer)
        {
            renderer->cancel(QStringLiteral("HTML layer removed"));
            delete renderer;
        }
        m_htmlCacheKeys.remove(it.key());
        m_htmlTokens.remove(it.key());
        m_htmlInFlight.remove(it.key());
        it = m_htmlRenderers.erase(it);
    }
}

QVector<EntityId> MainWindow::htmlLayersForFrame(int frame) const
{
    QVector<EntityId> layers;
    for (const Entity& entity : m_project->entities())
    {
        if (entity.type != EntityType::HtmlLayer)
            continue;
        if (m_project->isEffectivelyHidden(entity.id))
            continue;
        if (m_project->effectiveOpacity(entity.id, frame) <= 0.0)
            continue;
        layers.append(entity.id);
        if (layers.size() >= html::HtmlBrowser::kMaxLayers)
            break;
    }
    return layers;
}

QSize MainWindow::htmlPreviewSize() const
{
    QSize size(std::max(1, m_project->width), std::max(1, m_project->height));
    const Entity* selected = m_project->entity(m_selected);
    const bool selectedHtml =
        selected && selected->type == EntityType::HtmlLayer;
    if (m_playing && !selectedHtml && m_viewport)
    {
        const QSize view = m_viewport->size();
        size.setWidth(std::min(size.width(), std::max(1, view.width())));
        size.setHeight(std::min(size.height(), std::max(1, view.height())));
    }
    return QSize(std::clamp(size.width(), 1, html::Limits::kMaxEdge),
                 std::clamp(size.height(), 1, html::Limits::kMaxEdge));
}

html::HtmlAsset MainWindow::htmlAssetFor(const Entity& entity, QString* error)
{
    const QString path = entity.htmlAssetPath();
    if (path.isEmpty())
    {
        if (error)
            *error = QStringLiteral("HTML 层没有资产");
        return {};
    }
    const auto cached = m_htmlAssets.constFind(path);
    const QString stamp = htmlFileStamp(path);
    if (cached != m_htmlAssets.constEnd()
        && m_htmlAssetStamps.value(path) == stamp)
    {
        if (error)
            error->clear();
        return cached.value();
    }

    QString parseError;
    const html::HtmlAsset asset = html::HtmlAsset::parseFile(path, &parseError);
    if (!parseError.isEmpty())
    {
        if (error)
            *error = parseError;
        return {};
    }
    m_htmlAssets.insert(path, asset);
    m_htmlAssetStamps.insert(path, stamp);
    if (error)
        error->clear();
    return asset;
}

html::HtmlRenderer* MainWindow::htmlRendererFor(EntityId id)
{
    if (html::HtmlRenderer* existing = m_htmlRenderers.value(id, nullptr))
        return existing;
    if (!m_htmlBrowser)
        return nullptr;
    auto* renderer = new html::HtmlRenderer(m_htmlBrowser, this);
    m_htmlRenderers.insert(id, renderer);
    return renderer;
}

void MainWindow::applyHtmlFrame(EntityId id, quint64 token, const QString& cacheKey,
                                const QImage& image, const QString& error)
{
    m_htmlInFlight.remove(id);
    if (m_htmlTokens.value(id, 0) != token)
        return;   // superseded request; never let an old frame overwrite a new one
    Entity* entity = m_project->entity(id);
    if (!entity || entity->type != EntityType::HtmlLayer)
        return;

    if (image.isNull())
    {
        entity->htmlImage = QImage();
        entity->htmlError = error.isEmpty() ? QStringLiteral("HTML 抓帧失败") : error;
        entity->pixmap = QPixmap();
        entity->thumbnail = QImage();
        m_htmlCacheKeys.remove(id);
        if (m_htmlBrowser && !m_htmlBrowser->isRunning())
            m_htmlBrowserRetryClock.start();
    }
    else
    {
        entity->htmlImage = image;
        entity->htmlError.clear();
        entity->pixmap = QPixmap();
        entity->thumbnail = QImage();
        m_htmlCacheKeys.insert(id, cacheKey);
    }
    m_viewport->update();
    if (m_monitor)
        m_monitor->update();
    if (!image.isNull() && !m_htmlExporting)
        scheduleHtmlPreview(!m_playing);
}

void MainWindow::exportVideo()
{
    if (m_playing)
        togglePlayback();
    m_audioEngine->stop();

    ExportDialog dialog(*m_project, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ExportOptions options = dialog.options();
    const QSize renderSize(std::max(2, options.size.width() & ~1),
                           std::max(2, options.size.height() & ~1));

    auto* progress = new ExportProgressDialog(this);
    progress->setProgress(0, options.frameCount);
    progress->show();
    // Let the preview window paint before the (synchronous) audio mix.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    auto* exporter = new VideoExporter(this);
    m_exporter = exporter;
    connect(exporter, &VideoExporter::progress, progress,
            &ExportProgressDialog::setProgress);
    connect(exporter, &VideoExporter::previewFrame, progress,
            &ExportProgressDialog::setPreview);
    connect(progress, &ExportProgressDialog::cancelRequested, exporter,
            &VideoExporter::cancel);
    connect(exporter, &VideoExporter::finished, this,
            [this, progress, exporter, options](bool success, const QString& error) {
                progress->finish();
                progress->deleteLater();
                exporter->deleteLater();
                if (m_exporter == exporter)
                    m_exporter = nullptr;
                m_htmlExporting = false;

                // The export decoded video entities from their source files;
                // put the preview back on the proxy at the current playhead.
                m_lastScrubFrame = m_frame;
                m_videoPlayer.clear();
                setFrame(m_frame);

                if (success)
                {
                    QMessageBox::information(
                        this, QStringLiteral("导出完成"),
                        QStringLiteral("已导出：\n%1").arg(options.outputPath));
                }
                else if (!error.isEmpty())
                {
                    const QString detail = m_htmlExportError.isEmpty()
                                               ? error
                                               : m_htmlExportError;
                    m_htmlExportError.clear();
                    QMessageBox::warning(this, QStringLiteral("导出失败"), detail);
                }
            });

    const EntityId cameraId = options.cameraId;
    m_htmlExporting = true;
    m_htmlExportError.clear();
    // The exporter requests one frame at a time. HTML layers are captured
    // serially first (at output resolution), then the whole scene is
    // composited and handed back. A failed capture answers with a null image
    // so the exporter aborts instead of writing a placeholder.
    auto frameProvider = [this, cameraId, renderSize](
                             int frame, VideoExporter::FrameDone done) {
        m_videoPlayer.update(*m_project, frame, false);
        Camera::State state;
        const Entity* camera = m_project->entity(cameraId);
        if (camera && camera->isCamera())
            state = Camera::state(*m_project, cameraId, frame);
        if (state.size.isEmpty())
        {
            state.scale = 1.0;
            state.rotation = 0.0;
            state.center = QPointF();
            state.size = QSizeF(m_project->width, m_project->height);
        }

        const QVector<EntityId> layers = htmlLayersForFrame(frame);
        auto index = std::make_shared<int>(0);
        auto next = std::make_shared<std::function<void()>>();

        *next = [this, frame, layers, state, renderSize, index, next, done]() {
            if (*index >= layers.size())
            {
                done(CameraRenderer::renderCameraImage(*m_project, frame, state,
                                                       renderSize, false));
                return;
            }

            const EntityId id = layers[(*index)++];
            Entity* entity = m_project->entity(id);
            if (!entity || entity->type != EntityType::HtmlLayer)
            {
                (*next)();
                return;
            }

            QString assetError;
            const html::HtmlAsset asset = htmlAssetFor(*entity, &assetError);
            if (!assetError.isEmpty())
            {
                qWarning() << "HTML export failed:" << assetError;
                m_htmlExportError =
                    QStringLiteral("HTML 层素材无法读取：%1").arg(assetError);
                done(QImage());
                return;
            }

            html::HtmlRenderer* renderer = htmlRendererFor(id);
            if (!renderer)
            {
                qWarning() << "HTML export failed: no renderer for layer" << id;
                m_htmlExportError = QStringLiteral("HTML 层无法创建渲染器");
                done(QImage());
                return;
            }

            const QSize logical = entity->htmlLogicalSize();
            const double scaleX =
                renderSize.width() / std::max(1.0, state.size.width());
            const double scaleY =
                renderSize.height() / std::max(1.0, state.size.height());
            QSize captureSize(qRound(logical.width() * scaleX),
                              qRound(logical.height() * scaleY));
            captureSize.setWidth(
                std::clamp(captureSize.width(), 1, html::Limits::kMaxEdge));
            captureSize.setHeight(
                std::clamp(captureSize.height(), 1, html::Limits::kMaxEdge));

            QHash<QString, double> vars;
            for (const html::HtmlComponent& component : asset.components)
                for (const html::HtmlVariable& variable : component.variables)
                {
                    const QString key =
                        component.id + QLatin1Char('.') + variable.name;
                    vars.insert(key, m_project->value(
                                         id, QStringLiteral("var.") + key, frame));
                }

            html::FrameRequest request;
            request.asset = asset;
            request.pageSize = captureSize;
            request.vars = vars;
            request.frame = frame;
            request.time = frame / double(std::max(1, m_project->fps));
            request.deterministic = true;
            request.timeoutMs = 2000;

            const quint64 token = ++m_htmlTokens[id];
            renderer->requestFrame(
                request, [this, id, token, next, done](const QImage& image,
                                                       const QString& error) {
                    if (m_htmlTokens.value(id, 0) != token)
                        return;   // superseded or cancelled export
                    Entity* current = m_project->entity(id);
                    if (!current || current->type != EntityType::HtmlLayer)
                    {
                        m_htmlExportError = QStringLiteral("HTML 层已被删除");
                        done(QImage());
                        return;
                    }
                    if (image.isNull())
                    {
                        qWarning() << "HTML export frame failed:" << error;
                        current->htmlError = error;
                        m_htmlExportError =
                            QStringLiteral("HTML 层抓帧失败：%1").arg(error);
                        done(QImage());
                        return;
                    }
                    current->htmlImage = image;
                    current->htmlError.clear();
                    current->pixmap = QPixmap();
                    current->thumbnail = QImage();
                    // Export captures happen at output resolution; make the
                    // preview recapture at its own size on the next tick.
                    m_htmlCacheKeys.remove(id);
                    (*next)();
                });
        };
        (*next)();
    };

    QString error;
    if (!exporter->start(*m_project, options, frameProvider, &error))
    {
        m_htmlExporting = false;
        m_exporter = nullptr;
        progress->finish();
        progress->deleteLater();
        exporter->deleteLater();
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    }
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
        if (entity.type == EntityType::HtmlLayer)
        {
            const QString path = entity.htmlAssetPath();
            if (path.isEmpty())
                continue;
            const QString target = m_project->packHtmlAsset(path);
            if (target != path)
            {
                entity.data[QStringLiteral("htmlAsset")] = target;
                ++packed;
            }
            continue;
        }
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

void MainWindow::applyActiveSceneParent(EntityId id)
{
    Entity* created = m_project->entity(id);
    if (!created || created->parent != kInvalidEntity || created->isAudio())
        return;
    const Scene* scene = m_project->sceneAt(m_frame);
    if (!scene || sceneIsRetired(*m_project, *scene))
        return;
    const EntityId folderId = m_project->sceneFolderAt(m_frame);
    if (folderId == kInvalidEntity || folderId == id)
        return;
    const Entity* folder = m_project->entity(folderId);
    if (!folder || !folder->isFolder())
        return;
    if (m_project->isDescendantOf(folderId, id))
        return;
    m_project->setEntityParent(id, folderId);
}

void MainWindow::showSceneDialog(const QString& sceneId)
{
    const Scene* existing =
        sceneId.isEmpty() ? nullptr : m_project->sceneById(sceneId);
    if (!sceneId.isEmpty() && !existing)
        return;

    SceneDialog dialog(m_project, existing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    const bool ok =
        existing ? commitSceneUpdate(sceneId, dialog.name(), dialog.startFrame(),
                                     dialog.endFrame(), dialog.color(),
                                     dialog.note(), &error)
                 : commitSceneCreate(dialog.name(), dialog.startFrame(),
                                     dialog.endFrame(), dialog.color(),
                                     dialog.note(), &error);
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("场景"), error);
}

bool MainWindow::commitSceneCreate(const QString& name, int startFrame, int endFrame,
                                   const QColor& color, const QString& note,
                                   QString* error, QString* createdId)
{
    Scene* scene = m_project->addScene(name, startFrame, endFrame, color, note, error);
    if (!scene)
        return false;

    const Scene record = *scene;
    if (createdId)
        *createdId = record.id;
    const Entity* folder = m_project->entity(record.folderId);
    const Entity folderCopy = folder ? *folder : Entity();
    int folderIndex = 0;
    for (int i = 0; i < int(m_project->entities().size()); ++i)
    {
        if (m_project->entities()[i].id == record.folderId)
        {
            folderIndex = i;
            break;
        }
    }
    m_history->push(std::make_unique<AddSceneCommand>(record, folderCopy, folderIndex),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已新建场景「%1」").arg(record.name), 2000);
    return true;
}

bool MainWindow::commitSceneUpdate(const QString& id, const QString& name,
                                   int startFrame, int endFrame,
                                   const QColor& color, const QString& note,
                                   QString* error)
{
    const Scene* scene = m_project->sceneById(id);
    if (!scene)
    {
        if (error)
            *error = QStringLiteral("场景不存在");
        return false;
    }
    const Scene before = *scene;
    const Entity* folderBefore = m_project->entity(before.folderId);
    const QString oldFolderName = folderBefore ? folderBefore->name : QString();
    const bool syncFolderName = folderBefore && folderBefore->name == before.name;
    if (!m_project->updateScene(id, name, startFrame, endFrame, color, note, error))
        return false;
    const Scene* updated = m_project->sceneById(id);
    if (!updated)
        return false;
    QString newFolderName = oldFolderName;
    if (syncFolderName && updated->name != oldFolderName)
    {
        m_project->setEntityName(updated->folderId, updated->name);
        newFolderName = updated->name;
    }
    m_history->push(std::make_unique<UpdateSceneCommand>(
                        before, *updated, before.folderId, oldFolderName,
                        newFolderName),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已更新场景"), 2000);
    return true;
}

bool MainWindow::commitSceneRemove(const QString& id)
{
    const Scene* scene = m_project->sceneById(id);
    if (!scene)
        return false;
    const Scene record = *scene;
    const Entity* folder = m_project->entity(record.folderId);
    const Entity folderCopy = folder ? *folder : Entity();
    int folderIndex = 0;
    for (int i = 0; i < int(m_project->entities().size()); ++i)
    {
        if (m_project->entities()[i].id == record.folderId)
        {
            folderIndex = i;
            break;
        }
    }
    if (!m_project->removeScene(id))
        return false;
    m_history->push(std::make_unique<RemoveSceneCommand>(record, folderCopy,
                                                        folderIndex),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已删除场景记录，文件夹保留"), 3000);
    return true;
}

bool MainWindow::commitSceneRetired(const QString& id, bool retired)
{
    const Scene* scene = m_project->sceneById(id);
    if (!scene)
        return false;
    const Scene record = *scene;
    const Entity* folder = m_project->entity(record.folderId);
    if (!folder || !folder->isFolder())
        return false;
    if (sceneIsRetired(*m_project, record) == retired)
        return false;

    const QVector<Entity> before = m_project->entities();
    if (retired)
    {
        EntityId retiredRoot = kInvalidEntity;
        for (const Entity& entity : m_project->entities())
        {
            if (entity.id != record.folderId && entity.parent == kInvalidEntity
                && entity.isFolder()
                && entity.name == QStringLiteral("_retired"))
            {
                retiredRoot = entity.id;
                break;
            }
        }
        if (retiredRoot == kInvalidEntity)
        {
            Entity* created = m_project->addFolder(QStringLiteral("_retired"));
            if (!created)
                return false;
            retiredRoot = created->id;
        }

        Entity* current = m_project->entity(record.folderId);
        if (!current)
            return false;
        // The retired path is _retired/<scene name>; keep the folder name in
        // sync even if it was renamed independently in the layer tree.
        if (current->name != record.name)
            m_project->setEntityName(record.folderId, record.name);
        current = m_project->entity(record.folderId);
        if (!current)
            return false;
        // Remember the human values so restore does not depend on undo.
        const double oldOpacity =
            m_project->value(record.folderId, QStringLiteral("opacity"), 0);
        current->data[QStringLiteral("sceneRetired")] = true;
        current->data[QStringLiteral("sceneRetiredOpacity")] = oldOpacity;
        current->data[QStringLiteral("sceneRetiredLocked")] = current->locked;
        current->tracks.remove(QStringLiteral("opacity"));
        current->values[QStringLiteral("opacity")] = 0.0;

        m_project->setEntityParent(record.folderId, retiredRoot);
        m_project->setEntityFlag(record.folderId, EntityFlag::Locked, true);
    }
    else
    {
        Entity* current = m_project->entity(record.folderId);
        if (!current)
            return false;
        const double opacity =
            current->data.value(QStringLiteral("sceneRetiredOpacity"), 1.0).toDouble();
        const bool wasLocked =
            current->data.value(QStringLiteral("sceneRetiredLocked"), false).toBool();

        m_project->setEntityParent(record.folderId, kInvalidEntity);
        m_project->setEntityFlag(record.folderId, EntityFlag::Locked, false);
        current->tracks.remove(QStringLiteral("opacity"));
        current->values[QStringLiteral("opacity")] = opacity;
        current->data.remove(QStringLiteral("sceneRetired"));
        current->data.remove(QStringLiteral("sceneRetiredOpacity"));
        current->data.remove(QStringLiteral("sceneRetiredLocked"));
        m_project->setEntityFlag(record.folderId, EntityFlag::Locked, wasLocked);
    }

    m_project->notifyChanged();
    m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                        before, m_project->entities(),
                        retired ? QStringLiteral("退休场景")
                                : QStringLiteral("恢复场景")),
                    *m_project);
    statusBar()->showMessage(retired ? QStringLiteral("场景已退休到 _retired")
                                     : QStringLiteral("场景已恢复"),
                             3000);
    return true;
}

void MainWindow::showAnnotationDialog(const QString& annotationId, int frame)
{
    const Annotation* existing =
        annotationId.isEmpty() ? nullptr : m_project->annotationById(annotationId);
    if (!annotationId.isEmpty() && !existing)
        return;

    AnnotationDialog dialog(frame, existing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    const bool ok =
        existing ? commitAnnotationUpdate(annotationId, dialog.frame(), dialog.title(),
                                          dialog.note(), dialog.tags(),
                                          dialog.color(), &error)
                 : commitAnnotationCreate(dialog.frame(), dialog.title(),
                                          dialog.note(), dialog.tags(),
                                          dialog.color(), &error);
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("注释"), error);
}

bool MainWindow::commitAnnotationCreate(int frame, const QString& title,
                                        const QString& note,
                                        const QStringList& tags, const QColor& color,
                                        QString* error, QString* createdId)
{
    Annotation* annotation =
        m_project->addAnnotation(frame, title, note, tags, color, error);
    if (!annotation)
        return false;
    const Annotation record = *annotation;
    if (createdId)
        *createdId = record.id;
    m_history->push(std::make_unique<AddAnnotationCommand>(record), *m_project);
    statusBar()->showMessage(QStringLiteral("已新建注释"), 2000);
    return true;
}

bool MainWindow::commitAnnotationUpdate(const QString& id, int frame,
                                        const QString& title, const QString& note,
                                        const QStringList& tags, const QColor& color,
                                        QString* error)
{
    const Annotation* annotation = m_project->annotationById(id);
    if (!annotation)
    {
        if (error)
            *error = QStringLiteral("注释不存在");
        return false;
    }
    const Annotation before = *annotation;
    Annotation updated = before;
    updated.frame = frame;
    updated.title = title;
    updated.note = note;
    updated.tags = tags;
    if (color.isValid())
        updated.color = color;
    if (!m_project->updateAnnotation(updated, error))
        return false;
    const Annotation* current = m_project->annotationById(id);
    if (!current)
        return false;
    m_history->push(std::make_unique<UpdateAnnotationCommand>(before, *current),
                    *m_project);
    statusBar()->showMessage(QStringLiteral("已更新注释"), 2000);
    return true;
}

bool MainWindow::commitAnnotationRemove(const QString& id)
{
    const Annotation* annotation = m_project->annotationById(id);
    if (!annotation)
        return false;
    const Annotation record = *annotation;
    if (!m_project->removeAnnotation(id))
        return false;
    m_history->push(std::make_unique<RemoveAnnotationCommand>(record), *m_project);
    statusBar()->showMessage(QStringLiteral("已删除注释"), 2000);
    return true;
}

bool MainWindow::commitKeyframeNote(EntityId id, double frame, const QString& note)
{
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return false;

    QVector<KeyframeNoteRecord> records;
    bool found = false;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (std::abs(key.frame - frame) > 1e-6)
                continue;
            found = true;
            if (key.note == note)
                continue;
            KeyframeNoteRecord record;
            record.entity = id;
            record.property = it.key();
            record.frame = key.frame;
            record.oldNote = key.note;
            record.newNote = note;
            records.append(record);
        }
    }
    if (!found)
        return false;
    if (records.isEmpty())
        return true;

    m_history->push(std::make_unique<SetKeyframeNoteCommand>(records), *m_project);
    statusBar()->showMessage(note.isEmpty() ? QStringLiteral("已清除关键帧备注")
                                            : QStringLiteral("已保存关键帧备注"),
                             2000);
    return true;
}

void MainWindow::editKeyframeNote(EntityId id, double frame)
{
    const Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
        return;

    QStringList notes;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (std::abs(key.frame - frame) > 1e-6 || key.note.isEmpty()
                || notes.contains(key.note))
                continue;
            notes.append(key.note);
        }
    }

    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(
        this, QStringLiteral("关键帧备注"),
        QStringLiteral("第 %1 帧 · 仅作创作备注，不参与渲染")
            .arg(int(std::lround(frame))),
        notes.join(QStringLiteral("\n")), &ok);
    if (ok)
        commitKeyframeNote(id, frame, note);
}

QString MainWindow::addSceneForTest(const QString& name, int startFrame, int endFrame,
                                    const QColor& color, const QString& note)
{
    QString error;
    QString id;
    if (!commitSceneCreate(name, startFrame, endFrame, color, note, &error, &id))
    {
        qWarning() << "add scene failed" << error;
        return QString();
    }
    return id;
}

bool MainWindow::updateSceneForTest(const QString& id, const QString& name,
                                    int startFrame, int endFrame,
                                    const QColor& color, const QString& note,
                                    QString* error)
{
    return commitSceneUpdate(id, name, startFrame, endFrame, color, note, error);
}

bool MainWindow::removeSceneForTest(const QString& id)
{
    return commitSceneRemove(id);
}

bool MainWindow::setSceneRetiredForTest(const QString& id, bool retired)
{
    return commitSceneRetired(id, retired);
}

QString MainWindow::addAnnotationForTest(int frame, const QString& title,
                                         const QString& note,
                                         const QStringList& tags,
                                         const QColor& color)
{
    QString error;
    QString id;
    if (!commitAnnotationCreate(frame, title, note, tags, color, &error, &id))
    {
        qWarning() << "add annotation failed" << error;
        return QString();
    }
    return id;
}

bool MainWindow::updateAnnotationForTest(const QString& id, int frame,
                                         const QString& title, const QString& note,
                                         const QStringList& tags, const QColor& color,
                                         QString* error)
{
    return commitAnnotationUpdate(id, frame, title, note, tags, color, error);
}

bool MainWindow::removeAnnotationForTest(const QString& id)
{
    return commitAnnotationRemove(id);
}

bool MainWindow::setKeyframeNoteForTest(EntityId id, double frame,
                                        const QString& note)
{
    return commitKeyframeNote(id, frame, note);
}

EntityId MainWindow::createFolderForTest(EntityId parent)
{
    onCreateFolderRequested(parent);
    return m_selected;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
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
        event->accept();
        return;
    }

    if (!m_project)
    {
        event->accept();
        return;
    }

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
    disarmMcpWrites();
    if (m_mcpServer)
        m_mcpServer->stop();
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
    if (m_session)
    {
        sessionGoToFrame(frame);
        return;
    }

    m_frame = std::max(0, frame);
    m_videoPlayer.update(*m_project, m_frame);
    m_viewport->setCurrentFrame(m_frame);
    m_timeline->setCurrentFrame(m_frame);
    m_curve->setCurrentFrame(m_frame);
    m_wave->setCurrentFrame(m_frame);
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

    scheduleHtmlPreview(!m_playing);
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
    m_curve->setSelectedEntity(id);

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

    // A newly selected HTML layer gets a full-quality capture even while
    // playback is running.
    scheduleHtmlPreview(true);
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
    if (expected == EntityType::HtmlLayer)
    {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("更换 HTML 资产"), QString(),
            QStringLiteral("PVForge HTML (*.json);;所有文件 (*)"));
        if (file.isEmpty())
            return;

        QString importError;
        const QString imported = m_project->importHtmlAsset(file, &importError);
        if (imported.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("更换 HTML 失败"), importError);
            return;
        }

        const Entity before = *entity;
        QString error;
        if (!m_project->replaceHtmlAsset(id, imported, &error))
        {
            QMessageBox::warning(this, QStringLiteral("更换 HTML 失败"), error);
            return;
        }
        if (const Entity* after = m_project->entity(id))
            m_history->push(std::make_unique<ReplaceEntitySourcesCommand>(
                                QVector<Entity>{before}, QVector<Entity>{*after}),
                            *m_project);
        m_htmlCacheKeys.remove(id);
        scheduleHtmlPreview(true);
        return;
    }

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
    applyActiveSceneParent(id);

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
    if (m_session)
    {
        refreshSession();
        return;
    }
    if (!m_project)
        return;

    cleanupHtmlRenderers();
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
    m_curve->refreshTracks();
    m_wave->refresh();
    m_props->refresh();
    if (m_monitor)
        m_monitor->setProjectChanged();
    updateStatus();
    updateWindowTitle();
    scheduleHtmlPreview(!m_playing);
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

    if (m_playing)
    {
        m_playTimer->stop();
        m_scrubTimer->stop();
        m_audioEngine->stop();
        m_playing = false;
        PerfMonitor::instance().setPlaying(false);
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
    PerfMonitor::instance().setPlaying(true);
    m_audioEngine->play(m_frame);
    m_playClock.restart();
    m_playTimer->start(std::max(1, 1000 / std::max(1, m_project->fps)));
    if (m_monitor)
        m_monitor->setPlaying(true);
    m_playAction->setText(Lang::t(QStringLiteral("action.pause"), QStringLiteral("暂停")));
}

void MainWindow::onPlayTick()
{
    PerfMonitor::instance().tick();
    QElapsedTimer frameTimer;
    frameTimer.start();

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
            PerfMonitor::instance().recordFrame(frameTimer.nsecsElapsed() / 1e6);
            togglePlayback();
            return;
        }
    }
    setFrame(f);
    PerfMonitor::instance().recordFrame(frameTimer.nsecsElapsed() / 1e6);
}

void MainWindow::updatePerfLabel()
{
    PerfMonitor::instance().setEntityCount(int(m_project->entities().size()));
    const PerfSnapshot perf = PerfMonitor::instance().snapshot();
    m_perfLabel->setText(
        QStringLiteral("FPS %1 · 帧 %2ms · 场景 %3 · 视口 %4 · 时间轴 %5 · 视频 %6 · %7 实体")
            .arg(perf.fps, 0, 'f', 1)
            .arg(perf.frameMs, 0, 'f', 1)
            .arg(perf.sceneMs, 0, 'f', 1)
            .arg(perf.viewportMs, 0, 'f', 1)
            .arg(perf.timelineMs, 0, 'f', 1)
            .arg(perf.videoMs, 0, 'f', 1)
            .arg(perf.entities));
}

void MainWindow::wireMcpDialog(SettingsDialog* dialog)
{
    connect(dialog, &SettingsDialog::mcpStateChanged, this,
            [this, dialog](bool running, quint16 port, const QString& error) {
                m_mcpLastError = error;
                if (!running)
                    disarmMcpWrites();
                if (m_mcpLog)
                    m_mcpLog->setServerState(running, port, error);
                dialog->setMcpPermissionState(
                    m_mcpCommands && m_mcpCommands->isArmed(),
                    m_mcpCommands ? m_mcpCommands->sceneName() : QString());
                updateMcpStatus();
            });

    connect(dialog, &SettingsDialog::mcpPermissionArmRequested, this,
            [this, dialog] {
                QString error;
                if (!armMcpWrites(&error))
                {
                    dialog->setMcpPermissionState(false, QString(), error);
                    updateMcpStatus();
                    return;
                }
                const QString sceneName = m_mcpCommands->sceneName();
                dialog->setMcpPermissionState(true, sceneName, QString());
                if (m_mcpLog)
                {
                    m_mcpLog->setPermissionState(true, sceneName);
                    m_mcpLog->addSystemEvent(
                        QStringLiteral("已武装：场景 %1").arg(sceneName),
                        m_mcpCheckpointId.isEmpty()
                            ? QString()
                            : QStringLiteral("检查点 %1").arg(m_mcpCheckpointId));
                    if (m_mcpLog->loggingEnabled() && !m_mcpLog->isVisible())
                        m_mcpLog->showFromMenu();
                }
                updateMcpStatus();
            });

    connect(dialog, &SettingsDialog::mcpPermissionDisarmRequested, this,
            [this, dialog] {
                disarmMcpWrites();
                dialog->setMcpPermissionState(false, QString());
                updateMcpStatus();
            });

    dialog->setMcpPermissionState(
        m_mcpCommands && m_mcpCommands->isArmed(),
        m_mcpCommands ? m_mcpCommands->sceneName() : QString());
}

bool MainWindow::armMcpWrites(QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (!m_mcpServer || !m_mcpServer->isRunning())
        return fail(QStringLiteral("请先在“设置 → MCP”启用 MCP 服务。"));

    if (m_project->filePath.isEmpty()
        || !QFileInfo(m_project->filePath).isFile())
        return fail(QStringLiteral("请先保存工程：检查点需要工程的磁盘路径。"));

    if (m_project->modified)
    {
        QString saveError;
        if (!m_project->save(m_project->filePath, &saveError))
            return fail(QStringLiteral("保存工程失败，保持只读：%1").arg(saveError));
        updateWindowTitle();
    }

    const Scene* scene = m_project->sceneAt(m_frame);
    EntityId sceneFolderId = m_project->sceneFolderAt(m_frame);
    if (scene && sceneFolderId != kInvalidEntity)
    {
        const Entity* root = m_project->entity(sceneFolderId);
        if (!root || !root->isFolder())
            return fail(QStringLiteral("当前场景根文件夹不存在，保持只读。"));
    }
    else
    {
        // The clip timeline has no scene bands. With no scene at the
        // playhead, arm the whole timeline instead of blocking the AI
        // behind a scene it cannot create yet.
        scene = nullptr;
        sceneFolderId = kInvalidEntity;
    }

    // The checkpoint must exist before the write service is created. A failed
    // checkpoint leaves the server read-only; there is no "continue without a
    // checkpoint" path in v1.
    if (!m_mcpCheckpoints)
        m_mcpCheckpoints = std::make_unique<CheckpointManager>();

    QFile saved(m_project->filePath);
    if (!saved.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("无法读取工程文件，保持只读：%1")
                        .arg(saved.errorString()));
    const QByteArray projectJson = saved.readAll();
    saved.close();

    QString checkpointError;
    QString checkpointId;
    const QString scopeName =
        scene ? scene->name : QStringLiteral("未分场景（整个时间轴）");
    if (!m_mcpCheckpoints->createCheckpoint(
            m_project->filePath, projectJson,
            QStringLiteral("AI 解锁：%1").arg(scopeName), &checkpointError,
            &checkpointId))
        return fail(QStringLiteral("创建检查点失败，保持只读：%1")
                        .arg(checkpointError));

    m_mcpCommands = std::make_unique<McpCommandService>(
        m_project, m_history, sceneFolderId, true);
    if (!scene)
    {
        QString armError;
        if (!m_mcpCommands->armUnscoped(&armError))
            return fail(QStringLiteral("无法解锁整个时间轴，保持只读：%1")
                            .arg(armError));
    }
    // Global objects (active camera, audio, root logos) remain locked until a
    // separate human confirmation sets setAllowGlobal(true).
    m_mcpCheckpointId = checkpointId;
    m_mcpServer->setCommandService(m_mcpCommands.get());
    m_mcpLastError.clear();
    return true;
}

void MainWindow::disarmMcpWrites()
{
    if (!m_mcpCommands)
    {
        if (m_mcpServer)
            m_mcpServer->setCommandService(nullptr);
        if (m_mcpLog)
            m_mcpLog->setPermissionState(false, QString());
        m_mcpCheckpointId.clear();
        return;
    }

    m_mcpCommands->disarm();
    m_mcpCommands.reset();
    if (m_mcpServer)
        m_mcpServer->setCommandService(nullptr);
    if (m_mcpLog)
    {
        m_mcpLog->setPermissionState(false, QString());
        m_mcpLog->addSystemEvent(QStringLiteral("已切回只读"));
    }
    m_mcpCheckpointId.clear();
}

void MainWindow::stopMcpWrites()
{
    if (!m_mcpCommands)
    {
        statusBar()->showMessage(
            QStringLiteral("没有正在武装的 AI 写权限"), 3000);
        return;
    }

    m_mcpCommands->stop();
    // Keep the stopped service attached so any in-flight or later write call
    // receives stopped_by_human instead of a generic permission error.
    if (m_mcpServer)
        m_mcpServer->setCommandService(m_mcpCommands.get());
    if (m_mcpLog)
    {
        m_mcpLog->setPermissionState(false, QString(), true);
        m_mcpLog->addSystemEvent(
            QStringLiteral("已急停，写操作拒绝"),
            QStringLiteral("人类重新选择“允许修改”后才能再次武装。"));
    }
    // Reset an open settings page so "允许修改" is selectable again; a combo
    // already showing that item would not emit currentIndexChanged.
    for (SettingsDialog* dialog : findChildren<SettingsDialog*>())
        dialog->setMcpPermissionState(false, QString());
    statusBar()->showMessage(
        QStringLiteral("已急停：AI 写操作被拒绝，读操作仍可用"), 5000);
    updateMcpStatus();
}

SettingsDialog* MainWindow::openSettingsDialogForTest()
{
    auto* dialog = new SettingsDialog(this, m_mcpServer);
    dialog->setObjectName(QStringLiteral("settingsDialogTest"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    wireMcpDialog(dialog);
    dialog->show();
    return dialog;
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

void MainWindow::updateMcpStatus()
{
    if (!m_mcpStatusButton)
        return;

    const bool running = m_mcpServer && m_mcpServer->isRunning();
    const bool armed = m_mcpCommands && m_mcpCommands->isArmed();
    const bool stopped = m_mcpCommands && m_mcpCommands->isStopped();

    if (running)
    {
        if (armed)
            m_mcpStatusButton->setText(QStringLiteral("MCP 已武装 · 日志"));
        else if (stopped)
            m_mcpStatusButton->setText(QStringLiteral("MCP 已急停 · 日志"));
        else
            m_mcpStatusButton->setText(QStringLiteral("MCP 已启用 · 只读 · 日志"));
    }
    else if (!m_mcpLastError.isEmpty())
        m_mcpStatusButton->setText(QStringLiteral("MCP 错误 · 日志"));
    else
        m_mcpStatusButton->setText(QStringLiteral("MCP 关闭 · 日志"));

    if (running && armed)
    {
        m_mcpStatusButton->setToolTip(
            QStringLiteral("监听中：%1\n已武装：场景 %2\n点击打开 MCP 日志")
                .arg(m_mcpServer->endpointUrl(),
                     m_mcpCommands->sceneName()));
    }
    else if (running && stopped)
    {
        m_mcpStatusButton->setToolTip(
            QStringLiteral("监听中：%1\n人类已急停，写操作返回 stopped_by_human\n"
                           "点击打开 MCP 日志")
                .arg(m_mcpServer->endpointUrl()));
    }
    else if (running)
    {
        m_mcpStatusButton->setToolTip(
            QStringLiteral("监听中：%1\n只读模式\n点击打开 MCP 日志")
                .arg(m_mcpServer->endpointUrl()));
    }
    else if (!m_mcpLastError.isEmpty())
    {
        m_mcpStatusButton->setToolTip(
            QStringLiteral("MCP 启动失败：%1\n点击打开 MCP 日志")
                .arg(m_mcpLastError));
    }
    else
    {
        m_mcpStatusButton->setToolTip(
            QStringLiteral("MCP 未启用\n点击打开 MCP 日志"));
    }

    QPalette palette = QApplication::palette();
    if (!running && !m_mcpLastError.isEmpty())
        palette.setColor(QPalette::WindowText, QColor(220, 80, 70));
    else if (running && stopped)
        palette.setColor(QPalette::WindowText, QColor(220, 140, 60));
    m_mcpStatusButton->setPalette(palette);
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
