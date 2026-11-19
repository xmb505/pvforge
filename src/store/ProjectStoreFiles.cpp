#include "store/ProjectStoreSupport.h"

#include "model/Migrations.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>

#include <optional>
#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pvf::store::detail {

namespace {

constexpr char kTransactionDirectory[] = ".pvforge";
constexpr char kJournalName[] = "journal.json";
constexpr char kQuarantineDirectory[] = ".pvforge/quarantine";

void syncFile(QFile& file)
{
#ifdef Q_OS_UNIX
    if (file.isOpen())
        ::fsync(file.handle());
#else
    file.flush();
#endif
}

void syncDirectory(const QString& path)
{
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
#else
    Q_UNUSED(path);
#endif
}

QString transactionDirectory(const QString& root, const QString& name)
{
    return absolutePath(root, QString::fromLatin1(kTransactionDirectory))
           + QLatin1Char('/') + name;
}

QString uniqueTransactionName()
{
    static int counter = 0;
    return QStringLiteral("txn-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(++counter);
}

bool renameOverwrite(const QString& from, const QString& to)
{
#ifdef Q_OS_UNIX
    const QByteArray source = QFile::encodeName(from);
    const QByteArray target = QFile::encodeName(to);
    return ::rename(source.constData(), target.constData()) == 0;
#else
    if (QFileInfo::exists(to) && !QFile::remove(to))
        return false;
    return QFile::rename(from, to);
#endif
}

bool writeBytesSynced(const QString& path, const QByteArray& bytes, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }
    if (file.write(bytes) != bytes.size())
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }
    if (!file.flush())
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }
    syncFile(file);
    return true;
}

QByteArray readAll(const QString& path, bool* exists = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (exists)
            *exists = QFileInfo::exists(path);
        return QByteArray();
    }
    if (exists)
        *exists = true;
    return file.readAll();
}

bool removeDirectory(const QString& path)
{
    return !QFileInfo::exists(path) || QDir(path).removeRecursively();
}

// Moves a file that cannot be loaded out of the entity directory so the rest
// of the project can open. Returns the quarantine path, empty on failure.
QString quarantineFile(const QString& root, const QString& relativePath,
                       const QString& reason)
{
    const QString source = absolutePath(root, relativePath);
    const QString directory = absolutePath(root, QString::fromLatin1(kQuarantineDirectory));
    if (!QDir().mkpath(directory))
        return QString();

    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString target =
        directory + QLatin1Char('/') + stamp + QLatin1Char('-')
        + QFileInfo(source).fileName();
    for (int i = 1; QFileInfo::exists(target); ++i)
    {
        target = directory + QLatin1Char('/') + stamp + QLatin1Char('-')
                 + QString::number(i) + QLatin1Char('-')
                 + QFileInfo(source).fileName();
    }
    Q_UNUSED(reason);
    if (!QFile::rename(source, target))
        return QString();
    return target;
}

} // namespace

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
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

bool readFileBytes(const QString& path, QByteArray* bytes, bool* existed,
                   QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (!file.exists())
        {
            bytes->clear();
            if (existed)
                *existed = false;
            return true;
        }
        setError(error,
                 QStringLiteral("无法读取 %1：%2").arg(path, file.errorString()));
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        setError(error,
                 QStringLiteral("读取 %1 失败：%2").arg(path, file.errorString()));
        return false;
    }
    if (existed)
        *existed = true;
    return true;
}

