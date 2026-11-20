#include "Content.h"

#include "JsonUtil.h"

#include <QJsonValue>

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
    return std::nullopt;
}

bool finiteNumber(double value, const QString& label, QString* error)
{
    if (std::isfinite(value))
        return true;
    detail::setError(error,
                     QStringLiteral("%1 必须是有限数值").arg(label));
    return false;
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

bool readNumberObject(const QJsonObject& object, const QString& label,
                      QJsonObject* out, QString* error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (it.key().isEmpty())
        {
            detail::setError(error,
                             QStringLiteral("%1 的参数名不能为空").arg(label));
            return false;
        }
        if (!it.value().isDouble())
        {
            detail::setError(
                error,
                QStringLiteral("%1 的参数 %2 必须是数值").arg(label, it.key()));
            return false;
        }
        const double value = it.value().toDouble();
        if (!std::isfinite(value))
        {
            detail::setError(
                error,
                QStringLiteral("%1 的参数 %2 必须是有限数值").arg(label, it.key()));
            return false;
        }
        out->insert(it.key(), value);
    }
    return true;
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
        object.insert(QStringLiteral("fadeIn"), content.asset.fadeIn);
        object.insert(QStringLiteral("fadeOut"), content.asset.fadeOut);
        break;
    }
    detail::mergeExtras(object, content.extras);
    return object;
}

std::optional<Content> contentFromJson(const QJsonObject& object,
                                       const ParseOptions& options,
                                       QString* error)
{
    const QString kindName = detail::optionalString(
        object, QStringLiteral("kind"), QStringLiteral("empty"), options);
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
        content.asset.fadeIn = detail::jsonDouble(
            object, QStringLiteral("fadeIn"), content.asset.fadeIn);
        content.asset.fadeOut = detail::jsonDouble(
            object, QStringLiteral("fadeOut"), content.asset.fadeOut);
        break;
    }
    }

    QStringList knownKeys{QStringLiteral("kind")};
    switch (content.kind)
    {
    case ContentKind::Empty:
        break;
    case ContentKind::Text:
        knownKeys << QStringLiteral("text");
        break;
    case ContentKind::Vector:
        knownKeys << QStringLiteral("vector");
        break;
    case ContentKind::Camera:
        knownKeys << QStringLiteral("camera");
        break;
    case ContentKind::Asset:
        knownKeys << QStringLiteral("assetHash") << QStringLiteral("assetType")
                  << QStringLiteral("sourceIn") << QStringLiteral("volume")
                  << QStringLiteral("fadeIn") << QStringLiteral("fadeOut");
        break;
    }
    content.extras = detail::extrasFrom(object, knownKeys);

    if (!validateContent(content, error))
        return std::nullopt;
    return content;
}

std::optional<Content> contentFromJson(const QJsonObject& object, QString* error)
{
    return contentFromJson(object, ParseOptions{}, error);
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
        if (!std::isfinite(content.asset.fadeIn)
            || !std::isfinite(content.asset.fadeOut)
            || content.asset.fadeIn < 0.0 || content.asset.fadeOut < 0.0
            || content.asset.fadeIn > 36000.0
            || content.asset.fadeOut > 36000.0)
        {
            detail::setError(
                error,
                QStringLiteral("音频淡入/淡出必须是 0..36000 秒之间的有限值"));
            return false;
        }
        if (!finiteNumber(content.asset.volume, QStringLiteral("音量"), error))
            return false;
        return true;
    }
    return true;
}

QStringList animationKeys(const Content& content)
{
    switch (content.kind)
    {
    case ContentKind::Text:
        return {QStringLiteral("pixelSize"), QStringLiteral("lineHeight"),
                QStringLiteral("letterSpacing"), QStringLiteral("padding"),
                QStringLiteral("boxWidth"), QStringLiteral("boxHeight"),
                QStringLiteral("strokeWidth")};
    case ContentKind::Vector:
        return {QStringLiteral("width"), QStringLiteral("height"),
                QStringLiteral("cornerRadius"), QStringLiteral("strokeWidth")};
    case ContentKind::Asset:
        if (content.asset.type == AssetType::Audio)
            return {QStringLiteral("volume")};
        return {};
    case ContentKind::Empty:
    case ContentKind::Camera:
        return {};
    }
    return {};
}

bool isAnimationKey(const Content& content, const QString& key)
{
    return animationKeys(content).contains(key);
}

