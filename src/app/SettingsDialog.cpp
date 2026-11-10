#include "SettingsDialog.h"

#include "AppIcon.h"
#include "SystemInfo.h"
#include "core/Settings.h"
#include "media/DecoderInfo.h"
#include "mcp/McpServer.h"
#include "mcp/StdioBridge.h"
#include "next/settings/GitDetect.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace pvf {

namespace {

#ifndef PVFORGE_MCP_BINARY
#define PVFORGE_MCP_BINARY "pvforge-mcp"
#endif

QIcon themeIcon(const QStringList& names)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

QLabel* infoLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
    return label;
}

QString aboutText()
{
    QString text = collectSystemInfo().toText();
    text += QStringLiteral("\n\n【解码】\n") + decoderInfo().toText();
    text += QStringLiteral(
        "\n\n【项目】\n"
        "主页：https://github.com/xmb505/pvforge\n"
        "许可证：MIT");
    return text;
}

} // namespace

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
    addPage(QStringLiteral("HTML"), createAppHtmlPage(),
            {QStringLiteral("text-html"), QStringLiteral("applications-internet")});
    addPage(QStringLiteral("视频"), createAppVideoPage(),
            {QStringLiteral("video-x-generic"), QStringLiteral("media-playback-start")});
    addPage(QStringLiteral("工作台"), createAppWorkbenchPage(),
            {QStringLiteral("view-grid"), QStringLiteral("applications-graphics")});
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

QWidget* SettingsDialog::createAppHtmlPage()
{
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* cache = new QGroupBox(QStringLiteral("HTML 帧缓存"), content);
    auto* form = new QFormLayout(cache);
    m_appHtmlCacheEnabled = new QCheckBox(
        QStringLiteral("启用 HTML 帧缓存（内存 + 磁盘 LRU）"), cache);
    m_appHtmlCacheEnabled->setObjectName(QStringLiteral("htmlCacheEnabled"));
    form->addRow(m_appHtmlCacheEnabled);
    m_appHtmlMemoryMb = new QSpinBox(cache);
    m_appHtmlMemoryMb->setObjectName(QStringLiteral("htmlMemoryMb"));
    m_appHtmlMemoryMb->setRange(32, 4096);
    m_appHtmlMemoryMb->setSuffix(QStringLiteral(" MB"));
    form->addRow(QStringLiteral("内存上限"), m_appHtmlMemoryMb);
    m_appHtmlDiskGb = new QSpinBox(cache);
    m_appHtmlDiskGb->setObjectName(QStringLiteral("htmlDiskGb"));
    m_appHtmlDiskGb->setRange(1, 64);
    m_appHtmlDiskGb->setSuffix(QStringLiteral(" GB"));
    form->addRow(QStringLiteral("磁盘上限"), m_appHtmlDiskGb);
    m_appHtmlScale = new QComboBox(cache);
    m_appHtmlScale->setObjectName(QStringLiteral("htmlScale"));
    m_appHtmlScale->addItem(QStringLiteral("25%"), 0.25);
    m_appHtmlScale->addItem(QStringLiteral("50%"), 0.5);
    m_appHtmlScale->addItem(QStringLiteral("75%"), 0.75);
    m_appHtmlScale->addItem(QStringLiteral("100%"), 1.0);
    form->addRow(QStringLiteral("清晰度（工程分辨率比例）"), m_appHtmlScale);
    m_appHtmlMaxWidth = new QSpinBox(cache);
    m_appHtmlMaxWidth->setObjectName(QStringLiteral("htmlMaxWidth"));
    m_appHtmlMaxWidth->setRange(320, 4096);
    m_appHtmlMaxWidth->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("最大宽度"), m_appHtmlMaxWidth);
    m_appWatchdogMs = new QSpinBox(cache);
    m_appWatchdogMs->setObjectName(QStringLiteral("watchdogMs"));
    m_appWatchdogMs->setRange(500, 30000);
    m_appWatchdogMs->setSingleStep(250);
    m_appWatchdogMs->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("帧看门狗超时"), m_appWatchdogMs);
    layout->addWidget(cache);

    auto* chrome = new QGroupBox(QStringLiteral("Chrome / Chromium"), content);
    auto* chromeForm = new QFormLayout(chrome);
    auto* chromeRow = new QWidget(chrome);
    auto* chromeLayout = new QHBoxLayout(chromeRow);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    m_appChromeExecutable = new QLineEdit(chromeRow);
    m_appChromeExecutable->setObjectName(QStringLiteral("chromeExecutable"));
    m_appChromeExecutable->setPlaceholderText(QStringLiteral("留空自动检测"));
    m_appChromeBrowse = new QPushButton(QStringLiteral("浏览…"), chromeRow);
    m_appChromeBrowse->setObjectName(QStringLiteral("chromeBrowse"));
    m_appChromeDetect = new QPushButton(QStringLiteral("自动检测"), chromeRow);
    m_appChromeDetect->setObjectName(QStringLiteral("chromeDetect"));
    chromeLayout->addWidget(m_appChromeExecutable, 1);
    chromeLayout->addWidget(m_appChromeBrowse);
    chromeLayout->addWidget(m_appChromeDetect);
    chromeForm->addRow(QStringLiteral("可执行文件"), chromeRow);
    m_appClearHtmlCache = new QPushButton(QStringLiteral("清空 HTML 缓存"), chrome);
    m_appClearHtmlCache->setObjectName(QStringLiteral("clearHtmlCache"));
    chromeForm->addRow(QStringLiteral("缓存维护"), m_appClearHtmlCache);
    layout->addWidget(chrome);

    connect(m_appChromeBrowse, &QPushButton::clicked, this, [this] {
        QString start = m_appChromeExecutable->text().trimmed();
        if (start.isEmpty())
            start = QDir::homePath();
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 Chrome/Chromium 可执行文件"), start);
        if (!path.isEmpty())
            m_appChromeExecutable->setText(path);
    });
    connect(m_appChromeDetect, &QPushButton::clicked, this,
            &SettingsDialog::chromeDetectRequested);
    connect(m_appClearHtmlCache, &QPushButton::clicked, this,
            &SettingsDialog::clearHtmlCacheRequested);

    layout->addWidget(infoLabel(
        QStringLiteral("关闭缓存后只使用前端浏览器实时渲染；导出仍会启动成品浏览器。"),
        content));
    layout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return page;
}

