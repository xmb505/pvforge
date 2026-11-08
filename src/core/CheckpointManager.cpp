#include "CheckpointManager.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cstdio>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pvf {

namespace {

constexpr int kMaxSummaryChars = 4096;
constexpr int kDefaultGitTimeoutMs = 60000;
constexpr int kLargeGitTimeoutMs = 180000;

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Checkpoints created in the same millisecond still need a stable order for
// list/prune. Keep timestamps strictly increasing inside this process.
qint64 nextCreatedMs()
{
    static std::atomic<qint64> last{0};
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 previous = last.load(std::memory_order_relaxed);
    while (now <= previous)
        now = previous + 1;
    while (!last.compare_exchange_weak(previous, now, std::memory_order_relaxed))
    {
        if (now <= previous)
            now = previous + 1;
    }
    return now;
}

QString canonicalProjectPath(const QString& projectPath)
{
    const QFileInfo info(projectPath);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return canonical;
    return QDir::cleanPath(info.absoluteFilePath());
}

QString projectDirectory(const QString& projectPath)
{
    return QFileInfo(canonicalProjectPath(projectPath)).absolutePath();
}

QString resolveProjectAsset(const QString& projectPath, const QString& stored)
{
    if (stored.isEmpty())
        return QString();
    if (QFileInfo(stored).isAbsolute())
        return QDir::cleanPath(stored);
    return QDir(projectDirectory(projectPath)).absoluteFilePath(stored);
}

bool ensurePrivateDir(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    if (info.exists())
    {
        if (!info.isDir() || info.isSymLink())
            return false;
    }
    else
    {
        QDir dir;
        if (!dir.mkpath(path))
            return false;
    }
    return QFile::setPermissions(
        path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void syncDirectory(const QString& path)
{
#if defined(Q_OS_UNIX)
    const QByteArray encoded = QFile::encodeName(path);
    const int fd = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
#else
    Q_UNUSED(path);
#endif
}

bool writeDurable(const QString& path, const QByteArray& data, QString* error)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!ensurePrivateDir(directory))
    {
        if (error)
            *error = QStringLiteral("无法创建检查点目录");
        return false;
    }

    const QString temporary =
        path + QStringLiteral(".tmp-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'))
              .left(12);
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    QFile::setPermissions(temporary,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(data) != data.size())
    {
        if (error)
            *error = file.errorString();
        file.close();
        QFile::remove(temporary);
        return false;
    }
    if (!file.flush())
    {
        if (error)
            *error = file.errorString();
        file.close();
        QFile::remove(temporary);
        return false;
    }
#if defined(Q_OS_UNIX)
    if (file.handle() >= 0)
        ::fsync(static_cast<int>(file.handle()));
#endif
    file.close();

#if defined(Q_OS_UNIX)
    if (::rename(QFile::encodeName(temporary).constData(),
                 QFile::encodeName(path).constData()) != 0)
#else
    QFile::remove(path);
    if (!QFile::rename(temporary, path))
#endif
    {
        if (error)
            *error = QStringLiteral("无法写入检查点文件");
        QFile::remove(temporary);
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    syncDirectory(directory);
    return true;
}

bool readFileBytes(const QString& path, QByteArray* data, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    *data = file.readAll();
    return true;
}

bool parseJsonObject(const QByteArray& json, QJsonObject* object, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = QStringLiteral("工程 JSON 无效: %1").arg(parseError.errorString());
        return false;
    }
    *object = document.object();
    return true;
}

QJsonObject readJsonObjectFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

bool isObjectId(const QString& value)
{
    if (value.size() != 40 && value.size() != 64)
        return false;
    for (const QChar character : value)
    {
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
            return false;
    }
    return true;
}

bool isSafeSnapshotId(const QString& id)
{
    if (id.isEmpty() || id.size() > 200 || id.contains(QStringLiteral("..")))
        return false;
    for (const QChar character : id)
    {
        const ushort code = character.unicode();
        const bool safe = (code >= '0' && code <= '9') || (code >= 'a' && code <= 'z')
                          || (code >= 'A' && code <= 'Z') || code == '.' || code == '_'
                          || code == '-';
        if (!safe)
            return false;
    }
    return true;
}

void collectAssetPaths(const QJsonValue& value, QStringList* paths, QSet<QString>* seen)
{
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        {
            if (it.key() == QStringLiteral("sourcePath") && it.value().isString())
            {
                const QString path = it.value().toString();
                if (!path.isEmpty() && !seen->contains(path))
                {
                    seen->insert(path);
                    paths->append(path);
                }
            }
            else if (it.key() == QStringLiteral("logos") && it.value().isArray())
            {
                for (const QJsonValue& entry : it.value().toArray())
                {
                    if (!entry.isString())
                        continue;
                    const QString path = entry.toString();
                    if (!path.isEmpty() && !seen->contains(path))
                    {
                        seen->insert(path);
                        paths->append(path);
                    }
                }
            }
            collectAssetPaths(it.value(), paths, seen);
        }
    }
    else if (value.isArray())
    {
        for (const QJsonValue& entry : value.toArray())
            collectAssetPaths(entry, paths, seen);
    }
}

QStringList sceneNames(const QJsonObject& root)
{
    QStringList names;
    for (const QJsonValue& value : root.value(QStringLiteral("scenes")).toArray())
    {
        if (!value.isObject())
            continue;
        const QJsonObject scene = value.toObject();
        const QString name = scene.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            names.append(name);
        else
        {
            const QString id = scene.value(QStringLiteral("id")).toString().trimmed();
            if (!id.isEmpty())
                names.append(id);
        }
    }
    return names;
}

QJsonObject buildMeta(const QString& projectPath, const QByteArray& projectJson,
                      const QJsonObject& root, const QString& uuid,
                      const QString& summary, const QString& backend, qint64 createdMs)
{
    QJsonObject meta;
    meta[QStringLiteral("format")] = QStringLiteral("pvforge-checkpoint");
    meta[QStringLiteral("version")] = 1;
    meta[QStringLiteral("backend")] = backend;
    meta[QStringLiteral("createdMs")] = createdMs;
    meta[QStringLiteral("created")] =
        QDateTime::fromMSecsSinceEpoch(createdMs, QTimeZone::UTC).toString(Qt::ISODateWithMs);
    meta[QStringLiteral("projectUuid")] = uuid;
    meta[QStringLiteral("summary")] = summary.left(kMaxSummaryChars);
    meta[QStringLiteral("pathHash")] = CheckpointManager::projectPathHash(projectPath);
    meta[QStringLiteral("fileSha256")] = sha256Hex(projectJson);
    meta[QStringLiteral("fileSize")] = projectJson.size();

    QJsonArray scenes;
    for (const QString& name : sceneNames(root))
        scenes.append(name);
    meta[QStringLiteral("scenes")] = scenes;

    QStringList paths;
    QSet<QString> seen;
    collectAssetPaths(root, &paths, &seen);
    QJsonArray assets;
    for (const QString& stored : paths)
    {
        QJsonObject asset;
        asset[QStringLiteral("path")] = stored;
        asset[QStringLiteral("exists")] =
            QFileInfo(resolveProjectAsset(projectPath, stored)).isFile();
        assets.append(asset);
    }
    meta[QStringLiteral("assets")] = assets;
    return meta;
}

bool fillInfoFromMeta(const QJsonObject& meta, const QString& projectPath,
                      CheckpointInfo* info)
{
    if (meta.value(QStringLiteral("format")).toString()
        != QLatin1String("pvforge-checkpoint"))
        return false;

    info->created = QDateTime::fromMSecsSinceEpoch(
        meta.value(QStringLiteral("createdMs")).toInteger(), QTimeZone::UTC);
    if (!info->created.isValid())
        info->created = QDateTime::fromString(
            meta.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    info->summary = meta.value(QStringLiteral("summary")).toString();
    info->projectUuid = meta.value(QStringLiteral("projectUuid")).toString();
    info->fileSha256 = meta.value(QStringLiteral("fileSha256")).toString();
    info->fileSize = meta.value(QStringLiteral("fileSize")).toInteger();

    for (const QJsonValue& value : meta.value(QStringLiteral("scenes")).toArray())
        if (value.isString())
            info->scenes.append(value.toString());

    for (const QJsonValue& value : meta.value(QStringLiteral("assets")).toArray())
    {
        if (!value.isObject())
            continue;
        const QJsonObject asset = value.toObject();
        CheckpointAssetRef reference;
        reference.path = asset.value(QStringLiteral("path")).toString();
        if (reference.path.isEmpty())
            continue;
        reference.exists =
            QFileInfo(resolveProjectAsset(projectPath, reference.path)).isFile();
        if (!reference.exists)
            info->missingAssets.append(reference.path);
        info->assets.append(reference);
    }
    return true;
}

QString gitErrorMessage(int exitCode, const QByteArray& stderrText,
                        const QString& failure)
{
    QString message = QString::fromLocal8Bit(stderrText).trimmed();
    if (message.isEmpty())
        message = failure;
    message = message.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (message.size() > 300)
        message.truncate(300);
    if (message.isEmpty())
        message = QStringLiteral("git 退出码 %1").arg(exitCode);
    return message;
}

class GitRunner
{
public:
    struct Blob
    {
        bool found = false;
        QByteArray data;
    };

    struct Result
    {
        bool ok = false;
        int exitCode = -1;
        QByteArray out;
        QByteArray err;
        QString failure;
    };

    GitRunner(QString executable, QString root)
        : m_executable(std::move(executable))
        , m_root(std::move(root))
    {
    }

    Result run(const QString& gitDir, const QStringList& command,
               const QByteArray& input = QByteArray(),
               int timeoutMs = kDefaultGitTimeoutMs) const
    {
        Result result;
        QProcess process;
        process.setProcessEnvironment(environment());
#if defined(Q_OS_UNIX)
        process.setChildProcessModifier([]() { ::umask(077); });
#endif
        QStringList arguments = globalArguments();
        if (!gitDir.isEmpty())
            arguments.append(QStringLiteral("--git-dir=") + gitDir);
        arguments.append(command);
        process.start(m_executable, arguments);
        if (!process.waitForStarted(5000))
        {
            result.failure =
                QStringLiteral("无法启动 git: %1").arg(process.errorString());
            return result;
        }
        if (!input.isEmpty())
            process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(timeoutMs))
        {
            process.kill();
            process.waitForFinished(2000);
            result.failure = QStringLiteral("git 命令超时");
            return result;
        }
        result.exitCode = process.exitCode();
        result.out = process.readAllStandardOutput();
        result.err = process.readAllStandardError();
        result.ok = process.exitStatus() == QProcess::NormalExit
                    && result.exitCode == 0;
        if (!result.ok)
            result.failure = gitErrorMessage(result.exitCode, result.err, process.errorString());
        return result;
    }

    // Reads several <commit>:<path> blobs in one git process. Missing objects
    // are returned with found == false instead of failing the whole batch.
    QVector<Blob> readBlobs(const QString& gitDir, const QStringList& specs) const
    {
        QVector<Blob> blobs;
        if (specs.isEmpty())
            return blobs;

        QByteArray input;
        for (const QString& spec : specs)
            input += spec.toUtf8() + '\n';
        const Result result =
            run(gitDir, {QStringLiteral("cat-file"), QStringLiteral("--batch")},
                input, kLargeGitTimeoutMs);
        blobs.resize(specs.size());
        if (!result.ok)
            return blobs;

        int position = 0;
        for (int index = 0; index < specs.size() && position < result.out.size(); ++index)
        {
            const int lineEnd = result.out.indexOf('\n', position);
            if (lineEnd < 0)
                break;
            const QByteArray header = result.out.mid(position, lineEnd - position);
            position = lineEnd + 1;
            const QList<QByteArray> parts = header.split(' ');
            if (parts.size() < 2 || parts.at(1) != QByteArrayLiteral("blob"))
                continue;
            bool sizeOk = false;
            const qint64 size = parts.size() >= 3 ? parts.at(2).toLongLong(&sizeOk) : -1;
            if (!sizeOk || size < 0 || position + size > result.out.size())
                break;
            blobs[index].found = true;
            blobs[index].data = result.out.mid(position, int(size));
            position += int(size);
            if (position < result.out.size() && result.out.at(position) == '\n')
                ++position;
        }
        return blobs;
    }

private:
    QProcessEnvironment environment() const
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        const QString home = m_root + QStringLiteral("/.git-home");
        ensurePrivateDir(home);
        ensurePrivateDir(home + QStringLiteral("/.config"));
        environment.insert(QStringLiteral("HOME"), home);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"),
                           home + QStringLiteral("/.config"));
        environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
        environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), QStringLiteral("/dev/null"));
        environment.insert(QStringLiteral("GIT_CONFIG_SYSTEM"), QStringLiteral("/dev/null"));
        environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
        environment.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
        environment.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));

        const QString signature = QStringLiteral("PVForge Checkpoint");
        const QString email = QStringLiteral("checkpoint@pvforge.invalid");
        environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), signature);
        environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), email);
        environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), signature);
        environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), email);
        const QString date =
            QStringLiteral("@%1 +0000").arg(QDateTime::currentSecsSinceEpoch());
        environment.insert(QStringLiteral("GIT_AUTHOR_DATE"), date);
        environment.insert(QStringLiteral("GIT_COMMITTER_DATE"), date);

        // Never let a caller's environment redirect git at another repository,
        // inject configuration, or run an alternate git implementation.
        const QStringList inherited = {
            QStringLiteral("GIT_DIR"),
            QStringLiteral("GIT_WORK_TREE"),
            QStringLiteral("GIT_INDEX_FILE"),
            QStringLiteral("GIT_OBJECT_DIRECTORY"),
            QStringLiteral("GIT_ALTERNATE_OBJECT_DIRECTORIES"),
            QStringLiteral("GIT_COMMON_DIR"),
            QStringLiteral("GIT_NAMESPACE"),
            QStringLiteral("GIT_CEILING_DIRECTORIES"),
            QStringLiteral("GIT_CONFIG"),
            QStringLiteral("GIT_CONFIG_COUNT"),
            QStringLiteral("GIT_CONFIG_PARAMETERS"),
            QStringLiteral("GIT_TEMPLATE_DIR"),
            QStringLiteral("GIT_EXEC_PATH"),
            QStringLiteral("GIT_SSH"),
            QStringLiteral("GIT_SSH_COMMAND"),
            QStringLiteral("GIT_ASKPASS"),
            QStringLiteral("SSH_ASKPASS"),
        };
        for (const QString& key : inherited)
            environment.remove(key);
        const QStringList keys = environment.keys();
        for (const QString& key : keys)
            if (key.startsWith(QStringLiteral("GIT_CONFIG_KEY_"))
                || key.startsWith(QStringLiteral("GIT_CONFIG_VALUE_")))
                environment.remove(key);
        return environment;
    }

    static QStringList globalArguments()
    {
        return {
            QStringLiteral("--no-optional-locks"),
            QStringLiteral("-c"),
            QStringLiteral("core.hooksPath=/dev/null"),
            QStringLiteral("-c"),
            QStringLiteral("core.fsmonitor=false"),
            QStringLiteral("-c"),
            QStringLiteral("protocol.file.allow=never"),
            QStringLiteral("-c"),
            QStringLiteral("advice.detachedHead=false"),
            QStringLiteral("-c"),
            QStringLiteral("commit.gpgSign=false"),
            QStringLiteral("-c"),
            QStringLiteral("core.fsync=all"),
            QStringLiteral("-c"),
            QStringLiteral("core.fsyncMethod=fsync"),
            QStringLiteral("-c"),
            QStringLiteral("core.logAllRefUpdates=false"),
            QStringLiteral("-c"),
            QStringLiteral("gc.auto=0"),
            QStringLiteral("-c"),
            QStringLiteral("gc.pruneExpire=now"),
            QStringLiteral("-c"),
            QStringLiteral("maintenance.auto=false"),
        };
    }

    QString m_executable;
    QString m_root;
};

