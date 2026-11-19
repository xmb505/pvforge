#include "next/app/ProjectDocument.h"

#include "next/core/EntityLock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <functional>

namespace pvf::next {

namespace {

constexpr int kHumanCommitDelayMs = 1000;
constexpr char kHumanCommitMessage[] = "pvf: human edit";
QString playheadStatePath(const QString& root)
{
    return QDir(root).filePath(QStringLiteral(".pvforge/playhead.json"));
}

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

// Human edit lock shared by every Document write entry point.  The message is
// deliberately actionable: the user can wait for the AI tool or press 急停.
bool ensureHumanWrites(bool enabled, QString* error)
{
    if (enabled)
        return true;
    setError(error, QStringLiteral("AI 正在修改工程，请稍后再编辑，或先按“急停”"));
    return false;
}

// A project that opened read-only (quarantined files, failed reload) must not
// accept panel edits: writing now would silently drop the data that could not
// be loaded.
bool ensureWritable(const store::ProjectStore& store, QString* error)
{
    if (!store.readOnly())
        return true;
    setError(error, QStringLiteral("工程以只读方式打开：%1").arg(store.readOnlyReason()));
    return false;
}

// Export read-only window (issue #126).  The export thread already works on its
// own snapshot; this keeps the live project and its assets stable while the
// MP4 is encoded.
bool ensureNotExportLocked(bool locked, QString* error)
{
    if (!locked)
        return true;
    setError(error, QStringLiteral("导出进行中，工程暂时只读"));
    return false;
}

// Validates the project as it will exist after the write.  Upserts are already
// present in the in-memory project; deletes are validated as absent without
// copying the project first (model::validateProjectWithout treats them as
// already erased).  A locked ancestor rejects the delete: every descendant of a
// locked entity fails its own check, so the delete order cannot hide one.
bool validateForWrite(const model::Project& project,
                      const QVector<Uuid>& upserts,
                      const QVector<Uuid>& deletes, QString* error)
{
    for (const Uuid& uuid : upserts)
    {
        if (!project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return false;
        }
        // A locked ancestor owns the whole subtree: the stage and MCP must not
        // bypass it just because the entity's own locked flag is false.  The
        // entity's own flag is intentionally ignored so it can be unlocked.
        if (hasLockedAncestor(project, uuid))
        {
            setError(error,
                     QStringLiteral("实体 %1 的祖先已锁定，不能修改")
                         .arg(project.entities.value(uuid).name.isEmpty()
                                  ? uuid
                                  : project.entities.value(uuid).name));
            return false;
        }
    }

    if (deletes.isEmpty())
        return model::validateProject(project, error);

    QSet<Uuid> removed;
    removed.reserve(deletes.size());
    for (const Uuid& uuid : deletes)
    {
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd() || removed.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return false;
        }
        if (isEntityLocked(project, uuid))
        {
            setError(error,
                     QStringLiteral("实体 %1 或其祖先已锁定，不能删除")
                         .arg(it.value().name.isEmpty() ? uuid
                                                        : it.value().name));
            return false;
        }
        removed.insert(uuid);
    }

    return model::validateProjectWithout(project, removed, error);
}

// The store removes a whole subtree together with its top-most node.  Resolve
// the top-most nodes against the project as it exists before any delete: once
// a root is gone its descendants disappear from the live model, so a later
// parent lookup would see an empty parentUuid and try to delete the same
// subtree a second time (the delete then fails with 实体不存在).
QVector<Uuid> topMostNodes(const model::Project& project,
                           const QVector<Uuid>& nodes)
{
    QSet<Uuid> requested(nodes.begin(), nodes.end());
    QVector<Uuid> roots;
    roots.reserve(nodes.size());
    for (const Uuid& uuid : nodes)
    {
        bool hasRequestedAncestor = false;
        Uuid parent = project.entities.value(uuid).parentUuid;
        while (!parent.isEmpty())
        {
            if (requested.contains(parent))
            {
                hasRequestedAncestor = true;
                break;
            }
            const auto it = project.entities.constFind(parent);
            if (it == project.entities.constEnd())
                break;
            parent = it->parentUuid;
        }
        if (!hasRequestedAncestor)
            roots.append(uuid);
    }
    return roots;
}

} // namespace