QWidget* SettingsDialog::createAppVideoPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* video = new QGroupBox(QStringLiteral("视频预览"), page);
    auto* form = new QFormLayout(video);
    m_appVideoProxyEnabled =
        new QCheckBox(QStringLiteral("启用视频播放代理"), video);
    m_appVideoProxyEnabled->setObjectName(QStringLiteral("videoProxyEnabled"));
    form->addRow(m_appVideoProxyEnabled);
    m_appVideoProxyWidth = new QComboBox(video);
    m_appVideoProxyWidth->setObjectName(QStringLiteral("videoProxyWidth"));
    m_appVideoProxyWidth->addItem(QStringLiteral("960 px"), 960);
    m_appVideoProxyWidth->addItem(QStringLiteral("1280 px"), 1280);
    form->addRow(QStringLiteral("代理最大宽度"), m_appVideoProxyWidth);
    m_appVideoMemoryFrames = new QComboBox(video);
    m_appVideoMemoryFrames->setObjectName(QStringLiteral("videoMemoryFrames"));
    m_appVideoMemoryFrames->addItem(QStringLiteral("32 帧"), 32);
    m_appVideoMemoryFrames->addItem(QStringLiteral("64 帧"), 64);
    m_appVideoMemoryFrames->addItem(QStringLiteral("128 帧"), 128);
    form->addRow(QStringLiteral("内存帧数"), m_appVideoMemoryFrames);
    form->addRow(infoLabel(
        QStringLiteral("代理用于时间轴播放；导出始终解码原片。"), video));
    layout->addWidget(video);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAppWorkbenchPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* cache = new QGroupBox(QStringLiteral("工作台缓存"), page);
    auto* form = new QFormLayout(cache);
    m_appWorkbenchEnabled = new QCheckBox(
        QStringLiteral("启用工作台缓存（整帧预渲染）"), cache);
    m_appWorkbenchEnabled->setObjectName(QStringLiteral("workbenchCacheEnabled"));
    form->addRow(m_appWorkbenchEnabled);
    m_appWorkbenchScale = new QComboBox(cache);
    m_appWorkbenchScale->setObjectName(QStringLiteral("workbenchScale"));
    m_appWorkbenchScale->addItem(QStringLiteral("25%"), 25);
    m_appWorkbenchScale->addItem(QStringLiteral("50%"), 50);
    m_appWorkbenchScale->addItem(QStringLiteral("75%"), 75);
    m_appWorkbenchScale->addItem(QStringLiteral("100%"), 100);
    form->addRow(QStringLiteral("清晰度（工程分辨率比例）"), m_appWorkbenchScale);
    m_appWorkbenchExpand = new QSpinBox(cache);
    m_appWorkbenchExpand->setObjectName(QStringLiteral("workbenchExpandPercent"));
    m_appWorkbenchExpand->setRange(0, 200);
    m_appWorkbenchExpand->setSingleStep(5);
    m_appWorkbenchExpand->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("工作台缓存向外扩展"), m_appWorkbenchExpand);
    m_appWorkbenchDiskGb = new QSpinBox(cache);
    m_appWorkbenchDiskGb->setObjectName(QStringLiteral("workbenchDiskGb"));
    m_appWorkbenchDiskGb->setRange(1, 64);
    m_appWorkbenchDiskGb->setSuffix(QStringLiteral(" GB"));
    form->addRow(QStringLiteral("磁盘上限"), m_appWorkbenchDiskGb);
    form->addRow(infoLabel(
        QStringLiteral("缓存世界矩形在画布四周各扩展宽高的该比例；只影响编辑器"
                       "画布视图，相机视图和导出不扩展。"), cache));
    layout->addWidget(cache);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAppMcpPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* service = new QGroupBox(QStringLiteral("MCP 服务"), page);
    auto* form = new QFormLayout(service);
    m_appMcpPort = new QSpinBox(service);
    m_appMcpPort->setObjectName(QStringLiteral("mcpPort"));
    m_appMcpPort->setRange(1024, 65535);
    m_appMcpPort->setGroupSeparatorShown(false);
    form->addRow(QStringLiteral("监听端口"), m_appMcpPort);
    m_appMcpStatus = infoLabel(QStringLiteral("未启动"), service);
    m_appMcpStatus->setObjectName(QStringLiteral("mcpStatus"));
    form->addRow(QStringLiteral("状态"), m_appMcpStatus);
    m_appMcpToken = new QLineEdit(service);
    m_appMcpToken->setObjectName(QStringLiteral("mcpToken"));
    m_appMcpToken->setReadOnly(true);
    m_appMcpToken->setPlaceholderText(QStringLiteral("服务器未启动"));
    form->addRow(QStringLiteral("Bearer token"), m_appMcpToken);
    m_appResetMcpToken = new QPushButton(QStringLiteral("重置 token"), service);
    m_appResetMcpToken->setObjectName(QStringLiteral("resetMcpToken"));
    connect(m_appResetMcpToken, &QPushButton::clicked, this,
            &SettingsDialog::resetMcpTokenRequested);
    form->addRow(QStringLiteral("访问凭据"), m_appResetMcpToken);
    m_appMcpEndpoint = new QLineEdit(service);
    m_appMcpEndpoint->setObjectName(QStringLiteral("mcpEndpointPath"));
    m_appMcpEndpoint->setReadOnly(true);
    m_appMcpEndpoint->setText(defaultEndpointPath());
    form->addRow(QStringLiteral("Endpoint 文件"), m_appMcpEndpoint);
    form->addRow(infoLabel(
        QStringLiteral("只监听 127.0.0.1；token 保存在 endpoint 文件中。"
                       "Git 未启用时写工具返回 permission_denied。"), service));
    layout->addWidget(service);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAppGitPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* git = new QGroupBox(QStringLiteral("Git"), page);
    auto* form = new QFormLayout(git);
    m_appGitEnabled = new QCheckBox(
        QStringLiteral("启用 Git（历史、崩溃恢复、AI 写入）"), git);
    m_appGitEnabled->setObjectName(QStringLiteral("gitEnabled"));
    form->addRow(m_appGitEnabled);
    m_appGitStatus = infoLabel(QString(), git);
    m_appGitStatus->setObjectName(QStringLiteral("gitStatus"));
    form->addRow(QStringLiteral("检测状态"), m_appGitStatus);
    m_appGitDetect = new QPushButton(QStringLiteral("重新检测"), git);
    m_appGitDetect->setObjectName(QStringLiteral("gitDetect"));
    connect(m_appGitDetect, &QPushButton::clicked, this,
            &SettingsDialog::refreshAppGitStatus);
    form->addRow(QStringLiteral("检测"), m_appGitDetect);
    form->addRow(infoLabel(
        QStringLiteral("关闭时只保留本次运行的内存撤销；重启后无法撤销。"
                       "没有 Git 时 MCP 只读。"), git));
    layout->addWidget(git);

    auto* repo = new QGroupBox(QStringLiteral("当前工程"), page);
    auto* repoForm = new QFormLayout(repo);
    m_appGitRepository = infoLabel(QStringLiteral("（未打开工程）"), repo);
    m_appGitRepository->setObjectName(QStringLiteral("gitRepositoryPath"));
    repoForm->addRow(QStringLiteral("仓库路径"), m_appGitRepository);
    m_appGitRecent = infoLabel(QStringLiteral("—"), repo);
    m_appGitRecent->setObjectName(QStringLiteral("gitRecentCommit"));
    repoForm->addRow(QStringLiteral("最近提交"), m_appGitRecent);
    repoForm->addRow(infoLabel(
        QStringLiteral("Git 只管理文本工程文件（project.json、entities/*.json、"
                       "markers）；assets/ 与 .cache/ 不进提交。"), repo));
    layout->addWidget(repo);

    refreshAppGitStatus();
    layout->addStretch(1);
    return page;
}

