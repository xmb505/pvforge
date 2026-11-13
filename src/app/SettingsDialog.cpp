#include "SettingsDialog.h"

#include "AppIcon.h"
#include "SettingsDialogSupport.h"
#include "core/Settings.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace pvf {

using namespace settings_detail;

SettingsDialog::SettingsDialog(QWidget* parent, McpServer* mcpServer)
    : QDialog(parent)
    , m_mcpServer(mcpServer)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(660, 460);
    resize(760, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setFixedWidth(158);
    m_categories->setFrameShape(QFrame::StyledPanel);
    m_categories->setIconSize(QSize(16, 16));
    m_categories->setUniformItemSizes(true);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    body->addWidget(m_categories);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("settingsPages"));
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(m_pages, 1);

    addPage(QStringLiteral("常规"), createGeneralPage(),
            {QStringLiteral("preferences-system"), QStringLiteral("settings")});
    addPage(QStringLiteral("编辑器"), createEditorPage(),
            {QStringLiteral("document-edit"), QStringLiteral("accessories-text-editor")});
    addPage(QStringLiteral("新项目"), createProjectPage(),
            {QStringLiteral("document-new")});
    addPage(QStringLiteral("时间轴"), createTimelinePage(),
            {QStringLiteral("view-media-playlist"), QStringLiteral("media-playback-start")});
    addPage(QStringLiteral("素材"), createAssetsPage(),
            {QStringLiteral("folder")});
    addPage(QStringLiteral("解码"), createDecodePage(),
            {QStringLiteral("video-x-generic"), QStringLiteral("media-playback-start")});
    addPage(QStringLiteral("MCP"), createMcpPage(),
            {QStringLiteral("network-server"), QStringLiteral("network-wired")});
    m_aboutIndex = addPage(QStringLiteral("关于"), createAboutPage(),
                           {QStringLiteral("help-about")});

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_pages->setCurrentIndex(row);
    });
    m_categories->setCurrentRow(0);

    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(buttons);
}

SettingsDialog::SettingsDialog(next::SettingsStore* store, QWidget* parent)
    : QDialog(parent)
    , m_appStore(store)
{
    buildAppShell();
}

void SettingsDialog::buildAppShell()
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(720, 520);
    resize(820, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setFixedWidth(158);
    m_categories->setFrameShape(QFrame::StyledPanel);
    m_categories->setIconSize(QSize(16, 16));
    m_categories->setUniformItemSizes(true);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    body->addWidget(m_categories);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("settingsPages"));
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(m_pages, 1);

    addPage(QStringLiteral("常规"), createAppGeneralPage(),
            {QStringLiteral("preferences-system"), QStringLiteral("settings")});
    addPage(QStringLiteral("编辑器"), createAppEditorPage(),
            {QStringLiteral("document-edit"), QStringLiteral("accessories-text-editor")});
    addPage(QStringLiteral("缓存"), createAppCachePage(),
            {QStringLiteral("folder"), QStringLiteral("drive-harddisk")});
    addPage(QStringLiteral("HTML"), createAppHtmlPage(),
            {QStringLiteral("text-html"), QStringLiteral("applications-internet")});
    addPage(QStringLiteral("MCP"), createAppMcpPage(),
            {QStringLiteral("network-server"), QStringLiteral("network-wired")});
    addPage(QStringLiteral("Git"), createAppGitPage(),
            {QStringLiteral("git"), QStringLiteral("vcs-branch")});
    m_aboutIndex = addPage(QStringLiteral("关于"), createAboutPage(),
                           {QStringLiteral("help-about")});

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_pages->setCurrentIndex(row);
    });
    m_categories->setCurrentRow(0);
    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    auto* apply = buttons->addButton(QStringLiteral("应用"),
                                     QDialogButtonBox::ApplyRole);
    apply->setObjectName(QStringLiteral("applyButton"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, [this] {
        saveAppSettings();
        emit settingsApplied();
    });
    root->addWidget(buttons);

    setAppSettings(m_appStore ? m_appStore->load() : next::AppSettings{});
}

