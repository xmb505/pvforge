#include "Store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace pvf::store {

namespace {

constexpr char kInitMessage[] = "pvf: init project";

struct ProcessResult
{
    bool started = false;
    int exitCode = -1;
    QString out;
    QString err;
};

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

ProcessResult runProcess(const QString& program, const QStringList& arguments,
                         const QString& workDir = QString())
{
    ProcessResult result;
    QProcess process;
    if (!workDir.isEmpty())
        process.setWorkingDirectory(workDir);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    process.setProcessEnvironment(environment);

    process.start(program, arguments);
    if (!process.waitForStarted(10000))
    {
        result.err = process.errorString();
        return result;
    }
    result.started = true;

    if (!process.waitForFinished(120000))
    {
        process.kill();
        process.waitForFinished(5000);
        result.err = QStringLiteral("命令超时");
        return result;
    }

    result.exitCode = process.exitStatus() == QProcess::NormalExit
                          ? process.exitCode()
                          : -1;
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError());
    return result;
}

ProcessResult runGit(const QString& root, const QStringList& arguments)
{
    QStringList fullArguments;
    fullArguments.reserve(arguments.size() + 2);
    fullArguments << QStringLiteral("-C") << root;
    fullArguments += arguments;
    return runProcess(QStringLiteral("git"), fullArguments);
}

QString processFailure(const ProcessResult& result)
{
    QString detail = result.err.trimmed();
    if (detail.isEmpty())
        detail = result.out.trimmed();
    if (detail.isEmpty())
    {
        detail = result.started
                     ? QStringLiteral("退出码 %1").arg(result.exitCode)
                     : QStringLiteral("无法启动进程");
    }
    return detail;
}

bool checkGit(QString* error)
{
    const ProcessResult git = runProcess(QStringLiteral("git"),
                                         {QStringLiteral("--version")});
    if (!git.started || git.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("找不到 git，请安装 Git 后重试（%1）")
                     .arg(processFailure(git)));
        return false;
    }
    return true;
}

bool hasHead(const QString& root)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
               QStringLiteral("--quiet"), QStringLiteral("HEAD")});
    return result.exitCode == 0;
}

QString headHash(const QString& root)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    return result.exitCode == 0 ? result.out.trimmed() : QString();
}

bool isTracked(const QString& root, const QString& relativePath)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("ls-files"), QStringLiteral("--error-unmatch"),
               QStringLiteral("--"), relativePath});
    return result.exitCode == 0;
}

QString entityRelativePath(const Uuid& uuid)
{
    return QStringLiteral("entities/") + uuid + QStringLiteral(".json");
}

QString absolutePath(const QString& root, const QString& relativePath)
{
    return root + QLatin1Char('/') + relativePath;
}

