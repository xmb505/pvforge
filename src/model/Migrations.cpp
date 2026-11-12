#include "Migrations.h"

#include "JsonUtil.h"

namespace pvf::model::migrations {

namespace {

// True for an entity document (entities/<uuid>.json). project.json always
// carries the project format marker, so a stray uuid in it never flips the
// kind.
bool isEntityDocument(const QJsonObject& document)
{
    if (document.value(QStringLiteral("format")).toString()
        == QLatin1String(kProjectFormat))
    {
        return false;
    }
    return document.contains(QStringLiteral("uuid"));
}

// v1 entity files were anonymous: no "format", no "version". v2 makes every
// text document self-describing, so the step adds the entity format marker;
// upgrade() stamps the version itself. project.json is unchanged by this step.
QJsonObject v1ToV2(const QJsonObject& document)
{
    QJsonObject result = document;
    if (!result.contains(QStringLiteral("format")) && isEntityDocument(result))
        result.insert(QStringLiteral("format"), QLatin1String(kEntityFormat));
    return result;
}

} // namespace

const QVector<Migration>& registry()
{
    // Oldest first. Every step must handle project.json and entities/*.json.
    static const QVector<Migration> steps{
        Migration{1, QStringLiteral("v1_to_v2"), v1ToV2},
    };
    return steps;
}

int minimumSupportedVersion()
{
    const QVector<Migration>& steps = registry();
    return steps.isEmpty() ? kProjectFormatVersion : steps.first().fromVersion;
}

std::optional<int> documentVersion(const QJsonObject& document, QString* error)
{
    const QJsonValue value = document.value(QStringLiteral("version"));
    if (value.isDouble())
        return value.toInt();

    if (value.isUndefined() || value.isNull())
    {
        // v1 entity files predate the version field. They are the only
        // version-less documents the migration chain can upgrade.
        if (!document.contains(QStringLiteral("format"))
            && document.contains(QStringLiteral("uuid")))
        {
            return 1;
        }
        detail::setError(
            error, QStringLiteral("缺少整数 version（当前格式版本 %1）")
                       .arg(kProjectFormatVersion));
        return std::nullopt;
    }

    detail::setError(error, QStringLiteral("字段 version 必须是整数"));
    return std::nullopt;
}

bool checkDocumentVersion(const QJsonObject& document, const QString& label,
                          const ParseOptions& options, QString* error)
{
    const QJsonValue versionValue = document.value(QStringLiteral("version"));
    const bool missing = versionValue.isUndefined() || versionValue.isNull();
    if (!missing && !versionValue.isDouble())
    {
        detail::setError(error, QStringLiteral("字段 version 必须是整数"));
        return false;
    }

    // Lenient reading (tests, migration helpers) assumes the current version
    // for a document that predates the field, so old fixtures stay readable.
    if (missing && options.mode == ParseMode::Lenient)
    {
        detail::warn(options,
                     QStringLiteral("缺少%1版本，按当前格式版本 %2 读取")
                         .arg(label)
                         .arg(kProjectFormatVersion));
        return true;
    }

    QString versionError;
    const std::optional<int> version = documentVersion(document, &versionError);
    if (!version)
    {
        detail::setError(error, versionError);
        return false;
    }

    if (*version > kProjectFormatVersion)
    {
        detail::setError(
            error,
            QStringLiteral("%1版本 %2 高于本程序支持的版本 %3：请升级 PVForge "
                           "后再打开（原文件不会被修改）")
                .arg(label)
                .arg(*version)
                .arg(kProjectFormatVersion));
        return false;
    }
    if (*version < kProjectFormatVersion)
    {
        detail::setError(
            error,
            QStringLiteral("%1版本 %2 低于本程序支持的版本 %3：需要先迁移到版本 "
                           "%3（ProjectStore 打开时会自动升级）")
                .arg(label)
                .arg(*version)
                .arg(kProjectFormatVersion));
        return false;
    }
    return true;
}

std::optional<QJsonObject> upgrade(const QJsonObject& document,
                                   int fromVersion, int toVersion,
                                   const QVector<Migration>& chain,
                                   QString* error)
{
    if (fromVersion < 0)
    {
        detail::setError(
            error, QStringLiteral("工程版本号非法：%1").arg(fromVersion));
        return std::nullopt;
    }
    if (toVersion < fromVersion)
    {
        detail::setError(error,
                         QStringLiteral("无法从版本 %1 降级到版本 %2")
                             .arg(fromVersion)
                             .arg(toVersion));
        return std::nullopt;
    }

    QJsonObject current = document;
    for (int version = fromVersion; version < toVersion; ++version)
    {
        const Migration* step = nullptr;
        for (const Migration& candidate : chain)
        {
            if (candidate.fromVersion == version)
            {
                step = &candidate;
                break;
            }
        }
        if (!step)
        {
            detail::setError(
                error,
                QStringLiteral("缺少从版本 %1 到 %2 的迁移（工程版本 %3）")
                    .arg(version)
                    .arg(version + 1)
                    .arg(fromVersion));
            return std::nullopt;
        }

        current = step->apply(current);
        current.insert(QStringLiteral("version"), version + 1);
    }
    return current;
}

std::optional<QJsonObject> upgrade(const QJsonObject& document,
                                   int fromVersion, int toVersion,
                                   QString* error)
{
    return upgrade(document, fromVersion, toVersion, registry(), error);
}

} // namespace pvf::model::migrations
