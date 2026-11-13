#pragma once

#include <QString>

class QSqlDatabase;

namespace pvf::framecache {

// Current layout of the shared frame index.  Bumped when frame_entries changes
// shape; the legacy import is tracked separately (see LegacyImport.h) so a
// caller that opens a fresh store does not disable a later import pass.
constexpr int kSchemaVersion = 3;

// Creates frame_entries and its LRU index when missing and stamps
// user_version.  The caller owns an open connection.  This is the only place
// that knows the schema, so Store and LegacyImport cannot drift apart.
bool ensureSchema(QSqlDatabase& database, QString* error = nullptr);

} // namespace pvf::framecache
