#include "Content.h"

#include "JsonUtil.h"

#include <QRegularExpression>

#include <cmath>

namespace pvf::model {

namespace {

std::optional<ContentKind> contentKindFromName(const QString& name)
{
    if (name == QLatin1String("empty"))
        return ContentKind::Empty;
    if (name == QLatin1String("text"))
        return ContentKind::Text;
    if (name == QLatin1String("vector"))
        return ContentKind::Vector;
    if (name == QLatin1String("camera"))
        return ContentKind::Camera;
    if (name == QLatin1String("asset"))
        return ContentKind::Asset;
    if (name == QLatin1String("comicText"))
        return ContentKind::ComicText;
    if (name == QLatin1String("radialLines"))
        return ContentKind::RadialLines;
    if (name == QLatin1String("flowLines"))
        return ContentKind::FlowLines;
    if (name == QLatin1String("halftoneDots"))
        return ContentKind::HalftoneDots;
    if (name == QLatin1String("brandWall"))
        return ContentKind::BrandWall;
    return std::nullopt;
}

bool isComicShape(const QString& name)
{
    return name == QLatin1String("ellipse") || name == QLatin1String("rect")
           || name == QLatin1String("cloud") || name == QLatin1String("burst");
}

bool isHalftoneLayout(const QString& name)
{
    return name == QLatin1String("grid") || name == QLatin1String("radial");
}

// Colours stay plain strings in the model, which links only QtCore.  #rgb,
// #rrggbb and #aarrggbb match what QColor accepts for wall colours.
bool isColorValue(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    return pattern.match(value).hasMatch();
}

bool finiteNumber(double value, const QString& label, QString* error)
{
    if (std::isfinite(value))
        return true;
    detail::setError(error,
                     QStringLiteral("%1 必须是有限数值").arg(label));
    return false;
}

bool numberInRange(double value, double minimum, double maximum,
                   const QString& label, QString* error)
{
    if (!std::isfinite(value))
    {
        detail::setError(error,
                         QStringLiteral("%1 必须是有限数值").arg(label));
        return false;
    }
    if (value < minimum || value > maximum)
    {
        detail::setError(error,
                         QStringLiteral("%1 必须在 %2..%3 之间（当前 %4）")
                             .arg(label)
                             .arg(minimum)
                             .arg(maximum)
                             .arg(value));
        return false;
    }
    return true;
}

std::optional<AssetType> assetTypeFromName(const QString& name)
{
    if (name == QLatin1String("image"))
        return AssetType::Image;
    if (name == QLatin1String("video"))
        return AssetType::Video;
    if (name == QLatin1String("audio"))
        return AssetType::Audio;
    if (name == QLatin1String("html"))
        return AssetType::Html;
    return std::nullopt;
}

QJsonObject textToJson(const TextContent& text)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), text.text);
    object.insert(QStringLiteral("fontFamily"), text.fontFamily);
    object.insert(QStringLiteral("pixelSize"), text.pixelSize);
    object.insert(QStringLiteral("color"), text.color);
    object.insert(QStringLiteral("strokeColor"), text.strokeColor);
    object.insert(QStringLiteral("strokeWidth"), text.strokeWidth);
    object.insert(QStringLiteral("align"), text.align);
    object.insert(QStringLiteral("lineHeight"), text.lineHeight);
    object.insert(QStringLiteral("letterSpacing"), text.letterSpacing);
    object.insert(QStringLiteral("padding"), text.padding);
    object.insert(QStringLiteral("boxWidth"), text.boxWidth);
    object.insert(QStringLiteral("boxHeight"), text.boxHeight);
    object.insert(QStringLiteral("bold"), text.bold);
    object.insert(QStringLiteral("italic"), text.italic);
    object.insert(QStringLiteral("underline"), text.underline);
    object.insert(QStringLiteral("vertical"), text.vertical);
    object.insert(QStringLiteral("verticalRtl"), text.verticalRtl);
    return object;
}

