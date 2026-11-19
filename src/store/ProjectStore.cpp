#include "Store.h"

#include "store/ProjectStoreSupport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

namespace pvf::store {

// The file/git plumbing lives in ProjectStoreFiles.cpp and ProjectStoreGit.cpp;
// the helpers keep their historical names so the method bodies below read the
// same as before the split.
using namespace detail;

namespace {

constexpr char kInitMessage[] = "pvf: init project";
constexpr char kCommitFailureMarker[] = ".pvforge/commit-failed.json";

const char kGitIgnore[] = R"(# PVForge versions text project data only; media and caches stay local.
assets/
.cache/
.pvforge/
.trash/

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

namespace {

QVector<Uuid> collectSubtree(const model::Project& project, const Uuid& uuid)
{
    QVector<Uuid> result{uuid};
    QSet<Uuid> seen{uuid};
    for (int i = 0; i < result.size(); ++i)
    {
        const QVector<Uuid> children = model::childrenOf(project, result.at(i));
        for (const Uuid& child : children)
        {
            if (seen.contains(child))
                continue;
            seen.insert(child);
            result.append(child);
        }
    }
    return result;
}

} // namespace

ProjectStore::ProjectStore(QString root, model::Project project,
                           std::shared_ptr<detail::ProjectLock> lock)
    : m_root(std::move(root))
    , m_project(std::move(project))
    , m_lock(std::move(lock))
{
    m_assets.emplace(m_root);
    rebuildEntityRootIndex();
    refreshDigests();
}

bool ProjectStore::gitBinaryAvailable(QString* version)
{
    return gitVersion(version);
}

bool ProjectStore::ensureWritable(QString* error) const
{
    if (!readOnly())
        return true;
    setError(error,
             QStringLiteral("工程以只读方式打开：%1").arg(m_readOnlyReason));
    return false;
}

void ProjectStore::setReadOnly(const QString& reason)
{
    if (!reason.isEmpty())
        m_readOnlyReason = reason;
}

namespace {

// Removes everything this create() call wrote. The directory was empty (or
// absent) before, so removing exactly these entries is safe and lets the user
// retry after a failure instead of hitting "directory is not empty".
void cleanupCreatedProject(const QString& root)
{
    const QStringList entries = {
        QStringLiteral("project.json"), QStringLiteral(".gitignore"),
        QStringLiteral("entities"),     QStringLiteral("assets"),
        QStringLiteral(".git"),         QStringLiteral(".pvforge"),
        QStringLiteral(".trash"),
    };
    for (const QString& entry : entries)
    {
        const QString path = absolutePath(root, entry);
        const QFileInfo info(path);
        if (info.isDir())
            QDir(path).removeRecursively();
        else if (info.exists())
            QFile::remove(path);
    }
}

} // namespace

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
    const bool existedBefore = info.exists();
    if (existedBefore)
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

    const auto fail = [&](const QString& message) -> std::optional<ProjectStore> {
        setError(error, message);
        cleanupCreatedProject(root);
        if (!existedBefore)
            QDir().rmdir(root);
        return std::nullopt;
    };

    QDir directory;
    if (!directory.mkpath(root) || !directory.mkpath(absolutePath(root, QStringLiteral("entities")))
        || !directory.mkpath(absolutePath(root, QStringLiteral("assets"))))
    {
        return fail(QStringLiteral("无法创建工程目录：%1").arg(root));
    }

    if (!writeJsonObject(absolutePath(root, QStringLiteral("project.json")),
                         model::toJson(initialProject), error)
        || !writeTextFile(absolutePath(root, QStringLiteral(".gitignore")),
                          QString::fromLatin1(kGitIgnore), error))
    {
        const QString message = error ? *error : QStringLiteral("无法写入初始文件");
        return fail(message);
    }

