#include "mcp2/EntityToolsInternal.h"

#include <QJsonArray>
#include <QSet>

#include <cmath>
#include <optional>

namespace pvf::mcp2::detail::entity {

namespace {

// ---- special content fields ---------------------------------------------
// comicText / radialLines / flowLines / halftoneDots / brandWall live in the
// entity content as a same-named sub-object.  mcp2 checks the field names and
// JSON types here; model::contentFromJson validates the numeric ranges.

struct SpecialContentField
{
    const char* name;
    const char* type; // string | number | integer | boolean | string[]
};

QString specialContentContainer(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::ComicText:
        return QStringLiteral("comicText");
    case ContentKind::RadialLines:
        return QStringLiteral("radialLines");
    case ContentKind::FlowLines:
        return QStringLiteral("flowLines");
    case ContentKind::HalftoneDots:
        return QStringLiteral("halftoneDots");
    case ContentKind::BrandWall:
        return QStringLiteral("brandWall");
    default:
        return QString();
    }
}

const QVector<SpecialContentField>& specialContentFields(ContentKind kind)
{
    static const QVector<SpecialContentField> comic = {
        {"text", "string"},          {"fontFamily", "string"},
        {"pixelSize", "integer"},    {"color", "string"},
        {"strokeColor", "string"},   {"strokeWidth", "number"},
        {"align", "integer"},        {"lineHeight", "number"},
        {"bold", "boolean"},         {"italic", "boolean"},
        {"shape", "string"},         {"boxWidth", "number"},
        {"boxHeight", "number"},     {"fillColor", "string"},
        {"bubbleStrokeColor", "string"}, {"bubbleStrokeWidth", "number"},
        {"cornerRadius", "number"},  {"tail", "boolean"},
        {"tailX", "number"},         {"tailY", "number"},
        {"rotation", "number"},      {"padding", "number"},
        {"seed", "integer"},
    };
    static const QVector<SpecialContentField> radial = {
        {"count", "integer"},  {"radius", "number"},
        {"innerRatio", "number"}, {"width", "number"},
        {"angle", "number"},   {"color", "string"},
        {"seed", "integer"},
    };
    static const QVector<SpecialContentField> flow = {
        {"count", "integer"},   {"length", "number"},
        {"spacing", "number"},  {"thickness", "number"},
        {"area", "number"},     {"bend", "number"},
        {"angle", "number"},    {"phase", "number"},
        {"color", "string"},    {"seed", "integer"},
    };
    static const QVector<SpecialContentField> halftone = {
        {"layout", "string"},        {"area", "number"},
        {"radius", "number"},        {"spacing", "number"},
        {"dotSize", "number"},       {"angle", "number"},
        {"gradientAngle", "number"}, {"minRatio", "number"},
        {"jitter", "number"},        {"color", "string"},
        {"seed", "integer"},
    };
    static const QVector<SpecialContentField> brandWall = {
        {"logos", "string[]"},   {"columns", "integer"},
        {"rows", "integer"},     {"spacing", "number"},
        {"logoSize", "number"},  {"colors", "string[]"},
        {"alternate", "boolean"}, {"angle", "number"},
        {"background", "string"}, {"seed", "integer"},
    };
    static const QVector<SpecialContentField> none;

    switch (kind)
    {
    case ContentKind::ComicText:
        return comic;
    case ContentKind::RadialLines:
        return radial;
    case ContentKind::FlowLines:
        return flow;
    case ContentKind::HalftoneDots:
        return halftone;
    case ContentKind::BrandWall:
        return brandWall;
    default:
        return none;
    }
}

bool matchesFieldType(const QJsonValue& value, const QString& type)
{
    if (type == QLatin1String("string"))
        return value.isString();
    if (type == QLatin1String("boolean"))
        return value.isBool();
    if (type == QLatin1String("number"))
        return value.isDouble() && std::isfinite(value.toDouble());
    if (type == QLatin1String("integer"))
    {
        if (!value.isDouble())
            return false;
        const double number = value.toDouble();
        return std::isfinite(number) && std::floor(number) == number;
    }
    if (type == QLatin1String("string[]"))
    {
        if (!value.isArray())
            return false;
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array)
        {
            if (!item.isString())
                return false;
        }
        return true;
    }
    return false;
}

