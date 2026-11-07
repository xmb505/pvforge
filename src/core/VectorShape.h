#pragma once

#include <QPainterPath>
#include <QRectF>

namespace pvf {

// Shared vector shape geometry for the viewport, icons and previews.
namespace VectorShape {

// shape: 0 rect, 1 ellipse, 2 line, 3 rounded rect, 4 triangle, 5 heart,
//        6 star, 7 polygon, 8 arrow
QPainterPath path(int shape, const QRectF& rect, double cornerRadius, int sides,
                  double innerRatio);

} // namespace VectorShape

} // namespace pvf
