#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace pvf::framecache {

// One cached frame on disk.
//
// `key` is the owner-defined identity (the workbench uses decimal frame
// numbers, htmlcache uses its canonical HTML key).  `file` is always a plain
// file name inside the store directory; it is derived from (key, codec) so a
// directory can never be escaped by a malformed key.  `payload` and `tag` are
// opaque owner data (FrameMeta / sha1, asset hash) and are never interpreted
// here.
struct Record
{
    QString key;
    QString file;
    qint64 bytes = 0;
    qint64 lastAccess = 0;
    QString codec = QStringLiteral("webp");
    QByteArray payload;
    QByteArray tag;
};

struct Stats
{
    qint64 diskBytes = 0;
    qint64 budgetBytes = 0;
    int entries = 0;
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 stores = 0;
    quint64 evictions = 0;

    // Rows visited by budget sweeps.  A sweep is one ordered index pass, so
    // this grows with the number of victims, never with entries * victims.
    quint64 evictionRowsVisited = 0;
};

struct Options
{
    QString directory;
    qint64 budgetBytes = 0;

    // Files considered part of this store, for orphan cleanup.  The default
    // matches the htmlcache layout; the workbench adds "qoi".
    QStringList extensions{QStringLiteral("webp")};

    // Optional per-file validation on load.  Returning false drops the row and
    // removes the file.  The workbench only checks existence/size; htmlcache
    // additionally proves the WebP decodes.
    std::function<bool(const QString& path, const Record&)> validate;
};

// SQLite-backed frame index plus directory LRU.
//
// Layout: <directory>/index.sqlite and one file per record named
// <key>.<codec>.  The table is shared by every frame cache in the process:
//
//   frame_entries(key TEXT PRIMARY KEY, file TEXT, bytes INTEGER,
//                 last_access INTEGER, codec TEXT, payload BLOB, tag BLOB)
//
// Writes go through QSaveFile so a torn store never leaves a half-written
// frame.  Loading drops rows whose file disappeared, repairs stale sizes and
// file names, deletes unindexed files with a known extension, and enforces the
// byte budget with a single ordered oldest-first LRU pass.
//
// Store deliberately knows nothing about historical layouts: callers that may
// find a pre-framecache directory run importLegacyIndex() (LegacyImport.h)
// before constructing the Store.
//
// Not thread-safe: QSqlDatabase connections belong to the thread that created
// the Store.  The destructor closes and removes the connection.
class Store
{
public:
    explicit Store(Options options);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool isValid() const;
    QString error() const;
    QString directory() const;

    std::optional<Record> record(const QString& key) const;
    const QHash<QString, Record>& records() const;

    QString pathFor(const Record& record) const;
    static QString fileFor(const QString& key, const QString& codec);
    static bool safeKey(const QString& key);

    // Atomically writes `data` and inserts/replaces the row.  The record's
    // file, bytes and lastAccess are assigned here; codec/payload/tag are
    // taken from the argument.
    bool put(Record record, const QByteArray& data, QString* error = nullptr);

    // Updates an existing row in place (no file write).  Used when a reader
    // observes a different on-disk size.
    bool update(const Record& record, QString* error = nullptr);

    // Refreshes the LRU clock of an existing row.
    bool touch(const QString& key, QString* error = nullptr);

    // Removes a row and, when requested, its file.
    bool remove(const QString& key, bool deleteFile, QString* error = nullptr);

    // Removes every row and every file with a known extension.
    void clear();

    qint64 nextAccess();
    void setBudgetBytes(qint64 bytes);
    qint64 budgetBytes() const;

    Stats stats() const;
    void noteHit();
    void noteMiss();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pvf::framecache
