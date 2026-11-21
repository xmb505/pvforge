#include "EditorSession.h"

#include "EditActions.h"
#include "anim/Anim.h"
#include "mcp2/HttpServer.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "render2/Render.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <optional>

namespace pvf {

namespace {

constexpr auto kRecentKey = "recentProjects";
constexpr int kRecentLimit = 10;
// HTML prefetch: an edit burst produces one forced prefetch after this settle
// window instead of one per keystroke.  A scrub needs no timer of its own;
// MediaServices::prefetchHtml() already ignores a frame it has just planned.
constexpr int kHtmlEditPrefetchDelayMs = 120;

#ifdef PVFORGE_TEST_HOOKS
QString g_mcpConfigPathForTest;
std::optional<quint16> g_mcpPortOverrideForTest;
std::function<bool(const QString&)> g_externalImportConfirmerForTest;
#endif

QString normalizedProjectDir(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool isProjectDirectory(const QString& path)
{
    const QFileInfo info(path);
    return info.isDir()
           && QFileInfo::exists(QDir(info.absoluteFilePath())
                                    .filePath(QStringLiteral("project.json")));
}

} // namespace

EditorSession::EditorSession(std::unique_ptr<next::ProjectDocument> document)
    : m_document(std::move(document))
    , m_settingsStore(std::make_unique<next::SettingsStore>())
{
    m_appSettings = m_settingsStore->load();

    m_animCache.sharedSolverResults = &m_solverResults;
    // Keep the long-lived viewport cache bounded: playback samples every frame
    // and the poses of frames far from the playhead are not worth the memory
    // (#132).
    m_animCache.maxPosesPerEntity = 24;

    m_media = std::make_unique<next::MediaServices>(m_document.get(), m_appSettings);
    // The camera monitor renders through this service on every paint; sharing
    // the session's solved segments keeps a repaint from re-running physics.
    m_media->setSolverResults(&m_solverResults);
    QString ignored;
    m_media->start(&ignored);
    // The service is up (or already known to be unavailable): warm the frames
    // around the saved playhead before the first repaint can ask for them.
    prefetchHtmlNow(m_document ? m_document->playhead() : 0, true);

    m_playback = std::make_unique<next::PlaybackController>();
    m_playback->setWatchdogMs(m_appSettings.watchdogMs);
    m_playback->setAudioScrubEnabled(m_appSettings.audioScrub);
    m_playback->setDocument(m_document.get());

    // Any saved edit invalidates the cache.  The re-warm is queued so the
    // paint/signal that caused the change never pays for a solve; a paused
    // editor gets the result before the next paint, and playback starts from a
    // warm cache.
    QObject::connect(m_document.get(), &next::Document::changed, m_document.get(),
                     [this] { invalidateAnimCache(); });
    QObject::connect(m_playback.get(),
                     &next::PlaybackController::playbackStateChanged,
                     m_playback.get(), [this](bool playing) {
                         // Playback paints may only read the cache.  A dirty
                         // cache stays untouched until the clock stops, then
                         // it is cleared and re-warmed once.
                         m_animCache.solverResultsLocked = playing;
                         if (playing)
                             return;
                         if (m_solverResultsDirty)
                             invalidateAnimCache();
                     });
    // Playback raster misses are encoded off the UI thread.  The service may
    // be created later by the first viewport lookup; workbench() re-applies
    // the current state in that case.
    QObject::connect(m_playback.get(),
                     &next::PlaybackController::playbackStateChanged,
                     m_playback.get(), [this](bool playing) {
                         if (m_workbench)
                             m_workbench->setPlaybackActive(playing);
                     });
    invalidateAnimCache();

    // Prefetch hooks.  The cache key covers the whole-frame HTML variables, so
    // any saved edit (structure, keyframes, vars) invalidates the warm frames;
    // the debounce keeps a drag or a text edit from queueing one batch per
    // commit.
    QObject::connect(m_document.get(), &next::Document::changed, m_document.get(),
                     [this] { scheduleHtmlPrefetch(); });
    QObject::connect(m_document.get(), &next::Document::playheadChanged,
                     m_document.get(), [this] {
                         if (m_playback && m_playback->isPlaying())
                             return;
                         prefetchHtmlNow(m_document->playhead(), false);
                     });
    QObject::connect(m_playback.get(),
                     &next::PlaybackController::playbackStateChanged,
                     m_playback.get(), [this](bool playing) {
                         if (playing)
                             prefetchHtmlNow(m_document->playhead(), false);
                     });

    m_editActions = std::make_unique<EditActions>(this);

    // The workbench cache is created lazily by the first viewport lookup; do
    // not open it here, so merely opening a project creates no cache files.

    // The MCP endpoint belongs to the open project: a switch destroys the
    // session (and its listener) before the next project starts one.
    startMcpServer();
}

EditorSession::~EditorSession()
{
    // Explicit teardown keeps the documented order even if a future member is
    // added: stop the MCP listener, stop the frame clock, release the cache,
    // then the media services.
    stopMcpServer();
    if (m_htmlPrefetchTimer)
        m_htmlPrefetchTimer->stop();
    if (m_playback)
        m_playback->setDocument(nullptr);
    m_workbench.reset();
    if (m_media)
        m_media->stop();
}

#ifdef PVFORGE_TEST_HOOKS
void EditorSession::setMcpConfigPathForTest(const QString& path)
{
    g_mcpConfigPathForTest = path;
}

void EditorSession::setMcpPortOverrideForTest(quint16 port)
{
    g_mcpPortOverrideForTest = port;
}

void EditorSession::setExternalImportConfirmerForTest(
    std::function<bool(const QString&)> confirmer)
{
    g_externalImportConfirmerForTest = std::move(confirmer);
}
#endif

bool EditorSession::confirmExternalImport(const QString& path)
{
#ifdef PVFORGE_TEST_HOOKS
    if (g_externalImportConfirmerForTest)
        return g_externalImportConfirmerForTest(path);
#endif

    // Headless runs (MCP-only tests, --shot) never get an implicit "yes".
    if (!QApplication::instance())
        return false;

    QMessageBox box(
        QMessageBox::Question, QStringLiteral("确认导入工程外素材"),
        QStringLiteral("AI 请求把工程目录外的文件复制进素材库：\n\n%1\n\n"
                       "只有在你确认该文件可以交给 AI 服务端读取时才允许。")
            .arg(path),
        QMessageBox::NoButton, nullptr);
    QPushButton* allow =
        box.addButton(QStringLiteral("允许导入"), QMessageBox::AcceptRole);
    QPushButton* deny =
        box.addButton(QStringLiteral("拒绝"), QMessageBox::RejectRole);
    box.setDefaultButton(deny);
    box.setEscapeButton(deny);
    box.exec();
    return box.clickedButton() == allow;
}

void EditorSession::startMcpServer()
{
    stopMcpServer();
    m_restartMcpPending = false;
    if (!m_document || !m_document->store())
        return;

    m_mcpService = std::make_unique<mcp2::Service>(m_document->store(),
                                                   &m_document->assets());
    // Git is the write audit trail.  Without it the MCP surface stays readable
    // but every write tool is denied with permission_denied/git_disabled.
    m_mcpService->setWritesEnabled(m_document->store()->gitEnabled());
    m_mcpService->setExternalImportConfirmer([this](const QString& path) {
        return confirmExternalImport(path);
    });
    m_mcpService->setViewStateProvider([this] {
        mcp2::ViewState state;
        if (m_document)
        {
            state.playhead = m_document->playhead();
            state.selection = m_document->selection();
            state.cameraUuid = activeCameraUuid();
        }
        if (m_viewStateExtras)
            m_viewStateExtras(state);
        return state;
    });
    m_mcpService->setVideoFrameProvider([this](const Uuid& uuid, double frame) {
        return m_media ? m_media->videoFrame(uuid, frame) : QImage();
    });
    m_mcpService->setHtmlFrameProvider([this](const Uuid& uuid, double frame) {
        return m_media ? m_media->htmlFrameBlocking(uuid, frame) : QImage();
    });
    // Long batch tools run synchronously on the UI thread.  Pumping Qt events
    // at a checkpoint re-enters closeEvent / project switching and can destroy
    // this Service while execute() is still on the stack (issue #125).  The
    // stop flag is still read between batch items, so an emergency stop that
    // arrives before the tool starts takes effect; the UI stays frozen for the
    // remainder of one tool call instead of risking a use-after-free.

    auto server = std::make_unique<mcp2::HttpServer>(m_mcpService.get());
#ifdef PVFORGE_TEST_HOOKS
    if (!g_mcpConfigPathForTest.isEmpty())
    {
        server->setConfigFilePath(g_mcpConfigPathForTest);
        server->setEndpointFilePath(g_mcpConfigPathForTest
                                    + QStringLiteral(".endpoint.json"));
    }
#endif

    quint16 requestedPort = m_appSettings.mcpPort;
#ifdef PVFORGE_TEST_HOOKS
    if (g_mcpPortOverrideForTest)
        requestedPort = *g_mcpPortOverrideForTest;
#endif
    QString error;
    const quint16 port = server->start(requestedPort, &error);
    m_mcpError = port == 0
                     ? (error.isEmpty() ? QStringLiteral("未知错误") : error)
                     : QString();

    m_toolCallsInFlight = std::make_shared<std::atomic<int>>(0);
    const std::shared_ptr<std::atomic<int>> counter = m_toolCallsInFlight;
    QObject::connect(server.get(), &mcp2::HttpServer::toolCallStarted,
                     server.get(),
                     [counter](const QString&, const QString&, bool) {
                         counter->fetch_add(1, std::memory_order_relaxed);
                     });
    QObject::connect(
        server.get(), &mcp2::HttpServer::toolCallFinished, server.get(),
        [this, counter](const QString&, const QString&, qint64, bool) {
            counter->fetch_sub(1, std::memory_order_relaxed);
            if (counter->load(std::memory_order_relaxed) <= 0
                && m_restartMcpPending)
            {
                m_restartMcpPending = false;
                // Never restart from inside handleRpc(): the response for the
                // current call has not been sent yet. The document is the
                // context object, so a destroyed session cancels this.
                QTimer::singleShot(0, m_document.get(), [this] {
                    if (!toolCallInProgress() && m_restartMcpPending)
                    {
                        m_restartMcpPending = false;
                        startMcpServer();
                    }
                });
            }
        });
    m_mcpServer = std::move(server);
}

bool EditorSession::toolCallInProgress() const
{
    return m_toolCallsInFlight
           && m_toolCallsInFlight->load(std::memory_order_relaxed) > 0;
}

void EditorSession::stopMcpServer()
{
    if (m_mcpServer)
        m_mcpServer->stop();

    if (toolCallInProgress())
    {
        // execute() is still on this thread's stack: HttpServer::handleRpc
        // continues to use `this` and the Service after execute() returns.
        // Keep both alive until the current event-loop turn ends, then delete
        // them. shared_ptr makes the ownership explicit without a raw delete.
        std::shared_ptr<mcp2::HttpServer> server(m_mcpServer.release());
        std::shared_ptr<mcp2::Service> service(m_mcpService.release());
        QTimer::singleShot(0, [server, service] {
            Q_UNUSED(server);
            Q_UNUSED(service);
        });
        return;
    }

    m_mcpServer.reset();
    m_mcpService.reset();
}

void EditorSession::restartMcpServer()
{
    if (toolCallInProgress())
    {
        // The settings change / restart request arrived while handleRpc() is
        // on the stack. Perform it after the current call finishes.
        m_restartMcpPending = true;
        return;
    }
    startMcpServer();
}

void EditorSession::setHumanEditingEnabled(bool enabled)
{
    m_humanEditingEnabled = enabled;
    if (m_document)
        m_document->setHumanWritesEnabled(enabled);
}

void EditorSession::invalidateWorkbench()
{
    m_workbench.reset();
}

anim::Cache& EditorSession::animCache() const
{
    return m_animCache;
}

QHash<QString, solver::Result>& EditorSession::solverResults() const
{
    return m_solverResults;
}

void EditorSession::invalidateAnimCache()
{
    if (!m_document)
        return;

    // Playback must never pay a solve on a paint tick.  Keep the last warm
    // results visible and clear/re-warm once the clock stops.
    if (m_playback && m_playback->isPlaying())
    {
        m_solverResultsDirty = true;
        return;
    }

    m_animCache.clear();
    m_solverResults.clear();
    m_solverResultsDirty = true;
    scheduleSolverPrecompute();
}

void EditorSession::invalidateSolverResults()
{
    invalidateAnimCache();
}

int EditorSession::solverEvaluationCount() const
{
    return anim::solverEvaluationCount();
}

const solver::Result* EditorSession::solverResultFor(
    const Uuid& entityUuid, const Uuid& solverId) const
{
    if (!m_document || entityUuid.isEmpty() || solverId.isEmpty())
        return nullptr;

    const model::Project& project = m_document->project();
    const auto it = project.entities.constFind(entityUuid);
    if (it == project.entities.constEnd())
        return nullptr;

    for (const model::SolverSegment& segment : it->solvers)
    {
        if (segment.id != solverId)
            continue;
        if (const solver::Result* cached =
                anim::cachedSolverResultFor(it.value(), segment, m_animCache))
            return cached;
        if (m_playback && m_playback->isPlaying())
            return nullptr;
        render2::SceneWorld world(project, m_document->assets(), &m_animCache);
        return anim::solverResultFor(project, it.value(), segment, world,
                                     m_animCache);
    }
    return nullptr;
}

int EditorSession::solverStopFrame(const Uuid& entityUuid,
                                   const Uuid& solverId) const
{
    if (!m_document)
        return -1;
    const model::Project& project = m_document->project();
    const auto it = project.entities.constFind(entityUuid);
    if (it == project.entities.constEnd())
        return -1;

    for (const model::SolverSegment& segment : it->solvers)
    {
        if (segment.id != solverId)
            continue;
        const solver::Result* result = solverResultFor(entityUuid, solverId);
        if (!result || result->stopFrame < 0)
            return -1;
        // A key created after the solver start ends the segment earlier than
        // the physical stop; the band must never overrun it.
        return std::min(result->stopFrame,
                        model::solverStructuralEnd(it.value(), segment));
    }
    return -1;
}

void EditorSession::scheduleSolverPrecompute()
{
    if (!m_document || m_solverPrecomputeScheduled || m_solverPrecomputeRunning)
        return;
    m_solverPrecomputeScheduled = true;
    // The document is the context object: if the session dies, the queued call
    // is discarded with it.
    QTimer::singleShot(0, m_document.get(), [this] {
        m_solverPrecomputeScheduled = false;
        precomputeSolverResults();
    });
}

void EditorSession::precomputeSolverResults()
{
    if (!m_document)
        return;
    if (m_playback && m_playback->isPlaying())
        return; // stay dirty; the pause handler queues the retry

    m_solverResultsDirty = false;
    m_solverPrecomputeRunning = true;

    const model::Project& project = m_document->project();
    render2::SceneWorld world(project, m_document->assets(), &m_animCache);

    // One whole-segment solve per solver.  Results land in the shared map and
    // in m_animCache, so viewport, timeline, properties cards and the mini
    // preview all read the same samples.
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        for (const model::SolverSegment& segment : entity.solvers)
            anim::solverResultFor(project, entity, segment, world, m_animCache);
    }

