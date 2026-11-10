#include "next/app/MediaServices.h"

#include "assets/AssetStore.h"
#include "html2/HtmlFrameService.h"
#include "html2/PvhtmlRenderer.h"
#include "model/Entity.h"
#include "next/app/ProjectDocument.h"
#include "video2/VideoFrames.h"
#include "video2/VideoSequenceService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::next {

namespace {

constexpr qint64 kVideoDiskBudgetBytes = 2LL * 1024 * 1024 * 1024;
constexpr int kDefaultVideoProxyWidth = 960;
constexpr int kDefaultVideoMemoryFrames = 64;
constexpr int kMaxHtmlPrefetchFrames = 60;

int floorFrame(double value)
{
    return int(std::floor(value));
}

int projectContentEnd(const model::Project& project)
{
    int end = 0;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        if (it.value().parentUuid.isEmpty())
            end = std::max(end, it.value().end);
    }
    return end;
}

} // namespace

MediaServices::MediaServices(ProjectDocument* document, const AppSettings& settings,
                             QObject* parent)
    : QObject(parent)
    , m_document(document)
    , m_settings(settings)
{
    if (m_document)
    {
        const QString assetsDir = m_document->assets().assetsDirectory();
        m_cacheRoot = QDir(QFileInfo(assetsDir).absolutePath())
                          .filePath(QStringLiteral(".cache"));
    }

    m_videoFrames = makeVideoFrames();
    m_html = std::make_unique<html2::HtmlFrameService>(
        QDir(m_cacheRoot).filePath(QStringLiteral("html")),
        settings.htmlMemoryBytes, settings.htmlDiskBytes);
    m_html->setChromeExecutable(settings.chromeExecutable);

    // frameReady is already emitted through a queued invocation; the extra
    // queued hop keeps the viewport refresh out of the render completion stack.
    connect(m_html.get(), &html2::HtmlFrameService::frameReady, this,
            [this](const AssetHash&, int, const QImage&) {
                m_htmlPending = false;
                emit htmlFrameReady();
            },
            Qt::QueuedConnection);
}

MediaServices::~MediaServices()
{
    stop();
}

bool MediaServices::start(QString* error)
{
    if (!m_html)
    {
        if (error)
            *error = tr("HTML 服务不可用");
        return false;
    }
    if (error)
        error->clear();

    // Browsers are only kept alive for projects that actually show HTML.
    if (!projectHasHtml())
        return true;

    return startHtml(error);
}

void MediaServices::stop()
{
    m_sequences.clear();
    if (m_videoFrames)
        m_videoFrames->clear();
    m_exportVideoFrames.reset();
    if (m_html)
        m_html->stop();
    m_htmlInFlight = false;
    m_htmlPending = false;
    m_lastPrefetchFrame = -1;
}

void MediaServices::applySettings(const AppSettings& settings)
{
    const bool videoChanged = settings.videoProxyWidth != m_settings.videoProxyWidth
                              || settings.videoMemoryFrames != m_settings.videoMemoryFrames;
    const bool htmlConfigChanged = settings.htmlMemoryBytes != m_settings.htmlMemoryBytes
                                   || settings.htmlDiskBytes != m_settings.htmlDiskBytes
                                   || settings.chromeExecutable != m_settings.chromeExecutable;
    m_settings = settings;

    if (htmlConfigChanged && m_html)
    {
        m_html->stop();
        m_html = std::make_unique<html2::HtmlFrameService>(
            QDir(m_cacheRoot).filePath(QStringLiteral("html")),
            settings.htmlMemoryBytes, settings.htmlDiskBytes);
        m_html->setChromeExecutable(settings.chromeExecutable);
        connect(m_html.get(), &html2::HtmlFrameService::frameReady, this,
                [this](const AssetHash&, int, const QImage&) {
                    m_htmlPending = false;
                    emit htmlFrameReady();
                },
                Qt::QueuedConnection);
        m_htmlInFlight = false;
        m_htmlPending = false;
        m_lastPrefetchFrame = -1;
        m_htmlStartFailed = false;
        m_htmlError.clear();
        if (projectHasHtml())
        {
            QString startError;
            if (!startHtml(&startError))
                emit htmlServiceFailed(startError);
        }
    }

    if (!videoChanged)
        return;

    // SequenceOptions bake in the proxy width and memory budget, so both the
    // extracted sequences and the proxy decoder start over.
    m_sequences.clear();
    m_videoFrames = makeVideoFrames();
}

bool MediaServices::htmlRunning() const
{
    return m_html && m_html->isRunning();
}

void MediaServices::stopExportHtml()
{
    if (m_html)
        m_html->stopFinal();
}

