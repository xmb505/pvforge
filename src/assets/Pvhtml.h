#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace pvf::assets {

struct PvhtmlManifest
{
    QString entry = QStringLiteral("index.html");
    QSize logicalSize{800, 120};
    QVariantMap variables;
};

// Copies a .pvhtml bundle from a directory or a zip archive into `destDir`
// (which must not exist yet, or be empty). The archive may wrap the bundle in
// one top-level directory; that directory is unwrapped. `pvhtml.json` is
// optional and supplies entry/logicalSize/variables; without it the entry
// defaults to index.html. Symbolic links are rejected so a bundle cannot
// reach files outside its own tree.
//
// Validation: the manifest (if present) must be valid JSON, `entry` must be a
// safe relative path, and the entry file must exist inside the bundle. Zip
// extraction requires the `unzip` executable and rejects absolute paths and
// `..` entries. On success `manifest` (when given) receives the parsed values.
bool importPvhtml(const QString& sourceDirOrZip, const QString& destDir,
                  PvhtmlManifest* manifest, QString* error = nullptr);

} // namespace pvf::assets