void SettingsDialog::refreshAppGitStatus()
{
    if (!m_appGitStatus)
        return;
    const next::GitInfo info = next::detectGit();
    m_appGitStatus->setText(
        info.found
            ? QStringLiteral("已找到：%1（%2）").arg(info.executable, info.version)
            : QStringLiteral("未找到 Git：%1").arg(info.error));
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
    settings.workbenchCacheMemoryBytes = m_appWorkbenchMemoryBytes;

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
    m_appWorkbenchMemoryBytes = value.workbenchCacheMemoryBytes;

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

QWidget* SettingsDialog::createGeneralPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* interface = new QGroupBox(QStringLiteral("界面"), page);
    auto* form = new QFormLayout(interface);
    m_language = new QComboBox(interface);
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    const int index = m_language->findData(Settings::language());
    m_language->setCurrentIndex(index >= 0 ? index : 0);
    form->addRow(QStringLiteral("界面语言"), m_language);
    form->addRow(infoLabel(QStringLiteral("语言切换后需要重新启动 PVForge。"), interface));

    layout->addWidget(interface);

    auto* saving = new QGroupBox(QStringLiteral("保存"), page);
    auto* savingForm = new QFormLayout(saving);
    m_autosave = new QCheckBox(QStringLiteral("定期自动保存工程"), saving);
    m_autosave->setObjectName(QStringLiteral("autosaveEnabled"));
    m_autosave->setChecked(Settings::autosaveEnabled());
    savingForm->addRow(m_autosave);
    m_autosaveMinutes = new QSpinBox(saving);
    m_autosaveMinutes->setObjectName(QStringLiteral("autosaveMinutes"));
    m_autosaveMinutes->setRange(1, 120);
    m_autosaveMinutes->setSuffix(QStringLiteral(" 分钟"));
    m_autosaveMinutes->setValue(Settings::autosaveMinutes());
    m_autosaveMinutes->setEnabled(m_autosave->isChecked());
    connect(m_autosave, &QCheckBox::toggled, m_autosaveMinutes,
            &QWidget::setEnabled);
    savingForm->addRow(QStringLiteral("间隔"), m_autosaveMinutes);
    savingForm->addRow(infoLabel(
        QStringLiteral("只自动保存已经保存过路径的工程；未保存的新工程请先手动保存一次。"),
        saving));
    layout->addWidget(saving);

    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createEditorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* snapping = new QGroupBox(QStringLiteral("吸附"), page);
    auto* snapForm = new QFormLayout(snapping);
    m_snapPixels = new QSpinBox(snapping);
    m_snapPixels->setRange(0, 40);
    m_snapPixels->setSuffix(QStringLiteral(" px"));
    m_snapPixels->setValue(Settings::snapPixels());
    m_snapPixels->setToolTip(QStringLiteral("0 表示关闭吸附"));
    snapForm->addRow(QStringLiteral("吸附距离"), m_snapPixels);
    m_edgeCenterSnap = new QCheckBox(QStringLiteral("实体边缘吸附到画面中心线"), snapping);
    m_edgeCenterSnap->setChecked(Settings::edgeCenterSnap());
    snapForm->addRow(m_edgeCenterSnap);
    m_edgeMidpointSnap =
        new QCheckBox(QStringLiteral("吸附其他实体的边缘中点"), snapping);
    m_edgeMidpointSnap->setChecked(Settings::edgeMidpointSnap());
    snapForm->addRow(m_edgeMidpointSnap);
    layout->addWidget(snapping);

    auto* rotation = new QGroupBox(QStringLiteral("旋转"), page);
    auto* rotationForm = new QFormLayout(rotation);
    auto* angleRow = new QWidget(rotation);
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(8);
    const QList<int> angles = Settings::snapAngles();
    m_angle0 = new QCheckBox(QStringLiteral("0°"), angleRow);
    m_angle90 = new QCheckBox(QStringLiteral("90°"), angleRow);
    m_angle180 = new QCheckBox(QStringLiteral("180°"), angleRow);
    m_angle270 = new QCheckBox(QStringLiteral("270°"), angleRow);
    m_angle0->setChecked(angles.contains(0));
    m_angle90->setChecked(angles.contains(90));
    m_angle180->setChecked(angles.contains(180));
    m_angle270->setChecked(angles.contains(270));
    angleLayout->addWidget(m_angle0);
    angleLayout->addWidget(m_angle90);
    angleLayout->addWidget(m_angle180);
    angleLayout->addWidget(m_angle270);
    angleLayout->addStretch(1);
    rotationForm->addRow(QStringLiteral("吸附角度"), angleRow);
    layout->addWidget(rotation);

    auto* behaviour = new QGroupBox(QStringLiteral("关键帧与音频"), page);
    auto* behaviourForm = new QFormLayout(behaviour);
    m_autoKey = new QCheckBox(QStringLiteral("新项目默认开启自动关键帧"), behaviour);
    m_autoKey->setChecked(Settings::autoKeyDefault());
    behaviourForm->addRow(m_autoKey);
    m_audioScrub = new QCheckBox(QStringLiteral("拖动/逐帧时播放音频（音频擦洗）"), behaviour);
    m_audioScrub->setChecked(Settings::audioScrub());
    behaviourForm->addRow(m_audioScrub);
    layout->addWidget(behaviour);

    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createProjectPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* project = new QGroupBox(QStringLiteral("新项目默认值"), page);
    auto* form = new QFormLayout(project);
    m_width = new QSpinBox(project);
    m_width->setRange(16, 8192);
    m_width->setSuffix(QStringLiteral(" px"));
    m_width->setValue(Settings::defaultWidth());
    form->addRow(QStringLiteral("默认宽度"), m_width);
    m_height = new QSpinBox(project);
    m_height->setRange(16, 8192);
    m_height->setSuffix(QStringLiteral(" px"));
    m_height->setValue(Settings::defaultHeight());
    form->addRow(QStringLiteral("默认高度"), m_height);
    m_fps = new QSpinBox(project);
    m_fps->setRange(1, 240);
    m_fps->setSuffix(QStringLiteral(" fps"));
    m_fps->setValue(Settings::defaultFps());
    form->addRow(QStringLiteral("默认帧率"), m_fps);

    layout->addWidget(project);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createTimelinePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* timeline = new QGroupBox(QStringLiteral("时间轴"), page);
    auto* form = new QFormLayout(timeline);
    m_timelineZoom = new QDoubleSpinBox(timeline);
    m_timelineZoom->setRange(0.5, 32.0);
    m_timelineZoom->setDecimals(1);
    m_timelineZoom->setSuffix(QStringLiteral(" px/帧"));
    m_timelineZoom->setValue(Settings::timelinePixelsPerFrame());
    form->addRow(QStringLiteral("默认缩放"), m_timelineZoom);
    form->addRow(infoLabel(
        QStringLiteral("时间轴左上角的「秒 / 帧」按钮可以随时切换刻度单位。"), timeline));

    layout->addWidget(timeline);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAssetsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* assets = new QGroupBox(QStringLiteral("素材"), page);
    auto* form = new QFormLayout(assets);
    m_packAssets = new QCheckBox(QStringLiteral("新项目默认开启「全部打包到项目」"), assets);
    m_packAssets->setChecked(Settings::packAssetsDefault());
    form->addRow(m_packAssets);

    layout->addWidget(assets);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createDecodePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* decode = new QGroupBox(QStringLiteral("当前解码路径"), page);
    auto* decodeLayout = new QVBoxLayout(decode);
    decodeLayout->setSpacing(10);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    const DecoderInfo info = decoderInfo();
    int row = 0;
    auto addDecoderRow = [decode, grid, &row](const QString& title,
                                              const QString& value,
                                              const QString& objectName) {
        auto* name = new QLabel(title, decode);
        name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        name->setMinimumWidth(64);
        QLabel* field = infoLabel(value, decode);
        field->setObjectName(objectName);
        field->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(name, row, 0);
        grid->addWidget(field, row, 1);
        ++row;
    };
    addDecoderRow(QStringLiteral("解码方式"), info.mode,
                  QStringLiteral("decoderModeValue"));
    addDecoderRow(QStringLiteral("视频解码"), info.video,
                  QStringLiteral("decoderVideoValue"));
    addDecoderRow(QStringLiteral("音频解码"), info.audioDecode,
                  QStringLiteral("decoderAudioValue"));
    addDecoderRow(QStringLiteral("音频播放"), info.audioOutput,
                  QStringLiteral("decoderOutputValue"));
    addDecoderRow(QStringLiteral("硬件加速"), info.hardware,
                  QStringLiteral("decoderHardwareValue"));
    decodeLayout->addLayout(grid);

    decodeLayout->addWidget(infoLabel(
        QStringLiteral("这里是只读信息，显示当前构建实际使用的路径。"
                       "视频播放接入后再提供可切换的硬件解码选项。"),
        decode));

    auto* copy = new QPushButton(QStringLiteral("复制解码信息"), decode);
    copy->setObjectName(QStringLiteral("copyDecoderInfo"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(decoderInfo().toText());
    });
    decodeLayout->addWidget(copy);

    layout->addWidget(decode);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createMcpPage()
{
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("mcpSettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* service = new QGroupBox(QStringLiteral("MCP 服务"), content);
    auto* serviceForm = new QFormLayout(service);
    m_mcpEnabled = new QCheckBox(
        QStringLiteral("本次启动启用 MCP（重启后不会自动开启）"), service);
    m_mcpEnabled->setObjectName(QStringLiteral("mcpEnableCheck"));
    m_mcpEnabled->setToolTip(
        QStringLiteral("只有在这里手动启用后服务器才会监听；关闭设置页不会自动启动。"));
    connect(m_mcpEnabled, &QCheckBox::toggled, this,
            &SettingsDialog::applyMcpEnabled);
    serviceForm->addRow(m_mcpEnabled);

    auto* portRow = new QWidget(service);
    auto* portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(8);
    m_mcpAutoPort = new QCheckBox(QStringLiteral("自动选择端口"), portRow);
    m_mcpAutoPort->setObjectName(QStringLiteral("mcpAutoPort"));
    m_mcpAutoPort->setChecked(Settings::mcpPortAuto());
    m_mcpPort = new QSpinBox(portRow);
    m_mcpPort->setObjectName(QStringLiteral("mcpPortSpin"));
    m_mcpPort->setRange(1024, 65535);
    m_mcpPort->setValue(Settings::mcpPort());
    m_mcpPort->setEnabled(!m_mcpAutoPort->isChecked());
    portLayout->addWidget(m_mcpAutoPort);
    portLayout->addWidget(m_mcpPort);
    portLayout->addStretch(1);
    serviceForm->addRow(QStringLiteral("端口"), portRow);
    connect(m_mcpAutoPort, &QCheckBox::toggled, this,
            [this] { refreshMcpState(); });

    m_mcpStatus = infoLabel(QStringLiteral("未启用"), service);
    m_mcpStatus->setObjectName(QStringLiteral("mcpStatusLabel"));
    serviceForm->addRow(QStringLiteral("状态"), m_mcpStatus);

    m_mcpError = infoLabel(QString(), service);
    m_mcpError->setObjectName(QStringLiteral("mcpErrorLabel"));
    m_mcpError->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_mcpError->hide();
    serviceForm->addRow(m_mcpError);

    auto* buttons = new QWidget(service);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    m_mcpStart = new QPushButton(QStringLiteral("启动"), buttons);
    m_mcpStart->setObjectName(QStringLiteral("mcpStartButton"));
    m_mcpStop = new QPushButton(QStringLiteral("停止"), buttons);
    m_mcpStop->setObjectName(QStringLiteral("mcpStopButton"));
    buttonLayout->addWidget(m_mcpStart);
    buttonLayout->addWidget(m_mcpStop);
    buttonLayout->addStretch(1);
    serviceForm->addRow(buttons);
    connect(m_mcpStart, &QPushButton::clicked, this, [this] {
        if (m_mcpEnabled->isChecked())
            applyMcpEnabled(true);
        else
            m_mcpEnabled->setChecked(true);
    });
    connect(m_mcpStop, &QPushButton::clicked, this, [this] {
        if (!m_mcpEnabled->isChecked())
            applyMcpEnabled(false);
        else
            m_mcpEnabled->setChecked(false);
    });
    layout->addWidget(service);

    auto* tokenGroup = new QGroupBox(QStringLiteral("访问令牌"), content);
    auto* tokenLayout = new QVBoxLayout(tokenGroup);
    auto* tokenRow = new QWidget(tokenGroup);
    auto* tokenRowLayout = new QHBoxLayout(tokenRow);
    tokenRowLayout->setContentsMargins(0, 0, 0, 0);
    tokenRowLayout->setSpacing(8);
    m_mcpToken = new QLineEdit(tokenRow);
    m_mcpToken->setObjectName(QStringLiteral("mcpTokenEdit"));
    m_mcpToken->setReadOnly(true);
    m_mcpToken->setPlaceholderText(QStringLiteral("服务器未配置"));
    m_mcpCopyToken = new QPushButton(QStringLiteral("复制"), tokenRow);
    m_mcpCopyToken->setObjectName(QStringLiteral("mcpCopyTokenButton"));
    m_mcpResetToken = new QPushButton(QStringLiteral("重置"), tokenRow);
    m_mcpResetToken->setObjectName(QStringLiteral("mcpResetTokenButton"));
    m_mcpResetToken->setToolTip(QStringLiteral("生成新的随机令牌，已连接的客户端需更新配置"));
    tokenRowLayout->addWidget(m_mcpToken, 1);
    tokenRowLayout->addWidget(m_mcpCopyToken);
    tokenRowLayout->addWidget(m_mcpResetToken);
    tokenLayout->addWidget(tokenRow);
    tokenLayout->addWidget(infoLabel(
        QStringLiteral("令牌只保存在本次进程内；日志和状态文本不会回显令牌。"), tokenGroup));
    connect(m_mcpCopyToken, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpToken);
    connect(m_mcpResetToken, &QPushButton::clicked, this,
            &SettingsDialog::resetMcpToken);
    layout->addWidget(tokenGroup);

    auto* permissionGroup = new QGroupBox(QStringLiteral("权限模式"), content);
    auto* permissionForm = new QFormLayout(permissionGroup);
    m_mcpPermission = new QComboBox(permissionGroup);
    m_mcpPermission->setObjectName(QStringLiteral("mcpPermissionCombo"));
    m_mcpPermission->addItem(QStringLiteral("只读"));
    m_mcpPermission->addItem(QStringLiteral("允许修改"));
    m_mcpPermission->setCurrentIndex(0);
    m_mcpPermission->setToolTip(
        QStringLiteral("“允许修改”需要工程已保存且播放头位于一个场景内；"
                       "解锁前会先创建检查点。"));
    connect(m_mcpPermission, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index == 1)
                    emit mcpPermissionArmRequested();
                else
                    emit mcpPermissionDisarmRequested();
            });
    permissionForm->addRow(QStringLiteral("模式"), m_mcpPermission);
    m_mcpPermissionStatus = infoLabel(
        QStringLiteral("只读：AI 只能读取工程，写工具返回 permission_denied。"),
        permissionGroup);
    m_mcpPermissionStatus->setObjectName(
        QStringLiteral("mcpPermissionStatusLabel"));
    permissionForm->addRow(m_mcpPermissionStatus);
    layout->addWidget(permissionGroup);

    auto* logGroup = new QGroupBox(QStringLiteral("日志窗"), content);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_mcpLogEnabled =
        new QCheckBox(QStringLiteral("显示 MCP 日志窗（首次 AI 调用自动展开）"),
                      logGroup);
    m_mcpLogEnabled->setObjectName(QStringLiteral("mcpLogEnabled"));
    m_mcpLogEnabled->setChecked(Settings::mcpLogVisible());
    logLayout->addWidget(m_mcpLogEnabled);
    logLayout->addWidget(infoLabel(
        QStringLiteral("关闭日志窗后仍可从状态栏的 MCP 入口重新打开；日志只保存在内存中。"),
        logGroup));
    layout->addWidget(logGroup);

    auto* configGroup = new QGroupBox(QStringLiteral("客户端配置"), content);
    auto* configLayout = new QVBoxLayout(configGroup);
    configLayout->addWidget(infoLabel(
        QStringLiteral("远程 Streamable HTTP + Bearer 认证，只监听 127.0.0.1。"
                       "只支持 stdio 的客户端需要 pvforge-mcp 桥接进程。"),
        configGroup));
    m_mcpCopyConfig = new QPushButton(
        QStringLiteral("复制 OpenCode / DeepSeek 远程配置"), configGroup);
    m_mcpCopyConfig->setObjectName(QStringLiteral("mcpCopyConfigButton"));
    connect(m_mcpCopyConfig, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpConfig);
    configLayout->addWidget(m_mcpCopyConfig);
    m_mcpCopyStdioConfig = new QPushButton(
        QStringLiteral("复制 pvforge-mcp stdio 配置"), configGroup);
    m_mcpCopyStdioConfig->setObjectName(
        QStringLiteral("mcpCopyStdioConfigButton"));
    connect(m_mcpCopyStdioConfig, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpStdioConfig);
    configLayout->addWidget(m_mcpCopyStdioConfig);
    configLayout->addWidget(infoLabel(
        QStringLiteral("远程配置使用当前 URL 与令牌；stdio 配置引用本机的 "
                       "pvforge-mcp 桥接进程，由它读取端点文件。"),
        configGroup));
    layout->addWidget(configGroup);

    layout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    refreshMcpState();
    return page;
}

