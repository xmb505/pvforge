#include "SceneRenderer.h"

#include "core/Camera.h"
#include "core/BrandWall.h"
#include "core/ComicBox.h"
#include "core/LineDeco.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "core/VectorShape.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace pvf {
namespace SceneRenderer {

namespace {
constexpr int kThumbnailEdge = 512;
}

QTransform entityTransform(const Project& project, const Entity& entity, int frame)
{
    const double x = project.value(entity.id, QStringLiteral("x"), frame);
    const double y = project.value(entity.id, QStringLiteral("y"), frame);
    const double rotation = project.value(entity.id, QStringLiteral("rotation"), frame);
    const double ax = project.value(entity.id, QStringLiteral("rotPointX"), frame);
    const double ay = project.value(entity.id, QStringLiteral("rotPointY"), frame);
    const double scaleX = project.value(entity.id, QStringLiteral("scaleX"), frame);
    const double scaleY = project.value(entity.id, QStringLiteral("scaleY"), frame);

    QTransform transform;
    transform.translate(x, y);
    transform.rotate(rotation);
    transform.translate(-ax, -ay);
    transform.scale(scaleX, scaleY);
    return transform;
}

QTransform worldTransform(const Project& project, const Entity& entity, int frame)
{
    // Qt composes A * B as "apply A, then B"; the child goes first, then the
    // parent chain.
    return entityTransform(project, entity, frame)
           * parentWorldTransform(project, entity, frame);
}

QTransform parentWorldTransform(const Project& project, const Entity& entity, int frame)
{
    QTransform result;
    QSet<EntityId> seen;
    EntityId parent = entity.parent;
    while (parent != kInvalidEntity && !seen.contains(parent))
    {
        seen.insert(parent);
        const Entity* parentEntity = project.entity(parent);
        if (!parentEntity)
            break;
        QTransform parentTransform;
        if (parentEntity->isCamera())
        {
            // Camera-parented overlays live in camera-frame space. The camera
            // view cancels this transform, so a logo stays at the same screen
            // position and size while the camera moves/zooms/rotates.
            const Camera::State state =
                Camera::state(project, parentEntity->id, frame);
            parentTransform.translate(state.center.x(), state.center.y());
            parentTransform.rotate(state.rotation);
            parentTransform.scale(state.scale, state.scale);
        }
        else
        {
            parentTransform = entityTransform(project, *parentEntity, frame);
        }
        result = result * parentTransform;
        if (parentEntity->isCamera())
            break;   // Camera::state already includes the camera's ancestors.
        parent = parentEntity->parent;
    }
    return result;
}

QRectF entityLocalBounds(const Entity& entity)
{
    switch (entity.type)
    {
    case EntityType::Image:
        if (!entity.image.isNull())
            return QRectF(-entity.image.width() / 2.0, -entity.image.height() / 2.0,
                          entity.image.width(), entity.image.height());
        return QRectF(-40, -40, 80, 80);

    case EntityType::Video:
    {
        // The runtime image may be a low-resolution proxy; layout must keep
        // the original video dimensions or the entity would shrink.
        const int width =
            entity.data.value(QStringLiteral("sourceWidth"), entity.image.width())
                .toInt();
        const int height =
            entity.data.value(QStringLiteral("sourceHeight"), entity.image.height())
                .toInt();
        if (width > 0 && height > 0)
            return QRectF(-width / 2.0, -height / 2.0, width, height);
        return QRectF(-40, -40, 80, 80);
    }

    case EntityType::Text:
    {
        const TextBlock block = TextBlock::measure(TextStyle::fromData(entity.data));
        const QSizeF size = block.size();
        if (size.width() < 1 || size.height() < 1)
            return QRectF(-10, -10, 20, 20);
        return QRectF(-size.width() / 2.0, -size.height() / 2.0, size.width(),
                      size.height());
    }

    case EntityType::Vector:
    {
        const double width = std::max(1.0, entity.vectorWidth());
        const double height =
            entity.vectorShape() == 2 ? std::max(4.0, entity.strokeWidth())
                                      : std::max(1.0, entity.vectorHeight());
        return QRectF(-width / 2.0, -height / 2.0, width, height);
    }

    case EntityType::ComicText:
        return ComicBox::build(TextStyle::fromData(entity.data),
                               BubbleStyle::fromData(entity.data))
            .bounds();

    case EntityType::RadialLines:
        return LineDeco::radialBounds(RadialParams::fromData(entity.data));

    case EntityType::FlowLines:
        return LineDeco::flowBounds(FlowParams::fromData(entity.data));

    case EntityType::BrandWall:
        return BrandWall::bounds(BrandWallParams::fromData(entity.data));

    case EntityType::HalftoneDots:
        return LineDeco::halftoneBounds(HalftoneParams::fromData(entity.data));

    case EntityType::Camera:
        return QRectF();
    case EntityType::Audio:
        return QRectF();
    case EntityType::Folder:
        // Folders have no artwork; a fixed box gives the viewport a gizmo to
        // move / rotate / scale the group while its children follow.
        return QRectF(-80, -80, 160, 160);
    }
    return QRectF();
}

const QImage& thumbnail(const Entity& entity)
{
    if (entity.thumbnail.isNull() && !entity.image.isNull())
    {
        entity.thumbnail =
            entity.image.scaled(kThumbnailEdge, kThumbnailEdge, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
    }
    return entity.thumbnail;
}

void drawEntity(QPainter& painter, const Project& project, const Entity& entity, int frame,
                const QTransform& worldToScreen, bool useThumbnail)
{
    if (entity.isFolder() || entity.isCamera() || entity.isAudio()
        || project.isEffectivelyHidden(entity.id))
        return;

    const double opacity = project.effectiveOpacity(entity.id, frame);
    if (opacity <= 0.0)
        return;

    const QRectF bounds = entityLocalBounds(entity);
    if (bounds.isEmpty())
        return;

    const QTransform transform = worldTransform(project, entity, frame) * worldToScreen;

    painter.save();
    painter.setOpacity(opacity);
    painter.setTransform(transform);

    switch (entity.type)
    {
    case EntityType::Image:
    case EntityType::Video:
        if (!entity.image.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            if (useThumbnail)
            {
                const QImage& image = thumbnail(entity);
                if (!image.isNull())
                    painter.drawImage(bounds, image, QRectF(image.rect()));
            }
            else
            {
                if (entity.pixmap.isNull())
                    entity.pixmap = QPixmap::fromImage(entity.image);
                painter.drawPixmap(bounds, entity.pixmap, QRectF(entity.pixmap.rect()));
            }
        }
        break;

    case EntityType::Text:
    {
        TextBlock::measure(TextStyle::fromData(entity.data)).paint(painter, QPointF());
        break;
    }

    case EntityType::Vector:
    {
        const double w = entity.vectorWidth();
        const double h = entity.vectorHeight();
        const double stroke = entity.strokeWidth();
        const QRectF rect(-w / 2.0, -h / 2.0, w, h);
        const QPainterPath path =
            VectorShape::path(entity.vectorShape(), rect, entity.cornerRadius(),
                              entity.vectorSides(), entity.vectorInnerRatio());

        if (entity.vectorShape() == 2)
        {
            QPen pen(entity.strokeColor().alpha() > 0 ? entity.strokeColor()
                                                      : entity.fillColor());
            pen.setWidthF(std::max(1.0, stroke));
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
        }
        else
        {
            QBrush brush(entity.fillColor());
            if (entity.fillColor().alpha() == 0)
                brush = Qt::NoBrush;
            painter.setBrush(brush);
            if (stroke > 0)
            {
                QPen pen(entity.strokeColor());
                pen.setWidthF(stroke);
                painter.setPen(pen);
            }
            else
            {
                painter.setPen(Qt::NoPen);
            }
        }
        painter.drawPath(path);
        break;
    }

    case EntityType::ComicText:
        ComicBox::build(TextStyle::fromData(entity.data),
                        BubbleStyle::fromData(entity.data))
            .paint(painter);
        break;

    case EntityType::RadialLines:
        LineDeco::paintRadial(painter, RadialParams::fromData(entity.data),
                              frame / double(std::max(1, project.fps)));
        break;

    case EntityType::FlowLines:
        LineDeco::paintFlow(painter, FlowParams::fromData(entity.data),
                            frame / double(std::max(1, project.fps)));
        break;

    case EntityType::BrandWall:
        BrandWall::paint(painter, BrandWallParams::fromData(entity.data),
                         entity.wallLogos);
        break;

    case EntityType::HalftoneDots:
    {
        const HalftoneParams params = HalftoneParams::fromData(entity.data);
        // The dot pattern never animates; rasterise it once and reuse it as
        // a bitmap until a parameter changes.
        if (entity.halftoneImage.isNull() || entity.halftoneData != entity.data)
        {
            const int edge = int(std::clamp(params.area, 64.0, 2048.0));
            entity.halftoneImage = LineDeco::renderHalftone(params, edge);
            entity.halftoneData = entity.data;
        }
        if (!entity.halftoneImage.isNull())
        {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(LineDeco::halftoneBounds(params),
                              entity.halftoneImage);
        }
        break;
    }

    case EntityType::Camera:
        break;
    case EntityType::Audio:
        break;
    case EntityType::Folder:
        break;
    }

    painter.restore();
}

void drawEntities(QPainter& painter, const Project& project, int frame,
                  const QTransform& worldToScreen, bool useThumbnail)
{
    // The layer list runs top to bottom: draw the last (bottom) row first so
    // the first (top) row ends up in front.
    const QVector<int> order = project.entityOrder();
    for (int index = int(order.size()) - 1; index >= 0; --index)
        drawEntity(painter, project, project.entities()[order[index]], frame,
                   worldToScreen, useThumbnail);
}

} // namespace SceneRenderer
} // namespace pvf
