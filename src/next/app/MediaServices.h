#pragma once

#include "common/Types.h"
#include "html2/PvhtmlRenderer.h"
#include "next/settings/SettingsStore.h"
#include "solver/Solver.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>
#include <map>
#include <memory>
#include <utility>

namespace pvf::assets {
struct StoredAsset;
}

namespace pvf::html2 {
class HtmlFrameService;
}

namespace pvf::model {
struct Entity;
}

namespace pvf::video2 {
class VideoFrames;
class VideoSequenceService;
}

namespace pvf::next {

class ProjectDocument;

// Per-project media services behind the viewport frame providers.
//
// One instance follows one ProjectDocument: the HTML renderer cache lives in
// <projectRoot>/.cache/html and every video asset gets a lazily created
// VideoSequenceService cached under <projectRoot>/.cache/video.  The window
// destroys this object before the document it points at.
//
// The HTML frontend/cache browsers are only started when the project actually
// has an HTML entity (docs/ENTITY_MODEL.md 6.5: "有 HTML 层就常驻"); an HTML
// entity assigned later starts them on the first frame request.  A start
// failure never blocks the rest of the editor: htmlFrame() returns an empty
// image and the viewport keeps render2's placeholder.
class MediaServices : public QObject
{
    Q_OBJECT

public:
    MediaServices(ProjectDocument* document, const AppSettings& settings,
                  QObject* parent = nullptr);
    ~MediaServices() override;

    MediaServices(const MediaServices&) = delete;
    MediaServices& operator=(const MediaServices&) = delete;

    // Starts the HTML service when the project has HTML content.  Returns false
    // with *error on a start failure; video providers keep working either way.
    bool start(QString* error = nullptr);
    void stop();

    // HTML scale/maxWidth are read from the current settings per request; a
    // video proxy/memory change drops the decoders and sequence services.
    void applySettings(const AppSettings& settings);

    // Borrowed solver-result map owned by EditorSession.  CameraMonitor's
    // render2 path consults it so a monitor repaint reuses solved segments
    // instead of re-running physics.  Only the pointer is stored; the owner
    // clears and destroys the map.
    void setSolverResults(QHash<QString, solver::Result>* results)
    {
        m_solverResults = results;
    }
    QHash<QString, solver::Result>* solverResults() const
    {
        return m_solverResults;
    }

    // render2::FrameProvider implementations.  Both return a null image when
    // the entity, its asset or the decode result is missing.
    QImage videoFrame(const Uuid& entityUuid, double projectFrame);
    // Cache-only: never waits inside paintEvent().  A miss queues a background
    // render and returns a null image; htmlFrameReady follows.
    QImage htmlFrame(const Uuid& entityUuid, double projectFrame);
    // Blocks up to maxWaitMs for the HTML entities under the current playhead
    // (the documented 300 ms miss budget).  Must be called outside paintEvent().
    void prepareHtmlPreview(int maxWaitMs = 300);
    // Queues background low-resolution renders for the active HTML entities:
    // from `frame` forward to the next var.* keyframe, clip end or project end,
    // then backwards for earlier frames.  At most 60 requests per call and
    // never blocks.  A repeated call for the same frame is ignored unless
    // force is set (used after document edits).  No-op while the service is
    // stopped; callers should skip this during active playback.
    void prefetchHtml(int frame, bool force = false);
    // Test hook replacing the cache browser submit with a capture sink.
    using HtmlPrefetchSink =
        std::function<void(const QVector<html2::FrameRequest>&)>;
    void setHtmlPrefetchSinkForTest(HtmlPrefetchSink sink)
    {
        m_prefetchSink = std::move(sink);
    }
    // Single-entity blocking render for MCP preview.render_frame; safe on the
    // UI thread because it never runs inside paintEvent().
    QImage htmlFrameBlocking(const Uuid& entityUuid, double projectFrame,
                             int maxWaitMs = 300);

