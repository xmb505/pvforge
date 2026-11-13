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

// One uncommitted text path left behind by a previous session (crash
// recovery). `path` is relative to the project root; `status` is a readable
// summary of the git porcelain status: "modified", "added", "deleted",
// "renamed", "copied", "typechange" or "conflict".
struct DirtyText
{
    QString path;
    QString status;
};

// One in-memory undo step. Paths are relative to the project root and only
// cover text project data (project.json, entities/<root-uuid>.json); assets
// never enter the stack. An empty byte array means "the file did not exist".
struct UndoEntry
{
    QHash<QString, QByteArray> before;
    QHash<QString, QByteArray> after;
    QString message;
};

// A .pvproj directory, optionally backed by git.
//
// Git is optional. With git disabled (or unavailable) the store still writes
// text project data atomically and keeps the in-memory undo/redo stack, but it
// never runs git and no history survives a restart.
//
// Layout:
//   project.json            (settings + markers only; no entity list)
//   entities/<root-uuid>.json  (one recursive root subtree per file)
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
    // `useGit` only expresses intent. When git is missing, both
    // create(useGit=true) and open(useGit=true) fall back to a plain text store
    // (gitEnabled() == false) with in-memory undo instead of failing.
    static std::optional<ProjectStore> create(const QString& projectDir,
                                              const model::ProjectSettings& settings,
                                              QString* error = nullptr,
                                              bool useGit = true);
    static std::optional<ProjectStore> open(const QString& projectDir,
                                            QString* error = nullptr,
                                            bool useGit = true);

    // Whether this store is actually using git right now.
    bool gitEnabled() const { return m_gitEnabled; }

    // True when a `git --version` probe succeeds; `version` receives its
    // trimmed output when provided.
    static bool gitBinaryAvailable(QString* version = nullptr);

    // Enables git for this project. When the project has no repository yet,
    // writes .gitignore (if missing), runs git init and creates the initial
    // "pvf: init" commit. Disabling never deletes .git or the undo stack.
    bool setGitEnabled(bool enabled, QString* error = nullptr);

    const model::Project& project() const { return m_project; }
    model::Project& mutableProject() { return m_project; }

    bool saveEntity(const Uuid& uuid, QString* error = nullptr);
    bool saveProject(QString* error = nullptr);
    bool deleteEntity(const Uuid& uuid, QString* error = nullptr);
    bool commit(const QString& message, QString* error = nullptr);

    bool undo(QString* error = nullptr);
    bool redo(QString* error = nullptr);
    QVector<GitCommit> history(int limit = 50, QString* error = nullptr) const;

    // Makes the tracked text tree (project.json, entities/*.json, .gitignore)
    // identical to `hash`, then records it as a new commit
    // "pvf: restore <short>". The text working tree must be clean; dirty text
    // paths make this fail without discarding anything. The in-memory
    // undo/redo stacks are cleared and the project reloaded. Only a new commit
    // is created: HEAD never moves backwards and no reset is performed.
    bool restoreCommit(const QString& hash, QString* error = nullptr);

    // Uncommitted text project changes seen when the project was opened.
    // open() scans once; nothing is recovered or discarded automatically.
    const QVector<DirtyText>& dirtyText() const { return m_dirtyText; }

    // Scans `git status` for uncommitted text project paths (project.json,
    // entities/*.json, .gitignore). assets/ and .cache/ are ignored by git and
    // never reported.
    QVector<DirtyText> dirtyTextFiles(QString* error = nullptr) const;

    // Commits the current uncommitted text paths as "pvf: recover". Files the
    // user staged outside the text project paths stay staged and uncommitted.
    bool commitRecoveredText(QString* error = nullptr);

    // Discards uncommitted text changes: tracked files are restored from HEAD
    // and untracked new text files are deleted. assets/, .cache/ and unrelated
    // files are never touched.
    bool discardDirtyText(QString* error = nullptr);

    // Reloads the in-memory project from disk after discarding (or when the
    // files changed externally). Refreshes dirtyText().
    bool reloadAfterDiscard(QString* error = nullptr);

    int undoDepth() const { return int(m_undoStack.size()); }
    int redoDepth() const { return int(m_redoStack.size()); }

#ifdef PVFORGE_TEST_HOOKS
    // Test hook: shrink the default 200 entries / 512MB caps so eviction can
    // be exercised without hundreds of real commits.
    void setUndoLimitsForTest(int maxEntries, qint64 maxBytes);
#endif

    assets::AssetStore& assets() { return *m_assets; }
    QString root() const { return m_root; }

private:
    ProjectStore(QString root, model::Project project);

    bool reload(QString* error);
    void refreshDirtyText();
    void clearDirty();
    void notePath(const QString& relativePath);
    void recordTxnUndo(const QString& message);
    void pushUndo(UndoEntry entry);
    void trimStack(QVector<UndoEntry>& stack);
    bool applySnapshot(const QHash<QString, QByteArray>& snapshot,
                       QString* error);
    Uuid entityRootOf(const Uuid& uuid, QString* error = nullptr) const;
    void rebuildEntityRootIndex();
    bool writeEntityTree(const Uuid& rootUuid, QString* error);
    bool removeEntityTreeFile(const Uuid& rootUuid, QString* error);

    QString m_root;
    model::Project m_project;
    std::optional<assets::AssetStore> m_assets;
    QVector<UndoEntry> m_undoStack;
    QVector<UndoEntry> m_redoStack;
    QVector<DirtyText> m_dirtyText;
    // UUID -> root UUID of the entity file that held it at the last successful
    // load/save/delete. saveEntity() uses this to rewrite the old root file
    // when a subtree is reparented; mutableProject() can therefore be edited
    // freely before the next save.
    QHash<Uuid, Uuid> m_entityRoots;
    // Paths touched since the last successful commit, with the bytes they had
    // before the first touch (empty when the file did not exist).
    QHash<QString, QByteArray> m_txnBefore;
    QSet<QString> m_txnPaths;
    bool m_gitEnabled = false;
    int m_undoLimit = 200;
    qint64 m_undoByteLimit = 512LL * 1024 * 1024;
};

} // namespace pvf::store
