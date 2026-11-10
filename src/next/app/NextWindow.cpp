#include "next/app/NextWindow.h"

#include "export2/Export.h"
#include "html/HtmlBrowser.h"
#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"
#include "next/app/CameraMonitorWidget.h"
#include "next/app/ExportDialog.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchPanel.h"
#include "next/assets/AssetPanel.h"
#include "next/history/HistoryPanel.h"
#include "next/mcp/McpLogPanel.h"
#include "next/properties/PropertiesPanel.h"
#include "next/settings/OnboardingDialog.h"
#include "next/settings/SettingsDialog.h"
#include "next/timeline/TimelineWidget.h"
#include "next/viewport/ViewportWidget.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace pvf::next {

namespace {

constexpr int kMaxRecentProjects = 10;

// One row of the welcome page's recent-project list.  Callbacks are used
// instead of signals so the row needs no moc; the window defers the action by
// one event-loop turn, because opening/removing rebuilds the welcome page and
// would otherwise delete the widget that is still emitting.
class RecentProjectRow : public QFrame
{
public:
    RecentProjectRow(const QString& name, const QString& path,
                     std::function<void()> open, std::function<void()> remove,
                     QWidget* parent)
        : QFrame(parent)
        , m_open(std::move(open))
        , m_remove(std::move(remove))
    {
        setObjectName(QStringLiteral("recentProjectRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QDir::toNativeSeparators(path));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 8, 8, 8);
        layout->setSpacing(10);

        auto* text = new QVBoxLayout;
        text->setSpacing(2);

        auto* nameLabel = new QLabel(name, this);
        nameLabel->setObjectName(QStringLiteral("recentProjectName"));
        nameLabel->setToolTip(QDir::toNativeSeparators(path));
        QFont nameFont = nameLabel->font();
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        text->addWidget(nameLabel);

        auto* pathLabel = new QLabel(QDir::toNativeSeparators(path), this);
        pathLabel->setObjectName(QStringLiteral("recentProjectPath"));
        pathLabel->setToolTip(QDir::toNativeSeparators(path));
        QFont pathFont = pathLabel->font();
        pathFont.setPointSizeF(pathFont.pointSizeF() * 0.9);
        pathLabel->setFont(pathFont);
        text->addWidget(pathLabel);
        layout->addLayout(text, 1);

        auto* openButton = new QPushButton(NextWindow::tr("打开"), this);
        openButton->setObjectName(QStringLiteral("recentProjectOpenButton"));
        connect(openButton, &QPushButton::clicked, this, [this] {
            if (m_open)
                m_open();
        });
        layout->addWidget(openButton);

        auto* removeButton = new QPushButton(NextWindow::tr("移除"), this);
        removeButton->setObjectName(QStringLiteral("recentProjectRemoveButton"));
        removeButton->setToolTip(
            NextWindow::tr("从最近项目列表移除此项"));
        connect(removeButton, &QPushButton::clicked, this, [this] {
            if (m_remove)
                m_remove();
        });
        layout->addWidget(removeButton);
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_open) {
            m_open();
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        QMenu menu(this);
        QAction* openAction = menu.addAction(NextWindow::tr("打开"));
        menu.addSeparator();
        QAction* removeAction =
            menu.addAction(NextWindow::tr("从最近项目移除"));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == openAction && m_open)
            m_open();
        else if (chosen == removeAction && m_remove)
            m_remove();
    }

private:
    std::function<void()> m_open;
    std::function<void()> m_remove;
};

class ProjectPropertiesDialog : public QDialog
{
public:
    explicit ProjectPropertiesDialog(const model::ProjectSettings& settings,
                                     QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("projectPropertiesDialog"));
        setWindowTitle(tr("工程属性"));
        setModal(false);
        resize(460, 240);

        auto* root = new QVBoxLayout(this);

        auto* form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_name = new QLineEdit(settings.name, this);
        m_name->setObjectName(QStringLiteral("projectNameEdit"));
        m_name->setMaxLength(256);
        m_name->setPlaceholderText(tr("工程名称"));
        form->addRow(tr("名称"), m_name);

        // fps and resolution are locked once the project exists; they are
        // labels, not editors, so the UI cannot imply they are editable.
        m_resolution = new QLabel(
            tr("%1 × %2（创建后锁定）")
                .arg(settings.width)
                .arg(settings.height),
            this);
        m_resolution->setObjectName(QStringLiteral("projectResolutionLabel"));
        m_resolution->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("分辨率"), m_resolution);

        m_fps = new QLabel(tr("%1（创建后锁定）").arg(settings.fps), this);
        m_fps->setObjectName(QStringLiteral("projectFpsLabel"));
        m_fps->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("帧率"), m_fps);

        auto* backgroundRow = new QHBoxLayout;
        backgroundRow->setContentsMargins(0, 0, 0, 0);
        m_backgroundButton = new QPushButton(this);
        m_backgroundButton->setObjectName(
            QStringLiteral("projectBackgroundButton"));
        m_backgroundButton->setFixedWidth(44);
        m_backgroundButton->setToolTip(tr("选择背景色"));
        m_backgroundHex = new QLineEdit(this);
        m_backgroundHex->setObjectName(
            QStringLiteral("projectBackgroundHex"));
        m_backgroundHex->setPlaceholderText(QStringLiteral("#101014"));
        m_backgroundHex->setMaxLength(16);
        backgroundRow->addWidget(m_backgroundButton);
        backgroundRow->addWidget(m_backgroundHex, 1);
        form->addRow(tr("背景色"), backgroundRow);

        root->addLayout(form);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("projectPropertiesError"));
        m_error->setStyleSheet(QStringLiteral("color: #e08a3c;"));
        m_error->hide();
        root->addWidget(m_error);
        root->addStretch(1);

        m_buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(m_buttons);

        connect(m_buttons, &QDialogButtonBox::accepted, this,
                [this] { accept(); });
        connect(m_buttons, &QDialogButtonBox::rejected, this,
                &QDialog::reject);
        connect(m_name, &QLineEdit::textChanged, this,
                [this] { validate(); });
        connect(m_backgroundHex, &QLineEdit::textChanged, this,
                [this] { validate(); });
        connect(m_backgroundButton, &QPushButton::clicked, this,
                [this] { chooseBackground(); });

        const QColor background(settings.background);
        m_backgroundHex->setText(background.isValid()
                                     ? background.name()
                                     : QStringLiteral("#101014"));
        updateBackgroundSwatch();
        validate();
    }

    // Called from accept() while the widgets are still alive, so the shell can
    // read the edited values without relying on signal/deletion ordering.
    std::function<void()> onAccepted;

    QString name() const { return m_name->text().trimmed(); }
    QString background() const
    {
        return QColor(m_backgroundHex->text().trimmed()).name();
    }

    void accept() override
    {
        if (!isValid())
        {
            validate();
            return;
        }
        if (onAccepted)
            onAccepted();
        QDialog::accept();
    }

private:
    bool isValid() const
    {
        const QString trimmed = name();
        return !trimmed.isEmpty() && trimmed.size() <= 256
               && QColor(m_backgroundHex->text().trimmed()).isValid();
    }

    void validate()
    {
        const bool ok = isValid();
        if (m_buttons) {
            if (QPushButton* button =
                    m_buttons->button(QDialogButtonBox::Ok))
                button->setEnabled(ok);
        }
        if (!m_error)
            return;
        if (name().isEmpty())
            m_error->setText(tr("名称不能为空（1..256 个字符）"));
        else if (name().size() > 256)
            m_error->setText(tr("名称最多 256 个字符"));
        else if (!QColor(m_backgroundHex->text().trimmed()).isValid())
            m_error->setText(tr("背景色不是有效颜色，例如 #101014"));
        m_error->setVisible(!ok);
        updateBackgroundSwatch();
    }

    void chooseBackground()
    {
        const QColor current(m_backgroundHex->text().trimmed());
        const QColor color = QColorDialog::getColor(
            current.isValid() ? current : QColor(QStringLiteral("#101014")),
            this, tr("选择背景色"));
        if (!color.isValid())
            return;
        m_backgroundHex->setText(color.name());
    }

    void updateBackgroundSwatch()
    {
        const QColor color(m_backgroundHex->text().trimmed());
        if (!color.isValid())
            return;
        QPixmap swatch(16, 16);
        swatch.fill(color);
        m_backgroundButton->setIcon(QIcon(swatch));
    }

    QLineEdit* m_name = nullptr;
    QLabel* m_resolution = nullptr;
    QLabel* m_fps = nullptr;
    QPushButton* m_backgroundButton = nullptr;
    QLineEdit* m_backgroundHex = nullptr;
    QLabel* m_error = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

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
            tr("工程会创建为 <工程名>.pvproj 目录，并自动初始化 Git"
               "（只管理文本，素材不进 git）。"),
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

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

bool isTextInputWidget(const QWidget* widget)
{
    return qobject_cast<const QLineEdit*>(widget)
           || qobject_cast<const QTextEdit*>(widget)
           || qobject_cast<const QPlainTextEdit*>(widget)
           || qobject_cast<const QAbstractSpinBox*>(widget);
}

void collectSubtree(const model::Project& project, const Uuid& uuid,
                    QSet<Uuid>& visited, QVector<Uuid>& out)
{
    if (uuid.isEmpty() || visited.contains(uuid))
        return;
    visited.insert(uuid);
    out.append(uuid);
    for (const Uuid& child : model::childrenOf(project, uuid))
        collectSubtree(project, child, visited, out);
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
    m_playback = std::make_unique<PlaybackController>(this);

    m_settingsStore = std::make_unique<SettingsStore>();
    m_settings = m_settingsStore->load();
    loadRecentProjects();

    buildUi();
    connect(m_playback.get(), &PlaybackController::playbackStateChanged, this,
            [this](bool playing) {
                updatePlaybackAction();
                if (m_workbench)
                    m_workbench->setPlaybackActive(playing);
                if (!playing && m_media && m_document)
                    m_media->prefetchHtml(m_document->playhead(), true);
                updateStatus();
            });
    connect(m_playback.get(), &PlaybackController::stalled, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 8000);
                if (m_mcpLogPanel)
                    m_mcpLogPanel->addSystemEvent(tr("播放已暂停"), message);
            });
    m_playback->setWatchdogMs(m_settings.watchdogMs);

    // Polls the workbench cache counters; per-frame signals would be too
    // chatty for a status bar label.
    m_workbenchUiTimer = new QTimer(this);
    m_workbenchUiTimer->setInterval(500);
    connect(m_workbenchUiTimer, &QTimer::timeout, this,
            &NextWindow::updateWorkbenchStatus);
    m_workbenchUiTimer->start();

    if (m_document) {
        connectDocument();
        buildEditor();
    } else {
        buildWelcome();
    }

    m_playback->setDocument(m_document);
    applyHumanCommitDelay();
    updateActions();
    updateStatus();
    rebuildCameras();
    ensureWorkbenchServices();
    startMcpServer();
    refreshHistory();
    showOnboardingIfNeeded();
}

NextWindow::~NextWindow()
{
    resetWorkbenchServices();
    resetMediaServices();
    if (m_playback)
        m_playback->setDocument(nullptr);
    stopMcpServer();
    closeAllPrivateTimelines();
}

