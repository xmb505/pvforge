#include "framecache/Store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>
#include <utility>

namespace pvf::framecache {
namespace {

constexpr int kSchemaVersion = 3;

QString normalizeCodec(const QString& codec)
{
    return codec == QLatin1String("qoi") ? QStringLiteral("qoi")
                                         : QStringLiteral("webp");
}

bool validTableName(const QString& table)
{
    if (table.isEmpty())
        return false;
    for (const QChar character : table)
    {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_'))
            return false;
    }
    return true;
}

QStringList tableColumns(const QSqlDatabase& database, const QString& table)
{
    QStringList columns;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return columns;
    while (query.next())
        columns.append(query.value(1).toString());
    return columns;
}

bool tableExists(const QSqlDatabase& database, const QString& table)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?"));
    query.addBindValue(table);
    if (!query.exec() || !query.next())
        return false;
    return true;
}

} // namespace

struct Store::Impl
{
    Options options;
    QString directory;
    QString connectionName;
    QString error;
    bool valid = false;

    QHash<QString, Record> records;
    qint64 diskBytes = 0;
    qint64 clock = 0;
    qint64 budget = 0;
    Stats counters;

    QSqlDatabase database() const
    {
        return QSqlDatabase::database(connectionName, false);
    }

    void closeIndex()
    {
        if (connectionName.isEmpty())
            return;
        {
            QSqlDatabase db = database();
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        connectionName.clear();
    }

    qint64 nextAccess()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        clock = std::max(now, clock + 1);
        return clock;
    }

    bool fail(const QString& message)
    {
        error = message;
        return false;
    }

    bool openIndex()
    {
        connectionName =
            QStringLiteral("pvf_framecache_%1")
                .arg(reinterpret_cast<quintptr>(this), 0, 16);

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    connectionName);
        if (!db.isValid())
            return fail(QStringLiteral("Qt SQLite driver is not available"));

        db.setDatabaseName(
            QDir(directory).filePath(QStringLiteral("index.sqlite")));
        if (!db.open())
        {
            fail(db.lastError().text());
            return false;
        }

