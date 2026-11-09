#include "mcp2/Internal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pvf::mcp2::detail {

namespace {

const QSet<QString>& camelEasings()
{
    static const QSet<QString> names = {
        QStringLiteral("linear"),    QStringLiteral("instant"),
        QStringLiteral("step"),      QStringLiteral("bezier"),
        QStringLiteral("easeIn"),    QStringLiteral("easeOut"),
        QStringLiteral("easeInOut"),
    };
    return names;
}

const QSet<QString>& presetEasings()
{
    static const QSet<QString> names = {
        QStringLiteral("linear"),          QStringLiteral("instant"),
        QStringLiteral("bezier"),          QStringLiteral("easeinquad"),
        QStringLiteral("easeoutquad"),     QStringLiteral("easeinoutquad"),
        QStringLiteral("easeincubic"),     QStringLiteral("easeoutcubic"),
        QStringLiteral("easeinoutcubic"),  QStringLiteral("easeinquart"),
        QStringLiteral("easeoutquart"),    QStringLiteral("easeinoutquart"),
        QStringLiteral("easeinquint"),     QStringLiteral("easeoutquint"),
        QStringLiteral("easeinoutquint"),  QStringLiteral("easeinsine"),
        QStringLiteral("easeoutsine"),     QStringLiteral("easeinoutsine"),
        QStringLiteral("easeinexpo"),      QStringLiteral("easeoutexpo"),
        QStringLiteral("easeinoutexpo"),   QStringLiteral("easeincirc"),
        QStringLiteral("easeoutcirc"),     QStringLiteral("easeinoutcirc"),
        QStringLiteral("easeinelastic"),   QStringLiteral("easeoutelastic"),
        QStringLiteral("easeinoutelastic"),QStringLiteral("easeinback"),
        QStringLiteral("easeoutback"),     QStringLiteral("easeinoutback"),
        QStringLiteral("easeinbounce"),    QStringLiteral("easeoutbounce"),
        QStringLiteral("easeinoutbounce"),
    };
    return names;
}

QJsonValue metadataValue(const QJsonObject& metadata, const QString& key)
{
    const QJsonValue value = metadata.value(key);
    return value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
}

} // namespace

Result failure(const QString& code, const QString& message,
               const QJsonObject& details)
{
    Result result;
    result.ok = false;
    result.errorCode = code;
    result.message = message;
    result.details = details;
    return result;
}

Result success(const QJsonObject& data)
{
    Result result;
    result.ok = true;
    result.data = data;
    return result;
}

Tx::Tx(store::ProjectStore* projectStore)
    : store(projectStore)
    , backup(projectStore ? projectStore->project() : model::Project())
    , project(backup)
{
}

void Tx::drop(const Uuid& uuid)
{
    dirty.remove(uuid);
    deleted.insert(uuid);
}

Result Tx::finish(const QJsonObject& data, const QString& message)
{
    if (!store)
        return failure(QStringLiteral("not_available"), QStringLiteral("工程未打开"));

    QString validationError;
    if (!model::validateProject(project, &validationError))
        return failure(QStringLiteral("invalid_argument"), validationError);

    const auto restore = [this] {
        // Delete files that only exist because of this transaction before
        // restoring the in-memory project from the backup.
        for (const Uuid& uuid : dirty)
        {
            if (backup.entities.contains(uuid))
                continue;
            QFile::remove(store->root() + QStringLiteral("/entities/") + uuid
                          + QStringLiteral(".json"));
        }
        store->mutableProject() = backup;
        for (const Uuid& uuid : deleted)
        {
            QString ignored;
            store->saveEntity(uuid, &ignored);
        }
        for (const Uuid& uuid : dirty)
        {
            if (!backup.entities.contains(uuid))
                continue;
            QString ignored;
            store->saveEntity(uuid, &ignored);
        }
        if (projectDirty)
        {
            QString ignored;
            store->saveProject(&ignored);
        }
    };

    QSet<Uuid> removedNow;
    for (const Uuid& uuid : deleted)
    {
        QString error;
        if (!store->deleteEntity(uuid, &error))
        {
            restore();
            return failure(QStringLiteral("internal_error"), error);
        }
    }

    store->mutableProject() = project;

    QVector<Uuid> sortedDirty(dirty.begin(), dirty.end());
    std::sort(sortedDirty.begin(), sortedDirty.end());
    for (const Uuid& uuid : std::as_const(sortedDirty))
    {
        QString error;
        if (!store->saveEntity(uuid, &error))
        {
            restore();
            return failure(QStringLiteral("internal_error"), error);
        }
    }

    if (projectDirty)
    {
        QString error;
        if (!store->saveProject(&error))
        {
            restore();
            return failure(QStringLiteral("internal_error"), error);
        }
    }

    QString error;
    QString beforeHash;
    const QVector<store::GitCommit> before = store->history(1);
    if (!before.isEmpty())
        beforeHash = before.first().hash;

    if (!store->commit(message, &error))
        return failure(QStringLiteral("internal_error"), error);

    Result result = success(data);
    const QVector<store::GitCommit> history = store->history(1);
    if (!history.isEmpty() && history.first().hash != beforeHash)
        result.commitHash = history.first().hash;
    return result;
}

