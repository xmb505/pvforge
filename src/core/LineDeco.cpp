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
    params.flicker = data.value(QStringLiteral("flicker"), 10.0).toDouble();
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
    data[QStringLiteral("flicker")] = flicker;
    data[QStringLiteral("color")] = color;
    data[QStringLiteral("seed")] = seed;
}

FlowParams FlowParams::fromData(const QVariantMap& data)
{
    FlowParams params;
    params.count = data.value(QStringLiteral("count"), 26).toInt();
    params.length = data.value(QStringLiteral("length"), 420.0).toDouble();
    params.spacing = data.value(QStringLiteral("spacing"), 64.0).toDouble();
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

HalftoneParams HalftoneParams::fromData(const QVariantMap& data)
{
    HalftoneParams params;
    params.area = data.value(QStringLiteral("area"), 900.0).toDouble();
    params.spacing = data.value(QStringLiteral("spacing"), 18.0).toDouble();
    params.dotSize = data.value(QStringLiteral("dotSize"), 7.0).toDouble();
    params.angle = data.value(QStringLiteral("angle"), 45.0).toDouble();
    params.gradientAngle = data.value(QStringLiteral("gradientAngle"), 90.0).toDouble();
    params.minRatio = data.value(QStringLiteral("minRatio"), 0.0).toDouble();
    params.jitter = data.value(QStringLiteral("jitter"), 0.12).toDouble();
    const QColor color = data.value(QStringLiteral("color")).value<QColor>();
    if (color.isValid())
        params.color = color;
    params.seed = data.value(QStringLiteral("seed"), 1).toInt();
    return params;
}

void HalftoneParams::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("area")] = area;
    data[QStringLiteral("spacing")] = spacing;
    data[QStringLiteral("dotSize")] = dotSize;
    data[QStringLiteral("angle")] = angle;
    data[QStringLiteral("gradientAngle")] = gradientAngle;
    data[QStringLiteral("minRatio")] = minRatio;
    data[QStringLiteral("jitter")] = jitter;
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
    const double step = std::floor(seconds * std::max(0.0, params.flicker));

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

void paintHalftone(QPainter& painter, const HalftoneParams& params)
{
    if (params.spacing <= 0 || params.dotSize <= 0 || params.area <= 0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(params.color);

    const double half = params.area / 2.0;
    const QRectF clip(-half, -half, params.area, params.area);
    painter.setClipRect(clip, Qt::IntersectClip);

    const double gridAngle = params.angle * kPi / 180.0;
    const double fadeAngle = params.gradientAngle * kPi / 180.0;
    const QPointF fade(std::cos(fadeAngle), std::sin(fadeAngle));
    const double cosA = std::cos(gridAngle);
    const double sinA = std::sin(gridAngle);

    // A rotated grid needs to reach past the square's corners.
    const int count = int(std::ceil(half * 1.5 / params.spacing)) + 1;
    const double reach = std::max(1.0, params.area);
    const double minRatio = std::clamp(params.minRatio, 0.0, 1.0);
    const double jitter = std::clamp(params.jitter, 0.0, 1.0);

    for (int row = -count; row <= count; ++row)
    {
        for (int column = -count; column <= count; ++column)
        {
            const double baseX = column * params.spacing;
            const double baseY = row * params.spacing;
            const double wobbleX =
                (hash1(column * 11.3 + row * 3.7 + params.seed * 4000.0) - 0.5)
                * params.spacing * jitter * 0.6;
            const double wobbleY =
                (hash1(column * 7.1 + row * 13.9 + params.seed * 5000.0) - 0.5)
                * params.spacing * jitter * 0.6;
            const double gridX = baseX + wobbleX;
            const double gridY = baseY + wobbleY;
            const QPointF pos(gridX * cosA - gridY * sinA,
                              gridX * sinA + gridY * cosA);
            if (pos.x() < -half - params.dotSize
                || pos.x() > half + params.dotSize
                || pos.y() < -half - params.dotSize
                || pos.y() > half + params.dotSize)
                continue;

            // Dots are full sized at one edge and shrink toward the other.
            const double projection = pos.x() * fade.x() + pos.y() * fade.y();
            const double strength =
                std::clamp(0.5 - projection / reach, 0.0, 1.0);
            const double ratio = minRatio + (1.0 - minRatio) * strength;
            if (ratio <= 0.001)
                continue;

            double radius = params.dotSize * ratio;
            radius *= 1.0
                      + (hash1(column * 5.3 + row * 9.7 + params.seed * 3000.0) - 0.5)
                            * jitter;
            if (radius <= 0.2)
                continue;
            painter.drawEllipse(pos, radius, radius);
        }
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

QRectF halftoneBounds(const HalftoneParams& params)
{
    const double area = std::max(1.0, params.area);
    return QRectF(-area / 2.0, -area / 2.0, area, area);
}

} // namespace LineDeco
} // namespace pvf
