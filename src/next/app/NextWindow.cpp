#include "next/app/NextWindow.h"

#include "next/app/ProjectDocument.h"
#include "next/assets/AssetPanel.h"
#include "next/properties/PropertiesPanel.h"
#include "next/timeline/TimelineWidget.h"
#include "next/viewport/ViewportWidget.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace pvf::next {

namespace {

class NewProjectDialog : public QDialog
{
public:
    explicit NewProjectDialog(const QString& initialParent, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("新建 PVForge 工程"));
        setModal(true);
        resize(560, 340);

        auto* root = new QVBoxLayout(this);

        auto* form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_parentEdit = new QLineEdit(initialParent, this);
        m_parentEdit->setPlaceholderText(tr("工程目录的父目录"));
        auto* browse = new QPushButton(tr("浏览…"), this);
        auto* parentRow = new QHBoxLayout;
        parentRow->setContentsMargins(0, 0, 0, 0);
        parentRow->addWidget(m_parentEdit, 1);
        parentRow->addWidget(browse);
        form->addRow(tr("父目录"), parentRow);

        m_nameEdit = new QLineEdit(tr("未命名项目"), this);
        m_nameEdit->selectAll();
        form->addRow(tr("工程名"), m_nameEdit);

        m_fpsSpin = new QSpinBox(this);
        m_fpsSpin->setRange(1, 240);
        m_fpsSpin->setValue(30);
        m_fpsSpin->setSuffix(tr(" fps"));
        form->addRow(tr("帧率"), m_fpsSpin);

        m_widthSpin = new QSpinBox(this);
        m_widthSpin->setRange(16, 16384);
        m_widthSpin->setValue(1920);
        form->addRow(tr("宽度"), m_widthSpin);

        m_heightSpin = new QSpinBox(this);
        m_heightSpin->setRange(16, 16384);
        m_heightSpin->setValue(1080);
        form->addRow(tr("高度"), m_heightSpin);

        m_background = QColor(QStringLiteral("#101014"));
        m_backgroundButton = new QPushButton(this);
        m_backgroundButton->setMinimumWidth(120);
        form->addRow(tr("背景色"), m_backgroundButton);

        root->addLayout(form);

        auto* hint = new QLabel(
            tr("工程会创建为 <工程名>.pvproj 目录，并自动初始化 git 与 Git LFS。"),
            this);
        hint->setWordWrap(true);
        root->addWidget(hint);

        m_preview = new QLabel(this);
        m_preview->setObjectName(QStringLiteral("newProjectPreview"));
        m_preview->setWordWrap(true);
        m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(m_preview);
        root->addStretch(1);

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
        root->addWidget(m_buttons);

        connect(browse, &QPushButton::clicked, this, [this] { browseParent(); });
        connect(m_backgroundButton, &QPushButton::clicked, this,
                [this] { chooseBackground(); });
        connect(m_nameEdit, &QLineEdit::textChanged, this,
                [this] { updatePreview(); });
        connect(m_parentEdit, &QLineEdit::textChanged, this,
                [this] { updatePreview(); });
        connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
            if (nameIsValid())
                accept();
        });
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        updateBackgroundButton();
        updatePreview();
    }

    QString projectDir() const
    {
        QString name = m_nameEdit->text().trimmed();
        if (!name.endsWith(QStringLiteral(".pvproj"), Qt::CaseInsensitive))
            name += QStringLiteral(".pvproj");
        return QDir(m_parentEdit->text().trimmed()).filePath(name);
    }

    model::ProjectSettings settings() const
    {
        model::ProjectSettings settings;
        QString name = m_nameEdit->text().trimmed();
        if (name.endsWith(QStringLiteral(".pvproj"), Qt::CaseInsensitive))
            name.chop(7);
        settings.name = name;
        settings.width = m_widthSpin->value();
        settings.height = m_heightSpin->value();
        settings.fps = m_fpsSpin->value();
        settings.background = m_background.name();
        return settings;
    }

