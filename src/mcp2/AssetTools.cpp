#include "mcp2/Internal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <optional>

namespace pvf::mcp2::detail {

namespace {

constexpr int kMaxBatch = 200;

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

bool writeAssetName(const assets::StoredAsset& asset, const QString& name,
                    Result* error)
{
    const QString path = asset.directory + QStringLiteral("/asset.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = failure(QStringLiteral("internal_error"),
                         QStringLiteral("无法读取 %1").arg(path));
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject())
    {
        *error = failure(QStringLiteral("internal_error"),
                         QStringLiteral("%1 不是合法素材元数据").arg(path));
        return false;
    }

    QJsonObject metadata = document.object();
    metadata.insert(QStringLiteral("name"), name);

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        *error = failure(QStringLiteral("internal_error"),
                         QStringLiteral("无法写入 %1").arg(path));
        return false;
    }
    const QByteArray bytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit())
    {
        *error = failure(QStringLiteral("internal_error"),
                         QStringLiteral("无法写入 %1").arg(path));
        return false;
    }
    return true;
}

bool isVisualGroup(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
           || type == AssetType::Html;
}

bool isTimed(AssetType type)
{
    return type == AssetType::Video || type == AssetType::Audio;
}

int assetFrames(const assets::StoredAsset& asset)
{
    return asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
}

QJsonArray usedBy(const model::Project& project, const AssetHash& hash)
{
    QJsonArray array;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        if (it.value().content.kind == ContentKind::Asset
            && it.value().content.asset.hash == hash)
            array.append(it.key());
    }
    return array;
}

Result applyAsset(Tx& tx, model::Entity& entity, const assets::StoredAsset& asset,
                  int sourceIn, double volume, int* removedKeyframes)
{
    if (isTimed(asset.type))
    {
        const int frames = assetFrames(asset);
        if (frames > 0)
        {
            if (sourceIn < 0 || sourceIn >= frames)
                return failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("sourceIn 超出素材长度 %1").arg(frames));
            const int available = frames - sourceIn;
            if (entity.duration() > available)
            {
                TrimSummary summary;
                Result trimmed = trimEntityRange(tx, entity.uuid, entity.start,
                                                 entity.start + available, &summary);
                if (!trimmed.ok)
                    return trimmed;
                if (!summary.deletedUuids.isEmpty())
                    return failure(
                        QStringLiteral("conflict"),
                        QStringLiteral("换素材会删除子实体，请先裁剪子实体"));
                *removedKeyframes = summary.removedKeyframes;
                model::Entity* refreshed = findEntity(tx.p(), entity.uuid);
                if (!refreshed)
                    return failure(QStringLiteral("internal_error"),
                                   QStringLiteral("实体在裁剪后丢失"));
                entity = *refreshed;
            }
        }
    }

    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = asset.hash;
    entity.content.asset.type = asset.type;
    entity.content.asset.sourceIn = sourceIn;
    entity.content.asset.volume = volume;
    return success();
}

} // namespace

