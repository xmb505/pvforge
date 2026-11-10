#include "next/settings/SettingsDialog.h"

#include "next/settings/GitDetect.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pvf::next {

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

QLabel* selectableLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

QString SettingsDialog::defaultEndpointPath()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/PVForge/mcp-endpoint.json");
}

SettingsDialog::SettingsDialog(SettingsStore* store, QWidget* parent)
    : QDialog(parent)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(620, 520);
    resize(680, 560);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));

    buildHtmlPage();
    buildWorkbenchPage();
    buildVideoPage();
    buildMcpPage();
    buildGitPage();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->setObjectName(QStringLiteral("settingsButtons"));
    if (auto* ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->setText(QStringLiteral("确定"));
        ok->setDefault(true);
    }
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    auto* apply = buttons->addButton(QStringLiteral("应用"), QDialogButtonBox::ApplyRole);
    apply->setObjectName(QStringLiteral("applyButton"));

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveToStore();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, [this] { saveToStore(); });

    auto* root = new QVBoxLayout(this);
    root->addWidget(m_tabs, 1);
    root->addWidget(buttons);

    setSettings(m_store ? m_store->load() : AppSettings{});
}

void SettingsDialog::buildHtmlPage()
{
    auto* page = new QWidget(m_tabs);
    auto* form = new QFormLayout(page);

    m_htmlCacheEnabled =
        new QCheckBox(QStringLiteral("启用 HTML 帧缓存（内存 + 磁盘 LRU）"), page);
    m_htmlCacheEnabled->setObjectName(QStringLiteral("htmlCacheEnabled"));
    form->addRow(m_htmlCacheEnabled);

    m_htmlMemoryMb = new QSpinBox(page);
    m_htmlMemoryMb->setObjectName(QStringLiteral("htmlMemoryMb"));
    m_htmlMemoryMb->setRange(32, 4096);
    m_htmlMemoryMb->setSuffix(QStringLiteral(" MB"));
    form->addRow(QStringLiteral("内存上限"), m_htmlMemoryMb);

    m_htmlDiskGb = new QSpinBox(page);
    m_htmlDiskGb->setObjectName(QStringLiteral("htmlDiskGb"));
    m_htmlDiskGb->setRange(1, 64);
    m_htmlDiskGb->setSuffix(QStringLiteral(" GB"));
    form->addRow(QStringLiteral("磁盘上限"), m_htmlDiskGb);

    m_htmlScale = new QComboBox(page);
    m_htmlScale->setObjectName(QStringLiteral("htmlScale"));
    m_htmlScale->addItem(QStringLiteral("25%"), 0.25);
    m_htmlScale->addItem(QStringLiteral("50%"), 0.5);
    m_htmlScale->addItem(QStringLiteral("75%"), 0.75);
    m_htmlScale->addItem(QStringLiteral("100%"), 1.0);
    form->addRow(QStringLiteral("清晰度（工程分辨率比例）"), m_htmlScale);

    m_htmlMaxWidth = new QSpinBox(page);
    m_htmlMaxWidth->setObjectName(QStringLiteral("htmlMaxWidth"));
    m_htmlMaxWidth->setRange(320, 4096);
    m_htmlMaxWidth->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("最大宽度"), m_htmlMaxWidth);

    m_watchdogMs = new QSpinBox(page);
    m_watchdogMs->setObjectName(QStringLiteral("watchdogMs"));
    m_watchdogMs->setRange(500, 30000);
    m_watchdogMs->setSingleStep(250);
    m_watchdogMs->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("帧看门狗超时"), m_watchdogMs);

    auto* chromeRow = new QWidget(page);
    auto* chromeLayout = new QHBoxLayout(chromeRow);
    chromeLayout->setContentsMargins(0, 0, 0, 0);
    m_chromeExecutable = new QLineEdit(chromeRow);
    m_chromeExecutable->setObjectName(QStringLiteral("chromeExecutable"));
    m_chromeExecutable->setPlaceholderText(QStringLiteral("留空自动检测"));
    m_chromeBrowse = new QPushButton(QStringLiteral("浏览…"), chromeRow);
    m_chromeBrowse->setObjectName(QStringLiteral("chromeBrowse"));
    m_chromeDetect = new QPushButton(QStringLiteral("自动检测"), chromeRow);
    m_chromeDetect->setObjectName(QStringLiteral("chromeDetect"));
    chromeLayout->addWidget(m_chromeExecutable, 1);
    chromeLayout->addWidget(m_chromeBrowse);
    chromeLayout->addWidget(m_chromeDetect);
    form->addRow(QStringLiteral("Chrome 可执行文件"), chromeRow);

    connect(m_chromeBrowse, &QPushButton::clicked, this, [this] {
        QString start = m_chromeExecutable->text().trimmed();
        if (start.isEmpty())
            start = QDir::homePath();
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 Chrome/Chromium 可执行文件"), start);
        if (!path.isEmpty())
            m_chromeExecutable->setText(path);
    });
    connect(m_chromeDetect, &QPushButton::clicked, this,
            &SettingsDialog::chromeDetectRequested);

    m_clearHtmlCache =
        new QPushButton(QStringLiteral("清空 HTML 缓存"), page);
    m_clearHtmlCache->setObjectName(QStringLiteral("clearHtmlCache"));
    connect(m_clearHtmlCache, &QPushButton::clicked, this,
            &SettingsDialog::clearHtmlCacheRequested);
    form->addRow(QStringLiteral("缓存维护"), m_clearHtmlCache);

    form->addRow(selectableLabel(
        QStringLiteral("关闭缓存后只使用前端浏览器实时渲染；导出仍会启动成品浏览器。"),
        page));

    m_tabs->addTab(page, QStringLiteral("HTML / Chrome / 缓存"));
}