QJsonObject readIdentity(const QString& path)
{
    return readJsonObjectFile(path);
}

// The identity file keeps a derived UUID stable for projects that do not carry
// a projectId yet. If the file is gone, fall back to the current project JSON.
QString expectedProjectUuid(const QString& projectPath, const QString& identityFile)
{
    const QJsonObject identity = readIdentity(identityFile);
    const QString stored = identity.value(QStringLiteral("uuid")).toString().trimmed();
    if (!stored.isEmpty())
        return stored;

    const QJsonObject project = readJsonObjectFile(canonicalProjectPath(projectPath));
    return project.value(QStringLiteral("projectId")).toString().trimmed();
}

bool checkpointUuidMatches(const QString& projectPath, const QString& identityFile,
                           const QString& metaUuid)
{
    if (metaUuid.isEmpty())
        return true;
    const QString expected = expectedProjectUuid(projectPath, identityFile);
    return expected.isEmpty() || expected == metaUuid;
}

} // namespace

CheckpointManager::CheckpointManager(const QString& cacheRoot,
                                     const QString& gitExecutable)
{
    if (cacheRoot.isEmpty())
        m_root = defaultCacheRoot();
    else
    {
        const QFileInfo info(cacheRoot);
        const QString canonical = info.canonicalFilePath();
        m_root = canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
    }

    if (!gitExecutable.isEmpty())
        m_gitExecutable = gitExecutable;
    else
    {
        const QString fromEnvironment = qEnvironmentVariable("PVFORGE_GIT").trimmed();
        m_gitExecutable = fromEnvironment.isEmpty() ? QStringLiteral("git")
                                                    : fromEnvironment;
    }
}

