#include "LineDeco.h"

#include <QPainter>
#include <cmath>

namespace pvf {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

RadialParams RadialParams::fromData(const QVariantMap& data)
{
    RadialParams params;
    params.count = data.value(QStringLiteral("count"), 56).toInt();
    params.radius = data.value(QStringLiteral("radius"), 420.0).toDouble();
    params.innerRatio = data.value(QStringLiteral("innerRatio"), 0.05).toDouble();
    params.width = data.value(QStringLiteral("width"), 18.0).toDouble();
    params.spin = data.value(QStringLiteral("spin"), 0.0).toDouble();
    const QColor color = data.value(QStringLiteral("color")).value<QColor>();
    if (color.isValid())
        params.color = color;
    params.seed = data.value(QStringLiteral("seed"), 1).toInt();
    return params;
}

void RadialParams::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("count")] = count;
    data[QStringLiteral("radius")] = radius;
    data[QStringLiteral("innerRatio")] = innerRatio;
    data[QStringLiteral("width")] = width;
    data[QStringLiteral("spin")] = spin;
    data[QStringLiteral("color")] = color;
    data[QStringLiteral("seed")] = seed;
}

FlowParams FlowParams::fromData(const QVariantMap& data)
{
    FlowParams params;
    params.count = data.value(QStringLiteral("count"), 18).toInt();
    params.length = data.value(QStringLiteral("length"), 420.0).toDouble();
    params.spacing = data.value(QStringLiteral("spacing"), 90.0).toDouble();
    params.thickness = data.value(QStringLiteral("thickness"), 10.0).toDouble();
    params.area = data.value(QStringLiteral("area"), 1600.0).toDouble();
    params.speed = data.value(QStringLiteral("speed"), 900.0).toDouble();
    params.angle = data.value(QStringLiteral("angle"), 0.0).toDouble();
    const QColor color = data.value(QStringLiteral("color")).value<QColor>();
    if (color.isValid())
        params.color = color;
    params.seed = data.value(QStringLiteral("seed"), 1).toInt();
    return params;
}

void FlowParams::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("count")] = count;
    data[QStringLiteral("length")] = length;
    data[QStringLiteral("spacing")] = spacing;
    data[QStringLiteral("thickness")] = thickness;
    data[QStringLiteral("area")] = area;
    data[QStringLiteral("speed")] = speed;
    data[QStringLiteral("angle")] = angle;
    data[QStringLiteral("color")] = color;
    data[QStringLiteral("seed")] = seed;
}

namespace LineDeco {

double hash1(double n)
{
    const double value = std::sin(n * 12.9898 + 7.233) * 43758.5453;
    return value - std::floor(value);
}

void paintRadial(QPainter& painter, const RadialParams& params, double seconds)
{
    if (params.count <= 0 || params.radius <= 0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(params.color);

    const double spin = params.spin * kPi / 180.0 * seconds;
    const double outer = params.radius;
    const double inner = outer * std::clamp(params.innerRatio, 0.0, 0.9);
    // The thickness flickers ten times a second, like a hand drawn line that
    // is redrawn over and over.
    const double step = std::floor(seconds * 10.0);

    QPolygonF wedge;
    for (int index = 0; index < params.count; ++index)
    {
        const double base = (index / double(params.count)) * 2.0 * kPi;
        const double wobble = (hash1(index + params.seed * 1000.0) - 0.5) * 0.03;
        const double angle = base + wobble + spin;

        double width = params.width * (0.75 + 0.5 * hash1(index + 100 + params.seed * 1000.0));
        if (hash1(step * 97.0 + index + params.seed * 1000.0) < 0.6)
            width *= 1.4;

        const double halfOuter = width / 2.0 / std::max(1.0, outer);
        const double halfInner = halfOuter * 0.12;
        const auto point = [&](double radius, double offset) {
            return QPointF(std::cos(angle + offset) * radius,
                           std::sin(angle + offset) * radius);
        };
        wedge.clear();
        wedge << point(inner, -halfInner) << point(outer, -halfOuter)
              << point(outer, halfOuter) << point(inner, halfInner);
        painter.drawPolygon(wedge);
    }
    painter.restore();
}

void paintFlow(QPainter& painter, const FlowParams& params, double seconds)
{
    if (params.count <= 0 || params.spacing <= 0)
        return;

    const double angle = params.angle * kPi / 180.0;
    const QPointF direction(std::cos(angle), std::sin(angle));
    const QPointF across(-direction.y(), direction.x());
    const double period = params.area + params.length;
    const double shift = std::fmod(seconds * params.speed, period);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(QRectF(-params.area / 2.0, -params.area / 2.0, params.area,
                               params.area),
                        Qt::IntersectClip);
    QPen pen(params.color);
    pen.setWidthF(std::max(1.0, params.thickness));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    for (int index = 0; index < params.count; ++index)
    {
        const double wobble = (hash1(index + params.seed * 1000.0) - 0.5) * 0.3;
        const double offset =
            (index - (params.count - 1) / 2.0) * params.spacing + wobble * params.spacing;
        const double along =
            std::fmod(index * params.spacing * 1.7 + shift, period) - period / 2.0;
        const QPointF centre = across * offset + direction * along;
        painter.drawLine(centre - direction * (params.length / 2.0),
                         centre + direction * (params.length / 2.0));
    }
    painter.restore();
}

QRectF radialBounds(const RadialParams& params)
{
    const double radius = std::max(1.0, params.radius);
    return QRectF(-radius, -radius, radius * 2.0, radius * 2.0);
}

QRectF flowBounds(const FlowParams& params)
{
    const double area = std::max(1.0, params.area);
    return QRectF(-area / 2.0, -area / 2.0, area, area);
}

} // namespace LineDeco
} // namespace pvf