TextContent textFromJson(const QJsonObject& object)
{
    TextContent text;
    text.text = detail::jsonString(object, QStringLiteral("text"), text.text);
    text.fontFamily = detail::jsonString(
        object, QStringLiteral("fontFamily"), text.fontFamily);
    text.pixelSize = detail::jsonInt(
        object, QStringLiteral("pixelSize"), text.pixelSize);
    text.color = detail::jsonString(
        object, QStringLiteral("color"), text.color);
    text.strokeColor = detail::jsonString(
        object, QStringLiteral("strokeColor"), text.strokeColor);
    text.strokeWidth = detail::jsonDouble(
        object, QStringLiteral("strokeWidth"), text.strokeWidth);
    text.align = detail::jsonInt(object, QStringLiteral("align"), text.align);
    text.lineHeight = detail::jsonDouble(
        object, QStringLiteral("lineHeight"), text.lineHeight);
    text.letterSpacing = detail::jsonDouble(
        object, QStringLiteral("letterSpacing"), text.letterSpacing);
    text.padding = detail::jsonDouble(
        object, QStringLiteral("padding"), text.padding);
    text.boxWidth = detail::jsonDouble(
        object, QStringLiteral("boxWidth"), text.boxWidth);
    text.boxHeight = detail::jsonDouble(
        object, QStringLiteral("boxHeight"), text.boxHeight);
    text.bold = detail::jsonBool(object, QStringLiteral("bold"), text.bold);
    text.italic = detail::jsonBool(
        object, QStringLiteral("italic"), text.italic);
    text.underline = detail::jsonBool(
        object, QStringLiteral("underline"), text.underline);
    text.vertical = detail::jsonBool(
        object, QStringLiteral("vertical"), text.vertical);
    text.verticalRtl = detail::jsonBool(
        object, QStringLiteral("verticalRtl"), text.verticalRtl);
    return text;
}

QJsonObject vectorToJson(const VectorContent& vector)
{
    QJsonObject object;
    object.insert(QStringLiteral("shape"), vector.shape);
    object.insert(QStringLiteral("width"), vector.width);
    object.insert(QStringLiteral("height"), vector.height);
    object.insert(QStringLiteral("cornerRadius"), vector.cornerRadius);
    object.insert(QStringLiteral("sides"), vector.sides);
    object.insert(QStringLiteral("innerRatio"), vector.innerRatio);
    object.insert(QStringLiteral("fillColor"), vector.fillColor);
    object.insert(QStringLiteral("strokeColor"), vector.strokeColor);
    object.insert(QStringLiteral("strokeWidth"), vector.strokeWidth);
    return object;
}

VectorContent vectorFromJson(const QJsonObject& object)
{
    VectorContent vector;
    vector.shape = detail::jsonInt(object, QStringLiteral("shape"), vector.shape);
    vector.width = detail::jsonDouble(
        object, QStringLiteral("width"), vector.width);
    vector.height = detail::jsonDouble(
        object, QStringLiteral("height"), vector.height);
    vector.cornerRadius = detail::jsonDouble(
        object, QStringLiteral("cornerRadius"), vector.cornerRadius);
    vector.sides = detail::jsonInt(object, QStringLiteral("sides"), vector.sides);
    vector.innerRatio = detail::jsonDouble(
        object, QStringLiteral("innerRatio"), vector.innerRatio);
    vector.fillColor = detail::jsonString(
        object, QStringLiteral("fillColor"), vector.fillColor);
    vector.strokeColor = detail::jsonString(
        object, QStringLiteral("strokeColor"), vector.strokeColor);
    vector.strokeWidth = detail::jsonDouble(
        object, QStringLiteral("strokeWidth"), vector.strokeWidth);
    return vector;
}

QJsonObject comicToJson(const ComicTextContent& comic)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), comic.text);
    object.insert(QStringLiteral("fontFamily"), comic.fontFamily);
    object.insert(QStringLiteral("pixelSize"), comic.pixelSize);
    object.insert(QStringLiteral("color"), comic.color);
    object.insert(QStringLiteral("strokeColor"), comic.strokeColor);
    object.insert(QStringLiteral("strokeWidth"), comic.strokeWidth);
    object.insert(QStringLiteral("align"), comic.align);
    object.insert(QStringLiteral("lineHeight"), comic.lineHeight);
    object.insert(QStringLiteral("bold"), comic.bold);
    object.insert(QStringLiteral("italic"), comic.italic);
    object.insert(QStringLiteral("shape"), comic.shape);
    object.insert(QStringLiteral("boxWidth"), comic.boxWidth);
    object.insert(QStringLiteral("boxHeight"), comic.boxHeight);
    object.insert(QStringLiteral("fillColor"), comic.fillColor);
    object.insert(QStringLiteral("bubbleStrokeColor"), comic.bubbleStrokeColor);
    object.insert(QStringLiteral("bubbleStrokeWidth"), comic.bubbleStrokeWidth);
    object.insert(QStringLiteral("cornerRadius"), comic.cornerRadius);
    object.insert(QStringLiteral("tail"), comic.tail);
    object.insert(QStringLiteral("tailX"), comic.tailX);
    object.insert(QStringLiteral("tailY"), comic.tailY);
    object.insert(QStringLiteral("rotation"), comic.rotation);
    object.insert(QStringLiteral("padding"), comic.padding);
    object.insert(QStringLiteral("seed"), comic.seed);
    return object;
}