    if (useRepo)
    {
        const ProcessResult init =
            runGit(root, {QStringLiteral("init")});
        if (init.exitCode != 0)
        {
            return fail(QStringLiteral("git init 失败：%1")
                            .arg(processFailure(init)));
        }

        if (!markGitRepository(root, error))
            return std::nullopt;

        if (!ensureGitIdentity(root, error))
            return fail(error ? *error : QStringLiteral("无法配置 git 身份"));

        const ProcessResult add = runGit(
            root, {QStringLiteral("add"), QStringLiteral("--"),
                   QStringLiteral(".gitignore"), QStringLiteral("project.json")});
        if (add.exitCode != 0)
        {
            return fail(QStringLiteral("git add 初始文件失败：%1")
                            .arg(processFailure(add)));
        }

        QString ignoredHash;
        if (!commitOnlyPaths(
                root, QString::fromLatin1(kInitMessage),
                {QStringLiteral(".gitignore"), QStringLiteral("project.json")},
                &ignoredHash, error))
        {
            return fail(error ? *error : QStringLiteral("git 初始提交失败"));
        }
    }

    auto lock = acquireProjectLock(root, error);
    if (!lock)
        return fail(error ? *error : QStringLiteral("无法锁定工程目录"));

    ProjectStore store(root, std::move(initialProject), std::move(lock));
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

    auto lock = acquireProjectLock(root, error);
    if (!lock)
        return std::nullopt;

    // Finish any save a previous session started but did not complete before
    // reading the model: the journal contains fully staged bytes.
    QString recoveryError;
    if (!recoverPendingTransactions(root, &recoveryError))
    {
        setError(error,
                 QStringLiteral("无法恢复未完成的保存：%1").arg(recoveryError));
        return std::nullopt;
    }

    // Missing git or a missing repository is not an error: the project opens
    // as a plain text store. Only explicit useGit=false skips the probe.
    // A repository PVForge did not create is refused with a visible warning
    // instead of running project-controlled hooks/filters.
    bool useRepo = false;
    QString gitWarning;
    if (useGit && gitBinaryAvailable()
        && QFileInfo::exists(absolutePath(root, QStringLiteral(".git"))))
    {
        QString untrustedReason;
        if (gitRepositoryIsTrusted(root, &untrustedReason))
            useRepo = true;
        else
            gitWarning = untrustedReason;
    }

    model::Project project;
    LoadReport report;
    QString loadError;
    if (!loadProjectData(root, &project, &loadError, &report))
    {
        setError(error, loadError);
        return std::nullopt;
    }

    // The in-memory undo/redo history starts empty after opening a project;
    // across sessions git history is the cold backup.
    ProjectStore store(root, std::move(project), std::move(lock));
    store.m_gitEnabled = useRepo;
    for (const QuarantinedPath& item : report.quarantined)
        store.m_quarantined.append(QuarantinedFile{item.path, item.reason});
    if (!store.m_quarantined.isEmpty())
    {
        QStringList names;
        for (int i = 0; i < store.m_quarantined.size() && i < 5; ++i)
            names << QFileInfo(store.m_quarantined.at(i).path).fileName();
        if (store.m_quarantined.size() > names.size())
            names << QStringLiteral("…");
        store.m_readOnlyReason =
            QStringLiteral("打开时隔离了 %1 个损坏或重复的实体文件（%2），"
                           "为避免覆盖未加载的数据，工程以只读方式打开。")
                .arg(store.m_quarantined.size())
                .arg(names.join(QStringLiteral("、")));
    }
    store.m_gitWarning = gitWarning;

    // Scan once so the caller can offer recover/discard. A transient git
    // failure (for example a stale index.lock) must not block opening; the
    // caller can rescan with dirtyTextFiles().
    if (store.m_gitEnabled)
    {
        // Commit failures from a missing identity are confusing; fill in the
        // project-local identity while opening, like create()/setGitEnabled().
        QString identityError;
        ensureGitIdentity(root, &identityError);
        store.refreshDirtyText();
    }