bool MediaServices::startHtml(QString* error)
{
    if (htmlRunning())
    {
        if (error)
            error->clear();
        return true;
    }
    if (m_htmlStartFailed)
    {
        if (error)
            *error = m_htmlError;
        return false;
    }

    QString message;
    if (!m_html->start(&message))
    {
        m_htmlStartFailed = true;
        m_htmlError = message.isEmpty() ? tr("HTML 服务启动失败") : message;
        if (error)
            *error = m_htmlError;
        emit htmlServiceFailed(m_htmlError);
        return false;
    }

    m_htmlError.clear();
    if (error)
        error->clear();
    return true;
}

bool MediaServices::projectHasHtml() const
{
    if (!m_document)
        return false;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind == ContentKind::Asset
            && entity.content.asset.type == AssetType::Html)
            return true;
    }
    return false;
}

const model::Entity* MediaServices::findAssetEntity(const Uuid& uuid,
                                                    AssetType type) const
{
    if (!m_document)
        return nullptr;
    const auto& entities = m_document->project().entities;
    const auto it = entities.constFind(uuid);
    if (it == entities.constEnd())
        return nullptr;
    const model::Entity& entity = it.value();
    if (entity.content.kind != ContentKind::Asset
        || entity.content.asset.type != type)
        return nullptr;
    return &entity;
}

QString MediaServices::contentFilePath(const assets::StoredAsset& asset) const
{
    const QStringList names = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (names.isEmpty())
        return {};
    return QDir(asset.directory).filePath(names.first());
}

int MediaServices::projectFps() const
{
    if (!m_document)
        return 30;
    return std::max(1, m_document->project().settings.fps);
}

QSize MediaServices::proxySize(const assets::StoredAsset& asset) const
{
    const int sourceWidth = asset.metadata.value(QStringLiteral("width")).toInt();
    const int sourceHeight = asset.metadata.value(QStringLiteral("height")).toInt();
    if (sourceWidth <= 0 || sourceHeight <= 0)
        return {};

    const int maxWidth = m_settings.videoProxyWidth > 0
                             ? m_settings.videoProxyWidth
                             : kDefaultVideoProxyWidth;
    const int width = std::min(maxWidth, sourceWidth);
    const int height = std::max(
        1, int(std::lround(double(sourceHeight) * double(width) / double(sourceWidth))));
    return QSize(width, height);
}

QImage MediaServices::videoFrame(const Uuid& entityUuid, double projectFrame)
{
    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Video);
    if (!entity)
        return {};

    const auto asset =
        m_document->assets().get(entity->content.asset.hash);
    if (!asset)
        return {};

    const int localFrame = floorFrame(projectFrame - entity->start);
    const int sourceFrame = entity->content.asset.sourceIn + localFrame;

    if (video2::VideoSequenceService* sequence = sequenceService(*asset))
    {
        const QImage frame = sequence->frame(sourceFrame);
        if (!frame.isNull())
            return frame;
    }

    const QString path = contentFilePath(*asset);
    if (!path.isEmpty() && m_videoFrames)
    {
        video2::Request request;
        request.path = path;
        request.sourceFrame = sourceFrame;
        request.fps = projectFps();
        request.size = proxySize(*asset);
        request.proxy = true;
        const QImage frame = m_videoFrames->frame(request);
        if (!frame.isNull())
            return frame;
    }
    return {};
}

QImage MediaServices::htmlFrame(const Uuid& entityUuid, double projectFrame)
{
    if (!m_html || m_htmlInFlight)
        return {};

    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Html);
    if (!entity)
        return {};

    const auto asset =
        m_document->assets().get(entity->content.asset.hash);
    if (!asset)
        return {};

    const html2::FrameRequest request =
        makeHtmlRequest(*entity, *asset, projectFrame);

    // paintEvent() must never wait: a cache hit draws now, a miss queues the
    // frontend worker and the frameReady signal refreshes the viewport.
    QImage image = m_html->cachedLowRes(request);
    if (!image.isNull())
    {
        m_htmlPending = false;
        return image;
    }

    m_html->requestLowRes(request);
    m_htmlPending = true;
    return {};
}

void MediaServices::prepareHtmlPreview(int maxWaitMs){
    if (!m_html || !m_document || m_preparingHtml || m_htmlInFlight)
        return;

    const int playhead = m_document->playhead();
    QElapsedTimer elapsed;
    elapsed.start();

    const model::Project& project = m_document->project();
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind != ContentKind::Asset
            || entity.content.asset.type != AssetType::Html)
            continue;
        if (playhead < entity.start || playhead >= entity.end)
            continue;

        const auto asset = m_document->assets().get(entity.content.asset.hash);
        if (!asset)
            continue;

        const html2::FrameRequest request =
            makeHtmlRequest(entity, *asset, double(playhead));
        if (!m_html->cachedLowRes(request).isNull())
            continue;

        const int remaining = maxWaitMs - int(elapsed.elapsed());
        if (remaining <= 0)
            break;

        m_preparingHtml = true;
        const QImage image = m_html->lowResWait(request, remaining);
        m_preparingHtml = false;
        if (!image.isNull())
            m_htmlPending = false;
    }
}

