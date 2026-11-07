#include "SceneRenderer.h"

#include "core/Camera.h"
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
        QFont font(entity.fontFamily());
        font.setPixelSize(std::max(1, entity.fontSize()));
        const QString text = entity.textContent();
        if (text.isEmpty())
            return QRectF(-10, -10, 20, 20);
        const QFontMetricsF metrics(font);
        const QRectF bounds =
            metrics.boundingRect(QRectF(0, 0, 4000, 4000), Qt::TextWordWrap, text);
        if (bounds.width() < 1 || bounds.height() < 1)
            return QRectF(-10, -10, 20, 20);
        const double pad = 6.0;
        return QRectF(-bounds.width() / 2.0 - pad, -bounds.height() / 2.0 - pad,
                      bounds.width() + pad * 2.0, bounds.height() + pad * 2.0);
    }

    case EntityType::Vector:
    {
        const double width = std::max(1.0, entity.vectorWidth());
        const double height =
            entity.vectorShape() == 2 ? std::max(4.0, entity.strokeWidth())
                                      : std::max(1.0, entity.vectorHeight());
        return QRectF(-width / 2.0, -height / 2.0, width, height);
    }

    case EntityType::Camera:
        return QRectF();
    case EntityType::Audio:
        return QRectF();
    case EntityType::Folder:
        return QRectF();
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

    double opacity =
        std::clamp(project.value(entity.id, QStringLiteral("opacity"), frame), 0.0, 1.0);
    QSet<EntityId> seen;
    EntityId parent = entity.parent;
    while (parent != kInvalidEntity && !seen.contains(parent))
    {
        seen.insert(parent);
        const Entity* parentEntity = project.entity(parent);
        if (!parentEntity)
            break;
        opacity *= std::clamp(
            project.value(parentEntity->id, QStringLiteral("opacity"), frame), 0.0, 1.0);
        parent = parentEntity->parent;
    }
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
        QFont font(entity.fontFamily());
        font.setPixelSize(std::max(1, entity.fontSize()));
        painter.setFont(font);
        painter.setPen(entity.textColor());
        painter.drawText(bounds, entity.textAlign() | Qt::TextWordWrap,
                         entity.textContent());
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
    for (int index : project.entityOrder())
        drawEntity(painter, project, project.entities()[index], frame, worldToScreen,
                   useThumbnail);
}

} // namespace SceneRenderer
} // namespace pvf