    // A failed git commit from the previous session is a different state from
    // an abnormal exit; the recovery dialog reports it separately.
    const QString markerPath =
        absolutePath(root, QString::fromLatin1(kCommitFailureMarker));
    if (QFileInfo::exists(markerPath))
    {
        QJsonObject marker;
        QString markerError;
        if (readJsonObject(markerPath, &marker, &markerError))
            store.m_previousCommitFailure =
                marker.value(QStringLiteral("message")).toString();
        if (store.m_previousCommitFailure.isEmpty())
            store.m_previousCommitFailure = QStringLiteral("上次关闭时提交失败");
    }
    return store;
}

bool ProjectStore::setGitEnabled(bool enabled, QString* error)
{
    if (!ensureWritable(error))
        return false;

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
    if (QFileInfo::exists(gitDirectory))
    {
        QString reason;
        if (!gitRepositoryIsTrusted(m_root, &reason))
        {
            setError(error,
                     QStringLiteral("拒绝启用不安全的 Git 仓库：%1").arg(reason));
            return false;
        }
        m_gitWarning.clear();
        m_gitEnabled = true;
        refreshDirtyText();
        return true;
    }

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

        if (!markGitRepository(m_root, error))
            return false;

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

Uuid ProjectStore::entityRootOf(const Uuid& uuid, QString* error) const
{
    QSet<Uuid> visited;
    Uuid current = uuid;
    while (!current.isEmpty())
    {
        if (visited.contains(current))
        {
            setError(error,
                     QStringLiteral("实体父子关系存在循环：%1").arg(current));
            return QString();
        }
        visited.insert(current);

        const auto entity = m_project.entities.constFind(current);
        if (entity == m_project.entities.constEnd())
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(current));
            return QString();
        }
        if (entity->parentUuid.isEmpty())
            return current;
        current = entity->parentUuid;
    }

    setError(error, QStringLiteral("实体 %1 没有根实体").arg(uuid));
    return QString();
}

void ProjectStore::rebuildEntityRootIndex()
{
    m_entityRoots.clear();
    for (auto it = m_project.entities.constBegin(); it != m_project.entities.constEnd();
         ++it)
    {
        const Uuid root = entityRootOf(it.key());
        if (!root.isEmpty())
            m_entityRoots.insert(it.key(), root);
    }
}

namespace {

bool buildEntityTreeBytes(const model::Project& project, const Uuid& rootUuid,
                          QByteArray* bytes, QString* error)
{
    model::EntityTreeNode tree;
    QString buildError;
    if (!model::buildEntityTree(project, rootUuid, &tree, &buildError))
    {
        setError(error, buildError);
        return false;
    }

    // Reuse the reader's checks before writing: a subtree that would be
    // refused on open must not be written in the first place.
    QHash<Uuid, model::Entity> checked;
    QString treeError;
    if (!model::flattenEntityTree(tree, &checked, &treeError))
    {
        setError(error, treeError);
        return false;
    }

    QByteArray encoded =
        QJsonDocument(model::toJson(tree)).toJson(QJsonDocument::Indented);
    if (!encoded.endsWith('\n'))
        encoded.append('\n');
    *bytes = encoded;
    return true;
}

} // namespace