Result assetImport(Tx& tx, const QJsonObject& args)
{
    static const QStringList allowed = {
        QStringLiteral("path"), QStringLiteral("name"),
        QStringLiteral("type"), QStringLiteral("extractAudio"),
    };
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知参数 %1").arg(it.key()));
    }

    Result error;
    QString path;
    if (!readString(args, "path", &path, &error, true))
        return error;
    const QFileInfo source(path);
    if (path.isEmpty() || path.startsWith(QLatin1Char('~'))
        || !source.isAbsolute())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("path 必须是本地绝对路径"));
    if (!source.exists())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("路径不存在：%1").arg(path));

    QString name;
    if (!readString(args, "name", &name, &error))
        return error;
    if (args.contains(QStringLiteral("name"))
        && (name.isEmpty() || name.size() > 256))
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("name 必须是 1..256 字符"));

    QString typeName;
    std::optional<AssetType> requestedType;
    if (args.contains(QStringLiteral("type")))
    {
        if (!readString(args, "type", &typeName, &error, true))
            return error;
        requestedType = assetTypeFromName(typeName);
        if (!requestedType)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知素材类型：%1").arg(typeName));
    }

    bool extractAudio = true;
    if (!readBool(args, "extractAudio", &extractAudio, &error))
        return error;

    const bool html = requestedType ? *requestedType == AssetType::Html
                                    : (source.isDir()
                                       || source.suffix().compare(
                                              QStringLiteral("zip"), Qt::CaseInsensitive) == 0
                                       || source.suffix().compare(
                                              QStringLiteral("pvhtml"), Qt::CaseInsensitive) == 0);
    if (!html)
    {
        const auto detected = assets::detectType(path);
        if (!detected)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("无法识别素材类型：%1").arg(path));
        if (requestedType && *requestedType != *detected)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("type 与文件实际类型不一致"));
        if (*detected == AssetType::Html)
            return failure(QStringLiteral("not_available"),
                           QStringLiteral("HTML 必须使用 .pvhtml 目录包或 zip"));
    }

    QSet<AssetHash> before;
    for (const assets::StoredAsset& asset : tx.store->assets().list())
        before.insert(asset.hash);

    QString importError;
    std::optional<assets::StoredAsset> stored;
    std::optional<assets::StoredAsset> extracted;
    QString audioError;

    if (html)
    {
        stored = tx.store->assets().importPvhtml(path, &importError);
    }
    else if (extractAudio && assets::detectType(path) == AssetType::Video)
    {
        const auto imported = tx.store->assets().importVideoWithAudio(path, &importError);
        if (imported)
        {
            stored = imported->video;
            extracted = imported->audio;
            audioError = imported->audioError;
        }
    }
    else
    {
        stored = tx.store->assets().importFile(path, &importError);
    }

    if (!stored)
        return failure(QStringLiteral("invalid_argument"), importError);

    if (!audioError.isEmpty())
    {
        for (const assets::StoredAsset& asset : tx.store->assets().list())
        {
            if (!before.contains(asset.hash))
            {
                QString ignored;
                tx.store->assets().remove(asset.hash, &ignored);
            }
        }
        return failure(QStringLiteral("not_available"),
                       QStringLiteral("视频拆音频失败：%1").arg(audioError));
    }

    const bool deduplicated = before.contains(stored->hash);
    if (!name.isEmpty() && !deduplicated)
    {
        if (!writeAssetName(*stored, name, &error))
            return error;
        stored = tx.store->assets().get(stored->hash);
        if (!stored)
            return failure(QStringLiteral("internal_error"),
                           QStringLiteral("写入素材名后素材丢失"));
    }

    const bool extractedAudio = extracted.has_value();
    if (extracted && !before.contains(extracted->hash))
    {
        const QString audioName = stored->name + QStringLiteral(" 音频");
        if (!writeAssetName(*extracted, audioName, &error))
            return error;
        extracted = tx.store->assets().get(extracted->hash);
    }

    QJsonObject data;
    data.insert(QStringLiteral("asset"),
                assetJson(tx.store->assets(), tx.p(), *stored));
    data.insert(QStringLiteral("audioAsset"),
                extracted ? QJsonValue(assetJson(tx.store->assets(), tx.p(), *extracted))
                          : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("deduplicated"), deduplicated);
    data.insert(QStringLiteral("extractedAudio"), extractedAudio);
    return tx.finish(data, QStringLiteral("pvf: import asset \"%1\"")
                               .arg(stored->name));
}

