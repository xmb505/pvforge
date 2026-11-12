#include "Store.h"

#include "model/Migrations.h"

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
#include <optional>
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

// One text document (project.json or entities/<uuid>.json): read its version,
// run the production migration chain up to the current format version, and
// return the document the strict parser expects. Nothing is written to disk,
// so a document this build cannot read stays byte-identical.
bool migrateTextDocument(const QString& path, const QString& label,
                         const QString& uuid, const QJsonObject& input,
                         QJsonObject* output, QString* error)
{
    const QString subject =
        uuid.isEmpty()
            ? path
            : QStringLiteral("%1（uuid %2）").arg(path, uuid);

    QString versionError;
    const std::optional<int> version =
        model::migrations::documentVersion(input, &versionError);
    if (!version)
    {
        setError(error,
                 QStringLiteral("%1 缺少整数 version（当前格式版本 %2）：%3")
                     .arg(subject)
                     .arg(model::kProjectFormatVersion)
                     .arg(versionError));
        return false;
    }

    if (*version > model::kProjectFormatVersion)
    {
        setError(error,
                 QStringLiteral("%1 由更新版本的 PVForge 保存（%2版本 %3，"
                                "本程序支持到版本 %4）。请升级 PVForge 后再打开；"
                                "原文件不会被修改。")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::kProjectFormatVersion));
        return false;
    }

    if (*version < model::migrations::minimumSupportedVersion())
    {
        setError(error,
                 QStringLiteral("%1 的%2版本 %3 过旧（本程序最低支持版本 %4）")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::migrations::minimumSupportedVersion()));
        return false;
    }

    if (*version == model::kProjectFormatVersion)
    {
        *output = input;
        return true;
    }

    QString migrationError;
    const auto upgraded = model::migrations::upgrade(
        input, *version, model::kProjectFormatVersion, &migrationError);
    if (!upgraded)
    {
        setError(error,
                 QStringLiteral("%1 的%2版本 %3 无法升级到版本 %4：%5")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::kProjectFormatVersion)
                     .arg(migrationError));
        return false;
    }

    *output = *upgraded;
    return true;
}

bool loadProjectData(const QString& root, model::Project* project,
                     QString* error)
{
    const QString projectPath = absolutePath(root, QStringLiteral("project.json"));
    QJsonObject object;
    if (!readJsonObject(projectPath, &object, error))
        return false;

    const QJsonValue formatValue = object.value(QStringLiteral("format"));
    if (!formatValue.isString())
    {
        setError(error,
                 QStringLiteral("%1 缺少 format 字段（应为 %2）")
                     .arg(projectPath, QLatin1String(model::kProjectFormat)));
        return false;
    }
    if (formatValue.toString() != QLatin1String(model::kProjectFormat))
    {
        setError(error,
                 QStringLiteral("%1 不是 PVForge 工程（format=%2）")
                     .arg(projectPath, formatValue.toString()));
        return false;
    }

    QJsonObject document;
    if (!migrateTextDocument(projectPath, QStringLiteral("工程"), QString(),
                             object, &document, error))
    {
        return false;
    }

    // Opening a project is strict: a missing required field is an error that
    // names its JSON path instead of silently becoming a default.
    model::ParseOptions options;
    options.mode = model::ParseMode::Strict;

    QString parseError;
    const auto parsed = model::projectFromJson(document, options, &parseError);
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

            QJsonObject entityDocument;
            if (!migrateTextDocument(fileInfo.absoluteFilePath(),
                                     QStringLiteral("实体"), uuid, entityObject,
                                     &entityDocument, error))
            {
                return false;
            }

            QString entityError;
            const auto entity =
                model::entityFromJson(entityDocument, options, &entityError);
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
                          QStringLiteral("--allow-empty"), QStringLiteral("-m"),
                          message, QStringLiteral("--")};
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

bool resolveCommit(const QString& root, const QString& hash, QString* fullHash,
                   QString* error)
{
    const QString requested = hash.trimmed();
    if (requested.isEmpty())
    {
        setError(error, QStringLiteral("无效的提交：hash 不能为空"));
        return false;
    }
    // Keep a revision from being parsed as a git option.
    if (requested.startsWith(QLatin1Char('-')))
    {
        setError(error, QStringLiteral("无效的提交：%1").arg(hash));
        return false;
    }

    const ProcessResult type = runGit(
        root, {QStringLiteral("cat-file"), QStringLiteral("-t"), requested});
    if (type.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("无效的提交：%1（%2）")
                     .arg(requested, processFailure(type)));
        return false;
    }

    const QString objectType = type.out.trimmed();
    if (objectType != QLatin1String("commit"))
    {
        setError(error,
                 QStringLiteral("无效的提交：%1 不是 commit（类型：%2）")
                     .arg(requested, objectType));
        return false;
    }

    const ProcessResult resolved = runGit(
        root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
               requested + QStringLiteral("^{commit}")});
    if (resolved.exitCode != 0 || resolved.out.trimmed().isEmpty())
    {
        setError(error,
                 QStringLiteral("无效的提交：%1（%2）")
                     .arg(requested, processFailure(resolved)));
        return false;
    }

    *fullHash = resolved.out.trimmed();
    return true;
}

