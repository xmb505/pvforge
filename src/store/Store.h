#pragma once

#include "assets/Assets.h"
#include "common/Types.h"
#include "model/Model.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
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

// One in-memory undo step. Paths are relative to the project root and only
// cover text project data (project.json, entities/*.json); assets never enter
// the stack. An empty byte array means "the file did not exist".
struct UndoEntry
{
    QHash<QString, QByteArray> before;
    QHash<QString, QByteArray> after;
    QString message;
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
// Undo/redo is in-memory: every save/delete captures the previous bytes of a
// path the first time it is touched, and a successful commit() turns the
// touched paths into an UndoEntry. undo()/redo() write the snapshot back and
// reload the model; they never run git. Git is only the cold backup.
//
// The stacks are not persisted: after reopening a project the memory history
// is empty and only git history remains.
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

    int undoDepth() const { return int(m_undoStack.size()); }
    int redoDepth() const { return int(m_redoStack.size()); }

    // Test hook: shrink the default 200 entries / 512MB caps so eviction can
    // be exercised without hundreds of real commits.
    void setUndoLimitsForTest(int maxEntries, qint64 maxBytes);

    assets::AssetStore& assets() { return *m_assets; }
    QString root() const { return m_root; }

private:
    ProjectStore(QString root, model::Project project);

    bool reload(QString* error);
    void clearDirty();
    void notePath(const QString& relativePath);
    void pushUndo(UndoEntry entry);
    void trimStack(QVector<UndoEntry>& stack);
    bool applySnapshot(const QHash<QString, QByteArray>& snapshot,
                       QString* error);

    QString m_root;
    model::Project m_project;
    std::optional<assets::AssetStore> m_assets;
    QVector<UndoEntry> m_undoStack;
    QVector<UndoEntry> m_redoStack;
    // Paths touched since the last successful commit, with the bytes they had
    // before the first touch (empty when the file did not exist).
    QHash<QString, QByteArray> m_txnBefore;
    QSet<QString> m_txnPaths;
    int m_undoLimit = 200;
    qint64 m_undoByteLimit = 512LL * 1024 * 1024;
};

} // namespace pvf::store
