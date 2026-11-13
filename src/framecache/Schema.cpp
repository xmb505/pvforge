#include "framecache/Schema.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace pvf::framecache {

bool ensureSchema(QSqlDatabase& database, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS frame_entries ("
            "key TEXT PRIMARY KEY, "
            "file TEXT NOT NULL, "
            "bytes INTEGER NOT NULL, "
            "last_access INTEGER NOT NULL, "
            "codec TEXT NOT NULL DEFAULT 'webp', "
            "payload BLOB NOT NULL DEFAULT X'', "
            "tag BLOB NOT NULL DEFAULT X'')")))
        return fail(query.lastError().text());

    // Budget eviction walks (last_access, key) in one ordered pass, so the
    // index carries both columns and the deterministic tie-break.
    query.exec(QStringLiteral("DROP INDEX IF EXISTS frame_entries_access"));
    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS frame_entries_lru "
            "ON frame_entries(last_access, key)")))
        return fail(query.lastError().text());

    int version = 0;
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
        version = query.value(0).toInt();
    if (version < kSchemaVersion
        && !query.exec(QStringLiteral("PRAGMA user_version=%1")
                           .arg(kSchemaVersion)))
        return fail(query.lastError().text());
    return true;
}

} // namespace pvf::framecache