QString fileDigest(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString assetLivePath(const QString& root, const AssetHash& hash)
{
    return root + QStringLiteral("/assets/") + hash;
}

QString assetTrashPath(const QString& root, const AssetHash& hash)
{
    return root + QStringLiteral("/.trash/") + hash;
}

bool applyFileTransaction(const QString& root, const QVector<TransactionOp>& ops,
                          QString* error, int failOnWriteIndex,
                          int crashAfterApplyIndex)
{
    if (ops.isEmpty())
        return true;

    const QString directory =
        transactionDirectory(root, uniqueTransactionName());
    if (!QDir().mkpath(directory))
    {
        setError(error, QStringLiteral("无法创建事务目录：%1").arg(directory));
        return false;
    }

    const auto cleanup = [&] {
        removeDirectory(directory);
    };

    struct Staged
    {
        QString staged;
        QString backup;
        bool hadBackup = false;
    };
    QVector<Staged> staged;
    staged.reserve(ops.size());

    QJsonArray journalOps;
    for (int i = 0; i < ops.size(); ++i)
    {
        const TransactionOp& op = ops.at(i);
        const QString target = absolutePath(root, op.relativePath);
        Staged item;
        item.staged = directory + QStringLiteral("/staged-%1").arg(i);
        item.backup = directory + QStringLiteral("/backup-%1").arg(i);

        if (!op.remove)
        {
            if (failOnWriteIndex >= 0 && i == failOnWriteIndex)
            {
                setError(error,
                         QStringLiteral("模拟写入失败：%1").arg(op.relativePath));
                cleanup();
                return false;
            }
            if (!writeBytesSynced(item.staged, op.bytes, error))
            {
                cleanup();
                return false;
            }
        }

        QByteArray previous;
        bool existed = false;
        if (!readFileBytes(target, &previous, &existed, error))
        {
            cleanup();
            return false;
        }
        if (existed)
        {
            if (!writeBytesSynced(item.backup, previous, error))
            {
                cleanup();
                return false;
            }
            item.hadBackup = true;
        }

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), op.relativePath);
        // Names are relative to the transaction directory so a project that is
        // moved between the crash and the next open still recovers.
        entry.insert(QStringLiteral("staged"), QFileInfo(item.staged).fileName());
        entry.insert(QStringLiteral("remove"), op.remove);
        journalOps.append(entry);
        staged.append(item);
    }

    QJsonObject journal;
    journal.insert(QStringLiteral("version"), 1);
    journal.insert(QStringLiteral("ops"), journalOps);
    QByteArray journalBytes =
        QJsonDocument(journal).toJson(QJsonDocument::Compact);
    if (!writeBytesSynced(directory + QLatin1Char('/') + QLatin1String(kJournalName),
                          journalBytes, error))
    {
        cleanup();
        return false;
    }
    syncDirectory(directory);
    syncDirectory(absolutePath(root, QString::fromLatin1(kTransactionDirectory)));

    const auto applyOne = [&](int index) -> bool {
        const TransactionOp& op = ops.at(index);
        const QString target = absolutePath(root, op.relativePath);
        if (op.remove)
        {
            if (QFileInfo::exists(target) && !QFile::remove(target))
            {
                setError(error, QStringLiteral("无法删除 %1").arg(target));
                return false;
            }
            return true;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        // rename() over an existing file is atomic on POSIX, so a reader never
        // observes a half-written target.
        if (!renameOverwrite(staged.at(index).staged, target))
        {
            setError(error, QStringLiteral("无法写入 %1").arg(target));
            return false;
        }
        return true;
    };

    // Never remove an old root file before the new one is in place: the order
    // is the caller's (new root first, removal last), so a crash between the
    // two leaves the old file present and the journal completes the removal.
    for (int i = 0; i < ops.size(); ++i)
    {
        if (crashAfterApplyIndex >= 0 && i == crashAfterApplyIndex)
        {
            setError(error,
                     QStringLiteral("模拟进程在事务中途退出：%1")
                         .arg(ops.at(i).relativePath));
            return false; // leave the journal for the next open()
        }
        if (!applyOne(i))
        {
            // Roll back files that were already replaced, then drop the
            // journal. If that also fails, the journal stays and the next
            // open() rolls the whole transaction forward.
            bool rolledBack = true;
            for (int j = i - 1; j >= 0; --j)
            {
                const QString target = absolutePath(root, ops.at(j).relativePath);
                const Staged& item = staged.at(j);
                if (item.hadBackup)
                {
                    const QByteArray backup = readAll(item.backup);
                    QString restoreError;
                    if (!writeBytesFile(target, backup, &restoreError))
                        rolledBack = false;
                }
                else if (!ops.at(j).remove && QFileInfo::exists(target))
                {
                    if (!QFile::remove(target))
                        rolledBack = false;
                }
            }
            if (rolledBack)
            {
                cleanup();
            }
            return false;
        }
    }

    // Make the renames durable before the journal (which proves the intent)
    // disappears.
    QSet<QString> touchedDirectories;
    for (const TransactionOp& op : ops)
        touchedDirectories.insert(
            QFileInfo(absolutePath(root, op.relativePath)).absolutePath());
    for (const QString& directoryPath : touchedDirectories)
        syncDirectory(directoryPath);

    cleanup();
    return true;
}

