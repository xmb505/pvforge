#include "Migrations.h"

#include "JsonUtil.h"
#include "Project.h"

namespace pvf::model::migrations {

const QVector<Migration>& registry()
{
    // Add future steps here, oldest first:
    //   {1, QStringLiteral("v1_to_v2"), v1_to_v2},
    static const QVector<Migration> steps;
    return steps;
}

int minimumSupportedVersion()
{
    const QVector<Migration>& steps = registry();
    return steps.isEmpty() ? kProjectFormatVersion : steps.first().fromVersion;
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