void NextWindow::closeEvent(QCloseEvent* event)
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(tr("视频导出中，暂不能关闭"), 5000);
        event->ignore();
        return;
    }

    if (mcpToolRunning())
    {
        statusBar()->showMessage(tr("AI 正在操作工程，暂不能关闭"), 5000);
        event->ignore();
        return;
    }

    if (m_document)
    {
        QString error;
        m_document->flush(&error);
        if (!error.isEmpty())
        {
            showError(tr("提交失败"), error);
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void NextWindow::setDocument(std::unique_ptr<ProjectDocument> document)
{
    // The export worker holds raw providers into the current document and its
    // media services.
    if (m_exportRunning)
    {
        statusBar()->showMessage(tr("视频导出中，暂不能切换工程"), 5000);
        return;
    }

    // A running tool call may pump events at its batch checkpoints; destroying
    // the service under it would be a use-after-free.  The log panel's stop
    // button remains available to cancel the call first.
    if (mcpToolRunning())
    {
        statusBar()->showMessage(tr("AI 正在操作工程，暂不能切换"), 5000);
        return;
    }

    // Commit any debounced human edit before the old document goes away.
    if (m_document)
    {
        QString error;
        m_document->flush(&error);
        if (!error.isEmpty())
            statusBar()->showMessage(tr("提交失败：%1").arg(error), 8000);
    }

    // The playback clock, MCP service and private overlay timelines all point
    // into the old document; drop them before it dies.
    if (m_playback)
        m_playback->setDocument(nullptr);
    stopMcpServer();
    closeAllPrivateTimelines();
    // The properties dialog edits the current project; it must not survive a
    // project switch and write the old values into the new project.
    if (m_projectPropertiesDialog)
    {
        m_projectPropertiesDialog->close();
        m_projectPropertiesDialog = nullptr;
    }

    // The media services hold a raw ProjectDocument* and their HTML threads
    // must stop before the document goes away.  The workbench hooks reference
    // the media providers, so they go first.
    resetWorkbenchServices();
    resetMediaServices();

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

    // Entity copies may reference parent/solver UUIDs that only exist in the
    // old project; never let them leak into the new one.
    m_entityClipboard = model::Entity();
    m_hasEntityClipboard = false;

    // A newly created or opened project becomes the most recent one.  This is
    // the common tail of chooseNewProject() and openProject().
    if (m_document)
        recordRecentProject(m_document->root());

    if (m_autoKeyCheck) {
        QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(true);
    }

    if (m_document) {
        connectDocument();
        buildEditor();
    } else {
        buildWelcome();
    }

    if (m_playback)
        m_playback->setDocument(m_document);
    applyHumanCommitDelay();
    updateActions();
    updateStatus();
    rebuildCameras();
    ensureWorkbenchServices();
    startMcpServer();
    refreshHistory();
    updateSettingsDialogContext();
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

    m_exportAction = new QAction(tr("导出视频…"), this);
    m_exportAction->setObjectName(QStringLiteral("exportVideoAction"));
    m_exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_exportAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_exportAction, &QAction::triggered, this, &NextWindow::exportVideo);
    fileMenu->addAction(m_exportAction);

    fileMenu->addSeparator();
    m_settingsAction = new QAction(tr("设置…"), this);
    m_settingsAction->setObjectName(QStringLiteral("settingsAction"));
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    connect(m_settingsAction, &QAction::triggered, this, &NextWindow::openSettings);
    fileMenu->addAction(m_settingsAction);

    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("退出"), this);
    m_quitAction->setObjectName(QStringLiteral("quitAction"));
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(m_quitAction);

    QMenu* projectMenu = menuBar()->addMenu(tr("工程(&O)"));
    m_projectPropertiesAction = new QAction(tr("工程属性…"), this);
    m_projectPropertiesAction->setObjectName(
        QStringLiteral("projectPropertiesAction"));
    m_projectPropertiesAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_projectPropertiesAction, &QAction::triggered, this,
            &NextWindow::openProjectProperties);
    projectMenu->addAction(m_projectPropertiesAction);

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

    editMenu->addSeparator();
    m_copyEntityAction = new QAction(tr("复制实体"), this);
    m_copyEntityAction->setObjectName(QStringLiteral("copyEntityAction"));
    m_copyEntityAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_C));
    m_copyEntityAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_copyEntityAction, &QAction::triggered, this,
            &NextWindow::copySelectedEntity);
    editMenu->addAction(m_copyEntityAction);

    m_pasteEntityAction = new QAction(tr("粘贴实体"), this);
    m_pasteEntityAction->setObjectName(QStringLiteral("pasteEntityAction"));
    m_pasteEntityAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_V));
    m_pasteEntityAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_pasteEntityAction, &QAction::triggered, this,
            &NextWindow::pasteEntity);
    editMenu->addAction(m_pasteEntityAction);

    m_duplicateAction = new QAction(tr("原地复制实体"), this);
    m_duplicateAction->setObjectName(QStringLiteral("duplicateEntityAction"));
    m_duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_duplicateAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_duplicateAction, &QAction::triggered, this,
            &NextWindow::duplicateSelectedEntity);
    editMenu->addAction(m_duplicateAction);

    editMenu->addSeparator();
    m_deleteAction = new QAction(tr("删除实体"), this);
    m_deleteAction->setObjectName(QStringLiteral("deleteEntityAction"));
    m_deleteAction->setShortcuts(
        {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    m_deleteAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_deleteAction, &QAction::triggered, this,
            &NextWindow::deleteSelectedEntity);
    editMenu->addAction(m_deleteAction);

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

    QMenu* playbackMenu = menuBar()->addMenu(tr("播放(&P)"));
    m_playPauseAction = new QAction(tr("播放"), this);
    m_playPauseAction->setObjectName(QStringLiteral("playPauseAction"));
    m_playPauseAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playPauseAction, &QAction::triggered, this, [this] {
        if (m_playback)
            m_playback->toggle();
    });
    playbackMenu->addAction(m_playPauseAction);

    m_homeAction = new QAction(tr("跳到开头"), this);
    m_homeAction->setObjectName(QStringLiteral("homeAction"));
    m_homeAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(m_homeAction, &QAction::triggered, this, [this] {
        if (m_document)
            m_document->setPlayhead(0);
    });
    playbackMenu->addAction(m_homeAction);

    m_endAction = new QAction(tr("跳到末尾"), this);
    m_endAction->setObjectName(QStringLiteral("endAction"));
    m_endAction->setShortcut(QKeySequence(Qt::Key_End));
    connect(m_endAction, &QAction::triggered, this, [this] {
        if (m_document && m_playback)
            m_document->setPlayhead(m_playback->contentEnd());
    });
    playbackMenu->addAction(m_endAction);

    m_prevFrameAction = new QAction(tr("上一帧"), this);
    m_prevFrameAction->setObjectName(QStringLiteral("prevFrameAction"));
    m_prevFrameAction->setShortcut(QKeySequence(Qt::Key_Left));
    connect(m_prevFrameAction, &QAction::triggered, this, [this] {
        if (!m_document || !m_playback)
            return;
        const int end = m_playback->contentEnd();
        m_document->setPlayhead(
            std::clamp(m_document->playhead() - 1, 0, end));
    });
    playbackMenu->addAction(m_prevFrameAction);

    m_nextFrameAction = new QAction(tr("下一帧"), this);
    m_nextFrameAction->setObjectName(QStringLiteral("nextFrameAction"));
    m_nextFrameAction->setShortcut(QKeySequence(Qt::Key_Right));
    connect(m_nextFrameAction, &QAction::triggered, this, [this] {
        if (!m_document || !m_playback)
            return;
        const int end = m_playback->contentEnd();
        m_document->setPlayhead(
            std::clamp(m_document->playhead() + 1, 0, end));
    });
    playbackMenu->addAction(m_nextFrameAction);

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

    m_fitViewAction = new QAction(tr("适配画布"), this);
    m_fitViewAction->setObjectName(QStringLiteral("fitViewAction"));
    m_fitViewAction->setShortcut(QKeySequence(Qt::Key_F));
    m_fitViewAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_fitViewAction, &QAction::triggered, this, &NextWindow::fitView);
    viewMenu->addAction(m_fitViewAction);

    viewMenu->addSeparator();
    m_cameraMonitorAction = new QAction(tr("相机监视器"), this);
    m_cameraMonitorAction->setObjectName(
        QStringLiteral("showCameraMonitorAction"));
    m_cameraMonitorAction->setCheckable(true);
    m_cameraMonitorAction->setChecked(true);
    connect(m_cameraMonitorAction, &QAction::toggled, this,
            [this](bool checked) {
                if (!m_cameraMonitorDock)
                    return;
                m_cameraMonitorDock->setVisible(checked);
                if (checked)
                {
                    m_cameraMonitorDock->raise();
                    if (m_mcpLogDock && m_mcpLogDock->isVisible())
                    {
                        const int half = std::max(200, width() / 2);
                        resizeDocks({m_cameraMonitorDock, m_mcpLogDock},
                                    {half, half}, Qt::Horizontal);
                    }
                }
            });
    viewMenu->addAction(m_cameraMonitorAction);

    m_cameraMonitor = new CameraMonitorWidget(nullptr, nullptr);
    auto* monitorContainer = new QWidget(this);
    monitorContainer->setObjectName(QStringLiteral("cameraMonitorContainer"));
    auto* monitorLayout = new QVBoxLayout(monitorContainer);
    monitorLayout->setContentsMargins(0, 0, 0, 0);
    monitorLayout->setSpacing(0);
    monitorLayout->addWidget(m_cameraMonitor, 1);

    auto* monitorControls = new QHBoxLayout;
    monitorControls->setContentsMargins(6, 2, 6, 2);
    monitorControls->setSpacing(6);
    m_cameraImportButton = new QPushButton(tr("临摹视频…"), monitorContainer);
    m_cameraImportButton->setObjectName(
        QStringLiteral("cameraMonitorImportButton"));
    m_cameraImportButton->setToolTip(
        tr("选择本地视频，按播放头时间叠加在相机画面下面。"));
    monitorControls->addWidget(m_cameraImportButton);

    m_cameraClearButton = new QPushButton(tr("清除"), monitorContainer);
    m_cameraClearButton->setObjectName(
        QStringLiteral("cameraMonitorClearButton"));
    m_cameraClearButton->setEnabled(false);
    monitorControls->addWidget(m_cameraClearButton);
    monitorControls->addStretch(1);

    auto* opacityLabel = new QLabel(tr("透明度"), monitorContainer);
    monitorControls->addWidget(opacityLabel);
    m_cameraOpacitySlider = new QSlider(Qt::Horizontal, monitorContainer);
    m_cameraOpacitySlider->setObjectName(
        QStringLiteral("cameraMonitorOpacitySlider"));
    m_cameraOpacitySlider->setRange(0, 100);
    m_cameraOpacitySlider->setValue(45);
    m_cameraOpacitySlider->setMaximumWidth(120);
    monitorControls->addWidget(m_cameraOpacitySlider);
    m_cameraOpacityLabel = new QLabel(tr("45%"), monitorContainer);
    m_cameraOpacityLabel->setMinimumWidth(34);
    monitorControls->addWidget(m_cameraOpacityLabel);
    monitorLayout->addLayout(monitorControls);

    connect(m_cameraOpacitySlider, &QSlider::valueChanged, this,
            [this](int value) {
                if (m_cameraMonitor)
                    m_cameraMonitor->setReferenceOpacity(value / 100.0);
                if (m_cameraOpacityLabel)
                    m_cameraOpacityLabel->setText(
                        tr("%1%").arg(value));
            });
    connect(m_cameraImportButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("导入临摹视频"), m_lastBrowseDir,
            tr("视频文件 (*.mp4 *.webm *.mov *.mkv *.avi *.m4v);;所有文件 (*)"));
        if (path.isEmpty())
            return;
        m_lastBrowseDir = QFileInfo(path).absolutePath();
        QString error;
        if (!m_cameraMonitor || !m_cameraMonitor->setReferenceVideo(path, &error)) {
            statusBar()->showMessage(
                tr("无法加载临摹视频：%1").arg(error), 8000);
            return;
        }
        statusBar()->showMessage(
            tr("临摹视频已加载：%1").arg(QFileInfo(path).fileName()), 5000);
    });
    connect(m_cameraClearButton, &QPushButton::clicked, this, [this] {
        if (m_cameraMonitor)
            m_cameraMonitor->clearReference();
    });
    connect(m_cameraMonitor, &CameraMonitorWidget::referenceChanged, this,
            [this] {
                if (m_cameraClearButton && m_cameraMonitor) {
                    m_cameraClearButton->setEnabled(
                        !m_cameraMonitor->referencePath().isEmpty());
                }
            });

    m_cameraMonitorDock = new QDockWidget(tr("相机监视器"), this);
    m_cameraMonitorDock->setObjectName(QStringLiteral("cameraMonitorDock"));
    m_cameraMonitorDock->setAllowedAreas(Qt::TopDockWidgetArea
                                         | Qt::BottomDockWidgetArea);
    m_cameraMonitorDock->setFeatures(QDockWidget::DockWidgetClosable
                                     | QDockWidget::DockWidgetMovable
                                     | QDockWidget::DockWidgetFloatable);
    m_cameraMonitorDock->setWidget(monitorContainer);
    addDockWidget(Qt::TopDockWidgetArea, m_cameraMonitorDock);

    connect(m_cameraMonitorDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) {
                if (!isVisible())
                    return;
                if (m_cameraMonitorAction
                    && m_cameraMonitorAction->isChecked() != visible) {
                    QSignalBlocker blocker(m_cameraMonitorAction);
                    m_cameraMonitorAction->setChecked(visible);
                }
            });

    m_mcpLogAction = new QAction(tr("MCP 日志"), this);
    m_mcpLogAction->setObjectName(QStringLiteral("showMcpLogAction"));
    m_mcpLogAction->setCheckable(true);
    connect(m_mcpLogAction, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            showMcpLog();
        else if (m_mcpLogDock)
            m_mcpLogDock->hide();
    });
    viewMenu->addAction(m_mcpLogAction);

    m_mcpLogPanel = new McpLogPanel(this);
    m_mcpLogDock = new QDockWidget(tr("MCP 日志"), this);
    m_mcpLogDock->setObjectName(QStringLiteral("mcpLogDock"));
    m_mcpLogDock->setAllowedAreas(Qt::BottomDockWidgetArea
                                  | Qt::TopDockWidgetArea);
    m_mcpLogDock->setFeatures(QDockWidget::DockWidgetClosable
                              | QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    m_mcpLogDock->setWidget(m_mcpLogPanel);
    // Top dock area, right of the camera monitor (the view menu keeps its own
    // 相机监视器 toggle).  Both docks share the top strip; neither fills it.
    addDockWidget(Qt::TopDockWidgetArea, m_mcpLogDock);
    splitDockWidget(m_cameraMonitorDock, m_mcpLogDock, Qt::Horizontal);
    m_mcpLogDock->hide();

    connect(m_mcpLogPanel, &McpLogPanel::visibilityChanged, this,
            [this](bool visible) {
                // Hiding the main window also hides its children; that must
                // not be mistaken for the user closing the log panel.
                if (!isVisible())
                    return;
                if (m_mcpLogAction
                    && m_mcpLogAction->isChecked() != visible) {
                    QSignalBlocker blocker(m_mcpLogAction);
                    m_mcpLogAction->setChecked(visible);
                }
                if (!visible && m_mcpLogDock && m_mcpLogDock->isVisible())
                    m_mcpLogDock->hide();
            });
    connect(m_mcpLogPanel, &McpLogPanel::stopAiRequested, this, [this] {
        if (m_mcpServer)
            m_mcpServer->stopAi();
        else
            statusBar()->showMessage(tr("MCP 未启动，无法执行急停"), 4000);
    });
    connect(m_mcpLogPanel, &McpLogPanel::resumeAiRequested, this, [this] {
        if (m_mcpServer)
            m_mcpServer->resumeAi();
        else
            statusBar()->showMessage(tr("MCP 未启动，无需恢复"), 4000);
    });

    viewMenu->addSeparator();
    m_historyAction = new QAction(tr("Git 历史"), this);
    m_historyAction->setObjectName(QStringLiteral("showHistoryAction"));
    m_historyAction->setCheckable(true);
    connect(m_historyAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_historyDock)
            return;
        m_historyDock->setVisible(checked);
        if (checked)
            m_historyDock->raise();
    });
    viewMenu->addAction(m_historyAction);

    m_historyPanel = new HistoryPanel(this);
    m_historyDock = new QDockWidget(tr("Git 历史"), this);
    m_historyDock->setObjectName(QStringLiteral("historyDock"));
    m_historyDock->setAllowedAreas(Qt::RightDockWidgetArea
                                   | Qt::BottomDockWidgetArea);
    m_historyDock->setFeatures(QDockWidget::DockWidgetClosable
                               | QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable);
    m_historyDock->setWidget(m_historyPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);
    m_historyDock->hide();

    connect(m_historyDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) {
                if (!isVisible())
                    return;
                if (m_historyAction
                    && m_historyAction->isChecked() != visible) {
                    QSignalBlocker blocker(m_historyAction);
                    m_historyAction->setChecked(visible);
                }
            });
    connect(m_historyPanel, &HistoryPanel::refreshRequested, this,
            &NextWindow::refreshHistory);
    connect(m_historyPanel, &HistoryPanel::restoreRequested, this,
            [this](const QString& hash, const QString&) {
                restoreHistoryCommit(hash);
            });

    viewMenu->addSeparator();
    m_workbenchAction = new QAction(tr("工作台缓存…"), this);
    m_workbenchAction->setObjectName(QStringLiteral("showWorkbenchAction"));
    m_workbenchAction->setCheckable(true);
    connect(m_workbenchAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_workbenchDock)
            return;
        m_workbenchDock->setVisible(checked);
        if (checked) {
            m_workbenchDock->raise();
            if (m_workbenchPanel)
                m_workbenchPanel->refresh();
        }
    });
    viewMenu->addAction(m_workbenchAction);

    m_workbenchPanel = new WorkbenchPanel(this);
    m_workbenchDock = new QDockWidget(tr("工作台缓存"), this);
    m_workbenchDock->setObjectName(QStringLiteral("workbenchDock"));
    m_workbenchDock->setAllowedAreas(Qt::RightDockWidgetArea
                                     | Qt::BottomDockWidgetArea);
    m_workbenchDock->setFeatures(QDockWidget::DockWidgetClosable
                                 | QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable);
    m_workbenchDock->setWidget(m_workbenchPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_workbenchDock);
    m_workbenchDock->hide();

    connect(m_workbenchDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) {
                if (!isVisible())
                    return;
                if (m_workbenchAction
                    && m_workbenchAction->isChecked() != visible) {
                    QSignalBlocker blocker(m_workbenchAction);
                    m_workbenchAction->setChecked(visible);
                }
            });
    connect(m_workbenchPanel, &WorkbenchPanel::cacheCleared, this, [this] {
        if (m_viewport)
            m_viewport->refresh();
        statusBar()->showMessage(tr("已清空工作台缓存"), 5000);
        updateWorkbenchStatus();
    });

    QMenu* cacheMenu = menuBar()->addMenu(tr("缓存(&C)"));
    m_cacheAllAction = new QAction(tr("缓存全部"), this);
    m_cacheAllAction->setObjectName(QStringLiteral("cacheAllFramesAction"));
    connect(m_cacheAllAction, &QAction::triggered, this,
            &NextWindow::cacheAllFrames);
    cacheMenu->addAction(m_cacheAllAction);

    m_cachePlayheadAction = new QAction(tr("缓存到播放头"), this);
    m_cachePlayheadAction->setObjectName(
        QStringLiteral("cacheToPlayheadAction"));
    connect(m_cachePlayheadAction, &QAction::triggered, this,
            &NextWindow::cacheToPlayhead);
    cacheMenu->addAction(m_cachePlayheadAction);

    cacheMenu->addSeparator();
    m_cacheClearAction = new QAction(tr("清空工作台缓存"), this);
    m_cacheClearAction->setObjectName(QStringLiteral("clearWorkbenchCacheAction"));
    connect(m_cacheClearAction, &QAction::triggered, this,
            &NextWindow::clearWorkbenchCache);
    cacheMenu->addAction(m_cacheClearAction);

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
    toolbar->addAction(m_playPauseAction);

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
        if (m_cameraMonitor && m_cameraCombo)
            m_cameraMonitor->setCameraUuid(
                m_cameraCombo->currentData().toString());
        ensureWorkbenchServices();
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

    m_autoKeyCheck = new QCheckBox(tr("自动关键帧"), toolbar);
    m_autoKeyCheck->setObjectName(QStringLiteral("shellAutoKeyCheck"));
    m_autoKeyCheck->setChecked(true);
    m_autoKeyCheck->setContentsMargins(10, 0, 4, 0);
    m_autoKeyCheck->setToolTip(
        tr("开启后，编辑可动画属性会在播放头处插入或更新关键帧。"));
    connect(m_autoKeyCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_propertiesPanel)
            m_propertiesPanel->setAutoKey(checked);
        if (m_viewport)
            m_viewport->setAutoKeyEnabled(checked);
        statusBar()->showMessage(
            checked ? tr("自动关键帧：开") : tr("自动关键帧：关"), 3000);
    });
    toolbar->addWidget(m_autoKeyCheck);

    statusBar()->setSizeGripEnabled(true);
    m_statusDir = new QLabel(tr("未打开工程"), this);
    m_statusDir->setObjectName(QStringLiteral("statusProjectDir"));
    m_statusDir->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusDir, 1);

    m_statusCommit = new QLabel(QString(), this);
    m_statusCommit->setObjectName(QStringLiteral("statusCommit"));
    statusBar()->addPermanentWidget(m_statusCommit);

    m_statusAi = new QLabel(this);
    m_statusAi->setObjectName(QStringLiteral("statusAi"));
    m_statusAi->hide();
    statusBar()->addPermanentWidget(m_statusAi);

    m_statusMcp = new QLabel(tr("MCP 未启动"), this);
    m_statusMcp->setObjectName(QStringLiteral("statusMcp"));
    m_statusMcp->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_statusMcp);

    m_statusFrame = new QLabel(tr("帧 —"), this);
    m_statusFrame->setObjectName(QStringLiteral("statusFrame"));
    statusBar()->addPermanentWidget(m_statusFrame);

    m_statusFps = new QLabel(tr("fps —"), this);
    m_statusFps->setObjectName(QStringLiteral("statusFps"));
    statusBar()->addPermanentWidget(m_statusFps);

    m_statusWorkbench = new QLabel(this);
    m_statusWorkbench->setObjectName(QStringLiteral("statusWorkbench"));
    m_statusWorkbench->setToolTip(tr("工作台整帧缓存"));
    m_statusWorkbench->hide();
    statusBar()->addPermanentWidget(m_statusWorkbench);
}

