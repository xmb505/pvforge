#include "BrandWall.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

// Fallback tile colours when a slot has no explicit colour.
const QColor kPalette[] = {
    QColor(210, 70, 160),   // magenta
    QColor(232, 232, 238),  // light grey
    QColor(64, 130, 180),   // blue
    QColor(230, 200, 120),  // sand
    QColor(90, 160, 120),   // green
    QColor(150, 110, 180),  // violet
};

QColor slotColor(const BrandWallParams& params, int index)
{
    if (index >= 0 && index < params.tileColors.size())
    {
        const QColor color(params.tileColors[index]);
        if (color.isValid())
            return color;
    }
    const int count = int(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[index % count];
}

} // namespace

BrandWallParams BrandWallParams::fromData(const QVariantMap& data)
{
    BrandWallParams params;
    params.width = std::max(16.0, data.value(QStringLiteral("width"), 1600.0).toDouble());
    params.height = std::max(16.0, data.value(QStringLiteral("height"), 900.0).toDouble());
    params.columns = std::clamp(data.value(QStringLiteral("columns"), 6).toInt(), 1, 64);
    params.rows = std::clamp(data.value(QStringLiteral("rows"), 4).toInt(), 1, 64);
    params.gap = std::clamp(data.value(QStringLiteral("gap"), 0.0).toDouble(), 0.0, 64.0);
    params.logoOpacity =
        std::clamp(data.value(QStringLiteral("logoOpacity"), 1.0).toDouble(), 0.0, 1.0);
    params.logos = data.value(QStringLiteral("logos")).toStringList();
    params.tileColors = data.value(QStringLiteral("tileColors")).toStringList();
    return params;
}

void BrandWallParams::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("width")] = width;
    data[QStringLiteral("height")] = height;
    data[QStringLiteral("columns")] = columns;
    data[QStringLiteral("rows")] = rows;
    data[QStringLiteral("gap")] = gap;
    data[QStringLiteral("logoOpacity")] = logoOpacity;
    data[QStringLiteral("logos")] = logos;
    data[QStringLiteral("tileColors")] = tileColors;
}

namespace BrandWall {

QRectF bounds(const BrandWallParams& params)
{
    return QRectF(-params.width / 2.0, -params.height / 2.0, params.width,
                  params.height);
}

void paint(QPainter& painter, const BrandWallParams& params,
           const QVector<QImage>& logos)
{
    const QRectF rect = bounds(params);
    const int columns = std::max(1, params.columns);
    const int rows = std::max(1, params.rows);
    const double cellWidth = params.width / columns;
    const double cellHeight = params.height / rows;
    const int slotCount = std::max(1, int(params.logos.size()));
    const double inset = params.gap / 2.0;

    painter.save();
    painter.setClipRect(rect);
    const double baseOpacity = painter.opacity();
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            QRectF tileRect(rect.left() + column * cellWidth,
                            rect.top() + row * cellHeight, cellWidth, cellHeight);
            tileRect = tileRect.adjusted(inset, inset, -inset, -inset);
            // Checkerboard: every row is offset by one slot.
            const int slot = (row + column) % slotCount;
            painter.fillRect(tileRect, slotColor(params, slot));

            const QImage& logo = slot < logos.size() ? logos[slot] : QImage();
            if (logo.isNull())
                continue;
            const QSizeF available = tileRect.size() * 0.72;
            const QSizeF scaled = QSizeF(logo.size()).scaled(available,
                                                             Qt::KeepAspectRatio);
            const QRectF target(tileRect.center().x() - scaled.width() / 2.0,
                                tileRect.center().y() - scaled.height() / 2.0,
                                scaled.width(), scaled.height());
            painter.setOpacity(baseOpacity * params.logoOpacity);
            painter.drawImage(target, logo);
            painter.setOpacity(baseOpacity);
        }
    }
    painter.restore();
}

} // namespace BrandWall
} // namespace pvf