ComicTextContent comicFromJson(const QJsonObject& object)
{
    ComicTextContent comic;
    comic.text = detail::jsonString(object, QStringLiteral("text"), comic.text);
    comic.fontFamily = detail::jsonString(
        object, QStringLiteral("fontFamily"), comic.fontFamily);
    comic.pixelSize = detail::jsonInt(
        object, QStringLiteral("pixelSize"), comic.pixelSize);
    comic.color = detail::jsonString(object, QStringLiteral("color"), comic.color);
    comic.strokeColor = detail::jsonString(
        object, QStringLiteral("strokeColor"), comic.strokeColor);
    comic.strokeWidth = detail::jsonDouble(
        object, QStringLiteral("strokeWidth"), comic.strokeWidth);
    comic.align = detail::jsonInt(object, QStringLiteral("align"), comic.align);
    comic.lineHeight = detail::jsonDouble(
        object, QStringLiteral("lineHeight"), comic.lineHeight);
    comic.bold = detail::jsonBool(object, QStringLiteral("bold"), comic.bold);
    comic.italic = detail::jsonBool(
        object, QStringLiteral("italic"), comic.italic);
    comic.shape = detail::jsonString(
        object, QStringLiteral("shape"), comic.shape);
    comic.boxWidth = detail::jsonDouble(
        object, QStringLiteral("boxWidth"), comic.boxWidth);
    comic.boxHeight = detail::jsonDouble(
        object, QStringLiteral("boxHeight"), comic.boxHeight);
    comic.fillColor = detail::jsonString(
        object, QStringLiteral("fillColor"), comic.fillColor);
    comic.bubbleStrokeColor = detail::jsonString(
        object, QStringLiteral("bubbleStrokeColor"), comic.bubbleStrokeColor);
    comic.bubbleStrokeWidth = detail::jsonDouble(
        object, QStringLiteral("bubbleStrokeWidth"), comic.bubbleStrokeWidth);
    comic.cornerRadius = detail::jsonDouble(
        object, QStringLiteral("cornerRadius"), comic.cornerRadius);
    comic.tail = detail::jsonBool(object, QStringLiteral("tail"), comic.tail);
    comic.tailX = detail::jsonDouble(
        object, QStringLiteral("tailX"), comic.tailX);
    comic.tailY = detail::jsonDouble(
        object, QStringLiteral("tailY"), comic.tailY);
    comic.rotation = detail::jsonDouble(
        object, QStringLiteral("rotation"), comic.rotation);
    comic.padding = detail::jsonDouble(
        object, QStringLiteral("padding"), comic.padding);
    comic.seed = detail::jsonInt(object, QStringLiteral("seed"), comic.seed);
    return comic;
}

QJsonObject radialToJson(const RadialLinesContent& radial)
{
    QJsonObject object;
    object.insert(QStringLiteral("count"), radial.count);
    object.insert(QStringLiteral("radius"), radial.radius);
    object.insert(QStringLiteral("innerRatio"), radial.innerRatio);
    object.insert(QStringLiteral("width"), radial.width);
    object.insert(QStringLiteral("angle"), radial.angle);
    object.insert(QStringLiteral("color"), radial.color);
    object.insert(QStringLiteral("seed"), radial.seed);
    return object;
}

RadialLinesContent radialFromJson(const QJsonObject& object)
{
    RadialLinesContent radial;
    radial.count = detail::jsonInt(
        object, QStringLiteral("count"), radial.count);
    radial.radius = detail::jsonDouble(
        object, QStringLiteral("radius"), radial.radius);
    radial.innerRatio = detail::jsonDouble(
        object, QStringLiteral("innerRatio"), radial.innerRatio);
    radial.width = detail::jsonDouble(
        object, QStringLiteral("width"), radial.width);
    radial.angle = detail::jsonDouble(
        object, QStringLiteral("angle"), radial.angle);
    radial.color = detail::jsonString(
        object, QStringLiteral("color"), radial.color);
    radial.seed = detail::jsonInt(object, QStringLiteral("seed"), radial.seed);
    return radial;
}

