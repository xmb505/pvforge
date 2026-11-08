#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QPainter;

namespace pvf {

// A press conference backdrop: a grid of coloured tiles, each showing one
// of several brand logos. The logo list cycles over the tiles.
struct BrandWallParams
{
    double width = 1600.0;
    double height = 900.0;
    int columns = 6;               // tiles across, the density
    int rows = 4;                  // tiles down
    double gap = 0.0;              // grout between the tiles
    double logoOpacity = 1.0;
    QStringList logos;             // one image path per tile slot
    QStringList tileColors;        // "#aarrggbb" per tile slot

    static BrandWallParams fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;
};

namespace BrandWall {

QRectF bounds(const BrandWallParams& params);
void paint(QPainter& painter, const BrandWallParams& params,
           const QVector<QImage>& logos);

} // namespace BrandWall
} // namespace pvf