        QSqlQuery query(db);
        query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        query.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS frame_entries ("
                "key TEXT PRIMARY KEY, "
                "file TEXT NOT NULL, "
                "bytes INTEGER NOT NULL, "
                "last_access INTEGER NOT NULL, "
                "codec TEXT NOT NULL DEFAULT 'webp', "
                "payload BLOB NOT NULL DEFAULT X'', "
                "tag BLOB NOT NULL DEFAULT X'')")))
        {
            fail(query.lastError().text());
            return false;
        }

        query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS frame_entries_access "
            "ON frame_entries(last_access)"));

        int version = 0;
        if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
            version = query.value(0).toInt();

        if (version < kSchemaVersion)
        {
            if (!migrateLegacy(db))
            {
                return false;
            }
            query.exec(
                QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
        }
        return true;
    }

    // Imports the two historical layouts into frame_entries.  The old tables
    // stay untouched as a read-only backup; user_version prevents re-import.
    bool migrateLegacy(const QSqlDatabase& db)
    {
        for (const QString& table : options.legacyTables)
        {
            if (!validTableName(table) || !tableExists(db, table))
                continue;

            const QStringList columns = tableColumns(db, table);
            const bool workbench = columns.contains(QStringLiteral("frame"))
                                   && columns.contains(QStringLiteral("meta"))
                                   && columns.contains(QStringLiteral("sha1"));
            const bool html = columns.contains(QStringLiteral("key"))
                              && columns.contains(QStringLiteral("asset_hash"));
            if (!workbench && !html)
                continue;

            QString select;
            if (workbench)
            {
                const bool hasCodec = columns.contains(QStringLiteral("codec"));
                select = QStringLiteral(
                             "SELECT frame, meta, sha1, bytes, last_access%1 "
                             "FROM %2")
                             .arg(hasCodec ? QStringLiteral(", codec")
                                           : QStringLiteral(""),
                                  table);
            }
            else
            {
                select = QStringLiteral(
                             "SELECT key, size, last_access, asset_hash "
                             "FROM %1")
                             .arg(table);
            }

            QSqlQuery query(db);
            if (!query.exec(select))
                return fail(query.lastError().text());

            QVector<Record> imported;
            while (query.next())
            {
                Record record;
                if (workbench)
                {
                    record.key = QString::number(query.value(0).toInt());
                    record.codec =
                        normalizeCodec(query.value(5).toString());
                    record.payload = query.value(1).toByteArray();
                    record.tag = query.value(2).toByteArray();
                    record.bytes = query.value(3).toLongLong();
                    record.lastAccess = query.value(4).toLongLong();
                }
                else
                {
                    record.key = query.value(0).toString();
                    record.codec = QStringLiteral("webp");
                    record.bytes = query.value(1).toLongLong();
                    record.lastAccess = query.value(2).toLongLong();
                    record.payload = query.value(3).toString().toUtf8();
                    record.tag = record.payload;
                }
                if (record.key.isEmpty() || !safeKey(record.key))
                    continue;
                record.file = fileFor(record.key, record.codec);
                imported.append(record);
            }

            for (const Record& record : imported)
            {
                QSqlQuery insert(db);
                insert.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO frame_entries"
                    "(key, file, bytes, last_access, codec, payload, tag) "
                    "VALUES(?, ?, ?, ?, ?, ?, ?)"));
                insert.addBindValue(record.key);
                insert.addBindValue(record.file);
                insert.addBindValue(record.bytes);
                insert.addBindValue(record.lastAccess);
                insert.addBindValue(record.codec);
                insert.addBindValue(record.payload.isNull() ? QByteArray("")
                                                            : record.payload);
                insert.addBindValue(record.tag.isNull() ? QByteArray("")
                                                        : record.tag);
                if (!insert.exec())
                    return fail(insert.lastError().text());
            }
        }
        return importLegacyJson(db);
    }

    // htmlcache used a JSON sidecar when Qt SQL was unavailable.  Import it on
    // first open so upgrading does not discard frames; a corrupt sidecar is
    // ignored and the ordinary orphan sweep reclaims its files.
    bool importLegacyJson(const QSqlDatabase& db)
    {
        if (options.legacyJsonIndex.isEmpty())
            return true;

        QFile file(QDir(directory).filePath(options.legacyJsonIndex));
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            return true;

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError
            || !document.isObject())
            return true;

        const QJsonArray array =
            document.object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue& value : array)
        {
            if (!value.isObject())
                continue;
            const QJsonObject object = value.toObject();
            const QString key = object.value(QStringLiteral("key")).toString();
            if (!safeKey(key))
                continue;

            Record record;
            record.key = key;
            record.codec = QStringLiteral("webp");
            record.file = fileFor(key, record.codec);
            record.bytes = static_cast<qint64>(
                object.value(QStringLiteral("size")).toDouble());
            record.lastAccess = static_cast<qint64>(
                object.value(QStringLiteral("lastAccess")).toDouble());
            record.payload = object.value(QStringLiteral("assetHash"))
                                 .toString()
                                 .toUtf8();
            record.tag = record.payload;

            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO frame_entries"
                "(key, file, bytes, last_access, codec, payload, tag) "
                "VALUES(?, ?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(record.key);
            insert.addBindValue(record.file);
            insert.addBindValue(record.bytes);
            insert.addBindValue(record.lastAccess);
            insert.addBindValue(record.codec);
            insert.addBindValue(record.payload.isNull() ? QByteArray("")
                                                        : record.payload);
            insert.addBindValue(record.tag.isNull() ? QByteArray("")
                                                    : record.tag);
            if (!insert.exec())
                return fail(insert.lastError().text());
        }
        return true;
    }

    bool load()
    {
        QSqlDatabase db = database();
        QVector<QString> staleKeys;
        QVector<QString> corruptPaths;
        QVector<Record> repaired;
        QSet<QString> indexedFiles;

        {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral(
                    "SELECT key, file, bytes, last_access, codec, payload, tag "
                    "FROM frame_entries")))
                return fail(query.lastError().text());

            while (query.next())
            {
                Record record;
                record.key = query.value(0).toString();
                record.file = query.value(1).toString();
                record.bytes = query.value(2).toLongLong();
                record.lastAccess = query.value(3).toLongLong();
                record.codec = normalizeCodec(query.value(4).toString());
                record.payload = query.value(5).toByteArray();
                record.tag = query.value(6).toByteArray();

                if (!safeKey(record.key))
                {
                    // Never derive a path from a key we would not write.
                    staleKeys.append(record.key);
                    continue;
                }

                const QString expected = fileFor(record.key, record.codec);
                const QString path = QDir(directory).filePath(expected);
                const QFileInfo info(path);
                if (!info.exists() || !info.isFile() || info.size() <= 0)
                {
                    staleKeys.append(record.key);
                    continue;
                }

                const bool changed =
                    record.file != expected || record.bytes != info.size()
                    || record.lastAccess <= 0;
                record.file = expected;
                record.bytes = info.size();
                if (record.lastAccess <= 0)
                    record.lastAccess = nextAccess();

                if (options.validate
                    && !options.validate(path, record))
                {
                    staleKeys.append(record.key);
                    corruptPaths.append(path);
                    continue;
                }

                clock = std::max(clock, record.lastAccess);
                diskBytes += record.bytes;
                indexedFiles.insert(expected);
                records.insert(record.key, record);
                if (changed)
                    repaired.append(record);
            }
        }

        for (const Record& record : repaired)
            upsertRow(record);
        for (const QString& key : staleKeys)
            deleteRow(key);
        for (const QString& path : corruptPaths)
            QFile::remove(path);

        // A file with a cache extension but no row is a leftover from an
        // interrupted store or an unparseable index.  It would escape both the
        // byte accounting and the LRU budget, so remove it.
        QStringList filters;
        for (const QString& extension : options.extensions)
            filters.append(QStringLiteral("*.") + extension);
        const QFileInfoList files =
            QDir(directory).entryInfoList(filters, QDir::Files);
        for (const QFileInfo& info : files)
        {
            if (!indexedFiles.contains(info.fileName()))
                QFile::remove(info.absoluteFilePath());
        }

        enforceBudget();
        return true;
    }

    bool upsertRow(const Record& record)
    {
        QSqlDatabase db = database();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO frame_entries"
            "(key, file, bytes, last_access, codec, payload, tag) "
            "VALUES(?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(key) DO UPDATE SET "
            "file = excluded.file, bytes = excluded.bytes, "
            "last_access = excluded.last_access, codec = excluded.codec, "
            "payload = excluded.payload, tag = excluded.tag"));
        query.addBindValue(record.key);
        query.addBindValue(record.file);
        query.addBindValue(record.bytes);
        query.addBindValue(record.lastAccess);
        query.addBindValue(record.codec);
        query.addBindValue(record.payload.isNull() ? QByteArray("")
                                                   : record.payload);
        query.addBindValue(record.tag.isNull() ? QByteArray("") : record.tag);
        if (!query.exec())
            return fail(query.lastError().text());
        return true;
    }

    bool deleteRow(const QString& key)
    {
        QSqlDatabase db = database();
        QSqlQuery query(db);
        query.prepare(
            QStringLiteral("DELETE FROM frame_entries WHERE key = ?"));
        query.addBindValue(key);
        if (!query.exec())
            return fail(query.lastError().text());
        return true;
    }

    void forget(const QString& key, bool deleteFile)
    {
        auto it = records.find(key);
        if (it == records.end())
            return;
        if (deleteFile)
            QFile::remove(pathFor(*it));
        diskBytes = std::max<qint64>(0, diskBytes - it->bytes);
        records.erase(it);
        deleteRow(key);
    }

    void enforceBudget()
    {
        while (diskBytes > budget && !records.isEmpty())
        {
            QString victim;
            qint64 oldest = std::numeric_limits<qint64>::max();
            for (auto it = records.cbegin(); it != records.cend(); ++it)
            {
                if (it->lastAccess < oldest
                    || (it->lastAccess == oldest
                        && (victim.isEmpty() || it.key() < victim)))
                {
                    oldest = it->lastAccess;
                    victim = it.key();
                }
            }
            if (victim.isEmpty())
                break;
            forget(victim, true);
            ++counters.evictions;
        }
    }

    QString pathFor(const Record& record) const
    {
        return QDir(directory).filePath(
            record.file.isEmpty() ? fileFor(record.key, record.codec)
                                  : record.file);
    }
};