private:
    bool nameIsValid() const
    {
        const QString name = m_nameEdit->text().trimmed();
        const QString parent = m_parentEdit->text().trimmed();
        return !parent.isEmpty() && !name.isEmpty() && name != QStringLiteral(".")
            && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'));
    }

    void browseParent()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择工程父目录"), m_parentEdit->text());
        if (!dir.isEmpty())
            m_parentEdit->setText(dir);
    }

    void chooseBackground()
    {
        const QColor color = QColorDialog::getColor(
            m_background, this, tr("选择背景色"));
        if (!color.isValid())
            return;
        m_background = color;
        updateBackgroundButton();
    }

    void updateBackgroundButton()
    {
        QPixmap swatch(16, 16);
        swatch.fill(m_background);
        m_backgroundButton->setIcon(QIcon(swatch));
        m_backgroundButton->setText(m_background.name().toUpper());
    }

    void updatePreview()
    {
        m_preview->setText(tr("将创建：%1").arg(QDir::toNativeSeparators(projectDir())));
        if (m_buttons) {
            if (QPushButton* ok = m_buttons->button(QDialogButtonBox::Ok))
                ok->setEnabled(nameIsValid());
        }
    }

    QLineEdit* m_parentEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QPushButton* m_backgroundButton = nullptr;
    QLabel* m_preview = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QColor m_background;
};

QString defaultEntityName(ContentKind kind)
{
    switch (kind) {
    case ContentKind::Empty:
        return QStringLiteral("空实体");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("矢量");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        break;
    }
    return {};
}

} // namespace

NextWindow::NextWindow(std::unique_ptr<ProjectDocument> document, QWidget* parent)
    : QMainWindow(parent)
    , m_ownedDocument(std::move(document))
    , m_document(m_ownedDocument.get())
{
    setObjectName(QStringLiteral("NextWindow"));
    resize(1440, 900);
    setMinimumSize(960, 640);

    m_lastBrowseDir = QDir::homePath();

    buildUi();

    if (m_document) {
        connectDocument();
        buildEditor();
    } else {
        buildWelcome();
    }

    updateActions();
    updateStatus();
    rebuildCameras();
}

NextWindow::~NextWindow() = default;

void NextWindow::setDocument(std::unique_ptr<ProjectDocument> document)
{
    // Remove the old panels before the old document is destroyed: every panel
    // holds a raw Document*.
    if (QWidget* old = takeCentralWidget()) {
        m_assetPanel = nullptr;
        m_viewport = nullptr;
        m_propertiesPanel = nullptr;
        m_timeline = nullptr;
        delete old;
    }

    m_ownedDocument = std::move(document);
    m_document = m_ownedDocument.get();

    if (m_document) {
        connectDocument();
        buildEditor();
    } else {
        buildWelcome();
    }

    updateActions();
    updateStatus();
    rebuildCameras();
}

