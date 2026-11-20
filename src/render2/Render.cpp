#include "Render.h"

#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "core/VectorShape.h"
#include "perf/FrameProfiler.h"
#include "render2/ImageCache.h"

#include <QBrush>
#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QTransform>
#include <QVariantMap>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pvf::render2 {
namespace {

constexpr double kCheckerCell = 16.0;
constexpr int kDefaultVisualWidth = 320;
constexpr int kDefaultVisualHeight = 180;
constexpr int kDefaultHtmlWidth = 800;
constexpr int kDefaultHtmlHeight = 120;

const QColor kMissingCardBorder(0xc0, 0x39, 0x2b);
const QColor kMissingCardBase(26, 26, 30);
const QColor kMissingCardAlternate(38, 38, 44);

QColor colorOr(const QString& value, const QColor& fallback)
{
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

bool activeAt(const model::Entity& entity, double projectFrame)
{
    return projectFrame >= double(entity.start)
           && projectFrame < double(entity.end);
}

QVariantMap textStyleData(const model::TextContent& text)
{
    QVariantMap data;
    data.insert(QStringLiteral("text"), text.text);
    data.insert(QStringLiteral("font"), text.fontFamily);
    data.insert(QStringLiteral("size"), text.pixelSize);
    data.insert(QStringLiteral("color"), colorOr(text.color, Qt::white));
    data.insert(QStringLiteral("textStrokeColor"),
                colorOr(text.strokeColor, Qt::black));
    data.insert(QStringLiteral("textStrokeWidth"), text.strokeWidth);
    data.insert(QStringLiteral("align"), text.align);
    data.insert(QStringLiteral("lineHeight"), text.lineHeight);
    data.insert(QStringLiteral("letterSpacing"), text.letterSpacing);
    data.insert(QStringLiteral("padding"), text.padding);
    data.insert(QStringLiteral("boxWidth"), text.boxWidth);
    data.insert(QStringLiteral("boxHeight"), text.boxHeight);
    data.insert(QStringLiteral("bold"), text.bold);
    data.insert(QStringLiteral("italic"), text.italic);
    data.insert(QStringLiteral("underline"), text.underline);
    data.insert(QStringLiteral("vertical"), text.vertical);
    data.insert(QStringLiteral("verticalRtl"), text.verticalRtl);
    return data;
}

QSize metadataSize(const assets::StoredAsset& asset)
{
    const int width = asset.metadata.value(QStringLiteral("width")).toInt();
    const int height = asset.metadata.value(QStringLiteral("height")).toInt();
    if (width > 0 && height > 0)
        return QSize(width, height);
    return {};
}

QSize placeholderSize(const assets::AssetStore& store,
                      const model::AssetContent& content)
{
    if (const auto stored = store.get(content.hash))
    {
        const QSize size = metadataSize(*stored);
        if (!size.isEmpty())
            return size;
    }

    if (content.type == AssetType::Html)
        return QSize(kDefaultHtmlWidth, kDefaultHtmlHeight);
    return QSize(kDefaultVisualWidth, kDefaultVisualHeight);
}

QRectF centeredRect(const QSizeF& size)
{
    const double width = std::max(1.0, size.width());
    const double height = std::max(1.0, size.height());
    return QRectF(-width / 2.0, -height / 2.0, width, height);
}

QRectF centeredRect(const QSize& size)
{
    return centeredRect(QSizeF(size));
}

// Model-space rectangle of a visual asset entity.  A decoded video/HTML frame
// is only a texture: its pixel size depends on the preview proxy (960 px) or
// the export resolution and must never define the entity's world rectangle.
// The asset metadata (original width/height; HTML logicalSize) is the single
// source of truth, so painting, contentSize(), the selection box and the
// collision shape all share one rectangle.
QRectF assetBounds(const assets::AssetStore& store,
                   const model::AssetContent& content)
{
    return centeredRect(placeholderSize(store, content));
}

// Export diagnostics (issue #129): record that this entity produced no
// drawable content instead of silently painting the preview placeholder.
void noteContentIssue(const Options& options, const model::Entity& entity,
                      const QString& kind, double projectFrame)
{
    if (!options.contentIssue)
        return;
    const QString name = entity.name.isEmpty() ? entity.uuid : entity.name;
    options.contentIssue(
        QStringLiteral("%1实体 \"%2\" 在第 %3 帧没有画面")
            .arg(kind, name)
            .arg(QString::number(projectFrame, 'g', 10)));
}

void paintCheckerboard(QPainter& painter, const QRectF& rect,
                       const QColor& base = QColor(64, 64, 64),
                       const QColor& alternate = QColor(96, 96, 96),
                       const QColor& borderColor = QColor(145, 145, 145))
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRect(rect);

    painter.setBrush(alternate);
    const int columns = int(std::ceil(rect.width() / kCheckerCell));
    const int rows = int(std::ceil(rect.height() / kCheckerCell));
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            if ((row + column) % 2 == 0)
                continue;
            const QRectF cell(rect.left() + column * kCheckerCell,
                              rect.top() + row * kCheckerCell, kCheckerCell,
                              kCheckerCell);
            painter.drawRect(cell.intersected(rect));
        }
    }

    QPen border(borderColor);
    border.setWidthF(2.0);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
    painter.restore();
}

