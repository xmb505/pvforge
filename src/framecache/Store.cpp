#include "framecache/Store.h"

#include "framecache/Schema.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <utility>

namespace pvf::framecache {
namespace {

QString normalizeCodec(const QString& codec)
{
    return codec == QLatin1String("qoi") ? QStringLiteral("qoi")
                                         : QStringLiteral("webp");
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

        if (!ensureSchema(db, &error))
        {
            fail(error);
            return false;
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

    // One ordered index pass, oldest first, with the key as a deterministic
    // tie-break.  Victims are collected until enough bytes are reclaimed and
    // then removed together, instead of rescanning every record per eviction.
    void enforceBudget()
    {
        if (diskBytes <= budget || records.isEmpty())
            return;

        QSqlDatabase db = database();
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral(
                "SELECT key FROM frame_entries "
                "ORDER BY last_access ASC, key ASC")))
        {
            fail(query.lastError().text());
            return;
        }

        const qint64 needed = diskBytes - budget;
        QStringList victims;
        qint64 reclaimed = 0;
        while (reclaimed < needed && query.next())
        {
            ++counters.evictionRowsVisited;
            const QString key = query.value(0).toString();
            const auto it = records.constFind(key);
            if (it == records.constEnd())
                continue;
            victims.append(key);
            reclaimed += it->bytes;
        }
        query.finish();

        QSqlQuery deleteQuery(db);
        deleteQuery.prepare(
            QStringLiteral("DELETE FROM frame_entries WHERE key = ?"));
        for (const QString& key : victims)
        {
            const auto it = records.find(key);
            if (it == records.constEnd())
                continue;
            QFile::remove(pathFor(*it));
            diskBytes = std::max<qint64>(0, diskBytes - it->bytes);
            records.erase(it);
            deleteQuery.bindValue(0, key);
            if (!deleteQuery.exec())
                fail(deleteQuery.lastError().text());
            deleteQuery.finish();
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