void NextWindow::buildUi()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    m_newAction = new QAction(tr("新建工程…"), this);
    m_newAction->setObjectName(QStringLiteral("newProjectAction"));
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &NextWindow::chooseNewProject);
    fileMenu->addAction(m_newAction);

    m_openAction = new QAction(tr("打开工程…"), this);
    m_openAction->setObjectName(QStringLiteral("openProjectAction"));
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &NextWindow::chooseOpenProject);
    fileMenu->addAction(m_openAction);

    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("退出"), this);
    m_quitAction->setObjectName(QStringLiteral("quitAction"));
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(m_quitAction);

    QMenu* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    m_undoAction = new QAction(tr("撤销"), this);
    m_undoAction->setObjectName(QStringLiteral("undoAction"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &NextWindow::undo);
    editMenu->addAction(m_undoAction);

    m_redoAction = new QAction(tr("重做"), this);
    m_redoAction->setObjectName(QStringLiteral("redoAction"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &NextWindow::redo);
    editMenu->addAction(m_redoAction);

    QMenu* entityMenu = menuBar()->addMenu(tr("实体(&N)"));
    auto addCreateAction = [this, entityMenu](QAction*& target, const QString& text,
                                              const QString& objectName,
                                              ContentKind kind) {
        target = new QAction(text, this);
        target->setObjectName(objectName);
        connect(target, &QAction::triggered, this, [this, kind] { createEntity(kind); });
        entityMenu->addAction(target);
    };
    addCreateAction(m_createEmptyAction, tr("新建空实体"),
                    QStringLiteral("createEmptyEntityAction"), ContentKind::Empty);
    addCreateAction(m_createTextAction, tr("新建文字"),
                    QStringLiteral("createTextEntityAction"), ContentKind::Text);
    addCreateAction(m_createVectorAction, tr("新建矢量"),
                    QStringLiteral("createVectorEntityAction"), ContentKind::Vector);
    addCreateAction(m_createCameraAction, tr("新建摄像机"),
                    QStringLiteral("createCameraEntityAction"), ContentKind::Camera);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    m_gizmoAction = new QAction(tr("显示 Gizmo"), this);
    m_gizmoAction->setObjectName(QStringLiteral("showGizmoAction"));
    m_gizmoAction->setCheckable(true);
    m_gizmoAction->setChecked(true);
    connect(m_gizmoAction, &QAction::toggled, this, [this](bool checked) {
        if (m_gizmoCheck) {
            QSignalBlocker blocker(m_gizmoCheck);
            m_gizmoCheck->setChecked(checked);
        }
        if (m_viewport)
            m_viewport->setShowGizmo(checked);
    });
    viewMenu->addAction(m_gizmoAction);

    QToolBar* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    toolbar->addAction(m_createEmptyAction);
    toolbar->addAction(m_createTextAction);
    toolbar->addAction(m_createVectorAction);
    toolbar->addAction(m_createCameraAction);
    toolbar->addSeparator();

    auto* playheadLabel = new QLabel(tr("播放头"), toolbar);
    playheadLabel->setContentsMargins(4, 0, 6, 0);
    toolbar->addWidget(playheadLabel);

    m_playheadSpin = new QSpinBox(toolbar);
    m_playheadSpin->setObjectName(QStringLiteral("playheadSpin"));
    m_playheadSpin->setRange(0, 100000);
    m_playheadSpin->setMinimumWidth(90);
    connect(m_playheadSpin, &QSpinBox::valueChanged, this, [this](int frame) {
        if (m_document && m_document->playhead() != frame)
            m_document->setPlayhead(frame);
    });
    toolbar->addWidget(m_playheadSpin);

    m_fpsLabel = new QLabel(tr("fps —"), toolbar);
    m_fpsLabel->setContentsMargins(10, 0, 10, 0);
    toolbar->addWidget(m_fpsLabel);

    m_resolutionLabel = new QLabel(tr("— × —"), toolbar);
    m_resolutionLabel->setContentsMargins(0, 0, 10, 0);
    toolbar->addWidget(m_resolutionLabel);

    toolbar->addSeparator();
    auto* cameraLabel = new QLabel(tr("摄像机"), toolbar);
    cameraLabel->setContentsMargins(4, 0, 6, 0);
    toolbar->addWidget(cameraLabel);

    m_cameraCombo = new QComboBox(toolbar);
    m_cameraCombo->setObjectName(QStringLiteral("cameraCombo"));
    m_cameraCombo->setMinimumWidth(190);
    connect(m_cameraCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_viewport && m_cameraCombo)
            m_viewport->setCameraUuid(m_cameraCombo->currentData().toString());
    });
    toolbar->addWidget(m_cameraCombo);

    m_gizmoCheck = new QCheckBox(tr("Gizmo"), toolbar);
    m_gizmoCheck->setObjectName(QStringLiteral("gizmoCheck"));
    m_gizmoCheck->setChecked(true);
    m_gizmoCheck->setContentsMargins(10, 0, 4, 0);
    connect(m_gizmoCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_gizmoAction) {
            QSignalBlocker blocker(m_gizmoAction);
            m_gizmoAction->setChecked(checked);
        }
        if (m_viewport)
            m_viewport->setShowGizmo(checked);
    });
    toolbar->addWidget(m_gizmoCheck);

    statusBar()->setSizeGripEnabled(true);
    m_statusDir = new QLabel(tr("未打开工程"), this);
    m_statusDir->setObjectName(QStringLiteral("statusProjectDir"));
    m_statusDir->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusDir, 1);

    m_statusFrame = new QLabel(tr("帧 —"), this);
    m_statusFrame->setObjectName(QStringLiteral("statusFrame"));
    statusBar()->addPermanentWidget(m_statusFrame);

    m_statusFps = new QLabel(tr("fps —"), this);
    m_statusFps->setObjectName(QStringLiteral("statusFps"));
    statusBar()->addPermanentWidget(m_statusFps);
}