bool readJsonObject(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error,
                 QStringLiteral("无法读取 %1：%2").arg(path, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        setError(error,
                 QStringLiteral("%1 JSON 解析失败：%2（偏移 %3）")
                     .arg(path, parseError.errorString())
                     .arg(parseError.offset));
        return false;
    }
    if (!document.isObject())
    {
        setError(error, QStringLiteral("%1 的 JSON 顶层必须是对象").arg(path));
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(const QString& path, const QJsonObject& object,
                     QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }

    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n'))
        bytes.append('\n');

    if (file.write(bytes) != bytes.size())
    {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, QStringLiteral("无法保存 %1：%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool writeBytesFile(const QString& path, const QByteArray& bytes, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }

    if (file.write(bytes) != bytes.size())
    {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, QStringLiteral("无法保存 %1：%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool writeTextFile(const QString& path, const QString& content, QString* error)
{
    return writeBytesFile(path, content.toUtf8(), error);
}

// Missing files read as an empty byte array: undo/redo uses empty to mean
// "this path did not exist".
QByteArray readFileBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool loadProjectData(const QString& root, model::Project* project,
                     QString* error)
{
    const QString projectPath = absolutePath(root, QStringLiteral("project.json"));
    QJsonObject object;
    if (!readJsonObject(projectPath, &object, error))
        return false;

    const QString format =
        object.value(QStringLiteral("format")).toString();
    if (format != QLatin1String(model::kProjectFormat))
    {
        setError(error,
                 QStringLiteral("%1 不是 PVForge 工程（format=%2）")
                     .arg(projectPath, format));
        return false;
    }

    const int version = object.value(QStringLiteral("version")).toInt(-1);
    if (version != model::kProjectFormatVersion)
    {
        setError(error,
                 QStringLiteral("不支持的工程版本 %1（需要 %2）")
                     .arg(version)
                     .arg(model::kProjectFormatVersion));
        return false;
    }

    QString parseError;
    const auto parsed = model::projectFromJson(object, &parseError);
    if (!parsed)
    {
        setError(error,
                 QStringLiteral("%1 无效：%2").arg(projectPath, parseError));
        return false;
    }

    model::Project loaded = *parsed;
    QDir entitiesDirectory(absolutePath(root, QStringLiteral("entities")));
    if (entitiesDirectory.exists())
    {
        const QFileInfoList files = entitiesDirectory.entryInfoList(
            {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& fileInfo : files)
        {
            const QString fileName = fileInfo.fileName();
            const QString uuid = fileName.left(fileName.size() - 5);
            if (!model::isValidUuid(uuid))
            {
                setError(error,
                         QStringLiteral("实体文件名不是合法 UUID：%1")
                             .arg(fileInfo.absoluteFilePath()));
                return false;
            }

            QJsonObject entityObject;
            if (!readJsonObject(fileInfo.absoluteFilePath(), &entityObject, error))
                return false;

            QString entityError;
            const auto entity = model::entityFromJson(entityObject, &entityError);
            if (!entity)
            {
                setError(error,
                         QStringLiteral("实体文件 %1 无效：%2")
                             .arg(fileInfo.absoluteFilePath(), entityError));
                return false;
            }

            if (entity->uuid != uuid)
            {
                setError(error,
                         QStringLiteral("实体文件 %1 内的 uuid（%2）与文件名不一致")
                             .arg(fileInfo.absoluteFilePath(), entity->uuid));
                return false;
            }

            if (loaded.entities.contains(uuid))
            {
                setError(error,
                         QStringLiteral("实体 UUID 重复：%1").arg(uuid));
                return false;
            }
            loaded.entities.insert(uuid, *entity);
        }
    }

    if (!model::validateProject(loaded, error))
        return false;

    *project = std::move(loaded);
    return true;
}

QString normalizedMessage(const QString& message)
{
    QString normalized = message.trimmed();
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));
    normalized.replace(QChar(0x1f), QLatin1Char(' '));
    normalized = normalized.simplified();
    if (normalized.isEmpty())
        normalized = QStringLiteral("pvf: save");
    else if (!normalized.startsWith(QStringLiteral("pvf:")))
        normalized.prepend(QStringLiteral("pvf: "));
    if (normalized == QStringLiteral("pvf:"))
        normalized = QStringLiteral("pvf: save");
    return normalized;
}

bool ensureGitIdentity(const QString& root, QString* error)
{
    const ProcessResult name = runGit(
        root, {QStringLiteral("config"), QStringLiteral("--get"),
               QStringLiteral("user.name")});
    if (name.exitCode != 0 || name.out.trimmed().isEmpty())
    {
        const ProcessResult set =
            runGit(root, {QStringLiteral("config"), QStringLiteral("user.name"),
                          QStringLiteral("PVForge")});
        if (set.exitCode != 0)
        {
            setError(error,
                     QStringLiteral("无法配置 git user.name：%1")
                         .arg(processFailure(set)));
            return false;
        }
    }

    const ProcessResult email = runGit(
        root, {QStringLiteral("config"), QStringLiteral("--get"),
               QStringLiteral("user.email")});
    if (email.exitCode != 0 || email.out.trimmed().isEmpty())
    {
        const ProcessResult set = runGit(
            root, {QStringLiteral("config"), QStringLiteral("user.email"),
                   QStringLiteral("pvforge@localhost")});
        if (set.exitCode != 0)
        {
            setError(error,
                     QStringLiteral("无法配置 git user.email：%1")
                         .arg(processFailure(set)));
            return false;
        }
    }
    return true;
}

QStringList splitNul(const QString& text)
{
    QStringList result;
    const QStringList pieces = text.split(QLatin1Char('\0'));
    for (const QString& piece : pieces)
    {
        if (!piece.isEmpty())
            result << piece;
    }
    return result;
}

bool commitOnlyPaths(const QString& root, const QString& message,
                     const QStringList& paths, QString* hash, QString* error)
{
    if (paths.isEmpty())
    {
        setError(error, QStringLiteral("没有可提交的变更"));
        return false;
    }

    QStringList arguments{QStringLiteral("commit"), QStringLiteral("--only"),
                          QStringLiteral("-m"), message, QStringLiteral("--")};
    arguments += paths;
    const ProcessResult commit = runGit(root, arguments);
    if (commit.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git commit 失败：%1").arg(processFailure(commit)));
        return false;
    }

    const QString result = headHash(root);
    if (result.isEmpty())
    {
        setError(error, QStringLiteral("git commit 成功但无法读取 HEAD"));
        return false;
    }
    *hash = result;
    return true;
}

bool isProjectTextPath(const QString& relativePath)
{
    if (relativePath == QLatin1String("project.json")
        || relativePath == QLatin1String(".gitignore"))
    {
        return true;
    }
    return relativePath.startsWith(QLatin1String("entities/"))
           && relativePath.endsWith(QLatin1String(".json"));
}

QString porcelainStatusName(QChar index, QChar worktree)
{
    if (index == QLatin1Char('?') && worktree == QLatin1Char('?'))
        return QStringLiteral("added");
    if (index == QLatin1Char('R') || worktree == QLatin1Char('R'))
        return QStringLiteral("renamed");
    if (index == QLatin1Char('C') || worktree == QLatin1Char('C'))
        return QStringLiteral("copied");
    if (index == QLatin1Char('U') || worktree == QLatin1Char('U')
        || (index == QLatin1Char('A') && worktree == QLatin1Char('A'))
        || (index == QLatin1Char('D') && worktree == QLatin1Char('D')))
    {
        return QStringLiteral("conflict");
    }
    if (index == QLatin1Char('A'))
        return QStringLiteral("added");
    if (index == QLatin1Char('D') || worktree == QLatin1Char('D'))
        return QStringLiteral("deleted");
    if (index == QLatin1Char('T') || worktree == QLatin1Char('T'))
        return QStringLiteral("typechange");
    return QStringLiteral("modified");
}

struct RawDirtyText
{
    QString path;
    QString sourcePath; // rename/copy source; empty otherwise
    QChar index;
    QChar worktree;
};

// `git status --porcelain=v1 -z` entries. For renames/copies the source path
// follows the destination as an extra NUL-separated token.
QVector<RawDirtyText> scanRawDirtyText(const QString& root, QString* error)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
               QStringLiteral("-z"), QStringLiteral("--untracked-files=all")});
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git status 失败：%1").arg(processFailure(result)));
        return {};
    }

    QVector<RawDirtyText> entries;
    const QStringList tokens = result.out.split(QLatin1Char('\0'));
    for (int i = 0; i < tokens.size(); ++i)
    {
        const QString& token = tokens.at(i);
        if (token.size() < 4)
            continue;

        RawDirtyText entry;
        entry.index = token.at(0);
        entry.worktree = token.at(1);
        entry.path = token.mid(3);

        const bool hasSource =
            entry.index == QLatin1Char('R') || entry.worktree == QLatin1Char('R')
            || entry.index == QLatin1Char('C')
            || entry.worktree == QLatin1Char('C');
        if (hasSource && i + 1 < tokens.size())
            entry.sourcePath = tokens.at(++i);

        entries.append(entry);
    }
    return entries;
}

const char kGitIgnore[] = R"(# PVForge versions text project data only; media and caches stay local.
assets/
.cache/
.pvforge/

# Temporary preview and export files.
*.tmp
*.temp
*.part
.pvforge-preview-*
pvforge-preview-*
preview-*
*.swp
*.swo
*~
.DS_Store
Thumbs.db
)";

} // namespace

