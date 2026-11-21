#include "mcp2/Internal.h"
#include "mcp2/EntityToolsInternal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pvf::mcp2::detail {

namespace {

QJsonValue metadataValue(const QJsonObject& metadata, const QString& key)
{
    const QJsonValue value = metadata.value(key);
    return value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
}

} // namespace

QStringList acceptedEasingNames()
{
    // Keep this list derived from the model so MCP cannot drift from what
    // model::easedProgress() actually evaluates.  Aliases are the ones
    // model::canonicalEasingName() understands.
    static const QStringList names = [] {
        QStringList list{QStringLiteral("linear"), QStringLiteral("instant"),
                         QStringLiteral("bezier")};
        list.append(model::easingPresetNames());
        list << QStringLiteral("easein") << QStringLiteral("easeout")
             << QStringLiteral("easeinout") << QStringLiteral("step");
        return list;
    }();
    return names;
}

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

bool stopRequested(Tx& tx)
{
    return tx.stopCheck && tx.stopCheck();
}

Result stoppedByHuman()
{
    return failure(QStringLiteral("stopped_by_human"),
                   QStringLiteral("人类按下急停，此次操作未实现"));
}

Tx::Tx(store::ProjectStore* projectStore)
    : store(projectStore)
    , backup(projectStore ? projectStore->project() : model::Project())
    , project(backup)
    , assetTrashBefore(projectStore ? projectStore->trashedAssets()
                                    : QSet<QString>())
{
}

void Tx::drop(const Uuid& uuid)
{
    dirty.remove(uuid);
    deleted.insert(uuid);
}

bool Tx::restore(QString* error)
{
    if (!store)
        return true;

    QStringList failures;

    // Entity files created only by this transaction have no backup entity;
    // delete them before restoring the project copy. Files are addressed by
    // UUID, and entity files are named after their root, so only roots get a
    // direct unlink; descendants are rewritten with their root below.
    for (const Uuid& uuid : std::as_const(dirty))
    {
        if (backup.entities.contains(uuid))
            continue;
        const QString path = store->root() + QStringLiteral("/entities/")
                             + uuid + QStringLiteral(".json");
        if (QFileInfo::exists(path) && !QFile::remove(path))
            failures << QStringLiteral("无法删除新建实体文件 %1").arg(path);
    }

    store->mutableProject() = backup;

    for (const Uuid& uuid : std::as_const(deleted))
    {
        if (!backup.entities.contains(uuid))
            continue;
        QString saveError;
        if (!store->saveEntity(uuid, &saveError))
            failures << saveError;
    }
    for (const Uuid& uuid : std::as_const(dirty))
    {
        if (!backup.entities.contains(uuid))
            continue;
        QString saveError;
        if (!store->saveEntity(uuid, &saveError))
            failures << saveError;
    }
    if (projectDirty)
    {
        QString saveError;
        if (!store->saveProject(&saveError))
            failures << saveError;
    }

    QString assetError;
    QHash<QString, bool> assetState;
    for (const QString& hash : store->trashedAssets())
        assetState.insert(hash, assetTrashBefore.contains(hash));
    for (const QString& hash : std::as_const(assetTrashBefore))
        assetState.insert(hash, true);
    if (!store->applyAssetTrashState(assetState, &assetError))
        failures << assetError;

    if (!failures.isEmpty())
    {
        if (error)
            *error = failures.join(QStringLiteral("；"));
        return false;
    }
    return true;
}

