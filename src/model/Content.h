#pragma once

#include "common/Types.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace pvf::model {

struct Transform
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
    double opacity = 1.0;
};

struct TextContent
{
    QString text;
    QString fontFamily;
    int pixelSize = 96;
    QString color = QStringLiteral("#ffffff");
    QString strokeColor = QStringLiteral("#000000");
    double strokeWidth = 0.0;
    int align = 132;
    double lineHeight = 1.0;
    double letterSpacing = 0.0;
    double padding = 6.0;
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool vertical = false;
    bool verticalRtl = true;
};

struct VectorContent
{
    int shape = 0;
    double width = 400.0;
    double height = 300.0;
    double cornerRadius = 40.0;
    int sides = 5;
    double innerRatio = 0.45;
    double strokeWidth = 0.0;
    QString fillColor = QStringLiteral("#ffffff");
    QString strokeColor = QStringLiteral("#000000");
};

struct CameraContent
{
};

struct AssetContent
{
    AssetHash hash;
    AssetType type = AssetType::Image;
    int sourceIn = 0;
    double volume = 1.0;
};

// A comic balloon with text.  The balloon body is an explicit box; the text
// wraps inside it and is drawn centred.  `shape` is one of ellipse, rect,
// cloud or burst.  (tailX, tailY) is the spike tip as a share of the half
// size, so it follows the box when the entity is scaled.
struct ComicTextContent
{
    QString text;
    QString fontFamily;
    int pixelSize = 72;
    QString color = QStringLiteral("#111111");
    QString strokeColor = QStringLiteral("#000000");
    double strokeWidth = 0.0;
    int align = 132; // Qt::AlignHCenter | Qt::AlignVCenter
    double lineHeight = 1.0;
    bool bold = false;
    bool italic = false;
    QString shape = QStringLiteral("ellipse");
    double boxWidth = 480.0;
    double boxHeight = 260.0;
    QString fillColor = QStringLiteral("#ffffff");
    QString bubbleStrokeColor = QStringLiteral("#18181c");
    double bubbleStrokeWidth = 6.0;
    double cornerRadius = 24.0;
    bool tail = true;
    double tailX = 0.0;
    double tailY = 1.35;
    double rotation = 0.0;
    double padding = 24.0;
    int seed = 1;
};

// Focus lines: `count` wedges from a shared centre out to `radius`.  `width`
// is the ray width in pixels at the outer edge; `angle` rotates the pattern.
struct RadialLinesContent
{
    int count = 56;
    double radius = 420.0;
    double innerRatio = 0.05;
    double width = 18.0;
    double angle = 0.0;
    QString color = QStringLiteral("#ffffff");
    int seed = 1;
};

// Speed lines around a quadratic centre curve.  `bend` is the perpendicular
// offset of the curve midpoint in pixels; `phase` shifts the streaks along
// the curve, 0..1.
struct FlowLinesContent
{
    int count = 26;
    double length = 420.0;
    double spacing = 64.0;
    double thickness = 10.0;
    double area = 1600.0;
    double bend = 0.0;
    double angle = 0.0;
    double phase = 0.0;
    QString color = QStringLiteral("#ffffff");
    int seed = 1;
};

// Manga screentone.  `layout` is grid or radial.  Grid: a rotated dot grid
// fading along `gradientAngle`.  Radial: concentric rings fading outward.
struct HalftoneDotsContent
{
    QString layout = QStringLiteral("grid");
    double area = 900.0;
    double radius = 450.0;
    double spacing = 18.0;
    double dotSize = 7.0;
    double angle = 45.0;
    double gradientAngle = 90.0;
    double minRatio = 0.0;
    double jitter = 0.12;
    QString color = QStringLiteral("#ffffff");
    int seed = 1;
};

// Press-conference backdrop: a grid of coloured tiles, each showing one of
// several logo labels.  Tiles are `logoSize` square with `spacing` between
// them; `alternate` offsets the colour/logo slot on every other row
// (checkerboard), otherwise slots run left-to-right then top-to-bottom.
// `background` is an optional colour (empty = transparent).
struct BrandWallContent
{
    QStringList logos;
    int columns = 6;
    int rows = 4;
    double spacing = 8.0;
    double logoSize = 200.0;
    QStringList colors = {
        QStringLiteral("#d246a0"), QStringLiteral("#e8e8ee"),
        QStringLiteral("#4082b4"), QStringLiteral("#e6c878"),
        QStringLiteral("#5aa078"), QStringLiteral("#966eb4"),
    };
    bool alternate = true;
    double angle = 0.0;
    QString background;
    int seed = 1;
};

struct Content
{
    ContentKind kind = ContentKind::Empty;
    TextContent text;
    VectorContent vector;
    CameraContent camera;
    AssetContent asset;
    ComicTextContent comic;
    RadialLinesContent radial;
    FlowLinesContent flow;
    HalftoneDotsContent halftone;
    BrandWallContent brandWall;
};

QJsonObject toJson(const Transform& transform);
std::optional<Transform> transformFromJson(const QJsonObject& object,
                                           QString* error = nullptr);

QJsonObject toJson(const Content& content);
std::optional<Content> contentFromJson(const QJsonObject& object,
                                       QString* error = nullptr);

// Checks the content fields that have range constraints.  Unknown enum names,
// non-finite numbers and out-of-range sizes/counts produce a specific error.
bool validateContent(const Content& content, QString* error = nullptr);

} // namespace pvf::model