Store::Store(Options options)
    : d(std::make_unique<Impl>())
{
    d->options = std::move(options);
    d->budget = std::max<qint64>(0, d->options.budgetBytes);

    if (d->options.directory.isEmpty())
    {
        d->fail(QStringLiteral("cache directory is empty"));
        return;
    }

    d->directory = QDir(d->options.directory).absolutePath();
    if (!QDir().mkpath(d->directory))
    {
        d->fail(QStringLiteral("cannot create cache directory: %1")
                    .arg(d->directory));
        return;
    }

    if (d->options.extensions.isEmpty())
        d->options.extensions.append(QStringLiteral("webp"));

    if (!d->openIndex())
    {
        d->closeIndex();
        return;
    }
    if (!d->load())
    {
        d->closeIndex();
        return;
    }
    d->valid = true;
}

Store::~Store()
{
    if (d->connectionName.isEmpty())
        return;
    {
        QSqlDatabase db = d->database();
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(d->connectionName);
}

bool Store::isValid() const
{
    return d->valid;
}

QString Store::error() const
{
    return d->error;
}

QString Store::directory() const
{
    return d->directory;
}

std::optional<Record> Store::record(const QString& key) const
{
    const auto it = d->records.constFind(key);
    if (it == d->records.constEnd())
        return std::nullopt;
    return *it;
}

const QHash<QString, Record>& Store::records() const
{
    return d->records;
}

QString Store::pathFor(const Record& record) const
{
    return d->pathFor(record);
}

QString Store::fileFor(const QString& key, const QString& codec)
{
    return key + QLatin1Char('.') + normalizeCodec(codec);
}

bool Store::safeKey(const QString& key)
{
    if (key.isEmpty() || key == QLatin1String(".")
        || key == QLatin1String(".."))
        return false;
    return !key.contains(QLatin1Char('/'))
           && !key.contains(QLatin1Char('\\'))
           && !key.contains(QChar(0));
}

bool Store::put(Record record, const QByteArray& data, QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);
    if (data.isEmpty())
        return fail(QStringLiteral("cannot store empty bytes"));
    if (!safeKey(record.key))
        return fail(QStringLiteral("invalid cache key"));

    record.codec = normalizeCodec(record.codec);
    record.file = fileFor(record.key, record.codec);
    record.bytes = data.size();
    record.lastAccess = d->nextAccess();

    const QString path = d->pathFor(record);
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return fail(file.errorString());
        if (file.write(data) != data.size() || !file.commit())
            return fail(file.errorString());
    }

    const auto old = d->records.constFind(record.key);
    const bool hadOld = old != d->records.constEnd();
    const qint64 oldBytes = hadOld ? old->bytes : 0;
    const QString oldFile = hadOld ? old->file : QString();

    if (!d->upsertRow(record))
    {
        // The index rejected the row.  Drop any in-memory claim so the file
        // cannot escape accounting; the file itself is left for the orphan
        // sweep on the next open (it may be the pre-existing good frame).
        d->deleteRow(record.key);
        if (hadOld)
            d->records.remove(record.key);
        d->diskBytes = std::max<qint64>(0, d->diskBytes - oldBytes);
        if (!oldFile.isEmpty() && oldFile != record.file)
            QFile::remove(QDir(d->directory).filePath(oldFile));
        return fail(d->error);
    }

    if (!oldFile.isEmpty() && oldFile != record.file)
        QFile::remove(QDir(d->directory).filePath(oldFile));

    d->records.insert(record.key, record);
    d->diskBytes = std::max<qint64>(0, d->diskBytes - oldBytes);
    d->diskBytes += record.bytes;
    ++d->counters.stores;
    d->enforceBudget();
    return true;
}

