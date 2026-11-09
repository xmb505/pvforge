#pragma once

#include "common/Types.h"
#include "next/settings/SettingsStore.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <map>
#include <memory>

namespace pvf::assets {
struct StoredAsset;
}

namespace pvf::html2 {
class HtmlFrameService;
struct FrameRequest;
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

    // render2::FrameProvider implementations.  Both return a null image when
    // the entity, its asset or the decode result is missing.
    QImage videoFrame(const Uuid& entityUuid, double projectFrame);
    QImage htmlFrame(const Uuid& entityUuid, double projectFrame);

    QString cacheRoot() const { return m_cacheRoot; }
    bool htmlRunning() const;
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
    std::unique_ptr<video2::VideoFrames> makeVideoFrames() const;

    ProjectDocument* m_document = nullptr;
    AppSettings m_settings;
    QString m_cacheRoot;
    QString m_htmlError;
    bool m_htmlStartFailed = false;

    std::unique_ptr<video2::VideoFrames> m_videoFrames;
    std::map<AssetHash, std::unique_ptr<video2::VideoSequenceService>> m_sequences;
    std::unique_ptr<html2::HtmlFrameService> m_html;
    // Guards against a queued update() re-entering htmlFrame() while
    // lowResWait() pumps a nested event loop inside paintEvent().
    bool m_htmlInFlight = false;
};

} // namespace pvf::next
