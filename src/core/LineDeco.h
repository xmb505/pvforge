#pragma once

#include <QColor>
#include <QRectF>
#include <QVariantMap>

class QPainter;

namespace pvf {

// Concentric rays in the hand drawn "focus lines" style: each ray is a
// wedge that is nearly a point at the centre and wide at the outer edge.
// Every tenth of a second about 60% of the rays are drawn thicker, which
// gives the flickering, drawn by hand look.
struct RadialParams
{
    int count = 56;
    double radius = 420.0;
    double innerRatio = 0.05;   // where a ray starts, as a share of the radius
    double width = 18.0;        // ray width in pixels at the outer edge
    double spin = 0.0;          // whole pattern rotation, degrees per second
    double flicker = 10.0;      // random thickening speed, times per second
    QColor color = Qt::white;
    int seed = 1;

    static RadialParams fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;
};

// Parallel streaks flowing across the scene, used as a motion background.
struct FlowParams
{
    int count = 26;
    double length = 420.0;
    double spacing = 64.0;
    double thickness = 10.0;
    double area = 1600.0;       // size of the covered square
    double speed = 900.0;       // pixels per second
    double angle = 0.0;         // flow direction in degrees
    QColor color = Qt::white;
    int seed = 1;

    static FlowParams fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;
};

namespace LineDeco {

// Stable pseudo random number in [0, 1), same style as the reference art.
double hash1(double n);

void paintRadial(QPainter& painter, const RadialParams& params, double seconds);
void paintFlow(QPainter& painter, const FlowParams& params, double seconds);

QRectF radialBounds(const RadialParams& params);
QRectF flowBounds(const FlowParams& params);

} // namespace LineDeco
} // namespace pvf
