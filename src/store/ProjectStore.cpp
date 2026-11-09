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
constexpr char kUndoPrefix[] = "pvf: undo ";
constexpr char kRedoPrefix[] = "pvf: redo ";

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

bool checkGitTools(QString* error)
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

    const ProcessResult lfs = runProcess(QStringLiteral("git"),
                                         {QStringLiteral("lfs"),
                                          QStringLiteral("version")});
    if (!lfs.started || lfs.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("找不到 git-lfs，请安装 Git LFS 后重试（%1）")
                     .arg(processFailure(lfs)));
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

bool writeTextFile(const QString& path, const QString& content, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }

    const QByteArray bytes = content.toUtf8();
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

bool changedPathsOfCommit(const QString& root, const QString& hash,
                          QStringList* paths, QString* error)
{
    const ProcessResult result = runGit(
        root, {QStringLiteral("diff-tree"), QStringLiteral("--no-commit-id"),
               QStringLiteral("--name-only"), QStringLiteral("-r"),
               QStringLiteral("--root"), QStringLiteral("-z"), hash});
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git diff-tree 失败：%1")
                     .arg(processFailure(result)));
        return false;
    }
    *paths = splitNul(result.out);
    if (paths->isEmpty())
    {
        setError(error, QStringLiteral("提交 %1 没有可回退的文件").arg(hash));
        return false;
    }
    return true;
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

const char kGitAttributes[] = R"(# PVForge media is stored in Git LFS.
*.png filter=lfs diff=lfs merge=lfs -text
*.jpg filter=lfs diff=lfs merge=lfs -text
*.jpeg filter=lfs diff=lfs merge=lfs -text
*.jfif filter=lfs diff=lfs merge=lfs -text
*.gif filter=lfs diff=lfs merge=lfs -text
*.bmp filter=lfs diff=lfs merge=lfs -text
*.webp filter=lfs diff=lfs merge=lfs -text
*.tif filter=lfs diff=lfs merge=lfs -text
*.tiff filter=lfs diff=lfs merge=lfs -text
*.avif filter=lfs diff=lfs merge=lfs -text
*.heic filter=lfs diff=lfs merge=lfs -text
*.heif filter=lfs diff=lfs merge=lfs -text
*.mp4 filter=lfs diff=lfs merge=lfs -text
*.m4v filter=lfs diff=lfs merge=lfs -text
*.mov filter=lfs diff=lfs merge=lfs -text
*.webm filter=lfs diff=lfs merge=lfs -text
*.mkv filter=lfs diff=lfs merge=lfs -text
*.avi filter=lfs diff=lfs merge=lfs -text
*.mpg filter=lfs diff=lfs merge=lfs -text
*.mpeg filter=lfs diff=lfs merge=lfs -text
*.wmv filter=lfs diff=lfs merge=lfs -text
*.flv filter=lfs diff=lfs merge=lfs -text
*.mp3 filter=lfs diff=lfs merge=lfs -text
*.wav filter=lfs diff=lfs merge=lfs -text
*.ogg filter=lfs diff=lfs merge=lfs -text
*.oga filter=lfs diff=lfs merge=lfs -text
*.flac filter=lfs diff=lfs merge=lfs -text
*.m4a filter=lfs diff=lfs merge=lfs -text
*.aac filter=lfs diff=lfs merge=lfs -text
*.opus filter=lfs diff=lfs merge=lfs -text
*.aif filter=lfs diff=lfs merge=lfs -text
*.aiff filter=lfs diff=lfs merge=lfs -text
*.wma filter=lfs diff=lfs merge=lfs -text
# Case variants can appear inside imported HTML bundles.
*.PNG filter=lfs diff=lfs merge=lfs -text
*.JPG filter=lfs diff=lfs merge=lfs -text
*.JPEG filter=lfs diff=lfs merge=lfs -text
*.GIF filter=lfs diff=lfs merge=lfs -text
*.BMP filter=lfs diff=lfs merge=lfs -text
*.WEBP filter=lfs diff=lfs merge=lfs -text
*.TIFF filter=lfs diff=lfs merge=lfs -text
*.TIF filter=lfs diff=lfs merge=lfs -text
*.AVIF filter=lfs diff=lfs merge=lfs -text
*.HEIC filter=lfs diff=lfs merge=lfs -text
*.HEIF filter=lfs diff=lfs merge=lfs -text
*.MP4 filter=lfs diff=lfs merge=lfs -text
*.M4V filter=lfs diff=lfs merge=lfs -text
*.MOV filter=lfs diff=lfs merge=lfs -text
*.WEBM filter=lfs diff=lfs merge=lfs -text
*.MKV filter=lfs diff=lfs merge=lfs -text
*.AVI filter=lfs diff=lfs merge=lfs -text
*.MPG filter=lfs diff=lfs merge=lfs -text
*.MPEG filter=lfs diff=lfs merge=lfs -text
*.WMV filter=lfs diff=lfs merge=lfs -text
*.FLV filter=lfs diff=lfs merge=lfs -text
*.MP3 filter=lfs diff=lfs merge=lfs -text
*.WAV filter=lfs diff=lfs merge=lfs -text
*.OGG filter=lfs diff=lfs merge=lfs -text
*.OGA filter=lfs diff=lfs merge=lfs -text
*.FLAC filter=lfs diff=lfs merge=lfs -text
*.M4A filter=lfs diff=lfs merge=lfs -text
*.AAC filter=lfs diff=lfs merge=lfs -text
*.OPUS filter=lfs diff=lfs merge=lfs -text
*.AIF filter=lfs diff=lfs merge=lfs -text
*.AIFF filter=lfs diff=lfs merge=lfs -text
*.WMA filter=lfs diff=lfs merge=lfs -text
)";