void NextWindow::buildEditor()
{
    auto* top = new QSplitter(Qt::Horizontal);
    top->setObjectName(QStringLiteral("editorTopSplitter"));
    top->setChildrenCollapsible(false);

    m_assetPanel = new AssetPanel(m_document, top);
    m_viewport = new ViewportWidget(m_document, top);
    // The watchdog is fed only by pictures that actually reached the screen.
    connect(m_viewport, &ViewportWidget::framePresented, this,
            [this](int) {
                if (m_playback)
                    m_playback->noteFramePresented();
            });
    m_viewport->setSceneExpandPercent(
        m_settings.workbenchCacheExpandPercent);
    m_viewport->setAutoKeyEnabled(!m_autoKeyCheck
                                  || m_autoKeyCheck->isChecked());
    applyViewportSnapSettings();
    m_propertiesPanel = new PropertiesPanel(m_document, top);

    m_propertiesPanel->setAutoKey(!m_autoKeyCheck
                                  || m_autoKeyCheck->isChecked());
    if (auto* panelAutoKey =
            m_propertiesPanel->findChild<QCheckBox*>(
                QStringLiteral("autoKeyCheck"))) {
        connect(panelAutoKey, &QCheckBox::toggled, this,
                [this](bool checked) {
                    if (m_autoKeyCheck) {
                        QSignalBlocker blocker(m_autoKeyCheck);
                        m_autoKeyCheck->setChecked(checked);
                    }
                    if (m_propertiesPanel)
                        m_propertiesPanel->setAutoKey(checked);
                    if (m_viewport)
                        m_viewport->setAutoKeyEnabled(checked);
                    statusBar()->showMessage(
                        checked ? tr("自动关键帧：开")
                                : tr("自动关键帧：关"),
                        3000);
                });
    }

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

    connect(m_timeline, &TimelineWidget::privateTimelineRequested, this,
            &NextWindow::openPrivateTimeline);
    connect(m_timeline, &TimelineWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
            });
    connect(m_assetPanel, &AssetPanel::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
            });

    replaceCentralWidget(vertical);

    if (m_gizmoCheck)
        m_viewport->setShowGizmo(m_gizmoCheck->isChecked());

    rebuildCameras();
    ensureMediaServices();
    if (m_media && m_document)
        m_media->prefetchHtml(m_document->playhead(), true);
    ensureWorkbenchServices();
    configureVideoExtraction();
}

void NextWindow::buildWelcome()
{
    pruneRecentProjects();

    auto* welcome = new QWidget;
    welcome->setObjectName(QStringLiteral("welcomePage"));
    welcome->setStyleSheet(QStringLiteral(R"(
        #recentProjectRow {
            background: #151920;
            border: 1px solid #262e3a;
        }
        #recentProjectRow:hover {
            background: #1b222c;
            border-color: #333d4c;
        }
        #recentProjectName { color: #e4e9f1; }
        #recentProjectPath { color: #8b96a8; }
        #recentProjectsTitle { color: #c9d3e0; }
        #recentProjectsEmpty { color: #8b96a8; }
        QPushButton#recentProjectOpenButton,
        QPushButton#recentProjectRemoveButton {
            background: #212834;
            border: 1px solid #333d4c;
            color: #e4e9f1;
            padding: 4px 12px;
        }
        QPushButton#recentProjectOpenButton:hover,
        QPushButton#recentProjectRemoveButton:hover {
            background: #2a3341;
        }
        QPushButton#recentProjectOpenButton:pressed,
        QPushButton#recentProjectRemoveButton:pressed {
            background: #1a2029;
        }
    )"));

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

    root->addSpacing(16);
    auto* recentTitle = new QLabel(tr("最近项目"), welcome);
    recentTitle->setObjectName(QStringLiteral("recentProjectsTitle"));
    QFont recentTitleFont = recentTitle->font();
    recentTitleFont.setBold(true);
    recentTitle->setFont(recentTitleFont);
    recentTitle->setAlignment(Qt::AlignCenter);
    root->addWidget(recentTitle);

    auto* recentBody = new QWidget(welcome);
    recentBody->setMaximumWidth(720);
    auto* recentList = new QVBoxLayout(recentBody);
    recentList->setContentsMargins(0, 0, 0, 0);
    recentList->setSpacing(6);

    if (m_recentProjects.isEmpty()) {
        auto* empty = new QLabel(tr("暂无最近项目"), recentBody);
        empty->setObjectName(QStringLiteral("recentProjectsEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        recentList->addWidget(empty);
    } else {
        for (const QString& path : std::as_const(m_recentProjects)) {
            auto* row = new RecentProjectRow(
                recentProjectName(path), path,
                [this, path] {
                    QTimer::singleShot(0, this,
                                       [this, path] { openRecentProject(path); });
                },
                [this, path] {
                    QTimer::singleShot(0, this, [this, path] {
                        removeRecentProject(path);
                        if (!m_document)
                            buildWelcome();
                    });
                },
                recentBody);
            recentList->addWidget(row);
        }
    }
    root->addWidget(recentBody, 0, Qt::AlignHCenter);

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
        updateActions();
    });

    connect(m_document, &Document::playheadChanged, this, [this] {
        if (m_playheadSpin) {
            QSignalBlocker blocker(m_playheadSpin);
            m_playheadSpin->setValue(m_document->playhead());
        }
        if (m_timeline)
            m_timeline->refresh();
        // Give the current HTML frame its 300 ms miss budget outside
        // paintEvent(); the subsequent paint is cache-only.
        if (m_media) {
            m_media->prepareHtmlPreview(300);
            // The cache browser idles during active playback; once paused or
            // while scrubbing it fills the frames ahead of the playhead.
            if (!m_playback || !m_playback->isPlaying())
                m_media->prefetchHtml(m_document->playhead());
        }
        if (m_viewport)
            m_viewport->refresh();
        updateStatus();
    });

    connect(m_document, &Document::pendingCommitChanged, this,
            [this](bool pending) {
                updateStatus();
                // A successful flush turned the debounced edit into a commit;
                // the history row only exists after that.
                if (!pending)
                    refreshHistory();
            });
    connect(m_document, &Document::commitFailed, this,
            [this](const QString& message) {
                statusBar()->showMessage(tr("提交失败：%1").arg(message), 8000);
            });
}

