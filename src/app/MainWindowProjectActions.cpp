#include "MainWindow.h"

#include "EditActions.h"
#include "EditorSession.h"
#include "ExportDialog.h"
#include "ExportProgressDialog.h"
#include "NewProjectDialog.h"
#include "TestMode.h"
#include "assets/AssetStore.h"
#include "mcp2/HttpServer.h"
#include "next/app/MediaExportSession.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/settings/SettingsStore.h"
#include "ui/AssetLibraryPanel.h"
#include "ui/CameraMonitor.h"
#include "ui/Viewport.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace pvf {

#ifdef PVFORGE_TEST_HOOKS
bool MainWindow::openSessionProjectForTest(const QString& path, QString* error)
{
    if (m_session && m_session->toolCallInProgress())
    {
        if (error)
            *error = QStringLiteral("AI 工具正在执行，暂不能切换工程");
        return false;
    }
    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    auto session = openSessionProjectWithRecovery(
        path, useGit, m_crashRecoveryHandler, this, error);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
        return false;
    }
    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
    return true;
}
#endif

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

namespace {

SessionCrashRecoveryChoice askSessionCrashRecovery(
    const QVector<store::DirtyText>& dirty, const QString& commitFailure,
    QWidget* parent)
{
    QStringList lines;
    const int shown = std::min<int>(dirty.size(), 20);
    for (int i = 0; i < shown; ++i)
        lines << QStringLiteral("%1（%2）").arg(dirty.at(i).path, dirty.at(i).status);
    if (dirty.size() > shown)
        lines << QStringLiteral("...等 %1 个文件").arg(dirty.size());

    const bool failedCommit = !commitFailure.isEmpty();
    const QString title =
        failedCommit ? QStringLiteral("上次关闭时 Git 提交失败")
                     : QStringLiteral("发现未提交的文本改动");
    QString text;
    if (failedCommit)
    {
        text = QStringLiteral("上次关闭时工程文本已保存，但 Git 提交失败：\n\n%1\n\n"
                              "重试提交会把这些文本写入一个恢复点；"
                              "“恢复并打开”保留工作区文本但不再自动提交。")
                   .arg(commitFailure);
    }
    else
    {
        text = QStringLiteral("工程中有 %1 个未提交的文本文件，"
                              "可能来自上次异常退出：\n\n%2\n\n"
                              "请选择如何处理这些改动。")
                   .arg(dirty.size())
                   .arg(lines.join(QLatin1Char('\n')));
    }

    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton,
                    parent);
    QPushButton* restore =
        box.addButton(QStringLiteral("恢复并打开"), QMessageBox::AcceptRole);
    QPushButton* commit = box.addButton(
        failedCommit ? QStringLiteral("重试提交")
                     : QStringLiteral("提交为恢复点"),
        QMessageBox::ActionRole);
    QPushButton* discard =
        box.addButton(QStringLiteral("丢弃未提交文本"), QMessageBox::DestructiveRole);
    QPushButton* cancel =
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(failedCommit ? commit : restore);
    box.setEscapeButton(cancel);
    box.exec();

    if (box.clickedButton() == restore)
        return SessionCrashRecoveryChoice::Restore;
    if (box.clickedButton() == discard)
        return SessionCrashRecoveryChoice::Discard;
    if (box.clickedButton() == commit)
        return SessionCrashRecoveryChoice::Commit;
    return SessionCrashRecoveryChoice::Cancel;
}

} // namespace