QWidget* SettingsDialog::createAppGeneralPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* saving = new QGroupBox(QStringLiteral("保存"), page);
    auto* form = new QFormLayout(saving);
    m_appAutoSaveMinutes = new QSpinBox(saving);
    m_appAutoSaveMinutes->setObjectName(QStringLiteral("autoSaveMinutes"));
    m_appAutoSaveMinutes->setRange(1, 60);
    m_appAutoSaveMinutes->setSuffix(QStringLiteral(" 分钟"));
    m_appAutoSaveMinutes->setToolTip(
        QStringLiteral("有未提交编辑时，每隔这么久自动保存一次。"));
    form->addRow(QStringLiteral("自动保存间隔"), m_appAutoSaveMinutes);

    m_appCommitDelayMs = new QSpinBox(saving);
    m_appCommitDelayMs->setObjectName(QStringLiteral("humanCommitDelayMs"));
    m_appCommitDelayMs->setRange(200, 5000);
    m_appCommitDelayMs->setSingleStep(100);
    m_appCommitDelayMs->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("人类 commit debounce"), m_appCommitDelayMs);
    form->addRow(infoLabel(
        QStringLiteral("人类编辑停手后合并为一个 commit 的等待时间。"), saving));
    layout->addWidget(saving);

    layout->addWidget(infoLabel(
        QStringLiteral("自动保存只提交有未提交编辑的工程；播放头位置单独记在 "
                       "工程目录的 .pvforge/playhead.json，不进入 Git 提交。"),
        page));
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAppEditorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* snapping = new QGroupBox(QStringLiteral("吸附"), page);
    auto* form = new QFormLayout(snapping);
    m_appSnapPixels = new QSpinBox(snapping);
    m_appSnapPixels->setObjectName(QStringLiteral("snapPixels"));
    m_appSnapPixels->setRange(1, 32);
    m_appSnapPixels->setSuffix(QStringLiteral(" px"));
    m_appSnapPixels->setToolTip(
        QStringLiteral("画布中心线与实体边缘的吸附半径（屏幕像素）。"));
    form->addRow(QStringLiteral("吸附阈值"), m_appSnapPixels);

    m_appSnapCanvasCenter = new QCheckBox(
        QStringLiteral("允许实体边缘吸附画布中心线"), snapping);
    m_appSnapCanvasCenter->setObjectName(QStringLiteral("snapCanvasCenter"));
    form->addRow(m_appSnapCanvasCenter);
    m_appSnapEntityEdges =
        new QCheckBox(QStringLiteral("吸附其他实体边缘"), snapping);
    m_appSnapEntityEdges->setObjectName(QStringLiteral("snapEntityEdges"));
    form->addRow(m_appSnapEntityEdges);
    m_appSnapEdgeMidpoints =
        new QCheckBox(QStringLiteral("吸附其他实体边缘中点"), snapping);
    m_appSnapEdgeMidpoints->setObjectName(QStringLiteral("snapEdgeMidpoints"));
    form->addRow(m_appSnapEdgeMidpoints);

    auto* angles = new QGroupBox(QStringLiteral("旋转角度吸附"), snapping);
    auto* angleLayout = new QHBoxLayout(angles);
    angleLayout->setContentsMargins(8, 6, 8, 6);
    m_appSnapAngle0 = new QCheckBox(QStringLiteral("0°"), angles);
    m_appSnapAngle0->setObjectName(QStringLiteral("snapAngle0"));
    m_appSnapAngle30 = new QCheckBox(QStringLiteral("30°"), angles);
    m_appSnapAngle30->setObjectName(QStringLiteral("snapAngle30"));
    m_appSnapAngle45 = new QCheckBox(QStringLiteral("45°"), angles);
    m_appSnapAngle45->setObjectName(QStringLiteral("snapAngle45"));
    m_appSnapAngle90 = new QCheckBox(QStringLiteral("90°"), angles);
    m_appSnapAngle90->setObjectName(QStringLiteral("snapAngle90"));
    angleLayout->addWidget(m_appSnapAngle0);
    angleLayout->addWidget(m_appSnapAngle30);
    angleLayout->addWidget(m_appSnapAngle45);
    angleLayout->addWidget(m_appSnapAngle90);
    angleLayout->addStretch(1);
    form->addRow(angles);

    form->addRow(infoLabel(
        QStringLiteral("按住 Ctrl 拖动可临时关闭吸附。勾选 0°/90° 会同时覆盖 "
                       "180°/270°。"), snapping));
    layout->addWidget(snapping);
    layout->addStretch(1);
    return page;
}