bool isAssetMissing(const assets::AssetStore& store,
                    const model::AssetContent& content)
{
    return !store.get(content.hash).has_value();
}

QString assetTypeTag(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("IMAGE");
    case AssetType::Video:
        return QStringLiteral("VIDEO");
    case AssetType::Html:
        return QStringLiteral("HTML");
    case AssetType::Audio:
        return QStringLiteral("AUDIO");
    }
    return QStringLiteral("ASSET");
}

// PR-style offline card: dark checkerboard, 2px red frame, "素材丢失" headline
// and a compact MEDIA OFFLINE line with the short hash and asset type.  The
// caller's transform/opacity are already applied, so this only paints in local
// content coordinates and never adds rotation.
void paintMissingAssetCard(QPainter& painter, const QRectF& rect,
                           const model::AssetContent& content)
{
    const QRectF area = rect.normalized();
    if (area.width() < 1.0 || area.height() < 1.0)
        return;

    paintCheckerboard(painter, area, kMissingCardBase, kMissingCardAlternate,
                      kMissingCardBase);

    painter.save();
    QPen border(kMissingCardBorder);
    border.setWidthF(2.0);
    border.setJoinStyle(Qt::MiterJoin);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area.adjusted(1.0, 1.0, -1.0, -1.0));

    if (area.width() < 28.0 || area.height() < 14.0)
    {
        painter.restore();
        return;
    }

    const bool detailed = area.width() >= 160.0 && area.height() >= 52.0;

    int mainSize = int(std::clamp(area.height() * (detailed ? 0.17 : 0.30),
                                  9.0, 56.0));
    mainSize = std::min(mainSize, int(area.width() / 4.8));
    mainSize = std::max(6, mainSize);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(mainSize);
    painter.setFont(font);
    painter.setPen(QColor(240, 240, 240));

    const QRectF mainRect =
        detailed ? QRectF(area.left(), area.top() + area.height() * 0.14,
                          area.width(), area.height() * 0.44)
                 : area;
    painter.drawText(mainRect, Qt::AlignCenter, QStringLiteral("素材丢失"));

    if (detailed)
    {
        QFont secondary = font;
        secondary.setBold(false);
        const int secondarySize = std::max(
            7, int(std::clamp(area.height() * 0.075, 7.0, 14.0)));
        secondary.setPixelSize(secondarySize);
        painter.setFont(secondary);
        painter.setPen(QColor(168, 168, 174));

        QString detail = QStringLiteral("MEDIA OFFLINE");
        const QString shortHash = content.hash.left(8);
        if (!shortHash.isEmpty())
            detail += QStringLiteral("  ·  ") + shortHash;
        detail += QStringLiteral("  ·  ") + assetTypeTag(content.type);

        const QRectF detailRect(area.left() + 6.0,
                                area.top() + area.height() * 0.62,
                                std::max(1.0, area.width() - 12.0),
                                area.height() * 0.22);
        const QFontMetricsF metrics(secondary);
        const QString elided = metrics.elidedText(detail, Qt::ElideRight,
                                                  detailRect.width());
        painter.drawText(detailRect, Qt::AlignCenter, elided);
    }

    painter.restore();
}

void paintPlaceholder(QPainter& painter, const QRectF& rect,
                      const QString& label)
{
    paintCheckerboard(painter, rect);

    painter.save();
    QFont font = painter.font();
    font.setPixelSize(int(std::clamp(rect.height() * 0.12, 10.0, 56.0)));
    painter.setFont(font);
    painter.setPen(QColor(235, 235, 235));
    painter.drawText(rect, Qt::AlignCenter, label);
    painter.restore();
}