ProjectStore::ProjectStore(QString root, model::Project project)
    : m_root(std::move(root))
    , m_project(std::move(project))
{
    m_assets.emplace(m_root);
}

std::optional<ProjectStore> ProjectStore::create(
    const QString& projectDir, const model::ProjectSettings& settings,
    QString* error)
{
    if (projectDir.trimmed().isEmpty())
    {
        setError(error, QStringLiteral("工程目录不能为空"));
        return std::nullopt;
    }

    model::Project initialProject;
    initialProject.settings = settings;
    if (!model::validateProject(initialProject, error))
        return std::nullopt;

    if (!checkGit(error))
        return std::nullopt;

    const QString root =
        QDir::cleanPath(QFileInfo(projectDir).absoluteFilePath());
    const QFileInfo info(root);
    if (info.exists())
    {
        if (!info.isDir())
        {
            setError(error, QStringLiteral("%1 已存在且不是目录").arg(root));
            return std::nullopt;
        }
        const QStringList entries = QDir(root).entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System
                | QDir::NoDotAndDotDot);
        if (!entries.isEmpty())
        {
            setError(error, QStringLiteral("工程目录已存在且不为空：%1").arg(root));
            return std::nullopt;
        }
    }

    QDir directory;
    if (!directory.mkpath(root) || !directory.mkpath(absolutePath(root, QStringLiteral("entities")))
        || !directory.mkpath(absolutePath(root, QStringLiteral("assets"))))
    {
        setError(error, QStringLiteral("无法创建工程目录：%1").arg(root));
        return std::nullopt;
    }

    if (!writeJsonObject(absolutePath(root, QStringLiteral("project.json")),
                         model::toJson(initialProject), error)
        || !writeTextFile(absolutePath(root, QStringLiteral(".gitignore")),
                          QString::fromLatin1(kGitIgnore), error))
    {
        return std::nullopt;
    }

    const ProcessResult init =
        runGit(root, {QStringLiteral("init")});
    if (init.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git init 失败：%1").arg(processFailure(init)));
        return std::nullopt;
    }

    if (!ensureGitIdentity(root, error))
        return std::nullopt;

    const ProcessResult add = runGit(
        root, {QStringLiteral("add"), QStringLiteral("--"),
               QStringLiteral(".gitignore"), QStringLiteral("project.json")});
    if (add.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git add 初始文件失败：%1")
                     .arg(processFailure(add)));
        return std::nullopt;
    }

    QString ignoredHash;
    if (!commitOnlyPaths(
            root, QString::fromLatin1(kInitMessage),
            {QStringLiteral(".gitignore"), QStringLiteral("project.json")},
            &ignoredHash, error))
    {
        return std::nullopt;
    }

    return ProjectStore(root, std::move(initialProject));
}

