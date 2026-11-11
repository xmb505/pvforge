#include "Render.h"

#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "core/VectorShape.h"

#include <QBrush>
#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QTransform>
#include <QVariantMap>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pvf::render2 {
namespace {

constexpr double kCheckerCell = 16.0;
constexpr int kDefaultVisualWidth = 320;
constexpr int kDefaultVisualHeight = 180;
constexpr int kDefaultHtmlWidth = 800;
constexpr int kDefaultHtmlHeight = 120;

const QColor kMissingCardBorder(0xc0, 0x39, 0x2b);
const QColor kMissingCardBase(26, 26, 30);
const QColor kMissingCardAlternate(38, 38, 44);

QColor colorOr(const QString& value, const QColor& fallback)
{
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

bool activeAt(const model::Entity& entity, double projectFrame)
{
    return projectFrame >= double(entity.start)
           && projectFrame < double(entity.end);
}

// Value of a numeric content track at the entity-relative frame, or fallback
// when the track is missing or has no keys yet.
double contentTrackValue(const model::Entity& entity, const char* name,
                         double relativeFrame, double fallback)
{
    const auto it = entity.tracks.constFind(QLatin1String(name));
    if (it == entity.tracks.constEnd())
        return fallback;
    return model::evaluateTrack(it.value(), relativeFrame).value_or(fallback);
}

model::TextContent animatedText(const model::Entity& entity, double relativeFrame)
{
    model::TextContent text = entity.content.text;
    text.pixelSize = std::max(
        1, qRound(contentTrackValue(entity, "fontSize", relativeFrame,
                                    double(text.pixelSize))));
    text.lineHeight = std::max(
        0.01, contentTrackValue(entity, "lineHeight", relativeFrame,
                                text.lineHeight));
    text.letterSpacing = contentTrackValue(entity, "letterSpacing", relativeFrame,
                                           text.letterSpacing);
    text.padding =
        std::max(0.0, contentTrackValue(entity, "padding", relativeFrame,
                                        text.padding));
    text.boxWidth =
        std::max(0.0, contentTrackValue(entity, "boxWidth", relativeFrame,
                                        text.boxWidth));
    text.boxHeight =
        std::max(0.0, contentTrackValue(entity, "boxHeight", relativeFrame,
                                        text.boxHeight));
    text.strokeWidth =
        std::max(0.0, contentTrackValue(entity, "strokeWidth", relativeFrame,
                                        text.strokeWidth));
    return text;
}

model::VectorContent animatedVector(const model::Entity& entity,
                                    double relativeFrame)
{
    model::VectorContent vector = entity.content.vector;
    vector.width = std::max(1.0, contentTrackValue(entity, "width", relativeFrame,
                                                   vector.width));
    vector.height =
        std::max(1.0, contentTrackValue(entity, "height", relativeFrame,
                                        vector.height));
    vector.cornerRadius =
        std::max(0.0, contentTrackValue(entity, "cornerRadius", relativeFrame,
                                        vector.cornerRadius));
    vector.strokeWidth =
        std::max(0.0, contentTrackValue(entity, "strokeWidth", relativeFrame,
                                        vector.strokeWidth));
    return vector;
}

QVariantMap textStyleData(const model::TextContent& text)
{
    QVariantMap data;
    data.insert(QStringLiteral("text"), text.text);
    data.insert(QStringLiteral("font"), text.fontFamily);
    data.insert(QStringLiteral("size"), text.pixelSize);
    data.insert(QStringLiteral("color"), colorOr(text.color, Qt::white));
    data.insert(QStringLiteral("textStrokeColor"),
                colorOr(text.strokeColor, Qt::black));
    data.insert(QStringLiteral("textStrokeWidth"), text.strokeWidth);
    data.insert(QStringLiteral("align"), text.align);
    data.insert(QStringLiteral("lineHeight"), text.lineHeight);
    data.insert(QStringLiteral("letterSpacing"), text.letterSpacing);
    data.insert(QStringLiteral("padding"), text.padding);
    data.insert(QStringLiteral("boxWidth"), text.boxWidth);
    data.insert(QStringLiteral("boxHeight"), text.boxHeight);
    data.insert(QStringLiteral("bold"), text.bold);
    data.insert(QStringLiteral("italic"), text.italic);
    data.insert(QStringLiteral("underline"), text.underline);
    data.insert(QStringLiteral("vertical"), text.vertical);
    data.insert(QStringLiteral("verticalRtl"), text.verticalRtl);
    return data;
}

QSize metadataSize(const assets::StoredAsset& asset)
{
    const int width = asset.metadata.value(QStringLiteral("width")).toInt();
    const int height = asset.metadata.value(QStringLiteral("height")).toInt();
    if (width > 0 && height > 0)
        return QSize(width, height);
    return {};
}

QString contentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

struct Picture
{
    QImage image;
    QSize size;

    bool isValid() const { return !image.isNull(); }
};

Picture loadPicture(const assets::AssetStore& store, const AssetHash& hash)
{
    const auto stored = store.get(hash);
    if (!stored)
        return {};

    const QString path = contentFile(*stored);
    if (path.isEmpty())
        return {};

    Picture picture;
    picture.image = QImage(path);
    if (picture.image.isNull())
        return {};

    picture.size = metadataSize(*stored);
    if (picture.size.isEmpty())
        picture.size = picture.image.size();
    return picture;
}

QSize placeholderSize(const assets::AssetStore& store,
                      const model::AssetContent& content)
{
    if (const auto stored = store.get(content.hash))
    {
        const QSize size = metadataSize(*stored);
        if (!size.isEmpty())
            return size;
    }

    if (content.type == AssetType::Html)
        return QSize(kDefaultHtmlWidth, kDefaultHtmlHeight);
    return QSize(kDefaultVisualWidth, kDefaultVisualHeight);
}

QRectF centeredRect(const QSizeF& size)
{
    const double width = std::max(1.0, size.width());
    const double height = std::max(1.0, size.height());
    return QRectF(-width / 2.0, -height / 2.0, width, height);
}

QRectF centeredRect(const QSize& size)
{
    return centeredRect(QSizeF(size));
}

void paintCheckerboard(QPainter& painter, const QRectF& rect,
                       const QColor& base = QColor(64, 64, 64),
                       const QColor& alternate = QColor(96, 96, 96),
                       const QColor& borderColor = QColor(145, 145, 145))
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRect(rect);

    painter.setBrush(alternate);
    const int columns = int(std::ceil(rect.width() / kCheckerCell));
    const int rows = int(std::ceil(rect.height() / kCheckerCell));
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            if ((row + column) % 2 == 0)
                continue;
            const QRectF cell(rect.left() + column * kCheckerCell,
                              rect.top() + row * kCheckerCell, kCheckerCell,
                              kCheckerCell);
            painter.drawRect(cell.intersected(rect));
        }
    }

    QPen border(borderColor);
    border.setWidthF(2.0);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
    painter.restore();
}