void NextWindow::buildEditor()
{
    auto* top = new QSplitter(Qt::Horizontal);
    top->setObjectName(QStringLiteral("editorTopSplitter"));
    top->setChildrenCollapsible(false);

    m_assetPanel = new AssetPanel(m_document, top);
    m_viewport = new ViewportWidget(m_document, top);
    m_propertiesPanel = new PropertiesPanel(m_document, top);

    top->addWidget(m_assetPanel);
    top->addWidget(m_viewport);
    top->addWidget(m_propertiesPanel);
    top->setStretchFactor(0, 0);
    top->setStretchFactor(1, 1);
    top->setStretchFactor(2, 0);
    top->setSizes({260, 900, 320});

    auto* vertical = new QSplitter(Qt::Vertical);
    vertical->setObjectName(QStringLiteral("editorVerticalSplitter"));
    vertical->setChildrenCollapsible(false);
    vertical->addWidget(top);

    m_timeline = new TimelineWidget(m_document, vertical);
    m_timeline->setMinimumHeight(160);
    vertical->addWidget(m_timeline);
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 0);
    vertical->setSizes({700, 260});

    replaceCentralWidget(vertical);

    if (m_gizmoCheck)
        m_viewport->setShowGizmo(m_gizmoCheck->isChecked());

    rebuildCameras();
}

void NextWindow::buildWelcome()
{
    auto* welcome = new QWidget;
    welcome->setObjectName(QStringLiteral("welcomePage"));

    auto* root = new QVBoxLayout(welcome);
    root->setContentsMargins(48, 48, 48, 48);
    root->setSpacing(12);
    root->addStretch(1);

    auto* title = new QLabel(tr("PVForge Next"), welcome);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    auto* subtitle = new QLabel(
        tr("新建或打开一个 .pvproj 工程目录来开始编辑。"), welcome);
    subtitle->setAlignment(Qt::AlignCenter);
    root->addWidget(subtitle);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch(1);
    auto* newButton = new QPushButton(tr("新建工程…"), welcome);
    newButton->setObjectName(QStringLiteral("welcomeNewButton"));
    newButton->setMinimumWidth(160);
    connect(newButton, &QPushButton::clicked, this, &NextWindow::chooseNewProject);
    buttons->addWidget(newButton);

    auto* openButton = new QPushButton(tr("打开工程…"), welcome);
    openButton->setObjectName(QStringLiteral("welcomeOpenButton"));
    openButton->setMinimumWidth(160);
    connect(openButton, &QPushButton::clicked, this, &NextWindow::chooseOpenProject);
    buttons->addWidget(openButton);
    buttons->addStretch(1);
    root->addLayout(buttons);

    auto* hint = new QLabel(
        tr("也可以使用命令行：pvforge-next --open <工程目录>"), welcome);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color: #8b96a8;"));
    root->addWidget(hint);
    root->addStretch(2);

    replaceCentralWidget(welcome);
}

