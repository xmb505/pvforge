#include "mcp2/EntityToolsInternal.h"

#include <QJsonObject>

namespace pvf::mcp2::detail::entity {

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

    QString error;
    const auto parsed = model::contentFromJson(value.toObject(), &error);
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
    }
    return QStringLiteral("实体");
}

} // namespace pvf::mcp2::detail::entity