bool isAssetMissing(const assets::AssetStore& store,
                    const model::AssetContent& content)
{
    return !store.get(content.hash).has_value();
}

QString assetTypeTag(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("IMAGE");
    case AssetType::Video:
        return QStringLiteral("VIDEO");
    case AssetType::Html:
        return QStringLiteral("HTML");
    case AssetType::Audio:
        return QStringLiteral("AUDIO");
    }
    return QStringLiteral("ASSET");
}

// PR-style offline card: dark checkerboard, 2px red frame, "素材丢失" headline
// and a compact MEDIA OFFLINE line with the short hash and asset type.  The
// caller's transform/opacity are already applied, so this only paints in local
// content coordinates and never adds rotation.
void paintMissingAssetCard(QPainter& painter, const QRectF& rect,
                           const model::AssetContent& content)
{
    const QRectF area = rect.normalized();
    if (area.width() < 1.0 || area.height() < 1.0)
        return;

    paintCheckerboard(painter, area, kMissingCardBase, kMissingCardAlternate,
                      kMissingCardBase);

    painter.save();
    QPen border(kMissingCardBorder);
    border.setWidthF(2.0);
    border.setJoinStyle(Qt::MiterJoin);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area.adjusted(1.0, 1.0, -1.0, -1.0));

    if (area.width() < 28.0 || area.height() < 14.0)
    {
        painter.restore();
        return;
    }

    const bool detailed = area.width() >= 160.0 && area.height() >= 52.0;

    int mainSize = int(std::clamp(area.height() * (detailed ? 0.17 : 0.30),
                                  9.0, 56.0));
    mainSize = std::min(mainSize, int(area.width() / 4.8));
    mainSize = std::max(6, mainSize);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(mainSize);
    painter.setFont(font);
    painter.setPen(QColor(240, 240, 240));

    const QRectF mainRect =
        detailed ? QRectF(area.left(), area.top() + area.height() * 0.14,
                          area.width(), area.height() * 0.44)
                 : area;
    painter.drawText(mainRect, Qt::AlignCenter, QStringLiteral("素材丢失"));

    if (detailed)
    {
        QFont secondary = font;
        secondary.setBold(false);
        const int secondarySize = std::max(
            7, int(std::clamp(area.height() * 0.075, 7.0, 14.0)));
        secondary.setPixelSize(secondarySize);
        painter.setFont(secondary);
        painter.setPen(QColor(168, 168, 174));

        QString detail = QStringLiteral("MEDIA OFFLINE");
        const QString shortHash = content.hash.left(8);
        if (!shortHash.isEmpty())
            detail += QStringLiteral("  ·  ") + shortHash;
        detail += QStringLiteral("  ·  ") + assetTypeTag(content.type);

        const QRectF detailRect(area.left() + 6.0,
                                area.top() + area.height() * 0.62,
                                std::max(1.0, area.width() - 12.0),
                                area.height() * 0.22);
        const QFontMetricsF metrics(secondary);
        const QString elided = metrics.elidedText(detail, Qt::ElideRight,
                                                  detailRect.width());
        painter.drawText(detailRect, Qt::AlignCenter, elided);
    }

    painter.restore();
}

void paintPlaceholder(QPainter& painter, const QRectF& rect,
                      const QString& label)
{
    paintCheckerboard(painter, rect);

    painter.save();
    QFont font = painter.font();
    font.setPixelSize(int(std::clamp(rect.height() * 0.12, 10.0, 56.0)));
    painter.setFont(font);
    painter.setPen(QColor(235, 235, 235));
    painter.drawText(rect, Qt::AlignCenter, label);
    painter.restore();
}

void paintVector(QPainter& painter, const model::VectorContent& vector)
{
    const QRectF rect = centeredRect(QSizeF(vector.width, vector.height));
    const QPainterPath path = VectorShape::path(
        vector.shape, rect, vector.cornerRadius, vector.sides, vector.innerRatio);

    if (vector.shape == 2)
    {
        QColor stroke = colorOr(vector.strokeColor, QColor(0, 0, 0, 0));
        if (stroke.alpha() == 0)
            stroke = colorOr(vector.fillColor, Qt::white);
        QPen pen(stroke);
        pen.setWidthF(std::max(1.0, vector.strokeWidth));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        return;
    }

    const QColor fill = colorOr(vector.fillColor, QColor(0, 0, 0, 0));
    painter.setBrush(fill.alpha() > 0 ? QBrush(fill) : QBrush(Qt::NoBrush));
    if (vector.strokeWidth > 0.0)
    {
        const QColor stroke = colorOr(vector.strokeColor, QColor(0, 0, 0, 0));
        if (stroke.alpha() > 0)
        {
            QPen pen(stroke);
            pen.setWidthF(vector.strokeWidth);
            painter.setPen(pen);
        }
        else
        {
            painter.setPen(Qt::NoPen);
        }
    }
    else
    {
        painter.setPen(Qt::NoPen);
    }
    painter.drawPath(path);
}