void SettingsDialog::buildWorkbenchPage()
{
    auto* page = new QWidget(m_tabs);
    auto* form = new QFormLayout(page);

    m_workbenchCacheEnabled =
        new QCheckBox(QStringLiteral("启用工作台缓存（整帧预渲染）"), page);
    m_workbenchCacheEnabled->setObjectName(QStringLiteral("workbenchCacheEnabled"));
    form->addRow(m_workbenchCacheEnabled);

    m_workbenchScale = new QComboBox(page);
    m_workbenchScale->setObjectName(QStringLiteral("workbenchScale"));
    m_workbenchScale->addItem(QStringLiteral("25%"), 25);
    m_workbenchScale->addItem(QStringLiteral("50%"), 50);
    m_workbenchScale->addItem(QStringLiteral("75%"), 75);
    m_workbenchScale->addItem(QStringLiteral("100%"), 100);
    form->addRow(QStringLiteral("清晰度（工程分辨率比例）"), m_workbenchScale);

    m_workbenchDiskGb = new QSpinBox(page);
    m_workbenchDiskGb->setObjectName(QStringLiteral("workbenchDiskGb"));
    m_workbenchDiskGb->setRange(1, 64);
    m_workbenchDiskGb->setSuffix(QStringLiteral(" GB"));
    form->addRow(QStringLiteral("磁盘上限"), m_workbenchDiskGb);

    form->addRow(selectableLabel(
        QStringLiteral("整帧预渲染缓存；命中时播放直接贴图。关闭后只实时渲染。"),
        page));

    m_tabs->addTab(page, QStringLiteral("工作台缓存"));
}

void SettingsDialog::buildVideoPage()
{
    auto* page = new QWidget(m_tabs);
    auto* form = new QFormLayout(page);

    m_videoProxyEnabled = new QCheckBox(QStringLiteral("启用视频播放代理"), page);
    m_videoProxyEnabled->setObjectName(QStringLiteral("videoProxyEnabled"));
    form->addRow(m_videoProxyEnabled);

    m_videoProxyWidth = new QComboBox(page);
    m_videoProxyWidth->setObjectName(QStringLiteral("videoProxyWidth"));
    m_videoProxyWidth->addItem(QStringLiteral("960 px"), 960);
    m_videoProxyWidth->addItem(QStringLiteral("1280 px"), 1280);
    form->addRow(QStringLiteral("代理最大宽度"), m_videoProxyWidth);

    m_videoMemoryFrames = new QComboBox(page);
    m_videoMemoryFrames->setObjectName(QStringLiteral("videoMemoryFrames"));
    m_videoMemoryFrames->addItem(QStringLiteral("32 帧"), 32);
    m_videoMemoryFrames->addItem(QStringLiteral("64 帧"), 64);
    m_videoMemoryFrames->addItem(QStringLiteral("128 帧"), 128);
    form->addRow(QStringLiteral("内存帧数"), m_videoMemoryFrames);

    form->addRow(selectableLabel(
        QStringLiteral("代理用于时间轴播放；导出始终解码原片。"),
        page));

    m_tabs->addTab(page, QStringLiteral("视频预览"));
}