next::AppSettings SettingsDialog::appSettings() const
{
    if (!m_appStore)
        return next::AppSettings{};
    next::AppSettings settings;
    settings.autoSaveMinutes = m_appAutoSaveMinutes->value();
    settings.humanCommitDelayMs = m_appCommitDelayMs->value();
    settings.snapPixels = m_appSnapPixels->value();
    settings.snapCanvasCenter = m_appSnapCanvasCenter->isChecked();
    settings.snapEntityEdges = m_appSnapEntityEdges->isChecked();
    settings.snapEdgeMidpoints = m_appSnapEdgeMidpoints->isChecked();
    settings.snapAngles.clear();
    if (m_appSnapAngle0->isChecked())
        settings.snapAngles << 0.0 << 180.0;
    if (m_appSnapAngle30->isChecked())
        settings.snapAngles << 30.0;
    if (m_appSnapAngle45->isChecked())
        settings.snapAngles << 45.0;
    if (m_appSnapAngle90->isChecked())
        settings.snapAngles << 90.0 << 270.0;

    settings.htmlCacheEnabled = m_appHtmlCacheEnabled->isChecked();
    settings.htmlMemoryBytes = qint64(m_appHtmlMemoryMb->value()) * 1024 * 1024;
    settings.htmlDiskBytes = qint64(m_appHtmlDiskGb->value()) * 1024 * 1024 * 1024;
    settings.htmlScale = m_appHtmlScale->currentData().toDouble();
    settings.htmlMaxWidth = m_appHtmlMaxWidth->value();
    settings.watchdogMs = m_appWatchdogMs->value();
    settings.chromeExecutable = m_appChromeExecutable->text().trimmed();

    settings.videoProxyEnabled = m_appVideoProxyEnabled->isChecked();
    settings.videoProxyWidth = m_appVideoProxyWidth->currentData().toInt();
    settings.videoMemoryFrames = m_appVideoMemoryFrames->currentData().toInt();

    settings.workbenchCacheEnabled = m_appWorkbenchEnabled->isChecked();
    settings.workbenchCacheScalePercent = m_appWorkbenchScale->currentData().toInt();
    settings.workbenchCacheExpandPercent = m_appWorkbenchExpand->value();
    settings.workbenchCacheDiskBytes =
        qint64(m_appWorkbenchDiskGb->value()) * 1024 * 1024 * 1024;
    settings.workbenchCacheMemoryBytes =
        qint64(m_appWorkbenchMemoryMb->value()) * 1024 * 1024;
    settings.workbenchCacheWriterThreads =
        m_appWorkbenchWriterThreads->currentData().toInt();

    settings.mcpPort = static_cast<quint16>(m_appMcpPort->value());
    settings.gitEnabled = m_appGitEnabled->isChecked();
    settings.onboardingDone = m_appOnboardingDone;
    return next::SettingsStore::sanitized(settings);
}

