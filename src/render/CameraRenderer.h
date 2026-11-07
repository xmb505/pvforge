#pragma once

#include "core/Camera.h"
#include "core/Project.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QTransform>

class QPainter;

namespace pvf {
namespace CameraRenderer {

// Scene-camera output: black outside the canvas, project background inside,
// entities clipped to the canvas. The target keeps the project aspect.
void renderCamera(QPainter& painter, const Project& project, int frame,
                  const QRectF& target, const Camera::State& state, bool useThumbnails);

QImage renderCameraImage(const Project& project, int frame, const Camera::State& state,
                         const QSize& size, bool useThumbnails);

// Work-camera output: mirrors the main viewport transform without overlays.
void renderWorkCamera(QPainter& painter, const Project& project, int frame,
                      const QRectF& target,
                      const QTransform& viewportWorldToScreen,
                      const QSize& viewportSize, const QColor& outsideColor,
                      bool useThumbnails);

} // namespace CameraRenderer
} // namespace pvf