bool listTreeFiles(const QString& root, const QString& commit,
                   const QStringList& paths, QStringList* files, QString* error)
{
    QStringList arguments{QStringLiteral("ls-tree"), QStringLiteral("-r"),
                          QStringLiteral("--name-only"), QStringLiteral("-z"),
                          commit, QStringLiteral("--")};
    arguments += paths;
    const ProcessResult result = runGit(root, arguments);
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git ls-tree 失败：%1")
                     .arg(processFailure(result)));
        return false;
    }
    *files = splitNul(result.out);
    return true;
}

bool listTrackedFiles(const QString& root, const QString& path,
                      QStringList* files, QString* error)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("ls-files"), QStringLiteral("-z"),
               QStringLiteral("--"), path});
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git ls-files 失败：%1")
                     .arg(processFailure(result)));
        return false;
    }
    *files = splitNul(result.out);
    return true;
}

// Text paths currently present on disk or in the index. Paths absent from
// both are omitted because `git add` rejects unmatched pathspecs.
QStringList textPathsForState(const QString& root)
{
    QStringList paths;
    const auto add = [&](const QString& relativePath) {
        if (QFileInfo::exists(absolutePath(root, relativePath))
            || isTracked(root, relativePath))
        {
            paths << relativePath;
        }
    };
    add(QStringLiteral("project.json"));
    // An empty entities/ directory is invisible to git.  Passing it as a
    // pathspec makes `git commit -- entities` fail with "pathspec did not
    // match any file", so only include it when it actually holds files (or is
    // already tracked).
    const QDir entitiesDir(absolutePath(root, QStringLiteral("entities")));
    const bool entitiesHaveFiles =
        entitiesDir.exists()
        && !entitiesDir
                .entryList(QDir::Files | QDir::Hidden | QDir::System
                               | QDir::NoDotAndDotDot)
                .isEmpty();
    if (entitiesHaveFiles || isTracked(root, QStringLiteral("entities")))
        paths << QStringLiteral("entities");
    add(QStringLiteral(".gitignore"));
    return paths;
}

bool stageTextPaths(const QString& root, QString* error)
{
    const QStringList paths = textPathsForState(root);
    if (paths.isEmpty())
        return true;

    QStringList arguments{QStringLiteral("add"), QStringLiteral("-A"),
                          QStringLiteral("--")};
    arguments += paths;
    const ProcessResult add = runGit(root, arguments);
    if (add.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git add 失败：%1").arg(processFailure(add)));
        return false;
    }
    return true;
}

// Makes the working tree of project.json, entities/*.json and .gitignore match
// `commit`. Entity files absent from the target are deleted; files outside the
// text project paths are never touched. Does not stage or commit.
bool syncTextTreeToCommit(const QString& root, const QString& commit,
                          QString* error)
{
    QStringList targetFiles;
    if (!listTreeFiles(root, commit,
                       {QStringLiteral("project.json"),
                        QStringLiteral("entities"),
                        QStringLiteral(".gitignore")},
                       &targetFiles, error))
    {
        return false;
    }

    bool hasProject = false;
    bool hasEntities = false;
    bool hasIgnore = false;
    QSet<QString> targetSet;
    for (const QString& path : std::as_const(targetFiles))
    {
        targetSet.insert(path);
        if (path == QLatin1String("project.json"))
            hasProject = true;
        else if (path == QLatin1String(".gitignore"))
            hasIgnore = true;
        else if (path.startsWith(QLatin1String("entities/")))
            hasEntities = true;
    }

    if (!hasProject)
    {
        setError(error,
                 QStringLiteral("目标提交缺少 project.json：%1").arg(commit));
        return false;
    }

    QStringList checkoutPaths{QStringLiteral("project.json")};
    if (hasEntities)
        checkoutPaths << QStringLiteral("entities");
    if (hasIgnore)
        checkoutPaths << QStringLiteral(".gitignore");

    QStringList arguments{QStringLiteral("checkout"), commit, QStringLiteral("--")};
    arguments += checkoutPaths;
    const ProcessResult checkout = runGit(root, arguments);
    if (checkout.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git checkout 失败：%1")
                     .arg(processFailure(checkout)));
        return false;
    }

    QStringList currentEntities;
    if (!listTrackedFiles(root, QStringLiteral("entities"), &currentEntities,
                          error))
    {
        return false;
    }

    for (const QString& relativePath : std::as_const(currentEntities))
    {
        if (!relativePath.startsWith(QLatin1String("entities/"))
            || !relativePath.endsWith(QLatin1String(".json"))
            || targetSet.contains(relativePath))
        {
            continue;
        }

        const QString path = absolutePath(root, relativePath);
        if (QFileInfo::exists(path) && !QFile::remove(path))
        {
            setError(error,
                     QStringLiteral("无法删除目标提交中不存在的实体 %1").arg(path));
            return false;
        }
    }

    if (!hasIgnore)
    {
        const QString path = absolutePath(root, QStringLiteral(".gitignore"));
        if (QFileInfo::exists(path) && !QFile::remove(path))
        {
            setError(error, QStringLiteral("无法删除 %1").arg(path));
            return false;
        }
    }
    return true;
}

