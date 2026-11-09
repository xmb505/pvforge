#pragma once

#include "assets/Assets.h"
#include "common/Types.h"
#include "model/Model.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>

namespace pvf::store {

struct GitCommit
{
    QString hash;
    QString message;
    QDateTime time;
};

// A .pvproj directory backed by git.
//
// Layout:
//   project.json
//   entities/<uuid>.json
//   assets/                 (owned by assets::AssetStore, ignored by git)
//   .cache/                 (local caches, ignored by git)
//   .gitignore
//
// Git only manages text project data. Media in assets/ and any cache files are
// local to the machine and never committed.
//
// The store keeps the in-memory model and the files in sync. Writes are
// atomic (QSaveFile). commit() stages only text paths owned by the
// application (project.json and the entity files touched through this store);
// unrelated files the user changed are never included.
//
// undo()/redo() are implemented with git revert, one operation commit at a
// time. The undo/redo stacks are rebuilt from the pvf: commit log when a
// project is opened, so undo keeps working across sessions. Commit messages
// starting with "pvf: undo " or "pvf: redo " are reserved for the store.
class ProjectStore
{
public:
    static std::optional<ProjectStore> create(const QString& projectDir,
                                              const model::ProjectSettings& settings,
                                              QString* error = nullptr);
    static std::optional<ProjectStore> open(const QString& projectDir,
                                            QString* error = nullptr);

    const model::Project& project() const { return m_project; }
    model::Project& mutableProject() { return m_project; }

    bool saveEntity(const Uuid& uuid, QString* error = nullptr);
    bool saveProject(QString* error = nullptr);
    bool deleteEntity(const Uuid& uuid, QString* error = nullptr);
    bool commit(const QString& message, QString* error = nullptr);

    bool undo(QString* error = nullptr);
    bool redo(QString* error = nullptr);
    QVector<GitCommit> history(int limit = 50, QString* error = nullptr) const;

    assets::AssetStore& assets() { return *m_assets; }
    QString root() const { return m_root; }

private:
    struct RedoEntry
    {
        QString operation;
        QString revert;
    };

    ProjectStore(QString root, model::Project project);

    bool reload(QString* error);
    bool rebuildStacks(QString* error);
    void clearDirty();

    QString m_root;
    model::Project m_project;
    std::optional<assets::AssetStore> m_assets;
    QVector<QString> m_undoStack;
    QVector<RedoEntry> m_redoStack;
    QSet<QString> m_dirtyEntities;
    bool m_projectDirty = false;
};

} // namespace pvf::store