const char kGitIgnore[] = R"(# PVForge cache and temporary files.
.pvforge/
*.tmp
*.temp
*.part
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

    if (!checkGitTools(error))
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
        || !writeTextFile(absolutePath(root, QStringLiteral(".gitattributes")),
                          QString::fromLatin1(kGitAttributes), error)
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

    const ProcessResult lfsInstall = runGit(
        root, {QStringLiteral("lfs"), QStringLiteral("install"),
               QStringLiteral("--local")});
    if (lfsInstall.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git lfs install --local 失败：%1")
                     .arg(processFailure(lfsInstall)));
        return std::nullopt;
    }

    const ProcessResult add = runGit(
        root, {QStringLiteral("add"), QStringLiteral("--"),
               QStringLiteral(".gitattributes"), QStringLiteral(".gitignore"),
               QStringLiteral("project.json")});
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
            {QStringLiteral(".gitattributes"), QStringLiteral(".gitignore"),
             QStringLiteral("project.json")},
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

    if (!checkGitTools(error))
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

    ProjectStore store(root, std::move(project));
    if (!store.rebuildStacks(error))
        return std::nullopt;
    return store;
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
    if (!writeJsonObject(absolutePath(m_root, relativePath),
                         model::toJson(*it), error))
        return false;

    m_dirtyEntities.insert(relativePath);
    return true;
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

    if (!writeJsonObject(absolutePath(m_root, QStringLiteral("project.json")),
                         model::toJson(m_project), error))
        return false;

    m_projectDirty = true;
    return true;
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
    QFile file(path);
    if (file.exists() && !file.remove())
    {
        setError(error,
                 QStringLiteral("无法删除 %1：%2").arg(path, file.errorString()));
        return false;
    }

    m_project.entities.erase(it);
    m_dirtyEntities.insert(relativePath);
    return true;
}

bool ProjectStore::commit(const QString& message, QString* error)
{
    const QString normalized = normalizedMessage(message);

    QStringList candidates;
    if (m_projectDirty)
        candidates << QStringLiteral("project.json");

    QStringList dirtyEntities = m_dirtyEntities.values();
    std::sort(dirtyEntities.begin(), dirtyEntities.end());
    candidates += dirtyEntities;

    if (QFileInfo(absolutePath(m_root, QStringLiteral("assets"))).isDir())
        candidates << QStringLiteral("assets");

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

    m_undoStack.append(hash);
    m_redoStack.clear();
    clearDirty();
    return true;
}