bool recoverPendingTransactions(const QString& root, QString* error)
{
    const QString base =
        absolutePath(root, QString::fromLatin1(kTransactionDirectory));
    const QFileInfoList entries =
        QDir(base).entryInfoList({QStringLiteral("txn-*")}, QDir::Dirs,
                                 QDir::Name);
    for (const QFileInfo& entry : entries)
    {
        const QString journalPath =
            entry.absoluteFilePath() + QLatin1Char('/') + QLatin1String(kJournalName);
        QByteArray journalBytes;
        bool journalExists = false;
        if (!readFileBytes(journalPath, &journalBytes, &journalExists, error))
            return false;
        if (!journalExists)
        {
            // The crash happened before the journal was durable: no target
            // file was touched yet.
            removeDirectory(entry.absoluteFilePath());
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(journalBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            removeDirectory(entry.absoluteFilePath());
            continue;
        }

        const QJsonArray ops =
            document.object().value(QStringLiteral("ops")).toArray();
        for (const QJsonValue& value : ops)
        {
            const QJsonObject op = value.toObject();
            const QString relativePath =
                op.value(QStringLiteral("path")).toString();
            if (relativePath.isEmpty())
                continue;
            const QString target = absolutePath(root, relativePath);
            if (op.value(QStringLiteral("remove")).toBool())
            {
                if (QFileInfo::exists(target) && !QFile::remove(target))
                {
                    setError(error,
                             QStringLiteral("无法完成未结束的事务（删除 %1）")
                                 .arg(target));
                    return false;
                }
                continue;
            }
            const QString staged =
                entry.absoluteFilePath() + QLatin1Char('/')
                + op.value(QStringLiteral("staged")).toString();
            if (op.value(QStringLiteral("staged")).toString().isEmpty()
                || !QFileInfo::exists(staged))
                continue; // already applied before the interruption
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!renameOverwrite(staged, target))
            {
                setError(error,
                         QStringLiteral("无法完成未结束的事务（写入 %1）")
                             .arg(target));
                return false;
            }
        }
        removeDirectory(entry.absoluteFilePath());
    }
    return true;
}

bool hasPendingTransaction(const QString& root)
{
    const QDir base(
        absolutePath(root, QString::fromLatin1(kTransactionDirectory)));
    return !base
                .entryList({QStringLiteral("txn-*")}, QDir::Dirs, QDir::Name)
                .isEmpty();
}

namespace {

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
                 QStringLiteral("%1 的%2版本 %3 过旧（本程序最低支持版本 %4；"
                                "旧扁平实体目录/单文件工程已不兼容，不做导入）")
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

} // namespace