void MediaServices::prefetchHtml(int frame, bool force)
{
    if (!m_html || !m_document)
        return;
    if (force)
        m_lastPrefetchFrame = -1;
    if (m_lastPrefetchFrame == frame)
        return;
    if (!m_prefetchSink && !m_html->isRunning())
        return;

    const QVector<html2::FrameRequest> requests =
        buildHtmlPrefetchRequests(frame);
    // Record the frame even for an empty plan: repeating it would only rebuild
    // the same empty list.
    m_lastPrefetchFrame = frame;
    if (requests.isEmpty())
        return;

    if (m_prefetchSink)
        m_prefetchSink(requests);
    else
        m_html->prefetch(requests);
}

QVector<html2::FrameRequest> MediaServices::buildHtmlPrefetchRequests(
    int frame) const
{
    QVector<html2::FrameRequest> requests;
    if (!m_document)
        return requests;

    const model::Project& project = m_document->project();
    const int contentEnd = projectContentEnd(project);
    int remaining = kMaxHtmlPrefetchFrames;

    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.content.kind != ContentKind::Asset
            || entity.content.asset.type != AssetType::Html)
            continue;
        if (frame < entity.start || frame >= entity.end)
            continue;

        const auto asset = m_document->assets().get(entity.content.asset.hash);
        if (!asset)
            continue;

        int segmentEnd = entity.end;
        if (contentEnd > 0)
            segmentEnd = std::min(segmentEnd, contentEnd);
        if (segmentEnd <= frame)
            continue;

        // The next variable keyframe is the first frame whose cache key differs
        // from the current one; stop before it because the frontend browser
        // renders that frame when playback reaches it.
        int boundary = segmentEnd;
        for (auto track = entity.tracks.constBegin();
             track != entity.tracks.constEnd(); ++track)
        {
            if (!track.key().startsWith(QStringLiteral("var."),
                                        Qt::CaseInsensitive))
                continue;
            for (const model::Keyframe& key : track.value().keys)
            {
                const qint64 absolute = qint64(entity.start) + key.frame;
                if (absolute > frame && absolute < boundary)
                    boundary = int(absolute);
            }
        }

        const html2::FrameRequest current =
            makeHtmlRequest(entity, *asset, double(frame));
        // A static page shares one cache entry for every timeline frame.
        if (!current.timeDependent)
        {
            requests.append(current);
            if (--remaining <= 0)
                break;
            continue;
        }

        for (int f = frame; f < boundary && remaining > 0; ++f)
        {
            requests.append(makeHtmlRequest(entity, *asset, double(f)));
            --remaining;
        }
        // Then fill the frames before the playhead for scrubbing backwards.
        for (int f = frame - 1; f >= entity.start && remaining > 0; --f)
        {
            requests.append(makeHtmlRequest(entity, *asset, double(f)));
            --remaining;
        }
        if (remaining <= 0)
            break;
    }
    return requests;
}

QImage MediaServices::htmlFrameBlocking(const Uuid& entityUuid,
                                        double projectFrame, int maxWaitMs)
{
    if (!m_html || m_htmlInFlight)
        return {};

    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Html);
    if (!entity)
        return {};
    const auto asset = m_document->assets().get(entity->content.asset.hash);
    if (!asset)
        return {};

    const html2::FrameRequest request =
        makeHtmlRequest(*entity, *asset, projectFrame);
    m_htmlInFlight = true;
    const QImage image = m_html->lowResWait(request, maxWaitMs);
    m_htmlInFlight = false;
    return image;
}

QImage MediaServices::exportVideoFrame(const Uuid& entityUuid,
                                       double projectFrame)
{
    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Video);
    if (!entity || !m_document)
        return {};

    const auto asset =
        m_document->assets().get(entity->content.asset.hash);
    if (!asset)
        return {};

    const QString path = contentFilePath(*asset);
    if (path.isEmpty())
        return {};

    if (!m_exportVideoFrames)
    {
        // A couple of entries are enough: 2x/4x output fps samples the same
        // project frame several times in a row.  Full-resolution images are
        // large, so keep this cache tiny.
        m_exportVideoFrames = std::make_unique<video2::VideoFrames>(0, 2);
    }

    const int localFrame = floorFrame(projectFrame - entity->start);
    video2::Request request;
    request.path = path;
    request.sourceFrame = entity->content.asset.sourceIn + localFrame;
    request.fps = projectFps();
    request.size = QSize();
    request.proxy = false;

    QString error;
    const QImage image = m_exportVideoFrames->frame(request, &error);
    if (!error.isEmpty())
        qWarning().noquote() << "export video frame failed:" << error;
    return image;
}