CheckpointManager::~CheckpointManager() = default;

QString CheckpointManager::defaultCacheRoot()
{
    return QDir::homePath() + QStringLiteral("/.cache/PVForge/checkpoints");
}

QString CheckpointManager::projectPathHash(const QString& projectPath)
{
    return sha256Hex(canonicalProjectPath(projectPath).toUtf8());
}

QString CheckpointManager::repoPath(const QString& projectPath) const
{
    return QDir(m_root).filePath(projectPathHash(projectPath) + QStringLiteral(".git"));
}

QString CheckpointManager::snapshotDir(const QString& projectPath) const
{
    return QDir(m_root).filePath(projectPathHash(projectPath)
                                 + QStringLiteral(".snapshots"));
}

QString CheckpointManager::identityPath(const QString& projectPath) const
{
    return QDir(m_root).filePath(projectPathHash(projectPath)
                                 + QStringLiteral(".identity.json"));
}

bool CheckpointManager::isAvailable() const
{
    // The snapshot backend needs nothing but a private directory, so
    // checkpointing stays available even without git.
    return ensurePrivateDir(m_root);
}

bool CheckpointManager::gitAvailable() const
{
    if (m_gitState == 0)
        m_gitState = selfTestGit() ? 1 : -1;
    return m_gitState > 0;
}