std::optional<ProjectStore> ProjectStore::open(const QString& projectDir,
                                               QString* error)
{
    if (projectDir.trimmed().isEmpty())
    {
        setError(error, QStringLiteral("工程目录不能为空"));
        return std::nullopt;
    }

    const QString root =
        QDir::cleanPath(QFileInfo(projectDir).absoluteFilePath());
    const QFileInfo info(root);
    if (!info.exists() || !info.isDir())
    {
        setError(error, QStringLiteral("工程目录不存在：%1").arg(root));
        return std::nullopt;
    }

    if (!checkGit(error))
        return std::nullopt;

    if (!QFileInfo::exists(absolutePath(root, QStringLiteral(".git"))))
    {
        setError(error,
                 QStringLiteral("%1 不是 git 仓库（缺少 .git）").arg(root));
        return std::nullopt;
    }

    model::Project project;
    if (!loadProjectData(root, &project, error))
        return std::nullopt;

    // The in-memory undo/redo history starts empty after opening a project;
    // across sessions git history is the cold backup.
    ProjectStore store(root, std::move(project));

    // Scan once so the caller can offer recover/discard. A transient git
    // failure (for example a stale index.lock) must not block opening; the
    // caller can rescan with dirtyTextFiles().
    store.refreshDirtyText();
    return std::move(store);
}

bool ProjectStore::saveEntity(const Uuid& uuid, QString* error)
{
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (it->uuid != uuid)
    {
        setError(error,
                 QStringLiteral("实体索引键 %1 与实体 UUID %2 不一致")
                     .arg(uuid, it->uuid));
        return false;
    }
    if (!model::validateEntity(*it, error))
        return false;

    const QString relativePath = entityRelativePath(uuid);
    notePath(relativePath);
    return writeJsonObject(absolutePath(m_root, relativePath),
                           model::toJson(*it), error);
}

bool ProjectStore::saveProject(QString* error)
{
    // Validate settings and markers without requiring the entity tree to be
    // consistent; deleting a parent without cascading is the caller's job.
    model::Project check;
    check.settings = m_project.settings;
    check.markers = m_project.markers;
    if (!model::validateProject(check, error))
        return false;

    const QString relativePath = QStringLiteral("project.json");
    notePath(relativePath);
    return writeJsonObject(absolutePath(m_root, relativePath),
                           model::toJson(m_project), error);
}

