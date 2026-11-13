#include "store/ProjectStoreSupport.h"

#include "model/Migrations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <optional>
#include <utility>

namespace pvf::store::detail {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString entityRelativePath(const Uuid& uuid)
{
    return QStringLiteral("entities/") + uuid + QStringLiteral(".json");
}

QString absolutePath(const QString& root, const QString& relativePath)
{
    return root + QLatin1Char('/') + relativePath;
}

bool readJsonObject(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(error,
                 QStringLiteral("无法读取 %1：%2").arg(path, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        setError(error,
                 QStringLiteral("%1 JSON 解析失败：%2（偏移 %3）")
                     .arg(path, parseError.errorString())
                     .arg(parseError.offset));
        return false;
    }
    if (!document.isObject())
    {
        setError(error, QStringLiteral("%1 的 JSON 顶层必须是对象").arg(path));
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(const QString& path, const QJsonObject& object,
                     QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }

    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n'))
        bytes.append('\n');

    if (file.write(bytes) != bytes.size())
    {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, QStringLiteral("无法保存 %1：%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool writeBytesFile(const QString& path, const QByteArray& bytes, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error,
                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }

    if (file.write(bytes) != bytes.size())
    {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        setError(error, QStringLiteral("无法保存 %1：%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool writeTextFile(const QString& path, const QString& content, QString* error)
{
    return writeBytesFile(path, content.toUtf8(), error);
}

QByteArray readFileBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

namespace {

// One text document (project.json or entities/<uuid>.json): read its version,
// run the production migration chain up to the current format version, and
// return the document the strict parser expects. Nothing is written to disk,
// so a document this build cannot read stays byte-identical.
bool migrateTextDocument(const QString& path, const QString& label,
                         const QString& uuid, const QJsonObject& input,
                         QJsonObject* output, QString* error)
{
    const QString subject =
        uuid.isEmpty()
            ? path
            : QStringLiteral("%1（uuid %2）").arg(path, uuid);

    QString versionError;
    const std::optional<int> version =
        model::migrations::documentVersion(input, &versionError);
    if (!version)
    {
        setError(error,
                 QStringLiteral("%1 缺少整数 version（当前格式版本 %2）：%3")
                     .arg(subject)
                     .arg(model::kProjectFormatVersion)
                     .arg(versionError));
        return false;
    }

    if (*version > model::kProjectFormatVersion)
    {
        setError(error,
                 QStringLiteral("%1 由更新版本的 PVForge 保存（%2版本 %3，"
                                "本程序支持到版本 %4）。请升级 PVForge 后再打开；"
                                "原文件不会被修改。")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::kProjectFormatVersion));
        return false;
    }

    if (*version < model::migrations::minimumSupportedVersion())
    {
        setError(error,
                 QStringLiteral("%1 的%2版本 %3 过旧（本程序最低支持版本 %4）")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::migrations::minimumSupportedVersion()));
        return false;
    }

    if (*version == model::kProjectFormatVersion)
    {
        *output = input;
        return true;
    }

    QString migrationError;
    const auto upgraded = model::migrations::upgrade(
        input, *version, model::kProjectFormatVersion, &migrationError);
    if (!upgraded)
    {
        setError(error,
                 QStringLiteral("%1 的%2版本 %3 无法升级到版本 %4：%5")
                     .arg(subject, label)
                     .arg(*version)
                     .arg(model::kProjectFormatVersion)
                     .arg(migrationError));
        return false;
    }

    *output = *upgraded;
    return true;
}

} // namespace

bool loadProjectData(const QString& root, model::Project* project,
                     QString* error)
{
    const QString projectPath = absolutePath(root, QStringLiteral("project.json"));
    QJsonObject object;
    if (!readJsonObject(projectPath, &object, error))
        return false;

    const QJsonValue formatValue = object.value(QStringLiteral("format"));
    if (!formatValue.isString())
    {
        setError(error,
                 QStringLiteral("%1 缺少 format 字段（应为 %2）")
                     .arg(projectPath, QLatin1String(model::kProjectFormat)));
        return false;
    }
    if (formatValue.toString() != QLatin1String(model::kProjectFormat))
    {
        setError(error,
                 QStringLiteral("%1 不是 PVForge 工程（format=%2）")
                     .arg(projectPath, formatValue.toString()));
        return false;
    }

    QJsonObject document;
    if (!migrateTextDocument(projectPath, QStringLiteral("工程"), QString(),
                             object, &document, error))
    {
        return false;
    }

    // Opening a project is strict: a missing required field is an error that
    // names its JSON path instead of silently becoming a default.
    model::ParseOptions options;
    options.mode = model::ParseMode::Strict;

    QString parseError;
    const auto parsed = model::projectFromJson(document, options, &parseError);
    if (!parsed)
    {
        setError(error,
                 QStringLiteral("%1 无效：%2").arg(projectPath, parseError));
        return false;
    }

    model::Project loaded = *parsed;
    QDir entitiesDirectory(absolutePath(root, QStringLiteral("entities")));
    if (entitiesDirectory.exists())
    {
        const QFileInfoList files = entitiesDirectory.entryInfoList(
            {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& fileInfo : files)
        {
            const QString fileName = fileInfo.fileName();
            const QString uuid = fileName.left(fileName.size() - 5);
            if (!model::isValidUuid(uuid))
            {
                setError(error,
                         QStringLiteral("实体文件名不是合法 UUID：%1")
                             .arg(fileInfo.absoluteFilePath()));
                return false;
            }

            QJsonObject entityObject;
            if (!readJsonObject(fileInfo.absoluteFilePath(), &entityObject, error))
                return false;

            QJsonObject entityDocument;
            if (!migrateTextDocument(fileInfo.absoluteFilePath(),
                                     QStringLiteral("实体"), uuid, entityObject,
                                     &entityDocument, error))
            {
                return false;
            }

            QString entityError;
            const auto entity =
                model::entityFromJson(entityDocument, options, &entityError);
            if (!entity)
            {
                setError(error,
                         QStringLiteral("实体文件 %1 无效：%2")
                             .arg(fileInfo.absoluteFilePath(), entityError));
                return false;
            }

            if (entity->uuid != uuid)
            {
                setError(error,
                         QStringLiteral("实体文件 %1 内的 uuid（%2）与文件名不一致")
                             .arg(fileInfo.absoluteFilePath(), entity->uuid));
                return false;
            }

            if (loaded.entities.contains(uuid))
            {
                setError(error,
                         QStringLiteral("实体 UUID 重复：%1").arg(uuid));
                return false;
            }
            loaded.entities.insert(uuid, *entity);
        }
    }

    if (!model::validateProject(loaded, error))
        return false;

    *project = std::move(loaded);
    return true;
}

} // namespace pvf::store::detail