void paintVector(QPainter& painter, const model::VectorContent& vector)
{
    const QRectF rect = centeredRect(QSizeF(vector.width, vector.height));
    const QPainterPath path = VectorShape::path(
        vector.shape, rect, vector.cornerRadius, vector.sides, vector.innerRatio);

    if (vector.shape == 2)
    {
        QColor stroke = colorOr(vector.strokeColor, QColor(0, 0, 0, 0));
        if (stroke.alpha() == 0)
            stroke = colorOr(vector.fillColor, Qt::white);
        QPen pen(stroke);
        pen.setWidthF(std::max(1.0, vector.strokeWidth));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        return;
    }

    const QColor fill = colorOr(vector.fillColor, QColor(0, 0, 0, 0));
    painter.setBrush(fill.alpha() > 0 ? QBrush(fill) : QBrush(Qt::NoBrush));
    if (vector.strokeWidth > 0.0)
    {
        const QColor stroke = colorOr(vector.strokeColor, QColor(0, 0, 0, 0));
        if (stroke.alpha() > 0)
        {
            QPen pen(stroke);
            pen.setWidthF(vector.strokeWidth);
            painter.setPen(pen);
        }
        else
        {
            painter.setPen(Qt::NoPen);
        }
    }
    else
    {
        painter.setPen(Qt::NoPen);
    }
    painter.drawPath(path);
}

void paintText(QPainter& painter, const model::TextContent& text)
{
    if (text.text.isEmpty())
        return;
    const TextBlock block =
        TextBlock::measure(TextStyle::fromData(textStyleData(text)));
    if (block.size().width() < 1.0 || block.size().height() < 1.0)
        return;
    block.paint(painter, QPointF());
}

void paintContent(QPainter& painter, const model::Entity& entity,
                  const assets::AssetStore& store, const Options& options,
                  double projectFrame)
{
    // One whole-frame sample yields every animated content value.
    const model::Content content = animatedContent(entity, projectFrame);
    switch (content.kind)
    {
    case ContentKind::Empty:
    case ContentKind::Camera:
        return;

    case ContentKind::Vector:
        paintVector(painter, content.vector);
        return;

    case ContentKind::Text:
        paintText(painter, content.text);
        return;

    case ContentKind::Asset:
        break;
    }

    const model::AssetContent& asset = entity.content.asset;
    switch (asset.type)
    {
    case AssetType::Audio:
        return;

    case AssetType::Image:
    {
        const Picture picture = loadPicture(store, asset.hash);
        if (picture.isValid())
        {
            const QRectF bounds = centeredRect(picture.size);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, picture.image,
                              QRectF(picture.image.rect()));
        }
        else
        {
            // Missing metadata and unreadable files both count as offline: the
            // entity keeps existing, but there is nothing usable to draw.
            noteContentIssue(options, entity, QStringLiteral("图片"),
                             projectFrame);
            paintMissingAssetCard(painter,
                                  centeredRect(placeholderSize(store, asset)),
                                  asset);
        }
        return;
    }

    case AssetType::Video:
    {
        // Invoke the provider even for offline assets: previews rely on the
        // callback contract and may warm caches, but a stale frame must not
        // mask the missing-asset card.
        const QImage frame = options.videoFrame
                ? options.videoFrame(entity.uuid, projectFrame)
                : QImage();
        if (isAssetMissing(store, asset))
        {
            noteContentIssue(options, entity, QStringLiteral("视频"),
                             projectFrame);
            paintMissingAssetCard(painter, assetBounds(store, asset), asset);
            return;
        }

        // The proxy decoder returns 960 px frames while an export decodes the
        // original size; scaling the texture into the metadata rectangle keeps
        // both compositions identical.
        const QRectF bounds = assetBounds(store, asset);
        if (!frame.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, frame);
        }
        else
        {
            // The asset exists; the provider simply has no decoded frame yet.
            noteContentIssue(options, entity, QStringLiteral("视频"),
                             projectFrame);
            paintPlaceholder(painter, bounds, assetPlaceholderLabel(store, asset));
        }
        return;
    }

    case AssetType::Html:
    {
        const QImage frame = options.htmlFrame
                ? options.htmlFrame(entity.uuid, projectFrame)
                : QImage();
        if (isAssetMissing(store, asset))
        {
            noteContentIssue(options, entity, QStringLiteral("HTML"),
                             projectFrame);
            paintMissingAssetCard(painter, assetBounds(store, asset), asset);
            return;
        }

        const QRectF bounds = assetBounds(store, asset);
        if (!frame.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, frame);
        }
        else
        {
            // Asset present but Chrome has not produced a frame yet.
            noteContentIssue(options, entity, QStringLiteral("HTML"),
                             projectFrame);
            paintPlaceholder(painter, bounds, assetPlaceholderLabel(store, asset));
        }
        return;
    }
    }
}