void SettingsDialog::applyMcpEnabled(bool enabled)
{
    if (!m_mcpServer)
    {
        refreshMcpState();
        return;
    }

    if (enabled)
    {
        if (m_mcpServer->isRunning())
        {
            refreshMcpState();
            return;
        }
        QString error;
        const quint16 requested =
            m_mcpAutoPort->isChecked() ? 0 : quint16(m_mcpPort->value());
        const quint16 port = m_mcpServer->start(requested, &error);
        if (port == 0)
        {
            if (error.isEmpty())
                error = QStringLiteral("端口不可用");
            {
                const QSignalBlocker blocker(m_mcpEnabled);
                m_mcpEnabled->setChecked(false);
            }
            m_mcpError->setText(QStringLiteral("启动失败：%1").arg(error));
            m_mcpError->show();
            refreshMcpState();
            emit mcpStateChanged(false, 0, error);
            return;
        }
        m_mcpError->clear();
        m_mcpError->hide();
        refreshMcpState();
        emit mcpStateChanged(true, port, QString());
        return;
    }

    m_mcpServer->stop();
    m_mcpError->clear();
    m_mcpError->hide();
    refreshMcpState();
    emit mcpStateChanged(false, 0, QString());
}

void SettingsDialog::refreshMcpState()
{
    const bool hasServer = m_mcpServer != nullptr;
    const bool running = hasServer && m_mcpServer->isRunning();

    {
        const QSignalBlocker blocker(m_mcpEnabled);
        m_mcpEnabled->setChecked(running);
    }
    m_mcpEnabled->setEnabled(hasServer);
    m_mcpAutoPort->setEnabled(hasServer && !running);
    m_mcpPort->setEnabled(hasServer && !running && !m_mcpAutoPort->isChecked());
    m_mcpStart->setEnabled(hasServer && !running);
    m_mcpStop->setEnabled(hasServer && running);
    m_mcpCopyToken->setEnabled(hasServer);
    m_mcpResetToken->setEnabled(hasServer);
    m_mcpCopyConfig->setEnabled(hasServer);
    m_mcpCopyStdioConfig->setEnabled(hasServer);
    m_mcpPermission->setEnabled(hasServer);

    if (hasServer)
    {
        m_mcpToken->setText(QString::fromLatin1(m_mcpServer->token()));
        m_mcpToken->setEnabled(true);
    }
    else
    {
        m_mcpToken->clear();
        m_mcpToken->setEnabled(false);
    }

    if (!hasServer)
    {
        m_mcpStatus->setText(QStringLiteral("未连接到主窗口"));
    }
    else if (running)
    {
        m_mcpStatus->setText(
            QStringLiteral("监听中 · %1").arg(m_mcpServer->endpointUrl()));
    }
    else if (!m_mcpError->text().isEmpty())
    {
        m_mcpStatus->setText(QStringLiteral("启动失败"));
    }
    else
    {
        m_mcpStatus->setText(QStringLiteral("未启用"));
    }
}