// Syncs back to `commit` and stages the result, restoring both the working
// tree and the index to the pre-failure state. Used when a restore fails
// after it already changed files.
bool restoreTextTreeToCommit(const QString& root, const QString& commit,
                             QString* error)
{
    if (!syncTextTreeToCommit(root, commit, error))
        return false;
    return stageTextPaths(root, error);
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

bool ProjectStore::gitBinaryAvailable(QString* version)
{
    const ProcessResult git = runProcess(QStringLiteral("git"),
                                         {QStringLiteral("--version")});
    if (!git.started || git.exitCode != 0)
    {
        if (version)
            version->clear();
        return false;
    }
    if (version)
        *version = git.out.trimmed();
    return true;
}

std::optional<ProjectStore> ProjectStore::create(
    const QString& projectDir, const model::ProjectSettings& settings,
    QString* error, bool useGit)
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

    // Missing git is not an error: the project is still created as a plain
    // text store with in-memory undo, just like create(useGit=false).
    const bool useRepo = useGit && gitBinaryAvailable();

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

    if (useRepo)
    {
        const ProcessResult init =
            runGit(root, {QStringLiteral("init")});
        if (init.exitCode != 0)
        {
            setError(error,
                     QStringLiteral("git init 失败：%1")
                         .arg(processFailure(init)));
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
    }

    ProjectStore store(root, std::move(initialProject));
    store.m_gitEnabled = useRepo;
    return std::move(store);
}

std::optional<ProjectStore> ProjectStore::open(const QString& projectDir,
                                               QString* error, bool useGit)
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

    // Missing git or a missing repository is not an error: the project opens
    // as a plain text store. Only explicit useGit=false skips the probe.
    const bool useRepo =
        useGit
        && gitBinaryAvailable()
        && QFileInfo::exists(absolutePath(root, QStringLiteral(".git")));

    model::Project project;
    if (!loadProjectData(root, &project, error))
        return std::nullopt;

    // The in-memory undo/redo history starts empty after opening a project;
    // across sessions git history is the cold backup.
    ProjectStore store(root, std::move(project));
    store.m_gitEnabled = useRepo;

    // Scan once so the caller can offer recover/discard. A transient git
    // failure (for example a stale index.lock) must not block opening; the
    // caller can rescan with dirtyTextFiles().
    if (store.m_gitEnabled)
        store.refreshDirtyText();
    return std::move(store);
}