bool ProjectStore::undo(QString* error)
{
    if (m_undoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可撤销的提交"));
        return false;
    }

    const QString operation = m_undoStack.constLast();
    QStringList operationPaths;
    if (!changedPathsOfCommit(m_root, operation, &operationPaths, error))
        return false;

    const ProcessResult revert = runGit(
        m_root, {QStringLiteral("revert"), QStringLiteral("--no-commit"),
                 operation});
    if (revert.exitCode != 0)
    {
        runGit(m_root, {QStringLiteral("revert"), QStringLiteral("--abort")});
        setError(error,
                 QStringLiteral("撤销失败（git revert）：%1")
                     .arg(processFailure(revert)));
        return false;
    }

    QString revertHash;
    if (!commitOnlyPaths(
            m_root, QString::fromLatin1(kUndoPrefix) + operation,
            operationPaths, &revertHash, error))
    {
        runGit(m_root, {QStringLiteral("revert"), QStringLiteral("--abort")});
        return false;
    }

    m_undoStack.removeLast();
    m_redoStack.append({operation, revertHash});
    return reload(error);
}

bool ProjectStore::redo(QString* error)
{
    if (m_redoStack.isEmpty())
    {
        setError(error, QStringLiteral("没有可重做的提交"));
        return false;
    }

    const RedoEntry entry = m_redoStack.constLast();
    QStringList revertedPaths;
    if (!changedPathsOfCommit(m_root, entry.revert, &revertedPaths, error))
        return false;

    const ProcessResult revert = runGit(
        m_root, {QStringLiteral("revert"), QStringLiteral("--no-commit"),
                 entry.revert});
    if (revert.exitCode != 0)
    {
        runGit(m_root, {QStringLiteral("revert"), QStringLiteral("--abort")});
        setError(error,
                 QStringLiteral("重做失败（git revert）：%1")
                     .arg(processFailure(revert)));
        return false;
    }

    QString redoHash;
    if (!commitOnlyPaths(
            m_root, QString::fromLatin1(kRedoPrefix) + entry.revert,
            revertedPaths, &redoHash, error))
    {
        runGit(m_root, {QStringLiteral("revert"), QStringLiteral("--abort")});
        return false;
    }

    m_redoStack.removeLast();
    m_undoStack.append(entry.operation);
    return reload(error);
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

bool ProjectStore::reload(QString* error)
{
    model::Project project;
    if (!loadProjectData(m_root, &project, error))
        return false;

    m_project = std::move(project);
    clearDirty();
    return true;
}

bool ProjectStore::rebuildStacks(QString* error)
{
    m_undoStack.clear();
    m_redoStack.clear();

    if (!hasHead(m_root))
        return true;

    const ProcessResult result = runGit(
        m_root, {QStringLiteral("log"), QStringLiteral("--reverse"),
                 QStringLiteral("--format=%H%x1f%s")});
    if (result.exitCode != 0)
    {
        setError(error,
                 QStringLiteral("git log 失败：%1").arg(processFailure(result)));
        return false;
    }

    const QStringList lines =
        result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines)
    {
        const int separator = line.indexOf(QChar(0x1f));
        if (separator <= 0)
            continue;

        const QString hash = line.left(separator);
        const QString message = line.mid(separator + 1);
        if (message == QLatin1String(kInitMessage))
            continue;

        if (message.startsWith(QLatin1String(kUndoPrefix)))
        {
            const QString operation = message.mid(
                int(sizeof(kUndoPrefix)) - 1).trimmed();
            const int index = m_undoStack.lastIndexOf(operation);
            if (index >= 0)
                m_undoStack.removeAt(index);
            m_redoStack.append({operation, hash});
        }
        else if (message.startsWith(QLatin1String(kRedoPrefix)))
        {
            const QString reverted = message.mid(
                int(sizeof(kRedoPrefix)) - 1).trimmed();
            if (!m_redoStack.isEmpty()
                && m_redoStack.constLast().revert == reverted)
            {
                const RedoEntry entry = m_redoStack.takeLast();
                m_undoStack.append(entry.operation);
            }
        }
        else if (message.startsWith(QStringLiteral("pvf:")))
        {
            m_undoStack.append(hash);
            m_redoStack.clear();
        }
    }
    return true;
}

void ProjectStore::clearDirty()
{
    m_dirtyEntities.clear();
    m_projectDirty = false;
}

} // namespace pvf::store