void SettingsDialog::copyMcpToken()
{
    if (!m_mcpServer)
        return;
    QApplication::clipboard()->setText(
        QString::fromLatin1(m_mcpServer->token()));
}

void SettingsDialog::resetMcpToken()
{
    if (!m_mcpServer)
        return;
    m_mcpServer->setToken(QByteArray());
    refreshMcpState();
}

void SettingsDialog::setMcpPermissionState(bool armed, const QString& sceneName,
                                           const QString& error)
{
    if (m_mcpPermission)
    {
        const QSignalBlocker blocker(m_mcpPermission);
        m_mcpPermission->setCurrentIndex(armed ? 1 : 0);
    }
    if (m_mcpPermissionStatus)
    {
        if (armed)
        {
            m_mcpPermissionStatus->setText(
                QStringLiteral("已武装：%1。写操作仅限该场景根文件夹，急停按钮可随时"
                               "停止。")
                    .arg(sceneName.isEmpty() ? QStringLiteral("当前场景") : sceneName));
        }
        else
        {
            m_mcpPermissionStatus->setText(
                QStringLiteral("只读：AI 只能读取工程，写工具返回 "
                               "permission_denied。"));
        }
    }
    if (!error.isEmpty() && m_mcpError)
    {
        m_mcpError->setText(QStringLiteral("解锁失败：%1").arg(error));
        m_mcpError->show();
    }
    else if (error.isEmpty() && m_mcpError && m_mcpServer
             && m_mcpServer->isRunning())
    {
        m_mcpError->clear();
        m_mcpError->hide();
    }
}