std::unique_ptr<EditorSession> openSessionProjectWithRecovery(
    const QString& path, bool useGit, const SessionCrashRecoveryHandler& handler,
    QWidget* parent, QString* error)
{
    auto session = EditorSession::openProject(path, error, useGit);
    if (!session)
        return nullptr;

    const QVector<store::DirtyText> dirty =
        session->document()->store()->dirtyText();
    const QString commitFailure =
        session->document()->store()->previousCommitFailure();
    if (dirty.isEmpty() && commitFailure.isEmpty())
        return session;

    const SessionCrashRecoveryChoice choice =
        handler ? handler(dirty, commitFailure)
                : askSessionCrashRecovery(dirty, commitFailure, parent);
    QString recoveryError;
    switch (choice)
    {
    case SessionCrashRecoveryChoice::Restore:
        // The store already loaded the working tree, so keeping it is the
        // restore action.
        break;
    case SessionCrashRecoveryChoice::Discard:
        if (!session->document()->store()->discardDirtyText(&recoveryError)
            || !session->document()->store()->reloadAfterDiscard(&recoveryError))
        {
            if (error)
                *error = QStringLiteral("丢弃未提交文本失败：%1")
                             .arg(recoveryError.isEmpty()
                                      ? QStringLiteral("未知错误")
                                      : recoveryError);
            return nullptr;
        }
        session->document()->reloadAfterHistory(&recoveryError);
        break;
    case SessionCrashRecoveryChoice::Commit:
        if (!session->document()->store()->commitRecoveredText(&recoveryError))
        {
            if (error)
                *error = QStringLiteral("提交未提交文本失败：%1")
                             .arg(recoveryError.isEmpty()
                                      ? QStringLiteral("未知错误")
                                      : recoveryError);
            return nullptr;
        }
        break;
    case SessionCrashRecoveryChoice::Cancel:
        // Not an error: the caller suppresses an empty error message.
        if (error)
            error->clear();
        return nullptr;
    }

    // A project opened with quarantined files is read-only; tell the human and
    // name the isolated files instead of failing every later edit silently.
    const QString readOnlyReason =
        session->document()->store()->readOnlyReason();
    if (!readOnlyReason.isEmpty() && !testMode())
    {
        QMessageBox::warning(parent, QStringLiteral("工程以只读方式打开"),
                             readOnlyReason);
    }
    return session;
}

std::unique_ptr<EditorSession> openSessionProjectFromCli(
    const QString& path, const SessionCrashRecoveryHandler& handler,
    QString* error)
{
    const bool useGit = next::SettingsStore().load().gitEnabled;
    return openSessionProjectWithRecovery(path, useGit, handler, nullptr, error);
}

void MainWindow::openSessionProject()
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(
            QStringLiteral("导出正在进行中，暂时不能切换工程。"), 5000);
        return;
    }
    if (m_session && m_session->toolCallInProgress())
    {
        statusBar()->showMessage(
            QStringLiteral("AI 工具正在执行，请等待本次调用结束再切换工程。"), 5000);
        return;
    }

    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("打开项目（选择 .pvproj 工程目录）"), QString(),
        QFileDialog::ShowDirsOnly);
    if (path.isEmpty())
        return;

    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    QString error;
    auto session = openSessionProjectWithRecovery(
        path, useGit, m_crashRecoveryHandler, this, &error);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("无法打开项目"), error);
        return;
    }

    rememberRecentProject(session->root());
    showSessionWindow(std::move(session));
    close();
}

