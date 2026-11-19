#include "next/app/MediaExportSession.h"

#include "html2/HtmlFrameService.h"
#include "model/Entity.h"
#include "video2/VideoFrames.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pvf::next {

namespace {

constexpr int kDefaultHtmlWidth = 800;
constexpr int kDefaultHtmlHeight = 120;

int floorFrame(double value)
{
    return int(std::floor(value));
}

QSize htmlLogicalSize(const assets::StoredAsset& asset)
{
    QSize logical(asset.metadata.value(QStringLiteral("width")).toInt(),
                  asset.metadata.value(QStringLiteral("height")).toInt());
    if (logical.width() <= 0 || logical.height() <= 0)
        logical = QSize(kDefaultHtmlWidth, kDefaultHtmlHeight);
    return logical;
}

} // namespace

MediaExportSession::MediaExportSession(model::Project project,
                                       const assets::AssetStore& assets,
                                       const AppSettings& settings,
                                       QSize exportPixelSize)
    : m_project(std::move(project))
    , m_settings(settings)
    , m_exportPixelSize(exportPixelSize)
{
    for (const assets::StoredAsset& asset : assets.listIncludingMissing())
        m_assets.insert(asset.hash, asset);

    const QString assetsDir = assets.assetsDirectory();
    m_cacheRoot = QDir(QFileInfo(assetsDir).absolutePath())
                      .filePath(QStringLiteral(".cache"));
}

MediaExportSession::~MediaExportSession()
{
    finish();
}

const model::Entity* MediaExportSession::findAssetEntity(const Uuid& uuid,
                                                         AssetType type) const
{
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd())
        return nullptr;
    const model::Entity& entity = it.value();
    if (entity.content.kind != ContentKind::Asset
        || entity.content.asset.type != type)
        return nullptr;
    return &entity;
}

QString MediaExportSession::contentFilePath(
    const assets::StoredAsset& asset) const
{
    const QStringList names = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (names.isEmpty())
        return {};
    return QDir(asset.directory).filePath(names.first());
}

void MediaExportSession::noteError(const QString& message)
{
    if (!message.isEmpty())
        m_lastError = message;
}

QImage MediaExportSession::videoFrame(const Uuid& entityUuid,
                                      double projectFrame)
{
    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Video);
    if (!entity)
        return {};

    const auto asset = m_assets.constFind(entity->content.asset.hash);
    if (asset == m_assets.constEnd())
        return {};
    if (assets::missingContent(*asset))
        return {};

    const QString path = contentFilePath(*asset);
    if (path.isEmpty())
        return {};

    if (!m_videoFrames)
    {
        // Full-resolution decoder reserved for this export; a couple of entries
        // are enough because 2x/4x output fps repeats the same project frame.
        m_videoFrames = std::make_unique<video2::VideoFrames>(0, 2);
    }

    video2::Request request;
    request.path = path;
    request.sourceFrame =
        entity->content.asset.sourceIn + floorFrame(projectFrame - entity->start);
    request.fps = std::max(1, m_project.settings.fps);
    request.size = QSize();
    request.proxy = false;

    QString error;
    const QImage image = m_videoFrames->frame(request, &error);
    if (image.isNull())
        noteError(error);
    return image;
}

QSize MediaExportSession::htmlOutputSize(
    const assets::StoredAsset& asset) const
{
    // density = export pixels per project model unit; the logical HTML size
    // scales by the same factor so 2x/4x only add sharpness.
    const int projectWidth = std::max(1, m_project.settings.width);
    const int projectHeight = std::max(1, m_project.settings.height);
    const double densityX =
        double(std::max(1, m_exportPixelSize.width())) / double(projectWidth);
    const double densityY =
        double(std::max(1, m_exportPixelSize.height())) / double(projectHeight);
    const double density = std::min(densityX, densityY);
    const QSize logical = htmlLogicalSize(asset);
    return QSize(
        std::max(1, int(std::lround(double(logical.width()) * density))),
        std::max(1, int(std::lround(double(logical.height()) * density))));
}

QImage MediaExportSession::htmlFrame(const Uuid& entityUuid,
                                     double projectFrame)
{
    const model::Entity* entity = findAssetEntity(entityUuid, AssetType::Html);
    if (!entity)
        return {};

    const auto asset = m_assets.constFind(entity->content.asset.hash);
    if (asset == m_assets.constEnd())
        return {};
    if (assets::missingContent(*asset))
        return {};

    if (!m_html)
    {
        // A private, final-only service: no preview/cache browsers, no shared
        // cache index.  It lives entirely on the export worker thread.
        m_html = std::make_unique<html2::HtmlFrameService>(
            QDir(m_cacheRoot).filePath(QStringLiteral("html")),
            m_settings.htmlMemoryBytes, m_settings.htmlDiskBytes);
        m_html->setChromeExecutable(m_settings.chromeExecutable);
        m_html->setFinalOnly(true);
    }

    html2::FrameRequest request;
    request.assetHash = entity->content.asset.hash;
    request.bundleDir =
        QDir(asset->directory).filePath(QStringLiteral("bundle"));
    request.entry = asset->metadata.value(QStringLiteral("entry")).toString();
    if (request.entry.isEmpty())
        request.entry = QStringLiteral("index.html");
    request.timeDependent =
        asset->metadata.value(QStringLiteral("timeDependent")).toBool(true);
    request.localFrame = floorFrame(projectFrame - entity->start);
    request.time = (projectFrame - entity->start)
                   / double(std::max(1, m_project.settings.fps));
    request.size = htmlOutputSize(*asset);
    request.vars = asset->metadata.value(QStringLiteral("variables"))
                       .toObject()
                       .toVariantMap();

    const double relativeFrame = projectFrame - entity->start;
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd();
         ++it)
    {
        if (!it.key().startsWith(QStringLiteral("var."), Qt::CaseInsensitive))
            continue;
        const std::optional<double> value =
            model::evaluateTrack(it.value(), relativeFrame);
        if (value)
            request.vars.insert(it.key().mid(4), *value);
    }

    QString error;
    const QImage image = m_html->finalFrame(request, &error);
    if (image.isNull())
        noteError(error.isEmpty() ? QStringLiteral("HTML 帧渲染失败") : error);
    return image;
}

void MediaExportSession::finish()
{
    m_html.reset();
    m_videoFrames.reset();
}

} // namespace pvf::next