QJsonObject animationValues(const Content& content)
{
    QJsonObject values;
    switch (content.kind)
    {
    case ContentKind::Text:
        values.insert(QStringLiteral("pixelSize"), double(content.text.pixelSize));
        values.insert(QStringLiteral("lineHeight"), content.text.lineHeight);
        values.insert(QStringLiteral("letterSpacing"), content.text.letterSpacing);
        values.insert(QStringLiteral("padding"), content.text.padding);
        values.insert(QStringLiteral("boxWidth"), content.text.boxWidth);
        values.insert(QStringLiteral("boxHeight"), content.text.boxHeight);
        values.insert(QStringLiteral("strokeWidth"), content.text.strokeWidth);
        break;
    case ContentKind::Vector:
        values.insert(QStringLiteral("width"), content.vector.width);
        values.insert(QStringLiteral("height"), content.vector.height);
        values.insert(QStringLiteral("cornerRadius"), content.vector.cornerRadius);
        values.insert(QStringLiteral("strokeWidth"), content.vector.strokeWidth);
        break;
    case ContentKind::Asset:
        if (content.asset.type == AssetType::Audio)
            values.insert(QStringLiteral("volume"), content.asset.volume);
        break;
    case ContentKind::Empty:
    case ContentKind::Camera:
        break;
    }
    return values;
}

void applyAnimationValues(const QJsonObject& values, Content* content)
{
    if (!content)
        return;

    const auto number = [&values](const QString& key,
                                  double fallback) -> double {
        const QJsonValue value = values.value(key);
        return value.isDouble() ? value.toDouble() : fallback;
    };

    switch (content->kind)
    {
    case ContentKind::Text:
        content->text.pixelSize = std::max(
            1, int(std::lround(number(QStringLiteral("pixelSize"),
                                      double(content->text.pixelSize)))));
        content->text.lineHeight = std::max(
            0.01, number(QStringLiteral("lineHeight"), content->text.lineHeight));
        content->text.letterSpacing = number(QStringLiteral("letterSpacing"),
                                             content->text.letterSpacing);
        content->text.padding = std::max(
            0.0, number(QStringLiteral("padding"), content->text.padding));
        content->text.boxWidth = std::max(
            0.0, number(QStringLiteral("boxWidth"), content->text.boxWidth));
        content->text.boxHeight = std::max(
            0.0, number(QStringLiteral("boxHeight"), content->text.boxHeight));
        content->text.strokeWidth = std::max(
            0.0, number(QStringLiteral("strokeWidth"), content->text.strokeWidth));
        break;
    case ContentKind::Vector:
        content->vector.width = std::max(
            1.0, number(QStringLiteral("width"), content->vector.width));
        content->vector.height = std::max(
            1.0, number(QStringLiteral("height"), content->vector.height));
        content->vector.cornerRadius = std::max(
            0.0, number(QStringLiteral("cornerRadius"),
                        content->vector.cornerRadius));
        content->vector.strokeWidth = std::max(
            0.0, number(QStringLiteral("strokeWidth"),
                        content->vector.strokeWidth));
        break;
    case ContentKind::Asset:
        if (content->asset.type == AssetType::Audio)
            content->asset.volume = std::max(
                0.0, number(QStringLiteral("volume"), content->asset.volume));
        break;
    case ContentKind::Empty:
    case ContentKind::Camera:
        break;
    }
}

bool validateKeyframeContent(const Content& content, const QJsonObject& values,
                             QString* error)
{
    const QStringList allowed = animationKeys(content);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            detail::setError(
                error,
                QStringLiteral("关键帧内容参数 %1 不是该实体类型的动画参数")
                    .arg(it.key()));
            return false;
        }
        if (!it.value().isDouble() || !std::isfinite(it.value().toDouble()))
        {
            detail::setError(
                error,
                QStringLiteral("关键帧内容参数 %1 必须是有限数值").arg(it.key()));
            return false;
        }
    }
    return true;
}

bool validateKeyframeHtmlVars(const QJsonObject& values, QString* error)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        if (it.key().isEmpty())
        {
            detail::setError(error, QStringLiteral("HTML 变量名不能为空"));
            return false;
        }
        if (!it.value().isDouble() || !std::isfinite(it.value().toDouble()))
        {
            detail::setError(
                error,
                QStringLiteral("HTML 变量 %1 必须是有限数值").arg(it.key()));
            return false;
        }
    }
    return true;
}

} // namespace pvf::model