bool readInt(const QJsonObject& args, const char* key, int* out, Result* error,
             bool required)
{
    const QJsonValue value = args.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
    {
        if (required)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("缺少参数 %1").arg(QLatin1String(key)));
            return false;
        }
        return true;
    }
    if (!value.isDouble())
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是整数")
                             .arg(QLatin1String(key)));
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || std::abs(number) > 1e9)
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是范围内的整数")
                             .arg(QLatin1String(key)));
        return false;
    }
    *out = int(number);
    return true;
}

bool readDouble(const QJsonObject& args, const char* key, double* out,
                Result* error, bool required)
{
    const QJsonValue value = args.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
    {
        if (required)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("缺少参数 %1").arg(QLatin1String(key)));
            return false;
        }
        return true;
    }
    if (!value.isDouble())
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是数字")
                             .arg(QLatin1String(key)));
        return false;
    }
    const double number = value.toDouble();
    if (!isFiniteNumber(number))
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是有限数")
                             .arg(QLatin1String(key)));
        return false;
    }
    *out = number;
    return true;
}

bool readBool(const QJsonObject& args, const char* key, bool* out, Result* error,
              bool required)
{
    const QJsonValue value = args.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
    {
        if (required)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("缺少参数 %1").arg(QLatin1String(key)));
            return false;
        }
        return true;
    }
    if (!value.isBool())
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是布尔值")
                             .arg(QLatin1String(key)));
        return false;
    }
    *out = value.toBool();
    return true;
}

bool readString(const QJsonObject& args, const char* key, QString* out,
                Result* error, bool required)
{
    const QJsonValue value = args.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
    {
        if (required)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("缺少参数 %1").arg(QLatin1String(key)));
            return false;
        }
        return true;
    }
    if (!value.isString())
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是字符串")
                             .arg(QLatin1String(key)));
        return false;
    }
    *out = value.toString();
    return true;
}

bool readUuid(const QJsonObject& args, const char* key, Uuid* out, Result* error,
              bool required, bool allowNull)
{
    const QString name = QLatin1String(key);
    const QJsonValue value = args.value(name);
    if (value.isUndefined())
    {
        if (required)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("缺少参数 %1").arg(name));
            return false;
        }
        return true;
    }
    if (value.isNull())
    {
        if (!allowNull)
        {
            *error = failure(QStringLiteral("invalid_argument"),
                             QStringLiteral("参数 %1 不能为 null").arg(name));
            return false;
        }
        *out = QString();
        return true;
    }
    if (!value.isString())
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 必须是 UUID 字符串或 null").arg(name));
        return false;
    }
    const QString uuid = value.toString();
    if (uuid.isEmpty() && allowNull)
    {
        *out = QString();
        return true;
    }
    if (!model::isValidUuid(uuid))
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("参数 %1 不是合法 UUID").arg(name));
        return false;
    }
    *out = uuid;
    return true;
}

bool confirmed(const QJsonObject& args)
{
    const QJsonValue value = args.value(QStringLiteral("confirm"));
    return value.isBool() && value.toBool();
}