bool ProjectStore::saveEntity(const Uuid& uuid, QString* error)
{
    if (!ensureWritable(error))
        return false;
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

    const Uuid newRoot = entityRootOf(uuid, error);
    if (newRoot.isEmpty())
        return false;
    const Uuid oldRoot = m_entityRoots.value(uuid);

    QByteArray newRootBytes;
    if (!buildEntityTreeBytes(m_project, newRoot, &newRootBytes, error))
        return false;

    // A reparent can move this entity (or its whole subtree) between files.
    // Write the new root first and remove the old one last, and do both in one
    // transaction so a failure cannot leave the same UUID in two files.
    QVector<TransactionOp> ops;
    ops.append(TransactionOp{entityRelativePath(newRoot), newRootBytes, false});
    if (!oldRoot.isEmpty() && oldRoot != newRoot)
    {
        const bool oldRootStillRoot =
            m_project.entities.contains(oldRoot)
            && entityRootOf(oldRoot) == oldRoot;
        if (oldRootStillRoot)
        {
            QByteArray oldRootBytes;
            if (!buildEntityTreeBytes(m_project, oldRoot, &oldRootBytes, error))
                return false;
            ops.append(
                TransactionOp{entityRelativePath(oldRoot), oldRootBytes, false});
        }
        else
        {
            ops.append(TransactionOp{entityRelativePath(oldRoot), QByteArray(),
                                     true});
        }
    }

    if (!writeTrackedFiles(ops, error))
        return false;

    rebuildEntityRootIndex();
    return true;
}

bool ProjectStore::saveProject(QString* error)
{
    if (!ensureWritable(error))
        return false;

    // Validate settings and markers without requiring the entity tree to be
    // consistent; the entity files are written separately.
    model::Project check;
    check.settings = m_project.settings;
    check.markers = m_project.markers;
    if (!model::validateProject(check, error))
        return false;

    QByteArray bytes = QJsonDocument(model::toJson(m_project))
                           .toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n'))
        bytes.append('\n');
    return writeTrackedFiles(
        {TransactionOp{QStringLiteral("project.json"), bytes, false}}, error);
}

bool ProjectStore::deleteEntity(const Uuid& uuid, QString* error)
{
    if (!ensureWritable(error))
        return false;

    if (!model::isValidUuid(uuid))
    {
        setError(error, QStringLiteral("实体 UUID 格式非法：%1").arg(uuid));
        return false;
    }

    if (!m_project.entities.contains(uuid))
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }

    const Uuid root = entityRootOf(uuid, error);
    if (root.isEmpty())
        return false;

    const QVector<Uuid> ids = collectSubtree(m_project, uuid);
    QHash<Uuid, model::Entity> removed;
    for (const Uuid& id : ids)
        removed.insert(id, m_project.entities.take(id));

    QVector<TransactionOp> ops;
    if (uuid == root)
    {
        ops.append(TransactionOp{entityRelativePath(root), QByteArray(), true});
    }
    else if (m_project.entities.contains(root))
    {
        QByteArray rootBytes;
        if (!buildEntityTreeBytes(m_project, root, &rootBytes, error))
        {
            for (auto it = removed.constBegin(); it != removed.constEnd(); ++it)
                m_project.entities.insert(it.key(), it.value());
            return false;
        }
        ops.append(TransactionOp{entityRelativePath(root), rootBytes, false});
    }

    if (!writeTrackedFiles(ops, error))
    {
        for (auto it = removed.constBegin(); it != removed.constEnd(); ++it)
            m_project.entities.insert(it.key(), it.value());
        return false;
    }

    rebuildEntityRootIndex();
    return true;
}

