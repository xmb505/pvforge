#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pvf {

// One media/asset reference recorded in a checkpoint sidecar.
struct CheckpointAssetRef
{
    QString path;      // path as stored in the project JSON (may be relative)
    bool exists = false;
};

// Metadata for one hidden checkpoint. The project JSON itself is stored
// separately (as a git blob or a .pvproj snapshot file) and is only loaded by
// restoreCheckpoint().
struct CheckpointInfo
{
    QString id;              // git commit hash, or snapshot id
    QDateTime created;
    QString summary;         // AI session / operation summary
    QString projectUuid;
    QString fileSha256;      // SHA-256 of the stored project JSON
    qint64 fileSize = 0;
    QStringList scenes;      // scene names present in the snapshot
    QVector<CheckpointAssetRef> assets;
    QStringList missingAssets;
    bool gitBacked = false;
    bool valid = true;       // false when the sidecar exists but its payload is gone
};

// Hidden, per-project checkpoint store. Checkpoints live under
// <cache>/PVForge/checkpoints/<sha256(project path)>.git so the user's
// repository and project folder are never touched.
//
// The store uses the system git executable and plumbing commands only:
// hash-object -> mktree -> commit-tree -> update-ref. No index, worktree,
// add/commit/checkout, user config, or hooks are involved. If git is missing
// or a git write fails, the same API falls back to atomically written
// .snapshots/ files.
//
// All writes are verified before createCheckpoint() returns true so callers
// can gate AI mutations on a durable checkpoint. Restore only returns the
// stored JSON; it never writes the project file.
class CheckpointManager
{
public:
    // cacheRoot: empty means the default ~/.cache/PVForge/checkpoints.
    // gitExecutable: empty means $PVFORGE_GIT, or "git" from PATH.
    explicit CheckpointManager(const QString& cacheRoot = QString(),
                               const QString& gitExecutable = QString());
    ~CheckpointManager();

    static QString defaultCacheRoot();
    // Lowercase SHA-256 of the canonical project path. Used only as an opaque
    // directory name; the path itself is never stored in a file name.
    static QString projectPathHash(const QString& projectPath);

    QString cacheRoot() const { return m_root; }

    // True when the checkpoint store (git or snapshot) can be written.
    bool isAvailable() const;
    // False when git is missing/too broken; the snapshot backend is then used.
    bool gitAvailable() const;

    // Returns only after the payload and sidecar were written, synced and read
    // back. On success checkpointId receives the git commit hash or snapshot
    // id; a null pointer is fine. False means no durable checkpoint exists and
    // the caller must not gate a write on it.
    bool createCheckpoint(const QString& projectPath, const QByteArray& projectJson,
                          const QString& summary, QString* error = nullptr,
                          QString* checkpointId = nullptr);
    // Newest first.
    QVector<CheckpointInfo> listCheckpoints(const QString& projectPath);
    // Verifies the sidecar SHA-256 and project UUID before returning data.
    bool restoreCheckpoint(const QString& projectPath, const QString& checkpointId,
                           QByteArray* projectJson, QString* error = nullptr);
    // Keeps the newest `keep` checkpoints and enforces the byte budget.
    void prune(const QString& projectPath, int keep = kDefaultKeep);

    static constexpr int kDefaultKeep = 100;
    static constexpr qint64 kDiskBudgetBytes = 2LL * 1024 * 1024 * 1024;

private:
    QString repoPath(const QString& projectPath) const;
    QString snapshotDir(const QString& projectPath) const;
    QString identityPath(const QString& projectPath) const;
    QString projectUuid(const QString& projectPath, const QByteArray& projectJson,
                        bool create);
    bool createGitCheckpoint(const QString& projectPath, const QByteArray& projectJson,
                             const QString& summary, const QString& uuid,
                             QString* checkpointId, QString* error);
    bool createSnapshotCheckpoint(const QString& projectPath,
                                  const QByteArray& projectJson,
                                  const QString& summary, const QString& uuid,
                                  QString* checkpointId, QString* error);
    QVector<CheckpointInfo> listGitCheckpoints(const QString& projectPath);
    QVector<CheckpointInfo> listSnapshotCheckpoints(const QString& projectPath);
    bool restoreGitCheckpoint(const QString& projectPath, const QString& id,
                              QByteArray* projectJson, QString* error);
    bool restoreSnapshotCheckpoint(const QString& projectPath, const QString& id,
                                   QByteArray* projectJson, QString* error);
    bool selfTestGit() const;

    QString m_root;
    QString m_gitExecutable;
    mutable int m_gitState = 0;   // 0 unknown, 1 available, -1 unavailable
};

} // namespace pvf
