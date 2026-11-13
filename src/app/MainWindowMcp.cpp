#include "MainWindow.h"

#include "EditorSession.h"
#include "SettingsDialog.h"
#include "TestMode.h"
#include "core/Settings.h"
#include "framecache/CacheUsage.h"
#include "htmlcommon/HtmlBrowser.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/history/HistoryPanel.h"
#include "next/settings/OnboardingDialog.h"
#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"
#include "ui/McpLogPanel.h"
#include "ui/Viewport.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QUrl>

namespace pvf {

void MainWindow::wireSessionMcpServer()
{
    if (!m_session || !m_session->mcpServer())
    {
        updateSessionMcpStatus();
        return;
    }

    mcp2::HttpServer* server = m_session->mcpServer();
    if (m_wiredSessionMcpServer == server)
        return;
    m_wiredSessionMcpServer = server;

    connect(server, &mcp2::HttpServer::toolCallStarted, this,
            &MainWindow::onSessionMcpToolStarted);
    connect(server, &mcp2::HttpServer::toolCallFinished, this,
            &MainWindow::onSessionMcpToolFinished);
    connect(server, &mcp2::HttpServer::stoppedChanged, this,
            &MainWindow::onSessionMcpStoppedChanged);

    m_sessionAiStopped = server->aiStopped();
    if (m_mcpLog)
    {
        m_mcpLog->setServerState(server->isRunning(), server->port(),
                                 m_session->mcpError());
        m_mcpLog->setWritesEnabled(m_session->document()
                                   && m_session->document()->store()->gitEnabled());
        m_mcpLog->setAiState(m_sessionAiStopped, false,
                             m_session->document()
                                 && m_session->document()->store()->gitEnabled());
        m_mcpLog->addSystemEvent(
            server->isRunning() ? QStringLiteral("MCP 已启动")
                                : QStringLiteral("MCP 启动失败"),
            server->isRunning()
                ? QStringLiteral("127.0.0.1:%1").arg(server->port())
                : m_session->mcpError());
    }
    refreshSessionAiLock();
}

void MainWindow::onSessionMcpToolStarted(const QString& name,
                                         const QString& argsJson, bool write)
{
    m_sessionRunningToolArgs.insert(name, argsJson);
    if (write)
    {
        ++m_sessionRunningWrites[name];
        ++m_sessionAiBusyWrites;
    }

    if (m_mcpLog)
    {
        if (m_mcpLog->loggingEnabled() && !m_mcpLog->isVisible())
            showSessionMcpLog();
        m_mcpLog->addSystemEvent(QStringLiteral("开始：%1").arg(name), argsJson);
    }
    refreshSessionAiLock();
}

void MainWindow::onSessionMcpToolFinished(const QString& name,
                                          const QString& resultJson, qint64 ms,
                                          bool ok)
{
    const QString argsJson = m_sessionRunningToolArgs.take(name);
    const bool write = mcp2::Service::isWriteTool(name);
    if (write)
    {
        auto it = m_sessionRunningWrites.find(name);
        if (it != m_sessionRunningWrites.end())
        {
            if (--it.value() <= 0)
                m_sessionRunningWrites.erase(it);
        }
        if (m_sessionAiBusyWrites > 0)
            --m_sessionAiBusyWrites;
    }

    if (m_mcpLog)
        m_mcpLog->addToolInvocation(name, argsJson, resultJson, ms, ok);

    // Write tools commit straight through the store.  Reload the document from
    // disk first so the old timeline/viewport/properties/asset bin/workbench
    // all see the new bytes.
    if (write)
    {
        refreshSessionAfterExternalChange();
        refreshSessionHistory();
        updateSessionSettingsDialogContext();
    }

    refreshSessionAiLock();
    statusBar()->showMessage(
        ok ? QStringLiteral("AI %1 完成（%2 ms）").arg(name).arg(ms)
           : QStringLiteral("AI %1 失败（%2 ms）").arg(name).arg(ms),
        5000);
}

void MainWindow::onSessionMcpStoppedChanged(bool stopped)
{
    m_sessionAiStopped = stopped;
    if (m_mcpLog)
    {
        m_mcpLog->addSystemEvent(
            stopped ? QStringLiteral("AI 已急停")
                    : QStringLiteral("AI 写入已恢复"));
    }
    refreshSessionAiLock();
    statusBar()->showMessage(
        stopped ? QStringLiteral("AI 已急停：写入暂停，直到点“恢复”")
                : QStringLiteral("AI 写入已恢复"),
        5000);
}

void MainWindow::refreshSessionAiLock()
{
    // An emergency stop deliberately releases human editing even though the
    // in-flight tool is still unwinding; the Service keeps rejecting AI writes
    // until resumeAi().  Without a stop, only a running write locks.
    const bool writing = m_sessionAiBusyWrites > 0 && !m_sessionAiStopped;
    setHumanEditingEnabled(!writing && !m_exportRunning);

    const bool writesEnabled =
        m_session && m_session->document()
        && m_session->document()->store()->gitEnabled();
    if (m_mcpLog)
    {
        m_mcpLog->setWritesEnabled(writesEnabled);
        m_mcpLog->setAiState(m_sessionAiStopped, writing, writesEnabled);
    }
    updateSessionMcpStatus();
    updateSessionStatus();
}

void MainWindow::updateSessionMcpStatus()
{
    if (!m_mcpStatusButton || !m_session)
        return;

    mcp2::HttpServer* server = m_session->mcpServer();
    const bool running = server && server->isRunning();
    const bool writesEnabled =
        m_session->document() && m_session->document()->store()->gitEnabled();
    if (running)
    {
        if (m_sessionAiStopped)
            m_mcpStatusButton->setText(QStringLiteral("MCP 已急停 · 日志"));
        else if (m_sessionAiBusyWrites > 0)
            m_mcpStatusButton->setText(QStringLiteral("MCP AI 修改中 · 日志"));
        else if (!writesEnabled)
            m_mcpStatusButton->setText(QStringLiteral("MCP 只读 · 日志"));
        else
            m_mcpStatusButton->setText(QStringLiteral("MCP 已启用 · 日志"));
        QString tip = QStringLiteral("监听中：%1").arg(server->endpointUrl());
        tip += writesEnabled ? QStringLiteral("\n可写（Git 已启用）")
                             : QStringLiteral("\n只读：Git 未启用");
        if (m_sessionAiStopped)
            tip += QStringLiteral("\n人类已急停，写操作返回 stopped_by_human");
        tip += QStringLiteral("\n点击打开 MCP 日志");
        m_mcpStatusButton->setToolTip(tip);
    }
    else
    {
        const QString error = m_session->mcpError();
        m_mcpStatusButton->setText(error.isEmpty()
                                       ? QStringLiteral("MCP 关闭 · 日志")
                                       : QStringLiteral("MCP 错误 · 日志"));
        m_mcpStatusButton->setToolTip(
            (error.isEmpty() ? QStringLiteral("MCP 未启动")
                             : QStringLiteral("MCP 启动失败：%1").arg(error))
            + QStringLiteral("\n点击打开 MCP 日志"));
    }
}

void MainWindow::showSessionMcpLog()
{
    if (!m_mcpLog)
        return;
    m_mcpLog->showFromMenu();
    arrangeMcpLog();
}

void MainWindow::showMcpLogForTest()
{
    showSessionMcpLog();
}

QString MainWindow::mcpStatusTextForTest() const
{
    return m_mcpStatusButton ? m_mcpStatusButton->text() : QString();
}

void MainWindow::wireSessionSettingsDialog(SettingsDialog* dialog)
{
    if (!dialog)
        return;

    connect(dialog, &SettingsDialog::settingsApplied, this, [this, dialog] {
        if (!m_session)
            return;
        applySessionSettings(dialog->appSettings());
    });
    connect(dialog, &SettingsDialog::resetMcpTokenRequested, this, [this] {
        if (!m_session)
            return;
        if (m_session->mcpServer())
            m_session->mcpServer()->resetCredentials();
        updateSessionSettingsDialogContext();
        statusBar()->showMessage(
            QStringLiteral("MCP token 已重置；客户端需重新读取 endpoint 文件"),
            8000);
    });
    connect(dialog, &SettingsDialog::clearHtmlCacheRequested, this, [this] {
        if (!m_session)
            return;
        const QString path =
            QDir(m_session->root()).filePath(QStringLiteral(".cache/html"));
        const framecache::ClearResult result = framecache::clearDirectory(
            path, {QStringLiteral("webp")});
        refreshSessionCacheStats();
        if (!result.ok)
        {
            const QString first = result.failedPaths.isEmpty()
                                      ? QStringLiteral("（未知）")
                                      : QDir::toNativeSeparators(
                                            result.failedPaths.first());
            statusBar()->showMessage(
                QStringLiteral(
                    "清理 HTML 缓存失败：%1 个文件未能删除（例：%2），实际释放 %3 MB")
                    .arg(result.failedPaths.size())
                    .arg(first)
                    .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0,
                         'f', 1),
                12000);
            qWarning().noquote()
                << QStringLiteral("html cache clear failed: %1")
                       .arg(result.failedPaths.join(QStringLiteral(", ")));
            return;
        }
        statusBar()->showMessage(
            QStringLiteral("已清理 HTML 缓存：%1 个文件，释放 %2 MB")
                .arg(result.removedFiles)
                .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f',
                     1),
            8000);
    });
    connect(dialog, &SettingsDialog::clearWorkbenchCacheRequested, this,
            [this] { clearSessionWorkbenchCache(); });
    connect(dialog, &SettingsDialog::openCacheFolderRequested, this,
            [this] { openSessionCacheFolder({}); });
    connect(dialog, &SettingsDialog::openHtmlCacheFolderRequested, this,
            [this] { openSessionCacheFolder(QStringLiteral("html")); });
    connect(dialog, &SettingsDialog::rebuildSolveCacheRequested, this, [this] {
        if (!m_session)
            return;
        const bool playing =
            m_session->playback() && m_session->playback()->isPlaying();
        m_session->invalidateAnimCache();
        statusBar()->showMessage(
            playing ? QStringLiteral("播放中：求解缓存将在暂停后重新预计算")
                    : QStringLiteral("求解缓存已清空，正在重新预计算"),
            8000);
    });
    connect(dialog, &SettingsDialog::chromeDetectRequested, this,
            [this, dialog] {
                const QString requested = dialog->appSettings().chromeExecutable;
                QString found = html::HtmlBrowser::detectExecutable(requested);
                if (found.isEmpty())
                    found = html::HtmlBrowser::detectExecutable();
                if (found.isEmpty())
                {
                    statusBar()->showMessage(
                        QStringLiteral("未找到 Chrome/Chromium，请手动指定路径"),
                        8000);
                    return;
                }
                next::AppSettings settings = dialog->appSettings();
                settings.chromeExecutable = found;
                dialog->setAppSettings(settings);
                statusBar()->showMessage(
                    QStringLiteral("已检测到 Chrome：%1").arg(found), 8000);
            });
}

