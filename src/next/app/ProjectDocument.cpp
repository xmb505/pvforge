#include "next/app/ProjectDocument.h"

namespace pvf::next {

namespace {

constexpr int kHumanCommitDelayMs = 1000;
constexpr char kHumanCommitMessage[] = "pvf: human edit";

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
    // Best effort cold backup: a document can be destroyed without going
    // through the window's closeEvent (tests, project replacement, ...).
    QString ignored;
    flush(&ignored);
}

std::unique_ptr<ProjectDocument> ProjectDocument::create(
    const QString& projectDir, const model::ProjectSettings& settings, QString* error)
{
    auto store = store::ProjectStore::create(projectDir, settings, error);
    if (!store)
        return nullptr;
    return std::unique_ptr<ProjectDocument>(
        new ProjectDocument(std::make_unique<store::ProjectStore>(std::move(*store))));
}

std::unique_ptr<ProjectDocument> ProjectDocument::open(const QString& projectDir,
                                                       QString* error)
{
    auto store = store::ProjectStore::open(projectDir, error);
    if (!store)
        return nullptr;
    return std::unique_ptr<ProjectDocument>(
        new ProjectDocument(std::make_unique<store::ProjectStore>(std::move(*store))));
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

bool ProjectDocument::undo(QString* error)
{
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

void ProjectDocument::reloadAfterHistory(QString* error)
{
    if (!m_selection.isEmpty() && !m_store->project().entities.contains(m_selection))
        m_selection.clear();
    emit changed();
    Q_UNUSED(error);
}

} // namespace pvf::next