void SettingsDialog::buildMcpPage()
{
    auto* page = new QWidget(m_tabs);
    auto* form = new QFormLayout(page);

    m_mcpPort = new QSpinBox(page);
    m_mcpPort->setObjectName(QStringLiteral("mcpPort"));
    m_mcpPort->setRange(1024, 65535);
    m_mcpPort->setGroupSeparatorShown(false);
    form->addRow(QStringLiteral("MCP 端口"), m_mcpPort);

    m_mcpEndpointPath = new QLineEdit(page);
    m_mcpEndpointPath->setObjectName(QStringLiteral("mcpEndpointPath"));
    m_mcpEndpointPath->setReadOnly(true);
    m_mcpEndpointPath->setText(defaultEndpointPath());
    form->addRow(QStringLiteral("Endpoint 文件"), m_mcpEndpointPath);

    m_resetMcpToken = new QPushButton(QStringLiteral("重置 token"), page);
    m_resetMcpToken->setObjectName(QStringLiteral("resetMcpToken"));
    connect(m_resetMcpToken, &QPushButton::clicked, this,
            &SettingsDialog::resetMcpTokenRequested);
    form->addRow(QStringLiteral("访问凭据"), m_resetMcpToken);

    m_tabs->addTab(page, QStringLiteral("MCP"));
}

void SettingsDialog::buildGitPage()
{
    auto* page = new QWidget(m_tabs);
    auto* form = new QFormLayout(page);

    m_gitEnabled =
        new QCheckBox(QStringLiteral("启用 Git（历史、崩溃恢复、AI 写入）"), page);
    m_gitEnabled->setObjectName(QStringLiteral("gitEnabled"));
    form->addRow(m_gitEnabled);

    m_gitStatus = selectableLabel(QString(), page);
    m_gitStatus->setObjectName(QStringLiteral("gitStatus"));
    form->addRow(QStringLiteral("检测状态"), m_gitStatus);

    m_gitDetect = new QPushButton(QStringLiteral("重新检测"), page);
    m_gitDetect->setObjectName(QStringLiteral("gitDetect"));
    connect(m_gitDetect, &QPushButton::clicked, this, &SettingsDialog::refreshGitStatus);
    form->addRow(QStringLiteral("检测"), m_gitDetect);

    form->addRow(selectableLabel(
        QStringLiteral("关闭时只保留本次运行的内存撤销；重启后无法撤销。"
                       "没有 Git 时 MCP 只读。"),
        page));

    auto* gitGroup = new QGroupBox(QStringLiteral("当前工程"), page);
    auto* gitForm = new QFormLayout(gitGroup);
    m_gitRepositoryPath = selectableLabel(QStringLiteral("（未打开工程）"), gitGroup);
    m_gitRepositoryPath->setObjectName(QStringLiteral("gitRepositoryPath"));
    gitForm->addRow(QStringLiteral("仓库路径"), m_gitRepositoryPath);
    m_gitRecentCommit = selectableLabel(QStringLiteral("—"), gitGroup);
    m_gitRecentCommit->setObjectName(QStringLiteral("gitRecentCommit"));
    gitForm->addRow(QStringLiteral("最近提交"), m_gitRecentCommit);
    gitForm->addRow(selectableLabel(
        QStringLiteral("Git 只管理文本工程文件（project.json、entities/*.json、"
                       "markers）；assets/ 与 .cache/ 不进提交。"),
        gitGroup));
    form->addRow(gitGroup);

    m_humanCommitDelayMs = new QSpinBox(page);
    m_humanCommitDelayMs->setObjectName(QStringLiteral("humanCommitDelayMs"));
    m_humanCommitDelayMs->setRange(200, 5000);
    m_humanCommitDelayMs->setSingleStep(100);
    m_humanCommitDelayMs->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("人类 commit debounce"), m_humanCommitDelayMs);
    form->addRow(selectableLabel(
        QStringLiteral("人类编辑停手后合并为一个 commit 的等待时间。"), page));

    refreshGitStatus();
    m_tabs->addTab(page, QStringLiteral("Git"));
}

void SettingsDialog::refreshGitStatus()
{
    const GitInfo info = detectGit();
    m_gitStatus->setText(
        info.found
            ? QStringLiteral("已找到：%1（%2）").arg(info.executable, info.version)
            : QStringLiteral("未找到 Git：%1").arg(info.error));
}

