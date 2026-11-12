#pragma once

#include "common/Types.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace pvf::html {
class HtmlBrowser;
}

namespace pvf::html2 {

class LocalBundleServer;

// Largest accepted edge of a frame, in CSS pixels. Aligned with export2's
// kMaxDimension so a 1x export can always render at the project size.
constexpr int kMaxFrameEdge = 8192;

// Empty when `size` can be rendered; otherwise a user-facing reason. Shared by
// renderFrame() and HtmlFrameService::finalFrame(), so an oversized export is
// rejected before any browser starts.
QString validateFrameSize(const QSize& size);

// One deterministic frame of a .pvhtml bundle.
//
// `localFrame` is the asset-local frame number and `time` is seconds; both are
// handed to the page unchanged. `vars` is the flat variable map declared by
// pvhtml.json (the renderer does not clamp or rename values).
struct FrameRequest
{
    AssetHash assetHash;    // .pvhtml content hash; part of the cache key
    QString bundleDir;      // .pvhtml directory (contains index.html)
    QString entry = QStringLiteral("index.html");
    int localFrame = 0;
    double time = 0.0;
    QVariantMap vars;
    QSize size{1280, 720};
    bool timeDependent = true; // false: render/cache only localFrame=0, time=0
    bool final = false;        // true: high-resolution final render, never cached
    int timeoutMs = 3000;
};

// Single-browser Pvhtml frame renderer.
//
// Page protocol (also documented in docs/AI/ENTITY_MODEL.md):
//
//   // The host runs this after the document reaches readyState=complete and
//   // before every screenshot. It dispatches a synchronous DOM event, so a
//   // listener can update the DOM before the host checks readiness.
//   window.__PVFORGE__ = { frame, time, vars };
//   window.dispatchEvent(new CustomEvent("pvforgeframe",
//                                        { detail: window.__PVFORGE__ }));
//
//   // Optional page -> host handshake, set at load or while handling the event:
//   window.__PVFORGE_READY__ = true;             // ready immediately
//   window.__PVFORGE_READY__ = promiseOrThenable; // host awaits it
//
// The host dispatches the event, waits one macrotask (so a listener that
// defers via setTimeout(0) can publish its promise), awaits __PVFORGE_READY__
// when it is a thenable, awaits document.fonts.ready and two
// requestAnimationFrame callbacks, then calls Page.captureScreenshot with
// clip=(0,0,size.width,size.height), scale=1, format=PNG. The PNG is decoded
// to a QImage with an alpha channel; CSS that paints no background stays
// transparent.
//
// One CDP page is kept alive between renderFrame() calls. Chrome is started by
// start() and reused; renderFrame() navigates again only when the bundle
// directory (origin) changes, and updates Emulation.setDeviceMetricsOverride
// when the requested size changes. A timed-out frame closes its page target so
// the next call starts from a clean document.
//
// All methods are synchronous and use a private QEventLoop; never call them
// from a thread that cannot pump Qt events. The CDP calls themselves are
// asynchronous, so no QProcess wait blocks the UI thread.
class PvhtmlRenderer : public QObject
{
    Q_OBJECT

public:
    explicit PvhtmlRenderer(QObject* parent = nullptr);
    ~PvhtmlRenderer() override;

    // Number of renderers currently alive in the process. Diagnostics only:
    // the html2 tests use it to prove that stopping the service reclaims the
    // final renderer (and with it the browser and its QObject children) instead
    // of leaking it. Not part of the frame protocol.
    static int liveInstanceCount();

    // Explicit Chromium-based browser path. Empty means auto-detection. The
    // setting is applied by the next start(); it does not restart a running
    // browser.
    void setChromeExecutable(const QString& path) { m_chromeExecutable = path; }
    QString chromeExecutable() const { return m_chromeExecutable; }

    bool start(QString* error = nullptr);   // start Chrome (pipe transport first)
    void stop();
    bool isRunning() const;

    // Blocks until the frame is captured or the budget expires. On failure
    // returns a null QImage and fills `error`.
    QImage renderFrame(const FrameRequest& request, QString* error = nullptr);

    QString chromeVersion() const;
    QString lastError() const { return m_lastError; }

private:
    bool ensureServer(const QString& bundleDir, QString* error);
    void dropPage(bool closeTarget);

    pvf::html::HtmlBrowser* m_browser = nullptr;
    LocalBundleServer* m_server = nullptr;

    QString m_sessionId;
    QString m_targetId;
    QString m_loadedUrl;
    QSize m_viewport;
    QString m_chromeVersion;
    QString m_chromeExecutable;
    QString m_lastError;
};

} // namespace pvf::html2