bool CheckpointManager::selfTestGit() const
{
    if (!ensurePrivateDir(m_root))
        return false;

    QTemporaryDir temporary(m_root + QStringLiteral("/.selftest-XXXXXX"));
    if (!temporary.isValid())
        return false;
    QFile::setPermissions(temporary.path(), QFileDevice::ReadOwner
                                                | QFileDevice::WriteOwner
                                                | QFileDevice::ExeOwner);
    const QString templateDir = temporary.path() + QStringLiteral("/template");
    if (!ensurePrivateDir(templateDir))
        return false;

    const QString repo = temporary.path() + QStringLiteral("/repo.git");
    GitRunner git(m_gitExecutable, m_root);
    const GitRunner::Result init =
        git.run(QString(),
                {QStringLiteral("init"), QStringLiteral("--bare"),
                 QStringLiteral("--quiet"), QStringLiteral("--template=") + templateDir,
                 repo});
    if (!init.ok)
        return false;

    const GitRunner::Result object =
        git.run(repo,
                {QStringLiteral("hash-object"), QStringLiteral("-w"),
                 QStringLiteral("--stdin")},
                QByteArrayLiteral("pvforge-checkpoint-selftest\n"));
    return object.ok && isObjectId(QString::fromLatin1(object.out).trimmed());
}

