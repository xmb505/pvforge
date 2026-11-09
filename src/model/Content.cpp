#include "Content.h"

#include "JsonUtil.h"

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
    return std::nullopt;
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
    }

    return content;
}

} // namespace pvf::model