QJsonObject settingsJson(const model::ProjectSettings& settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), settings.name);
    object.insert(QStringLiteral("width"), settings.width);
    object.insert(QStringLiteral("height"), settings.height);
    object.insert(QStringLiteral("fps"), settings.fps);
    object.insert(QStringLiteral("background"), settings.background);
    return object;
}

QJsonObject entityBrief(const model::Entity& entity)
{
    QJsonObject object;
    object.insert(QStringLiteral("uuid"), entity.uuid);
    object.insert(QStringLiteral("name"), entity.name);
    object.insert(QStringLiteral("parentUuid"),
                  entity.parentUuid.isEmpty() ? QJsonValue(QJsonValue::Null)
                                              : QJsonValue(entity.parentUuid));
    object.insert(QStringLiteral("start"), entity.start);
    object.insert(QStringLiteral("end"), entity.end);
    object.insert(QStringLiteral("duration"), entity.duration());
    object.insert(QStringLiteral("row"), entity.row);
    object.insert(QStringLiteral("hidden"), entity.hidden);
    object.insert(QStringLiteral("locked"), entity.locked);
    object.insert(QStringLiteral("contentKind"), contentKindName(entity.content.kind));
    return object;
}

QJsonObject entityFull(const model::Entity& entity)
{
    QJsonObject object = model::toJson(entity);
    object.insert(QStringLiteral("duration"), entity.duration());
    return object;
}

QJsonObject pageJson(int offset, int limit, int total)
{
    const int returned = std::max(0, std::min(limit, total - offset));
    QJsonObject page;
    page.insert(QStringLiteral("offset"), offset);
    page.insert(QStringLiteral("limit"), limit);
    page.insert(QStringLiteral("total"), total);
    page.insert(QStringLiteral("hasMore"), offset + returned < total);
    page.insert(QStringLiteral("nextOffset"),
                offset + returned < total ? QJsonValue(offset + returned)
                                          : QJsonValue(QJsonValue::Null));
    return page;
}

QJsonObject assetJson(const assets::AssetStore& assetStore,
                      const model::Project& project,
                      const assets::StoredAsset& asset)
{
    Q_UNUSED(assetStore);
    const QJsonObject metadata = asset.metadata;

    QString path = asset.directory;
    if (asset.type == AssetType::Html)
    {
        QString entry = metadata.value(QStringLiteral("entry")).toString();
        if (entry.isEmpty())
            entry = QStringLiteral("index.html");
        path += QStringLiteral("/bundle/") + entry;
    }
    else
    {
        const QStringList files = QDir(asset.directory)
                                      .entryList({QStringLiteral("content.*")},
                                                 QDir::Files, QDir::Name);
        path += QLatin1Char('/') + (files.isEmpty() ? QString() : files.first());
    }

    QJsonArray usedBy;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        if (it.value().content.kind == ContentKind::Asset
            && it.value().content.asset.hash == asset.hash)
            usedBy.append(it.key());
    }

    QJsonObject object;
    object.insert(QStringLiteral("hash"), asset.hash);
    object.insert(QStringLiteral("type"), assetTypeName(asset.type));
    object.insert(QStringLiteral("name"), asset.name);
    object.insert(QStringLiteral("mime"),
                  metadata.value(QStringLiteral("mime")).toString());
    object.insert(QStringLiteral("width"),
                  metadata.value(QStringLiteral("width")).toInt(0));
    object.insert(QStringLiteral("height"),
                  metadata.value(QStringLiteral("height")).toInt(0));
    object.insert(QStringLiteral("durationFrames"),
                  metadata.value(QStringLiteral("durationFrames")).toInt(0));
    object.insert(QStringLiteral("sampleRate"),
                  metadata.value(QStringLiteral("sampleRate")).toInt(0));
    object.insert(QStringLiteral("channels"),
                  metadata.value(QStringLiteral("channels")).toInt(0));
    object.insert(QStringLiteral("entry"),
                  metadataValue(metadata, QStringLiteral("entry")));
    object.insert(QStringLiteral("variables"),
                  metadataValue(metadata, QStringLiteral("variables")));
    object.insert(QStringLiteral("usedBy"), usedBy);
    object.insert(QStringLiteral("missing"),
                  !QFileInfo::exists(asset.directory));
    object.insert(QStringLiteral("path"), path);
    return object;
}

