#include "store/ProjectStoreSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace pvf::store::detail {

namespace {

constexpr char kRepositoryMarker[] = "pvforge-repo";
constexpr char kMarkerContents[] = "format=pvforge-git-repository\nversion=1\n";

// Git configuration keys that run project-controlled programs or redirect git
// at another repository/file. PVForge only ever writes user.name/user.email and
// the default init config; anything here means the repository was modified.
bool isForbiddenConfigKey(const QString& fullKey)
{
    static const QSet<QString> exact = {
        QStringLiteral("core.hookspath"),
        QStringLiteral("core.fsmonitor"),
        QStringLiteral("core.sshcommand"),
        QStringLiteral("core.editor"),
        QStringLiteral("core.pager"),
        QStringLiteral("core.askpass"),
        QStringLiteral("core.attributesfile"),
        QStringLiteral("sequence.editor"),
        QStringLiteral("diff.external"),
        QStringLiteral("protocol.ext.allow"),
        QStringLiteral("credential.helper"),
    };
    const QString key = fullKey.toLower();
    if (exact.contains(key))
        return true;
    return key.startsWith(QLatin1String("filter."))
           || key.startsWith(QLatin1String("include."))
           || key.startsWith(QLatin1String("includeif."))
           || key.startsWith(QLatin1String("alias."))
           || key.startsWith(QLatin1String("pager."))
           || key.startsWith(QLatin1String("credential."))
           || key.endsWith(QLatin1String(".textconv"))
           || (key.startsWith(QLatin1String("diff."))
               && key.endsWith(QLatin1String(".command")));
}

// Returns a forbidden key name found in one .gitattributes-style file, or an
// empty string. `filter=` and `diff=` values name external drivers and
// `textconv` opts into an external converter; all three can execute code.
QString forbiddenAttribute(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QString text = QString::fromUtf8(file.read(1024 * 1024)).toLower();
    static const QRegularExpression pattern(
        QStringLiteral("(?:^|[\\s;])(filter|diff)\\s*=|(?:^|[\\s;])textconv(?:\\s|$)"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(0).trimmed() : QString();
}

QString forbiddenConfigIn(const QString& configPath)
{
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QStringList lines =
        QString::fromUtf8(file.read(1024 * 1024)).split(QLatin1Char('\n'));
    QString section;
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';')))
        {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            // [section "subsection"] -> section.subsection
            const int quote = section.indexOf(QLatin1Char('"'));
            if (quote >= 0)
            {
                section = section.left(quote).trimmed() + QLatin1Char('.')
                          + section.mid(quote + 1).remove(QLatin1Char('"'));
            }
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        section = section.trimmed();
        const QString fullKey =
            section.isEmpty() ? key : section + QLatin1Char('.') + key;
        if (isForbiddenConfigKey(fullKey))
            return fullKey;
    }
    return QString();
}

QString canonicalOrEmpty(const QString& path)
{
    return QFileInfo(path).canonicalFilePath();
}

bool isInside(const QString& base, const QString& path)
{
    const QString normalizedBase = canonicalOrEmpty(base);
    const QString normalizedPath = canonicalOrEmpty(path);
    if (normalizedBase.isEmpty() || normalizedPath.isEmpty())
        return false;
    return normalizedPath == normalizedBase
           || normalizedPath.startsWith(normalizedBase + QLatin1Char('/'));
}

} // namespace

