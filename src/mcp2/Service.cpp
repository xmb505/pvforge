#include "mcp2/Service.h"

#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>

#include <exception>

namespace pvf::mcp2 {

QJsonObject Result::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("ok"), ok);
    if (ok)
    {
        object.insert(QStringLiteral("data"), data);
        if (!commitHash.isEmpty())
        {
            QJsonObject commit;
            commit.insert(QStringLiteral("hash"), commitHash);
            commit.insert(QStringLiteral("shortHash"), commitHash.left(7));
            object.insert(QStringLiteral("commit"), commit);
        }
        return object;
    }

    QJsonObject error;
    error.insert(QStringLiteral("code"), errorCode);
    error.insert(QStringLiteral("message"), message);
    if (!details.isEmpty())
        error.insert(QStringLiteral("details"), details);
    object.insert(QStringLiteral("error"), error);
    return object;
}

namespace {

using Handler = Result (*)(detail::Tx&, const QJsonObject&);

struct ToolEntry
{
    const char* name;
    Handler handler;
};

const ToolEntry kTools[] = {
    {"project.get", &detail::projectGet},
    {"project.summary", &detail::projectSummary},
    {"project.update", &detail::projectUpdate},

    {"entity.create", &detail::entityCreate},
    {"entity.createTree", &detail::entityCreateTree},
    {"entity.createMany", &detail::entityCreateMany},
    {"entity.get", &detail::entityGet},
    {"entity.list", &detail::entityList},
    {"entity.update", &detail::entityUpdate},
    {"entity.updateMany", &detail::entityUpdateMany},
    {"entity.move", &detail::entityMove},
    {"entity.trim", &detail::entityTrim},
    {"entity.reparent", &detail::entityReparent},
    {"entity.duplicate", &detail::entityDuplicate},
    {"entity.delete", &detail::entityDelete},
    {"entity.deleteMany", &detail::entityDeleteMany},

    {"keyframes.get", &detail::keyframesGet},
    {"keyframes.set", &detail::keyframesSet},
    {"keyframes.setMany", &detail::keyframesSetMany},
    {"keyframes.remove", &detail::keyframesRemove},
    {"keyframes.move", &detail::keyframesMove},
    {"keyframes.clear", &detail::keyframesClear},
    {"keyframes.setEasing", &detail::keyframesSetEasing},

    {"asset.import", &detail::assetImport},
    {"asset.list", &detail::assetList},
    {"asset.get", &detail::assetGet},
    {"asset.assign", &detail::assetAssign},
    {"asset.unassign", &detail::assetUnassign},
    {"asset.rename", &detail::assetRename},
    {"asset.delete", &detail::assetDelete},

    {"markers.list", &detail::markersList},
    {"markers.set", &detail::markersSet},
    {"markers.remove", &detail::markersRemove},

    {"history.undo", &detail::historyUndo},
    {"history.redo", &detail::historyRedo},
    {"history.log", &detail::historyLog},

    {"schema.get", &detail::schemaGet},
    {"guide.get", &detail::guideGet},
};

} // namespace

Service::Service(store::ProjectStore* store, assets::AssetStore* assets)
    : m_store(store)
    , m_assets(assets)
{
}

QJsonObject Service::execute(const QString& tool, const QJsonObject& args)
{
    if (!m_store)
        return detail::failure(QStringLiteral("not_available"),
                               QStringLiteral("工程未打开"))
            .toJson();

    const ToolEntry* entry = nullptr;
    for (const ToolEntry& candidate : kTools)
    {
        if (tool == QLatin1String(candidate.name))
        {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
    {
        QJsonObject details;
        details.insert(QStringLiteral("tool"), tool);
        return detail::failure(QStringLiteral("invalid_argument"),
                               QStringLiteral("未知工具：%1").arg(tool), details)
            .toJson();
    }

    try
    {
        detail::Tx tx(m_store);
        return entry->handler(tx, args).toJson();
    }
    catch (const std::exception& exception)
    {
        return detail::failure(QStringLiteral("internal_error"),
                               QString::fromUtf8(exception.what()))
            .toJson();
    }
}

QJsonObject Service::listTools() const
{
    return detail::listTools();
}

} // namespace pvf::mcp2