QString CheckpointManager::projectUuid(const QString& projectPath,
                                       const QByteArray& projectJson, bool create)
{
    QJsonObject root;
    parseJsonObject(projectJson, &root, nullptr);

    const QString explicitUuid =
        root.value(QStringLiteral("projectId")).toString().trimmed();
    const QString identityFile = identityPath(projectPath);
    const QJsonObject identity = readIdentity(identityFile);
    const QString stored = identity.value(QStringLiteral("uuid")).toString().trimmed();

    if (!explicitUuid.isEmpty())
    {
        if (create && explicitUuid != stored)
        {
            QJsonObject object;
            object[QStringLiteral("format")] =
                QStringLiteral("pvforge-checkpoint-identity");
            object[QStringLiteral("version")] = 1;
            object[QStringLiteral("uuid")] = explicitUuid;
            object[QStringLiteral("createdMs")] =
                QDateTime::currentMSecsSinceEpoch();
            object[QStringLiteral("pathHash")] = projectPathHash(projectPath);
            QString ignored;
            if (!writeDurable(identityFile,
                              QJsonDocument(object).toJson(QJsonDocument::Indented),
                              &ignored))
                qWarning() << "CheckpointManager: could not persist project identity";
        }
        return explicitUuid;
    }

    if (!stored.isEmpty())
        return stored;
    if (!create)
        return QString();

    QFileInfo fileInfo(canonicalProjectPath(projectPath));
    QDateTime created = fileInfo.birthTime();
    if (!created.isValid())
        created = fileInfo.metadataChangeTime();
    if (!created.isValid())
        created = QDateTime::currentDateTimeUtc();

    const QString uuid = projectPathHash(projectPath).left(32) + QLatin1Char('-')
                         + QString::number(created.toMSecsSinceEpoch());
    QJsonObject object;
    object[QStringLiteral("format")] = QStringLiteral("pvforge-checkpoint-identity");
    object[QStringLiteral("version")] = 1;
    object[QStringLiteral("uuid")] = uuid;
    object[QStringLiteral("createdMs")] = created.toMSecsSinceEpoch();
    object[QStringLiteral("pathHash")] = projectPathHash(projectPath);
    QString ignored;
    if (!writeDurable(identityFile,
                      QJsonDocument(object).toJson(QJsonDocument::Indented), &ignored))
        qWarning() << "CheckpointManager: could not persist derived project identity";
    return uuid;
}

bool CheckpointManager::createCheckpoint(const QString& projectPath,
                                         const QByteArray& projectJson,
                                         const QString& summary, QString* error,
                                         QString* checkpointId)
{
    if (error)
        error->clear();
    if (checkpointId)
        checkpointId->clear();

    if (projectPath.trimmed().isEmpty())
    {
        if (error)
            *error = QStringLiteral("工程路径为空");
        return false;
    }
    QJsonObject root;
    if (!parseJsonObject(projectJson, &root, error))
        return false;

    if (!ensurePrivateDir(m_root))
    {
        if (error)
            *error = QStringLiteral("无法创建检查点缓存目录");
        return false;
    }

    QStringList assetPaths;
    QSet<QString> seenAssets;
    collectAssetPaths(root, &assetPaths, &seenAssets);
    int missingAssets = 0;
    for (const QString& stored : assetPaths)
        if (!QFileInfo(resolveProjectAsset(projectPath, stored)).isFile())
            ++missingAssets;
    if (missingAssets > 0)
        qWarning() << "CheckpointManager:" << missingAssets
                   << "referenced assets are missing; checkpoint continues";

    const QString uuid = projectUuid(projectPath, projectJson, true);

    if (gitAvailable())
    {
        QString gitError;
        if (createGitCheckpoint(projectPath, projectJson, summary, uuid,
                                checkpointId, &gitError))
        {
            prune(projectPath, kDefaultKeep);
            return true;
        }
        qWarning() << "CheckpointManager: git checkpoint failed, using snapshot:"
                   << gitError;
    }

    QString snapshotError;
    if (createSnapshotCheckpoint(projectPath, projectJson, summary, uuid,
                                 checkpointId, &snapshotError))
    {
        prune(projectPath, kDefaultKeep);
        return true;
    }
    if (error)
        *error = snapshotError;
    return false;
}

