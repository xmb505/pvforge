#include "SettingsDialog.h"

#include "SettingsDialogSupport.h"
#include "next/settings/GitDetect.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pvf {

using namespace settings_detail;

QWidget* SettingsDialog::createAppCachePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("cachePage"));
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

    auto* workbench = new QGroupBox(QStringLiteral("工作台帧缓存"), content);
    auto* workbenchForm = new QFormLayout(workbench);
    m_appWorkbenchEnabled = new QCheckBox(
        QStringLiteral("启用工作台缓存（整帧预渲染）"), workbench);
    m_appWorkbenchEnabled->setObjectName(QStringLiteral("workbenchCacheEnabled"));
    workbenchForm->addRow(m_appWorkbenchEnabled);
    m_appWorkbenchScale = new QComboBox(workbench);
    m_appWorkbenchScale->setObjectName(QStringLiteral("workbenchScale"));
    m_appWorkbenchScale->addItem(QStringLiteral("25%"), 25);
    m_appWorkbenchScale->addItem(QStringLiteral("50%"), 50);
    m_appWorkbenchScale->addItem(QStringLiteral("75%"), 75);
    m_appWorkbenchScale->addItem(QStringLiteral("100%"), 100);
    workbenchForm->addRow(QStringLiteral("清晰度（工程分辨率比例）"),
                          m_appWorkbenchScale);
    m_appWorkbenchExpand = new QSpinBox(workbench);
    m_appWorkbenchExpand->setObjectName(QStringLiteral("workbenchExpandPercent"));
    m_appWorkbenchExpand->setRange(0, 200);
    m_appWorkbenchExpand->setSingleStep(5);
    m_appWorkbenchExpand->setSuffix(QStringLiteral(" %"));
    workbenchForm->addRow(QStringLiteral("向外扩展"), m_appWorkbenchExpand);
    m_appWorkbenchDiskGb = new QSpinBox(workbench);
    m_appWorkbenchDiskGb->setObjectName(QStringLiteral("workbenchDiskGb"));
    m_appWorkbenchDiskGb->setRange(1, 64);
    m_appWorkbenchDiskGb->setSuffix(QStringLiteral(" GB"));
    workbenchForm->addRow(QStringLiteral("磁盘上限"), m_appWorkbenchDiskGb);
    m_appWorkbenchMemoryMb = new QSpinBox(workbench);
    m_appWorkbenchMemoryMb->setObjectName(QStringLiteral("workbenchMemoryMb"));
    m_appWorkbenchMemoryMb->setRange(32, 4096);
    m_appWorkbenchMemoryMb->setSuffix(QStringLiteral(" MB"));
    workbenchForm->addRow(QStringLiteral("内存上限"), m_appWorkbenchMemoryMb);
    m_appWorkbenchWriterThreads = new QComboBox(workbench);
    m_appWorkbenchWriterThreads->setObjectName(
        QStringLiteral("workbenchWriterThreads"));
    for (int threads = 1; threads <= 4; ++threads)
        m_appWorkbenchWriterThreads->addItem(
            QStringLiteral("%1 线程").arg(threads), threads);
    workbenchForm->addRow(QStringLiteral("后台编码线程"),
                          m_appWorkbenchWriterThreads);
    m_appWorkbenchCacheStats = infoLabel(QStringLiteral("（尚未统计）"), workbench);
    m_appWorkbenchCacheStats->setObjectName(QStringLiteral("workbenchCacheStats"));
    workbenchForm->addRow(QStringLiteral("当前占用"), m_appWorkbenchCacheStats);
    auto* workbenchButtons = new QWidget(workbench);
    auto* workbenchButtonLayout = new QHBoxLayout(workbenchButtons);
    workbenchButtonLayout->setContentsMargins(0, 0, 0, 0);
    workbenchButtonLayout->setSpacing(8);
    m_appClearWorkbenchCache =
        new QPushButton(QStringLiteral("清空工作台缓存"), workbenchButtons);
    m_appClearWorkbenchCache->setObjectName(QStringLiteral("clearWorkbenchCache"));
    m_appOpenCacheFolder =
        new QPushButton(QStringLiteral("打开缓存目录"), workbenchButtons);
    m_appOpenCacheFolder->setObjectName(QStringLiteral("openCacheFolder"));
    workbenchButtonLayout->addWidget(m_appClearWorkbenchCache);
    workbenchButtonLayout->addWidget(m_appOpenCacheFolder);
    workbenchButtonLayout->addStretch(1);
    workbenchForm->addRow(QStringLiteral("缓存维护"), workbenchButtons);
    workbenchForm->addRow(infoLabel(
        QStringLiteral("缓存世界矩形在画布四周各扩展宽高的该比例；只影响编辑器"
                       "画布视图，相机视图和导出不扩展。清空后播放时按需重建。"
                       "「后台编码线程」决定播放时同时编码的帧数，改完立即生效，"
                       "已经排队的帧不会丢。"),
        workbench));
    layout->addWidget(workbench);

    auto* html = new QGroupBox(QStringLiteral("HTML 帧缓存"), content);
    auto* htmlForm = new QFormLayout(html);
    m_appHtmlCacheEnabled = new QCheckBox(
        QStringLiteral("启用 HTML 帧缓存（内存 + 磁盘 LRU）"), html);
    m_appHtmlCacheEnabled->setObjectName(QStringLiteral("htmlCacheEnabled"));
    htmlForm->addRow(m_appHtmlCacheEnabled);
    m_appHtmlMemoryMb = new QSpinBox(html);
    m_appHtmlMemoryMb->setObjectName(QStringLiteral("htmlMemoryMb"));
    m_appHtmlMemoryMb->setRange(32, 4096);
    m_appHtmlMemoryMb->setSuffix(QStringLiteral(" MB"));
    htmlForm->addRow(QStringLiteral("内存上限"), m_appHtmlMemoryMb);
    m_appHtmlDiskGb = new QSpinBox(html);
    m_appHtmlDiskGb->setObjectName(QStringLiteral("htmlDiskGb"));
    m_appHtmlDiskGb->setRange(1, 64);
    m_appHtmlDiskGb->setSuffix(QStringLiteral(" GB"));
    htmlForm->addRow(QStringLiteral("磁盘上限"), m_appHtmlDiskGb);
    m_appHtmlScale = new QComboBox(html);
    m_appHtmlScale->setObjectName(QStringLiteral("htmlScale"));
    m_appHtmlScale->addItem(QStringLiteral("25%"), 0.25);
    m_appHtmlScale->addItem(QStringLiteral("50%"), 0.5);
    m_appHtmlScale->addItem(QStringLiteral("75%"), 0.75);
    m_appHtmlScale->addItem(QStringLiteral("100%"), 1.0);
    htmlForm->addRow(QStringLiteral("清晰度（工程分辨率比例）"), m_appHtmlScale);
    m_appHtmlMaxWidth = new QSpinBox(html);
    m_appHtmlMaxWidth->setObjectName(QStringLiteral("htmlMaxWidth"));
    m_appHtmlMaxWidth->setRange(320, 4096);
    m_appHtmlMaxWidth->setSuffix(QStringLiteral(" px"));
    htmlForm->addRow(QStringLiteral("最大宽度"), m_appHtmlMaxWidth);
    m_appHtmlCacheStats = infoLabel(QStringLiteral("（尚未统计）"), html);
    m_appHtmlCacheStats->setObjectName(QStringLiteral("htmlCacheStats"));
    htmlForm->addRow(QStringLiteral("当前占用"), m_appHtmlCacheStats);
    auto* htmlButtons = new QWidget(html);
    auto* htmlButtonLayout = new QHBoxLayout(htmlButtons);
    htmlButtonLayout->setContentsMargins(0, 0, 0, 0);
    htmlButtonLayout->setSpacing(8);
    m_appClearHtmlCache =
        new QPushButton(QStringLiteral("清空 HTML 缓存"), htmlButtons);
    m_appClearHtmlCache->setObjectName(QStringLiteral("clearHtmlCache"));
    m_appOpenHtmlCacheFolder =
        new QPushButton(QStringLiteral("打开缓存目录"), htmlButtons);
    m_appOpenHtmlCacheFolder->setObjectName(QStringLiteral("openHtmlCacheFolder"));
    htmlButtonLayout->addWidget(m_appClearHtmlCache);
    htmlButtonLayout->addWidget(m_appOpenHtmlCacheFolder);
    htmlButtonLayout->addStretch(1);
    htmlForm->addRow(QStringLiteral("缓存维护"), htmlButtons);
    layout->addWidget(html);

    auto* video = new QGroupBox(QStringLiteral("视频预览"), content);
    auto* videoForm = new QFormLayout(video);
    m_appVideoProxyEnabled =
        new QCheckBox(QStringLiteral("启用视频播放代理"), video);
    m_appVideoProxyEnabled->setObjectName(QStringLiteral("videoProxyEnabled"));
    videoForm->addRow(m_appVideoProxyEnabled);
    m_appVideoProxyWidth = new QComboBox(video);
    m_appVideoProxyWidth->setObjectName(QStringLiteral("videoProxyWidth"));
    m_appVideoProxyWidth->addItem(QStringLiteral("960 px"), 960);
    m_appVideoProxyWidth->addItem(QStringLiteral("1280 px"), 1280);
    videoForm->addRow(QStringLiteral("代理最大宽度"), m_appVideoProxyWidth);
    m_appVideoMemoryFrames = new QComboBox(video);
    m_appVideoMemoryFrames->setObjectName(QStringLiteral("videoMemoryFrames"));
    m_appVideoMemoryFrames->addItem(QStringLiteral("32 帧"), 32);
    m_appVideoMemoryFrames->addItem(QStringLiteral("64 帧"), 64);
    m_appVideoMemoryFrames->addItem(QStringLiteral("128 帧"), 128);
    videoForm->addRow(QStringLiteral("内存帧数"), m_appVideoMemoryFrames);
    videoForm->addRow(infoLabel(
        QStringLiteral("代理用于时间轴播放；导出始终解码原片。"), video));
    layout->addWidget(video);

    auto* solver = new QGroupBox(QStringLiteral("求解参数缓存"), content);
    auto* solverLayout = new QVBoxLayout(solver);
    auto* solverInfo = infoLabel(
        QStringLiteral("每段整段预计算，播放只读。编辑后自动失效并排队一次重建；"
                       "下面的按钮会立即清空并重新预计算。"),
        solver);
    solverInfo->setObjectName(QStringLiteral("solveCacheInfo"));
    solverLayout->addWidget(solverInfo);
    m_appRebuildSolveCache =
        new QPushButton(QStringLiteral("重新预计算"), solver);
    m_appRebuildSolveCache->setObjectName(QStringLiteral("rebuildSolveCache"));
    solverLayout->addWidget(m_appRebuildSolveCache);
    layout->addWidget(solver);

    connect(m_appClearWorkbenchCache, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Question, QStringLiteral("清空工作台缓存"),
                        QStringLiteral("将删除当前工程的工作台帧缓存。"
                                       "重新播放时会按需重建。"),
                        QMessageBox::NoButton, this);
        QPushButton* clear =
            box.addButton(QStringLiteral("清空"), QMessageBox::DestructiveRole);
        QPushButton* cancel =
            box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() == clear)
            emit clearWorkbenchCacheRequested();
    });
    connect(m_appOpenCacheFolder, &QPushButton::clicked, this,
            &SettingsDialog::openCacheFolderRequested);
    connect(m_appClearHtmlCache, &QPushButton::clicked, this,
            &SettingsDialog::clearHtmlCacheRequested);
    connect(m_appOpenHtmlCacheFolder, &QPushButton::clicked, this,
            &SettingsDialog::openHtmlCacheFolderRequested);
    connect(m_appRebuildSolveCache, &QPushButton::clicked, this,
            &SettingsDialog::rebuildSolveCacheRequested);

    layout->addWidget(infoLabel(
        QStringLiteral("关闭 HTML 缓存后只使用前端浏览器实时渲染；"
                       "导出仍会启动成品浏览器。"),
        content));
    layout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return page;
}

QWidget* SettingsDialog::createAppHtmlPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("htmlPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* chrome = new QGroupBox(QStringLiteral("Chrome / Chromium"), page);
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
    chromeForm->addRow(infoLabel(
        QStringLiteral("关闭缓存后只使用前端浏览器实时渲染；"
                       "导出仍会启动成品浏览器。"),
        chrome));
    layout->addWidget(chrome);

    auto* render = new QGroupBox(QStringLiteral("HTML 渲染"), page);
    auto* renderForm = new QFormLayout(render);
    m_appWatchdogMs = new QSpinBox(render);
    m_appWatchdogMs->setObjectName(QStringLiteral("watchdogMs"));
    m_appWatchdogMs->setRange(500, 30000);
    m_appWatchdogMs->setSingleStep(250);
    m_appWatchdogMs->setSuffix(QStringLiteral(" ms"));
    renderForm->addRow(QStringLiteral("帧看门狗超时"), m_appWatchdogMs);
    renderForm->addRow(infoLabel(
        QStringLiteral("缓存占用与清空、打开目录已移到「缓存」页。"), render));
    layout->addWidget(render);

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

} // namespace pvf