QSizeF contentSize(const model::Entity& entity, const assets::AssetStore& store,
                   double projectFrame)
{
    const model::Content content = animatedContent(entity, projectFrame);
    switch (content.kind)
    {
    case ContentKind::Vector:
        return QSizeF(std::max(1.0, content.vector.width),
                      std::max(1.0, content.vector.height));

    case ContentKind::Text:
    {
        if (content.text.text.isEmpty())
            return {};
        const TextBlock block = TextBlock::measure(
            TextStyle::fromData(textStyleData(content.text)));
        return block.size();
    }

    case ContentKind::Asset:
    {
        const model::AssetContent& asset = content.asset;
        if (asset.type == AssetType::Audio)
            return {};

        if (asset.type == AssetType::Image)
        {
            const Picture picture = loadPicture(store, asset.hash);
            if (picture.isValid())
                return QSizeF(picture.size);
        }

        return QSizeF(placeholderSize(store, asset));
    }

    case ContentKind::Empty:
    case ContentKind::Camera:
        return {};
    }
    return {};
}

QPainterPath localCollisionPath(const model::Entity& entity,
                                const assets::AssetStore& store,
                                double projectFrame)
{
    const model::Content content = animatedContent(entity, projectFrame);
    if (content.kind == ContentKind::Vector)
    {
        const model::VectorContent& vector = content.vector;
        const QRectF rect = centeredRect(QSizeF(vector.width, vector.height));
        // Vector paths are authored in content-local Y-down space, matching
        // paintContent().  Collision space is model Y-up, so apply the same
        // single content flip here; otherwise an asymmetric shape (arrow,
        // heart, triangle) would collide with its mirrored outline.
        QTransform contentToModel;
        contentToModel.scale(1.0, -1.0);
        return contentToModel.map(VectorShape::path(
            vector.shape, rect, vector.cornerRadius, vector.sides,
            vector.innerRatio));
    }

    if (entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Audio)
        return {};

    const QSizeF size = contentSize(entity, store, projectFrame);
    if (size.width() < 1.0 || size.height() < 1.0)
        return {};

    QPainterPath path;
    path.addRect(centeredRect(size));
    return path;
}

QTransform canvasView(const QSize& output, const QRectF& worldRect)
{
    const double width = std::max(1e-9, worldRect.width());
    const double height = std::max(1e-9, worldRect.height());
    const double scaleX = double(output.width()) / width;
    const double scaleY = double(output.height()) / height;

    // Model space is Y-up, the output image is Y-down.  This is the single
    // world -> screen Y flip; nothing below the renderer may flip again.
    // y' = (worldRect.bottom() - y) * scaleY, so the model top edge
    // (worldRect.bottom(), the larger Y) lands on image row 0.
    QTransform transform;
    transform.setMatrix(scaleX, 0.0, 0.0,
                        0.0, -scaleY, 0.0,
                        -worldRect.left() * scaleX,
                        worldRect.bottom() * scaleY, 1.0);
    return transform;
}

QTransform canvasView(const QSize& output, const model::ProjectSettings& settings)
{
    const double width = double(std::max(1, settings.width));
    const double height = double(std::max(1, settings.height));
    return canvasView(output, QRectF(-width / 2.0, -height / 2.0, width, height));
}

