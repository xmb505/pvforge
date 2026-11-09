#include "html2/PvhtmlRenderer.h"

#include "html/CdpClient.h"
#include "html/HtmlBrowser.h"
#include "html2/LocalBundleServer.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace pvf::html2 {

namespace {

constexpr int kMaxEdge = 4096;
constexpr int kMaxTimeoutMs = 30000;
constexpr int kMinTimeoutMs = 100;

struct SyncResult
{
    QJsonObject result;
    QString error;
    bool done = false;

    bool ok() const { return done && error.isEmpty(); }
};

// Runs one CDP call and pumps Qt events until the reply arrives. The callback
// may outlive this function (a late reply after the watchdog fired), so it only
// touches shared state and a guarded event loop.
SyncResult callSync(pvf::html::CdpClient* cdp, const QString& method,
                    const QJsonObject& params, const QString& sessionId, int timeoutMs)
{
    auto state = std::make_shared<SyncResult>();
    if (!cdp)
    {
        state->error = QStringLiteral("%1: CDP client is unavailable").arg(method);
        return *state;
    }

    const int callTimeout = qMax(1, timeoutMs);
    QEventLoop loop;
    QPointer<QEventLoop> guard(&loop);
    cdp->call(method, params, sessionId, callTimeout,
              [state, guard](const QJsonObject& result, const QString& error) {
                  state->result = result;
                  state->error = error;
                  state->done = true;
                  if (guard)
                      guard->quit();
              });

    if (!state->done)
    {
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        watchdog.start(callTimeout + 1000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        if (!state->done)
            state->error =
                QStringLiteral("%1 timed out after %2 ms").arg(method).arg(callTimeout);
    }
    return *state;
}

void sleepBriefly(int milliseconds)
{
    if (milliseconds <= 0)
        return;
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

QString exceptionText(const QJsonObject& result)
{
    const QJsonObject details = result.value(QStringLiteral("exceptionDetails")).toObject();
    if (details.isEmpty())
        return QString();

    QString text = details.value(QStringLiteral("text")).toString();
    const QJsonObject exception = details.value(QStringLiteral("exception")).toObject();
    const QString description = exception.value(QStringLiteral("description")).toString();
    if (!description.isEmpty())
    {
        if (!text.isEmpty())
            text += QStringLiteral(": ");
        text += description;
    }
    const int line = details.value(QStringLiteral("lineNumber")).toInt(-1);
    if (line >= 0)
        text += QStringLiteral(" (line %1)").arg(line + 1);
    return text.isEmpty() ? QStringLiteral("page script threw an exception") : text;
}

bool isTransientNavigationError(const QString& error)
{
    return error.contains(QStringLiteral("Execution context was destroyed"))
           || error.contains(QStringLiteral("Cannot find context"))
           || error.contains(QStringLiteral("Inspected target navigated"))
           || error.contains(QStringLiteral("Cannot find execution context"));
}

struct FrameClock
{
    QElapsedTimer timer;
    int totalMs = 0;

    int remainingMs() const { return qMax(0, totalMs - int(timer.elapsed())); }
    int budget(int cap = 0) const
    {
        const int remaining = remainingMs();
        return cap > 0 ? qMin(remaining, cap) : remaining;
    }
};

} // namespace

PvhtmlRenderer::PvhtmlRenderer(QObject* parent)
    : QObject(parent)
{
}

PvhtmlRenderer::~PvhtmlRenderer()
{
    // Do not pump a QEventLoop from a destructor. Killing the browser process
    // group already discards the page target.
    m_sessionId.clear();
    m_targetId.clear();
    m_loadedUrl.clear();
    m_viewport = QSize();
    if (m_server)
        m_server->stop();
    if (m_browser)
    {
        m_browser->stop();
        delete m_browser;
        m_browser = nullptr;
    }
}

bool PvhtmlRenderer::start(QString* error)
{
    if (isRunning())
    {
        if (error)
            error->clear();
        return true;
    }

    m_sessionId.clear();
    m_targetId.clear();
    m_loadedUrl.clear();
    m_viewport = QSize();
    m_lastError.clear();

    if (!m_browser)
        m_browser = new pvf::html::HtmlBrowser(this);

    pvf::html::BrowserStartOptions options;
    options.offline = true;
    options.timeoutMs = 15000;
    // HtmlBrowser blocks every network destination with a dead proxy and
    // removes Chromium's implicit loopback bypass. Re-add 127.0.0.1 so the
    // bundle server stays reachable while external traffic remains offline.
    // The host-resolver override is last so it replaces HtmlBrowser's
    // "MAP * ~NOTFOUND"; EXCLUDE keeps every other host blocked.
    options.extraArguments
        << QStringLiteral("--proxy-bypass-list=<-loopback>,127.0.0.1,localhost")
        << QStringLiteral("--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1");

    QString startError;
    if (!m_browser->start(options, &startError))
    {
        m_lastError = startError;
        if (error)
            *error = startError;
        return false;
    }

    m_chromeVersion = m_browser->version().trimmed();
    if (!m_server)
        m_server = new LocalBundleServer(this);

    if (error)
        error->clear();
    return true;
}

void PvhtmlRenderer::stop()
{
    if (m_browser && m_browser->isRunning() && m_browser->cdp() && !m_targetId.isEmpty())
    {
        callSync(m_browser->cdp(), QStringLiteral("Target.closeTarget"),
                 QJsonObject{{QStringLiteral("targetId"), m_targetId}}, QString(), 500);
    }

    m_sessionId.clear();
    m_targetId.clear();
    m_loadedUrl.clear();
    m_viewport = QSize();

    if (m_server)
        m_server->stop();
    if (m_browser)
    {
        m_browser->stop();
        delete m_browser;
        m_browser = nullptr;
    }
    m_chromeVersion.clear();
}

bool PvhtmlRenderer::isRunning() const
{
    return m_browser && m_browser->isRunning();
}

QString PvhtmlRenderer::chromeVersion() const
{
    if (m_browser && !m_browser->version().isEmpty())
        return m_browser->version();
    return m_chromeVersion;
}

bool PvhtmlRenderer::ensureServer(const QString& bundleDir, QString* error)
{
    const QFileInfo info(bundleDir);
    if (!info.exists() || !info.isDir())
    {
        const QString message =
            QStringLiteral("HTML bundle directory does not exist: %1").arg(bundleDir);
        m_lastError = message;
        if (error)
            *error = message;
        return false;
    }

    const QString root = info.canonicalFilePath();
    if (root.isEmpty())
    {
        const QString message =
            QStringLiteral("cannot resolve the HTML bundle directory: %1").arg(bundleDir);
        m_lastError = message;
        if (error)
            *error = message;
        return false;
    }

    if (m_server && m_server->isRunning() && m_server->rootDir() == root)
    {
        if (error)
            error->clear();
        return true;
    }

    if (!m_server)
        m_server = new LocalBundleServer(this);

    m_server->stop();
    m_loadedUrl.clear();

    QString serverError;
    if (!m_server->start(root, &serverError))
    {
        m_lastError = serverError;
        if (error)
            *error = serverError;
        return false;
    }

    if (error)
        error->clear();
    return true;
}

void PvhtmlRenderer::dropPage(bool closeTarget)
{
    if (closeTarget && m_browser && m_browser->isRunning() && m_browser->cdp()
        && !m_targetId.isEmpty())
    {
        callSync(m_browser->cdp(), QStringLiteral("Target.closeTarget"),
                 QJsonObject{{QStringLiteral("targetId"), m_targetId}}, QString(), 500);
    }
    m_sessionId.clear();
    m_targetId.clear();
    m_loadedUrl.clear();
    m_viewport = QSize();
}

QImage PvhtmlRenderer::renderFrame(const FrameRequest& request, QString* error)
{
    auto fail = [this, error](const QString& message) -> QImage {
        m_lastError = message;
        if (error)
            *error = message;
        return QImage{};
    };

    if (request.bundleDir.isEmpty())
        return fail(QStringLiteral("HTML frame has no bundle directory"));
    if (!isSafeBundleRelativePath(request.entry))
        return fail(QStringLiteral("HTML entry is not a safe bundle-relative path: %1")
                        .arg(request.entry));
    if (request.size.isEmpty() || !request.size.isValid())
        return fail(QStringLiteral("HTML frame size is invalid"));
    if (request.size.width() > kMaxEdge || request.size.height() > kMaxEdge)
        return fail(QStringLiteral("HTML frame size exceeds %1 px").arg(kMaxEdge));

    const int timeoutMs =
        request.timeoutMs > 0
            ? qBound(kMinTimeoutMs, request.timeoutMs, kMaxTimeoutMs)
            : 3000;

    if (!isRunning())
    {
        QString startError;
        if (!start(&startError))
            return fail(startError);
    }

    FrameClock clock;
    clock.totalMs = timeoutMs;
    clock.timer.start();

    auto timeoutMessage = [timeoutMs]() {
        return QStringLiteral("HTML frame timed out after %1 ms").arg(timeoutMs);
    };

    // Report an error, always returning a null image, while cleaning up the
    // page target so a stuck document cannot poison the next frame.
    auto failFrame = [&](const QString& message) -> QImage {
        dropPage(true);
        return fail(message.isEmpty() ? timeoutMessage() : message);
    };

    QString serverError;
    if (!ensureServer(request.bundleDir, &serverError))
        return fail(serverError);

    pvf::html::CdpClient* cdp = m_browser ? m_browser->cdp() : nullptr;
    if (!cdp || !cdp->isConnected())
        return fail(QStringLiteral("HTML browser is not connected"));

    auto call = [&](const QString& method, const QJsonObject& params,
                    const QString& session, int cap) -> SyncResult {
        SyncResult result;
        const int budget = clock.budget(cap);
        if (budget <= 0)
        {
            result.error = timeoutMessage();
            return result;
        }
        return callSync(cdp, method, params, session, budget);
    };

    // --- page setup -------------------------------------------------------
    if (m_sessionId.isEmpty())
    {
        SyncResult created =
            call(QStringLiteral("Target.createTarget"),
                 QJsonObject{{QStringLiteral("url"), QStringLiteral("about:blank")},
                             {QStringLiteral("background"), false}},
                 QString(), 5000);
        if (!created.ok())
            return failFrame(QStringLiteral("cannot create the HTML page: %1")
                                 .arg(created.error));

        m_targetId = created.result.value(QStringLiteral("targetId")).toString();
        if (m_targetId.isEmpty())
            return failFrame(QStringLiteral("Target.createTarget returned no target id"));

        SyncResult attached =
            call(QStringLiteral("Target.attachToTarget"),
                 QJsonObject{{QStringLiteral("targetId"), m_targetId},
                             {QStringLiteral("flatten"), true}},
                 QString(), 5000);
        if (!attached.ok())
            return failFrame(QStringLiteral("cannot attach to the HTML page: %1")
                                 .arg(attached.error));

        m_sessionId = attached.result.value(QStringLiteral("sessionId")).toString();
        if (m_sessionId.isEmpty())
            return failFrame(QStringLiteral("Target.attachToTarget returned no session id"));

        for (const QString& method : {QStringLiteral("Page.enable"),
                                      QStringLiteral("Runtime.enable")})
        {
            const SyncResult enabled = call(method, QJsonObject{}, m_sessionId, 5000);
            if (!enabled.ok())
                return failFrame(QStringLiteral("cannot enable %1: %2")
                                     .arg(method, enabled.error));
        }

        // Cache disabling is best-effort: a failure only means bundle edits may
        // need a fresh renderer, not that this frame cannot be captured.
        call(QStringLiteral("Network.enable"), QJsonObject{}, m_sessionId, 2000);
        call(QStringLiteral("Network.setCacheDisabled"),
             QJsonObject{{QStringLiteral("cacheDisabled"), true}}, m_sessionId, 2000);
        call(QStringLiteral("Page.setDownloadBehavior"),
             QJsonObject{{QStringLiteral("behavior"), QStringLiteral("deny")}},
             m_sessionId, 2000);

        const QJsonObject transparent{
            {QStringLiteral("color"),
             QJsonObject{{QStringLiteral("r"), 0},
                         {QStringLiteral("g"), 0},
                         {QStringLiteral("b"), 0},
                         {QStringLiteral("a"), 0}}}};
        const SyncResult background =
            call(QStringLiteral("Emulation.setDefaultBackgroundColorOverride"), transparent,
                 m_sessionId, 5000);
        if (!background.ok())
            return failFrame(QStringLiteral("cannot enable a transparent HTML background: %1")
                                 .arg(background.error));
    }

    // --- viewport ---------------------------------------------------------
    if (m_viewport != request.size)
    {
        const QJsonObject metrics{
            {QStringLiteral("width"), request.size.width()},
            {QStringLiteral("height"), request.size.height()},
            {QStringLiteral("deviceScaleFactor"), 1},
            {QStringLiteral("mobile"), false},
            {QStringLiteral("screenWidth"), request.size.width()},
            {QStringLiteral("screenHeight"), request.size.height()},
        };
        const SyncResult applied =
            call(QStringLiteral("Emulation.setDeviceMetricsOverride"), metrics, m_sessionId,
                 5000);
        if (!applied.ok())
            return failFrame(QStringLiteral("cannot set the HTML viewport: %1")
                                 .arg(applied.error));
        m_viewport = request.size;
    }

    // --- navigation and load ---------------------------------------------
    QUrl url = m_server->baseUrl();
    url.setPath(QStringLiteral("/") + request.entry);
    const QString targetUrl = url.toString(QUrl::FullyEncoded);

    if (m_loadedUrl != targetUrl)
    {
        const SyncResult navigation =
            call(QStringLiteral("Page.navigate"),
                 QJsonObject{{QStringLiteral("url"), targetUrl}}, m_sessionId, 5000);
        if (!navigation.ok())
            return failFrame(QStringLiteral("cannot open %1: %2")
                                 .arg(targetUrl, navigation.error));

        const QString navigateError =
            navigation.result.value(QStringLiteral("errorText")).toString();
        if (!navigateError.isEmpty())
            return failFrame(QStringLiteral("cannot open %1: %2")
                                 .arg(targetUrl, navigateError));

        bool loaded = false;
        while (clock.remainingMs() > 0)
        {
            const SyncResult state =
                call(QStringLiteral("Runtime.evaluate"),
                     QJsonObject{
                         {QStringLiteral("expression"),
                          QStringLiteral("({url: document.URL, ready: document.readyState})")},
                         {QStringLiteral("returnByValue"), true}},
                     m_sessionId, 500);
            if (!state.ok())
            {
                if (isTransientNavigationError(state.error))
                {
                    sleepBriefly(20);
                    continue;
                }
                if (state.error.contains(QStringLiteral("timed out")))
                    return failFrame(timeoutMessage());
                return failFrame(QStringLiteral("cannot read the HTML page state: %1")
                                     .arg(state.error));
            }

            const QJsonObject value =
                state.result.value(QStringLiteral("result"))
                    .toObject()
                    .value(QStringLiteral("value"))
                    .toObject();
            const QUrl currentUrl(value.value(QStringLiteral("url")).toString());
            if (currentUrl == url
                && value.value(QStringLiteral("ready")).toString()
                       == QStringLiteral("complete"))
            {
                loaded = true;
                break;
            }
            sleepBriefly(20);
        }
        if (!loaded)
            return failFrame(QStringLiteral("HTML page did not finish loading within %1 ms")
                                 .arg(timeoutMs));

        m_loadedUrl = targetUrl;
    }

    // --- foreground -------------------------------------------------------
    // Headless Chrome only composites the foreground tab, and a background tab
    // may throttle requestAnimationFrame. Activate before dispatching so the
    // page's double-rAF wait and the screenshot both see a live tab.
    const SyncResult activated =
        call(QStringLiteral("Target.activateTarget"),
             QJsonObject{{QStringLiteral("targetId"), m_targetId}}, QString(), 5000);
    if (!activated.ok())
        return failFrame(QStringLiteral("cannot activate the HTML page: %1")
                             .arg(activated.error));

    const SyncResult front =
        call(QStringLiteral("Page.bringToFront"), QJsonObject{}, m_sessionId, 5000);
    if (!front.ok())
        return failFrame(QStringLiteral("cannot bring the HTML page to the front: %1")
                             .arg(front.error));

    // --- frame handshake --------------------------------------------------
    const QJsonObject detail{
        {QStringLiteral("frame"), request.localFrame},
        {QStringLiteral("time"), request.time},
        {QStringLiteral("vars"), QJsonObject::fromVariantMap(request.vars)},
    };
    const QString detailJson =
        QString::fromUtf8(QJsonDocument(detail).toJson(QJsonDocument::Compact));

    const QString frameScript = QStringLiteral(R"JS(
(async function () {
  const detail = %1;
  window.__PVFORGE__ = detail;
  window.dispatchEvent(new CustomEvent("pvforgeframe", { detail: detail }));
  await new Promise(function (resolve) { setTimeout(resolve, 0); });
  const ready = window.__PVFORGE_READY__;
  if (ready && typeof ready.then === "function") {
    await ready;
  }
  if (document.fonts && document.fonts.ready
      && typeof document.fonts.ready.then === "function") {
    await document.fonts.ready;
  }
  await new Promise(function (resolve) {
    requestAnimationFrame(function () { requestAnimationFrame(resolve); });
  });
  return true;
})()
)JS")
                                   .arg(detailJson);

    const SyncResult frameResult =
        call(QStringLiteral("Runtime.evaluate"),
             QJsonObject{{QStringLiteral("expression"), frameScript},
                         {QStringLiteral("awaitPromise"), true},
                         {QStringLiteral("returnByValue"), true},
                         {QStringLiteral("userGesture"), false}},
             m_sessionId, 0);
    if (!frameResult.ok())
    {
        if (frameResult.error.contains(QStringLiteral("timed out")))
            return failFrame(timeoutMessage());
        return failFrame(QStringLiteral("HTML frame handshake failed: %1")
                             .arg(frameResult.error));
    }
    if (frameResult.result.contains(QStringLiteral("exceptionDetails")))
        return failFrame(QStringLiteral("HTML frame script failed: %1")
                             .arg(exceptionText(frameResult.result)));

    // --- screenshot -------------------------------------------------------
    const QJsonObject clip{
        {QStringLiteral("x"), 0},
        {QStringLiteral("y"), 0},
        {QStringLiteral("width"), request.size.width()},
        {QStringLiteral("height"), request.size.height()},
        {QStringLiteral("scale"), 1},
    };
    const QJsonObject captureParams{
        {QStringLiteral("format"), QStringLiteral("png")},
        {QStringLiteral("fromSurface"), true},
        {QStringLiteral("captureBeyondViewport"), false},
        {QStringLiteral("clip"), clip},
    };

    const SyncResult shot =
        call(QStringLiteral("Page.captureScreenshot"), captureParams, m_sessionId, 15000);
    if (!shot.ok())
    {
        if (shot.error.contains(QStringLiteral("timed out")))
            return failFrame(timeoutMessage());
        return failFrame(QStringLiteral("HTML screenshot failed: %1").arg(shot.error));
    }

    const QString encoded = shot.result.value(QStringLiteral("data")).toString();
    if (encoded.isEmpty())
        return failFrame(QStringLiteral("browser returned an empty screenshot"));

    const QByteArray png = QByteArray::fromBase64(encoded.toLatin1());
    QImage image = QImage::fromData(png, "PNG");
    if (image.isNull())
        return failFrame(QStringLiteral("cannot decode the browser screenshot PNG"));
    if (image.size() != request.size)
        return failFrame(QStringLiteral("screenshot is %1x%2, expected %3x%4")
                             .arg(image.width())
                             .arg(image.height())
                             .arg(request.size.width())
                             .arg(request.size.height()));

    m_lastError.clear();
    if (error)
        error->clear();
    return image;
}

} // namespace pvf::html2