bool ProjectStore::writeTrackedFiles(const QVector<TransactionOp>& ops,
                                     QString* error)
{
    if (ops.isEmpty())
        return true;
    if (!ensureWritable(error))
        return false;
    if (!verifyDigests(ops, error))
        return false;

    QVector<QPair<QString, QByteArray>> before;
    before.reserve(ops.size());
    for (const TransactionOp& op : ops)
    {
        QByteArray bytes;
        bool existed = false;
        if (!readFileBytes(absolutePath(m_root, op.relativePath), &bytes,
                           &existed, error))
        {
            return false;
        }
        before.append({op.relativePath, existed ? bytes : QByteArray()});
    }

    int writeFailure = -1;
    int crashAfter = -1;
#ifdef PVFORGE_TEST_HOOKS
    writeFailure = m_writeFailureForTest;
    crashAfter = m_crashAfterApplyForTest;
    if (m_writeFailureForTest >= 0)
        m_writeFailureForTest = -1;
    if (m_crashAfterApplyForTest >= 0)
        m_crashAfterApplyForTest = -1;
#endif

    if (!applyFileTransaction(m_root, ops, error, writeFailure, crashAfter))
    {
        if (hasPendingTransaction(m_root))
        {
            setReadOnly(QStringLiteral(
                "保存事务未完成，已留下可恢复日志。请重新打开工程，"
                "程序会自动完成或回滚这次保存。"));
        }
        return false;
    }

    m_pendingHumanEdit = true;
    for (const auto& item : std::as_const(before))
    {
        if (!m_txnBefore.contains(item.first))
            m_txnBefore.insert(item.first, item.second);
        m_txnPaths.insert(item.first);
    }
    for (const TransactionOp& op : ops)
    {
        if (op.remove)
        {
            m_digests.insert(op.relativePath, QString());
        }
        else
        {
            m_digests.insert(op.relativePath, fileDigest(op.bytes));
        }
    }
    return true;
}

void ProjectStore::refreshDigests()
{
    m_digests.clear();
    const auto record = [this](const QString& relativePath) {
        QByteArray bytes;
        bool existed = false;
        if (readFileBytes(absolutePath(m_root, relativePath), &bytes, &existed,
                          nullptr))
        {
            m_digests.insert(relativePath,
                             existed ? fileDigest(bytes) : QString());
        }
    };
    record(QStringLiteral("project.json"));
    const QDir entities(absolutePath(m_root, QStringLiteral("entities")));
    for (const QFileInfo& fileInfo :
         entities.entryInfoList({QStringLiteral("*.json")}, QDir::Files,
                                QDir::Name))
    {
        record(QStringLiteral("entities/") + fileInfo.fileName());
    }
}

bool ProjectStore::verifyDigests(const QVector<TransactionOp>& ops,
                                 QString* error) const
{
    QStringList checked;
    for (const TransactionOp& op : ops)
    {
        if (checked.contains(op.relativePath))
            continue;
        checked << op.relativePath;

        QByteArray bytes;
        bool existed = false;
        if (!readFileBytes(absolutePath(m_root, op.relativePath), &bytes,
                           &existed, error))
        {
            return false;
        }
        const QString digest = existed ? fileDigest(bytes) : QString();
        const auto known = m_digests.constFind(op.relativePath);
        if (known == m_digests.constEnd())
        {
            // The store did not have this path in its snapshot. A file that
            // appeared underneath it was created by another program; a missing
            // path is simply a new entity.
            if (existed)
            {
                setError(error,
                         QStringLiteral("工程文件被外部程序修改：%1。"
                                        "请重新打开工程后再保存，避免覆盖外部改动。")
                             .arg(op.relativePath));
                return false;
            }
            continue;
        }
        if (known.value() != digest)
        {
            setError(error,
                     QStringLiteral("工程文件被外部程序修改：%1。"
                                    "请重新打开工程后再保存，避免覆盖外部改动。")
                         .arg(op.relativePath));
            return false;
        }
    }
    return true;
}

bool ProjectStore::ensureTrustedGit(QString* error) const
{
    if (!m_gitEnabled)
    {
        setError(error, QStringLiteral("Git 未启用"));
        return false;
    }
    QString reason;
    if (!gitRepositoryIsTrusted(m_root, &reason))
    {
        setError(error,
                 QStringLiteral("Git 仓库已不可信，写操作已阻止：%1").arg(reason));
        return false;
    }
    return true;
}

