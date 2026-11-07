#pragma once

#include "core/Project.h"

#include <QPointF>
#include <QPolygonF>
#include <QSizeF>
#include <QTransform>

namespace pvf {
namespace Camera {

// A 2D camera is a rotated rectangle inside the project canvas. Its values
// reuse the normal entity tracks: x/y is the frame centre and scaleX/scaleY
// is the fraction of the project canvas (1.0 = the whole canvas).
constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 1.0;

struct State
{
    double scale = 1.0;
    double rotation = 0.0;
    QPointF center;
    QSizeF size;
};

State state(const Project& project, EntityId id, int frame);

// Clamps the centre so the unrotated frame stays inside the canvas. Rotation
// may make the corners poke out; those areas render black.
QPointF clampCenter(const Project& project, double scale, const QPointF& center);

QTransform frameToProject(const State& state);   // local centred rect -> project
QPolygonF polygon(const State& state);           // frame corners in project coords

} // namespace Camera
} // namespace pvf