void NextWindow::replaceCentralWidget(QWidget* widget)
{
    if (QWidget* old = takeCentralWidget())
        delete old;
    setCentralWidget(widget);
}

void NextWindow::connectDocument()
{
    connect(m_document, &Document::changed, this, &NextWindow::refreshAll);

    connect(m_document, &Document::selectionChanged, this, [this] {
        if (m_propertiesPanel)
            m_propertiesPanel->refresh();
        if (m_timeline)
            m_timeline->refresh();
        if (m_viewport)
            m_viewport->refresh();
    });

    connect(m_document, &Document::playheadChanged, this, [this] {
        if (m_playheadSpin) {
            QSignalBlocker blocker(m_playheadSpin);
            m_playheadSpin->setValue(m_document->playhead());
        }
        if (m_timeline)
            m_timeline->refresh();
        if (m_viewport)
            m_viewport->refresh();
        updateStatus();
    });
}

void NextWindow::refreshAll()
{
    if (m_assetPanel)
        m_assetPanel->refresh();
    if (m_propertiesPanel)
        m_propertiesPanel->refresh();
    if (m_timeline)
        m_timeline->refresh();
    if (m_viewport)
        m_viewport->refresh();

    rebuildCameras();
    updateActions();
    updateStatus();
}

void NextWindow::rebuildCameras()
{
    if (!m_cameraCombo)
        return;

    const QString wanted = m_viewport ? m_viewport->cameraUuid()
                                      : m_cameraCombo->currentData().toString();

    QSignalBlocker blocker(m_cameraCombo);
    m_cameraCombo->clear();
    m_cameraCombo->addItem(tr("全画布"), QString());

    if (m_document) {
        struct CameraEntry
        {
            QString uuid;
            QString name;
        };
        QVector<CameraEntry> cameras;
        const auto& entities = m_document->project().entities;
        for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
            const model::Entity& entity = it.value();
            if (entity.content.kind != ContentKind::Camera)
                continue;
            cameras.append({entity.uuid, entity.name});
        }

        std::sort(cameras.begin(), cameras.end(),
                  [](const CameraEntry& a, const CameraEntry& b) {
                      const int byName = QString::localeAwareCompare(a.name, b.name);
                      if (byName != 0)
                          return byName < 0;
                      return a.uuid < b.uuid;
                  });

        for (const CameraEntry& camera : cameras) {
            const QString name = camera.name.trimmed().isEmpty()
                                     ? tr("摄像机")
                                     : camera.name;
            m_cameraCombo->addItem(
                tr("%1 (%2)").arg(name, camera.uuid.left(8)), camera.uuid);
        }
    }

    int index = wanted.isEmpty() ? 0 : m_cameraCombo->findData(wanted);
    if (index < 0)
        index = 0;
    m_cameraCombo->setCurrentIndex(index);

    if (m_viewport)
        m_viewport->setCameraUuid(m_cameraCombo->currentData().toString());
}