bool CheckpointManager::createGitCheckpoint(const QString& projectPath,
                                            const QByteArray& projectJson,
                                            const QString& summary,
                                            const QString& uuid,
                                            QString* checkpointId, QString* error)
{
    const QString repo = repoPath(projectPath);
    const QFileInfo repoInfo(repo);
    if (repoInfo.exists() && (!repoInfo.isDir() || repoInfo.isSymLink()))
    {
        if (error)
            *error = QStringLiteral("检查点仓库路径被占用");
        return false;
    }
    if (!ensurePrivateDir(QFileInfo(repo).absolutePath()))
    {
        if (error)
            *error = QStringLiteral("无法创建检查点仓库目录");
        return false;
    }

    GitRunner git(m_gitExecutable, m_root);
    if (!QFileInfo(repo + QStringLiteral("/HEAD")).isFile())
    {
        if (QDir(repo).exists() && !QDir(repo).removeRecursively())
        {
            if (error)
                *error = QStringLiteral("无法重建检查点仓库");
            return false;
        }
        const QString templateDir = m_root + QStringLiteral("/.git-template");
        if (!ensurePrivateDir(templateDir))
        {
            if (error)
                *error = QStringLiteral("无法创建 git 模板目录");
            return false;
        }
        const GitRunner::Result init =
            git.run(QString(),
                    {QStringLiteral("init"), QStringLiteral("--bare"),
                     QStringLiteral("--quiet"),
                     QStringLiteral("--template=") + templateDir, repo});
        if (!init.ok)
        {
            if (error)
                *error = QStringLiteral("git 初始化失败: %1")
                             .arg(gitErrorMessage(init.exitCode, init.err, init.failure));
            return false;
        }
        QFile::setPermissions(repo, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    }

    QJsonObject root;
    parseJsonObject(projectJson, &root, nullptr);
    const qint64 createdMs = nextCreatedMs();
    const QByteArray meta =
        QJsonDocument(buildMeta(projectPath, projectJson, root, uuid, summary,
                                QStringLiteral("git"), createdMs))
            .toJson(QJsonDocument::Indented);

    const auto hashObject = [&](const QByteArray& data, QString* hash,
                                QString* failure) -> bool {
        const GitRunner::Result result =
            git.run(repo,
                    {QStringLiteral("hash-object"), QStringLiteral("-w"),
                     QStringLiteral("--stdin")},
                    data);
        const QString value = QString::fromLatin1(result.out).trimmed();
        if (!result.ok || !isObjectId(value))
        {
            *failure = QStringLiteral("git hash-object 失败: %1")
                           .arg(gitErrorMessage(result.exitCode, result.err,
                                                result.failure));
            return false;
        }
        *hash = value;
        return true;
    };

    QString metaHash;
    QString projectHash;
    QString failure;
    if (!hashObject(meta, &metaHash, &failure)
        || !hashObject(projectJson, &projectHash, &failure))
    {
        if (error)
            *error = failure;
        return false;
    }

    QByteArray treeInput;
    treeInput += QByteArrayLiteral("100644 blob ") + metaHash.toLatin1()
                 + QByteArrayLiteral("\tmeta.json\n");
    treeInput += QByteArrayLiteral("100644 blob ") + projectHash.toLatin1()
                 + QByteArrayLiteral("\tproject.pvproj\n");
    const GitRunner::Result tree =
        git.run(repo, {QStringLiteral("mktree")}, treeInput);
    const QString treeHash = QString::fromLatin1(tree.out).trimmed();
    if (!tree.ok || !isObjectId(treeHash))
    {
        if (error)
            *error = QStringLiteral("git mktree 失败: %1")
                         .arg(gitErrorMessage(tree.exitCode, tree.err, tree.failure));
        return false;
    }

    QString message = summary.trimmed();
    if (message.isEmpty())
        message = QStringLiteral("PVForge checkpoint");
    message = message.left(2000);
    message += QStringLiteral("\n\npvforge-checkpoint: ")
               + QUuid::createUuid().toString(QUuid::WithoutBraces);

    const GitRunner::Result commit =
        git.run(repo, {QStringLiteral("commit-tree"), treeHash,
                       QStringLiteral("-m"), message});
    const QString commitHash = QString::fromLatin1(commit.out).trimmed();
    if (!commit.ok || !isObjectId(commitHash))
    {
        if (error)
            *error = QStringLiteral("git commit-tree 失败: %1")
                         .arg(gitErrorMessage(commit.exitCode, commit.err,
                                              commit.failure));
        return false;
    }

    const GitRunner::Result update =
        git.run(repo, {QStringLiteral("update-ref"),
                       QStringLiteral("refs/checkpoints/") + commitHash, commitHash});
    if (!update.ok)
    {
        if (error)
            *error = QStringLiteral("git update-ref 失败: %1")
                         .arg(gitErrorMessage(update.exitCode, update.err,
                                              update.failure));
        return false;
    }

    // Verify the commit is readable and the stored bytes are exactly what the
    // caller handed us before claiming that a durable checkpoint exists.
    const QVector<GitRunner::Blob> readBack =
        git.readBlobs(repo,
                      {commitHash + QStringLiteral(":meta.json"),
                       commitHash + QStringLiteral(":project.pvproj")});
    if (readBack.size() != 2 || !readBack.at(0).found || !readBack.at(1).found
        || readBack.at(0).data != meta || readBack.at(1).data != projectJson)
    {
        if (error)
            *error = QStringLiteral("检查点写入校验失败");
        return false;
    }

    syncDirectory(repo + QStringLiteral("/refs/checkpoints"));
    syncDirectory(repo + QStringLiteral("/refs"));
    syncDirectory(repo);
    if (checkpointId)
        *checkpointId = commitHash;
    return true;
}

bool CheckpointManager::createSnapshotCheckpoint(const QString& projectPath,
                                                 const QByteArray& projectJson,
                                                 const QString& summary,
                                                 const QString& uuid,
                                                 QString* checkpointId,
                                                 QString* error)
{
    const QString directory = snapshotDir(projectPath);
    if (!ensurePrivateDir(directory))
    {
        if (error)
            *error = QStringLiteral("无法创建快照目录");
        return false;
    }

    QJsonObject root;
    parseJsonObject(projectJson, &root, nullptr);
    const qint64 createdMs = nextCreatedMs();
    const QByteArray meta =
        QJsonDocument(buildMeta(projectPath, projectJson, root, uuid, summary,
                                QStringLiteral("snapshot"), createdMs))
            .toJson(QJsonDocument::Indented);

    const QString id =
        QString::number(createdMs) + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'))
              .left(12);
    const QString payload = directory + QLatin1Char('/') + id + QStringLiteral(".pvproj");
    const QString sidecar = directory + QLatin1Char('/') + id + QStringLiteral(".json");

    QString writeError;
    if (!writeDurable(payload, projectJson, &writeError))
    {
        if (error)
            *error = writeError;
        return false;
    }
    if (!writeDurable(sidecar, meta, &writeError))
    {
        QFile::remove(payload);
        if (error)
            *error = writeError;
        return false;
    }

    QByteArray readBack;
    QString readError;
    if (!readFileBytes(payload, &readBack, &readError)
        || sha256Hex(readBack)
               != QString::fromLatin1(
                   QCryptographicHash::hash(projectJson, QCryptographicHash::Sha256)
                       .toHex()))
    {
        QFile::remove(payload);
        QFile::remove(sidecar);
        if (error)
            *error = QStringLiteral("快照写入校验失败");
        return false;
    }

    syncDirectory(directory);
    if (checkpointId)
        *checkpointId = id;
    return true;
}

QVector<CheckpointInfo> CheckpointManager::listCheckpoints(const QString& projectPath)
{
    QVector<CheckpointInfo> checkpoints = listGitCheckpoints(projectPath);
    checkpoints += listSnapshotCheckpoints(projectPath);
    std::sort(checkpoints.begin(), checkpoints.end(),
              [](const CheckpointInfo& a, const CheckpointInfo& b) {
                  if (a.created == b.created)
                      return a.id > b.id;
                  return a.created > b.created;
              });
    return checkpoints;
}

QVector<CheckpointInfo> CheckpointManager::listGitCheckpoints(
    const QString& projectPath)
{
    QVector<CheckpointInfo> checkpoints;
    const QString repo = repoPath(projectPath);
    if (!QFileInfo(repo + QStringLiteral("/HEAD")).isFile())
        return checkpoints;

    GitRunner git(m_gitExecutable, m_root);
    const GitRunner::Result refs =
        git.run(repo, {QStringLiteral("for-each-ref"),
                       QStringLiteral("--format=%(refname:strip=2)"),
                       QStringLiteral("refs/checkpoints/")});
    if (!refs.ok)
        return checkpoints;

    QStringList ids;
    for (const QByteArray& line : refs.out.split('\n'))
    {
        const QString id = QString::fromLatin1(line).trimmed();
        if (isObjectId(id))
            ids.append(id);
    }
    if (ids.isEmpty())
        return checkpoints;

    QStringList specs;
    for (const QString& id : ids)
        specs.append(id + QStringLiteral(":meta.json"));
    const QVector<GitRunner::Blob> blobs = git.readBlobs(repo, specs);

    for (int index = 0; index < ids.size() && index < blobs.size(); ++index)
    {
        if (!blobs.at(index).found)
            continue;
        QJsonObject meta;
        if (!parseJsonObject(blobs.at(index).data, &meta, nullptr))
            continue;
        CheckpointInfo info;
        info.id = ids.at(index);
        info.gitBacked = true;
        if (!fillInfoFromMeta(meta, projectPath, &info))
            continue;
        checkpoints.append(info);
    }
    return checkpoints;
}

QVector<CheckpointInfo> CheckpointManager::listSnapshotCheckpoints(
    const QString& projectPath)
{
    QVector<CheckpointInfo> checkpoints;
    const QDir directory(snapshotDir(projectPath));
    if (!directory.exists())
        return checkpoints;

    const QStringList sidecars =
        directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& name : sidecars)
    {
        const QString id = name.left(name.size() - int(QStringLiteral(".json").size()));
        if (!isSafeSnapshotId(id))
            continue;

        QByteArray sidecarBytes;
        if (!readFileBytes(directory.filePath(name), &sidecarBytes, nullptr))
            continue;
        QJsonObject meta;
        if (!parseJsonObject(sidecarBytes, &meta, nullptr))
            continue;

        CheckpointInfo info;
        info.id = id;
        info.gitBacked = false;
        if (!fillInfoFromMeta(meta, projectPath, &info))
            continue;

        const QFileInfo payload(directory.filePath(id + QStringLiteral(".pvproj")));
        info.valid = payload.isFile() && payload.size() == info.fileSize;
        checkpoints.append(info);
    }
    return checkpoints;
}

