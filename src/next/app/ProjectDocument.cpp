#include "next/app/ProjectDocument.h"

#include "next/core/EntityLock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

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

// Validates the project as it will exist after the write.  Upserts are already
// present in the in-memory project; deletes are removed from a copy first.
bool validateForWrite(const model::Project& project,
                      const QVector<Uuid>& upserts,
                      const QVector<Uuid>& deletes, QString* error)
{
    model::Project check = project;
    for (const Uuid& uuid : upserts)
    {
        if (!check.entities.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return false;
        }
    }

    for (const Uuid& uuid : deletes)
    {
        const auto it = check.entities.constFind(uuid);
        if (it == check.entities.constEnd())
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return false;
        }
        if (isEntityLocked(check, uuid))
        {
            setError(error,
                     QStringLiteral("实体 %1 或其祖先已锁定，不能删除")
                         .arg(it.value().name.isEmpty() ? uuid
                                                        : it.value().name));
            return false;
        }
        check.entities.remove(uuid);
    }

    return model::validateProject(check, error);
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
    Q_UNUSED(message);
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!validateForWrite(m_store->project(), {uuid}, {}, error))
        return false;
    if (!m_store->saveEntity(uuid, error))
        return false;
    scheduleCommit();
    emit changed();
    return true;
}

bool ProjectDocument::commitEntities(const QVector<Uuid>& uuids, const QString& message,
                                     QString* error)
{
    Q_UNUSED(message);
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!validateForWrite(m_store->project(), uuids, {}, error))
        return false;
    for (const Uuid& uuid : uuids)
    {
        if (!m_store->saveEntity(uuid, error))
            return false;
    }
    scheduleCommit();
    emit changed();
    return true;
}

bool ProjectDocument::deleteEntities(const QVector<Uuid>& uuids, const QString& message,
                                     QString* error)
{
    Q_UNUSED(message);
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!validateForWrite(m_store->project(), {}, uuids, error))
        return false;
    for (const Uuid& uuid : uuids)
    {
        if (!m_store->deleteEntity(uuid, error))
            return false;
        if (uuid == m_selection)
            m_selection.clear();
    }
    scheduleCommit();
    emit changed();
    return true;
}

bool ProjectDocument::commitChanges(const QVector<Uuid>& upserts,
                                    const QVector<Uuid>& deletes,
                                    const QString& message,
                                    QString* error)
{
    Q_UNUSED(message);
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!validateForWrite(m_store->project(), upserts, deletes, error))
        return false;
    for (const Uuid& uuid : upserts)
    {
        if (!m_store->saveEntity(uuid, error))
            return false;
    }
    for (const Uuid& uuid : deletes)
    {
        if (!m_store->deleteEntity(uuid, error))
            return false;
        if (uuid == m_selection)
            m_selection.clear();
    }
    scheduleCommit();
    emit changed();
    return true;
}

bool ProjectDocument::commitProject(const QString& message, QString* error)
{
    Q_UNUSED(message);
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    if (!validateForWrite(m_store->project(), {}, {}, error))
        return false;
    if (!m_store->saveProject(error))
        return false;
    scheduleCommit();
    emit changed();
    return true;
}

void ProjectDocument::flush(QString* error)
{
    if (error)
        error->clear();

    m_commitTimer.stop();

    QString commitError;
    if (!m_store->commit(QString::fromLatin1(kHumanCommitMessage), &commitError))
    {
        if (error)
            *error = commitError;
        emit commitFailed(commitError);
        return;
    }

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
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    // Do not flush the pending edit first: Ctrl+Z applies the last memory
    // snapshot and drops the debounce. If undo itself fails, keep the pending
    // edit alive so it can still reach git later.
    const bool wasPending = m_pendingCommit;
    m_commitTimer.stop();
    m_pendingCommit = false;

    if (!m_store->undo(error))
    {
        if (wasPending)
        {
            m_pendingCommit = true;
            m_commitTimer.start();
        }
        return false;
    }

    if (wasPending)
        emit pendingCommitChanged(false);

    reloadAfterHistory(error);
    return true;
}

bool ProjectDocument::redo(QString* error)
{
    if (!ensureHumanWrites(m_humanWritesEnabled, error))
        return false;
    const bool wasPending = m_pendingCommit;
    m_commitTimer.stop();
    m_pendingCommit = false;

    if (!m_store->redo(error))
    {
        if (wasPending)
        {
            m_pendingCommit = true;
            m_commitTimer.start();
        }
        return false;
    }

    if (wasPending)
        emit pendingCommitChanged(false);

    reloadAfterHistory(error);
    return true;
}

void ProjectDocument::scheduleCommit()
{
    m_commitTimer.start();
    if (!m_pendingCommit)
    {
        m_pendingCommit = true;
        emit pendingCommitChanged(true);
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
    // open timeline (docs/DESIGN.md), so only the toolbar spin box's sanity
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

    // Refresh even when the reload failed: the service may have partially
    // written assets or text and the panels must not keep the old picture.
    emit changed();
    return reloaded;
}

} // namespace pvf::next