void NextWindow::refreshAll()
{
    ensureMediaServices();
    if (m_assetPanel)
        m_assetPanel->refresh();
    if (m_propertiesPanel)
        m_propertiesPanel->refresh();
    if (m_timeline)
        m_timeline->refresh();
    if (m_media) {
        m_media->prepareHtmlPreview(300);
        if ((!m_playback || !m_playback->isPlaying()) && m_document)
            m_media->prefetchHtml(m_document->playhead(), true);
    }
    if (m_viewport)
        m_viewport->refresh();

    closeOrphanedPrivateTimelines();
    rebuildCameras();
    ensureWorkbenchServices();
    updateActions();
    updateStatus();
}

void NextWindow::ensureMediaServices()
{
    if (!m_document) {
        resetMediaServices();
        return;
    }

    if (!m_media) {
        m_media = std::make_unique<MediaServices>(m_document, m_settings);
        connect(m_media.get(), &MediaServices::htmlFrameReady, this, [this] {
            if (m_playback)
                m_playback->noteFramePresented();
            if (m_viewport)
                m_viewport->refresh();
            // Releases a prewarm frame held waiting for this HTML picture.
            if (m_workbench)
                m_workbench->notifyHtmlFrameReady();
        }, Qt::QueuedConnection);
        connect(m_media.get(), &MediaServices::htmlServiceFailed, this,
                [this](const QString& message) {
                    const QString text = tr("HTML 服务未启动：%1").arg(message);
                    statusBar()->showMessage(text, 8000);
                    qWarning().noquote() << text;
                    if (m_mcpLogPanel)
                        m_mcpLogPanel->addSystemEvent(tr("HTML 服务启动失败"),
                                                      message);
                });
        // Failures surface through the htmlServiceFailed signal; the video
        // providers keep working when Chrome is missing.
        QString ignored;
        m_media->start(&ignored);
        m_mediaProvidersAttached = false;
    }

    if (!m_mediaProvidersAttached)
        attachMediaProviders();
    if (m_cameraMonitor)
        m_cameraMonitor->setContext(m_document, m_media.get());
}

void NextWindow::configureVideoExtraction()
{
    if (!m_assetPanel)
        return;

    // The frame cache under .cache/video is keyed by the project fps and this
    // width, so a width change starts a new cache directory.  The disk budget
    // is the documented 2 GiB LRU limit.
    m_assetPanel->setVideoExtractionOptions(
        m_settings.videoProxyWidth, 2LL * 1024 * 1024 * 1024);
    // Cheap for finished videos: the queue skips complete cache indexes and
    // videos that are already queued or running.
    m_assetPanel->enqueueMissingVideoExtractions();
}

void NextWindow::resetMediaServices()
{
    // The monitor holds the media pointer for its frame providers; detach it
    // before the service (and the document behind it) goes away.
    if (m_cameraMonitor)
        m_cameraMonitor->setContext(nullptr, nullptr);
    if (m_viewport) {
        m_viewport->setVideoFrameProvider({});
        m_viewport->setHtmlFrameProvider({});
    }
    m_mediaProvidersAttached = false;
    if (m_media) {
        m_media->stop();
        m_media.reset();
    }
}

void NextWindow::attachMediaProviders()
{
    if (!m_viewport)
        return;

    m_viewport->setVideoFrameProvider(
        [this](const Uuid& entityUuid, double projectFrame) {
            return m_media ? m_media->videoFrame(entityUuid, projectFrame)
                           : QImage();
        });
    m_viewport->setHtmlFrameProvider(
        [this](const Uuid& entityUuid, double projectFrame) {
            return m_media ? m_media->htmlFrame(entityUuid, projectFrame)
                           : QImage();
        });
    m_mediaProvidersAttached = true;
}

void NextWindow::ensureWorkbenchServices()
{
    if (!m_document || !m_settings.workbenchCacheEnabled) {
        resetWorkbenchServices();
        return;
    }

    const Uuid camera = m_viewport ? m_viewport->cameraUuid() : Uuid();
    const int expand = camera.isEmpty()
                           ? std::clamp(
                                 m_settings.workbenchCacheExpandPercent, 0, 200)
                           : 0;
    if (m_workbench && m_workbench->cameraUuid() == camera
        && m_workbench->expandPercent() == expand)
        return;

    resetWorkbenchServices();

    WorkbenchServices::Providers providers;
    providers.videoFrame = [this](const Uuid& uuid, double frame) {
        return m_media ? m_media->videoFrame(uuid, frame) : QImage();
    };
    providers.htmlFrame = [this](const Uuid& uuid, double frame) {
        return m_media ? m_media->htmlFrame(uuid, frame) : QImage();
    };
    providers.htmlPending = [this] {
        return m_media && m_media->htmlPending();
    };

    m_workbench = std::make_unique<WorkbenchServices>(
        m_document, m_settings, camera, providers);

    if (m_viewport)
    {
        m_viewport->setSceneCacheHooks(
            [this](int frame, const Uuid& cameraUuid, bool* hit) {
                return m_workbench
                           ? m_workbench->lookup(frame, cameraUuid, hit)
                           : QImage();
            },
            [this](int frame, const Uuid& cameraUuid, const QImage& image) {
                if (m_workbench)
                    m_workbench->store(frame, cameraUuid, image);
            });
    }

    if (m_workbenchPanel)
        m_workbenchPanel->setServices(m_workbench.get());
    if (m_playback)
        m_workbench->setPlaybackActive(m_playback->isPlaying());
    updateWorkbenchStatus();
}

void NextWindow::resetWorkbenchServices()
{
    if (m_viewport)
        m_viewport->setSceneCacheHooks({}, {});
    if (m_workbenchPanel)
        m_workbenchPanel->setServices(nullptr);
    m_workbench.reset();
    updateWorkbenchStatus();
}

void NextWindow::updateWorkbenchStatus()
{
    if (!m_statusWorkbench)
        return;

    if (!m_document || !m_settings.workbenchCacheEnabled || !m_workbench) {
        m_statusWorkbench->hide();
        return;
    }

    const workbench::CacheStats stats = m_workbench->stats();
    const WorkbenchPrewarmProgress progress = m_workbench->progress();
    QString text;
    if (progress.scanning) {
        text = tr("缓存 %1/%2 · 预热 %3/%4")
                   .arg(stats.entries)
                   .arg(m_workbench->totalFrames())
                   .arg(progress.done)
                   .arg(progress.total);
    } else {
        text = tr("缓存 %1/%2 · 命中 %3 · 失效 %4")
                   .arg(stats.entries)
                   .arg(m_workbench->totalFrames())
                   .arg(stats.hits)
                   .arg(stats.misses);
    }
    m_statusWorkbench->setText(text);
    m_statusWorkbench->show();
}

void NextWindow::cacheAllFrames()
{
    if (!m_workbench) {
        statusBar()->showMessage(tr("工作台缓存未启用"), 4000);
        return;
    }
    m_workbench->startScanNow();
    statusBar()->showMessage(tr("正在缓存全部帧…"), 4000);
}

void NextWindow::cacheToPlayhead()
{
    if (!m_workbench || !m_document) {
        statusBar()->showMessage(tr("工作台缓存未启用"), 4000);
        return;
    }
    m_workbench->startScanNow(0, m_document->playhead());
    statusBar()->showMessage(
        tr("正在缓存到播放头（帧 %1）…").arg(m_document->playhead()), 4000);
}

void NextWindow::clearWorkbenchCache()
{
    if (!m_workbench) {
        statusBar()->showMessage(tr("工作台缓存未启用"), 4000);
        return;
    }
    m_workbench->clearCache();
    if (m_viewport)
        m_viewport->refresh();
    statusBar()->showMessage(tr("已清空工作台缓存"), 5000);
    updateWorkbenchStatus();
}

workbench::CacheStats NextWindow::workbenchStatsForTest() const
{
    return m_workbench ? m_workbench->stats() : workbench::CacheStats{};
}

QString NextWindow::workbenchCacheDirForTest() const
{
    return m_workbench ? m_workbench->cacheDirectory() : QString();
}

WorkbenchPrewarmProgress NextWindow::prewarmProgressForTest() const
{
    return m_workbench ? m_workbench->progress() : WorkbenchPrewarmProgress{};
}

QImage NextWindow::lookupForTest(int frame, const Uuid& cameraUuid)
{
    return m_workbench ? m_workbench->lookupForTest(frame, cameraUuid)
                       : QImage();
}

