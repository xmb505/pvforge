#pragma once

#include "common/Types.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace pvf::assets {

struct StoredAsset
{
    AssetHash hash;
    AssetType type = AssetType::Image;
    QString name;
    QString directory;   // absolute path of assets/<sha256>
    QJsonObject metadata;
};

// Result of importVideoWithAudio: the stored video plus the WAV asset that was
// split out of its audio track. `audio` is empty when the video has no audio
// stream; `audioError` is set when there was an audio stream but extracting or
// storing it failed. In both cases the video import still succeeds.
struct VideoImport
{
    StoredAsset video;
    std::optional<StoredAsset> audio;
    QString audioError;
};

// Content-addressed asset store rooted at <projectRoot>/assets.
//
// Layout:
//   assets/<sha256>/asset.json
//   assets/<sha256>/content.<ext>       image/video/audio
//   assets/<sha256>/bundle/             html bundle (entry at bundle/<entry>)
//
// Same content is deduplicated by hash and keeps the name from the first
// import. Re-importing the same content simply returns the existing asset.
//
// Stored metadata includes hash/type/name/mime plus width/height for visual
// assets, durationSeconds/sampleRate/channels for timed media, and
// durationFrames computed from <projectRoot>/project.json fps (fallback 30).
//
// Video behaviour: importFile() stores the video and, when the file has an
// audio stream, also extracts a 48 kHz stereo WAV asset and records its hash
// in the video metadata as "audioHash". The returned StoredAsset is the video
// only. Use importVideoWithAudio() when both results are needed in one call.
class AssetStore
{
public:
    explicit AssetStore(QString projectRoot);

    // Imports an image/video/audio file. HTML must go through importPvhtml().
    std::optional<StoredAsset> importFile(const QString& path, QString* error = nullptr);

    // Same as importFile(), but returns the split-out WAV asset as well.
    std::optional<VideoImport> importVideoWithAudio(const QString& path,
                                                    QString* error = nullptr);

    // Imports a .pvhtml directory or zip archive.
    std::optional<StoredAsset> importPvhtml(const QString& path, QString* error = nullptr);

    // Returns std::nullopt when the asset directory or its content is missing.
    std::optional<StoredAsset> get(const AssetHash& hash) const;

    // All valid assets, sorted by name then hash.
    QVector<StoredAsset> list() const;

    // Removes the asset directory. Returns false if it does not exist.
    bool remove(const AssetHash& hash, QString* error = nullptr);

    QString projectRoot() const { return m_root; }
    QString assetsDirectory() const { return m_assetsDir; }

private:
    std::optional<StoredAsset> importFileImpl(const QString& path,
                                              QString* audioError,
                                              QString* error);
    std::optional<StoredAsset> ensureAudio(const StoredAsset& video, QString* error);
    std::optional<StoredAsset> getByHash(const AssetHash& hash) const;

    QString m_root;
    QString m_assetsDir;
};

} // namespace pvf::assets