bool ProjectStore::commit(const QString& message, QString* error)
{
    const QString normalized = normalizedMessage(message);

    // Without git the commit is purely an in-memory undo step.
    if (!m_gitEnabled)
    {
        if (!recordTxnUndo(normalized, error))
            return false;
        clearDirty();
        return true;
    }

    if (!ensureTrustedGit(error))
        return false;

    // Git backs up text project data only. assets/, .cache/ and .trash/ are
    // local and ignored, so they are never staged here.
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
        // An asset deletion has no text path to commit, but it still has to be
        // undoable. recordTxnUndo() drops empty text entries and keeps the
        // asset move.
        if (!recordTxnUndo(normalized, error))
            return false;
        clearDirty();
        return true;
    }

    QStringList addArguments{QStringLiteral("add"), QStringLiteral("-A"),
                             QStringLiteral("--")};
    addArguments += addPaths;
    const ProcessResult add = runGit(m_root, addArguments);
    if (add.exitCode != 0)
    {
        const QString failure =
            QStringLiteral("git add 失败：%1").arg(processFailure(add));
        setError(error, failure);
        markCommitFailure(failure);
        return false;
    }

    QStringList diffArguments{QStringLiteral("diff"), QStringLiteral("--cached"),
                              QStringLiteral("--name-only"), QStringLiteral("-z"),
                              QStringLiteral("--")};
    diffArguments += addPaths;
    const ProcessResult diff = runGit(m_root, diffArguments);
    if (diff.exitCode != 0)
    {
        const QString failure =
            QStringLiteral("git diff 失败：%1").arg(processFailure(diff));
        setError(error, failure);
        markCommitFailure(failure);
        return false;
    }

    const QStringList staged = splitNul(diff.out);
    if (staged.isEmpty())
    {
        if (!recordTxnUndo(normalized, error))
            return false;
        clearDirty();
        return true;
    }

    // --only keeps changes the user staged for unrelated files out of this
    // commit, and leaves them staged in the index.
    QString hash;
    if (!commitOnlyPaths(m_root, normalized, staged, &hash, error))
    {
        markCommitFailure(error ? *error : QStringLiteral("git commit 失败"));
        return false;
    }

    // The cold backup now exists; remember exactly what it changed so undo
    // can restore it without touching git.
    if (!recordTxnUndo(normalized, error))
        return false;

    clearDirty();
    clearCommitFailure();
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
    if (!ensureWritable(error) || !ensureTrustedGit(error))
        return false;

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
        const QString reason =
            QStringLiteral("恢复已提交（%1），但重新加载工程失败：%2。"
                           "工程已切换为只读，请重新打开。")
                .arg(newHash.left(7), reloadError);
        setReadOnly(reason);
        setError(error, reason);
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
    if (!ensureWritable(error) || !ensureTrustedGit(error))
        return false;

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
        clearCommitFailure();
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
        markCommitFailure(error ? *error : QStringLiteral("恢复提交失败"));
        return false;
    }

    m_dirtyText.clear();
    clearCommitFailure();
    return true;
}

bool ProjectStore::discardDirtyText(QString* error)
{
    if (!ensureWritable(error) || !ensureTrustedGit(error))
        return false;

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
    clearCommitFailure();
    refreshDigests();
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
    rebuildEntityRootIndex();
    refreshDigests();
    clearDirty();
    refreshDirtyText();
    return true;
}

bool ProjectStore::markCommitFailure(const QString& message)
{
    m_previousCommitFailure = message;
    const QString path =
        absolutePath(m_root, QString::fromLatin1(kCommitFailureMarker));
    QJsonObject marker;
    marker.insert(QStringLiteral("message"), message);
    marker.insert(QStringLiteral("time"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QString error;
    return writeJsonObject(path, marker, &error);
}

void ProjectStore::clearCommitFailure()
{
    QFile::remove(absolutePath(m_root, QString::fromLatin1(kCommitFailureMarker)));
    m_previousCommitFailure.clear();
}

void ProjectStore::clearDirty()
{
    m_txnPaths.clear();
    m_txnBefore.clear();
    m_txnAssets.clear();
    m_txnAssetBefore.clear();
    m_pendingHumanEdit = false;
}

} // namespace pvf::store
