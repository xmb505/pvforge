#pragma once

#include "core/Project.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pvf {

struct MediaImportResult
{
    QString sourcePath;
    QString importedPath;
    EntityType type = EntityType::Image;
    QImage thumbnail;
    double duration = 0.0;
    QString displayName;
    QString error;
};

// Copies a file into a content-addressed asset directory. Safe to call from
// a worker thread; returns the source path unchanged when directory is empty.
QString copyIntoAssetDirectory(const QString& sourcePath, const QString& directory);

// Hash, copy (when needed), detect type and build a thumbnail. Pure function
// with no Project access, so it can run on a worker thread.
MediaImportResult processMediaFile(const QString& path, const QString& assetDirectory);

QVector<MediaImportResult> processMediaFiles(const QStringList& paths,
                                             const QString& assetDirectory);

} // namespace pvf