ProjectDocument::ProjectDocument(std::unique_ptr<store::ProjectStore> store,
                                 QObject* parent)
    : Document(parent)
    , m_store(std::move(store))
{
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(kHumanCommitDelayMs);
    connect(&m_commitTimer, &QTimer::timeout, this, [this] {
        QString error;
        flush(&error);
    });
}

ProjectDocument::~ProjectDocument()
{
    // The playhead lives outside project.json so restoring it never shows up
    // as a git change.
    savePlayheadState();
    // Best effort cold backup: a document can be destroyed without going
    // through the window's closeEvent (tests, project replacement, ...).
    QString ignored;
    flush(&ignored);
}

std::unique_ptr<ProjectDocument> ProjectDocument::create(
    const QString& projectDir, const model::ProjectSettings& settings, QString* error,
    bool useGit)
{
    auto store = store::ProjectStore::create(projectDir, settings, error, useGit);
    if (!store)
        return nullptr;
    return std::unique_ptr<ProjectDocument>(
        new ProjectDocument(std::make_unique<store::ProjectStore>(std::move(*store))));
}

std::unique_ptr<ProjectDocument> ProjectDocument::open(const QString& projectDir,
                                                       QString* error, bool useGit)
{
    auto store = store::ProjectStore::open(projectDir, error, useGit);
    if (!store)
        return nullptr;
    auto document = std::unique_ptr<ProjectDocument>(
        new ProjectDocument(std::make_unique<store::ProjectStore>(std::move(*store))));
    document->loadPlayheadState();
    return document;
}

bool ProjectDocument::setGitEnabled(bool enabled, QString* error)
{
    return m_store->setGitEnabled(enabled, error);
}

const model::Project& ProjectDocument::project() const
{
    return m_store->project();
}

model::Project& ProjectDocument::mutableProject()
{
    return m_store->mutableProject();
}

assets::AssetStore& ProjectDocument::assets()
{
    return m_store->assets();
}

int ProjectDocument::playhead() const
{
    return m_playhead;
}

void ProjectDocument::setPlayhead(int frame)
{
    const int clamped = frame < 0 ? 0 : frame;
    if (clamped == m_playhead)
        return;
    m_playhead = clamped;
    emit playheadChanged();
}

Uuid ProjectDocument::selection() const
{
    return m_selection;
}

void ProjectDocument::setSelection(const Uuid& uuid)
{
    Uuid next = uuid;
    if (!next.isEmpty() && !m_store->project().entities.contains(next))
        next.clear();
    if (next == m_selection)
        return;
    m_selection = next;
    emit selectionChanged();
}

bool ProjectDocument::commitEntity(const Uuid& uuid, const QString& message,
                                   QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;
    if (!validateForWrite(m_store->project(), {uuid}, {}, error))
        return false;
    if (!m_store->saveEntity(uuid, error))
        return false;
    scheduleCommit(message);
    emit changed();
    return true;
}

bool ProjectDocument::commitEntities(const QVector<Uuid>& uuids, const QString& message,
                                     QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;
    if (!validateForWrite(m_store->project(), uuids, {}, error))
        return false;
    for (const Uuid& uuid : uuids)
    {
        if (!m_store->saveEntity(uuid, error))
            return false;
    }
    scheduleCommit(message);
    emit changed();
    return true;
}

