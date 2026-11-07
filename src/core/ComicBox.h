#pragma once

#include "TextStyle.h"

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QVariantMap>

class QPainter;

namespace pvf {

// Shape and colours of a comic balloon.
struct BubbleStyle
{
    int shape = 0;         // 0 = ellipse, 1 = rectangle, 2 = cloud,
                           // 3 = burst
    bool tail = true;      // the movable spike (never drawn on the burst)
    QColor fill = QColor(255, 255, 255);
    QColor stroke = QColor(24, 24, 28);
    double strokeWidth = 6.0;
    double radius = 0.0;   // 0 = sharp manga style corners
    // The spike tip, as a share of the balloon half size: 0 is the centre,
    // 1 is the edge. The spike always points at it, like a callout shape.
    double tipX = 0.0;
    double tipY = 1.35;
    int seed = 1;          // keeps the hand drawn wobble stable

    static BubbleStyle fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;
};

// A comic balloon: the border path plus the text it holds.
class ComicBox
{
public:
    static ComicBox build(const TextStyle& text, const BubbleStyle& bubble);

    // Symmetric around the entity origin, tail included.
    QRectF bounds() const { return m_bounds; }
    // Local position of the spike tip, used by the drag handle.
    QPointF tip() const { return m_tip; }
    QSizeF bodySize() const { return m_bodySize; }
    void paint(QPainter& painter) const;

private:
    TextStyle m_text;
    BubbleStyle m_bubble;
    QPainterPath m_path;
    QRectF m_bounds;
    QPointF m_tip;
    QSizeF m_bodySize;
};

} // namespace pvf