    m_solverPrecomputeRunning = false;
}

void EditorSession::prefetchHtmlNow(int frame, bool force)
{
    if (!m_media)
        return;
#ifdef PVFORGE_TEST_HOOKS
    ++m_htmlPrefetchCount;
    m_htmlPrefetchLastFrame = frame;
    m_htmlPrefetchLastForce = force;
#endif
    // MediaServices builds the batch and hands it to the background
    // HtmlFrameService: no render happens on this thread.
    m_media->prefetchHtml(frame, force);
}

void EditorSession::scheduleHtmlPrefetch()
{
    if (!m_document || m_htmlPrefetchScheduled)
        return;
    m_htmlPrefetchScheduled = true;
    if (!m_htmlPrefetchTimer)
    {
        m_htmlPrefetchTimer = new QTimer(m_document.get());
        m_htmlPrefetchTimer->setSingleShot(true);
        m_htmlPrefetchTimer->setTimerType(Qt::CoarseTimer);
        QObject::connect(m_htmlPrefetchTimer, &QTimer::timeout,
                         m_document.get(), [this] {
                             m_htmlPrefetchScheduled = false;
                             prefetchHtmlNow(m_document->playhead(), true);
                         });
    }
    m_htmlPrefetchTimer->start(kHtmlEditPrefetchDelayMs);
}

#ifdef PVFORGE_TEST_HOOKS
void EditorSession::flushHtmlPrefetchDebounceForTest()
{
    if (!m_htmlPrefetchTimer || !m_htmlPrefetchTimer->isActive())
        return;
    m_htmlPrefetchTimer->stop();
    if (!m_htmlPrefetchScheduled)
        return;
    m_htmlPrefetchScheduled = false;
    prefetchHtmlNow(m_document ? m_document->playhead() : 0, true);
}
#endif

bool EditorSession::applyAppSettings(const next::AppSettings& settings,
                                     QString* error)
{
    if (error)
        error->clear();

    const next::AppSettings previous = m_appSettings;
    m_appSettings = next::SettingsStore::sanitized(settings);
    if (m_settingsStore)
        m_settingsStore->save(m_appSettings);

    if (m_media)
        m_media->applySettings(m_appSettings);
    if (m_playback)
    {
        m_playback->setWatchdogMs(m_appSettings.watchdogMs);
        m_playback->setAudioScrubEnabled(m_appSettings.audioScrub);
    }
    if (m_document)
        m_document->setHumanCommitDelayMs(m_appSettings.humanCommitDelayMs);

    // Scale/expansion/disk budget are baked into the cache directory key, so
    // the service is rebuilt on the next viewport request.  Frames are cached
    // on demand; nothing is rescanned in the background.  The writer thread
    // count is not part of that key: retune the live pool so the frames it
    // already queued still get written.
    const bool workbenchLayoutChanged =
        previous.workbenchCacheEnabled != m_appSettings.workbenchCacheEnabled
        || previous.workbenchCacheScalePercent
               != m_appSettings.workbenchCacheScalePercent
        || previous.workbenchCacheExpandPercent
               != m_appSettings.workbenchCacheExpandPercent
        || previous.workbenchCacheDiskBytes
               != m_appSettings.workbenchCacheDiskBytes
        || previous.workbenchCacheMemoryBytes
               != m_appSettings.workbenchCacheMemoryBytes;
    if (workbenchLayoutChanged)
        invalidateWorkbench();
    else if (m_workbench)
        m_workbench->setWriterThreads(
            m_appSettings.workbenchCacheWriterThreads);

    bool ok = true;
    const bool actualGit =
        m_document && m_document->store() && m_document->store()->gitEnabled();
    if (m_document && m_appSettings.gitEnabled != actualGit)
    {
        QString gitError;
        if (!m_document->setGitEnabled(m_appSettings.gitEnabled, &gitError))
        {
            // Settings only express a wish; persist the state the store
            // actually reached so the next launch does not claim a repository
            // that is not there (for example when git is missing).
            m_appSettings.gitEnabled =
                m_document->store() && m_document->store()->gitEnabled();
            if (m_settingsStore)
                m_settingsStore->save(m_appSettings);
            if (error)
                *error = gitError.isEmpty() ? QStringLiteral("Git 切换失败")
                                            : gitError;
            ok = false;
        }
    }

    if (m_mcpService && m_document)
        m_mcpService->setWritesEnabled(m_document->store()->gitEnabled());

    if (previous.mcpPort != m_appSettings.mcpPort)
    {
        const bool wasStopped = m_mcpServer && m_mcpServer->aiStopped();
        restartMcpServer();
        if (wasStopped && m_mcpServer)
            m_mcpServer->stopAi();
    }

    return ok;
}

std::unique_ptr<EditorSession> EditorSession::createProject(
    const QString& projectDir, const model::ProjectSettings& settings,
    QString* error, bool useGit)
{
    auto document =
        next::ProjectDocument::create(normalizedProjectDir(projectDir), settings,
                                      error, useGit);
    if (!document)
        return nullptr;
    return std::unique_ptr<EditorSession>(new EditorSession(std::move(document)));
}

std::unique_ptr<EditorSession> EditorSession::openProject(
    const QString& projectDir, QString* error, bool useGit)
{
    const QString path = normalizedProjectDir(projectDir);
    const QFileInfo info(path);
    if (!info.exists())
    {
        if (error)
            *error = QStringLiteral("工程目录不存在：%1").arg(path);
        return nullptr;
    }
    if (info.isFile())
    {
        if (error)
            *error = QStringLiteral(
                "旧工程格式已废弃：%1\n\n"
                "PVForge 现在使用 .pvproj 目录工程。请新建工程，"
                "或选择包含 project.json 的工程目录。")
                         .arg(path);
        return nullptr;
    }
    if (!info.isDir() || !isProjectDirectory(path))
    {
        if (error)
            *error = QStringLiteral("不是 PVForge 目录工程（缺少 project.json）：%1")
                         .arg(path);
        return nullptr;
    }

    auto document = next::ProjectDocument::open(path, error, useGit);
    if (!document)
        return nullptr;
    return std::unique_ptr<EditorSession>(new EditorSession(std::move(document)));
}

const model::Project& EditorSession::project() const
{
    return m_document->project();
}

model::Project& EditorSession::mutableProject()
{
    return m_document->mutableProject();
}

assets::AssetStore& EditorSession::assets()
{
    return m_document->assets();
}

const model::ProjectSettings& EditorSession::settings() const
{
    return m_document->project().settings;
}

QString EditorSession::root() const
{
    return m_document->root();
}

QString EditorSession::name() const
{
    const QString name = m_document->project().settings.name;
    return name.isEmpty() ? QFileInfo(root()).fileName() : name;
}

next::WorkbenchServices* EditorSession::workbench()
{
    if (!m_appSettings.workbenchCacheEnabled)
    {
        m_workbench.reset();
        return nullptr;
    }

    // The legacy viewport always edits the whole canvas (the camera is only a
    // viewfinder overlay), so its whole-frame cache is the canvas cache.  The
    // monitor renders camera views live and does not read this cache.
    const Uuid camera;
    const int expand = std::clamp(m_appSettings.workbenchCacheExpandPercent, 0, 200);
    if (m_workbench && m_workbench->cameraUuid() == camera
        && m_workbench->expandPercent() == expand)
        return m_workbench.get();

    m_workbench.reset();

    next::WorkbenchServices::Providers providers;
    providers.videoFrame = [this](const Uuid& uuid, double frame) {
        return m_media ? m_media->videoFrame(uuid, frame) : QImage();
    };
    providers.htmlFrame = [this](const Uuid& uuid, double frame) {
        return m_media ? m_media->htmlFrame(uuid, frame) : QImage();
    };
    providers.htmlPending = [this] {
        return m_media && m_media->htmlPending();
    };

    m_workbench = std::make_unique<next::WorkbenchServices>(
        m_document.get(), m_appSettings, camera, providers);
    // The workbench builds a frame meta on every lookup; sharing the solved
    // segments keeps an image-cache hit from re-running physics.
    m_workbench->setSolverResults(&m_solverResults);
    if (m_playback)
        m_workbench->setPlaybackActive(m_playback->isPlaying());
    return m_workbench.get();
}

Uuid EditorSession::activeCameraUuid() const
{
    if (!m_document)
        return {};

    const model::Project& project = m_document->project();
    const auto isVisibleCamera = [&project](const Uuid& uuid) {
        if (uuid.isEmpty())
            return false;
        const auto it = project.entities.constFind(uuid);
        return it != project.entities.constEnd()
               && it->content.kind == ContentKind::Camera && !it->hidden;
    };

    if (m_monitorCameraExplicit)
        return isVisibleCamera(m_monitorCamera) ? m_monitorCamera : Uuid();

    const Uuid selected = m_document->selection();
    if (isVisibleCamera(selected))
        return selected;

    return {};
}

void EditorSession::setMonitorCameraUuid(const Uuid& uuid)
{
    if (uuid.isEmpty())
    {
        m_monitorCameraExplicit = true;
        m_monitorCameraFromSelection = false;
        m_monitorCamera.clear();
        return;
    }
    if (!m_document)
        return;
    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd()
        || it->content.kind != ContentKind::Camera)
        return;
    m_monitorCamera = uuid;
    m_monitorCameraExplicit = true;
    m_monitorCameraFromSelection = false;
}

