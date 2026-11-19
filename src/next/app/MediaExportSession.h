#pragma once

#include "assets/AssetStore.h"
#include "model/Project.h"
#include "next/settings/SettingsStore.h"

#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace pvf::html2 {
class HtmlFrameService;
}

namespace pvf::video2 {
class VideoFrames;
}

namespace pvf::next {

// Immutable, export-only view of one project (issue #126).
//
// The object is constructed on the UI thread from a deep copy of the project
// and a snapshot of the asset metadata; every content file is read from the
// captured asset directory.  After construction only the single export worker
// thread touches it: videoFrame()/htmlFrame() use private decoders (a full
// resolution VideoFrames and a final-only HtmlFrameService) so an export can
// never observe a live Document or fight the preview services for a cache or a
// CDP pipe.
//
// finish() releases the decoders and the export browser.  Call it on the export
// worker thread before the future completes; the destructor is then just a
// no-op for an empty session.
class MediaExportSession
{
public:
    MediaExportSession(model::Project project,
                       const assets::AssetStore& assets,
                       const AppSettings& settings,
                       QSize exportPixelSize);
    ~MediaExportSession();

    MediaExportSession(const MediaExportSession&) = delete;
    MediaExportSession& operator=(const MediaExportSession&) = delete;

    // render2::FrameProvider implementations.  An empty image means the entity,
    // its asset, the decode or the HTML frame is unavailable; render2 turns
    // that into a failed export (issue #129).
    QImage videoFrame(const Uuid& entityUuid, double projectFrame);
    QImage htmlFrame(const Uuid& entityUuid, double projectFrame);

    // Releases the decoders/browser.  Safe to call once; later frame requests
    // return null images.
    void finish();

    // Last decoder/HTML error, for the export error message.
    QString lastError() const { return m_lastError; }

private:
    const model::Entity* findAssetEntity(const Uuid& uuid, AssetType type) const;
    QSize htmlOutputSize(const assets::StoredAsset& asset) const;
    QString contentFilePath(const assets::StoredAsset& asset) const;
    void noteError(const QString& message);

    model::Project m_project;
    QHash<AssetHash, assets::StoredAsset> m_assets;
    AppSettings m_settings;
    QString m_cacheRoot;
    QSize m_exportPixelSize;

    std::unique_ptr<video2::VideoFrames> m_videoFrames;
    std::unique_ptr<html2::HtmlFrameService> m_html;
    QString m_lastError;
};

} // namespace pvf::next