Result assetList(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString typeName;
    if (!readString(args, "type", &typeName, &error))
        return error;
    std::optional<AssetType> type;
    if (!typeName.isEmpty())
    {
        type = assetTypeFromName(typeName);
        if (!type)
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("未知素材类型：%1").arg(typeName));
    }

    QString query;
    if (!readString(args, "query", &query, &error))
        return error;

    int offset = 0;
    int limit = 100;
    if (!readInt(args, "offset", &offset, &error)
        || !readInt(args, "limit", &limit, &error))
        return error;
    if (offset < 0 || limit < 1 || limit > 500)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("分页参数非法（limit 1..500）"));

    QVector<assets::StoredAsset> assets;
    for (const assets::StoredAsset& asset : tx.store->assets().list())
    {
        if (type && asset.type != *type)
            continue;
        if (!query.isEmpty()
            && !asset.name.contains(query, Qt::CaseInsensitive))
            continue;
        assets.append(asset);
    }

    QJsonArray array;
    for (int i = offset; i < assets.size() && i < offset + limit; ++i)
        array.append(assetJson(tx.store->assets(), tx.p(), assets.at(i)));

    QJsonObject data;
    data.insert(QStringLiteral("assets"), array);
    data.insert(QStringLiteral("page"), pageJson(offset, limit, assets.size()));
    return success(data);
}

Result assetGet(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString hash;
    if (!readString(args, "hash", &hash, &error, true))
        return error;
    const auto asset = tx.store->assets().get(hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(hash));
    return success(assetJson(tx.store->assets(), tx.p(), *asset));
}

Result assetAssign(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "entityUuid", &uuid, &error, true))
        return error;
    QString hash;
    if (!readString(args, "assetHash", &hash, &error, true))
        return error;

    const auto asset = tx.store->assets().get(hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(hash));

    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    if (!ensureUnlocked(tx.p(), uuid, &error))
        return error;

    const ContentKind current = entity->content.kind;
    if (current == ContentKind::Camera)
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("摄像机不能换素材"));
    if (current == ContentKind::Text || current == ContentKind::Vector)
        return failure(QStringLiteral("conflict"),
                       QStringLiteral("请先把实体内容改为 empty 再换素材"));
    if (current == ContentKind::Asset)
    {
        const AssetType previous = entity->content.asset.type;
        const bool compatible =
            previous == AssetType::Audio
                ? asset->type == AssetType::Audio
                : isVisualGroup(previous) && isVisualGroup(asset->type);
        if (!compatible)
            return failure(QStringLiteral("conflict"),
                           QStringLiteral("素材类型不兼容"));
    }

    int sourceIn = current == ContentKind::Asset
                       ? entity->content.asset.sourceIn
                       : 0;
    if (!readInt(args, "sourceIn", &sourceIn, &error))
        return error;
    if (sourceIn < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("sourceIn 不能小于 0"));

    double volume = current == ContentKind::Asset
                        ? entity->content.asset.volume
                        : 1.0;
    if (!readDouble(args, "volume", &volume, &error))
        return error;
    if (volume < 0)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("volume 不能小于 0"));

    if (!isTimed(asset->type))
        sourceIn = 0;

    int removedKeyframes = 0;
    Result applied = applyAsset(tx, *entity, *asset, sourceIn, volume,
                                &removedKeyframes);
    if (!applied.ok)
        return applied;

    entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("internal_error"),
                       QStringLiteral("实体在换素材后丢失"));
    tx.touch(uuid);

    QJsonObject adjusted;
    adjusted.insert(QStringLiteral("start"), entity->start);
    adjusted.insert(QStringLiteral("end"), entity->end);
    adjusted.insert(QStringLiteral("duration"), entity->duration());
    adjusted.insert(QStringLiteral("sourceIn"), entity->content.asset.sourceIn);
    adjusted.insert(QStringLiteral("removedKeyframes"), removedKeyframes);

    QJsonObject data;
    data.insert(QStringLiteral("entity"), entityBrief(*entity));
    data.insert(QStringLiteral("asset"),
                assetJson(tx.store->assets(), tx.p(), *asset));
    data.insert(QStringLiteral("adjusted"), adjusted);
    return tx.finish(data, QStringLiteral("pvf: assign asset \"%1\" to \"%2\"")
                               .arg(asset->name, entity->name));
}