bool ProjectDocument::deleteEntities(const QVector<Uuid>& uuids, const QString& message,
                                     QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;

    const model::Project& project = m_store->project();
    QSet<Uuid> requested;
    for (const Uuid& uuid : uuids)
    {
        if (!project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return false;
        }
        if (requested.contains(uuid))
        {
            setError(error, QStringLiteral("重复删除实体：%1").arg(uuid));
            return false;
        }
        requested.insert(uuid);
    }

    // Deleting a folder entity removes its whole subtree.  Validate the
    // expanded set once, but only ask the store to delete the top-most nodes:
    // ProjectStore already removes descendants and their files.
    QSet<Uuid> removed;
    std::function<void(const Uuid&)> collect = [&](const Uuid& uuid) {
        if (removed.contains(uuid))
            return;
        removed.insert(uuid);
        for (const model::Entity& entity : project.entities)
            if (entity.parentUuid == uuid)
                collect(entity.uuid);
    };
    for (const Uuid& uuid : requested)
        collect(uuid);

    QVector<Uuid> validation;
    validation.reserve(removed.size());
    for (const Uuid& uuid : removed)
        validation.append(uuid);
    if (!validateForWrite(project, {}, validation, error))
        return false;

    // Resolve the top-most requested nodes before deleting anything: the store
    // removes descendants with their root, so a live parent lookup after the
    // first delete would see the descendant's parent link as empty and try to
    // delete the same subtree twice.  QSet iteration order is unspecified,
    // which is why that failure only showed up on some runs.
    QVector<Uuid> requestedList;
    requestedList.reserve(requested.size());
    for (const Uuid& uuid : requested)
        requestedList.append(uuid);
    const QVector<Uuid> roots = topMostNodes(project, requestedList);
    for (const Uuid& uuid : roots)
    {
        if (!m_store->deleteEntity(uuid, error))
            return false;
    }

    for (const Uuid& uuid : removed)
        if (uuid == m_selection)
            m_selection.clear();
    scheduleCommit(message);
    emit changed();
    return true;
}

bool ProjectDocument::commitChanges(const QVector<Uuid>& upserts,
                                    const QVector<Uuid>& deletes,
                                    const QString& message,
                                    QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;
    if (!validateForWrite(m_store->project(), upserts, deletes, error))
        return false;
    for (const Uuid& uuid : upserts)
    {
        if (!m_store->saveEntity(uuid, error))
            return false;
    }
    // One plan can carry a whole subtree in `deletes` (a trim can drop a
    // parent and its children).  Delete only the top-most nodes: the store
    // removes their descendants, and deleting the same subtree twice fails.
    for (const Uuid& uuid : topMostNodes(m_store->project(), deletes))
    {
        if (!m_store->deleteEntity(uuid, error))
            return false;
    }
    if (!m_selection.isEmpty()
        && !m_store->project().entities.contains(m_selection))
        m_selection.clear();
    scheduleCommit(message);
    emit changed();
    return true;
}

bool ProjectDocument::commitProject(const QString& message, QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;
    if (!validateForWrite(m_store->project(), {}, {}, error))
        return false;
    if (!m_store->saveProject(error))
        return false;
    scheduleCommit(message);
    emit changed();
    return true;
}

void ProjectDocument::flush(QString* error)
{
    if (error)
        error->clear();

    m_commitTimer.stop();

    // A read-only project never wrote anything; flush has nothing to commit.
    const QString message = m_pendingMessage.isEmpty()
                                ? QString::fromLatin1(kHumanCommitMessage)
                                : m_pendingMessage;

    QString commitError;
    if (!m_store->commit(message, &commitError))
    {
        if (error)
            *error = commitError;
        emit commitFailed(commitError);
        return;
    }

    m_pendingMessage.clear();
    if (m_pendingCommit)
    {
        m_pendingCommit = false;
        emit pendingCommitChanged(false);
    }
}

bool ProjectDocument::hasPendingCommit() const
{
    return m_pendingCommit;
}

void ProjectDocument::setHumanCommitDelayMs(int ms)
{
    const int delay = std::max(0, ms);
    if (m_commitTimer.interval() == delay)
        return;
    // QTimer::setInterval restarts a running single-shot timer, so a pending
    // edit commits with the new debounce instead of keeping the old deadline.
    m_commitTimer.setInterval(delay);
}

int ProjectDocument::humanCommitDelayMs() const
{
    return m_commitTimer.interval();
}

bool ProjectDocument::undo(QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;

    // Materialize the debounced edit first: it becomes the newest undo layer,
    // so Ctrl+Z within the debounce window undoes exactly the current edit
    // instead of jumping over it to an older snapshot.
    if (m_pendingCommit)
    {
        QString flushError;
        flush(&flushError);
        if (!flushError.isEmpty())
        {
            setError(error,
                     QStringLiteral("撤销前提交当前编辑失败：%1").arg(flushError));
            return false;
        }
    }

    if (!m_store->undo(error))
        return false;

    reloadAfterHistory(error);
    return true;
}