    // Export providers (src/export2).  Unlike the viewport providers they
    // never use the proxy/WebP sequence or the low-resolution HTML browsers.
    //
    // exportVideoFrame decodes the original asset file at full resolution with
    // its own VideoFrames instance, so it can be called from the single export
    // worker thread without disturbing preview playback.
    //
    // exportHtmlFrame renders through the final (third) browser at outputSize.
    // The caller must only use it from one export worker thread; MediaServices
    // never calls finalFrame() itself, so the service starts lazily on the
    // first export request.
    QImage exportVideoFrame(const Uuid& entityUuid, double projectFrame);
    QImage exportHtmlFrame(const Uuid& entityUuid, double projectFrame,
                           const QSize& outputSize);

    QString cacheRoot() const { return m_cacheRoot; }
    bool htmlRunning() const;
    // Releases the final (export) HTML browser.  Must be called from the
    // export worker thread that produced the final frames; stop() on the UI
    // thread does not own that renderer.
    void stopExportHtml();
    // True while the latest htmlFrame() missed the cache and timed out: the
    // frontend browser is still rendering it and htmlFrameReady will follow.
    // The viewport shows render2's placeholder meanwhile, so framePresented
    // stays silent; the workbench prewarm also waits for this signal.
    bool htmlPending() const { return m_htmlPending; }
    bool htmlStartFailed() const { return m_htmlStartFailed; }
    QString htmlError() const { return m_htmlError; }

    // Test accessors.
    QSize htmlRequestSizeForTest() const { return htmlRequestSize(); }
    html2::HtmlFrameService* htmlServiceForTest() const { return m_html.get(); }
    video2::VideoSequenceService* videoSequenceForTest(const AssetHash& hash) const;

signals:
    // A low-resolution frame finished rendering in the background.  The window
    // connects this to ViewportWidget::refresh() with a queued connection.
    void htmlFrameReady();
    // The HTML browsers could not be started; the message is for the status
    // bar / MCP log.  HTML frames stay empty until the project is reopened.
    void htmlServiceFailed(const QString& message);

private:
    const model::Entity* findAssetEntity(const Uuid& uuid, AssetType type) const;
    bool projectHasHtml() const;
    bool startHtml(QString* error);
    video2::VideoSequenceService* sequenceService(const assets::StoredAsset& asset);
    QString contentFilePath(const assets::StoredAsset& asset) const;
    QSize htmlRequestSize() const;
    QSize proxySize(const assets::StoredAsset& asset) const;
    int projectFps() const;
    html2::FrameRequest makeHtmlRequest(const model::Entity& entity,
                                        const assets::StoredAsset& asset,
                                        double projectFrame) const;
    QVector<html2::FrameRequest> buildHtmlPrefetchRequests(int frame) const;
    std::unique_ptr<video2::VideoFrames> makeVideoFrames() const;

    ProjectDocument* m_document = nullptr;
    // Borrowed from EditorSession; never cleared or destroyed here.
    QHash<QString, solver::Result>* m_solverResults = nullptr;
    AppSettings m_settings;
    QString m_cacheRoot;
    QString m_htmlError;
    bool m_htmlStartFailed = false;

    std::unique_ptr<video2::VideoFrames> m_videoFrames;
    // Full-resolution decoder reserved for the export worker; not shared with
    // the preview/proxy decoder above.
    std::unique_ptr<video2::VideoFrames> m_exportVideoFrames;
    std::map<AssetHash, std::unique_ptr<video2::VideoSequenceService>> m_sequences;
    std::unique_ptr<html2::HtmlFrameService> m_html;
    HtmlPrefetchSink m_prefetchSink;
    // Last frame passed to prefetchHtml(); avoids re-submitting the same batch
    // on every repaint.  force or a document edit resets it.
    int m_lastPrefetchFrame = -1;
    // Guards against a queued update() re-entering htmlFrame() while
    // prepareHtmlPreview() pumps a nested event loop.
    bool m_htmlInFlight = false;
    bool m_preparingHtml = false;
    // Set by a missed htmlFrame() request, cleared by htmlFrameReady.
    bool m_htmlPending = false;
};

} // namespace pvf::next
