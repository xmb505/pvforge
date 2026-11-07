#pragma once

#include "core/Project.h"

#include <QRectF>
#include <QTransform>

class QPainter;

namespace pvf {
namespace SceneRenderer {

// Local entity coordinates (centred at zero) to project coordinates.
QTransform entityTransform(const Project& project, const Entity& entity, int frame);

QRectF entityLocalBounds(const Entity& entity);

// Draws the entity into a painter already clipped by the caller. The
// worldToScreen transform maps project coordinates to target pixels.
void drawEntity(QPainter& painter, const Project& project, const Entity& entity, int frame,
                const QTransform& worldToScreen, bool useThumbnail);

void drawEntities(QPainter& painter, const Project& project, int frame,
                  const QTransform& worldToScreen, bool useThumbnail);

// Downscaled cache used by draft previews. Safe to call with a const entity.
const QImage& thumbnail(const Entity& entity);

} // namespace SceneRenderer
} // namespace pvf