void NextWindow::createEntity(ContentKind kind)
{
    if (!m_document || kind == ContentKind::Asset)
        return;

    const int previousPlayhead = m_document->playhead();
    const Uuid previousSelection = m_document->selection();

    const model::ProjectSettings& settings = m_document->project().settings;
    const int fps = std::max(1, settings.fps);

    Uuid parentUuid = m_timeline ? m_timeline->parentUuid() : Uuid();
    const model::Entity* parent = nullptr;
    if (!parentUuid.isEmpty()) {
        const auto parentIt =
            m_document->project().entities.constFind(parentUuid);
        if (parentIt != m_document->project().entities.constEnd())
            parent = &parentIt.value();
        else
            parentUuid.clear();
    }

    int start = 0;
    int end = 0;
    if (parent) {
        const int lastFrame = std::max(parent->start, parent->end - 1);
        start = std::clamp(previousPlayhead, parent->start, lastFrame);
        end = std::min(start + fps, parent->end);
        if (end <= start) {
            end = std::min(start + 1, parent->end);
            if (end <= start)
                end = parent->end;
        }
    } else {
        start = std::max(0, previousPlayhead);
        end = start + fps;
    }

    int highestRow = -1;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        const model::Entity& other = it.value();
        if (other.parentUuid != parentUuid)
            continue;
        if (other.start >= end || start >= other.end)
            continue;
        highestRow = std::max(highestRow, other.row);
    }

    model::Entity entity;
    entity.uuid = model::newUuid();
    entity.name = defaultEntityName(kind);
    entity.parentUuid = parentUuid;
    entity.start = start;
    entity.end = end;
    entity.row = highestRow + 1;
    entity.content.kind = kind;

    switch (kind) {
    case ContentKind::Text:
        entity.content.text.text = QStringLiteral("文字");
        entity.content.text.pixelSize = 96;
        entity.content.text.color = QStringLiteral("#ffffff");
        entity.content.text.align = int(Qt::AlignCenter);
        break;
    case ContentKind::Vector:
        entity.content.vector.shape = 0;
        entity.content.vector.width = 240.0;
        entity.content.vector.height = 120.0;
        entity.content.vector.fillColor = QStringLiteral("#ffffff");
        entity.content.vector.cornerRadius = 8.0;
        break;
    case ContentKind::Empty:
    case ContentKind::Camera:
    case ContentKind::Asset:
        break;
    }

    m_document->mutableProject().entities.insert(entity.uuid, entity);

    QString error;
    const QString message =
        QStringLiteral("pvf: create entity \"%1\"").arg(entity.name);
    if (!m_document->commitEntity(entity.uuid, message, &error)) {
        m_document->mutableProject().entities.remove(entity.uuid);
        m_document->setPlayhead(previousPlayhead);
        m_document->setSelection(previousSelection);
        refreshAll();
        showError(tr("新建实体失败"), error);
        return;
    }

    m_document->setSelection(entity.uuid);
    refreshAll();
}

QAction* NextWindow::createEntityActionForTest(ContentKind kind) const
{
    switch (kind) {
    case ContentKind::Empty:
        return m_createEmptyAction;
    case ContentKind::Text:
        return m_createTextAction;
    case ContentKind::Vector:
        return m_createVectorAction;
    case ContentKind::Camera:
        return m_createCameraAction;
    case ContentKind::Asset:
        break;
    }
    return nullptr;
}

void NextWindow::updateActions()
{
    const bool open = m_document != nullptr;
    if (m_undoAction)
        m_undoAction->setEnabled(open);
    if (m_redoAction)
        m_redoAction->setEnabled(open);
    for (QAction* action : {m_createEmptyAction, m_createTextAction,
                            m_createVectorAction, m_createCameraAction}) {
        if (action)
            action->setEnabled(open);
    }
    if (m_playheadSpin)
        m_playheadSpin->setEnabled(open);
    if (m_cameraCombo)
        m_cameraCombo->setEnabled(open);
    if (m_gizmoCheck)
        m_gizmoCheck->setEnabled(open);
    if (m_gizmoAction)
        m_gizmoAction->setEnabled(open);
}