QImage MediaServices::exportHtmlFrame(const Uuid& entityUuid,
                                      double projectFrame,
                                      const QSize& outputSize)
{
    if (!m_html || !m_document)
        return {};
    if (!outputSize.isValid() || outputSize.isEmpty())
        return {};

    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Html);
    if (!entity)
        return {};

    const auto asset =
        m_document->assets().get(entity->content.asset.hash);
    if (!asset)
        return {};

    // The renderer/service is started lazily by finalFrame(); NextWindow
    // normally pre-starts it on the main thread before launching the worker.
    html2::FrameRequest request =
        makeHtmlRequest(*entity, *asset, projectFrame);
    request.size = outputSize;

    QString error;
    const QImage image = m_html->finalFrame(request, &error);
    if (image.isNull())
        qWarning().noquote() << "export HTML frame failed:"
                             << (error.isEmpty() ? m_htmlError : error);
    return image;
}

QSize MediaServices::htmlRequestSize() const
{
    if (!m_document)
        return {};

    const model::ProjectSettings& project = m_document->project().settings;
    int width = std::max(1, int(std::lround(project.width * m_settings.htmlScale)));
    int height = std::max(1, int(std::lround(project.height * m_settings.htmlScale)));

    const int maxWidth = m_settings.htmlMaxWidth;
    if (maxWidth > 0 && width > maxWidth)
    {
        height = std::max(
            1, int(std::lround(double(height) * double(maxWidth) / double(width))));
        width = maxWidth;
    }
    return QSize(width, height);
}

html2::FrameRequest MediaServices::makeHtmlRequest(const model::Entity& entity,
                                                   const assets::StoredAsset& asset,
                                                   double projectFrame) const
{
    html2::FrameRequest request;
    request.assetHash = entity.content.asset.hash;
    request.bundleDir =
        QDir(asset.directory).filePath(QStringLiteral("bundle"));
    request.entry = asset.metadata.value(QStringLiteral("entry")).toString();
    if (request.entry.isEmpty())
        request.entry = QStringLiteral("index.html");
    request.timeDependent =
        asset.metadata.value(QStringLiteral("timeDependent")).toBool(true);
    request.localFrame = floorFrame(projectFrame - entity.start);
    request.time = (projectFrame - entity.start) / double(projectFps());
    request.size = htmlRequestSize();

    // Metadata declares the variables (and their defaults); an entity track
    // named var.<name> overrides the default at the entity-relative frame.
    request.vars = asset.metadata.value(QStringLiteral("variables"))
                       .toObject()
                       .toVariantMap();
    const double relativeFrame = projectFrame - entity.start;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        if (!it.key().startsWith(QStringLiteral("var."), Qt::CaseInsensitive))
            continue;
        const std::optional<double> value =
            model::evaluateTrack(it.value(), relativeFrame);
        if (value)
            request.vars.insert(it.key().mid(4), *value);
    }
    return request;
}

video2::VideoSequenceService* MediaServices::sequenceService(
    const assets::StoredAsset& asset)
{
    const auto existing = m_sequences.find(asset.hash);
    if (existing != m_sequences.end())
        return existing->second.get();

    const QString path = contentFilePath(asset);
    if (path.isEmpty())
        return nullptr;

    video2::SequenceOptions options;
    options.cacheRoot = QDir(m_cacheRoot).filePath(QStringLiteral("video"));
    options.assetHash = asset.hash;
    options.videoPath = path;
    options.projectFps = projectFps();
    options.maxWidth = m_settings.videoProxyWidth > 0
                           ? m_settings.videoProxyWidth
                           : kDefaultVideoProxyWidth;
    options.diskBudgetBytes = kVideoDiskBudgetBytes;
    options.memoryEntries = m_settings.videoMemoryFrames;

    auto sequence = std::make_unique<video2::VideoSequenceService>(options);
    video2::VideoSequenceService* raw = sequence.get();
    m_sequences.emplace(asset.hash, std::move(sequence));
    return raw;
}

video2::VideoSequenceService* MediaServices::videoSequenceForTest(
    const AssetHash& hash) const
{
    const auto it = m_sequences.find(hash);
    return it == m_sequences.end() ? nullptr : it->second.get();
}

std::unique_ptr<video2::VideoFrames> MediaServices::makeVideoFrames() const
{
    const int proxyWidth = m_settings.videoProxyWidth > 0
                               ? m_settings.videoProxyWidth
                               : kDefaultVideoProxyWidth;
    const int memoryEntries = m_settings.videoMemoryFrames >= 0
                                  ? m_settings.videoMemoryFrames
                                  : kDefaultVideoMemoryFrames;
    return std::make_unique<video2::VideoFrames>(proxyWidth, memoryEntries);
}

} // namespace pvf::next
