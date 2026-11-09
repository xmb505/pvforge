#pragma once

#include "common/Types.h"

#include <QJsonObject>
#include <QRect>
#include <QString>

#include <functional>

namespace pvf::store {
class ProjectStore;
}

namespace pvf::assets {
class AssetStore;
}

namespace pvf::mcp2 {

// Structured result of one MCP tool call. The transport layer turns toJson()
// into the MCP result envelope; this module never speaks HTTP or JSON-RPC.
struct Result
{
    bool ok = false;
    QJsonObject data;
    QString errorCode; // invalid_argument / conflict / permission_denied /
                       // confirm_required / not_available / internal_error
    QString message;
    QJsonObject details;
    QString commitHash;

    QJsonObject toJson() const;
};

// Editor-owned UI state exposed through view.state and project.summary.
struct ViewState
{
    int playhead = 0;
    Uuid selection;
    Uuid timelineParent;   // empty = root timeline
    Uuid cameraUuid;       // empty = whole canvas
    QRect viewRect;        // visible project range, may be empty
};
using ViewStateProvider = std::function<ViewState()>;

// Tool service: no transport.
//
// Every successful write tool is one logical operation and produces exactly one
// git commit through store::ProjectStore::commit(). Batch tools validate the
// whole payload before touching the store, so a failure leaves no partial data.
class Service
{
public:
    Service(store::ProjectStore* store, assets::AssetStore* assets);

    // Installs the callback that supplies live UI state. An empty provider
    // yields ViewState defaults.
    void setViewStateProvider(ViewStateProvider provider);

    // Executes one tool. Unknown tools and malformed arguments return a
    // structured failure envelope; the method never throws.
    QJsonObject execute(const QString& tool, const QJsonObject& args);

    // MCP tools/list payload: { "tools": [ {name, description, inputSchema} ] }.
    QJsonObject listTools() const;

private:
    store::ProjectStore* m_store = nullptr;
    assets::AssetStore* m_assets = nullptr;
    ViewStateProvider m_viewStateProvider;
};

} // namespace pvf::mcp2