void SettingsDialog::setAppSettings(const next::AppSettings& settings)
{
    if (!m_appStore)
        return;
    const next::AppSettings value = next::SettingsStore::sanitized(settings);
    m_appAutoSaveMinutes->setValue(value.autoSaveMinutes);
    m_appCommitDelayMs->setValue(value.humanCommitDelayMs);
    m_appSnapPixels->setValue(value.snapPixels);
    m_appSnapCanvasCenter->setChecked(value.snapCanvasCenter);
    m_appSnapEntityEdges->setChecked(value.snapEntityEdges);
    m_appSnapEdgeMidpoints->setChecked(value.snapEdgeMidpoints);
    const auto hasAngle = [&value](double angle) {
        return value.snapAngles.contains(angle);
    };
    m_appSnapAngle0->setChecked(hasAngle(0.0) || hasAngle(180.0));
    m_appSnapAngle30->setChecked(hasAngle(30.0));
    m_appSnapAngle45->setChecked(hasAngle(45.0));
    m_appSnapAngle90->setChecked(hasAngle(90.0) || hasAngle(270.0));

    m_appHtmlCacheEnabled->setChecked(value.htmlCacheEnabled);
    m_appHtmlMemoryMb->setValue(int(value.htmlMemoryBytes / (1024 * 1024)));
    m_appHtmlDiskGb->setValue(int(value.htmlDiskBytes / (1024 * 1024 * 1024)));
    const int htmlScale = m_appHtmlScale->findData(value.htmlScale);
    if (htmlScale >= 0)
        m_appHtmlScale->setCurrentIndex(htmlScale);
    m_appHtmlMaxWidth->setValue(value.htmlMaxWidth);
    m_appWatchdogMs->setValue(value.watchdogMs);
    m_appChromeExecutable->setText(value.chromeExecutable);

    m_appVideoProxyEnabled->setChecked(value.videoProxyEnabled);
    const int proxyWidth = m_appVideoProxyWidth->findData(value.videoProxyWidth);
    if (proxyWidth >= 0)
        m_appVideoProxyWidth->setCurrentIndex(proxyWidth);
    const int memoryFrames = m_appVideoMemoryFrames->findData(value.videoMemoryFrames);
    if (memoryFrames >= 0)
        m_appVideoMemoryFrames->setCurrentIndex(memoryFrames);

    m_appWorkbenchEnabled->setChecked(value.workbenchCacheEnabled);
    const int scale = m_appWorkbenchScale->findData(value.workbenchCacheScalePercent);
    if (scale >= 0)
        m_appWorkbenchScale->setCurrentIndex(scale);
    m_appWorkbenchExpand->setValue(value.workbenchCacheExpandPercent);
    m_appWorkbenchDiskGb->setValue(
        int(value.workbenchCacheDiskBytes / (1024 * 1024 * 1024)));
    m_appWorkbenchMemoryMb->setValue(
        int(value.workbenchCacheMemoryBytes / (1024 * 1024)));
    const int writerThreads =
        m_appWorkbenchWriterThreads->findData(value.workbenchCacheWriterThreads);
    if (writerThreads >= 0)
        m_appWorkbenchWriterThreads->setCurrentIndex(writerThreads);

    m_appMcpPort->setValue(value.mcpPort);
    m_appGitEnabled->setChecked(value.gitEnabled);
    m_appOnboardingDone = value.onboardingDone;
}

void SettingsDialog::saveAppSettings()
{
    if (!m_appStore)
        return;
    m_appStore->save(appSettings());
}

void SettingsDialog::setMcpStatus(bool running, quint16 port,
                                  const QString& token, const QString& error,
                                  bool writesEnabled)
{
    if (!m_appMcpStatus)
        return;
    if (running)
    {
        m_appMcpStatus->setText(
            QStringLiteral("监听中 · 127.0.0.1:%1 · %2")
                .arg(port)
                .arg(writesEnabled ? QStringLiteral("可写")
                                   : QStringLiteral("只读（Git 未启用）")));
        m_appMcpToken->setText(token);
    }
    else
    {
        m_appMcpStatus->setText(
            error.isEmpty() ? QStringLiteral("未启动")
                            : QStringLiteral("启动失败：%1").arg(error));
        m_appMcpToken->clear();
    }
}

void SettingsDialog::setGitInfo(const QString& repositoryPath,
                                const QString& recentCommit)
{
    m_appGitRepositoryValue = repositoryPath;
    m_appGitRecentValue = recentCommit;
    if (!m_appGitRepository || !m_appGitRecent)
        return;
    m_appGitRepository->setText(repositoryPath.isEmpty()
                                    ? QStringLiteral("（未打开工程）")
                                    : repositoryPath);
    m_appGitRecent->setText(recentCommit.isEmpty() ? QStringLiteral("—")
                                                   : recentCommit);
}