void paintText(QPainter& painter, const model::TextContent& text)
{
    if (text.text.isEmpty())
        return;
    const TextBlock block =
        TextBlock::measure(TextStyle::fromData(textStyleData(text)));
    if (block.size().width() < 1.0 || block.size().height() < 1.0)
        return;
    block.paint(painter, QPointF());
}

constexpr double kPi = 3.14159265358979323846;

// Stable pseudo random number in [0, 1), shared by the comic and decoration
// shapes so a seed always produces the same hand drawn wobble.
double hash1(double n)
{
    const double value = std::sin(n * 12.9898 + 7.233) * 43758.5453;
    return value - std::floor(value);
}

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

QPainterPath smoothClosedPath(const QVector<QPointF>& points)
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

QPainterPath comicCloudPath(const QSizeF& size, int seed)
{
    QVector<QPointF> points = superEllipse(size, 56, 2.62);
    const double jitter = std::max(0.8, size.height() / 400.0);
    for (int index = 0; index < points.size(); ++index)
    {
        points[index] += QPointF(
            (hash1(index + seed * 1000.0) - 0.5) * 1.2 * jitter,
            (hash1(index + 100 + seed * 1000.0) - 0.5) * 1.2 * jitter);
    }
    return smoothClosedPath(points);
}

// Spiky oval used by the "burst" balloon.  Peaks sit exactly on the box so
// the declared box stays a truthful bounds hint.
QPainterPath comicBurstPath(const QRectF& rect, int seed)
{
    QPainterPath path;
    constexpr int spikes = 14;
    const double a = rect.width() / 2.0;
    const double b = rect.height() / 2.0;
    const QPointF centre = rect.center();
    for (int index = 0; index < spikes * 2; ++index)
    {
        const double t = (index / double(spikes * 2)) * 2.0 * kPi - kPi / 2.0;
        const double factor = (index % 2 == 0) ? 1.0 : 0.78;
        const double wobble = 1.0 + (hash1(index + seed * 37.0) - 0.5) * 0.08;
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

// Triangle spike whose base lies on the balloon outline, so the union has no
// notch where the tail meets the body.
QPainterPath comicTailPath(const QPainterPath& body, const QRectF& rect,
                           bool curved, const QPointF& tip, double base)
{
    const QPointF centre = rect.center();
    const double halfW = rect.width() / 2.0;
    const double halfH = rect.height() / 2.0;
    if (halfW < 1e-6 || halfH < 1e-6)
        return body;

    QPointF base1;
    QPointF base2;
    if (curved)
    {
        const double angle = std::atan2((tip.y() - centre.y()) / halfH,
                                        (tip.x() - centre.x()) / halfW);
        constexpr double spread = 0.26;
        base1 = QPointF(halfW * std::cos(angle + spread),
                        halfH * std::sin(angle + spread));
        base2 = QPointF(halfW * std::cos(angle - spread),
                        halfH * std::sin(angle - spread));
    }
    else
    {
        QPointF direction = tip - centre;
        const double length = std::hypot(direction.x(), direction.y());
        if (length < 1e-6)
            return body;
        direction /= length;

        const double tx = std::fabs(direction.x()) > 1e-6
                              ? halfW / std::fabs(direction.x())
                              : 1e9;
        const double ty = std::fabs(direction.y()) > 1e-6
                              ? halfH / std::fabs(direction.y())
                              : 1e9;
        if (ty < tx)
        {
            const double y = direction.y() < 0.0 ? -halfH : halfH;
            const double x = direction.x() * ty;
            base1 = QPointF(x - base / 2.0, y);
            base2 = QPointF(x + base / 2.0, y);
        }
        else
        {
            const double x = direction.x() < 0.0 ? -halfW : halfW;
            const double y = direction.y() * tx;
            base1 = QPointF(x, y - base / 2.0);
            base2 = QPointF(x, y + base / 2.0);
        }
    }

    QPainterPath spike;
    spike.moveTo(base1);
    spike.lineTo(tip);
    spike.lineTo(base2);
    spike.closeSubpath();
    return body.united(spike);
}

QPainterPath comicPath(const model::ComicTextContent& comic)
{
    const QRectF rect = centeredRect(QSizeF(comic.boxWidth, comic.boxHeight));
    QPainterPath path;
    if (comic.shape == QLatin1String("rect"))
    {
        path.addRoundedRect(rect, comic.cornerRadius, comic.cornerRadius);
    }
    else if (comic.shape == QLatin1String("cloud"))
    {
        path = comicCloudPath(rect.size(), comic.seed);
    }
    else if (comic.shape == QLatin1String("burst"))
    {
        path = comicBurstPath(rect, comic.seed);
    }
    else
    {
        path.addEllipse(rect);
    }

    const bool inside =
        std::abs(comic.tailX) < 0.55 && std::abs(comic.tailY) < 0.55;
    if (comic.tail && comic.shape != QLatin1String("burst") && !inside)
    {
        const QPointF tip(comic.tailX * rect.width() / 2.0,
                          comic.tailY * rect.height() / 2.0);
        const double base = std::min(rect.width(), rect.height()) * 0.34;
        path = comicTailPath(path, rect, comic.shape != QLatin1String("rect"),
                             tip, base);
    }
    return path;
}

void paintComicText(QPainter& painter, const model::ComicTextContent& comic)
{
    if (comic.boxWidth <= 0.0 || comic.boxHeight <= 0.0)
        return;

    const QPainterPath path = comicPath(comic);
    if (path.isEmpty())
        return;

    painter.save();
    painter.rotate(comic.rotation);
    painter.setBrush(colorOr(comic.fillColor, Qt::white));
    if (comic.bubbleStrokeWidth > 0.0)
    {
        QPen pen(colorOr(comic.bubbleStrokeColor, Qt::black));
        pen.setWidthF(comic.bubbleStrokeWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
    }
    else
    {
        painter.setPen(Qt::NoPen);
    }
    painter.drawPath(path);

    if (!comic.text.isEmpty())
    {
        TextStyle style;
        style.text = comic.text;
        style.fontFamily = comic.fontFamily;
        style.pixelSize = comic.pixelSize;
        style.color = colorOr(comic.color, Qt::white);
        style.strokeColor = colorOr(comic.strokeColor, Qt::black);
        style.strokeWidth = comic.strokeWidth;
        style.align = comic.align;
        style.lineHeight = comic.lineHeight;
        style.padding = 0.0;
        style.boxWidth = std::max(1.0, comic.boxWidth - 2.0 * comic.padding);
        style.boxHeight = std::max(0.0, comic.boxHeight - 2.0 * comic.padding);
        style.bold = comic.bold;
        style.italic = comic.italic;

        const TextBlock block = TextBlock::measure(style);
        if (block.size().width() >= 1.0 && block.size().height() >= 1.0)
            block.paint(painter, QPointF());
    }
    painter.restore();
}

void paintRadialLines(QPainter& painter,
                      const model::RadialLinesContent& radial)
{
    if (radial.count <= 0 || radial.radius <= 0.0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colorOr(radial.color, Qt::white));

    const double outer = radial.radius;
    const double inner = outer * std::clamp(radial.innerRatio, 0.0, 0.95);
    const double patternAngle = qDegreesToRadians(radial.angle);
    const auto point = [&](double angle, double radius) {
        return QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
    };

    QPolygonF wedge;
    for (int index = 0; index < radial.count; ++index)
    {
        const double base =
            (index / double(radial.count)) * 2.0 * kPi + patternAngle;
        const double wobble =
            (hash1(index + radial.seed * 1000.0) - 0.5) * 0.03;
        const double angle = base + wobble;
        const double width =
            radial.width
            * (0.75 + 0.5 * hash1(index + 100 + radial.seed * 1000.0));
        const double halfOuter = width / 2.0 / std::max(1.0, outer);
        const double halfInner = halfOuter * 0.12;

        wedge.clear();
        wedge << point(angle - halfInner, inner)
              << point(angle - halfOuter, outer)
              << point(angle + halfOuter, outer)
              << point(angle + halfInner, inner);
        painter.drawPolygon(wedge);
    }
    painter.restore();
}

void paintFlowLines(QPainter& painter, const model::FlowLinesContent& flow)
{
    if (flow.count <= 0 || flow.area <= 0.0 || flow.spacing <= 0.0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.rotate(flow.angle);
    const QRectF areaRect(-flow.area / 2.0, -flow.area / 2.0, flow.area,
                          flow.area);
    painter.setClipRect(areaRect, Qt::IntersectClip);

    QPen pen(colorOr(flow.color, Qt::white));
    pen.setWidthF(std::max(0.1, flow.thickness));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const auto point = [&](double t, double offset) {
        const double x = (t - 0.5) * flow.area;
        const double y = flow.bend * 4.0 * t * (1.0 - t) + offset;
        return QPointF(x, y);
    };

    const double span = std::clamp(flow.length / flow.area, 0.02, 1.0);
    for (int index = 0; index < flow.count; ++index)
    {
        const double wobble =
            (hash1(index + flow.seed * 1000.0) - 0.5) * 0.3;
        const double offset =
            (index - (flow.count - 1) / 2.0) * flow.spacing
            + wobble * flow.spacing;
        const double lineSpan =
            std::clamp(span
                           * (0.65
                              + 0.35 * hash1(index + 100 + flow.seed * 1000.0)),
                       0.02, 1.0);
        const double slack = 1.0 - lineSpan;
        const double start = std::clamp(
            slack
                * (0.5 + flow.phase
                   + (hash1(index + 200 + flow.seed * 1000.0) - 0.5) * 0.4),
            0.0, slack);

        const int steps = std::clamp(int(std::ceil(lineSpan * 64.0)), 2, 64);
        QPainterPath path;
        path.moveTo(point(start, offset));
        for (int step = 1; step <= steps; ++step)
        {
            const double t = start + lineSpan * step / steps;
            path.lineTo(point(t, offset));
        }
        painter.drawPath(path);
    }
    painter.restore();
}

void paintHalftoneDots(QPainter& painter,
                       const model::HalftoneDotsContent& halftone)
{
    if (halftone.spacing <= 0.0 || halftone.dotSize <= 0.0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colorOr(halftone.color, Qt::white));

    const double minRatio = std::clamp(halftone.minRatio, 0.0, 1.0);
    const double jitter = std::clamp(halftone.jitter, 0.0, 1.0);

    if (halftone.layout == QLatin1String("radial"))
    {
        const double radius = halftone.radius;
        if (radius <= 0.0)
        {
            painter.restore();
            return;
        }
        const int rings = int(std::ceil(radius / halftone.spacing));
        for (int ring = 1; ring <= rings; ++ring)
        {
            const double ringRadius = ring * halftone.spacing;
            if (ringRadius > radius)
                break;
            const int count =
                std::max(6, int(2.0 * kPi * ringRadius / halftone.spacing));
            const double ratio =
                minRatio
                + (1.0 - minRatio) * std::clamp(1.0 - ringRadius / radius, 0.0, 1.0);
            for (int index = 0; index < count; ++index)
            {
                const double angle =
                    (index / double(count)) * 2.0 * kPi
                    + qDegreesToRadians(halftone.angle)
                    + (hash1(index + ring * 17.0 + halftone.seed * 1000.0)
                       - 0.5)
                          * 0.06;
                QPointF pos(std::cos(angle) * ringRadius,
                            std::sin(angle) * ringRadius);
                pos += QPointF(
                    (hash1(index * 3.1 + ring * 7.7 + halftone.seed * 4000.0)
                     - 0.5)
                        * halftone.spacing * jitter * 0.5,
                    (hash1(index * 5.3 + ring * 9.1 + halftone.seed * 5000.0)
                     - 0.5)
                        * halftone.spacing * jitter * 0.5);
                double dotRadius = halftone.dotSize * ratio;
                dotRadius *=
                    1.0
                    + (hash1(index * 7.1 + ring * 3.9 + halftone.seed * 3000.0)
                       - 0.5)
                          * jitter;
                if (dotRadius > 0.2)
                    painter.drawEllipse(pos, dotRadius, dotRadius);
            }
        }
        painter.restore();
        return;
    }

    const double half = halftone.area / 2.0;
    const QRectF clip(-half, -half, halftone.area, halftone.area);
    painter.setClipRect(clip, Qt::IntersectClip);

    const double gridAngle = qDegreesToRadians(halftone.angle);
    const double fadeAngle = qDegreesToRadians(halftone.gradientAngle);
    const QPointF fade(std::cos(fadeAngle), std::sin(fadeAngle));
    const double cosA = std::cos(gridAngle);
    const double sinA = std::sin(gridAngle);

    const int count = int(std::ceil(half * 1.5 / halftone.spacing)) + 1;
    const double reach = std::max(1.0, halftone.area);
    for (int row = -count; row <= count; ++row)
    {
        for (int column = -count; column <= count; ++column)
        {
            const double baseX = column * halftone.spacing;
            const double baseY = row * halftone.spacing;
            const double wobbleX =
                (hash1(column * 11.3 + row * 3.7 + halftone.seed * 4000.0)
                 - 0.5)
                * halftone.spacing * jitter * 0.6;
            const double wobbleY =
                (hash1(column * 7.1 + row * 13.9 + halftone.seed * 5000.0)
                 - 0.5)
                * halftone.spacing * jitter * 0.6;
            const double gridX = baseX + wobbleX;
            const double gridY = baseY + wobbleY;
            const QPointF pos(gridX * cosA - gridY * sinA,
                              gridX * sinA + gridY * cosA);
            if (pos.x() < -half - halftone.dotSize
                || pos.x() > half + halftone.dotSize
                || pos.y() < -half - halftone.dotSize
                || pos.y() > half + halftone.dotSize)
                continue;

            const double projection = pos.x() * fade.x() + pos.y() * fade.y();
            const double strength =
                std::clamp(0.5 - projection / reach, 0.0, 1.0);
            const double ratio = minRatio + (1.0 - minRatio) * strength;
            if (ratio <= 0.001)
                continue;

            double dotRadius = halftone.dotSize * ratio;
            dotRadius *=
                1.0
                + (hash1(column * 5.3 + row * 9.7 + halftone.seed * 3000.0)
                   - 0.5)
                      * jitter;
            if (dotRadius > 0.2)
                painter.drawEllipse(pos, dotRadius, dotRadius);
        }
    }
    painter.restore();
}

void paintBrandWall(QPainter& painter, const model::BrandWallContent& wall)
{
    if (wall.columns <= 0 || wall.rows <= 0 || wall.logoSize <= 0.0)
        return;

    const double spacing = std::max(0.0, wall.spacing);
    const double cell = wall.logoSize;
    const double width =
        wall.columns * cell + std::max(0, wall.columns - 1) * spacing;
    const double height =
        wall.rows * cell + std::max(0, wall.rows - 1) * spacing;
    const QRectF bounds(-width / 2.0, -height / 2.0, width, height);

    QVector<QColor> colors;
    colors.reserve(wall.colors.size());
    for (const QString& value : wall.colors)
    {
        const QColor color(value);
        if (color.isValid())
            colors.append(color);
    }
    if (colors.isEmpty())
        colors.append(QColor(210, 70, 160));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.rotate(wall.angle);

    const QColor background(wall.background);
    if (!wall.background.isEmpty() && background.isValid()
        && background.alpha() > 0)
        painter.fillRect(bounds, background);

    painter.setClipRect(bounds, Qt::IntersectClip);

    QFont font;
    font.setBold(true);
    const int basePixel =
        std::clamp(int(std::lround(cell * 0.32)), 4, 512);
    font.setPixelSize(basePixel);

    const double step = cell + spacing;
    for (int row = 0; row < wall.rows; ++row)
    {
        for (int column = 0; column < wall.columns; ++column)
        {
            const QRectF tile(bounds.left() + column * step,
                              bounds.top() + row * step, cell, cell);
            const int slot = wall.alternate ? row + column
                                            : row * wall.columns + column;
            const QColor color = colors.at(slot % colors.size());
            painter.fillRect(tile, color);

            if (wall.logos.isEmpty())
                continue;

            const int logoCount = wall.logos.size();
            int logoIndex =
                int((qint64(slot) + qint64(wall.seed)) % logoCount);
            if (logoIndex < 0)
                logoIndex += logoCount;
            const QString& label = wall.logos.at(logoIndex);
            if (label.isEmpty())
                continue;

            QFont tileFont = font;
            QFontMetricsF metrics(tileFont);
            const double availableWidth = std::max(1.0, cell * 0.84);
            const double availableHeight = std::max(1.0, cell * 0.84);
            const double textWidth =
                std::max(1.0, metrics.horizontalAdvance(label));
            const double textHeight = std::max(1.0, metrics.height());
            if (textWidth > availableWidth || textHeight > availableHeight)
            {
                const double scale = std::min(
                    availableWidth / textWidth, availableHeight / textHeight);
                tileFont.setPixelSize(std::max(
                    3, int(std::floor(basePixel * scale))));
            }

            painter.save();
            painter.setClipRect(tile, Qt::IntersectClip);
            painter.setFont(tileFont);
            painter.setPen(qGray(color.rgb()) > 150 ? Qt::black : Qt::white);
            painter.drawText(tile, Qt::AlignCenter | Qt::TextWordWrap, label);
            painter.restore();
        }
    }
    painter.restore();
}

void paintContent(QPainter& painter, const model::Entity& entity,
                  const assets::AssetStore& store, const Options& options,
                  double projectFrame)
{
    switch (entity.content.kind)
    {
    case ContentKind::Empty:
    case ContentKind::Camera:
        return;

    case ContentKind::Vector:
        paintVector(painter,
                    animatedVector(entity, projectFrame - entity.start));
        return;

    case ContentKind::Text:
        paintText(painter, animatedText(entity, projectFrame - entity.start));
        return;

    case ContentKind::ComicText:
        paintComicText(painter, entity.content.comic);
        return;

    case ContentKind::RadialLines:
        paintRadialLines(painter, entity.content.radial);
        return;

    case ContentKind::FlowLines:
        paintFlowLines(painter, entity.content.flow);
        return;

    case ContentKind::HalftoneDots:
        paintHalftoneDots(painter, entity.content.halftone);
        return;

    case ContentKind::BrandWall:
        paintBrandWall(painter, entity.content.brandWall);
        return;

    case ContentKind::Asset:
        break;
    }

    const model::AssetContent& asset = entity.content.asset;
    switch (asset.type)
    {
    case AssetType::Audio:
        return;

    case AssetType::Image:
    {
        const Picture picture = loadPicture(store, asset.hash);
        if (picture.isValid())
        {
            const QRectF bounds = centeredRect(picture.size);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, picture.image,
                              QRectF(picture.image.rect()));
        }
        else
        {
            // Missing metadata and unreadable files both count as offline: the
            // entity keeps existing, but there is nothing usable to draw.
            paintMissingAssetCard(painter,
                                  centeredRect(placeholderSize(store, asset)),
                                  asset);
        }
        return;
    }

    case AssetType::Video:
    {
        // Invoke the provider even for offline assets: previews rely on the
        // callback contract and may warm caches, but a stale frame must not
        // mask the missing-asset card.
        const QImage frame = options.videoFrame
                ? options.videoFrame(entity.uuid, projectFrame)
                : QImage();
        if (isAssetMissing(store, asset))
        {
            paintMissingAssetCard(painter,
                                  centeredRect(placeholderSize(store, asset)),
                                  asset);
            return;
        }

        if (!frame.isNull())
        {
            const QRectF bounds(-frame.width() / 2.0, -frame.height() / 2.0,
                                frame.width(), frame.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, frame);
        }
        else
        {
            // The asset exists; the provider simply has no decoded frame yet.
            paintPlaceholder(painter, centeredRect(placeholderSize(store, asset)),
                             QStringLiteral("视频"));
        }
        return;
    }

    case AssetType::Html:
    {
        const QImage frame = options.htmlFrame
                ? options.htmlFrame(entity.uuid, projectFrame)
                : QImage();
        if (isAssetMissing(store, asset))
        {
            paintMissingAssetCard(painter,
                                  centeredRect(placeholderSize(store, asset)),
                                  asset);
            return;
        }

        if (!frame.isNull())
        {
            const QRectF bounds(-frame.width() / 2.0, -frame.height() / 2.0,
                                frame.width(), frame.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(bounds, frame);
        }
        else
        {
            // Asset present but Chrome has not produced a frame yet.
            paintPlaceholder(painter, centeredRect(placeholderSize(store, asset)),
                             QStringLiteral("HTML"));
        }
        return;
    }
    }
}

QSizeF contentSize(const model::Entity& entity, const assets::AssetStore& store,
                   double projectFrame)
{
    switch (entity.content.kind)
    {
    case ContentKind::Vector:
    {
        const model::VectorContent vector =
            animatedVector(entity, projectFrame - entity.start);
        return QSizeF(std::max(1.0, vector.width), std::max(1.0, vector.height));
    }

    case ContentKind::Text:
    {
        const model::TextContent text =
            animatedText(entity, projectFrame - entity.start);
        if (text.text.isEmpty())
            return {};
        const TextBlock block = TextBlock::measure(
            TextStyle::fromData(textStyleData(text)));
        return block.size();
    }

    case ContentKind::Asset:
    {
        const model::AssetContent& asset = entity.content.asset;
        if (asset.type == AssetType::Audio)
            return {};

        if (asset.type == AssetType::Image)
        {
            const Picture picture = loadPicture(store, asset.hash);
            if (picture.isValid())
                return QSizeF(picture.size);
        }

        return QSizeF(placeholderSize(store, asset));
    }

    case ContentKind::ComicText:
    {
        const model::ComicTextContent& comic = entity.content.comic;
        if (comic.boxWidth <= 0.0 || comic.boxHeight <= 0.0)
            return {};
        QRectF bounds = comicPath(comic).boundingRect();
        const double margin =
            std::max(1.0, comic.bubbleStrokeWidth / 2.0 + 1.0);
        bounds.adjust(-margin, -margin, margin, margin);
        if (std::abs(comic.rotation) > 1e-6)
        {
            QTransform rotation;
            rotation.rotate(comic.rotation);
            bounds = rotation.mapRect(bounds);
        }
        return bounds.size();
    }

    case ContentKind::RadialLines:
    {
        const model::RadialLinesContent& radial = entity.content.radial;
        const double extent = radial.radius + std::max(0.0, radial.width);
        return QSizeF(std::max(1.0, extent * 2.0),
                      std::max(1.0, extent * 2.0));
    }

    case ContentKind::FlowLines:
    {
        const model::FlowLinesContent& flow = entity.content.flow;
        const double angle = qDegreesToRadians(flow.angle);
        const double extent =
            flow.area
                * (std::abs(std::cos(angle)) + std::abs(std::sin(angle)))
            + std::max(0.0, flow.thickness);
        return QSizeF(std::max(1.0, extent), std::max(1.0, extent));
    }

    case ContentKind::HalftoneDots:
    {
        const model::HalftoneDotsContent& halftone = entity.content.halftone;
        if (halftone.layout == QLatin1String("radial"))
        {
            const double extent =
                halftone.radius + std::max(0.0, halftone.dotSize);
            return QSizeF(std::max(1.0, extent * 2.0),
                          std::max(1.0, extent * 2.0));
        }
        const double angle = qDegreesToRadians(halftone.angle);
        const double extent =
            halftone.area
                * (std::abs(std::cos(angle)) + std::abs(std::sin(angle)))
            + std::max(0.0, halftone.dotSize) * 2.0;
        return QSizeF(std::max(1.0, extent), std::max(1.0, extent));
    }

    case ContentKind::BrandWall:
    {
        const model::BrandWallContent& wall = entity.content.brandWall;
        const int columns = std::max(1, wall.columns);
        const int rows = std::max(1, wall.rows);
        const double spacing = std::max(0.0, wall.spacing);
        const double size = std::max(0.0, wall.logoSize);
        QRectF bounds(0.0, 0.0,
                      columns * size + (columns - 1) * spacing,
                      rows * size + (rows - 1) * spacing);
        if (std::abs(wall.angle) > 1e-6)
        {
            QTransform rotation;
            rotation.rotate(wall.angle);
            bounds = rotation.mapRect(bounds);
        }
        return QSizeF(std::max(1.0, bounds.width()),
                      std::max(1.0, bounds.height()));
    }

    case ContentKind::Empty:
    case ContentKind::Camera:
        return {};
    }
    return {};
}

QPainterPath localCollisionPath(const model::Entity& entity,
                                const assets::AssetStore& store,
                                double projectFrame)
{
    if (entity.content.kind == ContentKind::Vector)
    {
        const model::VectorContent vector =
            animatedVector(entity, projectFrame - entity.start);
        const QRectF rect = centeredRect(QSizeF(vector.width, vector.height));
        // Vector paths are authored in content-local Y-down space, matching
        // paintContent().  Collision space is model Y-up, so apply the same
        // single content flip here; otherwise an asymmetric shape (arrow,
        // heart, triangle) would collide with its mirrored outline.
        QTransform contentToModel;
        contentToModel.scale(1.0, -1.0);
        return contentToModel.map(VectorShape::path(
            vector.shape, rect, vector.cornerRadius, vector.sides,
            vector.innerRatio));
    }

    if (entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Audio)
        return {};

    const QSizeF size = contentSize(entity, store, projectFrame);
    if (size.width() < 1.0 || size.height() < 1.0)
        return {};

    QPainterPath path;
    path.addRect(centeredRect(size));
    return path;
}

QTransform canvasView(const QSize& output, const QRectF& worldRect)
{
    const double width = std::max(1e-9, worldRect.width());
    const double height = std::max(1e-9, worldRect.height());
    const double scaleX = double(output.width()) / width;
    const double scaleY = double(output.height()) / height;

    // Model space is Y-up, the output image is Y-down.  This is the single
    // world -> screen Y flip; nothing below the renderer may flip again.
    // y' = (worldRect.bottom() - y) * scaleY, so the model top edge
    // (worldRect.bottom(), the larger Y) lands on image row 0.
    QTransform transform;
    transform.setMatrix(scaleX, 0.0, 0.0,
                        0.0, -scaleY, 0.0,
                        -worldRect.left() * scaleX,
                        worldRect.bottom() * scaleY, 1.0);
    return transform;
}

QTransform canvasView(const QSize& output, const model::ProjectSettings& settings)
{
    const double width = double(std::max(1, settings.width));
    const double height = double(std::max(1, settings.height));
    return canvasView(output, QRectF(-width / 2.0, -height / 2.0, width, height));
}

QTransform cameraView(const model::Project& project, const Options& options,
                      anim::World& world, anim::Cache& cache,
                      const QSize& output)
{
    const auto camera = project.entities.constFind(options.cameraUuid);
    if (camera == project.entities.constEnd())
        return canvasView(output, project.settings);

    const anim::Pose pose = anim::evaluateEntity(
        project, options.cameraUuid, options.projectFrame, world, cache);
    const double width = std::max(1, project.settings.width);
    const double height = std::max(1, project.settings.height);

    // Camera-local corners are also Y-up: top-left is (-w/2, +h/2).
    QPolygonF source;
    source << QPointF(-width / 2.0, height / 2.0)
           << QPointF(width / 2.0, height / 2.0)
           << QPointF(width / 2.0, -height / 2.0)
           << QPointF(-width / 2.0, -height / 2.0);
    const QPolygonF quad = pose.world.map(source);

    QPolygonF target;
    target << QPointF(0.0, 0.0)
           << QPointF(output.width(), 0.0)
           << QPointF(output.width(), output.height())
           << QPointF(0.0, output.height());

    // quadToQuad carries the world -> screen Y flip from the camera-local
    // Y-up polygon to the Y-down output polygon.
    QTransform transform;
    if (QTransform::quadToQuad(quad, target, transform))
        return transform;

    // Degenerate camera transform: fall back to the frame's bounding box with
    // the same canvas mapping (including the Y flip).
    return canvasView(output, quad.boundingRect());
}

void drawChildren(QPainter& painter, const model::Project& project,
                  const assets::AssetStore& store, const Uuid& parentUuid,
                  double projectFrame, anim::World& world, anim::Cache& cache,
                  QSet<Uuid>& visited, const Options& options)
{
    const QVector<Uuid> children = model::childrenOf(project, parentUuid);
    for (const Uuid& uuid : children)
    {
        if (visited.contains(uuid))
            continue;
        visited.insert(uuid);

        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;

        const model::Entity& entity = it.value();
        if (activeAt(entity, projectFrame))
        {
            const anim::Pose pose = anim::evaluateEntity(
                project, uuid, projectFrame, world, cache);
            if (pose.visible && pose.opacity > 0.0)
            {
                painter.save();
                painter.setOpacity(std::clamp(pose.opacity, 0.0, 1.0));
                painter.setTransform(pose.world, true);
                // Content (Qt text, images, vector paths) is authored in
                // local Y-down space; model space is Y-up.  Flip once here,
                // around the content origin, so content is upright in model
                // space.  The view transform already did world -> screen.
                painter.scale(1.0, -1.0);
                paintContent(painter, entity, store, options, projectFrame);
                painter.restore();
            }
        }

        drawChildren(painter, project, store, uuid, projectFrame, world, cache,
                     visited, options);
    }
}

} // namespace

model::Content animatedContent(const model::Entity& entity, double projectFrame)
{
    model::Content content = entity.content;
    const double relativeFrame = projectFrame - double(entity.start);
    switch (content.kind)
    {
    case ContentKind::Text:
        content.text = animatedText(entity, relativeFrame);
        break;
    case ContentKind::Vector:
        content.vector = animatedVector(entity, relativeFrame);
        break;
    default:
        break;
    }
    return content;
}

SceneWorld::SceneWorld(const model::Project& project,
                       const assets::AssetStore& assets)
    : m_project(project)
    , m_assets(assets)
{
}

solver::Pose SceneWorld::poseOf(const Uuid& uuid, int projectFrame) const
{
    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this);
    solver::Pose result;
    result.position = pose.world.map(QPointF(0.0, 0.0));
    result.rotation =
        qRadiansToDegrees(std::atan2(pose.world.m12(), pose.world.m11()));
    return result;
}

QPainterPath SceneWorld::shapeOf(const Uuid& uuid, int projectFrame) const
{
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd())
        return {};
    if (!activeAt(it.value(), projectFrame))
        return {};

    const QPainterPath local =
        localCollisionPath(it.value(), m_assets, double(projectFrame));
    if (local.isEmpty())
        return {};

    const anim::Pose pose =
        anim::evaluateEntity(m_project, uuid, projectFrame, *this);
    return pose.world.map(local);
}

bool SceneWorld::physicsInteract(const Uuid& uuid, int projectFrame) const
{
    return anim::physicsInteractAt(m_project, uuid, projectFrame);
}

QVector<Uuid> SceneWorld::physicsEntities(int projectFrame) const
{
    QVector<Uuid> result;
    for (auto it = m_project.entities.constBegin();
         it != m_project.entities.constEnd(); ++it)
    {
        if (!activeAt(it.value(), projectFrame))
            continue;
        if (anim::physicsInteractAt(m_project, it.key(), projectFrame))
            result.append(it.key());
    }
    std::sort(result.begin(), result.end());
    return result;
}

QImage renderFrame(const model::Project& project,
                   const assets::AssetStore& assets, const Options& options,
                   anim::World& world, anim::Cache& cache)
{
    QSize output = options.outputSize;
    if (output.isEmpty())
        output = QSize(project.settings.width, project.settings.height);
    output = QSize(std::max(1, output.width()), std::max(1, output.height()));

    QColor background = options.background;
    if (!background.isValid())
        background = QColor(project.settings.background);
    if (!background.isValid())
        background = Qt::black;

    const bool canvasMode = options.cameraUuid.isEmpty();
    const double canvasWidth = double(std::max(1, project.settings.width));
    const double canvasHeight = double(std::max(1, project.settings.height));
    // World origin is the canvas centre; +Y is up.
    const QRectF projectCanvas(-canvasWidth / 2.0, -canvasHeight / 2.0,
                               canvasWidth, canvasHeight);
    const bool customWorld =
        canvasMode && options.worldRect.isValid()
        && options.worldRect.width() > 0.0 && options.worldRect.height() > 0.0;
    const QRectF worldRect = customWorld ? options.worldRect : projectCanvas;
    // Only the expanded editor preview asks for a transparent surround.  The
    // canvas itself keeps the project background so the cached image still
    // carries the real picture inside the frame.
    const bool transparent =
        canvasMode && customWorld && options.transparentOutsideCanvas;

    QImage image(output, QImage::Format_ARGB32_Premultiplied);
    image.fill(transparent ? QColor(Qt::transparent) : background);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QTransform view =
        canvasMode ? canvasView(output, worldRect)
                   : cameraView(project, options, world, cache, output);
    painter.setTransform(view);
    if (transparent)
        painter.fillRect(projectCanvas, background);

    QSet<Uuid> visited;
    drawChildren(painter, project, assets, Uuid(), options.projectFrame, world,
                 cache, visited, options);
    painter.end();
    return image;
}

QString placeholderLabelForTest(const assets::AssetStore& store,
                                const model::AssetContent& content)
{
    if (isAssetMissing(store, content))
        return QStringLiteral("素材丢失");

    switch (content.type)
    {
    case AssetType::Image:
        return loadPicture(store, content.hash).isValid()
                   ? QString()
                   : QStringLiteral("素材丢失");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    case AssetType::Audio:
        return {};
    }
    return {};
}

} // namespace pvf::render2