QJsonObject flowToJson(const FlowLinesContent& flow)
{
    QJsonObject object;
    object.insert(QStringLiteral("count"), flow.count);
    object.insert(QStringLiteral("length"), flow.length);
    object.insert(QStringLiteral("spacing"), flow.spacing);
    object.insert(QStringLiteral("thickness"), flow.thickness);
    object.insert(QStringLiteral("area"), flow.area);
    object.insert(QStringLiteral("bend"), flow.bend);
    object.insert(QStringLiteral("angle"), flow.angle);
    object.insert(QStringLiteral("phase"), flow.phase);
    object.insert(QStringLiteral("color"), flow.color);
    object.insert(QStringLiteral("seed"), flow.seed);
    return object;
}

FlowLinesContent flowFromJson(const QJsonObject& object)
{
    FlowLinesContent flow;
    flow.count = detail::jsonInt(object, QStringLiteral("count"), flow.count);
    flow.length = detail::jsonDouble(
        object, QStringLiteral("length"), flow.length);
    flow.spacing = detail::jsonDouble(
        object, QStringLiteral("spacing"), flow.spacing);
    flow.thickness = detail::jsonDouble(
        object, QStringLiteral("thickness"), flow.thickness);
    flow.area = detail::jsonDouble(object, QStringLiteral("area"), flow.area);
    flow.bend = detail::jsonDouble(object, QStringLiteral("bend"), flow.bend);
    flow.angle = detail::jsonDouble(object, QStringLiteral("angle"), flow.angle);
    flow.phase = detail::jsonDouble(object, QStringLiteral("phase"), flow.phase);
    flow.color = detail::jsonString(
        object, QStringLiteral("color"), flow.color);
    flow.seed = detail::jsonInt(object, QStringLiteral("seed"), flow.seed);
    return flow;
}

QJsonObject halftoneToJson(const HalftoneDotsContent& halftone)
{
    QJsonObject object;
    object.insert(QStringLiteral("layout"), halftone.layout);
    object.insert(QStringLiteral("area"), halftone.area);
    object.insert(QStringLiteral("radius"), halftone.radius);
    object.insert(QStringLiteral("spacing"), halftone.spacing);
    object.insert(QStringLiteral("dotSize"), halftone.dotSize);
    object.insert(QStringLiteral("angle"), halftone.angle);
    object.insert(QStringLiteral("gradientAngle"), halftone.gradientAngle);
    object.insert(QStringLiteral("minRatio"), halftone.minRatio);
    object.insert(QStringLiteral("jitter"), halftone.jitter);
    object.insert(QStringLiteral("color"), halftone.color);
    object.insert(QStringLiteral("seed"), halftone.seed);
    return object;
}

HalftoneDotsContent halftoneFromJson(const QJsonObject& object)
{
    HalftoneDotsContent halftone;
    halftone.layout = detail::jsonString(
        object, QStringLiteral("layout"), halftone.layout);
    halftone.area = detail::jsonDouble(
        object, QStringLiteral("area"), halftone.area);
    halftone.radius = detail::jsonDouble(
        object, QStringLiteral("radius"), halftone.radius);
    halftone.spacing = detail::jsonDouble(
        object, QStringLiteral("spacing"), halftone.spacing);
    halftone.dotSize = detail::jsonDouble(
        object, QStringLiteral("dotSize"), halftone.dotSize);
    halftone.angle = detail::jsonDouble(
        object, QStringLiteral("angle"), halftone.angle);
    halftone.gradientAngle = detail::jsonDouble(
        object, QStringLiteral("gradientAngle"), halftone.gradientAngle);
    halftone.minRatio = detail::jsonDouble(
        object, QStringLiteral("minRatio"), halftone.minRatio);
    halftone.jitter = detail::jsonDouble(
        object, QStringLiteral("jitter"), halftone.jitter);
    halftone.color = detail::jsonString(
        object, QStringLiteral("color"), halftone.color);
    halftone.seed = detail::jsonInt(object, QStringLiteral("seed"), halftone.seed);
    return halftone;
}