AppSettings SettingsDialog::settings() const
{
    AppSettings settings;
    settings.htmlCacheEnabled = m_htmlCacheEnabled->isChecked();
    settings.htmlMemoryBytes = qint64(m_htmlMemoryMb->value()) * kMiB;
    settings.htmlDiskBytes = qint64(m_htmlDiskGb->value()) * kGiB;
    settings.htmlScale = m_htmlScale->currentData().toDouble();
    settings.htmlMaxWidth = m_htmlMaxWidth->value();
    settings.watchdogMs = m_watchdogMs->value();
    settings.chromeExecutable = m_chromeExecutable->text().trimmed();

    settings.videoProxyEnabled = m_videoProxyEnabled->isChecked();
    settings.videoProxyWidth = m_videoProxyWidth->currentData().toInt();
    settings.videoMemoryFrames = m_videoMemoryFrames->currentData().toInt();

    settings.workbenchCacheEnabled = m_workbenchCacheEnabled->isChecked();
    settings.workbenchCacheScalePercent = m_workbenchScale->currentData().toInt();
    settings.workbenchCacheDiskBytes = qint64(m_workbenchDiskGb->value()) * kGiB;
    settings.workbenchCacheMemoryBytes = m_workbenchMemoryBytes;

    settings.mcpPort = static_cast<quint16>(m_mcpPort->value());
    settings.humanCommitDelayMs = m_humanCommitDelayMs->value();
    settings.gitEnabled = m_gitEnabled->isChecked();
    settings.onboardingDone = m_onboardingDone;

    return SettingsStore::sanitized(settings);
}

void SettingsDialog::setSettings(const AppSettings& settings)
{
    const AppSettings value = SettingsStore::sanitized(settings);

    m_htmlCacheEnabled->setChecked(value.htmlCacheEnabled);
    m_htmlMemoryMb->setValue(int(value.htmlMemoryBytes / kMiB));
    m_htmlDiskGb->setValue(int(value.htmlDiskBytes / kGiB));
    const int scaleIndex = m_htmlScale->findData(value.htmlScale);
    if (scaleIndex >= 0)
        m_htmlScale->setCurrentIndex(scaleIndex);
    m_htmlMaxWidth->setValue(value.htmlMaxWidth);
    m_watchdogMs->setValue(value.watchdogMs);
    m_chromeExecutable->setText(value.chromeExecutable);

    m_videoProxyEnabled->setChecked(value.videoProxyEnabled);
    const int proxyWidthIndex = m_videoProxyWidth->findData(value.videoProxyWidth);
    if (proxyWidthIndex >= 0)
        m_videoProxyWidth->setCurrentIndex(proxyWidthIndex);
    const int memoryFramesIndex = m_videoMemoryFrames->findData(value.videoMemoryFrames);
    if (memoryFramesIndex >= 0)
        m_videoMemoryFrames->setCurrentIndex(memoryFramesIndex);

    m_workbenchCacheEnabled->setChecked(value.workbenchCacheEnabled);
    const int workbenchScaleIndex =
        m_workbenchScale->findData(value.workbenchCacheScalePercent);
    if (workbenchScaleIndex >= 0)
        m_workbenchScale->setCurrentIndex(workbenchScaleIndex);
    m_workbenchDiskGb->setValue(int(value.workbenchCacheDiskBytes / kGiB));
    m_workbenchMemoryBytes = value.workbenchCacheMemoryBytes;

    m_mcpPort->setValue(value.mcpPort);
    m_humanCommitDelayMs->setValue(value.humanCommitDelayMs);
    m_gitEnabled->setChecked(value.gitEnabled);
    m_onboardingDone = value.onboardingDone;
}

void SettingsDialog::setGitInfo(const QString& repositoryPath, const QString& recentCommit)
{
    m_gitRepositoryValue = repositoryPath;
    m_gitRecentCommitValue = recentCommit;
    m_gitRepositoryPath->setText(repositoryPath.isEmpty()
                                     ? QStringLiteral("（未打开工程）")
                                     : repositoryPath);
    m_gitRecentCommit->setText(recentCommit.isEmpty() ? QStringLiteral("—")
                                                       : recentCommit);
}

QString SettingsDialog::gitRepositoryPath() const
{
    return m_gitRepositoryValue;
}

QString SettingsDialog::gitRecentCommit() const
{
    return m_gitRecentCommitValue;
}

void SettingsDialog::setEndpointPath(const QString& path)
{
    m_mcpEndpointPath->setText(path);
}

QString SettingsDialog::endpointPath() const
{
    return m_mcpEndpointPath->text();
}

void SettingsDialog::saveToStore()
{
    if (m_store)
        m_store->save(settings());
}

} // namespace pvf::next
