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

#include <memory>
#include <optional>

namespace pvf::store::detail {
struct ProjectLock;
struct TransactionOp;
} // namespace pvf::store::detail

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

// One entity file quarantined while opening a project because it was corrupt
// or repeated a UUID already loaded from another file. The file is moved to
// `.pvforge/quarantine/`; the project opens read-only until the human decides
// what to keep.
struct QuarantinedFile
{
    QString path;
    QString reason;
};

// One in-memory undo step. Text paths are relative to the project root and
// cover project.json and entities/<root-uuid>.json. Assets are tracked by hash:
// a hash in the "trash" set means the asset directory lives in `.trash/<hash>`
// instead of `assets/<hash>`. An empty byte array means "the file did not
// exist".
struct UndoEntry
{
    QHash<QString, QByteArray> before;
    QHash<QString, QByteArray> after;
    QHash<QString, bool> assetsInTrashBefore;
    QHash<QString, bool> assetsInTrashAfter;
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
//   .trash/<hash>/          (deleted assets kept for undo, ignored by git)
//   .pvforge/               (local state: lock, journal, quarantine, playhead)
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

    // Read-only mode is entered when the project was opened with quarantined
    // files or when a reload after a write failed. Every write entry point
    // refuses and names the reason; reading and git history keep working.
    bool readOnly() const { return !m_readOnlyReason.isEmpty(); }
    QString readOnlyReason() const { return m_readOnlyReason; }
    void setReadOnly(const QString& reason);
    const QVector<QuarantinedFile>& quarantinedFiles() const
    {
        return m_quarantined;
    }

    // True when a human edit was saved but not committed to git yet. The MCP
    // history tools flush this before undoing so an edit in the debounce window
    // is not silently overwritten. Snapshots written back by undo/redo are not
    // counted: they are already on disk and the next save commits them.
    bool hasPendingWrites() const { return m_pendingHumanEdit; }

    // Message recorded by markCommitFailure() for the previous close, empty
    // when the last session ended cleanly. It lets the recovery dialog tell a
    // failed git commit apart from an abnormal exit.
    QString previousCommitFailure() const { return m_previousCommitFailure; }
    bool markCommitFailure(const QString& message);
    void clearCommitFailure();

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

    // Moves assets/<hash> to `.trash/<hash>` and records the move in the
    // pending transaction; commit() turns it into an undo step that puts the
    // directory back. AssetStore::remove() stays a permanent physical delete
    // for import rollbacks.
    bool trashAsset(const AssetHash& hash, QString* error = nullptr);
    // Entities whose content references `hash`.
    QStringList entitiesReferencingAsset(const AssetHash& hash) const;
    QSet<QString> trashedAssets() const;
    // Moves assets so each hash in `trashed` lives in .trash/<hash> and every
    // other hash lives in assets/<hash>. Missing content is left untouched
    // (undo of a deletion cannot resurrect bytes the user never had).
    bool applyAssetTrashState(const QHash<QString, bool>& trashed,
                              QString* error = nullptr);

    // Turns the pending writes into an undo entry without running git. Used
    // when a rollback failed and the human still needs a way to revert.
    bool recordPendingAsUndo(const QString& message,
                             QString* error = nullptr);

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
    // Test hook: fail the (0-based) nth staged file write inside the next file
    // transaction. -1 disables the hook.
    void setWriteFailureForTest(int index) { m_writeFailureForTest = index; }
    // Test hook: leave a transaction journal on disk after applying `count`
    // operations, as if the process were killed mid-save.
    void setCrashAfterApplyForTest(int count)
    {
        m_crashAfterApplyForTest = count;
    }
#endif

    assets::AssetStore& assets() { return *m_assets; }
    QString root() const { return m_root; }

private:
    ProjectStore(QString root, model::Project project,
                 std::shared_ptr<detail::ProjectLock> lock);

    bool ensureWritable(QString* error) const;
    bool reload(QString* error);
    void refreshDirtyText();
    void clearDirty();
    void refreshDigests();
    bool verifyDigests(const QVector<detail::TransactionOp>& ops,
                       QString* error) const;
    bool writeTrackedFiles(const QVector<detail::TransactionOp>& ops,
                           QString* error);
    void noteExistingPath(const QString& relativePath);
    void noteAssetTouch(const QString& hash);
    bool recordTxnUndo(const QString& message, QString* error);
    void pushUndo(UndoEntry entry);
    void trimStack(QVector<UndoEntry>& stack);
    bool applySnapshot(const QHash<QString, QByteArray>& snapshot,
                       const QHash<QString, bool>& assetsInTrash,
                       QString* error);
    Uuid entityRootOf(const Uuid& uuid, QString* error = nullptr) const;
    void rebuildEntityRootIndex();

    QString m_root;
    model::Project m_project;
    std::optional<assets::AssetStore> m_assets;
    // Shared so several ProjectStore objects in one process (tests, a future
    // multi-view shell) share one project lock; the QLockFile is released when
    // the last reference dies.
    std::shared_ptr<detail::ProjectLock> m_lock;
    QVector<UndoEntry> m_undoStack;
    QVector<UndoEntry> m_redoStack;
    QVector<DirtyText> m_dirtyText;
    QVector<QuarantinedFile> m_quarantined;
    QString m_readOnlyReason;
    QString m_previousCommitFailure;
    // UUID -> root UUID of the entity file that held it at the last successful
    // load/save/delete. saveEntity() uses this to rewrite the old root file
    // when a subtree is reparented; mutableProject() can therefore be edited
    // freely before the next save.
    QHash<Uuid, Uuid> m_entityRoots;
    // Paths touched since the last successful commit, with the bytes they had
    // before the first touch (empty when the file did not exist).
    QHash<QString, QByteArray> m_txnBefore;
    QSet<QString> m_txnPaths;
    // A real edit (not an undo/redo snapshot) is waiting for its debounced
    // git commit.
    bool m_pendingHumanEdit = false;
    // Asset hashes touched since the last commit; `true` means the directory
    // was in .trash before this transaction touched it.
    QHash<QString, bool> m_txnAssetBefore;
    QSet<QString> m_txnAssets;
    // SHA-256 (hex) of the bytes this store last wrote or read for each text
    // path. "Known absent" is an empty string. A mismatch before a write means
    // another program changed the project and overwriting it would lose data.
    QHash<QString, QString> m_digests;
    bool m_gitEnabled = false;
    int m_undoLimit = 200;
    qint64 m_undoByteLimit = 512LL * 1024 * 1024;
#ifdef PVFORGE_TEST_HOOKS
    int m_writeFailureForTest = -1;
    int m_crashAfterApplyForTest = -1;
#endif
};

} // namespace pvf::store
