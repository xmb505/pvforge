#include "store/ProjectStoreSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>

#include <algorithm>
#include <utility>

namespace pvf::store::detail {

ProcessResult runProcess(const QString& program, const QStringList& arguments,
                         const QString& workDir)
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

bool gitVersion(QString* version)
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

} // namespace pvf::store::detail
