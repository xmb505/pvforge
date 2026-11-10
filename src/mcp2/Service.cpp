#include "mcp2/Service.h"

#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <exception>
#include <utility>

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

Result gitDisabledReadOnly()
{
    QJsonObject details;
    details.insert(QStringLiteral("reason"), QStringLiteral("git_disabled"));
    return detail::failure(
        QStringLiteral("permission_denied"),
        QStringLiteral("Git 未启用，MCP 当前只读；请先在设置中启用 Git"), details);
}

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

    {"solver.create", &detail::solverCreate},
    {"solver.update", &detail::solverUpdate},
    {"solver.delete", &detail::solverDelete},
    {"solver.list", &detail::solverList},
    {"solver.evaluate", &detail::solverEvaluate},

    {"preview.render_frame", &detail::previewRenderFrame},
    {"view.state", &detail::viewStateGet},

    {"asset.import", &detail::assetImport},
    {"asset.list", &detail::assetList},
    {"asset.get", &detail::assetGet},
    {"asset.assign", &detail::assetAssign},
    {"asset.unassign", &detail::assetUnassign},
    {"asset.rename", &detail::assetRename},
    {"asset.setTags", &detail::assetSetTags},
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

void Service::setViewStateProvider(ViewStateProvider provider)
{
    m_viewStateProvider = std::move(provider);
}

void Service::setVideoFrameProvider(FrameProvider provider)
{
    m_videoFrameProvider = std::move(provider);
}

void Service::setHtmlFrameProvider(FrameProvider provider)
{
    m_htmlFrameProvider = std::move(provider);
}

void Service::setBatchCheckpoint(BatchCheckpoint checkpoint)
{
    m_batchCheckpoint = std::move(checkpoint);
}

void Service::setWritesEnabled(bool enabled)
{
    m_writesEnabled.store(enabled, std::memory_order_relaxed);
}

bool Service::writesEnabled() const
{
    return m_writesEnabled.load(std::memory_order_relaxed);
}

void Service::requestStop()
{
    m_stopped.store(true, std::memory_order_relaxed);
}

void Service::resumeWrites()
{
    m_stopped.store(false, std::memory_order_relaxed);
}

bool Service::isStopped() const
{
    return m_stopped.load(std::memory_order_relaxed);
}

bool Service::isWriteTool(const QString& tool)
{
    // Everything not listed here modifies project text. Unknown names count
    // as writes so a stopped server never forwards a surprising mutation.
    static const QSet<QString> readOnly = {
        QStringLiteral("project.get"),
        QStringLiteral("project.summary"),
        QStringLiteral("entity.get"),
        QStringLiteral("entity.list"),
        QStringLiteral("keyframes.get"),
        QStringLiteral("solver.list"),
        QStringLiteral("solver.evaluate"),
        QStringLiteral("preview.render_frame"),
        QStringLiteral("view.state"),
        QStringLiteral("asset.list"),
        QStringLiteral("asset.get"),
        QStringLiteral("markers.list"),
        QStringLiteral("history.log"),
        QStringLiteral("schema.get"),
        QStringLiteral("guide.get"),
    };
    return !readOnly.contains(tool);
}

QJsonObject Service::execute(const QString& tool, const QJsonObject& args)
{
    // Read-only mode wins over the emergency stop: when Git is unavailable the
    // AI must be told to enable Git instead of waiting for a human resume.
    if (isWriteTool(tool) && !writesEnabled())
        return gitDisabledReadOnly().toJson();

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

    if (isWriteTool(tool) && isStopped())
        return detail::stoppedByHuman().toJson();

    try
    {
        detail::Tx tx(m_store);
        if (m_viewStateProvider)
            tx.viewState = m_viewStateProvider();
        tx.videoFrameProvider = m_videoFrameProvider;
        tx.htmlFrameProvider = m_htmlFrameProvider;
        tx.stopCheck = [this] {
            if (m_batchCheckpoint)
                m_batchCheckpoint();
            return isStopped();
        };
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
