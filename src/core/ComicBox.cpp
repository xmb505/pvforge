#include "ComicBox.h"

#include "TextBlock.h"

#include <QPainter>
#include <QVector>
#include <cmath>

namespace pvf {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Stable pseudo random number in [0, 1), same style as the reference art.
double hash1(double n)
{
    const double value = std::sin(n * 12.9898 + 7.233) * 43758.5453;
    return value - std::floor(value);
}

// Superellipse outline, the shape the hand drawn cloud balloon uses.
QVector<QPointF> superEllipse(const QSizeF& size, int points, double exponent)
{
    QVector<QPointF> result;
    const double a = size.width() / 2.0;
    const double b = size.height() / 2.0;
    const double e = 2.0 / exponent;
    for (int index = 0; index < points; ++index)
    {
        const double t = -kPi / 2.0 + index * 2.0 * kPi / points;
        const double c = std::cos(t);
        const double s = std::sin(t);
        result.append(QPointF(a * std::copysign(std::pow(std::fabs(c), e), c),
                               b * std::copysign(std::pow(std::fabs(s), e), s)));
    }
    return result;
}

// Catmull-Rom style smoothing, so the cloud looks drawn instead of cut.
QPainterPath smoothPath(const QVector<QPointF>& points)
{
    QPainterPath path;
    const int count = points.size();
    if (count == 0)
        return path;
    path.moveTo(points.first());
    for (int index = 0; index < count; ++index)
    {
        const QPointF p1 = points[index];
        const QPointF p2 = points[(index + 1) % count];
        const QPointF p0 = points[(index - 1 + count) % count];
        const QPointF p3 = points[(index + 2) % count];
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    path.closeSubpath();
    return path;
}

QPainterPath cloudPath(const QSizeF& size, int seed)
{
    QVector<QPointF> points = superEllipse(size, 56, 2.62);
    const double jitter = std::max(0.8, size.height() / 400.0);
    for (int index = 0; index < points.size(); ++index)
    {
        points[index] += QPointF(
            (hash1(index + seed * 1000.0) - 0.5) * 1.2 * jitter,
            (hash1(index + 100 + seed * 1000.0) - 0.5) * 1.2 * jitter);
    }
    return smoothPath(points);
}

// A spike that points at the tip: its base sits on the body edge in the
// tip direction, so the balloon keeps pointing wherever the handle goes.
QPainterPath spikePath(const QPainterPath& body, const QRectF& rect, bool ellipse,
                        const QPointF& tip, double base)
{
    const QPointF centre = rect.center();
    QPointF direction = tip - centre;
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-6)
        return body;
    direction /= length;

    QPointF edge;
    if (ellipse)
    {
        const double a = rect.width() / 2.0;
        const double b = rect.height() / 2.0;
        const double t = 1.0 / std::sqrt((direction.x() / a) * (direction.x() / a) +
                                         (direction.y() / b) * (direction.y() / b));
        edge = centre + direction * t;
    }
    else
    {
        double distance = 1e9;
        if (std::fabs(direction.x()) > 1e-6)
            distance = std::min(distance, std::fabs((rect.width() / 2.0) / direction.x()));
        if (std::fabs(direction.y()) > 1e-6)
            distance = std::min(distance, std::fabs((rect.height() / 2.0) / direction.y()));
        edge = centre + direction * distance;
    }

    const QPointF tangent(-direction.y(), direction.x());
    QPainterPath spike;
    spike.moveTo(edge + tangent * (base / 2.0));
    spike.lineTo(tip);
    spike.lineTo(edge - tangent * (base / 2.0));
    spike.closeSubpath();
    return body.united(spike);
}

// Spiky oval used for the "explosion" balloon.
QPainterPath burstPath(const QRectF& rect, const BubbleStyle& bubble)
{
    QPainterPath path;
    const int spikes = 14;
    const double a = rect.width() / 2.0 * 1.28;
    const double b = rect.height() / 2.0 * 1.28;
    const QPointF centre = rect.center();
    for (int index = 0; index < spikes * 2; ++index)
    {
        const double t = (index / double(spikes * 2)) * 2.0 * kPi - kPi / 2.0;
        const double factor = (index % 2 == 0) ? 1.0 : 0.78;
        const double wobble = 1.0 + (hash1(index + bubble.seed * 37.0) - 0.5) * 0.08;
        const QPointF point(centre.x() + a * factor * wobble * std::cos(t),
                            centre.y() + b * factor * wobble * std::sin(t));
        if (index == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

} // namespace

BubbleStyle BubbleStyle::fromData(const QVariantMap& data)
{
    BubbleStyle style;
    style.shape = data.value(QStringLiteral("bubbleShape"), 0).toInt();
    style.tail = data.value(QStringLiteral("bubbleTail"), true).toBool();
    const QColor fill = data.value(QStringLiteral("fill")).value<QColor>();
    if (fill.isValid())
        style.fill = fill;
    const QColor stroke = data.value(QStringLiteral("stroke")).value<QColor>();
    if (stroke.isValid())
        style.stroke = stroke;
    style.strokeWidth = data.value(QStringLiteral("strokeWidth"), 6.0).toDouble();
    style.radius = data.value(QStringLiteral("bubbleRadius"), 0.0).toDouble();
    style.tipX = data.value(QStringLiteral("bubbleTipX"), 0.0).toDouble();
    style.tipY = data.value(QStringLiteral("bubbleTipY"), 1.35).toDouble();
    style.seed = data.value(QStringLiteral("bubbleSeed"), 1).toInt();
    return style;
}

void BubbleStyle::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("bubbleShape")] = shape;
    data[QStringLiteral("bubbleTail")] = tail;
    data[QStringLiteral("fill")] = fill;
    data[QStringLiteral("stroke")] = stroke;
    data[QStringLiteral("strokeWidth")] = strokeWidth;
    data[QStringLiteral("bubbleRadius")] = radius;
    data[QStringLiteral("bubbleTipX")] = tipX;
    data[QStringLiteral("bubbleTipY")] = tipY;
    data[QStringLiteral("bubbleSeed")] = seed;
}

ComicBox ComicBox::build(const TextStyle& text, const BubbleStyle& bubble)
{
    ComicBox box;
    box.m_text = text;
    box.m_bubble = bubble;

    const TextBlock block = TextBlock::measure(text);
    const QSizeF inner = block.contentSize() +
                         QSizeF(text.padding * 2.0, text.padding * 2.0);
    const double margin = bubble.strokeWidth + 4.0;
    const QRectF rect(-inner.width() / 2.0 - margin, -inner.height() / 2.0 - margin,
                       inner.width() + margin * 2.0, inner.height() + margin * 2.0);

    QPainterPath path;
    switch (bubble.shape)
    {
    case 1:
        path.addRoundedRect(rect, bubble.radius, bubble.radius);
        break;
    case 2:
        path = cloudPath(rect.size(), bubble.seed);
        break;
    case 3:
        path = burstPath(QRectF(-inner.width() / 2.0, -inner.height() / 2.0,
                                inner.width(), inner.height()),
                         bubble);
        break;
    default:
        path.addEllipse(rect);
        break;
    }

    // The tip position is a share of the half size, so the spike follows the
    // balloon when the entity is scaled.
    box.m_bodySize = rect.size();
    const QPointF tip(bubble.tipX * rect.width() / 2.0,
                      bubble.tipY * rect.height() / 2.0);
    box.m_tip = tip;
    const bool inside = std::fabs(bubble.tipX) < 0.55 && std::fabs(bubble.tipY) < 0.55;
    // The spike is optional; the manga rectangle and the explosion keep it
    // off by default.
    if (bubble.tail && bubble.shape != 3 && !inside)
    {
        const double base = std::min(rect.width(), rect.height()) * 0.34;
        path = spikePath(path, rect, bubble.shape != 1, tip, base);
    }
    box.m_path = path;

    const QRectF pathBounds = path.boundingRect();
    const double halfWidth =
        std::max(std::abs(pathBounds.left()), std::abs(pathBounds.right()));
    const double halfHeight =
        std::max(std::abs(pathBounds.top()), std::abs(pathBounds.bottom()));
    box.m_bounds = QRectF(-halfWidth, -halfHeight, halfWidth * 2.0, halfHeight * 2.0);
    return box;
}

void ComicBox::paint(QPainter& painter) const
{
    if (m_path.isEmpty())
        return;

    painter.save();
    painter.setBrush(m_bubble.fill);
    if (m_bubble.strokeWidth > 0)
    {
        QPen pen(m_bubble.stroke);
        pen.setWidthF(m_bubble.strokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
    }
    else
    {
        painter.setPen(Qt::NoPen);
    }
    painter.drawPath(m_path);
    painter.restore();

    TextBlock::measure(m_text).paint(painter, QPointF());
}

} // namespace pvf
