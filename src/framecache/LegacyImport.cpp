#include "framecache/LegacyImport.h"

#include "framecache/Schema.h"
#include "framecache/Store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVector>

namespace pvf::framecache {
namespace {

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
    return query.exec() && query.next();
}

bool alreadyImported(const QSqlDatabase& database, const QString& source)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM legacy_imports WHERE source = ?"));
    query.addBindValue(source);
    return query.exec() && query.next();
}

bool markImported(const QSqlDatabase& database, const QString& source,
                  QString* error)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO legacy_imports(source) "
                                 "VALUES(?)"));
    query.addBindValue(source);
    if (!query.exec())
    {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

bool insertRecord(const QSqlDatabase& database, const Record& record,
                  QString* error)
{
    QSqlQuery insert(database);
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
    insert.addBindValue(record.tag.isNull() ? QByteArray("") : record.tag);
    if (!insert.exec())
    {
        *error = insert.lastError().text();
        return false;
    }
    return true;
}

// Returns false only on a fatal SQL error; a table with an unknown shape is
// skipped.  `found` reports whether a usable legacy table was present, so a
// source that does not exist yet is not marked as imported.
bool importTable(const QSqlDatabase& database, const QString& table,
                 LegacyImportResult& result, bool* found)
{
    *found = false;
    if (!validTableName(table) || !tableExists(database, table))
        return true;

    const QStringList columns = tableColumns(database, table);
    const bool workbench = columns.contains(QStringLiteral("frame"))
                           && columns.contains(QStringLiteral("meta"))
                           && columns.contains(QStringLiteral("sha1"));
    const bool html = columns.contains(QStringLiteral("key"))
                      && columns.contains(QStringLiteral("asset_hash"));
    if (!workbench && !html)
        return true;
    *found = true;

    QString select;
    const bool hasCodec = columns.contains(QStringLiteral("codec"));
    if (workbench)
    {
        select = QStringLiteral("SELECT frame, meta, sha1, bytes, last_access%1 "
                                "FROM %2")
                     .arg(hasCodec ? QStringLiteral(", codec") : QString(),
                          table);
    }
    else
    {
        select = QStringLiteral(
                     "SELECT key, size, last_access, asset_hash FROM %1")
                     .arg(table);
    }

    QSqlQuery query(database);
    if (!query.exec(select))
    {
        result.error = query.lastError().text();
        return false;
    }

    QVector<Record> imported;
    while (query.next())
    {
        Record record;
        if (workbench)
        {
            record.key = QString::number(query.value(0).toInt());
            record.codec =
                hasCodec ? normalizeCodec(query.value(5).toString())
                         : QStringLiteral("webp");
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
        if (!Store::safeKey(record.key))
            continue;
        record.file = Store::fileFor(record.key, record.codec);
        imported.append(record);
    }
    query.finish();

    for (const Record& record : imported)
    {
        if (!insertRecord(database, record, &result.error))
            return false;
    }
    result.records += imported.size();
    return true;
}

bool importJson(const QString& directory, const QString& jsonIndex,
                const QSqlDatabase& database, LegacyImportResult& result,
                bool* found)
{
    *found = false;
    if (jsonIndex.isEmpty())
        return true;

    QFile file(QDir(directory).filePath(jsonIndex));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return true;
    *found = true;

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return true; // A corrupt sidecar is ignored; the sweep reclaims it.

    const QJsonArray array =
        document.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        Record record;
        record.key = object.value(QStringLiteral("key")).toString();
        if (!Store::safeKey(record.key))
            continue;
        record.codec = QStringLiteral("webp");
        record.file = Store::fileFor(record.key, record.codec);
        record.bytes = static_cast<qint64>(
            object.value(QStringLiteral("size")).toDouble());
        record.lastAccess = static_cast<qint64>(
            object.value(QStringLiteral("lastAccess")).toDouble());
        record.payload =
            object.value(QStringLiteral("assetHash")).toString().toUtf8();
        record.tag = record.payload;
        if (!insertRecord(database, record, &result.error))
            return false;
        ++result.records;
    }
    return true;
}

} // namespace

LegacyImportResult importLegacyIndex(const QString& directory,
                                     const QStringList& tables,
                                     const QString& jsonIndex)
{
    LegacyImportResult result;

    if (directory.isEmpty())
    {
        result.error = QStringLiteral("cache directory is empty");
        return result;
    }

    const QString connectionName =
        QStringLiteral("pvf_framecache_legacy_%1")
            .arg(reinterpret_cast<quintptr>(&result), 0, 16);

    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(
            QDir(directory).filePath(QStringLiteral("index.sqlite")));
        if (!db.open())
        {
            result.error = db.lastError().text();
        }
        else
        {
            {
                QSqlQuery pragma(db);
                pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
                pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
            }

            QString error;
            if (!ensureSchema(db, &error))
            {
                result.error = error;
            }
            else
            {
                QSqlQuery query(db);
                if (!query.exec(QStringLiteral(
                        "CREATE TABLE IF NOT EXISTS legacy_imports("
                        "source TEXT PRIMARY KEY)")))
                    result.error = query.lastError().text();
            }

            const bool started = result.ok() && db.transaction();
            if (result.ok() && !started)
                result.error = db.lastError().text();

            if (result.ok())
            {
                for (const QString& table : tables)
                {
                    const QString source = QStringLiteral("table:") + table;
                    if (alreadyImported(db, source))
                        continue;
                    bool found = false;
                    if (!importTable(db, table, result, &found)
                        || !result.ok())
                        break;
                    if (found && !markImported(db, source, &result.error))
                        break;
                }
            }

            if (result.ok() && !jsonIndex.isEmpty())
            {
                const QString source = QStringLiteral("json:") + jsonIndex;
                if (!alreadyImported(db, source))
                {
                    bool found = false;
                    if (importJson(directory, jsonIndex, db, result, &found)
                        && found)
                        markImported(db, source, &result.error);
                }
            }

            if (!result.ok())
                db.rollback();
            else if (!db.commit())
                result.error = db.lastError().text();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

} // namespace pvf::framecache