void MainWindow::createSessionProject()
{
    if (m_exportRunning)
    {
        statusBar()->showMessage(
            QStringLiteral("导出正在进行中，暂时不能切换工程。"), 5000);
        return;
    }
    if (m_session && m_session->toolCallInProgress())
    {
        statusBar()->showMessage(
            QStringLiteral("AI 工具正在执行，请等待本次调用结束再切换工程。"), 5000);
        return;
    }

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
    const bool useGit = m_session ? m_session->appSettings().gitEnabled
                                  : next::SettingsStore().load().gitEnabled;
    if (m_session)
    {
        m_session->stopMcpServer();
        m_wiredSessionMcpServer = nullptr;
    }
    auto session = EditorSession::createProject(path, settings, &error, useGit);
    if (!session)
    {
        if (m_session)
        {
            m_session->restartMcpServer();
            wireSessionMcpServer();
        }
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

void MainWindow::exportVideo()
{
    if (!m_session || !m_session->document())
        return;
    if (m_exportRunning)
    {
        statusBar()->showMessage(QStringLiteral("导出正在进行中。"), 4000);
        return;
    }

    if (m_playing)
        togglePlayback();
    if (m_session->playback())
        m_session->playback()->pause();

    QString error;
    m_session->flush(&error);
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("提交工程失败：%1").arg(error));
        return;
    }

    ExportDialog dialog(m_session->project(), m_session->activeCameraUuid(),
                        m_session->root(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    startSessionExport(dialog.cameraUuid(), dialog.startFrame(),
                       dialog.endFrame() + 1, dialog.outputPath(),
                       dialog.includeAudio(), dialog.outputFps(),
                       dialog.resolutionScale(), dialog.crf(),
                       dialog.encoder(), dialog.preset(),
                       dialog.overwriteExisting());
}

void MainWindow::startSessionExport(const Uuid& cameraUuid, int startFrame,
                                    int endFrame, const QString& outputPath,
                                    bool includeAudio, int outputFps,
                                    int resolutionScale, int crf,
                                    const QString& encoder,
                                    const QString& preset, bool overwrite)
{
    if (!m_session || !m_session->document() || m_exportRunning)
        return;
    if (outputPath.trimmed().isEmpty())
        return;

    auto* media = m_session->media();
    if (media)
    {
        // Pre-warm the preview service, not the export browsers: the export
        // worker owns a private final-only service (see MediaExportSession).
        QString htmlError;
        media->start(&htmlError);
        if (!htmlError.isEmpty())
            qWarning().noquote() << "export HTML service:" << htmlError;
    }

    auto project = std::make_shared<model::Project>(m_session->project());
    auto assets = std::make_shared<assets::AssetStore>(m_session->assets());
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

    // Immutable snapshot for the worker: it never reads the live Document or
    // the preview media services (issue #126).
    auto mediaSnapshot = std::make_shared<next::MediaExportSession>(
        *project, *assets, m_session->appSettings(), exportSize);

    export2::Options options;
    options.outputPath = outputPath;
    options.cameraUuid = cameraUuid;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    options.outputSize = QSize(); // project size * resolutionScale
    options.fps = selectedFps;
    options.outputFps = selectedFps;
    options.resolutionScale = scale;
    options.crf = std::clamp(crf, 0, 51);
    options.encoder = encoder;
    options.preset = preset;
    options.includeAudio = includeAudio;
    options.overwrite = overwrite;
    options.videoFrame = [mediaSnapshot](const Uuid& entityUuid,
                                         double projectFrame) {
        return mediaSnapshot->videoFrame(entityUuid, projectFrame);
    };
    options.htmlFrame = [mediaSnapshot](const Uuid& entityUuid,
                                        double projectFrame) {
        return mediaSnapshot->htmlFrame(entityUuid, projectFrame);
    };

    if (m_exportOptionsProbe)
        m_exportOptionsProbe(options);

    int rangeStart = startFrame;
    int rangeEnd = endFrame;
    if (rangeEnd <= rangeStart)
    {
        const auto camera = project->entities.constFind(cameraUuid);
        if (camera != project->entities.constEnd())
        {
            rangeStart = camera->start;
            rangeEnd = camera->end + 1;
        }
    }
    const double durationSeconds =
        std::max(0, rangeEnd - rangeStart) / double(projectFps);
    const int totalFrames =
        std::max(1, int(std::llround(durationSeconds * double(selectedFps))));

    m_exportCancelled = std::make_shared<std::atomic_bool>(false);
    m_exportFrame = std::make_shared<std::atomic_int>(0);
    m_exportTotal = std::make_shared<std::atomic_int>(totalFrames);
    m_exportEta = std::make_shared<std::atomic<double>>(0.0);

    auto* progress = new ExportProgressDialog(this);
    m_exportProgress = progress;
    progress->setProgress(0, totalFrames, 0.0);
    connect(progress, &ExportProgressDialog::cancelRequested, this, [this] {
        if (m_exportCancelled)
            m_exportCancelled->store(true);
    });
    progress->show();
    // Let the preview window paint before the worker starts decoding.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    auto* watcher = new QFutureWatcher<export2::Result>(this);
    m_exportWatcher = watcher;
    auto* timer = new QTimer(this);
    m_exportTimer = timer;
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] {
        if (!m_exportProgress || !m_exportFrame || !m_exportTotal || !m_exportEta)
            return;
        const int frame = m_exportFrame->load();
        const int total = std::max(1, m_exportTotal->load());
        const double eta = m_exportEta->load();
        m_exportProgress->setProgress(frame, total, eta);
        if (m_exportProgressHandler)
            m_exportProgressHandler(frame, total, eta);
    });
    timer->start();

    connect(watcher, &QFutureWatcher<export2::Result>::finished, this,
            [this, watcher, progress, timer, outputPath] {
                timer->stop();
                timer->deleteLater();
                if (m_exportTimer == timer)
                    m_exportTimer = nullptr;
                m_exportRunning = false;

                // Release the export write lock; a human AI stop that predates
                // the export stays in effect.
                if (m_session->document())
                    m_session->document()->setWritesLocked(false);
                if (m_exportHoldsAiStop)
                {
                    m_exportHoldsAiStop = false;
                    if (mcp2::HttpServer* server = m_session->mcpServer())
                        server->resumeAi();
                }

                const export2::Result result = watcher->result();
                progress->finish();
                progress->deleteLater();
                if (m_exportProgress == progress)
                    m_exportProgress = nullptr;
                watcher->deleteLater();
                if (m_exportWatcher == watcher)
                    m_exportWatcher = nullptr;

                // Restore the panels, but keep an AI write lock if one is
                // still active.
                refreshSessionAiLock();
                refreshSession();

                if (m_exportFinishedHandler)
                    m_exportFinishedHandler(result.ok, result.error);
                if (m_suppressExportCompletionDialog)
                    return;

                if (result.ok)
                {
                    QString text = QStringLiteral("已导出 %1 帧到\n%2")
                                       .arg(result.framesWritten)
                                       .arg(QDir::toNativeSeparators(outputPath));
                    if (!result.warnings.isEmpty())
                        text += QStringLiteral("\n\n警告 %1 条（音频被跳过）：\n%2")
                                    .arg(result.warnings.size())
                                    .arg(result.warnings.join('\n'));
                    const QString folder = QFileInfo(outputPath).absolutePath();

                    QMessageBox box(QMessageBox::Information,
                                    QStringLiteral("导出完成"), text,
                                    QMessageBox::NoButton, this);
                    QPushButton* openFolderButton =
                        box.addButton(QStringLiteral("打开文件夹"),
                                      QMessageBox::ActionRole);
                    QPushButton* okButton =
                        box.addButton(QStringLiteral("确定"),
                                      QMessageBox::AcceptRole);
                    box.setDefaultButton(okButton);
                    box.exec();
                    if (box.clickedButton() == openFolderButton)
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(folder));
                }
                else if (result.error == QStringLiteral("cancelled"))
                {
                    statusBar()->showMessage(QStringLiteral("导出已取消"),
                                             5000);
                }
                else
                {
                    QMessageBox::warning(
                        this, QStringLiteral("导出失败"),
                        friendlyExportError(result.error));
                }
            });

    m_exportRunning = true;
    setHumanEditingEnabled(false);
    updateSessionActionStates();

    // Freeze the live project for the duration of the export: human edits and
    // undo/redo are rejected by the document, MCP write tools are paused.  A
    // stop that the human set before the export is left untouched.
    if (m_session->document())
        m_session->document()->setWritesLocked(true);
    if (mcp2::HttpServer* server = m_session->mcpServer())
    {
        if (!server->aiStopped())
        {
            m_exportHoldsAiStop = true;
            server->stopAi();
        }
    }

    watcher->setFuture(QtConcurrent::run(
        [project, assets, options, mediaSnapshot, cancelled = m_exportCancelled,
         frame = m_exportFrame, total = m_exportTotal, eta = m_exportEta] {
            export2::Result result = export2::exportVideo(
                *project, *assets, options,
                [frame, total, eta, cancelled](int done, int all,
                                               double seconds) {
                    frame->store(done);
                    total->store(all);
                    eta->store(seconds);
                    return !cancelled->load();
                });
            // Surface the decoder/HTML reason for a failed frame (the renderer
            // only knows that a frame was missing).
            if (!result.ok && result.error != QStringLiteral("cancelled"))
            {
                const QString detail = mediaSnapshot->lastError();
                if (!detail.isEmpty() && !result.error.contains(detail))
                    result.error += QStringLiteral("（%1）").arg(detail);
            }
            // Release the private export browser/decoder on the thread that
            // created them.
            mediaSnapshot->finish();
            return result;
        }));
}