bool CheckpointManager::restoreCheckpoint(const QString& projectPath,
                                          const QString& checkpointId,
                                          QByteArray* projectJson, QString* error)
{
    if (projectJson)
        projectJson->clear();
    if (error)
        error->clear();

    if (checkpointId.isEmpty())
    {
        if (error)
            *error = QStringLiteral("检查点 id 为空");
        return false;
    }

    if (isObjectId(checkpointId)
        && QFileInfo(repoPath(projectPath) + QStringLiteral("/HEAD")).isFile())
        return restoreGitCheckpoint(projectPath, checkpointId, projectJson, error);

    if (isSafeSnapshotId(checkpointId)
        && QFileInfo(snapshotDir(projectPath) + QLatin1Char('/') + checkpointId
                     + QStringLiteral(".json"))
               .isFile())
        return restoreSnapshotCheckpoint(projectPath, checkpointId, projectJson,
                                         error);

    if (error)
    {
        if (isObjectId(checkpointId))
            *error = QStringLiteral("检查点仓库不存在或已损坏");
        else
            *error = QStringLiteral("找不到检查点");
    }
    return false;
}

bool CheckpointManager::restoreGitCheckpoint(const QString& projectPath,
                                             const QString& id,
                                             QByteArray* projectJson, QString* error)
{
    const QString repo = repoPath(projectPath);
    if (!QFileInfo(repo + QStringLiteral("/HEAD")).isFile())
    {
        if (error)
            *error = QStringLiteral("检查点仓库不存在或已损坏");
        return false;
    }

    GitRunner git(m_gitExecutable, m_root);
    const QVector<GitRunner::Blob> blobs =
        git.readBlobs(repo, {id + QStringLiteral(":meta.json"),
                             id + QStringLiteral(":project.pvproj")});
    if (blobs.size() != 2 || !blobs.at(0).found)
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据缺失");
        return false;
    }

    QJsonObject meta;
    if (!parseJsonObject(blobs.at(0).data, &meta, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据无法解析");
        return false;
    }
    CheckpointInfo info;
    info.id = id;
    if (!fillInfoFromMeta(meta, projectPath, &info))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据格式不符");
        return false;
    }
    if (!checkpointUuidMatches(projectPath, identityPath(projectPath),
                               info.projectUuid))
    {
        if (error)
            *error = QStringLiteral("检查点属于另一个工程，拒绝恢复");
        return false;
    }
    if (!blobs.at(1).found)
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：工程数据缺失");
        return false;
    }
    if (blobs.at(1).data.size() != info.fileSize
        || sha256Hex(blobs.at(1).data) != info.fileSha256)
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：SHA-256 不匹配");
        return false;
    }
    QJsonObject payload;
    if (!parseJsonObject(blobs.at(1).data, &payload, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：工程 JSON 无法解析");
        return false;
    }
    if (projectJson)
        *projectJson = blobs.at(1).data;
    return true;
}

