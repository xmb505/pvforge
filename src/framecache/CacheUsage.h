#pragma once

#include <QString>
#include <QStringList>

namespace pvf::framecache {

// Measured occupancy of one cache directory tree.  Frame images and the index
// (SQLite/WAL/sidecars and anything else that is not a frame) are separate
// buckets so the UI never quotes a byte count whose frame count means
// something else.
struct CacheUsage
{
    qint64 frameBytes = 0;
    qint64 indexBytes = 0;
    int frameCount = 0;
    int fileCount = 0;

    qint64 totalBytes() const { return frameBytes + indexBytes; }
};

// What a clear actually removed.  `before` and `after` are two real scans of
// the tree; freed/removed numbers are their difference, never the size read
// before the attempt.  `ok` is false while any file is still present (and
// `failedPaths` lists them), so callers cannot report a success the disk
// contradicts.
struct ClearResult
{
    bool ok = true;
    qint64 freedBytes = 0;
    int removedFrames = 0;
    int removedFiles = 0;
    CacheUsage before;
    CacheUsage after;
    QStringList failedPaths;
    QString error;
};

// Recursively scans `root`.  A file whose suffix is in `frameExtensions`
// (case-insensitive, without the dot) counts as a frame image; every other
// file counts as index/metadata bytes.  A missing root is simply empty.
CacheUsage scanDirectory(
    const QString& root,
    const QStringList& frameExtensions = {QStringLiteral("webp"),
                                          QStringLiteral("qoi")});

// Removes every file under `root`, then the empty directories.  The result is
// verified by re-scanning, so read-only directories and files locked by a
// running renderer are reported instead of silently counted as freed.
ClearResult clearDirectory(
    const QString& root,
    const QStringList& frameExtensions = {QStringLiteral("webp"),
                                          QStringLiteral("qoi")});

} // namespace pvf::framecache