QString SettingsDialog::mcpStdioConfigJson() const
{
    QJsonObject server;
    server[QStringLiteral("command")] = QStringLiteral(PVFORGE_MCP_BINARY);
    server[QStringLiteral("args")] =
        QJsonArray{QStringLiteral("--endpoint-file"),
                   defaultMcpEndpointPath()};

    QJsonObject root;
    root[QStringLiteral("mcpServers")] =
        QJsonObject{{QStringLiteral("pvforge"), server}};
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void SettingsDialog::copyMcpStdioConfig()
{
    QApplication::clipboard()->setText(mcpStdioConfigJson());
}

QString SettingsDialog::mcpConfigJson() const
{
    QString url;
    if (m_mcpServer && m_mcpServer->isRunning())
        url = m_mcpServer->endpointUrl();
    else if (m_mcpAutoPort->isChecked())
        url = QStringLiteral("http://127.0.0.1:PORT/mcp");
    else
        url = QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_mcpPort->value());

    const QString token = m_mcpServer
                              ? QString::fromLatin1(m_mcpServer->token())
                              : QString();
    QJsonObject server;
    server[QStringLiteral("type")] = QStringLiteral("streamable-http");
    server[QStringLiteral("url")] = url;
    server[QStringLiteral("headers")] =
        QJsonObject{{QStringLiteral("Authorization"),
                     QStringLiteral("Bearer %1").arg(token)}};

    QJsonObject root;
    root[QStringLiteral("mcpServers")] =
        QJsonObject{{QStringLiteral("pvforge"), server}};
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void SettingsDialog::copyMcpConfig()
{
    QApplication::clipboard()->setText(mcpConfigJson());
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