bool CheckpointManager::restoreSnapshotCheckpoint(const QString& projectPath,
                                                  const QString& id,
                                                  QByteArray* projectJson,
                                                  QString* error)
{
    const QString directory = snapshotDir(projectPath);
    const QString sidecarPath = directory + QLatin1Char('/') + id + QStringLiteral(".json");
    const QString payloadPath =
        directory + QLatin1Char('/') + id + QStringLiteral(".pvproj");

    QByteArray sidecarBytes;
    if (!readFileBytes(sidecarPath, &sidecarBytes, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据缺失");
        return false;
    }
    QJsonObject meta;
    if (!parseJsonObject(sidecarBytes, &meta, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据无法解析");
        return false;
    }
    CheckpointInfo info;
    info.id = id;
    if (!fillInfoFromMeta(meta, projectPath, &info))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：元数据格式不符");
        return false;
    }
    if (!checkpointUuidMatches(projectPath, identityPath(projectPath),
                               info.projectUuid))
    {
        if (error)
            *error = QStringLiteral("检查点属于另一个工程，拒绝恢复");
        return false;
    }

    QByteArray payload;
    if (!readFileBytes(payloadPath, &payload, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：工程数据缺失");
        return false;
    }
    if (payload.size() != info.fileSize || sha256Hex(payload) != info.fileSha256)
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：SHA-256 不匹配");
        return false;
    }
    QJsonObject object;
    if (!parseJsonObject(payload, &object, nullptr))
    {
        if (error)
            *error = QStringLiteral("检查点已损坏：工程 JSON 无法解析");
        return false;
    }
    if (projectJson)
        *projectJson = payload;
    return true;
}

void CheckpointManager::prune(const QString& projectPath, int keep)
{
    if (!ensurePrivateDir(m_root))
        return;
    if (keep < 0)
        keep = 0;

    QVector<CheckpointInfo> checkpoints = listCheckpoints(projectPath);
    if (checkpoints.isEmpty())
        return;

    std::sort(checkpoints.begin(), checkpoints.end(),
              [](const CheckpointInfo& a, const CheckpointInfo& b) {
                  if (a.created == b.created)
                      return a.id < b.id;
                  return a.created < b.created;
              });

    qint64 total = 0;
    for (const CheckpointInfo& info : checkpoints)
        total += std::max<qint64>(0, info.fileSize);

    GitRunner git(m_gitExecutable, m_root);
    const QString repo = repoPath(projectPath);
    bool removedGit = false;
    int remaining = checkpoints.size();

    const auto removeOne = [&](const CheckpointInfo& info) -> bool {
        if (info.gitBacked)
        {
            if (!QFileInfo(repo + QStringLiteral("/HEAD")).isFile())
                return true;
            const GitRunner::Result result =
                git.run(repo, {QStringLiteral("update-ref"),
                               QStringLiteral("-d"),
                               QStringLiteral("refs/checkpoints/") + info.id});
            if (!result.ok)
                return false;
            removedGit = true;
            return true;
        }

        const QString directory = snapshotDir(projectPath);
        const bool payload =
            QFile::remove(directory + QLatin1Char('/') + info.id
                          + QStringLiteral(".pvproj"));
        const bool sidecar =
            QFile::remove(directory + QLatin1Char('/') + info.id
                          + QStringLiteral(".json"));
        return payload || sidecar || !QFileInfo(directory + QLatin1Char('/')
                                                 + info.id + QStringLiteral(".json"))
                                    .exists();
    };

    for (const CheckpointInfo& info : checkpoints)
    {
        if (remaining <= keep && total <= kDiskBudgetBytes)
            break;
        if (!removeOne(info))
            continue;
        --remaining;
        total -= std::max<qint64>(0, info.fileSize);
    }

    if (removedGit && QFileInfo(repo + QStringLiteral("/HEAD")).isFile())
        git.run(repo,
                {QStringLiteral("gc"), QStringLiteral("--prune=now"),
                 QStringLiteral("--quiet")},
                QByteArray(), kLargeGitTimeoutMs);
}

} // namespace pvf