bool ProjectStore::deleteEntity(const Uuid& uuid, QString* error)
{
    if (!model::isValidUuid(uuid))
    {
        setError(error, QStringLiteral("实体 UUID 格式非法：%1").arg(uuid));
        return false;
    }

    const auto it = m_project.entities.find(uuid);
    if (it == m_project.entities.end())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }

    const QString relativePath = entityRelativePath(uuid);
    const QString path = absolutePath(m_root, relativePath);
    notePath(relativePath);
    QFile file(path);
    if (file.exists() && !file.remove())
    {
        setError(error,
                 QStringLiteral("无法删除 %1：%2").arg(path, file.errorString()));
        return false;
    }

    m_project.entities.erase(it);
    return true;
}

bool ProjectStore::commit(const QString& message, QString* error)
{
    const QString normalized = normalizedMessage(message);

    // Git backs up text project data only. assets/ and .cache/ are local and
    // ignored, so they are never staged here.
    QStringList candidates = m_txnPaths.values();
    std::sort(candidates.begin(), candidates.end());

    QStringList addPaths;
    for (const QString& relativePath : std::as_const(candidates))
    {
        if (QFileInfo::exists(absolutePath(m_root, relativePath))
            || isTracked(m_root, relativePath))
        {
            addPaths << relativePath;
        }
    }

    if (addPaths.isEmpty())
    {
        clearDirty();
        return true;
    }

    QStringList addArguments{QStringLiteral("add"), QStringLiteral("-A"),
                             QStringLiteral("--")};
    addArguments += addPaths;
    const ProcessResult add = runGit(m_root, addArguments);
    if (add.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git add 失败：%1").arg(processFailure(add)));
        return false;
    }

    QStringList diffArguments{QStringLiteral("diff"), QStringLiteral("--cached"),
                              QStringLiteral("--name-only"), QStringLiteral("-z"),
                              QStringLiteral("--")};
    diffArguments += addPaths;
    const ProcessResult diff = runGit(m_root, diffArguments);
    if (diff.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git diff 失败：%1").arg(processFailure(diff)));
        return false;
    }

    const QStringList staged = splitNul(diff.out);
    if (staged.isEmpty())
    {
        clearDirty();
        return true;
    }

    // --only keeps changes the user staged for unrelated files out of this
    // commit, and leaves them staged in the index.
    QString hash;
    if (!commitOnlyPaths(m_root, normalized, staged, &hash, error))
        return false;

    // The cold backup now exists; remember exactly what it changed so undo
    // can restore it without touching git.
    UndoEntry entry;
    entry.message = normalized;
    for (const QString& relativePath : std::as_const(candidates))
    {
        const QByteArray before = m_txnBefore.value(relativePath);
        const QByteArray after =
            readFileBytes(absolutePath(m_root, relativePath));
        if (before == after)
            continue;
        entry.before.insert(relativePath, before);
        entry.after.insert(relativePath, after);
    }
    if (!entry.before.isEmpty())
        pushUndo(std::move(entry));

    clearDirty();
    return true;
}

bool ProjectStore::undo(QString* error)
{
    if (m_undoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可撤销的操作"));
        return false;
    }

    UndoEntry entry = m_undoStack.takeLast();
    if (!applySnapshot(entry.before, error))
    {
        m_undoStack.append(std::move(entry));
        return false;
    }

    if (!reload(error))
    {
        // Best effort: put the previous state back so the stack and the disk
        // stay consistent even though the reload failed.
        QString ignored;
        applySnapshot(entry.after, &ignored);
        reload(&ignored);
        m_undoStack.append(std::move(entry));
        return false;
    }

    // The restored state is on disk but not committed. Track it so the next
    // human save/flush writes it to git as the cold backup.
    for (auto it = entry.before.constBegin(); it != entry.before.constEnd(); ++it)
        notePath(it.key());

    m_redoStack.append(std::move(entry));
    trimStack(m_redoStack);
    return true;
}

bool ProjectStore::redo(QString* error)
{
    if (m_redoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可重做的操作"));
        return false;
    }

    UndoEntry entry = m_redoStack.takeLast();
    if (!applySnapshot(entry.after, error))
    {
        m_redoStack.append(std::move(entry));
        return false;
    }

    if (!reload(error))
    {
        QString ignored;
        applySnapshot(entry.before, &ignored);
        reload(&ignored);
        m_redoStack.append(std::move(entry));
        return false;
    }

    for (auto it = entry.after.constBegin(); it != entry.after.constEnd(); ++it)
        notePath(it.key());

    m_undoStack.append(std::move(entry));
    trimStack(m_undoStack);
    return true;
}