bool ProjectStore::setGitEnabled(bool enabled, QString* error)
{
    if (!enabled)
    {
        m_gitEnabled = false;
        m_dirtyText.clear();
        return true;
    }

    if (!checkGit(error))
        return false;

    const QString gitDirectory =
        absolutePath(m_root, QStringLiteral(".git"));
    if (m_gitEnabled && QFileInfo::exists(gitDirectory))
    {
        refreshDirtyText();
        return true;
    }

    if (!QFileInfo::exists(gitDirectory))
    {
        const QString ignorePath =
            absolutePath(m_root, QStringLiteral(".gitignore"));
        if (!QFileInfo::exists(ignorePath)
            && !writeTextFile(ignorePath, QString::fromLatin1(kGitIgnore), error))
        {
            return false;
        }

        const ProcessResult init = runGit(m_root, {QStringLiteral("init")});
        if (init.exitCode != 0)
        {
            setError(error,
                     QStringLiteral("git init 失败：%1")
                         .arg(processFailure(init)));
            return false;
        }

        if (!ensureGitIdentity(m_root, error))
            return false;

        QStringList paths{QStringLiteral(".gitignore"),
                          QStringLiteral("project.json")};
        const QFileInfoList entityFiles =
            QDir(absolutePath(m_root, QStringLiteral("entities")))
                .entryInfoList({QStringLiteral("*.json")}, QDir::Files,
                               QDir::Name);
        for (const QFileInfo& fileInfo : entityFiles)
            paths << QStringLiteral("entities/") + fileInfo.fileName();

        QStringList addArguments{QStringLiteral("add"), QStringLiteral("--")};
        addArguments += paths;
        const ProcessResult add = runGit(m_root, addArguments);
        if (add.exitCode != 0)
        {
            setError(error,
                     QStringLiteral("git add 初始文件失败：%1")
                         .arg(processFailure(add)));
            return false;
        }

        QString hash;
        if (!commitOnlyPaths(m_root, QStringLiteral("pvf: init"), paths, &hash,
                             error))
        {
            return false;
        }
    }

    m_gitEnabled = true;
    refreshDirtyText();
    return true;
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

    // Without git the commit is purely an in-memory undo step.
    if (!m_gitEnabled)
    {
        recordTxnUndo(normalized);
        clearDirty();
        return true;
    }

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
    recordTxnUndo(normalized);

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
    if (!m_gitEnabled)
    {
        setError(error, QStringLiteral("Git 未启用"));
        return {};
    }
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

bool ProjectStore::restoreCommit(const QString& hash, QString* error)
{
    if (!m_gitEnabled)
    {
        setError(error, QStringLiteral("Git 未启用"));
        return false;
    }

    QString commit;
    if (!resolveCommit(m_root, hash, &commit, error))
        return false;

    QString scanError;
    const QVector<DirtyText> dirty = dirtyTextFiles(&scanError);
    if (!scanError.isEmpty())
    {
        setError(error, scanError);
        return false;
    }
    if (!dirty.isEmpty())
    {
        m_dirtyText = dirty;
        QStringList paths;
        paths.reserve(dirty.size());
        for (const DirtyText& item : dirty)
            paths << QStringLiteral("%1（%2）").arg(item.path, item.status);
        setError(error,
                 QStringLiteral("工作区有未提交的文本改动，无法恢复：%1。"
                                "请先提交或丢弃这些改动。")
                     .arg(paths.join(QStringLiteral("、"))));
        return false;
    }

    const QString originalHead = headHash(m_root);
    if (originalHead.isEmpty())
    {
        setError(error, QStringLiteral("工程没有 HEAD 提交，无法恢复"));
        return false;
    }

    // Any failure after this point restores the pre-call text tree and index,
    // which is exactly the state the clean-worktree check above observed.
    const auto rollback = [&](const QString& reason) {
        QString rollbackError;
        if (restoreTextTreeToCommit(m_root, originalHead, &rollbackError))
            return reason;
        return reason
               + QStringLiteral("；回滚到原 HEAD 失败：%1").arg(rollbackError);
    };

    QString syncError;
    if (!syncTextTreeToCommit(m_root, commit, &syncError))
    {
        setError(error, rollback(syncError));
        return false;
    }

    // Validate the target data before creating a commit: a restore into an
    // unreadable project must leave the repository untouched.
    model::Project check;
    QString loadError;
    if (!loadProjectData(m_root, &check, &loadError))
    {
        setError(error,
                 rollback(QStringLiteral("目标提交的工程数据无效：%1").arg(loadError)));
        return false;
    }

    QString stageError;
    if (!stageTextPaths(m_root, &stageError))
    {
        setError(error, rollback(stageError));
        return false;
    }

    const QString message =
        QStringLiteral("pvf: restore %1").arg(commit.left(7));
    QString newHash;
    QString commitError;
    if (!commitOnlyPaths(m_root, message, textPathsForState(m_root), &newHash,
                         &commitError))
    {
        setError(error, rollback(commitError));
        return false;
    }

    clearDirty();
    m_undoStack.clear();
    m_redoStack.clear();

    QString reloadError;
    if (!reload(&reloadError))
    {
        setError(error,
                 QStringLiteral("恢复已提交（%1），但重新加载工程失败：%2")
                     .arg(newHash.left(7), reloadError));
        return false;
    }
    return true;
}

QVector<DirtyText> ProjectStore::dirtyTextFiles(QString* error) const
{
    QVector<DirtyText> files;
    if (!m_gitEnabled)
        return files; // No repository, so nothing can be dirty.

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
    if (!m_gitEnabled)
    {
        setError(error, QStringLiteral("Git 未启用"));
        return false;
    }

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
    if (!m_gitEnabled)
    {
        setError(error, QStringLiteral("Git 未启用"));
        return false;
    }

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
    if (!m_gitEnabled)
    {
        m_dirtyText.clear();
        return;
    }
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

void ProjectStore::recordTxnUndo(const QString& message)
{
    QStringList candidates = m_txnPaths.values();
    std::sort(candidates.begin(), candidates.end());

    UndoEntry entry;
    entry.message = message;
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
