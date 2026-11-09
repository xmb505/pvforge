#pragma once

#include <QString>

namespace pvf::assets {

// Lowercase hex SHA-256 of a single file. Returns an empty string on failure
// and, when given, writes a human readable reason to `error`.
QString sha256File(const QString& path, QString* error = nullptr);

// Deterministic SHA-256 of a directory tree. Files are visited recursively,
// sorted by their forward-slash relative path; each file contributes
// `<relative path> NUL <size> NUL <bytes>` to the running hash. Mtimes,
// permissions, empty directories and traversal order do not affect the
// result. An empty directory hashes like an empty byte string.
QString sha256Directory(const QString& dir, QString* error = nullptr);

} // namespace pvf::assets