QVector<GitCommit> ProjectStore::history(int limit, QString* error) const
{
    if (!hasHead(m_root))
        return {};

    QStringList arguments{QStringLiteral("log"),
                          QStringLiteral("--format=%H%x1f%cI%x1f%s")};
    if (limit > 0)
        arguments << QStringLiteral("--max-count=%1").arg(limit);

    const ProcessResult result = runGit(m_root, arguments);
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git log 失败：%1").arg(processFailure(result)));
        return {};
    }

    QVector<GitCommit> commits;
    const QStringList lines =
        result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines)
    {
        const int firstSeparator = line.indexOf(QChar(0x1f));
        const int secondSeparator =
            firstSeparator >= 0 ? line.indexOf(QChar(0x1f), firstSeparator + 1)
                                : -1;
        if (firstSeparator <= 0 || secondSeparator < 0)
        {
            setError(error,
                     QStringLiteral("无法解析 git log 输出：%1").arg(line));
            return {};
        }

        GitCommit commit;
        commit.hash = line.left(firstSeparator);
        commit.time = QDateTime::fromString(
            line.mid(firstSeparator + 1, secondSeparator - firstSeparator - 1),
            Qt::ISODate);
        commit.message = line.mid(secondSeparator + 1);
        commits.append(commit);
    }
    return commits;
}

QVector<DirtyText> ProjectStore::dirtyTextFiles(QString* error) const
{
    QVector<DirtyText> files;
    const QVector<RawDirtyText> entries = scanRawDirtyText(m_root, error);
    files.reserve(entries.size());
    for (const RawDirtyText& entry : entries)
    {
        if (isProjectTextPath(entry.path))
        {
            files.append(DirtyText{entry.path,
                                   porcelainStatusName(entry.index,
                                                       entry.worktree)});
        }
        // A rename/copy deletes the source path; include it so recovery covers
        // both sides of the move.
        if (!entry.sourcePath.isEmpty()
            && isProjectTextPath(entry.sourcePath))
        {
            files.append(
                DirtyText{entry.sourcePath, QStringLiteral("deleted")});
        }
    }

    std::sort(files.begin(), files.end(),
              [](const DirtyText& a, const DirtyText& b) {
                  return a.path < b.path;
              });
    return files;
}

bool ProjectStore::commitRecoveredText(QString* error)
{
    QString scanError;
    const QVector<DirtyText> dirty = dirtyTextFiles(&scanError);
    if (!scanError.isEmpty())
    {
        setError(error, scanError);
        return false;
    }

    QStringList paths;
    paths.reserve(dirty.size());
    for (const DirtyText& item : dirty)
    {
        if (!paths.contains(item.path))
            paths << item.path;
    }
    if (paths.isEmpty())
    {
        m_dirtyText.clear();
        return true;
    }

    // Untracked files must enter the index before `commit --only` can name
    // them; tracked deletions are picked up by -A as well.
    QStringList addArguments{QStringLiteral("add"), QStringLiteral("-A"),
                             QStringLiteral("--")};
    addArguments += paths;
    const ProcessResult add = runGit(m_root, addArguments);
    if (add.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git add 失败：%1").arg(processFailure(add)));
        return false;
    }

    QString hash;
    if (!commitOnlyPaths(m_root, QStringLiteral("pvf: recover"), paths, &hash,
                         error))
    {
        return false;
    }

    m_dirtyText.clear();
    return true;
}

