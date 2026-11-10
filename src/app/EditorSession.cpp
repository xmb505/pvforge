#include "EditorSession.h"

#include "next/app/ProjectDocument.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

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
{
}

EditorSession::~EditorSession() = default;

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