bool gitRepositoryIsTrusted(const QString& root, QString* reason)
{
    auto reject = [reason](const QString& message) {
        if (reason)
            *reason = message;
        return false;
    };

    const QString gitDir = absolutePath(root, QStringLiteral(".git"));
    const QFileInfo gitInfo(gitDir);
    if (!gitInfo.exists())
        return reject(QStringLiteral("工程没有 .git 仓库"));
    // A `.git` file is a `gitdir:` pointer: git would operate on a repository
    // outside the project. PVForge never creates that layout.
    if (gitInfo.isSymLink() || !gitInfo.isDir())
        return reject(QStringLiteral(".git 不是普通目录（疑似 gitdir 指针或符号链接）"));
    if (!isInside(root, gitDir))
        return reject(QStringLiteral(".git 指向工程目录之外"));

    const QString marker = absolutePath(gitDir, QString::fromLatin1(kRepositoryMarker));
    const QFileInfo markerInfo(marker);
    if (!markerInfo.exists() || !markerInfo.isFile() || markerInfo.isSymLink())
        return reject(QStringLiteral(
            ".git 不是由 PVForge 创建（缺少 .git/pvforge-repo 标记）"));

    const QString configPath = absolutePath(gitDir, QStringLiteral("config"));
    if (QFileInfo::exists(configPath))
    {
        const QFileInfo configInfo(configPath);
        if (configInfo.isSymLink() || !configInfo.isFile()
            || !isInside(gitDir, configPath))
        {
            return reject(QStringLiteral(".git/config 被替换或指向工程之外"));
        }
        const QString key = forbiddenConfigIn(configPath);
        if (!key.isEmpty())
        {
            return reject(QStringLiteral("仓库配置包含可执行项：%1").arg(key));
        }
    }

    const QString infoAttributes =
        absolutePath(gitDir, QStringLiteral("info/attributes"));
    if (QFileInfo::exists(infoAttributes))
    {
        const QString attribute = forbiddenAttribute(infoAttributes);
        if (!attribute.isEmpty())
        {
            return reject(QStringLiteral(".git/info/attributes 声明了外部过滤器：%1")
                              .arg(attribute));
        }
    }

    // .gitattributes is project content. It cannot define a filter itself, but
    // it can bind a path to a filter declared in the repository config, so any
    // filter/diff/textconv attribute is refused before git add runs.
    for (const QString& relative :
         {QStringLiteral(".gitattributes"), QStringLiteral("entities/.gitattributes")})
    {
        const QString path = absolutePath(root, relative);
        if (!QFileInfo::exists(path))
            continue;
        const QFileInfo attributeInfo(path);
        if (attributeInfo.isSymLink() || !isInside(root, path))
            return reject(QStringLiteral("%1 被替换或指向工程之外").arg(relative));
        const QString attribute = forbiddenAttribute(path);
        if (!attribute.isEmpty())
            return reject(QStringLiteral("%1 声明了外部过滤器：%2")
                              .arg(relative, attribute));
    }

    if (reason)
        reason->clear();
    return true;
}

bool markGitRepository(const QString& root, QString* error)
{
    const QString gitDir = absolutePath(root, QStringLiteral(".git"));
    if (!QFileInfo(gitDir).isDir())
    {
        setError(error, QStringLiteral("无法标记仓库：.git 目录不存在"));
        return false;
    }
    return writeTextFile(absolutePath(gitDir, QString::fromLatin1(kRepositoryMarker)),
                         QString::fromLatin1(kMarkerContents), error);
}

ProcessResult runProcess(const QString& program, const QStringList& arguments,
                         const QString& workDir)
{
    ProcessResult result;
    QProcess process;
    if (!workDir.isEmpty())
        process.setWorkingDirectory(workDir);

    // Never inherit repository redirection, injected config or an alternate
    // git/home directory from the caller. `git --version` is harmless but uses
    // the same environment for consistency.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QStringList inheritedKeys = environment.keys();
    for (const QString& key : inheritedKeys)
        if (key.startsWith(QLatin1String("GIT_")))
            environment.remove(key);
    environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), QStringLiteral("/dev/null"));
    environment.insert(QStringLiteral("GIT_CONFIG_SYSTEM"), QStringLiteral("/dev/null"));
    environment.insert(QStringLiteral("GIT_ATTR_NOSYSTEM"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
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
    // Global hardening applies to every invocation. Hooks and fsmonitor are
    // disabled outright; protocol.file.allow=never keeps a project-controlled
    // submodule/alternate from reading arbitrary files.
    QStringList fullArguments{
        QStringLiteral("--no-optional-locks"),
        QStringLiteral("-c"),
        QStringLiteral("core.hooksPath=/dev/null"),
        QStringLiteral("-c"),
        QStringLiteral("core.fsmonitor=false"),
        QStringLiteral("-c"),
        QStringLiteral("core.attributesFile=/dev/null"),
        QStringLiteral("-c"),
        QStringLiteral("protocol.file.allow=never"),
        QStringLiteral("-c"),
        QStringLiteral("protocol.ext.allow=never"),
        QStringLiteral("-c"),
        QStringLiteral("commit.gpgSign=false"),
        QStringLiteral("-c"),
        QStringLiteral("gc.auto=0"),
        QStringLiteral("-c"),
        QStringLiteral("maintenance.auto=false"),
        QStringLiteral("-c"),
        QStringLiteral("advice.detachedHead=false"),
        QStringLiteral("-C"),
        root,
    };
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
