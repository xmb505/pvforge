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
// Missing files read as an empty byte array: undo/redo uses empty to mean
// "this path did not exist".
QByteArray readFileBytes(const QString& path);
bool loadProjectData(const QString& root, model::Project* project,
                     QString* error);

QString normalizedMessage(const QString& message);

} // namespace pvf::store::detail