QJsonObject brandWallToJson(const BrandWallContent& wall)
{
    QJsonObject object;
    object.insert(QStringLiteral("logos"), QJsonArray::fromStringList(wall.logos));
    object.insert(QStringLiteral("columns"), wall.columns);
    object.insert(QStringLiteral("rows"), wall.rows);
    object.insert(QStringLiteral("spacing"), wall.spacing);
    object.insert(QStringLiteral("logoSize"), wall.logoSize);
    object.insert(QStringLiteral("colors"), QJsonArray::fromStringList(wall.colors));
    object.insert(QStringLiteral("alternate"), wall.alternate);
    object.insert(QStringLiteral("angle"), wall.angle);
    object.insert(QStringLiteral("background"), wall.background);
    object.insert(QStringLiteral("seed"), wall.seed);
    return object;
}

BrandWallContent brandWallFromJson(const QJsonObject& object)
{
    BrandWallContent wall;
    wall.logos = detail::jsonStringList(object, QStringLiteral("logos"));
    wall.columns = detail::jsonInt(
        object, QStringLiteral("columns"), wall.columns);
    wall.rows = detail::jsonInt(object, QStringLiteral("rows"), wall.rows);
    wall.spacing = detail::jsonDouble(
        object, QStringLiteral("spacing"), wall.spacing);
    wall.logoSize = detail::jsonDouble(
        object, QStringLiteral("logoSize"), wall.logoSize);
    wall.colors = detail::jsonStringList(object, QStringLiteral("colors"));
    wall.alternate = detail::jsonBool(
        object, QStringLiteral("alternate"), wall.alternate);
    wall.angle = detail::jsonDouble(
        object, QStringLiteral("angle"), wall.angle);
    wall.background = detail::jsonString(
        object, QStringLiteral("background"), wall.background);
    wall.seed = detail::jsonInt(object, QStringLiteral("seed"), wall.seed);
    return wall;
}

} // namespace

QJsonObject toJson(const Transform& transform)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), transform.x);
    object.insert(QStringLiteral("y"), transform.y);
    object.insert(QStringLiteral("rotation"), transform.rotation);
    object.insert(QStringLiteral("scaleX"), transform.scaleX);
    object.insert(QStringLiteral("scaleY"), transform.scaleY);
    object.insert(QStringLiteral("pivotX"), transform.pivotX);
    object.insert(QStringLiteral("pivotY"), transform.pivotY);
    object.insert(QStringLiteral("opacity"), transform.opacity);
    return object;
}

std::optional<Transform> transformFromJson(const QJsonObject& object,
                                           QString* error)
{
    Q_UNUSED(error);
    Transform transform;
    transform.x = detail::jsonDouble(object, QStringLiteral("x"), transform.x);
    transform.y = detail::jsonDouble(object, QStringLiteral("y"), transform.y);
    transform.rotation = detail::jsonDouble(
        object, QStringLiteral("rotation"), transform.rotation);
    transform.scaleX = detail::jsonDouble(
        object, QStringLiteral("scaleX"), transform.scaleX);
    transform.scaleY = detail::jsonDouble(
        object, QStringLiteral("scaleY"), transform.scaleY);
    transform.pivotX = detail::jsonDouble(
        object, QStringLiteral("pivotX"), transform.pivotX);
    transform.pivotY = detail::jsonDouble(
        object, QStringLiteral("pivotY"), transform.pivotY);
    transform.opacity = detail::jsonDouble(
        object, QStringLiteral("opacity"), transform.opacity);
    return transform;
}

QJsonObject toJson(const Content& content)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), contentKindName(content.kind));

    switch (content.kind)
    {
    case ContentKind::Empty:
        break;
    case ContentKind::Text:
        object.insert(QStringLiteral("text"), textToJson(content.text));
        break;
    case ContentKind::Vector:
        object.insert(QStringLiteral("vector"), vectorToJson(content.vector));
        break;
    case ContentKind::Camera:
        object.insert(QStringLiteral("camera"), QJsonObject{});
        break;
    case ContentKind::Asset:
        object.insert(QStringLiteral("assetHash"), content.asset.hash);
        object.insert(QStringLiteral("assetType"),
                      assetTypeName(content.asset.type));
        object.insert(QStringLiteral("sourceIn"), content.asset.sourceIn);
        object.insert(QStringLiteral("volume"), content.asset.volume);
        break;
    case ContentKind::ComicText:
        object.insert(QStringLiteral("comicText"),
                      comicToJson(content.comic));
        break;
    case ContentKind::RadialLines:
        object.insert(QStringLiteral("radialLines"),
                      radialToJson(content.radial));
        break;
    case ContentKind::FlowLines:
        object.insert(QStringLiteral("flowLines"), flowToJson(content.flow));
        break;
    case ContentKind::HalftoneDots:
        object.insert(QStringLiteral("halftoneDots"),
                      halftoneToJson(content.halftone));
        break;
    case ContentKind::BrandWall:
        object.insert(QStringLiteral("brandWall"),
                      brandWallToJson(content.brandWall));
        break;
    }
    return object;
}