bool Store::update(const Record& record, QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);

    auto it = d->records.find(record.key);
    if (it == d->records.end())
        return fail(QStringLiteral("unknown cache key"));

    Record updated = record;
    updated.codec = normalizeCodec(updated.codec);
    updated.file = fileFor(updated.key, updated.codec);

    if (!d->upsertRow(updated))
        return fail(d->error);

    d->diskBytes =
        std::max<qint64>(0, d->diskBytes - it->bytes) + updated.bytes;
    *it = updated;
    return true;
}

bool Store::touch(const QString& key, QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);

    auto it = d->records.find(key);
    if (it == d->records.end())
        return fail(QStringLiteral("unknown cache key"));

    it->lastAccess = d->nextAccess();
    if (!d->upsertRow(*it))
        return fail(d->error);
    return true;
}

bool Store::remove(const QString& key, bool deleteFile, QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);
    if (!d->records.contains(key))
        return true;

    d->forget(key, deleteFile);
    return true;
}

void Store::clear()
{
    if (!d->valid)
        return;

    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    query.exec(QStringLiteral("DELETE FROM frame_entries"));

    for (auto it = d->records.cbegin(); it != d->records.cend(); ++it)
        QFile::remove(d->pathFor(*it));

    QStringList filters;
    for (const QString& extension : d->options.extensions)
        filters.append(QStringLiteral("*.") + extension);
    const QFileInfoList files =
        QDir(d->directory).entryInfoList(filters, QDir::Files);
    for (const QFileInfo& info : files)
        QFile::remove(info.absoluteFilePath());

    d->records.clear();
    d->diskBytes = 0;
}

qint64 Store::nextAccess()
{
    return d->nextAccess();
}

void Store::setBudgetBytes(qint64 bytes)
{
    d->budget = std::max<qint64>(0, bytes);
    d->counters.budgetBytes = d->budget;
    d->enforceBudget();
}

qint64 Store::budgetBytes() const
{
    return d->budget;
}

Stats Store::stats() const
{
    Stats result = d->counters;
    result.diskBytes = d->diskBytes;
    result.budgetBytes = d->budget;
    result.entries = d->records.size();
    return result;
}

void Store::noteHit()
{
    ++d->counters.hits;
}

void Store::noteMiss()
{
    ++d->counters.misses;
}

} // namespace pvf::framecache
