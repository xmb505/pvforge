#include "EditorSession.h"

#include "EditActions.h"
#include "anim/Anim.h"
#include "mcp2/HttpServer.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "render2/Render.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <optional>

namespace pvf {

namespace {

constexpr auto kRecentKey = "recentProjects";
constexpr int kRecentLimit = 10;

QString g_mcpConfigPathForTest;
std::optional<quint16> g_mcpPortOverrideForTest;

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

    m_media = std::make_unique<next::MediaServices>(m_document.get(), m_appSettings);
    QString ignored;
    m_media->start(&ignored);

    m_playback = std::make_unique<next::PlaybackController>();
    m_playback->setWatchdogMs(m_appSettings.watchdogMs);
    m_playback->setDocument(m_document.get());

    // Any saved edit invalidates the solver results.  The re-warm is queued so
    // the paint/signal that caused the change never pays for a solve; a paused
    // editor gets the result before the next paint, and playback starts from a
    // warm map.
    QObject::connect(m_document.get(), &next::Document::changed, m_document.get(),
                     [this] {
                         invalidateSolverResults();
                         scheduleSolverPrecompute();
                     });
    QObject::connect(m_playback.get(),
                     &next::PlaybackController::playbackStateChanged,
                     m_playback.get(), [this](bool playing) {
                         if (!playing && m_solverResultsDirty)
                             scheduleSolverPrecompute();
                     });
    scheduleSolverPrecompute();

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
    if (m_playback)
        m_playback->setDocument(nullptr);
    m_workbench.reset();
    if (m_media)
        m_media->stop();
}

void EditorSession::setMcpConfigPathForTest(const QString& path)
{
    g_mcpConfigPathForTest = path;
}

void EditorSession::setMcpPortOverrideForTest(quint16 port)
{
    g_mcpPortOverrideForTest = port;
}

void EditorSession::startMcpServer()
{
    stopMcpServer();
    if (!m_document || !m_document->store())
        return;

    m_mcpService = std::make_unique<mcp2::Service>(m_document->store(),
                                                   &m_document->assets());
    // Git is the write audit trail.  Without it the MCP surface stays readable
    // but every write tool is denied with permission_denied/git_disabled.
    m_mcpService->setWritesEnabled(m_document->store()->gitEnabled());
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
    // Long batch tools run on the UI thread.  Pumping events at each batch
    // checkpoint keeps the emergency-stop button and the AI lock indicator
    // responsive while a transaction is in flight.
    m_mcpService->setBatchCheckpoint([] {
        QCoreApplication::processEvents();
    });

    auto server = std::make_unique<mcp2::HttpServer>(m_mcpService.get());
    if (!g_mcpConfigPathForTest.isEmpty())
    {
        server->setConfigFilePath(g_mcpConfigPathForTest);
        server->setEndpointFilePath(g_mcpConfigPathForTest
                                    + QStringLiteral(".endpoint.json"));
    }

    const quint16 requestedPort = g_mcpPortOverrideForTest
                                      ? *g_mcpPortOverrideForTest
                                      : m_appSettings.mcpPort;
    QString error;
    const quint16 port = server->start(requestedPort, &error);
    m_mcpError = port == 0
                     ? (error.isEmpty() ? QStringLiteral("未知错误") : error)
                     : QString();
    m_mcpServer = std::move(server);
}

void EditorSession::stopMcpServer()
{
    if (m_mcpServer)
    {
        m_mcpServer->stop();
        m_mcpServer.reset();
    }
    m_mcpService.reset();
}

void EditorSession::restartMcpServer()
{
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

QHash<QString, solver::Result>& EditorSession::solverResults() const
{
    return m_solverResults;
}

void EditorSession::invalidateSolverResults()
{
    m_solverResults.clear();
    m_solverResultsDirty = true;
}

int EditorSession::solverEvaluationCount() const
{
    return anim::solverEvaluationCount();
}

const solver::Result* EditorSession::solverResultFor(
    const Uuid& entityUuid, const QString& solverId) const
{
    if (!m_document || solverId.isEmpty())
        return nullptr;
    const auto it = m_document->project().entities.constFind(entityUuid);
    if (it == m_document->project().entities.constEnd())
        return nullptr;
    for (const model::SolverSegment& segment : it->solvers)
    {
        if (segment.id != solverId)
            continue;
        const auto cached =
            m_solverResults.constFind(anim::solverResultKey(it.value(), segment));
        return cached == m_solverResults.constEnd() ? nullptr : &cached.value();
    }
    return nullptr;
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
    anim::Cache cache;
    cache.sharedSolverResults = &m_solverResults;
    render2::SceneWorld world(project, m_document->assets(), &cache);

    // Evaluate at each segment's first frame: that is the one frame where the
    // segment is guaranteed to be active, so exactly one solve per segment is
    // requested.  Overlapping segments are covered by their own start frames.
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        const int duration = entity.duration();
        if (duration <= 0 || entity.solvers.isEmpty())
            continue;
        for (const model::SolverSegment& segment : entity.solvers)
        {
            if (segment.startFrame < 0 || segment.startFrame >= duration)
                continue;
            anim::evaluateEntity(project, entity.uuid,
                                 entity.start + segment.startFrame, world, cache);
        }
    }

    m_solverPrecomputeRunning = false;
}

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
        m_playback->setWatchdogMs(m_appSettings.watchdogMs);
    if (m_document)
        m_document->setHumanCommitDelayMs(m_appSettings.humanCommitDelayMs);

    // Scale/expansion/disk budget are baked into the cache directory key, so
    // the service is rebuilt on the next viewport request.  Frames are cached
    // on demand; nothing is rescanned in the background.
    invalidateWorkbench();

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