model::Entity* findEntity(model::Project& project, const Uuid& uuid)
{
    const auto it = project.entities.find(uuid);
    return it == project.entities.end() ? nullptr : &it.value();
}

const model::Entity* findEntity(const model::Project& project, const Uuid& uuid)
{
    const auto it = project.entities.constFind(uuid);
    return it == project.entities.constEnd() ? nullptr : &it.value();
}

Uuid lockedBy(const model::Project& project, const Uuid& uuid)
{
    QSet<Uuid> visited;
    Uuid current = uuid;
    while (!current.isEmpty() && !visited.contains(current))
    {
        visited.insert(current);
        const model::Entity* entity = findEntity(project, current);
        if (!entity)
            return QString();
        if (entity->locked)
            return current;
        current = entity->parentUuid;
    }
    return QString();
}

bool ensureUnlocked(const model::Project& project, const Uuid& uuid, Result* error)
{
    const Uuid source = lockedBy(project, uuid);
    if (source.isEmpty())
        return true;
    QJsonObject details;
    details.insert(QStringLiteral("lockedBy"), source);
    *error = failure(QStringLiteral("conflict"),
                     QStringLiteral("实体被锁定，无法写入"), details);
    return false;
}

QVector<Uuid> subtree(const model::Project& project, const Uuid& uuid)
{
    QVector<Uuid> result;
    QVector<Uuid> pending{uuid};
    QSet<Uuid> seen{uuid};
    while (!pending.isEmpty())
    {
        const Uuid current = pending.takeLast();
        for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
             ++it)
        {
            if (it.value().parentUuid != current || seen.contains(it.key()))
                continue;
            seen.insert(it.key());
            result.append(it.key());
            pending.append(it.key());
        }
    }
    return result;
}

int depthOf(const model::Project& project, const Uuid& uuid)
{
    int depth = 0;
    QSet<Uuid> visited;
    Uuid current = uuid;
    while (!current.isEmpty() && !visited.contains(current))
    {
        visited.insert(current);
        const model::Entity* entity = findEntity(project, current);
        if (!entity)
            break;
        ++depth;
        current = entity->parentUuid;
    }
    return depth;
}

QStringList drivenTracks(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
    case SolverEffect::Orbit:
    case SolverEffect::BlackHole:
        return {QStringLiteral("x"), QStringLiteral("y")};
    case SolverEffect::Spin:
        return {QStringLiteral("rotation")};
    }
    return {};
}

int solverEffectiveEnd(const model::Entity& entity, const model::SolverSegment& segment)
{
    return segment.endFrame == -1 ? entity.duration() - 1 : segment.endFrame;
}

QStringList allowedTracks(const model::Project& project,
                          const assets::AssetStore& assetStore,
                          const model::Entity& entity)
{
    Q_UNUSED(project);
    QStringList tracks = {
        QStringLiteral("x"),       QStringLiteral("y"),
        QStringLiteral("rotation"),QStringLiteral("scaleX"),
        QStringLiteral("scaleY"),  QStringLiteral("opacity"),
        QStringLiteral("pivotX"),  QStringLiteral("pivotY"),
        QStringLiteral("visible"), QStringLiteral("physicsInteract"),
    };

    switch (entity.content.kind)
    {
    case ContentKind::Text:
        tracks << QStringLiteral("fontSize") << QStringLiteral("lineHeight")
               << QStringLiteral("letterSpacing") << QStringLiteral("strokeWidth")
               << QStringLiteral("padding") << QStringLiteral("boxWidth")
               << QStringLiteral("boxHeight");
        break;
    case ContentKind::Vector:
        tracks << QStringLiteral("width") << QStringLiteral("height")
               << QStringLiteral("cornerRadius") << QStringLiteral("strokeWidth");
        break;
    case ContentKind::Camera:
        tracks.removeAll(QStringLiteral("opacity"));
        tracks.removeAll(QStringLiteral("pivotX"));
        tracks.removeAll(QStringLiteral("pivotY"));
        tracks.removeAll(QStringLiteral("visible"));
        tracks.removeAll(QStringLiteral("physicsInteract"));
        break;
    case ContentKind::Asset:
        if (entity.content.asset.type == AssetType::Audio)
            tracks << QStringLiteral("volume");
        if (entity.content.asset.type == AssetType::Html)
        {
            const auto asset = assetStore.get(entity.content.asset.hash);
            const QJsonObject variables =
                asset ? asset->metadata.value(QStringLiteral("variables")).toObject()
                      : QJsonObject();
            for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
                tracks << QStringLiteral("var.") + it.key();
        }
        break;
    case ContentKind::Empty:
        break;
    }

    // Tracks kept from a previous content type stay readable and writable.
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd(); ++it)
    {
        if (!tracks.contains(it.key()))
            tracks << it.key();
    }
    return tracks;
}

