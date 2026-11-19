#pragma once

// Shared private helpers for store/ProjectStore*.cpp.  This header is not part
// of the public store API: only the ProjectStore implementation files include
// it, so the helpers stay out of Store.h and out of every consumer's compile
// unit.

#include "common/Types.h"
#include "model/Model.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

class QLockFile;

namespace pvf::store::detail {

struct ProcessResult
{
    bool started = false;
    int exitCode = -1;
    QString out;
    QString err;
};

void setError(QString* error, const QString& message);

// ---- process / git ------------------------------------------------------
ProcessResult runProcess(const QString& program, const QStringList& arguments,
                         const QString& workDir = QString());
ProcessResult runGit(const QString& root, const QStringList& arguments);
QString processFailure(const ProcessResult& result);
bool gitVersion(QString* version);
bool checkGit(QString* error);
bool ensureGitIdentity(const QString& root, QString* error);
bool hasHead(const QString& root);
QString headHash(const QString& root);
bool isTracked(const QString& root, const QString& relativePath);
QStringList splitNul(const QString& text);
bool commitOnlyPaths(const QString& root, const QString& message,
                     const QStringList& paths, QString* hash, QString* error);

// ---- text tree ----------------------------------------------------------
bool isProjectTextPath(const QString& relativePath);
bool resolveCommit(const QString& root, const QString& hash, QString* fullHash,
                   QString* error);
bool listTreeFiles(const QString& root, const QString& commit,
                   const QStringList& paths, QStringList* files, QString* error);
bool listTrackedFiles(const QString& root, const QString& path,
                      QStringList* files, QString* error);
QStringList textPathsForState(const QString& root);
bool stageTextPaths(const QString& root, QString* error);
bool syncTextTreeToCommit(const QString& root, const QString& commit,
                          QString* error);
bool restoreTextTreeToCommit(const QString& root, const QString& commit,
                             QString* error);

// ---- dirty text scan ----------------------------------------------------
QString porcelainStatusName(QChar index, QChar worktree);

struct RawDirtyText
{
    QString path;
    QString sourcePath; // rename/copy source; empty otherwise
    QChar index;
    QChar worktree;
};

QVector<RawDirtyText> scanRawDirtyText(const QString& root, QString* error);

// ---- files --------------------------------------------------------------
QString entityRelativePath(const Uuid& uuid);
QString absolutePath(const QString& root, const QString& relativePath);
bool readJsonObject(const QString& path, QJsonObject* object, QString* error);
bool writeJsonObject(const QString& path, const QJsonObject& object,
                     QString* error);
bool writeBytesFile(const QString& path, const QByteArray& bytes, QString* error);
bool writeTextFile(const QString& path, const QString& content, QString* error);
// Reads a file and distinguishes "missing" from "read error". A missing file
// yields an empty byte array with *existed=false; any other failure returns
// false and sets *error. Undo/redo must never treat a read error as "the file
// did not exist".
bool readFileBytes(const QString& path, QByteArray* bytes, bool* existed,
                   QString* error);
// SHA-256 of `bytes` in lowercase hex.
QString fileDigest(const QByteArray& bytes);

// Asset locations: live assets/<hash> vs `.trash/<hash>`.
QString assetLivePath(const QString& root, const AssetHash& hash);
QString assetTrashPath(const QString& root, const AssetHash& hash);

// ---- transactional writes ----------------------------------------------
// One member of a file transaction. `remove` deletes the target after every
// staged write has landed; `bytes` is otherwise written.
struct TransactionOp
{
    QString relativePath;
    QByteArray bytes;
    bool remove = false;
};

// Writes all ops as one recoverable transaction: stage every file under
// `.pvforge/txn-*`, write and fsync the journal, then rename the staged files
// into place (new files first, removals last). A failure while staging touches
// no target file. A failure while applying leaves the journal behind so the
// next open() can finish the same set of operations.
//
// `failOnWriteIndex` and `crashAfterApplyIndex` are test hooks: they fail the
// nth staged write and leave a journal after applying n operations.
bool applyFileTransaction(const QString& root, const QVector<TransactionOp>& ops,
                          QString* error, int failOnWriteIndex = -1,
                          int crashAfterApplyIndex = -1);

// Applies every journal left by an interrupted transaction and removes it.
bool recoverPendingTransactions(const QString& root, QString* error);

// True when a transaction directory is still present after a failed apply.
// The store switches to read-only then: the next open() finishes the journal,
// but continuing to write now could build on a half-applied tree.
bool hasPendingTransaction(const QString& root);

struct QuarantinedPath
{
    QString path;
    QString reason;
};

struct LoadReport
{
    QVector<QuarantinedPath> quarantined;
};

// Strict by default. When `report` is non-null, a corrupt/duplicate/unsupported
// entity file is moved to `.pvforge/quarantine/`, recorded in the report and
// skipped instead of failing the whole load. project.json stays fatal: without
// it there is no project to open read-only.
bool loadProjectData(const QString& root, model::Project* project,
                     QString* error, LoadReport* report = nullptr);

QString normalizedMessage(const QString& message);

// ---- project lock -------------------------------------------------------
// Process-wide owner of the project directory QLockFile. Reference counted so
// tests (and a future multi-view shell) can hold several stores for the same
// root in one process, while a second process still gets a clear refusal.
struct ProjectLock
{
    QString root;
    std::unique_ptr<QLockFile> file;
    ~ProjectLock();
};

std::shared_ptr<ProjectLock> acquireProjectLock(const QString& root,
                                                QString* error);

} // namespace pvf::store::detail