Result assetUnassign(Tx& tx, const QJsonObject& args)
{
    Result error;
    Uuid uuid;
    if (!readUuid(args, "entityUuid", &uuid, &error, true))
        return error;
    model::Entity* entity = findEntity(tx.p(), uuid);
    if (!entity)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体不存在：%1").arg(uuid));
    if (!ensureUnlocked(tx.p(), uuid, &error))
        return error;
    if (entity->content.kind != ContentKind::Asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("实体当前不是素材内容"));

    const AssetHash previous = entity->content.asset.hash;
    entity->content.kind = ContentKind::Empty;
    tx.touch(uuid);

    QJsonObject data;
    data.insert(QStringLiteral("entity"), entityBrief(*entity));
    data.insert(QStringLiteral("previousAssetHash"), previous);
    return tx.finish(data, QStringLiteral("pvf: unassign asset from \"%1\"")
                               .arg(entity->name));
}

Result assetRename(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString hash;
    if (!readString(args, "hash", &hash, &error, true))
        return error;
    QString name;
    if (!readString(args, "name", &name, &error, true))
        return error;
    if (name.isEmpty() || name.size() > 256)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("name 必须是 1..256 字符"));

    const auto asset = tx.store->assets().get(hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(hash));
    if (!writeAssetName(*asset, name, &error))
        return error;

    QJsonObject data;
    data.insert(QStringLiteral("hash"), hash);
    data.insert(QStringLiteral("name"), name);
    return tx.finish(data, QStringLiteral("pvf: rename asset \"%1\"").arg(name));
}

Result assetSetTags(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString hash;
    if (!readString(args, "hash", &hash, &error, true))
        return error;

    const auto asset = tx.store->assets().get(hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(hash));

    const QJsonValue tagsValue = args.value(QStringLiteral("tags"));
    if (!tagsValue.isArray())
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("tags 必须是字符串数组"));
    QStringList tags;
    for (const QJsonValue& value : tagsValue.toArray())
    {
        if (!value.isString())
            return failure(QStringLiteral("invalid_argument"),
                           QStringLiteral("tags 必须是字符串数组"));
        tags.append(value.toString());
    }

    QString tagError;
    if (!tx.store->assets().setTags(hash, tags, &tagError))
        return failure(QStringLiteral("invalid_argument"), tagError);

    const auto refreshed = tx.store->assets().get(hash);
    return tx.finish(assetJson(tx.store->assets(), tx.p(), *refreshed),
                     QStringLiteral("pvf: set asset tags \"%1\"").arg(refreshed->name));
}

Result assetDelete(Tx& tx, const QJsonObject& args)
{
    Result error;
    QString hash;
    if (!readString(args, "hash", &hash, &error, true))
        return error;
    const auto asset = tx.store->assets().get(hash);
    if (!asset)
        return failure(QStringLiteral("invalid_argument"),
                       QStringLiteral("素材不存在：%1").arg(hash));

    QJsonArray missing = usedBy(tx.p(), hash);
    if (!confirmed(args))
    {
        QJsonObject affected;
        affected.insert(QStringLiteral("missingEntities"), missing);
        QJsonObject details;
        details.insert(QStringLiteral("requires"), QStringLiteral("confirm"));
        details.insert(QStringLiteral("affected"), affected);
        return failure(QStringLiteral("confirm_required"),
                       QStringLiteral("删除素材需要 confirm"), details);
    }

    const QString name = asset->name;
    QString removeError;
    if (!tx.store->assets().remove(hash, &removeError))
        return failure(QStringLiteral("internal_error"), removeError);

    QJsonObject data;
    data.insert(QStringLiteral("hash"), hash);
    data.insert(QStringLiteral("deleted"), true);
    data.insert(QStringLiteral("missingEntities"), missing);
    return tx.finish(data, QStringLiteral("pvf: delete asset \"%1\"").arg(name));
}

} // namespace pvf::mcp2::detail