Result Tx::finish(const QJsonObject& data, const QString& message)
{
    if (!store)
        return failure(QStringLiteral("not_available"), QStringLiteral("工程未打开"));

    // Early exits happen after a tool may already have trashed an asset. Undo
    // that side effect; if it cannot be undone, say so instead of pretending
    // the project is unchanged.
    const auto restoreNote = [this]() -> QString {
        QString rollbackError;
        if (restore(&rollbackError))
            return QString();
        const QString reason =
            QStringLiteral("回滚失败：%1。工程可能不一致，请重新打开。")
                .arg(rollbackError);
        store->setReadOnly(reason);
        return QStringLiteral("（%1）").arg(reason);
    };

    // A stop that lands after the last batch item still cancels this call
    // before anything reaches the store.
    if (stopRequested(*this))
    {
        const QString note = restoreNote();
        if (note.isEmpty())
            return stoppedByHuman();
        QJsonObject details;
        details.insert(QStringLiteral("inconsistent"), true);
        return failure(QStringLiteral("stopped_by_human"),
                       QStringLiteral("人类按下急停，此次操作未实现") + note,
                       details);
    }

    // The HTTP layer told the client the request timed out (or is about to).
    // Refuse to write so a retry cannot create a duplicate entity.
    if (deadlineCheck && deadlineCheck())
    {
        const QString note = restoreNote();
        QJsonObject details;
        if (!note.isEmpty())
            details.insert(QStringLiteral("inconsistent"), true);
        return failure(QStringLiteral("request_timeout"),
                       QStringLiteral("请求超时，未写入任何变更；请重试") + note,
                       details);
    }

    QString validationError;
    if (!model::validateProject(project, &validationError))
    {
        const QString note = restoreNote();
        QJsonObject details;
        if (!note.isEmpty())
            details.insert(QStringLiteral("inconsistent"), true);
        return failure(QStringLiteral("invalid_argument"),
                       validationError + note, details);
    }

    // Entity files hold a whole root subtree. Deleting every UUID in one tree
    // would make the second call fail, so hand the store only the topmost
    // deleted entities; it removes their descendants with the file rewrite.
    QVector<Uuid> deleteRoots;
    for (const Uuid& uuid : deleted)
    {
        const auto it = backup.entities.constFind(uuid);
        if (it == backup.entities.constEnd())
            continue;
        if (!it->parentUuid.isEmpty() && deleted.contains(it->parentUuid))
            continue;
        deleteRoots.append(uuid);
    }
    std::sort(deleteRoots.begin(), deleteRoots.end());

    const auto rollbackFailure =
        [this](const QString& code, const QString& reason) -> Result {
        QString rollbackError;
        if (restore(&rollbackError))
            return failure(code,
                           reason + QStringLiteral("（已完整回滚，工程未改变）"));

        // Keep the human in control even when the rollback itself failed: turn
        // the applied writes into one undo step and stop writing.
        QString undoError;
        const bool undoRecorded =
            store->recordPendingAsUndo(QStringLiteral("pvf: AI 写入（回滚失败）"),
                                       &undoError);
        const QString inconsistent =
            QStringLiteral("%1；回滚失败：%2。工程可能不一致，请重新打开工程。")
                .arg(reason, rollbackError);
        store->setReadOnly(inconsistent);
        QJsonObject details;
        details.insert(QStringLiteral("inconsistent"), true);
        details.insert(QStringLiteral("rollbackError"), rollbackError);
        details.insert(QStringLiteral("undoRecorded"), undoRecorded);
        return failure(QStringLiteral("internal_error"), inconsistent, details);
    };

    for (const Uuid& uuid : std::as_const(deleteRoots))
    {
        QString error;
        if (!store->deleteEntity(uuid, &error))
            return rollbackFailure(QStringLiteral("internal_error"), error);
    }

    store->mutableProject() = project;

    QVector<Uuid> sortedDirty(dirty.begin(), dirty.end());
    std::sort(sortedDirty.begin(), sortedDirty.end());
    for (const Uuid& uuid : std::as_const(sortedDirty))
    {
        QString error;
        if (!store->saveEntity(uuid, &error))
            return rollbackFailure(QStringLiteral("internal_error"), error);
    }

    if (projectDirty)
    {
        QString error;
        if (!store->saveProject(&error))
            return rollbackFailure(QStringLiteral("internal_error"), error);
    }

    QString error;
    QString beforeHash;
    const QVector<store::GitCommit> before = store->history(1);
    if (!before.isEmpty())
        beforeHash = before.first().hash;

    if (!store->commit(message, &error))
        return rollbackFailure(QStringLiteral("internal_error"), error);

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
    object.insert(QStringLiteral("space"), model::entitySpaceName(entity.space));
    object.insert(QStringLiteral("followParentRange"), entity.followParentRange);
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
    QJsonArray tagArray;
    for (const QString& tag : asset.tags)
        tagArray.append(tag);
    object.insert(QStringLiteral("tags"), tagArray);
    object.insert(QStringLiteral("usedBy"), usedBy);
    object.insert(QStringLiteral("missing"), assets::missingContent(asset));
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

int solverEffectiveEnd(const model::Entity& entity, const model::SolverSegment& segment)
{
    return segment.endFrame == -1 ? entity.duration() - 1 : segment.endFrame;
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
        if (other.start > end || other.end < start)
            continue;
        highest = std::max(highest, other.row);
    }
    return highest + 1;
}

bool isFiniteNumber(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1e9;
}

bool validateRow(int row, bool onMainTimeline, Result* error)
{
    if (onMainTimeline && row < 0)
    {
        *error = failure(QStringLiteral("invalid_argument"),
                         QStringLiteral("主时间线 row 必须 >= 0（私有时间线允许负 "
                                        "row）"));
        return false;
    }
    if (row < -entity::kMaxFrame || row > entity::kMaxFrame)
    {
        QJsonObject range;
        range.insert(QStringLiteral("min"), -entity::kMaxFrame);
        range.insert(QStringLiteral("max"), entity::kMaxFrame);
        QJsonObject details;
        details.insert(QStringLiteral("allowedRange"), range);
        *error = failure(
            QStringLiteral("invalid_argument"),
            QStringLiteral("row 必须在 %1..%2 之间")
                .arg(-entity::kMaxFrame)
                .arg(entity::kMaxFrame),
            details);
        return false;
    }
    return true;
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
        const QString trimmed = name.trimmed();
        const QString canonical = model::canonicalEasingName(trimmed);
        if (acceptedEasingNames().contains(trimmed.toLower()))
        {
            // Normalize to the model's canonical lowercase name so the value
            // reported back matches what a reload from disk would produce
            // (e.g. "easeInOut" -> "easeinoutquad", "step" -> "instant").
            out->type = canonical;
            *normalized = canonical;
            return true;
        }
        return invalid(QStringLiteral("未知缓动：%1（允许：%2）")
                           .arg(name, acceptedEasingNames().join(
                                         QStringLiteral(", "))));
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