Result validateSpecialContentFields(const QJsonObject& object, ContentKind kind)
{
    const QString container = specialContentContainer(kind);
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (it.key() != QLatin1String("kind") && it.key() != container)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("content 含未知字段 %1（kind=%2 只接受 "
                                          "kind 与 %3）")
                               .arg(it.key(), contentKindName(kind), container));
    }

    const QJsonValue nestedValue = object.value(container);
    if (nestedValue.isUndefined() || nestedValue.isNull())
        return success();
    if (!nestedValue.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("content.%1 必须是对象").arg(container));

    const QVector<SpecialContentField>& fields = specialContentFields(kind);
    QStringList allowed;
    for (const SpecialContentField& field : fields)
        allowed << QString::fromLatin1(field.name);

    const QJsonObject nested = nestedValue.toObject();
    for (auto it = nested.constBegin(); it != nested.constEnd(); ++it)
    {
        const SpecialContentField* match = nullptr;
        for (const SpecialContentField& field : fields)
        {
            if (it.key() == QLatin1String(field.name))
            {
                match = &field;
                break;
            }
        }

        if (!match)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("content.%1 含未知字段 %2（允许：%3）")
                               .arg(container, it.key(),
                                    allowed.join(QStringLiteral(", "))));

        const QString type = QString::fromLatin1(match->type);
        if (!matchesFieldType(it.value(), type))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("content.%1.%2 必须是 %3")
                               .arg(container, it.key(), type));
    }
    return success();
}

Result validateContentFieldsForCreate(const QJsonObject& object)
{
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString())
        return success();
    return validateContentFieldsJson(object, kindValue.toString());
}

} // namespace

std::optional<ContentKind> specialContentKindFromName(const QString& name)
{
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

Result validateContentFieldsJson(const QJsonObject& object,
                                 const QString& kindName)
{
    const auto kind = specialContentKindFromName(kindName);
    if (!kind)
        return success();
    return validateSpecialContentFields(object, *kind);
}

int assetDurationFrames(const assets::StoredAsset& asset)
{
    return asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
}

Result checkAssetDuration(const assets::AssetStore& assetStore,
                          const model::Content& content, int duration)
{
    if (content.kind != ContentKind::Asset)
        return success();
    if (content.asset.type != AssetType::Video
        && content.asset.type != AssetType::Audio)
        return success();

    const auto asset = assetStore.get(content.asset.hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(content.asset.hash));
    const int frames = assetDurationFrames(*asset);
    if (frames <= 0)
        return success();
    if (content.asset.sourceIn >= frames)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("sourceIn 超出素材长度（%1 帧）").arg(frames));
    if (content.asset.sourceIn + duration > frames)
        return failure(
            QStringLiteral("invalid_argument"),
            QStringLiteral("实体时长 %1 超出素材剩余长度 %2")
                .arg(duration)
                .arg(frames - content.asset.sourceIn));
    return success();
}

Result parseContentForCreate(const assets::AssetStore& assetStore,
                             const QJsonValue& value, model::Content* out)
{
    if (value.isUndefined() || value.isNull())
    {
        *out = model::Content();
        return success();
    }
    if (!value.isObject())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("content 必须是对象"));

    const QJsonObject object = value.toObject();
    Result fieldCheck = validateContentFieldsForCreate(object);
    if (!fieldCheck.ok)
        return fieldCheck;

    QString error;
    const auto parsed = model::contentFromJson(object, &error);
    if (!parsed)
        return failure(QStringLiteral("invalid_argument"), error);

    *out = *parsed;
    if (out->kind != ContentKind::Asset)
        return success();

    const auto asset = assetStore.get(out->asset.hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(out->asset.hash));
    out->asset.type = asset->type;
    if (out->asset.sourceIn < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("sourceIn 不能小于 0"));
    if (!isFiniteNumber(out->asset.volume) || out->asset.volume < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("volume 必须是非负有限数"));
    return success();
}

QString defaultNameForContent(const assets::AssetStore& assetStore,
                              const model::Content& content)
{
    switch (content.kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("实体");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("形状");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        if (const auto asset = assetStore.get(content.asset.hash))
            return asset->name;
        return QStringLiteral("素材");
    case ContentKind::ComicText:
        return QStringLiteral("漫画文字");
    case ContentKind::RadialLines:
        return QStringLiteral("集中线");
    case ContentKind::FlowLines:
        return QStringLiteral("速度线");
    case ContentKind::HalftoneDots:
        return QStringLiteral("网点");
    case ContentKind::BrandWall:
        return QStringLiteral("品牌墙");
    }
    return QStringLiteral("实体");
}

} // namespace pvf::mcp2::detail::entity