bool ProjectDocument::redo(QString* error)
{
    if (!ensureNotExportLocked(m_writesLocked, error)
        || !ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!ensureWritable(*m_store, error))
        return false;

    if (m_pendingCommit)
    {
        QString flushError;
        flush(&flushError);
        if (!flushError.isEmpty())
        {
            setError(error,
                     QStringLiteral("重做前提交当前编辑失败：%1").arg(flushError));
            return false;
        }
    }

    if (!m_store->redo(error))
        return false;

    reloadAfterHistory(error);
    return true;
}

void ProjectDocument::scheduleCommit(const QString& message)
{
    m_commitTimer.start();
    if (!m_pendingCommit)
    {
        m_pendingCommit = true;
        m_pendingMessage = message.trimmed().isEmpty()
                               ? QString::fromLatin1(kHumanCommitMessage)
                               : message;
        emit pendingCommitChanged(true);
    }
    else if (!message.trimmed().isEmpty())
    {
        // Coalesced edits share one commit; the newest message describes the
        // gesture that is still in flight.
        m_pendingMessage = message;
    }
}

QString ProjectDocument::root() const
{
    return m_store->root();
}

int ProjectDocument::contentEnd() const
{
    // Same definition as PlaybackController::contentEnd(): the furthest frame
    // reached by any root entity.  Private timeline children stay inside their
    // parent's range.
    int end = 0;
    const auto& entities = m_store->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it)
    {
        if (it.value().parentUuid.isEmpty())
            end = std::max(end, it.value().end);
    }
    return std::max(0, end);
}

void ProjectDocument::savePlayheadState() const
{
    const QString path = playheadStatePath(m_store->root());
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;

    QJsonObject object;
    object.insert(QStringLiteral("frame"), m_playhead);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    // Best effort: a read-only project keeps working without a saved playhead.
    file.commit();
}

void ProjectDocument::loadPlayheadState()
{
    QFile file(playheadStatePath(m_store->root()));
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonValue value = document.object().value(QStringLiteral("frame"));
    if (!value.isDouble())
        return;

    // With content, the playhead must stay inside it.  An empty project has an
    // open timeline (docs/AI/DESIGN.md), so only the toolbar spin box's sanity
    // bound applies.
    const int end = contentEnd();
    const int frame = value.toInt();
    m_playhead = end > 0 ? std::clamp(frame, 0, end)
                         : std::clamp(frame, 0, 100000);
}

void ProjectDocument::reloadAfterHistory(QString* error)
{
    if (!m_selection.isEmpty() && !m_store->project().entities.contains(m_selection))
        m_selection.clear();
    emit changed();
    Q_UNUSED(error);
}

bool ProjectDocument::notifyExternalChange(QString* error)
{
    // Playhead and selection live on the Document, not in the store, so a
    // store reload cannot lose them.  A selection whose entity disappeared
    // (deleted by the MCP tool) is cleared instead of dangling.
    const Uuid selection = m_selection;
    const bool reloaded = m_store->reloadAfterDiscard(error);
    if (selection.isEmpty()
        || m_store->project().entities.contains(selection))
        m_selection = selection;
    else
        m_selection.clear();

    if (!reloaded)
    {
        // Continuing with the old in-memory model would let the next human
        // save overwrite the AI's on-disk result. Refuse writes until the
        // project is reopened and loaded from disk again.
        const QString reason =
            QStringLiteral("重新加载工程失败：%1。为避免旧内存覆盖磁盘，"
                           "工程已切换为只读，请重新打开工程。")
                .arg(error && !error->isEmpty() ? *error
                                                : QStringLiteral("未知错误"));
        m_store->setReadOnly(reason);
        if (error)
            *error = reason;
    }

    // Refresh even when the reload failed: the service may have partially
    // written assets or text and the panels must not keep the old picture.
    emit changed();
    return reloaded;
}

} // namespace pvf::next