void EditorSession::syncSelectionCamera(const Uuid& selected)
{
    if (!m_document)
        return;
    const auto it = m_document->project().entities.constFind(selected);
    if (it != m_document->project().entities.constEnd()
        && it->content.kind == ContentKind::Camera && !it->hidden)
    {
        m_monitorCamera = selected;
        m_monitorCameraExplicit = true;
        m_monitorCameraFromSelection = true;
        return;
    }

    if (m_monitorCameraFromSelection)
    {
        m_monitorCamera.clear();
        m_monitorCameraExplicit = true;
        m_monitorCameraFromSelection = false;
    }
}

int EditorSession::playhead() const
{
    return m_document->playhead();
}

void EditorSession::setPlayhead(int frame)
{
    m_document->setPlayhead(frame);
}

void EditorSession::savePlayhead()
{
    m_document->savePlayheadState();
}

void EditorSession::flush(QString* error)
{
    QString flushError;
    m_document->flush(&flushError);
    m_document->savePlayheadState();
    if (error)
        *error = flushError;
}

bool EditorSession::hasPendingCommit() const
{
    return m_document->hasPendingCommit();
}

bool EditorSession::commitProject(const QString& message, QString* error)
{
    return m_document->commitProject(message, error);
}

QStringList recentProjects()
{
    QSettings settings;
    const QStringList stored = settings.value(kRecentKey).toStringList();
    QStringList kept;
    for (const QString& entry : stored)
    {
        const QString path = normalizedProjectDir(entry);
        if (isProjectDirectory(path) && !kept.contains(path))
            kept.append(path);
        if (kept.size() >= kRecentLimit)
            break;
    }
    if (kept != stored)
        settings.setValue(kRecentKey, kept);
    return kept;
}

void rememberRecentProject(const QString& path)
{
    const QString normalized = normalizedProjectDir(path);
    if (!isProjectDirectory(normalized))
        return;

    QStringList entries = recentProjects();
    entries.removeAll(normalized);
    entries.prepend(normalized);
    while (entries.size() > kRecentLimit)
        entries.removeLast();

    QSettings settings;
    settings.setValue(kRecentKey, entries);
}

} // namespace pvf
