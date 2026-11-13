#pragma once

#include <QString>
#include <QStringList>

namespace pvf::framecache {

struct LegacyImportResult
{
    int records = 0;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// Imports historical frame indexes into <directory>/index.sqlite so frames
// written by older builds keep their metadata (and are not swept as orphans).
//
// Call this explicitly before constructing Store.  Two layouts are understood:
//
//   * SQL tables in `tables`; the shape is detected from the columns
//     (workbench: frame/meta/sha1/bytes/last_access[/codec],
//      htmlcache: key/path/size/last_access/asset_hash).
//   * an optional JSON sidecar `jsonIndex` (htmlcache's no-SQLite fallback).
//
// The legacy sources are left untouched as read-only backups.  Each source is
// recorded in legacy_imports, so repeated calls (and reopens) are no-ops, and
// rows already present in frame_entries always win (INSERT OR IGNORE).
//
// Missing directory/table/file is a successful no-op.  On failure the caller
// must not open Store: Store's orphan sweep would delete the unimported frames.
LegacyImportResult importLegacyIndex(const QString& directory,
                                     const QStringList& tables,
                                     const QString& jsonIndex = QString());

} // namespace pvf::framecache
