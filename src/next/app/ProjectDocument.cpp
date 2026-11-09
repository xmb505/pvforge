#include "next/app/ProjectDocument.h"

namespace pvf::next {

ProjectDocument::ProjectDocument(std::unique_ptr<store::ProjectStore> store,
                                 QObject* parent)
    : Document(parent)
    , m_store(std::move(store))
{
}

ProjectDocument::~ProjectDocument() = default;

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
    if (!m_store->saveEntity(uuid, error))
        return false;
    if (!m_store->commit(message, error))
        return false;
    emit changed();
    return true;
}

bool ProjectDocument::commitEntities(const QVector<Uuid>& uuids, const QString& message,
                                     QString* error)
{
    for (const Uuid& uuid : uuids)
    {
        if (!m_store->saveEntity(uuid, error))
            return false;
    }
    if (!m_store->commit(message, error))
        return false;
    emit changed();
    return true;
}

bool ProjectDocument::commitProject(const QString& message, QString* error)
{
    if (!m_store->saveProject(error))
        return false;
    if (!m_store->commit(message, error))
        return false;
    emit changed();
    return true;
}

bool ProjectDocument::undo(QString* error)
{
    if (!m_store->undo(error))
        return false;
    reloadAfterHistory(error);
    return true;
}

bool ProjectDocument::redo(QString* error)
{
    if (!m_store->redo(error))
        return false;
    reloadAfterHistory(error);
    return true;
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