void NextWindow::updateStatus()
{
    if (!m_document) {
        setWindowTitle(tr("PVForge Next"));
        if (m_statusDir)
            m_statusDir->setText(tr("未打开工程"));
        if (m_statusFrame)
            m_statusFrame->setText(tr("帧 —"));
        if (m_statusFps)
            m_statusFps->setText(tr("fps —"));
        if (m_fpsLabel)
            m_fpsLabel->setText(tr("fps —"));
        if (m_resolutionLabel)
            m_resolutionLabel->setText(tr("— × —"));
        return;
    }

    const model::ProjectSettings& settings = m_document->project().settings;
    QString name = settings.name.trimmed();
    if (name.isEmpty())
        name = tr("未命名项目");
    setWindowTitle(tr("%1 — PVForge Next").arg(name));

    if (m_statusDir)
        m_statusDir->setText(
            tr("工程：%1").arg(QDir::toNativeSeparators(m_document->root())));
    if (m_statusFrame)
        m_statusFrame->setText(tr("帧 %1").arg(m_document->playhead()));
    if (m_statusFps)
        m_statusFps->setText(tr("fps %1").arg(settings.fps));
    if (m_fpsLabel)
        m_fpsLabel->setText(tr("fps %1").arg(settings.fps));
    if (m_resolutionLabel)
        m_resolutionLabel->setText(
            tr("%1 × %2").arg(settings.width).arg(settings.height));

    if (m_playheadSpin) {
        QSignalBlocker blocker(m_playheadSpin);
        m_playheadSpin->setValue(m_document->playhead());
    }
}

void NextWindow::chooseNewProject()
{
    NewProjectDialog dialog(m_lastBrowseDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString projectDir = dialog.projectDir();
    QString error;
    auto document = ProjectDocument::create(projectDir, dialog.settings(), &error);
    if (!document) {
        showError(tr("新建工程失败"), error);
        return;
    }

    m_lastBrowseDir = QFileInfo(projectDir).absolutePath();
    setDocument(std::move(document));
    statusBar()->showMessage(tr("已新建工程 %1").arg(projectDir), 4000);
}

void NextWindow::chooseOpenProject()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("打开 PVForge 工程目录"), m_lastBrowseDir);
    if (dir.isEmpty())
        return;

    QString error;
    if (!openProject(dir, &error)) {
        QMessageBox::warning(this, tr("打开工程失败"), error);
        return;
    }

    m_lastBrowseDir = QFileInfo(dir).absolutePath();
}

bool NextWindow::openProject(const QString& path, QString* error)
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    const QFileInfo info(path);
    if (!info.exists())
        return fail(tr("路径不存在：%1").arg(path));

    if (info.isFile()) {
        if (info.suffix().compare(QStringLiteral("pvproj"), Qt::CaseInsensitive) == 0)
            return fail(tr("旧工程格式已废弃：%1 是单文件 .pvproj。"
                           "请使用新版的目录工程，或让 AI 重新创建。")
                            .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
        return fail(tr("不是工程目录：%1").arg(path));
    }

    if (!info.isDir())
        return fail(tr("不是工程目录：%1").arg(path));

    const QString projectDir = info.absoluteFilePath();
    if (!QFileInfo::exists(QDir(projectDir).filePath(QStringLiteral("project.json")))) {
        if (path.endsWith(QStringLiteral(".pvproj"), Qt::CaseInsensitive))
            return fail(tr("旧工程格式已废弃：%1 中没有 project.json。"
                           "请使用新版的目录工程，或让 AI 重新创建。")
                            .arg(QDir::toNativeSeparators(projectDir)));
        return fail(tr("目录中没有 project.json：%1")
                        .arg(QDir::toNativeSeparators(projectDir)));
    }

    auto document = ProjectDocument::open(projectDir, error);
    if (!document)
        return false;

    setDocument(std::move(document));
    return true;
}

void NextWindow::undo()
{
    if (!m_document)
        return;
    QString error;
    if (!m_document->undo(&error))
        showError(tr("撤销失败"), error);
}

void NextWindow::redo()
{
    if (!m_document)
        return;
    QString error;
    if (!m_document->redo(&error))
        showError(tr("重做失败"), error);
}

void NextWindow::showError(const QString& title, const QString& error)
{
    QMessageBox::critical(this, title,
                          error.isEmpty() ? tr("未知错误") : error);
}

} // namespace pvf::next