void NextWindow::startWorkbenchScanForTest()
{
    if (m_workbench)
        m_workbench->startScanNow();
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
    if (m_cameraMonitor)
        m_cameraMonitor->setCameraUuid(
            m_cameraCombo->currentData().toString());
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

void NextWindow::openProjectProperties()
{
    if (!m_document)
    {
        statusBar()->showMessage(tr("没有打开的工程"), 4000);
        return;
    }
    if (m_projectPropertiesDialog)
    {
        m_projectPropertiesDialog->raise();
        m_projectPropertiesDialog->activateWindow();
        return;
    }

    auto* dialog =
        new ProjectPropertiesDialog(m_document->project().settings, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // The callback runs inside accept(), before the dialog starts closing, so
    // the edited values are still readable.
    dialog->onAccepted = [this, dialog] {
        applyProjectSettings(dialog->name(), dialog->background());
    };
    m_projectPropertiesDialog = dialog;
    connect(dialog, &QObject::destroyed, this,
            [this] { m_projectPropertiesDialog = nullptr; });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void NextWindow::applyProjectSettings(const QString& name,
                                      const QString& background)
{
    if (!m_document)
        return;

    const QString trimmed = name.trimmed();
    const QColor color(background);
    if (trimmed.isEmpty() || trimmed.size() > 256)
    {
        showError(tr("工程属性"), tr("名称不能为空，最多 256 个字符。"));
        return;
    }
    if (!color.isValid())
    {
        showError(tr("工程属性"), tr("背景色不是有效颜色。"));
        return;
    }

    model::ProjectSettings& settings = m_document->mutableProject().settings;
    const QString previousName = settings.name;
    const QString previousBackground = settings.background;
    settings.name = trimmed;
    settings.background = color.name();

    QString error;
    if (!m_document->commitProject(
            QStringLiteral("pvf: edit project settings"), &error))
    {
        settings.name = previousName;
        settings.background = previousBackground;
        refreshAll();
        showError(tr("工程属性保存失败"), error);
        return;
    }

    refreshAll();
    statusBar()->showMessage(tr("工程属性已更新：%1").arg(trimmed), 5000);
}

Uuid NextWindow::insertEntityCopy(const model::Entity& source, int offsetX,
                                  int offsetY, const QString& message)
{
    if (!m_document)
        return {};

    model::Entity copy = source;
    copy.uuid = model::newUuid();
    copy.transform.x += offsetX;
    copy.transform.y += offsetY;

    m_document->mutableProject().entities.insert(copy.uuid, copy);

    QString error;
    if (!m_document->commitEntity(copy.uuid, message, &error))
    {
        m_document->mutableProject().entities.remove(copy.uuid);
        refreshAll();
        showError(tr("复制实体失败"), error);
        return {};
    }

    m_document->setSelection(copy.uuid);
    refreshAll();
    return copy.uuid;
}

void NextWindow::duplicateSelectedEntity()
{
    if (!m_document)
        return;
    // Ctrl+D inside a text editor is the editor's shortcut, not ours.
    if (focusIsTextInput())
        return;

    const Uuid uuid = m_document->selection();
    const auto it = m_document->project().entities.constFind(uuid);
    if (uuid.isEmpty() || it == m_document->project().entities.constEnd())
    {
        statusBar()->showMessage(tr("没有选中实体"), 4000);
        return;
    }
    if (it.value().locked)
    {
        statusBar()->showMessage(tr("实体已锁定，不能复制"), 4000);
        return;
    }

    const QString name = it.value().name;
    insertEntityCopy(it.value(), 0, 0,
                     QStringLiteral("pvf: duplicate entity \"%1\"").arg(name));
}

void NextWindow::copySelectedEntity()
{
    // Timeline focus means the keyframe clipboard owns Ctrl+C.
    if (forwardClipboardKey(Qt::Key_C))
        return;
    if (!m_document)
        return;

    const Uuid uuid = m_document->selection();
    const auto it = m_document->project().entities.constFind(uuid);
    if (uuid.isEmpty() || it == m_document->project().entities.constEnd())
    {
        statusBar()->showMessage(tr("没有选中实体"), 4000);
        return;
    }
    if (it.value().locked)
    {
        statusBar()->showMessage(tr("实体已锁定，不能复制"), 4000);
        return;
    }

    m_entityClipboard = it.value();
    m_hasEntityClipboard = true;
    updateActions();
    statusBar()->showMessage(tr("已复制实体：%1").arg(it.value().name), 3000);
}

void NextWindow::pasteEntity()
{
    // Timeline focus means the keyframe clipboard owns Ctrl+V.
    if (forwardClipboardKey(Qt::Key_V))
        return;
    if (!m_document)
        return;
    if (!m_hasEntityClipboard)
    {
        statusBar()->showMessage(tr("实体剪贴板为空"), 4000);
        return;
    }

    // The source parent may have been deleted after the copy.  A paste that
    // silently promotes the entity to root would change its world transform.
    if (!m_entityClipboard.parentUuid.isEmpty()
        && !m_document->project().entities.contains(
            m_entityClipboard.parentUuid))
    {
        statusBar()->showMessage(tr("原父实体已不存在，无法粘贴"), 4000);
        return;
    }

    insertEntityCopy(
        m_entityClipboard, 8, 8,
        QStringLiteral("pvf: paste entity \"%1\"")
            .arg(m_entityClipboard.name));
}

void NextWindow::fitView()
{
    if (!m_viewport || !m_document)
        return;

    // ViewportWidget's documented fit state is zoom == 1.0 (the canvas is
    // scaled to the widget).  The only public control is the wheel handler, so
    // replay one wheel step that lands exactly on the baseline zoom.  Pan is a
    // separate user offset and is intentionally left alone.
    const double zoom = m_viewport->zoom();
    if (!qFuzzyCompare(zoom, 1.0))
    {
        const double delta =
            120.0 * std::log(1.0 / zoom) / std::log(1.2);
        const int angle = int(std::lround(delta));
        if (angle != 0)
        {
            const QPointF position(m_viewport->width() / 2.0,
                                   m_viewport->height() / 2.0);
            QWheelEvent event(
                position,
                QPointF(m_viewport->mapToGlobal(position.toPoint())),
                QPoint(), QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(m_viewport, &event);
        }
    }

    statusBar()->showMessage(tr("视图已适配画布"), 3000);
}

bool NextWindow::focusIsTextInput() const
{
    return isTextInputWidget(QApplication::focusWidget());
}

bool NextWindow::forwardClipboardKey(int key) const
{
    QWidget* focus = QApplication::focusWidget();
    if (!focus)
        return false;

    // A focused timeline (including its scroll bar child) owns Ctrl+C/V for
    // keyframes.  The action already consumed the real shortcut, so replay it
    // on the timeline widget.
    TimelineWidget* timeline = qobject_cast<TimelineWidget*>(focus);
    for (QWidget* parent = focus->parentWidget();
         parent && !timeline; parent = parent->parentWidget())
    {
        timeline = qobject_cast<TimelineWidget*>(parent);
    }
    if (timeline)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::ControlModifier);
        // Deliver straight to the widget's event handler.  sendEvent() would
        // run the shortcut map again and re-enter this slot.
        static_cast<QObject*>(timeline)->event(&event);
        return true;
    }

    // Standard editors keep their native copy/paste.
    if (isTextInputWidget(focus))
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::ControlModifier);
        static_cast<QObject*>(focus)->event(&event);
        return true;
    }

    return false;
}

void NextWindow::setMcpConfigPathForTest(const QString& path)
{
    m_mcpConfigPath = path;
    if (m_document)
        startMcpServer();
}

void NextWindow::setCrashRecoveryHandlerForTest(CrashRecoveryHandler handler)
{
    m_crashRecoveryHandler = std::move(handler);
}

void NextWindow::setSettingsForTest(const AppSettings& settings)
{
    const AppSettings previous = m_settings;
    m_settings = settings;
    applySettings(previous);
}

void NextWindow::setOpenFolderHandlerForTest(
    std::function<void(const QString&)> handler)
{
    m_openFolderHandler = std::move(handler);
}