bool loadProjectData(const QString& root, model::Project* project,
                     QString* error, LoadReport* report)
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
            const QString path = fileInfo.absoluteFilePath();
            const QString relativePath = QStringLiteral("entities/") + fileName;
            if (!model::isValidUuid(uuid))
            {
                if (report)
                {
                    const QString reason =
                        QStringLiteral("实体文件名不是合法 UUID：%1").arg(fileName);
                    const QString moved = quarantineFile(root, relativePath, reason);
                    if (!moved.isEmpty())
                    {
                        report->quarantined.append(QuarantinedPath{moved, reason});
                        continue;
                    }
                    setError(error, reason);
                    return false;
                }
                setError(error,
                         QStringLiteral("实体文件名不是合法 UUID：%1").arg(path));
                return false;
            }

            QString fileError;
            QJsonObject entityObject;
            QJsonObject entityDocument;
            std::optional<model::EntityTreeNode> tree;
            QHash<Uuid, model::Entity> treeEntities;
            bool loadedFile = readJsonObject(path, &entityObject, &fileError)
                              && migrateTextDocument(path, QStringLiteral("实体"),
                                                     uuid, entityObject,
                                                     &entityDocument, &fileError);
            if (loadedFile)
            {
                model::ParseOptions entityOptions = options;
                entityOptions.prefix = path;
                tree = model::entityTreeFromJson(entityDocument, entityOptions,
                                                 &fileError);
                loadedFile = tree.has_value();
            }
            if (!loadedFile)
            {
                if (!report)
                {
                    setError(error, fileError);
                    return false;
                }
                const QString moved = quarantineFile(root, relativePath, fileError);
                if (!moved.isEmpty())
                {
                    report->quarantined.append(
                        QuarantinedPath{moved, fileError});
                    continue;
                }
                setError(error, fileError);
                return false;
            }

            if (tree->entity.uuid != uuid)
            {
                const QString reason =
                    QStringLiteral("实体文件 %1 内的根 UUID（%2）与文件名不一致")
                        .arg(path, tree->entity.uuid);
                if (!report)
                {
                    setError(error, reason);
                    return false;
                }
                const QString moved = quarantineFile(root, relativePath, reason);
                if (!moved.isEmpty())
                {
                    report->quarantined.append(QuarantinedPath{moved, reason});
                    continue;
                }
                setError(error, reason);
                return false;
            }

            QString flattenError;
            if (!model::flattenEntityTree(*tree, &treeEntities, &flattenError))
            {
                const QString reason =
                    QStringLiteral("实体文件 %1 的根子树无效：%2")
                        .arg(path, flattenError);
                if (!report)
                {
                    setError(error, reason);
                    return false;
                }
                const QString moved = quarantineFile(root, relativePath, reason);
                if (!moved.isEmpty())
                {
                    report->quarantined.append(QuarantinedPath{moved, reason});
                    continue;
                }
                setError(error, reason);
                return false;
            }

            bool duplicate = false;
            for (auto it = treeEntities.constBegin();
                 it != treeEntities.constEnd(); ++it)
            {
                if (loaded.entities.contains(it.key()))
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                const QString reason =
                    QStringLiteral("实体 UUID 重复：%1 的根子树与已加载的实体冲突")
                        .arg(path);
                if (!report)
                {
                    setError(error, reason);
                    return false;
                }
                const QString moved = quarantineFile(root, relativePath, reason);
                if (!moved.isEmpty())
                {
                    report->quarantined.append(QuarantinedPath{moved, reason});
                    continue;
                }
                setError(error, reason);
                return false;
            }

            for (auto it = treeEntities.constBegin();
                 it != treeEntities.constEnd(); ++it)
            {
                loaded.entities.insert(it.key(), it.value());
            }
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

ProjectLock::~ProjectLock()
{
    if (file)
        file->unlock();
}

std::shared_ptr<ProjectLock> acquireProjectLock(const QString& root,
                                                QString* error)
{
    static QMutex mutex;
    static QHash<QString, std::weak_ptr<ProjectLock>> registry;

    QMutexLocker locker(&mutex);
    if (const auto existing = registry.value(root).lock())
        return existing;

    const QString directory =
        absolutePath(root, QString::fromLatin1(kTransactionDirectory));
    if (!QDir().mkpath(directory))
    {
        setError(error, QStringLiteral("无法创建工程锁目录：%1").arg(directory));
        return nullptr;
    }

    auto lock = std::make_shared<ProjectLock>();
    lock->root = root;
    lock->file = std::make_unique<QLockFile>(
        directory + QStringLiteral("/project.lock"));
    // A crashed instance on this machine is detected by pid. Time-based
    // staleness is disabled so a long editing session on a shared drive cannot
    // have its lock stolen; QLockFile still removes locks whose pid is dead.
    lock->file->setStaleLockTime(0);
    if (!lock->file->tryLock(0))
    {
        qint64 pid = 0;
        QString hostname;
        QString appname;
        lock->file->getLockInfo(&pid, &hostname, &appname);
        const QString owner =
            pid > 0
                ? QStringLiteral("进程 %1，主机 %2")
                      .arg(pid)
                      .arg(hostname.isEmpty() ? QStringLiteral("未知")
                                              : hostname)
                : QStringLiteral("未知进程");
        setError(error,
                 QStringLiteral("工程已被另一个 PVForge 实例打开（%1）。"
                                "请先关闭另一个窗口，避免两个实例互相覆盖；"
                                "如果确认没有其他窗口，请删除 %2 后重试。")
                     .arg(owner, lock->file->fileName()));
        return nullptr;
    }

    registry.insert(root, lock);
    return lock;
}

} // namespace pvf::store::detail