void SettingsDialog::setWorkbenchCacheStats(
    bool enabled, const framecache::CacheUsage& usage,
    const QString& directory)
{
    if (!m_appWorkbenchCacheStats)
        return;
    if (!enabled)
    {
        m_appWorkbenchCacheStats->setText(
            QStringLiteral("未启用（不会生成新的缓存文件）"));
        return;
    }
    const QString where = directory.isEmpty()
                              ? QStringLiteral("（尚未写入）")
                              : directory;
    m_appWorkbenchCacheStats->setText(
        QStringLiteral("帧数 %1 · 帧 %2 MB · 索引 %3 MB · %4")
            .arg(usage.frameCount)
            .arg(double(usage.frameBytes) / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(double(usage.indexBytes) / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(where));
}

void SettingsDialog::setHtmlCacheStats(const framecache::CacheUsage& usage,
                                       const QString& directory)
{
    if (!m_appHtmlCacheStats)
        return;
    const QString where = directory.isEmpty()
                              ? QStringLiteral("（尚未写入）")
                              : directory;
    m_appHtmlCacheStats->setText(
        QStringLiteral("文件 %1 · 帧 %2 MB · 索引 %3 MB · %4")
            .arg(usage.fileCount)
            .arg(double(usage.frameBytes) / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(double(usage.indexBytes) / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(where));
}

void SettingsDialog::setEndpointPath(const QString& path)
{
    if (m_appMcpEndpoint)
        m_appMcpEndpoint->setText(path);
}

QString SettingsDialog::endpointPath() const
{
    return m_appMcpEndpoint ? m_appMcpEndpoint->text()
                            : defaultEndpointPath();
}

QString SettingsDialog::defaultEndpointPath()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/PVForge/mcp-endpoint.json");
}

int SettingsDialog::addPage(const QString& title, QWidget* page,
                            const QStringList& iconNames)
{
    auto* item = new QListWidgetItem(themeIcon(iconNames), title, m_categories);
    item->setSizeHint(QSize(0, 30));
    m_pages->addWidget(page);
    return m_categories->row(item);
}

void SettingsDialog::showAboutPage()
{
    m_categories->setCurrentRow(m_aboutIndex);
}

QWidget* SettingsDialog::createAboutPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* header = new QWidget(page);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* logo = new QLabel(header);
    logo->setPixmap(makeCubeIcon().pixmap(48, 48));
    logo->setFixedSize(48, 48);
    headerLayout->addWidget(logo);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("PVForge"), header);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titleLayout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("2D 音乐 PV 编辑器 · MIT License"), header);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout, 1);
    layout->addWidget(header);

    m_aboutText = new QPlainTextEdit(page);
    m_aboutText->setObjectName(QStringLiteral("aboutText"));
    m_aboutText->setReadOnly(true);
    m_aboutText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_aboutText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_aboutText->setPlainText(aboutText());
    layout->addWidget(m_aboutText, 1);

    auto* footer = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("复制信息"), page);
    copy->setObjectName(QStringLiteral("aboutCopy"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_aboutText->toPlainText());
    });
    footer->addWidget(copy);
    footer->addStretch(1);
    layout->addLayout(footer);

    return page;
}

void SettingsDialog::accept()
{
    if (m_appStore)
    {
        saveAppSettings();
        emit settingsApplied();
        QDialog::accept();
        return;
    }

    Settings::setLanguage(m_language->currentData().toString());
    Settings::setSnapPixels(m_snapPixels->value());
    Settings::setEdgeCenterSnap(m_edgeCenterSnap->isChecked());
    Settings::setEdgeMidpointSnap(m_edgeMidpointSnap->isChecked());
    QList<int> angles;
    if (m_angle0->isChecked())
        angles.append(0);
    if (m_angle90->isChecked())
        angles.append(90);
    if (m_angle180->isChecked())
        angles.append(180);
    if (m_angle270->isChecked())
        angles.append(270);
    Settings::setSnapAngles(angles);
    Settings::setAutoKeyDefault(m_autoKey->isChecked());
    Settings::setAudioScrub(m_audioScrub->isChecked());
    Settings::setAutosaveEnabled(m_autosave->isChecked());
    Settings::setAutosaveMinutes(m_autosaveMinutes->value());
    Settings::setDefaultSize(m_width->value(), m_height->value(), m_fps->value());
    Settings::setTimelinePixelsPerFrame(m_timelineZoom->value());
    Settings::setPackAssetsDefault(m_packAssets->isChecked());
    Settings::setMcpPortAuto(m_mcpAutoPort->isChecked());
    Settings::setMcpPort(m_mcpPort->value());
    Settings::setMcpLogVisible(m_mcpLogEnabled->isChecked());
    QDialog::accept();
}

} // namespace pvf