void NextWindow::showOnboardingIfNeeded()
{
    if (m_settings.onboardingDone)
        return;

    m_onboardingShown = true;
    auto* dialog = new OnboardingDialog(m_settings, this);
    dialog->setObjectName(QStringLiteral("onboardingDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // Non-modal keeps the shell usable and lets tests accept it without a
    // nested event loop.
    dialog->setModal(false);

    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        const AppSettings previous = m_settings;
        m_settings.gitEnabled = dialog->gitEnabled();
        m_settings.onboardingDone = true;
        if (m_settingsStore)
            m_settingsStore->save(m_settings);
        applySettings(previous);
    });
    connect(dialog, &QDialog::rejected, this, [this] {
        // Dismissing the welcome page still counts as "asked once"; Git stays
        // off until the human enables it in the settings dialog.
        m_settings.onboardingDone = true;
        if (m_settingsStore)
            m_settingsStore->save(m_settings);
    });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void NextWindow::refreshHistory()
{
    if (!m_historyPanel)
        return;

    if (!m_document || !m_document->store()) {
        m_historyPanel->setCommits({});
        m_historyPanel->setStatus(tr("未打开工程"));
        return;
    }

    store::ProjectStore* store = m_document->store();
    if (!store->gitEnabled()) {
        m_historyPanel->setCommits({});
        m_historyPanel->setStatus(tr("Git 未启用"));
        return;
    }

    QString error;
    const QVector<store::GitCommit> commits = store->history(200, &error);
    if (!error.isEmpty()) {
        m_historyPanel->setCommits({});
        m_historyPanel->setStatus(tr("读取历史失败：%1").arg(error));
        return;
    }

    m_historyPanel->setCommits(commits);
    m_historyPanel->setStatus(commits.isEmpty()
                                  ? tr("还没有提交")
                                  : tr("%1 个提交").arg(commits.size()));
}

void NextWindow::restoreHistoryCommit(const QString& hash)
{
    if (!m_document || !m_document->store() || hash.isEmpty())
        return;

    if (m_historyPanel)
        m_historyPanel->setBusy(true);

    QMessageBox box(QMessageBox::Question, tr("恢复 Git 历史"),
                    tr("恢复到 %1？会丢弃当前未提交文本。").arg(hash.left(7)),
                    QMessageBox::NoButton, this);
    QPushButton* restore =
        box.addButton(tr("恢复"), QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();

    QString error;
    const bool ok = box.clickedButton() == restore
                    && restoreCommitInternal(hash, &error);
    if (m_historyPanel)
        m_historyPanel->setBusy(false);

    if (!ok) {
        if (!error.isEmpty()) {
            statusBar()->showMessage(tr("恢复失败：%1").arg(error), 8000);
            QMessageBox::warning(this, tr("恢复失败"), error);
        }
        return;
    }

    statusBar()->showMessage(tr("已恢复到 %1").arg(hash.left(7)), 5000);
    if (m_mcpLogPanel)
        m_mcpLogPanel->addSystemEvent(tr("恢复历史"),
                                      tr("已恢复到 %1").arg(hash.left(7)));
}

bool NextWindow::restoreCommitInternal(const QString& hash, QString* error)
{
    if (error)
        error->clear();
    if (!m_document || !m_document->store()) {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }
    if (hash.isEmpty()) {
        if (error)
            *error = tr("未选择提交");
        return false;
    }

    store::ProjectStore* store = m_document->store();
    if (!store->gitEnabled()) {
        if (error)
            *error = tr("Git 未启用");
        return false;
    }

    // Restoring requires a clean text tree; push the debounced human edit out
    // first so it is either committed (and then discarded by the restore) or
    // reported instead of leaving a half-restored project.
    QString flushError;
    if (m_document->hasPendingCommit())
        m_document->flush(&flushError);
    if (!flushError.isEmpty()) {
        if (error)
            *error = flushError;
        return false;
    }

    if (!store->restoreCommit(hash, error))
        return false;

    m_document->reloadAfterHistory(error);
    refreshAll();
    refreshHistory();
    return true;
}

void NextWindow::openOutputFolder(const QString& path)
{
    if (m_openFolderHandler) {
        m_openFolderHandler(path);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void NextWindow::startMcpServer()
{
    stopMcpServer();

    if (!m_document || !m_document->store()) {
        if (m_mcpLogPanel)
            m_mcpLogPanel->setServerState(false, 0, {});
        updateStatus();
        return;
    }

    m_mcpService = std::make_unique<mcp2::Service>(
        m_document->store(), &m_document->assets());
    // Git is the write audit trail.  Without it the MCP surface stays
    // readable but every write tool is denied with permission_denied.
    m_mcpService->setWritesEnabled(m_document->store()->gitEnabled());
    const bool writesEnabled = m_mcpService->writesEnabled();
    m_mcpService->setViewStateProvider([this] {
        mcp2::ViewState state;
        if (m_document) {
            state.playhead = m_document->playhead();
            state.selection = m_document->selection();
            if (m_timeline)
                state.timelineParent = m_timeline->parentUuid();
            if (m_viewport)
                state.cameraUuid = m_viewport->cameraUuid();
        }
        return state;
    });
    m_mcpService->setVideoFrameProvider([this](const Uuid& uuid, double frame) {
        if (!m_media)
            ensureMediaServices();
        return m_media ? m_media->videoFrame(uuid, frame) : QImage();
    });
    m_mcpService->setHtmlFrameProvider([this](const Uuid& uuid, double frame) {
        if (!m_media)
            ensureMediaServices();
        return m_media ? m_media->htmlFrameBlocking(uuid, frame) : QImage();
    });
    // Long batch tools run on the UI thread.  Pumping events at each batch
    // checkpoint keeps the emergency-stop button and the AI lock indicator
    // responsive while a transaction is in flight.
    m_mcpService->setBatchCheckpoint([] {
        QCoreApplication::processEvents();
    });

    auto server = std::make_unique<mcp2::HttpServer>(m_mcpService.get());
    if (!m_mcpConfigPath.isEmpty()) {
        server->setConfigFilePath(m_mcpConfigPath);
        // Keep the test endpoint file next to the temporary config instead of
        // overwriting the user's real endpoint file.
        server->setEndpointFilePath(m_mcpConfigPath
                                    + QStringLiteral(".endpoint.json"));
    }

    mcp2::HttpServer* rawServer = server.get();

    connect(rawServer, &mcp2::HttpServer::toolCallStarted, this,
            [this](const QString& name, const QString& argsJson, bool write) {
                ++m_activeToolCalls;
                m_runningToolArgs.insert(name, argsJson);
                if (write) {
                    ++m_runningWrites[name];
                    ++m_aiBusyWrites;
                }

                if (!m_mcpAutoShown) {
                    m_mcpAutoShown = true;
                    if (m_mcpLogAction) {
                        QSignalBlocker blocker(m_mcpLogAction);
                        m_mcpLogAction->setChecked(true);
                    }
                    showMcpLog();
                }
                if (m_mcpLogPanel)
                    m_mcpLogPanel->addSystemEvent(tr("开始：%1").arg(name),
                                                  argsJson);
                refreshAiLock();
            });

    connect(rawServer, &mcp2::HttpServer::toolCallFinished, this,
            [this](const QString& name, const QString& resultJson, qint64 ms,
                   bool ok) {
                if (m_activeToolCalls > 0)
                    --m_activeToolCalls;
                const QString argsJson = m_runningToolArgs.take(name);
                auto writeIt = m_runningWrites.find(name);
                if (writeIt != m_runningWrites.end()) {
                    if (m_aiBusyWrites > 0)
                        --m_aiBusyWrites;
                    if (--writeIt.value() <= 0)
                        m_runningWrites.erase(writeIt);
                }

                if (m_mcpLogPanel)
                    m_mcpLogPanel->addToolInvocation(name, argsJson, resultJson,
                                                     ms, ok);

                // Write tools commit straight through the store.  Reload the
                // document from disk first so every panel sees the new bytes,
                // even when the tool failed after partially writing assets.
                if (mcp2::Service::isWriteTool(name))
                {
                    QString reloadError;
                    if (m_document
                        && !m_document->notifyExternalChange(&reloadError)
                        && !reloadError.isEmpty())
                    {
                        if (m_mcpLogPanel)
                            m_mcpLogPanel->addSystemEvent(tr("刷新工程失败"),
                                                          reloadError);
                        statusBar()->showMessage(
                            tr("刷新工程失败：%1").arg(reloadError), 8000);
                    }
                    refreshAll();
                    refreshHistory();
                }
                refreshAiLock();
                statusBar()->showMessage(
                    ok ? tr("AI %1 完成（%2 ms）").arg(name).arg(ms)
                       : tr("AI %1 失败（%2 ms）").arg(name).arg(ms),
                    5000);
            });

    connect(rawServer, &mcp2::HttpServer::stoppedChanged, this,
            [this](bool stopped) {
                m_aiStopped = stopped;
                if (m_mcpLogPanel)
                    m_mcpLogPanel->addSystemEvent(
                        stopped ? tr("AI 已急停")
                                : tr("AI 写入已恢复"));
                refreshAiLock();
                statusBar()->showMessage(
                    stopped ? tr("AI 已急停：写入暂停，直到点“恢复 AI”")
                            : tr("AI 写入已恢复"),
                    5000);
            });

    // The test transport config supplies its own port (usually 0 for an
    // ephemeral one); production follows the persisted settings.
    const quint16 requestedPort =
        m_mcpConfigPath.isEmpty() ? m_settings.mcpPort : 0;
    QString error;
    const quint16 port = rawServer->start(requestedPort, &error);
    if (port == 0) {
        m_mcpError = error.isEmpty() ? tr("未知错误") : error;
        m_mcpStatus = tr("MCP 未启动: %1").arg(m_mcpError);
        qWarning().noquote() << QStringLiteral("PVForge MCP:") << m_mcpError;
    } else {
        m_mcpError.clear();
        m_mcpStatus = tr("MCP 127.0.0.1:%1").arg(port);
    }

    m_mcpServer = std::move(server);
    m_aiStopped = rawServer->aiStopped();
    if (m_mcpLogPanel) {
        m_mcpLogPanel->setServerState(port != 0, port, m_mcpError);
        m_mcpLogPanel->setAiState(m_aiStopped, false);
        m_mcpLogPanel->addSystemEvent(port != 0 ? tr("MCP 已启动")
                                                : tr("MCP 启动失败"),
                                      port != 0 ? m_mcpStatus : m_mcpError);
        if (!writesEnabled)
            m_mcpLogPanel->addSystemEvent(
                tr("MCP 只读"),
                tr("Git 未启用：写工具返回 permission_denied（git_disabled），读取工具仍可用"));
    }
    refreshAiLock();
    updateStatus();
}

void NextWindow::stopMcpServer()
{
    if (m_mcpServer) {
        m_mcpServer->stop();
        m_mcpServer.reset();
    }
    m_mcpService.reset();
    m_mcpStatus.clear();
    m_mcpError.clear();
    resetAiLock();
    if (m_mcpLogPanel)
        m_mcpLogPanel->setServerState(false, 0, {});
}

void NextWindow::setHumanEditingEnabled(bool enabled)
{
    m_humanEditingEnabled = enabled;

    // Playback, playhead scrubbers, camera/view controls and the MCP log stay
    // live so the human can stop the AI or inspect the timeline while locked.
    for (QWidget* panel : {static_cast<QWidget*>(m_assetPanel),
                           static_cast<QWidget*>(m_viewport),
                           static_cast<QWidget*>(m_propertiesPanel),
                           static_cast<QWidget*>(m_timeline)}) {
        if (panel)
            panel->setEnabled(enabled);
    }

    if (m_mcpLogPanel)
        m_mcpLogPanel->setEnabled(true);

    // updateActions() also gates every write QAction on this flag, so a
    // document change refresh cannot re-enable a shortcut while locked.
    updateActions();
}

void NextWindow::refreshAiLock()
{
    // An emergency stop deliberately releases human editing even though the
    // in-flight tool is still unwinding; the Service keeps rejecting AI
    // writes until resumeAi().  Without a stop, only a running write locks.
    const bool writing = m_aiBusyWrites > 0 && !m_aiStopped;
    setHumanEditingEnabled(!writing);

    if (m_statusAi) {
        if (m_aiStopped) {
            m_statusAi->setText(tr("AI 已急停"));
            m_statusAi->setStyleSheet(QStringLiteral("color: #e08a3c;"));
            m_statusAi->show();
        } else if (writing) {
            m_statusAi->setText(tr("AI 修改中…"));
            m_statusAi->setStyleSheet(QStringLiteral("color: #5aa9e6;"));
            m_statusAi->show();
        } else {
            m_statusAi->clear();
            m_statusAi->hide();
        }
    }

    // The log panel itself must stay usable so the human can press stop or
    // resume while the editor is locked.
    if (m_mcpLogPanel)
        m_mcpLogPanel->setAiState(m_aiStopped, writing);
}

void NextWindow::resetAiLock()
{
    m_aiStopped = false;
    m_activeToolCalls = 0;
    m_aiBusyWrites = 0;
    m_runningWrites.clear();
    m_runningToolArgs.clear();
    refreshAiLock();
}

void NextWindow::applyHumanCommitDelay()
{
    if (m_document)
        m_document->setHumanCommitDelayMs(m_settings.humanCommitDelayMs);
}

void NextWindow::applyViewportSnapSettings()
{
    if (!m_viewport)
        return;
    m_viewport->setSnapThresholdPixels(m_settings.snapPixels);
    m_viewport->setSnapCanvasCenter(m_settings.snapCanvasCenter);
    m_viewport->setSnapEntityEdges(m_settings.snapEntityEdges);
    m_viewport->setSnapEdgeMidpoints(m_settings.snapEdgeMidpoints);
    m_viewport->setSnapAngles(m_settings.snapAngles);
}

void NextWindow::applySettings(const AppSettings& previous)
{
    applyHumanCommitDelay();
    applyViewportSnapSettings();
    if (m_playback)
        m_playback->setWatchdogMs(m_settings.watchdogMs);

    if (m_media) {
        m_media->applySettings(m_settings);
        if (m_viewport)
            m_viewport->refresh();
        if (m_cameraMonitor)
            m_cameraMonitor->refresh();
    }

    // The cache directory key includes the scale and expansion, so a workbench
    // settings change rebuilds the service (and its scan) instead of reusing
    // the old one.
    if (previous.workbenchCacheEnabled != m_settings.workbenchCacheEnabled
        || previous.workbenchCacheScalePercent
               != m_settings.workbenchCacheScalePercent
        || previous.workbenchCacheExpandPercent
               != m_settings.workbenchCacheExpandPercent
        || previous.workbenchCacheDiskBytes
               != m_settings.workbenchCacheDiskBytes
        || previous.workbenchCacheMemoryBytes
               != m_settings.workbenchCacheMemoryBytes) {
        if (m_viewport)
            m_viewport->setSceneExpandPercent(
                m_settings.workbenchCacheExpandPercent);
        resetWorkbenchServices();
        ensureWorkbenchServices();
    }

    // A new proxy width changes the extraction cache key; enqueue again so
    // existing videos fill the new directory.
    configureVideoExtraction();

    const bool portChanged = previous.mcpPort != m_settings.mcpPort;
    if (portChanged && !m_document) {
        statusBar()->showMessage(
            tr("MCP 端口已保存为 %1，下次启动 MCP 时生效").arg(m_settings.mcpPort),
            6000);
    } else if (portChanged && mcpToolRunning()) {
        statusBar()->showMessage(
            tr("MCP 端口已保存为 %1，当前 AI 操作结束后重启应用生效")
                .arg(m_settings.mcpPort),
            8000);
    } else if (portChanged) {
        // Rebind immediately instead of leaving the status bar claiming the
        // old port.  startMcpServer() stops the previous listener first.
        const bool wasStopped = m_aiStopped;
        startMcpServer();
        if (wasStopped && m_mcpServer)
            m_mcpServer->stopAi();
        statusBar()->showMessage(
            m_mcpServer && m_mcpServer->isRunning()
                ? tr("MCP 端口已切换为 %1").arg(m_mcpServer->port())
                : tr("MCP 端口已改为 %1，但启动失败：%2")
                      .arg(m_settings.mcpPort)
                      .arg(m_mcpError),
            8000);
    }

    if (m_mcpLogPanel && portChanged)
        m_mcpLogPanel->addSystemEvent(tr("设置已更新"),
                                      tr("MCP 端口：%1").arg(m_settings.mcpPort));

    if (m_document && previous.gitEnabled != m_settings.gitEnabled) {
        QString error;
        if (!m_document->setGitEnabled(m_settings.gitEnabled, &error)) {
            // Settings is only a wish; the store reports what actually
            // happened (for example no git binary).  Persist the truth so the
            // next launch does not claim a repository that is not there.
            const bool actual = m_document->store()
                                 && m_document->store()->gitEnabled();
            m_settings.gitEnabled = actual;
            if (m_settingsStore)
                m_settingsStore->save(m_settings);
            const QString text = tr("Git 切换失败：%1")
                                     .arg(error.isEmpty() ? tr("未知错误")
                                                          : error);
            statusBar()->showMessage(text, 8000);
            if (m_mcpLogPanel)
                m_mcpLogPanel->addSystemEvent(tr("Git 切换失败"), text);
        } else {
            statusBar()->showMessage(m_settings.gitEnabled ? tr("Git 已启用")
                                                           : tr("Git 已关闭"),
                                     5000);
            if (m_mcpLogPanel)
                m_mcpLogPanel->addSystemEvent(
                    m_settings.gitEnabled ? tr("Git 已启用")
                                          : tr("Git 已关闭"),
                    m_settings.gitEnabled
                        ? tr("MCP 写工具可用")
                        : tr("MCP 只读：写工具返回 permission_denied"));
        }
        if (m_mcpService)
            m_mcpService->setWritesEnabled(m_document->store()->gitEnabled());
        refreshHistory();
        updateSettingsDialogContext();
    }

    updateStatus();
}

void NextWindow::openSettings()
{
    if (m_settingsDialog) {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }

    auto* dialog = new SettingsDialog(m_settingsStore.get(), this);
    dialog->setObjectName(QStringLiteral("settingsDialog"));
    // Non-modal so the rest of the shell and the dock stay observable (and
    // testable) while the dialog is open.
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setSettings(m_settingsStore->load());
    m_settingsDialog = dialog;
    updateSettingsDialogContext();

    connect(dialog, &SettingsDialog::clearHtmlCacheRequested, this,
            &NextWindow::clearHtmlCache);
    connect(dialog, &SettingsDialog::resetMcpTokenRequested, this,
            &NextWindow::resetMcpToken);
    connect(dialog, &SettingsDialog::chromeDetectRequested, this,
            &NextWindow::detectChrome);
    connect(dialog, &QDialog::accepted, this,
            &NextWindow::reloadSettingsFromStore);
    if (auto* apply =
            dialog->findChild<QPushButton*>(QStringLiteral("applyButton"))) {
        connect(apply, &QPushButton::clicked, this,
                &NextWindow::reloadSettingsFromStore);
    }
    connect(dialog, &QObject::destroyed, this, [this] {
        m_settingsDialog = nullptr;
    });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void NextWindow::reloadSettingsFromStore()
{
    if (!m_settingsStore)
        return;
    const AppSettings previous = m_settings;
    m_settings = m_settingsStore->load();
    applySettings(previous);
}

void NextWindow::updateSettingsDialogContext()
{
    if (!m_settingsDialog)
        return;

    QString repositoryPath;
    QString recentCommit;
    if (m_document) {
        repositoryPath = m_document->root();
        if (m_document->store()) {
            QString error;
            const QVector<store::GitCommit> history =
                m_document->store()->history(1, &error);
            if (!history.isEmpty()) {
                const store::GitCommit& commit = history.first();
                recentCommit = QStringLiteral("%1 %2")
                                   .arg(commit.hash.left(7), commit.message);
            }
        }
    }
    m_settingsDialog->setGitInfo(repositoryPath, recentCommit);
    m_settingsDialog->setEndpointPath(
        m_mcpServer ? m_mcpServer->endpointFilePath()
                    : SettingsDialog::defaultEndpointPath());
}

void NextWindow::clearHtmlCache()
{
    QStringList targets;
    const QString appCache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!appCache.isEmpty())
        targets.append(QDir(appCache).filePath(QStringLiteral("html")));
    if (m_document)
        targets.append(
            QDir(m_document->root()).filePath(QStringLiteral(".cache/html")));

    int files = 0;
    qint64 bytes = 0;
    QStringList removed;
    QStringList failed;
    for (const QString& path : targets) {
        if (!QFileInfo::exists(path))
            continue;

        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++files;
            bytes += it.fileInfo().size();
        }

        if (QDir(path).removeRecursively())
            removed.append(QDir::toNativeSeparators(path));
        else
            failed.append(QDir::toNativeSeparators(path));
    }

    QString message;
    if (removed.isEmpty() && failed.isEmpty()) {
        message = tr("没有可清理的 HTML 缓存");
    } else {
        message = tr("已清理 HTML 缓存：%1 个文件，%2")
                      .arg(files)
                      .arg(formatBytes(bytes));
        if (!failed.isEmpty())
            message += tr("；%1 个目录清理失败：%2")
                           .arg(failed.size())
                           .arg(failed.join(QStringLiteral("、")));
    }

    statusBar()->showMessage(message, 10000);
    if (m_mcpLogPanel)
        m_mcpLogPanel->addSystemEvent(tr("清空 HTML 缓存"), message);
}

void NextWindow::resetMcpToken()
{
    if (m_mcpServer) {
        m_mcpServer->resetCredentials();
    } else {
        // No project is open, but the persisted credential still lives in the
        // MCP config file.  Rotate it without starting a listener.
        mcp2::HttpServer server(nullptr);
        if (!m_mcpConfigPath.isEmpty())
            server.setConfigFilePath(m_mcpConfigPath);
        server.resetCredentials();
    }

    const QString message =
        tr("MCP token 已重置；客户端需要重新读取 endpoint 文件");
    statusBar()->showMessage(message, 8000);
    if (m_mcpLogPanel)
        m_mcpLogPanel->addSystemEvent(tr("重置 MCP token"), message);
}

void NextWindow::detectChrome()
{
    QString requested = m_settings.chromeExecutable;
    if (m_settingsDialog) {
        if (auto* edit = m_settingsDialog->findChild<QLineEdit*>(
                QStringLiteral("chromeExecutable")))
            requested = edit->text().trimmed();
    }

    QString found = html::HtmlBrowser::detectExecutable(requested);
    if (found.isEmpty() && !requested.isEmpty())
        found = html::HtmlBrowser::detectExecutable();

    if (found.isEmpty()) {
        const QString message =
            tr("未找到 Chrome/Chromium。请安装 Chrome/Chromium，"
               "或在设置里手动指定可执行文件路径。");
        statusBar()->showMessage(message, 10000);
        if (m_mcpLogPanel)
            m_mcpLogPanel->addSystemEvent(tr("Chrome 检测失败"), message);

        auto* box = new QMessageBox(QMessageBox::Warning, tr("Chrome 检测"),
                                    message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return;
    }

    if (m_settingsDialog) {
        AppSettings updated = m_settingsDialog->settings();
        updated.chromeExecutable = found;
        m_settingsDialog->setSettings(updated);
    }

    const QString message = tr("已检测到 Chrome：%1")
                                .arg(QDir::toNativeSeparators(found));
    statusBar()->showMessage(message, 8000);
    if (m_mcpLogPanel)
        m_mcpLogPanel->addSystemEvent(tr("Chrome 检测成功"), message);
}

void NextWindow::showMcpLog()
{
    if (!m_mcpLogDock)
        return;
    m_mcpLogDock->show();
    // Keep the log beside the camera monitor instead of letting its wide size
    // hint squeeze the monitor into a narrow strip.
    if (m_cameraMonitorDock) {
        const int half = std::max(200, width() / 2);
        resizeDocks({m_cameraMonitorDock, m_mcpLogDock}, {half, half},
                    Qt::Horizontal);
    }
    if (m_mcpLogPanel) {
        m_mcpLogPanel->show();
        m_mcpLogPanel->raise();
    }
}

void NextWindow::exportVideo()
{
    if (!m_document)
        return;

    if (m_exportHandler)
    {
        m_exportHandler();
        return;
    }

    QString flushError;
    m_document->flush(&flushError);
    if (!flushError.isEmpty())
    {
        statusBar()->showMessage(tr("提交失败：%1").arg(flushError), 8000);
        return;
    }

    const Uuid preferred = m_viewport ? m_viewport->cameraUuid() : Uuid();
    ExportDialog dialog(m_document->project(), preferred, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    startExport(dialog.cameraUuid(), dialog.startFrame(), dialog.endFrame(),
                dialog.outputPath(), dialog.includeAudio(), dialog.outputFps(),
                dialog.resolutionScale(), dialog.overwriteExisting());
}

void NextWindow::startExport(const Uuid& cameraUuid, int startFrame, int endFrame,
                             const QString& outputPath, bool includeAudio,
                             int outputFps, int resolutionScale,
                             bool overwriteExisting)
{
    if (!m_document || outputPath.isEmpty() || m_exportRunning)
        return;

    export2::Options options;
    options.overwrite = overwriteExisting;
    if (m_exportOverwriteProbe)
    {
        m_exportOverwriteProbe(options.overwrite);
        return;
    }

    // The export worker captures the media services; make sure they exist and
    // that the HTML browsers were started by this (the owning) thread.
    ensureMediaServices();
    if (!m_media)
        return;
    QString htmlError;
    m_media->start(&htmlError);
    if (!htmlError.isEmpty())
        qWarning().noquote() << "export HTML service:" << htmlError;
    QPointer<MediaServices> media(m_media.get());

    auto project = std::make_shared<model::Project>(m_document->project());
    auto assets = std::make_shared<assets::AssetStore>(m_document->assets());

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

    options.outputPath = outputPath;
    options.cameraUuid = cameraUuid;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    options.outputSize = QSize(); // empty: project size * resolutionScale
    options.fps = selectedFps;
    options.outputFps = selectedFps;
    options.resolutionScale = scale;
    options.includeAudio = includeAudio;
    options.crf = 18;
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

    // Progress counts output frames, not project frames: 2x/4x fps exports
    // have two/four times as many.
    const double durationSeconds =
        double(std::max(0, endFrame - startFrame)) / double(projectFps);
    const int totalFrames =
        std::max(1, int(std::llround(durationSeconds * double(selectedFps))));
    auto progressValue = std::make_shared<std::atomic_int>(0);
    auto cancelled = std::make_shared<std::atomic_bool>(false);

    auto* progress = new QProgressDialog(
        tr("正在导出视频…"), tr("取消"), 0, totalFrames, this);
    progress->setWindowTitle(tr("导出视频"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setLabelText(tr("正在导出视频… 0 / %1 帧").arg(totalFrames));
    progress->setValue(0);

    auto* watcher = new QFutureWatcher<export2::Result>(this);
    auto* timer = new QTimer(watcher);

    connect(progress, &QProgressDialog::canceled, this,
            [cancelled] { cancelled->store(true); });
    connect(timer, &QTimer::timeout, this,
            [progress, progressValue, cancelled] {
                if (progress->wasCanceled())
                    cancelled->store(true);
                const int frame = progressValue->load();
                if (frame > progress->value()) {
                    progress->setValue(std::min(frame, progress->maximum()));
                    progress->setLabelText(
                        tr("正在导出视频… %1 / %2 帧")
                            .arg(frame)
                            .arg(progress->maximum()));
                }
            });
    timer->start(100);

    connect(watcher, &QFutureWatcher<export2::Result>::finished, this,
            [this, watcher, progress, timer, outputPath] {
                m_exportRunning = false;
                timer->stop();
                const export2::Result result = watcher->result();
                progress->close();
                progress->deleteLater();
                watcher->deleteLater();

                if (result.ok) {
                    QString text = tr("已导出 %1 帧到 %2")
                                       .arg(result.framesWritten)
                                       .arg(QDir::toNativeSeparators(outputPath));
                    if (!result.warnings.isEmpty())
                        text += QStringLiteral("\n\n") + result.warnings.join('\n');

                    const QString folder = QFileInfo(outputPath).absolutePath();

                    QMessageBox box(QMessageBox::Information, tr("导出完成"),
                                    text, QMessageBox::NoButton, this);
                    QPushButton* openFolderButton =
                        box.addButton(tr("打开文件夹"), QMessageBox::ActionRole);
                    QPushButton* okButton =
                        box.addButton(tr("确定"), QMessageBox::AcceptRole);
                    box.setDefaultButton(okButton);
                    box.exec();
                    if (box.clickedButton() == openFolderButton)
                        openOutputFolder(folder);
                } else if (result.error == QStringLiteral("cancelled")) {
                    statusBar()->showMessage(tr("导出已取消"), 5000);
                } else {
                    QMessageBox::critical(this, tr("导出失败"), result.error);
                }
            });

    progress->show();
    m_exportRunning = true;
    watcher->setFuture(QtConcurrent::run(
        [project, assets, options, progressValue, cancelled, media] {
            export2::Result result = export2::exportVideo(
                *project, *assets, options,
                [progressValue, cancelled](int frame, int) {
                    progressValue->store(frame);
                    return !cancelled->load();
                });
            // The final HTML browser was created on this worker thread; release
            // it here so it is never destroyed from the UI thread.
            if (media)
                media->stopExportHtml();
            return result;
        }));
}

void NextWindow::openPrivateTimelineForTest(const Uuid& parentUuid)
{
    openPrivateTimeline(parentUuid);
}

QDialog* NextWindow::privateTimelineDialogForTest(const Uuid& parentUuid) const
{
    return m_privateTimelines.value(parentUuid, nullptr);
}

void NextWindow::togglePlaybackForTest()
{
    if (m_playback)
        m_playback->toggle();
}

bool NextWindow::isPlayingForTest() const
{
    return m_playback && m_playback->isPlaying();
}

bool NextWindow::deleteSelectedEntityForTest(bool confirmed, QString* error)
{
    const QVector<Uuid> uuids = collectDeletionSet(error);
    if (uuids.isEmpty())
        return false;
    if (uuids.size() > 1 && !confirmed) {
        if (error)
            *error = tr("需要确认级联删除");
        return false;
    }
    return deleteEntitySet(uuids, error);
}

void NextWindow::openPrivateTimeline(const Uuid& parentUuid)
{
    if (!m_document)
        return;

    const auto owner =
        m_document->project().entities.constFind(parentUuid);
    if (owner == m_document->project().entities.constEnd()) {
        showError(tr("私有时间线"), tr("父实体不存在或已被删除。"));
        return;
    }

    if (QDialog* existing = m_privateTimelines.value(parentUuid)) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    const QString ownerName = owner->name.trimmed().isEmpty()
                                  ? tr("未命名实体")
                                  : owner->name;

    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("privateTimelineDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->setWindowTitle(tr("私有时间线 - %1").arg(ownerName));
    dialog->resize(900, 300);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* timeline = new TimelineWidget(m_document, dialog);
    timeline->setObjectName(QStringLiteral("privateTimelineWidget"));
    timeline->setParentUuid(parentUuid);
    layout->addWidget(timeline);

    m_privateTimelines.insert(parentUuid, dialog);
    connect(dialog, &QDialog::destroyed, this, [this, parentUuid] {
        m_privateTimelines.remove(parentUuid);
    });
    connect(timeline, &TimelineWidget::privateTimelineRequested, this,
            &NextWindow::openPrivateTimeline);
    connect(timeline, &TimelineWidget::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
            });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void NextWindow::closeOrphanedPrivateTimelines()
{
    if (m_privateTimelines.isEmpty())
        return;
    if (!m_document) {
        closeAllPrivateTimelines();
        return;
    }

    QVector<Uuid> stale;
    const auto& entities = m_document->project().entities;
    for (auto it = m_privateTimelines.constBegin();
         it != m_privateTimelines.constEnd(); ++it) {
        if (!entities.contains(it.key()))
            stale.append(it.key());
    }

    for (const Uuid& uuid : stale) {
        QDialog* dialog = m_privateTimelines.take(uuid);
        if (dialog) {
            // The destroyed handler would touch m_privateTimelines; detach it
            // before WA_DeleteOnClose eventually destroys the dialog.
            disconnect(dialog, nullptr, this, nullptr);
            dialog->close();
        }
    }
}

void NextWindow::closeAllPrivateTimelines()
{
    if (m_privateTimelines.isEmpty())
        return;

    const auto dialogs = m_privateTimelines.values();
    m_privateTimelines.clear();
    for (QDialog* dialog : dialogs) {
        if (dialog) {
            disconnect(dialog, nullptr, this, nullptr);
            dialog->close();
        }
    }
}

void NextWindow::updatePlaybackAction()
{
    if (!m_playPauseAction)
        return;
    const bool playing = m_playback && m_playback->isPlaying();
    m_playPauseAction->setText(playing ? tr("暂停") : tr("播放"));
    m_playPauseAction->setEnabled(m_document != nullptr);
}

void NextWindow::updateActions()
{
    const bool open = m_document != nullptr;
    const bool editing = open && m_humanEditingEnabled;
    const bool actionsEnabled = m_humanEditingEnabled;
    updatePlaybackAction();
    if (m_undoAction)
        m_undoAction->setEnabled(editing);
    if (m_redoAction)
        m_redoAction->setEnabled(editing);

    bool hasCamera = false;
    if (m_document) {
        const auto& entities = m_document->project().entities;
        for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
            if (it.value().content.kind == ContentKind::Camera) {
                hasCamera = true;
                break;
            }
        }
    }
    if (m_exportAction)
        m_exportAction->setEnabled(editing && hasCamera);

    if (m_projectPropertiesAction)
        m_projectPropertiesAction->setEnabled(editing);
    if (m_fitViewAction)
        m_fitViewAction->setEnabled(open);
    // Project switching destroys the MCP service, so it is a write entry too.
    if (m_newAction)
        m_newAction->setEnabled(actionsEnabled);
    if (m_openAction)
        m_openAction->setEnabled(actionsEnabled);
    if (m_settingsAction)
        m_settingsAction->setEnabled(actionsEnabled);

    bool canEditSelection = false;
    if (m_document) {
        const Uuid selection = m_document->selection();
        const auto it = m_document->project().entities.constFind(selection);
        canEditSelection = it != m_document->project().entities.constEnd()
                           && !it.value().locked;
    }
    if (m_deleteAction)
        m_deleteAction->setEnabled(canEditSelection && m_humanEditingEnabled);
    // Copy/duplicate act on the selected editable entity.  Paste only needs a
    // snapshot; the parent check happens when it is actually pasted.
    if (m_copyEntityAction)
        m_copyEntityAction->setEnabled(canEditSelection && m_humanEditingEnabled);
    if (m_duplicateAction)
        m_duplicateAction->setEnabled(canEditSelection && m_humanEditingEnabled);
    if (m_pasteEntityAction)
        m_pasteEntityAction->setEnabled(editing && m_hasEntityClipboard);

    for (QAction* action : {m_createEmptyAction, m_createTextAction,
                            m_createVectorAction, m_createCameraAction}) {
        if (action)
            action->setEnabled(editing);
    }
    if (m_autoKeyCheck)
        m_autoKeyCheck->setEnabled(open);
    if (m_playheadSpin)
        m_playheadSpin->setEnabled(open);
    if (m_cameraCombo)
        m_cameraCombo->setEnabled(open);
    if (m_gizmoCheck)
        m_gizmoCheck->setEnabled(open);
    if (m_gizmoAction)
        m_gizmoAction->setEnabled(open);
    for (QAction* action : {m_homeAction, m_endAction, m_prevFrameAction,
                            m_nextFrameAction}) {
        if (action)
            action->setEnabled(open);
    }
}

void NextWindow::updateStatus()
{
    if (m_statusMcp) {
        m_statusMcp->setText(m_mcpStatus.isEmpty() ? tr("MCP 未启动")
                                                   : m_mcpStatus);
    }

    if (!m_document) {
        setWindowTitle(tr("PVForge Next"));
        if (m_statusDir)
            m_statusDir->setText(tr("未打开工程"));
        if (m_statusFrame)
            m_statusFrame->setText(tr("帧 —"));
        if (m_statusFps)
            m_statusFps->setText(tr("fps —"));
        if (m_statusCommit)
            m_statusCommit->setText(QString());
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
        m_statusFrame->setText(tr("帧 %1 / 总帧 %2")
                                   .arg(m_document->playhead())
                                   .arg(m_playback ? m_playback->contentEnd() : 0));
    if (m_statusFps)
        m_statusFps->setText(tr("fps %1").arg(settings.fps));
    if (m_fpsLabel)
        m_fpsLabel->setText(tr("fps %1").arg(settings.fps));
    if (m_resolutionLabel)
        m_resolutionLabel->setText(
            tr("%1 × %2").arg(settings.width).arg(settings.height));
    if (m_statusCommit)
        m_statusCommit->setText(m_document->hasPendingCommit() ? tr("未提交")
                                                              : tr("已提交"));

    if (m_playheadSpin) {
        QSignalBlocker blocker(m_playheadSpin);
        m_playheadSpin->setValue(m_document->playhead());
    }
}

void NextWindow::chooseNewProject()
{
    if (mcpToolRunning()) {
        statusBar()->showMessage(tr("AI 正在操作工程，暂不能新建工程"), 5000);
        return;
    }

    NewProjectDialog dialog(m_lastBrowseDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString projectDir = dialog.projectDir();
    QString error;
    auto document = ProjectDocument::create(projectDir, dialog.settings(), &error,
                                            m_settings.gitEnabled);
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
        // An empty error means the human cancelled the crash-recovery dialog;
        // that is not a failure worth a warning box.
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("打开工程失败"), error);
        return;
    }

    m_lastBrowseDir = QFileInfo(dir).absolutePath();
}

CrashRecoveryChoice NextWindow::askCrashRecovery(
    const QVector<store::DirtyText>& dirty)
{
    if (m_crashRecoveryHandler)
        return m_crashRecoveryHandler(dirty);

    QStringList lines;
    const int shown = std::min<int>(dirty.size(), 20);
    for (int i = 0; i < shown; ++i)
        lines << QStringLiteral("%1（%2）")
                     .arg(dirty.at(i).path, dirty.at(i).status);
    if (dirty.size() > shown)
        lines << tr("...等 %1 个文件").arg(dirty.size());

    QMessageBox box(
        QMessageBox::Question, tr("发现未提交的文本改动"),
        tr("工程中有 %1 个未提交的文本文件，可能来自上次异常退出：\n\n"
           "%2\n\n请选择如何处理这些改动。")
            .arg(dirty.size())
            .arg(lines.join(QLatin1Char('\n'))),
        QMessageBox::NoButton, this);

    QPushButton* restore =
        box.addButton(tr("恢复并打开"), QMessageBox::AcceptRole);
    QPushButton* discard =
        box.addButton(tr("丢弃未提交文本"), QMessageBox::DestructiveRole);
    QPushButton* commit =
        box.addButton(tr("提交为恢复点"), QMessageBox::ActionRole);
    QPushButton* cancel = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(restore);
    box.setEscapeButton(cancel);
    box.exec();

    if (box.clickedButton() == restore)
        return CrashRecoveryChoice::Restore;
    if (box.clickedButton() == discard)
        return CrashRecoveryChoice::Discard;
    if (box.clickedButton() == commit)
        return CrashRecoveryChoice::Commit;
    return CrashRecoveryChoice::Cancel;
}

bool NextWindow::openProject(const QString& path, QString* error)
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (mcpToolRunning())
        return fail(tr("AI 正在操作工程，请稍后再打开工程"));
    if (m_exportRunning)
        return fail(tr("视频导出中，请稍后再打开工程"));

    const QFileInfo info(path);
    if (!info.exists()) {
        removeRecentProject(path);
        return fail(tr("路径不存在：%1").arg(path));
    }
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

    auto document = ProjectDocument::open(projectDir, error, m_settings.gitEnabled);
    if (!document)
        return false;

    QString recoveryStatus;
    const QVector<store::DirtyText> dirty = document->store()->dirtyText();
    if (!dirty.isEmpty())
    {
        const CrashRecoveryChoice choice = askCrashRecovery(dirty);

        // The modal dialog runs a nested event loop, so an MCP tool or an
        // export may have started in the meantime.  Never swap the document
        // under either of them.
        if (mcpToolRunning())
            return fail(tr("AI 正在操作工程，请稍后再打开工程"));
        if (m_exportRunning)
            return fail(tr("视频导出中，请稍后再打开工程"));

        QString recoveryError;
        switch (choice)
        {
        case CrashRecoveryChoice::Restore:
            recoveryStatus = tr("已恢复 %1 个未提交文件").arg(dirty.size());
            break;
        case CrashRecoveryChoice::Discard:
            if (!document->store()->discardDirtyText(&recoveryError)
                || !document->store()->reloadAfterDiscard(&recoveryError))
                return fail(tr("丢弃未提交文本失败：%1")
                                .arg(recoveryError.isEmpty()
                                         ? tr("未知错误")
                                         : recoveryError));
            recoveryStatus = tr("已丢弃 %1 个未提交文件").arg(dirty.size());
            break;
        case CrashRecoveryChoice::Commit:
            if (!document->store()->commitRecoveredText(&recoveryError))
                return fail(tr("提交未提交文本失败：%1")
                                .arg(recoveryError.isEmpty()
                                         ? tr("未知错误")
                                         : recoveryError));
            recoveryStatus =
                tr("已提交 %1 个未提交文件为恢复点").arg(dirty.size());
            break;
        case CrashRecoveryChoice::Cancel:
            // Not an error: chooseOpenProject() suppresses empty errors.
            return fail(QString());
        }
    }

    setDocument(std::move(document));
    // Shown after setDocument(): building the editor may enqueue video
    // extraction, which posts its own status message.
    if (!recoveryStatus.isEmpty())
        statusBar()->showMessage(recoveryStatus, 8000);
    return true;
}

void NextWindow::loadRecentProjects()
{
    QSettings store(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
    m_recentProjects =
        store.value(QStringLiteral("recent/projects")).toStringList();
}

void NextWindow::saveRecentProjects() const
{
    QSettings store(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
    store.setValue(QStringLiteral("recent/projects"), m_recentProjects);
    store.sync();
}

void NextWindow::pruneRecentProjects()
{
    QStringList kept;
    for (const QString& path : std::as_const(m_recentProjects)) {
        if (path.isEmpty() || kept.contains(path) || !QFileInfo::exists(path))
            continue;
        kept.append(path);
        if (kept.size() >= kMaxRecentProjects)
            break;
    }
    if (kept == m_recentProjects)
        return;
    m_recentProjects = kept;
    saveRecentProjects();
}

void NextWindow::recordRecentProject(const QString& path)
{
    if (path.isEmpty())
        return;

    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList updated = m_recentProjects;
    updated.removeAll(absolute);
    updated.prepend(absolute);
    while (updated.size() > kMaxRecentProjects)
        updated.removeLast();
    if (updated == m_recentProjects)
        return;

    m_recentProjects = updated;
    saveRecentProjects();
}

void NextWindow::removeRecentProject(const QString& path)
{
    if (path.isEmpty())
        return;
    if (m_recentProjects.removeAll(QFileInfo(path).absoluteFilePath()) > 0)
        saveRecentProjects();
}

void NextWindow::openRecentProject(const QString& path)
{
    if (path.isEmpty())
        return;

    QString error;
    if (!openProject(path, &error)) {
        // An empty error means the human cancelled the crash-recovery dialog;
        // that is not a failure worth a warning box.
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("打开工程失败"), error);
        if (!m_document)
            buildWelcome();
        return;
    }

    m_lastBrowseDir = QFileInfo(path).absolutePath();
}

QString NextWindow::recentProjectName(const QString& path) const
{
    QFile file(QDir(path).filePath(QStringLiteral("project.json")));
    if (file.open(QIODevice::ReadOnly)) {
        const QString name = QJsonDocument::fromJson(file.readAll())
                                 .object()
                                 .value(QStringLiteral("name"))
                                 .toString()
                                 .trimmed();
        if (!name.isEmpty())
            return name;
    }

    const QString dirName = QFileInfo(path).fileName();
    return dirName.isEmpty() ? QDir::toNativeSeparators(path) : dirName;
}

int NextWindow::recentProjectCountForTest() const
{
    return m_recentProjects.size();
}

QString NextWindow::recentProjectForTest(int index) const
{
    if (index < 0 || index >= m_recentProjects.size())
        return QString();
    return m_recentProjects.at(index);
}

void NextWindow::openRecentForTest(int index)
{
    openRecentProject(recentProjectForTest(index));
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

QVector<Uuid> NextWindow::collectDeletionSet(QString* error) const
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QVector<Uuid>();
    };

    if (!m_document)
        return fail(tr("没有打开工程"));

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return fail(tr("没有选中实体"));

    const auto& entities = m_document->project().entities;
    const auto it = entities.constFind(uuid);
    if (it == entities.constEnd())
        return fail(tr("选中的实体不存在"));
    if (it.value().locked)
        return fail(tr("实体已锁定，不能删除"));

    QVector<Uuid> uuids;
    QSet<Uuid> visited;
    collectSubtree(m_document->project(), uuid, visited, uuids);
    return uuids;
}

bool NextWindow::deleteEntitySet(const QVector<Uuid>& uuids, QString* error)
{
    if (!m_document || uuids.isEmpty()) {
        if (error)
            *error = tr("没有可删除的实体");
        return false;
    }

    QString message;
    if (uuids.size() == 1) {
        const auto it = m_document->project().entities.constFind(uuids.first());
        const QString name =
            it != m_document->project().entities.constEnd() ? it.value().name
                                                            : QString();
        message = QStringLiteral("pvf: delete entity \"%1\"").arg(name);
    } else {
        message = QStringLiteral("pvf: delete %1 entities").arg(uuids.size());
    }

    if (!m_document->deleteEntities(uuids, message, error))
        return false;

    m_document->setSelection(Uuid());
    refreshAll();
    return true;
}

void NextWindow::deleteSelectedEntity()
{
    QString error;
    const QVector<Uuid> uuids = collectDeletionSet(&error);
    if (uuids.isEmpty()) {
        showError(tr("删除实体失败"), error);
        return;
    }

    if (uuids.size() > 1) {
        const auto answer = QMessageBox::question(
            this, tr("删除实体"),
            tr("将级联删除 %1 个实体。确定吗？").arg(uuids.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    if (!deleteEntitySet(uuids, &error))
        showError(tr("删除实体失败"), error);
}

void NextWindow::showError(const QString& title, const QString& error)
{
    QMessageBox::critical(this, title,
                          error.isEmpty() ? tr("未知错误") : error);
}

} // namespace pvf::next