#ifdef PVFORGE_TEST_HOOKS
void MainWindow::cancelSessionExportForTest()
{
    if (m_exportCancelled)
        m_exportCancelled->store(true);
}

void MainWindow::startSessionExportForTest(
    const Uuid& cameraUuid, int startFrame, int endFrame,
    const QString& outputPath, bool includeAudio, int outputFps,
    int resolutionScale, int crf, const QString& encoder,
    const QString& preset, bool overwrite)
{
    startSessionExport(cameraUuid, startFrame, endFrame, outputPath, includeAudio,
                       outputFps, resolutionScale, crf, encoder, preset,
                       overwrite);
}
#endif

void MainWindow::replaceSessionAssetFromDialog(const QString& uuid)
{
    if (!m_session || !m_session->document())
        return;

    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return;

    const model::Content& content = it.value().content;
    if (content.kind != ContentKind::Asset && content.kind != ContentKind::Empty)
    {
        const QString message =
            content.kind == ContentKind::Camera
                ? QStringLiteral("摄像机不能更换素材")
                : QStringLiteral("生成内容实体不能直接换素材，请先清空内容");
        QMessageBox::warning(this, QStringLiteral("更换素材失败"), message);
        return;
    }

    const AssetType currentType =
        content.kind == ContentKind::Asset ? content.asset.type : AssetType::Image;
    const AssetHash currentHash =
        content.kind == ContentKind::Asset ? content.asset.hash : AssetHash();
    const QString hash = AssetLibraryPanel::pickAsset(
        this, m_session, content.kind, currentType, currentHash);
    if (hash.isEmpty())
        return;

    // A video's split-out audio track is a following child (issue #128).
    // Replacing the picture should not leave the old sound behind, but the user
    // decides: the new video's audio replaces it, or the old track stays.
    bool syncChildAudio = true;
    const auto stored = m_session->assets().get(hash);
    if (stored && stored->type == AssetType::Video)
    {
        const model::Project& project = m_session->project();
        const QString newAudio =
            stored->metadata.value(QStringLiteral("audioHash")).toString();
        bool hasAudioChild = false;
        bool alreadyMatches = false;
        for (auto child = project.entities.constBegin();
             child != project.entities.constEnd(); ++child)
        {
            if (child.value().parentUuid != uuid)
                continue;
            if (child.value().content.kind != ContentKind::Asset
                || child.value().content.asset.type != AssetType::Audio)
                continue;
            hasAudioChild = true;
            alreadyMatches = !newAudio.isEmpty()
                             && child.value().content.asset.hash == newAudio;
        }

        if (hasAudioChild && !alreadyMatches)
        {
            QMessageBox box(
                QMessageBox::Question, QStringLiteral("更换视频素材"),
                newAudio.isEmpty()
                    ? QStringLiteral("新视频没有音轨。要移除当前分离出的音频片段吗？")
                    : QStringLiteral("要同时把分离出的音频片段换成新视频的音轨吗？"),
                QMessageBox::NoButton, this);
            QPushButton* sync = box.addButton(
                newAudio.isEmpty() ? QStringLiteral("移除音频")
                                   : QStringLiteral("替换音频"),
                QMessageBox::AcceptRole);
            QPushButton* keep = box.addButton(QStringLiteral("保留旧音频"),
                                              QMessageBox::RejectRole);
            box.setDefaultButton(sync);
            box.setEscapeButton(keep);
            box.exec();
            syncChildAudio = box.clickedButton() == sync;
        }
    }

    QString error;
    if (!m_session->editActions()->replaceAsset(uuid, hash, &error,
                                                syncChildAudio))
    {
        QMessageBox::warning(this, QStringLiteral("更换素材失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已更换素材"), 3000);
    if (m_library)
        m_library->enqueueMissingVideoExtractions();
    if (m_viewport)
        m_viewport->refreshSession();
    if (m_monitor)
        m_monitor->setProjectChanged();
}

#ifdef PVFORGE_TEST_HOOKS
bool MainWindow::replaceAssetForTest(const QString& uuid, const QString& hash,
                                     QString* error)
{
    if (!m_session || !m_session->editActions())
    {
        if (error)
            *error = QStringLiteral("没有打开的工程");
        return false;
    }
    return m_session->editActions()->replaceAsset(uuid, hash, error);
}
#endif

} // namespace pvf
