#pragma once

#include "common/Types.h"

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>

#include <atomic>
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

    // Optional media providers used by preview.render_frame.  Empty providers
    // keep render2's placeholders.  Signatures match render2::FrameProvider.
    using FrameProvider = std::function<QImage(const Uuid&, double)>;
    void setVideoFrameProvider(FrameProvider provider);
    void setHtmlFrameProvider(FrameProvider provider);

    // Executes one tool. Unknown tools and malformed arguments return a
    // structured failure envelope; the method never throws.
    QJsonObject execute(const QString& tool, const QJsonObject& args);

    // Human emergency stop. While stopped, write tools fail with
    // errorCode "stopped_by_human" and touch nothing; read-only tools keep
    // working so the AI can inspect state. Already committed history stays.
    // Writes resume only through resumeWrites().
    void requestStop();
    void resumeWrites();
    bool isStopped() const;

    // True for tools that modify project text (project.update, entity.*,
    // keyframes.*, solver.create/update/delete, asset.*, markers.*,
    // history.undo/redo). Unknown names count as writes.
    static bool isWriteTool(const QString& tool);

    // Optional hook run at every batch checkpoint, right before the stop flag
    // is read. The UI can pump its event loop here so the stop button stays
    // clickable during a long transaction.
    using BatchCheckpoint = std::function<void()>;
    void setBatchCheckpoint(BatchCheckpoint checkpoint);

    // MCP tools/list payload: { "tools": [ {name, description, inputSchema} ] }.
    QJsonObject listTools() const;

private:
    store::ProjectStore* m_store = nullptr;
    assets::AssetStore* m_assets = nullptr;
    ViewStateProvider m_viewStateProvider;
    FrameProvider m_videoFrameProvider;
    FrameProvider m_htmlFrameProvider;
    BatchCheckpoint m_batchCheckpoint;
    std::atomic<bool> m_stopped{false};
};

} // namespace pvf::mcp2