int defaultRow(const model::Project& project, const Uuid& parentUuid, int start,
               int end)
{
    int highest = -1;
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& other = it.value();
        if (other.parentUuid != parentUuid)
            continue;
        if (other.start >= end || other.end <= start)
            continue;
        highest = std::max(highest, other.row);
    }
    return highest + 1;
}

bool isFiniteNumber(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1e9;
}

bool parseEasing(const QJsonValue& value, model::EasingSpec* out,
                 QJsonValue* normalized, Result* error)
{
    const auto invalid = [&](const QString& message) {
        *error = failure(QStringLiteral("invalid_argument"), message);
        return false;
    };

    if (value.isUndefined() || value.isNull())
    {
        *out = model::EasingSpec();
        *normalized = QStringLiteral("linear");
        return true;
    }

    if (value.isString())
    {
        const QString name = value.toString();
        if (camelEasings().contains(name) || presetEasings().contains(name))
        {
            out->type = name;
            *normalized = name;
            return true;
        }
        return invalid(QStringLiteral("未知缓动：%1").arg(name));
    }

    if (!value.isObject())
        return invalid(QStringLiteral("easing 必须是字符串或对象"));

    const QJsonObject object = value.toObject();
    const QString type = object.value(QStringLiteral("type"))
                             .toString(QStringLiteral("bezier"));
    if (type != QLatin1String("bezier"))
        return invalid(QStringLiteral("缓动对象只支持 type=bezier"));

    double inX = 0.42;
    double inY = 0.0;
    double outX = 0.58;
    double outY = 1.0;
    if (!readDouble(object, "inX", &inX, error) ||
        !readDouble(object, "inY", &inY, error) ||
        !readDouble(object, "outX", &outX, error) ||
        !readDouble(object, "outY", &outY, error))
        return false;

    inX = std::clamp(inX, 0.0, 1.0);
    outX = std::clamp(outX, 0.0, 1.0);

    model::EasingSpec spec;
    spec.type = QStringLiteral("bezier");
    spec.inX = inX;
    spec.inY = inY;
    spec.outX = outX;
    spec.outY = outY;
    *out = spec;

    QJsonObject objectOut;
    objectOut.insert(QStringLiteral("type"), spec.type);
    objectOut.insert(QStringLiteral("inX"), spec.inX);
    objectOut.insert(QStringLiteral("inY"), spec.inY);
    objectOut.insert(QStringLiteral("outX"), spec.outX);
    objectOut.insert(QStringLiteral("outY"), spec.outY);
    *normalized = objectOut;
    return true;
}

QJsonValue easingJson(const model::EasingSpec& easing)
{
    if (easing.type != QLatin1String("bezier"))
    {
        const QString type =
            easing.type.isEmpty() ? QStringLiteral("linear") : easing.type;
        return type;
    }
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("bezier"));
    object.insert(QStringLiteral("inX"), easing.inX);
    object.insert(QStringLiteral("inY"), easing.inY);
    object.insert(QStringLiteral("outX"), easing.outX);
    object.insert(QStringLiteral("outY"), easing.outY);
    return object;
}

} // namespace pvf::mcp2::detail
