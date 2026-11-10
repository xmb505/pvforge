#include "EditorSession.h"

#include "EditActions.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSettings>

#include <algorithm>

namespace pvf {

namespace {

constexpr auto kRecentKey = "recentProjects";
constexpr int kRecentLimit = 10;

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

    m_editActions = std::make_unique<EditActions>(this);

    // Start the per-camera workbench cache so its directory exists even before
    // the first paint; the prewarm scan is debounced internally.
    workbench();
}

EditorSession::~EditorSession()
{
    // Explicit teardown keeps the documented order even if a future member is
    // added: stop the frame clock, release the cache, then the media services.
    if (m_playback)
        m_playback->setDocument(nullptr);
    m_workbench.reset();
    if (m_media)
        m_media->stop();
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