std::optional<Content> contentFromJson(const QJsonObject& object,
                                       QString* error)
{
    const QString kindName = detail::jsonString(
        object, QStringLiteral("kind"), QStringLiteral("empty"));
    const auto kind = contentKindFromName(kindName);
    if (!kind)
    {
        detail::setError(error,
                         QStringLiteral("未知内容类型：%1").arg(kindName));
        return std::nullopt;
    }

    Content content;
    content.kind = *kind;

    switch (content.kind)
    {
    case ContentKind::Empty:
        break;
    case ContentKind::Text:
    {
        const QJsonValue value = object.value(QStringLiteral("text"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("text 必须是对象"));
            return std::nullopt;
        }
        content.text = textFromJson(value.toObject());
        break;
    }
    case ContentKind::Vector:
    {
        const QJsonValue value = object.value(QStringLiteral("vector"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("vector 必须是对象"));
            return std::nullopt;
        }
        content.vector = vectorFromJson(value.toObject());
        break;
    }
    case ContentKind::Camera:
    {
        const QJsonValue value = object.value(QStringLiteral("camera"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("camera 必须是对象"));
            return std::nullopt;
        }
        break;
    }
    case ContentKind::Asset:
    {
        content.asset.hash = detail::jsonString(
            object, QStringLiteral("assetHash"));
        const QString typeName = detail::jsonString(
            object, QStringLiteral("assetType"), QStringLiteral("image"));
        const auto type = assetTypeFromName(typeName);
        if (!type)
        {
            detail::setError(error,
                             QStringLiteral("未知素材类型：%1").arg(typeName));
            return std::nullopt;
        }
        content.asset.type = *type;
        content.asset.sourceIn = detail::jsonInt(
            object, QStringLiteral("sourceIn"), content.asset.sourceIn);
        content.asset.volume = detail::jsonDouble(
            object, QStringLiteral("volume"), content.asset.volume);
        break;
    }
    case ContentKind::ComicText:
    {
        const QJsonValue value = object.value(QStringLiteral("comicText"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("comicText 必须是对象"));
            return std::nullopt;
        }
        content.comic = comicFromJson(value.toObject());
        break;
    }
    case ContentKind::RadialLines:
    {
        const QJsonValue value = object.value(QStringLiteral("radialLines"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("radialLines 必须是对象"));
            return std::nullopt;
        }
        content.radial = radialFromJson(value.toObject());
        break;
    }
    case ContentKind::FlowLines:
    {
        const QJsonValue value = object.value(QStringLiteral("flowLines"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("flowLines 必须是对象"));
            return std::nullopt;
        }
        content.flow = flowFromJson(value.toObject());
        break;
    }
    case ContentKind::HalftoneDots:
    {
        const QJsonValue value = object.value(QStringLiteral("halftoneDots"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("halftoneDots 必须是对象"));
            return std::nullopt;
        }
        content.halftone = halftoneFromJson(value.toObject());
        break;
    }
    case ContentKind::BrandWall:
    {
        const QJsonValue value = object.value(QStringLiteral("brandWall"));
        if (!value.isUndefined() && !value.isNull() && !value.isObject())
        {
            detail::setError(error, QStringLiteral("brandWall 必须是对象"));
            return std::nullopt;
        }
        content.brandWall = brandWallFromJson(value.toObject());
        break;
    }
    }

    if (!validateContent(content, error))
        return std::nullopt;
    return content;
}

