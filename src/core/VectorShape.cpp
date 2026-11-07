#include "VectorShape.h"

#include <algorithm>
#include <cmath>

namespace pvf {
namespace VectorShape {

namespace {

QPainterPath heart(const QRectF& r)
{
    const double w = r.width();
    const double h = r.height();
    const double cx = r.center().x();
    const double cy = r.center().y();

    QPainterPath path;
    path.moveTo(cx, cy + h * 0.35);
    path.cubicTo(cx - w * 0.5, cy + h * 0.05, cx - w * 0.45, cy - h * 0.42, cx,
                 cy - h * 0.18);
    path.cubicTo(cx + w * 0.45, cy - h * 0.42, cx + w * 0.5, cy + h * 0.05, cx,
                 cy + h * 0.35);
    path.closeSubpath();
    return path;
}

QPainterPath star(const QRectF& r, int points, double innerRatio)
{
    const int count = std::max(3, points);
    const double ratio = std::clamp(innerRatio, 0.05, 1.0);
    const double rx = r.width() / 2.0;
    const double ry = r.height() / 2.0;
    const QPointF center = r.center();

    QPainterPath path;
    for (int i = 0; i < count * 2; ++i)
    {
        const double angle = -M_PI / 2.0 + i * M_PI / count;
        const double radius = (i % 2 == 0) ? 1.0 : ratio;
        const QPointF p(center.x() + std::cos(angle) * rx * radius,
                        center.y() + std::sin(angle) * ry * radius);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

QPainterPath polygon(const QRectF& r, int sides)
{
    const int count = std::max(3, sides);
    const double rx = r.width() / 2.0;
    const double ry = r.height() / 2.0;
    const QPointF center = r.center();

    QPainterPath path;
    for (int i = 0; i < count; ++i)
    {
        const double angle = -M_PI / 2.0 + i * 2.0 * M_PI / count;
        const QPointF p(center.x() + std::cos(angle) * rx,
                        center.y() + std::sin(angle) * ry);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

QPainterPath arrow(const QRectF& r)
{
    const double w = r.width();
    const double h = r.height();
    const double cx = r.center().x();
    const double cy = r.center().y();
    const double shaft = h * 0.36;

    QPainterPath path;
    path.moveTo(cx - w * 0.5, cy - shaft / 2);
    path.lineTo(cx + w * 0.1, cy - shaft / 2);
    path.lineTo(cx + w * 0.1, cy - h * 0.5);
    path.lineTo(cx + w * 0.5, cy);
    path.lineTo(cx + w * 0.1, cy + h * 0.5);
    path.lineTo(cx + w * 0.1, cy + shaft / 2);
    path.lineTo(cx - w * 0.5, cy + shaft / 2);
    path.closeSubpath();
    return path;
}

} // namespace

QPainterPath path(int shape, const QRectF& rect, double cornerRadius, int sides,
                  double innerRatio)
{
    QPainterPath result;
    switch (shape)
    {
    case 0:
        result.addRect(rect);
        break;
    case 1:
        result.addEllipse(rect);
        break;
    case 2:
        result.moveTo(rect.left(), rect.center().y());
        result.lineTo(rect.right(), rect.center().y());
        break;
    case 3:
        result.addRoundedRect(rect, cornerRadius, cornerRadius);
        break;
    case 4:
    {
        QPolygonF triangle;
        triangle << QPointF(rect.center().x(), rect.top()) << rect.bottomRight()
                 << rect.bottomLeft();
        result.addPolygon(triangle);
        result.closeSubpath();
        break;
    }
    case 5:
        result = heart(rect);
        break;
    case 6:
        result = star(rect, sides, innerRatio);
        break;
    case 7:
        result = polygon(rect, sides);
        break;
    case 8:
        result = arrow(rect);
        break;
    default:
        result.addRect(rect);
        break;
    }
    return result;
}

} // namespace VectorShape
} // namespace pvf
