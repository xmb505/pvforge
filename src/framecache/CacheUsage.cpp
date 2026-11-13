#include "framecache/CacheUsage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace pvf::framecache {

CacheUsage scanDirectory(const QString& root,
                         const QStringList& frameExtensions)
{
    CacheUsage usage;
    if (root.isEmpty() || !QFileInfo::exists(root))
        return usage;

    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        const QFileInfo info = it.fileInfo();
        ++usage.fileCount;
        if (frameExtensions.contains(info.suffix(), Qt::CaseInsensitive))
        {
            usage.frameBytes += info.size();
            ++usage.frameCount;
        }
        else
        {
            usage.indexBytes += info.size();
        }
    }
    return usage;
}

ClearResult clearDirectory(const QString& root,
                           const QStringList& frameExtensions)
{
    ClearResult result;
    result.before = scanDirectory(root, frameExtensions);
    if (root.isEmpty() || !QFileInfo::exists(root))
    {
        result.after = result.before;
        return result;
    }

    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        files.append(it.filePath());
    }
    for (const QString& path : files)
        QFile::remove(path);

    // Files are gone; this only removes the empty directory skeleton (and any
    // file that appeared between the two passes).  Its return value is not
    // trusted: the scan below is the source of truth.
    QDir(root).removeRecursively();

    result.after = scanDirectory(root, frameExtensions);
    QDirIterator remaining(root, QDir::Files | QDir::Hidden | QDir::System,
                           QDirIterator::Subdirectories);
    while (remaining.hasNext())
    {
        remaining.next();
        result.failedPaths.append(remaining.filePath());
    }

    result.ok = result.failedPaths.isEmpty();
    if (!result.ok)
    {
        result.error = QStringLiteral("%1 cache files could not be removed")
                           .arg(result.failedPaths.size());
    }
    result.freedBytes = std::max<qint64>(
        0, result.before.totalBytes() - result.after.totalBytes());
    result.removedFrames =
        std::max(0, result.before.frameCount - result.after.frameCount);
    result.removedFiles =
        std::max(0, result.before.fileCount - result.after.fileCount);
    return result;
}

} // namespace pvf::framecache