QTransform cameraView(const model::Project& project, const Options& options,
                      anim::World& world, anim::Cache& cache,
                      const QSize& output)
{
    const auto camera = project.entities.constFind(options.cameraUuid);
    if (camera == project.entities.constEnd())
        return canvasView(output, project.settings);

    const anim::Pose pose = anim::evaluateEntity(
        project, options.cameraUuid, options.projectFrame, world, cache);
    const double width = std::max(1, project.settings.width);
    const double height = std::max(1, project.settings.height);

    // Camera-local corners are also Y-up: top-left is (-w/2, +h/2).
    QPolygonF source;
    source << QPointF(-width / 2.0, height / 2.0)
           << QPointF(width / 2.0, height / 2.0)
           << QPointF(width / 2.0, -height / 2.0)
           << QPointF(-width / 2.0, -height / 2.0);
    const QPolygonF quad = pose.world.map(source);

    QPolygonF target;
    target << QPointF(0.0, 0.0)
           << QPointF(output.width(), 0.0)
           << QPointF(output.width(), output.height())
           << QPointF(0.0, output.height());

    // quadToQuad carries the world -> screen Y flip from the camera-local
    // Y-up polygon to the Y-down output polygon.
    QTransform transform;
    if (QTransform::quadToQuad(quad, target, transform))
        return transform;

    // Degenerate camera transform: fall back to the frame's bounding box with
    // the same canvas mapping (including the Y flip).
    return canvasView(output, quad.boundingRect());
}

} // namespace

namespace detail {

bool activeAt(const model::Entity& entity, double projectFrame)
{
    return pvf::render2::activeAt(entity, projectFrame);
}

void paintContent(QPainter& painter, const model::Entity& entity,
                  const assets::AssetStore& store, const Options& options,
                  double projectFrame)
{
    pvf::render2::paintContent(painter, entity, store, options, projectFrame);
}

QTransform canvasView(const QSize& output, const QRectF& worldRect)
{
    return pvf::render2::canvasView(output, worldRect);
}

QTransform cameraView(const model::Project& project, const Options& options,
                      anim::World& world, anim::Cache& cache,
                      const QSize& output)
{
    return pvf::render2::cameraView(project, options, world, cache, output);
}

} // namespace detail

model::Content animatedContent(const model::Entity& entity, double projectFrame)
{
    const model::KeyframeSample sample =
        model::entitySampleAt(entity, projectFrame - double(entity.start));
    model::Content content = entity.content;
    model::applyAnimationValues(sample.content, &content);
    return content;
}

SceneWorld::SceneWorld(const model::Project& project,
                       const assets::AssetStore& assets, anim::Cache* cache)
    : m_project(project)
    , m_assets(assets)
    , m_cache(cache)
{
}

solver::Pose SceneWorld::poseOf(const Uuid& uuid, int projectFrame) const
{
    anim::Cache local;
    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this,
                             m_cache ? *m_cache : local);
    solver::Pose result;
    result.position = pose.world.map(QPointF(0.0, 0.0));
    result.rotation =
        qRadiansToDegrees(std::atan2(pose.world.m12(), pose.world.m11()));
    return result;
}

QPainterPath SceneWorld::shapeOf(const Uuid& uuid, int projectFrame) const
{
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd())
        return {};
    if (!activeAt(it.value(), projectFrame))
        return {};

    const QPainterPath local =
        localCollisionPath(it.value(), m_assets, double(projectFrame));
    if (local.isEmpty())
        return {};

    anim::Cache fresh;
    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this,
                             m_cache ? *m_cache : fresh);
    return pose.world.map(local);
}

bool SceneWorld::physicsInteract(const Uuid& uuid, int projectFrame) const
{
    return anim::physicsInteractAt(m_project, uuid, projectFrame);
}

QVector<Uuid> SceneWorld::physicsEntities(int projectFrame) const
{
    QVector<Uuid> result;
    for (auto it = m_project.entities.constBegin();
         it != m_project.entities.constEnd(); ++it)
    {
        if (!activeAt(it.value(), projectFrame))
            continue;
        if (anim::physicsInteractAt(m_project, it.key(), projectFrame))
            result.append(it.key());
    }
    std::sort(result.begin(), result.end());
    return result;
}

QString assetPlaceholderLabel(const assets::AssetStore& store,
                              const model::AssetContent& content)
{
    if (isAssetMissing(store, content))
        return QStringLiteral("素材丢失");

    switch (content.type)
    {
    case AssetType::Image:
        return loadPicture(store, content.hash).isValid()
                   ? QString()
                   : QStringLiteral("素材丢失");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    case AssetType::Audio:
        return {};
    }
    return {};
}

} // namespace pvf::render2
