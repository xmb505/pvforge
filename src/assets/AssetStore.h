#pragma once

#include "common/Types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace pvf::assets {

struct StoredAsset
{
    AssetHash hash;
    AssetType type = AssetType::Image;
    QString name;
    QString directory;   // absolute path of assets/<sha256>
    QStringList tags;    // stored in asset.json as "tags"; empty for old assets
    QJsonObject metadata;
};

// True when asset.json and the directory exist but the content file (or the
// HTML bundle entry) is gone. Kept separate so get()/list() keep rejecting
// broken entries and only the *IncludingMissing variants return them.
bool missingContent(const StoredAsset& asset);

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
// Tags live in asset.json under "tags"; assets imported by older versions have
// no such field and are treated as untagged.
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

    // Like get(), but keeps an asset whose directory and asset.json exist while
    // the content file (or HTML bundle entry) is missing. Callers detect that
    // state with StoredAsset::missingContent().
    std::optional<StoredAsset> getIncludingMissing(const AssetHash& hash) const;

    // All valid assets, sorted by name then hash.
    QVector<StoredAsset> list() const;

    // Like list(), but also returns assets whose content file is missing.
    QVector<StoredAsset> listIncludingMissing() const;

    // Replaces the asset's tags in asset.json. Tags are trimmed, empty entries
    // dropped and duplicates removed. Tags longer than 64 characters and lists
    // with more than 32 unique tags are rejected. Returns false with an error
    // for a missing asset or invalid tags.
    bool setTags(const AssetHash& hash, const QStringList& tags,
                 QString* error = nullptr);

    // Removes the asset directory. Returns false if it does not exist.
    bool remove(const AssetHash& hash, QString* error = nullptr);

    QString projectRoot() const { return m_root; }
    QString assetsDirectory() const { return m_assetsDir; }

private:
    std::optional<StoredAsset> importFileImpl(const QString& path,
                                              QString* audioError,
                                              QString* error);
    std::optional<StoredAsset> ensureAudio(const StoredAsset& video, QString* error);
    std::optional<StoredAsset> getByHash(const AssetHash& hash,
                                         bool requireContent = true) const;

    QString m_root;
    QString m_assetsDir;
};

} // namespace pvf::assets