bool validateContent(const Content& content, QString* error)
{
    switch (content.kind)
    {
    case ContentKind::Empty:
    case ContentKind::Text:
    case ContentKind::Vector:
    case ContentKind::Camera:
        return true;

    case ContentKind::Asset:
        if (content.asset.hash.isEmpty())
        {
            detail::setError(error, QStringLiteral("素材内容缺少 assetHash"));
            return false;
        }
        return true;

    case ContentKind::ComicText:
    {
        const ComicTextContent& comic = content.comic;
        if (!isComicShape(comic.shape))
        {
            detail::setError(error,
                             QStringLiteral("漫画文字 shape 非法：%1（必须是 "
                                            "ellipse、rect、cloud 或 burst）")
                                 .arg(comic.shape));
            return false;
        }
        if (comic.pixelSize < 1 || comic.pixelSize > 8192)
        {
            detail::setError(error,
                             QStringLiteral("漫画文字 pixelSize 必须在 1..8192 "
                                            "之间（当前 %1）")
                                 .arg(comic.pixelSize));
            return false;
        }
        if (!numberInRange(comic.boxWidth, 1.0, 1000000.0,
                           QStringLiteral("漫画文字 boxWidth"), error))
            return false;
        if (!numberInRange(comic.boxHeight, 1.0, 1000000.0,
                           QStringLiteral("漫画文字 boxHeight"), error))
            return false;
        if (!numberInRange(comic.padding, 0.0, 100000.0,
                           QStringLiteral("漫画文字 padding"), error))
            return false;
        if (!numberInRange(comic.strokeWidth, 0.0, 1000.0,
                           QStringLiteral("漫画文字 strokeWidth"), error))
            return false;
        if (!numberInRange(comic.bubbleStrokeWidth, 0.0, 1000.0,
                           QStringLiteral("漫画文字 bubbleStrokeWidth"),
                           error))
            return false;
        if (!numberInRange(comic.cornerRadius, 0.0, 1000000.0,
                           QStringLiteral("漫画文字 cornerRadius"), error))
            return false;
        if (!numberInRange(comic.lineHeight, 0.1, 10.0,
                           QStringLiteral("漫画文字 lineHeight"), error))
            return false;
        if (comic.align < 0 || comic.align > 0xffff)
        {
            detail::setError(error,
                             QStringLiteral("漫画文字 align 必须在 0..65535 "
                                            "之间（当前 %1）")
                                 .arg(comic.align));
            return false;
        }
        if (!numberInRange(comic.tailX, -16.0, 16.0,
                           QStringLiteral("漫画文字 tailX"), error))
            return false;
        if (!numberInRange(comic.tailY, -16.0, 16.0,
                           QStringLiteral("漫画文字 tailY"), error))
            return false;
        if (!finiteNumber(comic.rotation,
                          QStringLiteral("漫画文字 rotation"), error))
            return false;
        return true;
    }

    case ContentKind::RadialLines:
    {
        const RadialLinesContent& radial = content.radial;
        if (radial.count < 1 || radial.count > 512)
        {
            detail::setError(error,
                             QStringLiteral("径向线 count 必须在 1..512 之间"
                                            "（当前 %1）")
                                 .arg(radial.count));
            return false;
        }
        if (!numberInRange(radial.radius, 1.0, 1000000.0,
                           QStringLiteral("径向线 radius"), error))
            return false;
        if (!numberInRange(radial.innerRatio, 0.0, 0.95,
                           QStringLiteral("径向线 innerRatio"), error))
            return false;
        if (!numberInRange(radial.width, 0.1, 100000.0,
                           QStringLiteral("径向线 width"), error))
            return false;
        if (!finiteNumber(radial.angle, QStringLiteral("径向线 angle"), error))
            return false;
        return true;
    }

    case ContentKind::FlowLines:
    {
        const FlowLinesContent& flow = content.flow;
        if (flow.count < 1 || flow.count > 512)
        {
            detail::setError(error,
                             QStringLiteral("流线 count 必须在 1..512 之间"
                                            "（当前 %1）")
                                 .arg(flow.count));
            return false;
        }
        if (!numberInRange(flow.length, 1.0, 1000000.0,
                           QStringLiteral("流线 length"), error))
            return false;
        if (!numberInRange(flow.spacing, 0.1, 1000000.0,
                           QStringLiteral("流线 spacing"), error))
            return false;
        if (!numberInRange(flow.thickness, 0.1, 100000.0,
                           QStringLiteral("流线 thickness"), error))
            return false;
        if (!numberInRange(flow.area, 1.0, 10000000.0,
                           QStringLiteral("流线 area"), error))
            return false;
        if (!numberInRange(flow.bend, -1000000.0, 1000000.0,
                           QStringLiteral("流线 bend"), error))
            return false;
        if (!numberInRange(flow.phase, 0.0, 1.0,
                           QStringLiteral("流线 phase"), error))
            return false;
        if (!finiteNumber(flow.angle, QStringLiteral("流线 angle"), error))
            return false;
        return true;
    }

    case ContentKind::HalftoneDots:
    {
        const HalftoneDotsContent& halftone = content.halftone;
        if (!isHalftoneLayout(halftone.layout))
        {
            detail::setError(error,
                             QStringLiteral("点阵 layout 非法：%1（必须是 grid "
                                            "或 radial）")
                                 .arg(halftone.layout));
            return false;
        }
        if (!numberInRange(halftone.area, 1.0, 10000000.0,
                           QStringLiteral("点阵 area"), error))
            return false;
        if (!numberInRange(halftone.radius, 1.0, 1000000.0,
                           QStringLiteral("点阵 radius"), error))
            return false;
        if (!numberInRange(halftone.spacing, 0.1, 100000.0,
                           QStringLiteral("点阵 spacing"), error))
            return false;
        if (!numberInRange(halftone.dotSize, 0.1, 100000.0,
                           QStringLiteral("点阵 dotSize"), error))
            return false;
        if (!numberInRange(halftone.minRatio, 0.0, 1.0,
                           QStringLiteral("点阵 minRatio"), error))
            return false;
        if (!numberInRange(halftone.jitter, 0.0, 1.0,
                           QStringLiteral("点阵 jitter"), error))
            return false;
        if (!finiteNumber(halftone.angle, QStringLiteral("点阵 angle"), error))
            return false;
        if (!finiteNumber(halftone.gradientAngle,
                          QStringLiteral("点阵 gradientAngle"), error))
            return false;
        if (halftone.layout == QLatin1String("radial")
            && halftone.radius / halftone.spacing > 256.0)
        {
            detail::setError(error,
                             QStringLiteral("点阵过密：radius/spacing 不能超过 "
                                            "256（当前 %1）")
                                 .arg(halftone.radius / halftone.spacing));
            return false;
        }
        if (halftone.layout != QLatin1String("radial")
            && halftone.area / halftone.spacing > 512.0)
        {
            detail::setError(error,
                             QStringLiteral("点阵过密：area/spacing 不能超过 "
                                            "512（当前 %1）")
                                 .arg(halftone.area / halftone.spacing));
            return false;
        }
        return true;
    }

    case ContentKind::BrandWall:
    {
        const BrandWallContent& wall = content.brandWall;
        if (wall.columns < 1 || wall.columns > 64)
        {
            detail::setError(error,
                             QStringLiteral("品牌墙 columns 必须在 1..64 之间"
                                            "（当前 %1）")
                                 .arg(wall.columns));
            return false;
        }
        if (wall.rows < 1 || wall.rows > 64)
        {
            detail::setError(error,
                             QStringLiteral("品牌墙 rows 必须在 1..64 之间"
                                            "（当前 %1）")
                                 .arg(wall.rows));
            return false;
        }
        if (!numberInRange(wall.spacing, 0.0, 512.0,
                           QStringLiteral("品牌墙 spacing"), error))
            return false;
        if (!numberInRange(wall.logoSize, 1.0, 1024.0,
                           QStringLiteral("品牌墙 logoSize"), error))
            return false;
        if (!numberInRange(wall.angle, -360.0, 360.0,
                           QStringLiteral("品牌墙 angle"), error))
            return false;
        if (wall.logos.size() > 64)
        {
            detail::setError(error,
                             QStringLiteral("品牌墙 logos 最多 64 个（当前 %1）")
                                 .arg(wall.logos.size()));
            return false;
        }
        for (const QString& logo : wall.logos)
        {
            if (logo.isEmpty() || logo.size() > 128)
            {
                detail::setError(error,
                                 QStringLiteral("品牌墙 logos 每项必须在 "
                                                "1..128 字之间"));
                return false;
            }
        }
        if (wall.colors.isEmpty())
        {
            detail::setError(error,
                             QStringLiteral("品牌墙 colors 至少需要一个颜色"));
            return false;
        }
        for (const QString& color : wall.colors)
        {
            if (!isColorValue(color))
            {
                detail::setError(error,
                                 QStringLiteral("品牌墙 colors 颜色非法：%1"
                                                "（需 #rgb、#rrggbb 或 "
                                                "#aarrggbb）")
                                     .arg(color));
                return false;
            }
        }
        if (!wall.background.isEmpty() && !isColorValue(wall.background))
        {
            detail::setError(error,
                             QStringLiteral("品牌墙 background 颜色非法：%1"
                                            "（需 #rgb、#rrggbb 或 "
                                            "#aarrggbb）")
                                 .arg(wall.background));
            return false;
        }
        return true;
    }
    }
    return true;
}

} // namespace pvf::model