void MainWindow::openSessionSettings()
{
    if (!m_session || !m_session->settingsStore())
        return;
    if (m_sessionSettingsDialog)
    {
        m_sessionSettingsDialog->raise();
        m_sessionSettingsDialog->activateWindow();
        return;
    }

    auto* dialog = new SettingsDialog(m_session->settingsStore(), this);
    dialog->setObjectName(QStringLiteral("settingsDialog"));
    m_sessionSettingsDialog = dialog;
    wireSessionSettingsDialog(dialog);
    updateSessionSettingsDialogContext();
    connect(dialog, &QObject::destroyed, this, [this] {
        m_sessionSettingsDialog = nullptr;
    });

    // Legacy interaction: Edit -> Settings is modal.  Apply still takes effect
    // immediately, without restarting the window.
    dialog->exec();
    applySessionSettings(m_session->settingsStore()->load());
    dialog->deleteLater();
    m_sessionSettingsDialog = nullptr;
}

SettingsDialog* MainWindow::openSessionSettingsDialogForTest()
{
    if (!m_session || !m_session->settingsStore())
        return nullptr;
    if (m_sessionSettingsDialog)
        return m_sessionSettingsDialog;

    auto* dialog = new SettingsDialog(m_session->settingsStore(), this);
    dialog->setObjectName(QStringLiteral("settingsDialogTest"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    m_sessionSettingsDialog = dialog;
    wireSessionSettingsDialog(dialog);
    updateSessionSettingsDialogContext();
    connect(dialog, &QObject::destroyed, this, [this] {
        m_sessionSettingsDialog = nullptr;
    });
    dialog->show();
    return dialog;
}

void MainWindow::updateSessionSettingsDialogContext()
{
    if (!m_session || !m_sessionSettingsDialog)
        return;

    QString repositoryPath = m_session->root();
    QString recentCommit;
    if (m_session->document() && m_session->document()->store()
        && m_session->document()->store()->gitEnabled())
    {
        QString error;
        const QVector<store::GitCommit> history =
            m_session->document()->store()->history(1, &error);
        if (!history.isEmpty())
            recentCommit = QStringLiteral("%1 %2")
                               .arg(history.first().hash.left(7),
                                    history.first().message);
    }
    m_sessionSettingsDialog->setGitInfo(repositoryPath, recentCommit);

    mcp2::HttpServer* server = m_session->mcpServer();
    const bool running = server && server->isRunning();
    m_sessionSettingsDialog->setEndpointPath(
        server ? server->endpointFilePath()
               : SettingsDialog::defaultEndpointPath());
    m_sessionSettingsDialog->setMcpStatus(
        running, server ? server->port() : 0,
        server ? QString::fromLatin1(server->token()) : QString(),
        m_session->mcpError(),
        m_session->document() && m_session->document()->store()->gitEnabled());

    refreshSessionCacheStats();
}

void MainWindow::refreshSessionCacheStats()
{
    if (!m_session || !m_sessionSettingsDialog)
        return;

    const QString workbenchRoot =
        sessionCacheFolderPathForTest(QStringLiteral("workbench"));
    next::WorkbenchServices* workbench = m_session->workbench();
    const framecache::CacheUsage workbenchUsage =
        workbench ? workbench->cacheUsage()
                  : framecache::scanDirectory(
                        workbenchRoot,
                        {QStringLiteral("webp"), QStringLiteral("qoi")});
    m_sessionSettingsDialog->setWorkbenchCacheStats(
        m_session->appSettings().workbenchCacheEnabled, workbenchUsage,
        workbenchRoot);

    const QString htmlRoot = sessionCacheFolderPathForTest(QStringLiteral("html"));
    m_sessionSettingsDialog->setHtmlCacheStats(
        framecache::scanDirectory(htmlRoot, {QStringLiteral("webp")}),
        htmlRoot);
}

void MainWindow::clearSessionWorkbenchCache()
{
    if (!m_session)
        return;

    const QString workbenchRoot =
        sessionCacheFolderPathForTest(QStringLiteral("workbench"));
    next::WorkbenchServices* workbench = m_session->workbench();
    const framecache::ClearResult result =
        workbench ? workbench->clearAll()
                  : framecache::clearDirectory(
                        workbenchRoot,
                        {QStringLiteral("webp"), QStringLiteral("qoi")});
    refreshSessionCacheStats();

    if (!result.ok)
    {
        const QString first = result.failedPaths.isEmpty()
                                  ? QStringLiteral("（未知）")
                                  : QDir::toNativeSeparators(
                                        result.failedPaths.first());
        statusBar()->showMessage(
            QStringLiteral(
                "清空工作台缓存失败：%1 个文件未能删除（例：%2），实际释放 %3 MB")
                .arg(result.failedPaths.size())
                .arg(first)
                .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f',
                     1),
            12000);
        qWarning().noquote()
            << QStringLiteral("workbench cache clear failed: %1")
                   .arg(result.failedPaths.join(QStringLiteral(", ")));
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("已清空工作台缓存：%1 帧，释放 %2 MB")
            .arg(result.removedFrames)
            .arg(double(result.freedBytes) / (1024.0 * 1024.0), 0, 'f', 1),
        8000);
}

void MainWindow::openSessionCacheFolder(const QString& subdirectory)
{
    if (!m_session)
        return;
    const QString path = sessionCacheFolderPathForTest(subdirectory);
    if (!QFileInfo::exists(path))
    {
        statusBar()->showMessage(
            QStringLiteral("缓存目录尚不存在：%1").arg(path), 8000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString MainWindow::sessionCacheFolderPathForTest(
    const QString& subdirectory) const
{
    if (!m_session)
        return {};
    QString path = QDir(m_session->root()).filePath(QStringLiteral(".cache"));
    if (!subdirectory.isEmpty())
        path = QDir(path).filePath(subdirectory);
    return QDir::cleanPath(path);
}

void MainWindow::applySessionSettings(const next::AppSettings& settings)
{
    if (!m_session)
        return;

    QString error;
    const bool ok = m_session->applyAppSettings(settings, &error);
    const next::AppSettings actual = m_session->appSettings();
    Q_UNUSED(actual);

    updateAutosaveTimer();
    if (m_mcpLog)
    {
        m_mcpLog->setLoggingEnabled(Settings::mcpLogVisible());
        m_mcpLog->setWritesEnabled(m_session->document()
                                   && m_session->document()->store()->gitEnabled());
    }
    // A port change rebuilds the listener; reconnect the new instance.
    wireSessionMcpServer();
    updateSessionMcpStatus();
    refreshSessionHistory();
    updateSessionSettingsDialogContext();
    if (m_viewport)
        m_viewport->refreshSession();

    if (!error.isEmpty())
    {
        if (m_mcpLog)
            m_mcpLog->addSystemEvent(QStringLiteral("设置应用失败"), error);
        statusBar()->showMessage(QStringLiteral("设置应用失败：%1").arg(error),
                                 8000);
    }
    else if (!m_session->mcpError().isEmpty())
    {
        if (m_mcpLog)
            m_mcpLog->addSystemEvent(QStringLiteral("MCP 启动失败"),
                                     m_session->mcpError());
        statusBar()->showMessage(
            QStringLiteral("设置已保存，但 MCP 启动失败：%1")
                .arg(m_session->mcpError()),
            8000);
    }
    else
    {
        statusBar()->showMessage(ok ? QStringLiteral("设置已应用")
                                    : QStringLiteral("设置已保存"),
                                 3000);
    }
    updateSessionStatus();
}

void MainWindow::refreshSessionHistory()
{
    if (!m_sessionHistory)
        return;

    if (!m_session || !m_session->document())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(QStringLiteral("未打开工程"));
        return;
    }
    store::ProjectStore* projectStore = m_session->document()->store();
    if (!projectStore || !projectStore->gitEnabled())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(QStringLiteral("Git 未启用"));
        return;
    }

    QString error;
    const QVector<store::GitCommit> commits = projectStore->history(200, &error);
    if (!error.isEmpty())
    {
        m_sessionHistory->setCommits({});
        m_sessionHistory->setStatus(
            QStringLiteral("读取历史失败：%1").arg(error));
        return;
    }
    m_sessionHistory->setCommits(commits);
    m_sessionHistory->setStatus(
        commits.isEmpty() ? QStringLiteral("还没有提交")
                          : QStringLiteral("%1 个提交").arg(commits.size()));
}

bool MainWindow::restoreSessionCommitInternal(const QString& hash,
                                              QString* error)
{
    if (error)
        error->clear();
    if (!m_session || !m_session->document())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    store::ProjectStore* projectStore = m_session->document()->store();
    if (!projectStore || !projectStore->gitEnabled())
    {
        if (error)
            *error = QStringLiteral("Git 未启用");
        return false;
    }

    QString flushError;
    if (m_session->document()->hasPendingCommit())
        m_session->document()->flush(&flushError);
    if (!flushError.isEmpty())
    {
        if (error)
            *error = flushError;
        return false;
    }
    if (!projectStore->restoreCommit(hash, error))
        return false;

    m_session->document()->reloadAfterHistory(error);
    refreshSessionAfterExternalChange();
    refreshSessionHistory();
    return true;
}

void MainWindow::restoreSessionCommit(const QString& hash)
{
    if (!m_session || !m_session->humanEditingEnabled())
        return;
    if (m_sessionHistory)
        m_sessionHistory->setBusy(true);

    QMessageBox box(QMessageBox::Question, QStringLiteral("恢复 Git 历史"),
                    QStringLiteral("恢复到 %1？会丢弃当前未提交文本。")
                        .arg(hash.left(7)),
                    QMessageBox::NoButton, this);
    QPushButton* restore =
        box.addButton(QStringLiteral("恢复"), QMessageBox::AcceptRole);
    QPushButton* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();

    QString error;
    const bool ok = box.clickedButton() == restore
                    && restoreSessionCommitInternal(hash, &error);
    if (m_sessionHistory)
        m_sessionHistory->setBusy(false);
    if (!ok)
    {
        if (!error.isEmpty())
            statusBar()->showMessage(
                QStringLiteral("恢复失败：%1").arg(error), 8000);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("已恢复到 %1").arg(hash.left(7)), 5000);
    if (m_mcpLog)
        m_mcpLog->addSystemEvent(
            QStringLiteral("恢复历史"),
            QStringLiteral("已恢复到 %1").arg(hash.left(7)));
}

bool MainWindow::restoreSessionCommitForTest(const QString& hash,
                                             QString* error)
{
    return restoreSessionCommitInternal(hash, error);
}

void MainWindow::showSessionOnboardingIfNeeded()
{
    if (!m_session || m_session->appSettings().onboardingDone)
        return;

    // Tests must never block on the first-run dialog; the setting stays
    // untouched so the test can drive it explicitly.
    if (testMode())
        return;

    auto* dialog = new next::OnboardingDialog(m_session->appSettings(), this);
    dialog->setObjectName(QStringLiteral("onboardingDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        next::AppSettings settings = m_session->appSettings();
        settings.gitEnabled = dialog->gitEnabled();
        settings.onboardingDone = true;
        applySessionSettings(settings);
    });
    connect(dialog, &QDialog::rejected, this, [this] {
        next::AppSettings settings = m_session->appSettings();
        settings.onboardingDone = true;
        applySessionSettings(settings);
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

} // namespace pvf
