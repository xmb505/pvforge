#include "Render.h"

#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "core/VectorShape.h"

#include <QBrush>
#include <QDir>
#include <QFont>
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

QColor colorOr(const QString& value, const QColor& fallback)
{
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

bool activeAt(const model::Entity& entity, int projectFrame)
{
    return projectFrame >= entity.start && projectFrame < entity.end;
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

QString contentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

struct Picture
{
    QImage image;
    QSize size;

    bool isValid() const { return !image.isNull(); }
};

Picture loadPicture(const assets::AssetStore& store, const AssetHash& hash)
{
    const auto stored = store.get(hash);
    if (!stored)
        return {};

    const QString path = contentFile(*stored);
    if (path.isEmpty())
        return {};

    Picture picture;
    picture.image = QImage(path);
    if (picture.image.isNull())
        return {};

    picture.size = metadataSize(*stored);
    if (picture.size.isEmpty())
        picture.size = picture.image.size();
    return picture;
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

void paintCheckerboard(QPainter& painter, const QRectF& rect)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(64, 64, 64));
    painter.drawRect(rect);

    painter.setBrush(QColor(96, 96, 96));
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

    QPen border(QColor(145, 145, 145));
    border.setWidthF(2.0);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
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
                  const assets::AssetStore& store)
{
    switch (entity.content.kind)
    {
    case ContentKind::Empty:
    case ContentKind::Camera:
        return;

    case ContentKind::Vector:
        paintVector(painter, entity.content.vector);
        return;

    case ContentKind::Text:
        paintText(painter, entity.content.text);
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
            paintPlaceholder(painter, centeredRect(placeholderSize(store, asset)),
                             QStringLiteral("素材缺失"));
        }
        return;
    }

    case AssetType::Video:
        paintPlaceholder(painter, centeredRect(placeholderSize(store, asset)),
                         QStringLiteral("视频"));
        return;

    case AssetType::Html:
        paintPlaceholder(painter, centeredRect(placeholderSize(store, asset)),
                         QStringLiteral("HTML"));
        return;
    }
}

QSizeF contentSize(const model::Entity& entity, const assets::AssetStore& store)
{
    switch (entity.content.kind)
    {
    case ContentKind::Vector:
    {
        const model::VectorContent& vector = entity.content.vector;
        return QSizeF(std::max(1.0, vector.width), std::max(1.0, vector.height));
    }

    case ContentKind::Text:
    {
        if (entity.content.text.text.isEmpty())
            return {};
        const TextBlock block = TextBlock::measure(
            TextStyle::fromData(textStyleData(entity.content.text)));
        return block.size();
    }

    case ContentKind::Asset:
    {
        const model::AssetContent& asset = entity.content.asset;
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
                                const assets::AssetStore& store)
{
    if (entity.content.kind == ContentKind::Vector)
    {
        const model::VectorContent& vector = entity.content.vector;
        const QRectF rect = centeredRect(QSizeF(vector.width, vector.height));
        return VectorShape::path(vector.shape, rect, vector.cornerRadius,
                                 vector.sides, vector.innerRatio);
    }

    if (entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Audio)
        return {};

    const QSizeF size = contentSize(entity, store);
    if (size.width() < 1.0 || size.height() < 1.0)
        return {};

    QPainterPath path;
    path.addRect(centeredRect(size));
    return path;
}

QTransform canvasView(const QSize& output, const model::ProjectSettings& settings)
{
    QTransform transform;
    transform.scale(double(output.width()) / std::max(1, settings.width),
                    double(output.height()) / std::max(1, settings.height));
    return transform;
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

    QPolygonF source;
    source << QPointF(-width / 2.0, -height / 2.0)
           << QPointF(width / 2.0, -height / 2.0)
           << QPointF(width / 2.0, height / 2.0)
           << QPointF(-width / 2.0, height / 2.0);
    const QPolygonF quad = pose.world.map(source);

    QPolygonF target;
    target << QPointF(0.0, 0.0)
           << QPointF(output.width(), 0.0)
           << QPointF(output.width(), output.height())
           << QPointF(0.0, output.height());

    QTransform transform;
    if (QTransform::quadToQuad(quad, target, transform))
        return transform;

    // Degenerate camera transform: fall back to the frame's bounding box.
    const QRectF bounds = quad.boundingRect();
    const QRectF viewport(0.0, 0.0, output.width(), output.height());
    transform.translate(viewport.left(), viewport.top());
    transform.scale(viewport.width() / std::max(1.0, bounds.width()),
                    viewport.height() / std::max(1.0, bounds.height()));
    transform.translate(-bounds.left(), -bounds.top());
    return transform;
}

void drawChildren(QPainter& painter, const model::Project& project,
                  const assets::AssetStore& store, const Uuid& parentUuid,
                  int projectFrame, anim::World& world, anim::Cache& cache,
                  QSet<Uuid>& visited)
{
    const QVector<Uuid> children = model::childrenOf(project, parentUuid);
    for (const Uuid& uuid : children)
    {
        if (visited.contains(uuid))
            continue;
        visited.insert(uuid);

        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;

        const model::Entity& entity = it.value();
        if (activeAt(entity, projectFrame))
        {
            const anim::Pose pose = anim::evaluateEntity(
                project, uuid, projectFrame, world, cache);
            if (pose.visible && pose.opacity > 0.0)
            {
                painter.save();
                painter.setOpacity(std::clamp(pose.opacity, 0.0, 1.0));
                painter.setTransform(pose.world, true);
                paintContent(painter, entity, store);
                painter.restore();
            }
        }

        drawChildren(painter, project, store, uuid, projectFrame, world, cache,
                     visited);
    }
}

} // namespace

SceneWorld::SceneWorld(const model::Project& project,
                       const assets::AssetStore& assets)
    : m_project(project)
    , m_assets(assets)
{
}

solver::Pose SceneWorld::poseOf(const Uuid& uuid, int projectFrame) const
{
    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this);
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

    const QPainterPath local = localCollisionPath(it.value(), m_assets);
    if (local.isEmpty())
        return {};

    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this);
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

QImage renderFrame(const model::Project& project,
                   const assets::AssetStore& assets, const Options& options,
                   anim::World& world, anim::Cache& cache)
{
    QSize output = options.outputSize;
    if (output.isEmpty())
        output = QSize(project.settings.width, project.settings.height);
    output = QSize(std::max(1, output.width()), std::max(1, output.height()));

    QColor background = options.background;
    if (!background.isValid())
        background = QColor(project.settings.background);
    if (!background.isValid())
        background = Qt::black;

    QImage image(output, QImage::Format_ARGB32_Premultiplied);
    image.fill(background);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QTransform view =
        options.cameraUuid.isEmpty()
            ? canvasView(output, project.settings)
            : cameraView(project, options, world, cache, output);
    painter.setTransform(view);

    QSet<Uuid> visited;
    drawChildren(painter, project, assets, Uuid(), options.projectFrame, world,
                 cache, visited);
    painter.end();
    return image;
}

} // namespace pvf::render2