bool ProjectStore::discardDirtyText(QString* error)
{
    QString scanError;
    const QVector<RawDirtyText> entries = scanRawDirtyText(m_root, &scanError);
    if (!scanError.isEmpty())
    {
        setError(error, scanError);
        return false;
    }

    // A crash between `git add` and `git commit` leaves text changes in the
    // index. Reset only our text paths first so the index matches HEAD:
    // tracked files can then be checked out, staged additions become
    // untracked and get deleted.
    if (hasHead(m_root))
    {
        QStringList staged;
        for (const RawDirtyText& entry : entries)
        {
            if (isProjectTextPath(entry.path)
                && entry.index != QLatin1Char(' ')
                && entry.index != QLatin1Char('?'))
            {
                staged << entry.path;
            }
            if (!entry.sourcePath.isEmpty()
                && isProjectTextPath(entry.sourcePath))
            {
                staged << entry.sourcePath;
            }
        }

        if (!staged.isEmpty())
        {
            QStringList arguments{QStringLiteral("reset"), QStringLiteral("-q"),
                                  QStringLiteral("--")};
            arguments += staged;
            const ProcessResult reset = runGit(m_root, arguments);
            if (reset.exitCode != 0)
            {
                setError(error,
                         QStringLiteral("git reset 失败：%1")
                             .arg(processFailure(reset)));
                return false;
            }
        }
    }

    for (const RawDirtyText& entry : entries)
    {
        QStringList paths{entry.path};
        if (!entry.sourcePath.isEmpty())
            paths << entry.sourcePath;

        for (const QString& relativePath : std::as_const(paths))
        {
            if (!isProjectTextPath(relativePath))
                continue;

            if (isTracked(m_root, relativePath))
            {
                const ProcessResult checkout = runGit(
                    m_root, {QStringLiteral("checkout"), QStringLiteral("--"),
                             relativePath});
                if (checkout.exitCode != 0)
                {
                    setError(error,
                             QStringLiteral("git checkout 失败：%1")
                                 .arg(processFailure(checkout)));
                    return false;
                }
                continue;
            }

            const QString path = absolutePath(m_root, relativePath);
            const QFileInfo info(path);
            if (!info.exists())
                continue;
            const bool removed = info.isDir()
                                     ? QDir(path).removeRecursively()
                                     : QFile::remove(path);
            if (!removed)
            {
                setError(error,
                         QStringLiteral("无法删除未提交文件 %1").arg(path));
                return false;
            }
        }
    }

    clearDirty();
    m_dirtyText.clear();
    return true;
}

bool ProjectStore::reloadAfterDiscard(QString* error)
{
    return reload(error);
}

void ProjectStore::refreshDirtyText()
{
    QString ignored;
    m_dirtyText = dirtyTextFiles(&ignored);
}

bool ProjectStore::reload(QString* error)
{
    model::Project project;
    if (!loadProjectData(m_root, &project, error))
        return false;

    m_project = std::move(project);
    clearDirty();
    refreshDirtyText();
    return true;
}

void ProjectStore::setUndoLimitsForTest(int maxEntries, qint64 maxBytes)
{
    m_undoLimit = std::max(1, maxEntries);
    m_undoByteLimit = std::max<qint64>(1, maxBytes);
    trimStack(m_undoStack);
    trimStack(m_redoStack);
}

void ProjectStore::notePath(const QString& relativePath)
{
    if (m_txnBefore.contains(relativePath))
        return;
    m_txnBefore.insert(relativePath,
                       readFileBytes(absolutePath(m_root, relativePath)));
    m_txnPaths.insert(relativePath);
}

void ProjectStore::pushUndo(UndoEntry entry)
{
    m_undoStack.append(std::move(entry));
    m_redoStack.clear();
    trimStack(m_undoStack);
}

void ProjectStore::trimStack(QVector<UndoEntry>& stack)
{
    const auto cost = [](const UndoEntry& entry) {
        qint64 bytes = 0;
        for (const QByteArray& before : entry.before)
            bytes += before.size();
        for (const QByteArray& after : entry.after)
            bytes += after.size();
        return bytes;
    };

    qint64 total = 0;
    for (const UndoEntry& entry : std::as_const(stack))
        total += cost(entry);

    while (!stack.isEmpty()
           && (stack.size() > m_undoLimit || total > m_undoByteLimit))
    {
        total -= cost(stack.first());
        stack.removeFirst();
    }
}

bool ProjectStore::applySnapshot(const QHash<QString, QByteArray>& snapshot,
                                 QString* error)
{
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
    {
        const QString path = absolutePath(m_root, it.key());
        if (it.value().isEmpty())
        {
            QFile file(path);
            if (file.exists() && !file.remove())
            {
                setError(error,
                         QStringLiteral("无法删除 %1：%2")
                             .arg(path, file.errorString()));
                return false;
            }
            continue;
        }

        if (!writeBytesFile(path, it.value(), error))
            return false;
    }
    return true;
}

void ProjectStore::clearDirty()
{
    m_txnPaths.clear();
    m_txnBefore.clear();
}

} // namespace pvf::store
