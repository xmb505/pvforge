#include "html/HtmlRenderer.h"

#include "html/CdpClient.h"
#include "html/HtmlBrowser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QTimer>

#include <cmath>

namespace pvf {
namespace html {

namespace {

constexpr int kSetupTimeoutMs = 8000;
constexpr int kDocumentTimeoutMs = 10000;
constexpr int kCaptureTimeoutMs = 15000;

QJsonObject objectFromValue(const QJsonValue& value)
{
    return value.isObject() ? value.toObject() : QJsonObject{};
}

} // namespace

HtmlRenderer::HtmlRenderer(HtmlBrowser* browser, QObject* parent)
    : QObject(parent)
    , m_browser(browser)
{
    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this]() {
        if (!m_inFlight)
            return;
        const quint64 generation = m_activeGeneration;
        abortPage(generation);
        finish(generation, QImage{},
               QStringLiteral("HTML frame timed out after %1 ms")
                   .arg(m_activeRequest.timeoutMs));
    });
}

HtmlRenderer::~HtmlRenderer()
{
    if (m_browser)
        m_browser->cancelCapture(this);
    if (m_inFlight)
        abortPage(m_activeGeneration);
    m_deadline->stop();
    m_inFlight = false;
    m_activeDone = nullptr;
    m_pendingDone = nullptr;
    m_hasPending = false;
}

bool HtmlRenderer::current(quint64 generation) const
{
    return m_inFlight && generation == m_activeGeneration;
}

void HtmlRenderer::requestFrame(const FrameRequest& request, FrameCallback done)
{
    FrameRequest prepared = request;
    if (!prepared.pageSize.isValid())
        prepared.pageSize = prepared.asset.logicalSize;
    if (prepared.timeoutMs <= 0)
        prepared.timeoutMs = 1000;
    prepared.timeoutMs = qBound(100, prepared.timeoutMs, 30000);

    const QString error = validate(prepared);
    if (!error.isEmpty())
    {
        m_lastError = error;
        QTimer::singleShot(0, this, [done = std::move(done), error]() {
            if (done)
                done(QImage{}, error);
        });
        return;
    }

    if (!m_browser)
    {
        const QString message = QStringLiteral("HTML renderer has no browser");
        m_lastError = message;
        QTimer::singleShot(0, this, [done = std::move(done), message]() {
            if (done)
                done(QImage{}, message);
        });
        return;
    }

    if (!m_browser->isRunning())
    {
        // A restarted browser invalidates every page id from the old process.
        m_sessionId.clear();
        m_targetId.clear();
        m_frameId.clear();

        QString startError;
        if (!m_browser->start(QString(), &startError))
        {
            m_lastError = startError;
            QTimer::singleShot(0, this, [done = std::move(done), startError]() {
                if (done)
                    done(QImage{}, startError);
            });
            return;
        }
    }

    if (m_inFlight || m_finishing)
    {
        if (m_hasPending && m_pendingDone)
        {
            FrameCallback dropped = std::move(m_pendingDone);
            QTimer::singleShot(0, this, [dropped = std::move(dropped)]() {
                if (dropped)
                    dropped(QImage{}, QStringLiteral("superseded by a newer frame request"));
            });
        }
        m_pendingRequest = prepared;
        m_pendingDone = std::move(done);
        m_hasPending = true;

        if (m_inFlight && !m_finishing)
        {
            const quint64 generation = m_activeGeneration;
            abortPage(generation);
            finish(generation, QImage{},
                   QStringLiteral("superseded by a newer frame request"));
        }
        return;
    }

    startRequest(prepared, std::move(done));
}

void HtmlRenderer::cancel(const QString& reason)
{
    if (m_hasPending)
    {
        FrameCallback pending = std::move(m_pendingDone);
        m_pendingDone = nullptr;
        m_hasPending = false;
        QTimer::singleShot(0, this, [pending = std::move(pending), reason]() {
            if (pending)
                pending(QImage{}, reason);
        });
    }
    if (m_inFlight)
    {
        const quint64 generation = m_activeGeneration;
        abortPage(generation);
        finish(generation, QImage{}, reason);
    }
}

void HtmlRenderer::startRequest(const FrameRequest& request, FrameCallback done)
{
    m_activeGeneration = ++m_generation;
    m_activeRequest = request;
    m_activeDone = std::move(done);
    m_inFlight = true;
    // Page creation is bounded by per-command timeouts; this outer guard is
    // only a backstop until the page exists. Once ready, the frame timeout is
    // restarted so setup time does not consume the component's budget.
    m_deadline->start(request.timeoutMs + 15000);

    const quint64 generation = m_activeGeneration;
    ensureSession(generation, [this, generation, request]() {
        if (!current(generation))
            return;
        m_deadline->start(request.timeoutMs);
        prepareAndCapture(generation, request);
    });
}

void HtmlRenderer::ensureSession(quint64 generation, std::function<void()> ready)
{
    if (!m_sessionId.isEmpty())
    {
        ready();
        return;
    }
    if (!m_browser || !m_browser->isRunning() || !m_browser->cdp())
    {
        finish(generation, QImage{}, QStringLiteral("HTML browser is not running"));
        return;
    }

    CdpClient* cdp = m_browser->cdp();
    auto failSession = [this, generation](const QString& error) {
        resetSession(true);
        finish(generation, QImage{},
               QStringLiteral("HTML page setup failed: %1").arg(error));
    };

    cdp->call(QStringLiteral("Target.createTarget"),
              QJsonObject{{QStringLiteral("url"), QStringLiteral("about:blank")},
                          {QStringLiteral("background"), false}},
              QString(), kSetupTimeoutMs,
              [this, generation, cdp, failSession, ready](const QJsonObject& result,
                                                          const QString& error) {
                  if (!current(generation))
                      return;
                  if (!error.isEmpty())
                  {
                      failSession(error);
                      return;
                  }
                  m_targetId = result.value(QStringLiteral("targetId")).toString();
                  if (m_targetId.isEmpty())
                  {
                      failSession(QStringLiteral("Target.createTarget returned no target id"));
                      return;
                  }

                  cdp->call(QStringLiteral("Target.attachToTarget"),
                            QJsonObject{{QStringLiteral("targetId"), m_targetId},
                                        {QStringLiteral("flatten"), true}},
                            QString(), kSetupTimeoutMs,
                            [this, generation, cdp, failSession, ready](
                                const QJsonObject& attachResult, const QString& attachError) {
                                if (!current(generation))
                                    return;
                                if (!attachError.isEmpty())
                                {
                                    failSession(attachError);
                                    return;
                                }
                                m_sessionId =
                                    attachResult.value(QStringLiteral("sessionId")).toString();
                                if (m_sessionId.isEmpty())
                                {
                                    failSession(QStringLiteral(
                                        "Target.attachToTarget returned no session id"));
                                    return;
                                }

                                cdp->call(
                                    QStringLiteral("Page.enable"), QJsonObject{}, m_sessionId,
                                    kSetupTimeoutMs,
                                    [this, generation, cdp, failSession, ready](
                                        const QJsonObject&, const QString& pageError) {
                                        if (!current(generation))
                                            return;
                                        if (!pageError.isEmpty())
                                        {
                                            failSession(pageError);
                                            return;
                                        }
                                        cdp->call(
                                            QStringLiteral("Runtime.enable"), QJsonObject{},
                                            m_sessionId, kSetupTimeoutMs,
                                            [this, generation, cdp, failSession, ready](
                                                const QJsonObject&,
                                                const QString& runtimeError) {
                                                if (!current(generation))
                                                    return;
                                                if (!runtimeError.isEmpty())
                                                {
                                                    failSession(runtimeError);
                                                    return;
                                                }
                                                cdp->call(
                                                    QStringLiteral("Network.enable"),
                                                    QJsonObject{}, m_sessionId,
                                                    kSetupTimeoutMs,
                                                    [this, generation, cdp, failSession,
                                                     ready](const QJsonObject&,
                                                            const QString& networkError) {
                                                        if (!current(generation))
                                                            return;
                                                        if (!networkError.isEmpty())
                                                        {
                                                            failSession(networkError);
                                                            return;
                                                        }
                                                        const QJsonArray blockedUrls{
                                                            QStringLiteral("http://*"),
                                                            QStringLiteral("https://*"),
                                                            QStringLiteral("ws://*"),
                                                            QStringLiteral("wss://*"),
                                                            QStringLiteral("ftp://*"),
                                                            QStringLiteral("file://*"),
                                                        };
                                                        cdp->call(
                                                            QStringLiteral(
                                                                "Network.setBlockedURLs"),
                                                            QJsonObject{
                                                                {QStringLiteral("urls"),
                                                                 blockedUrls}},
                                                            m_sessionId, kSetupTimeoutMs,
                                                            [this, generation, cdp,
                                                             failSession, ready](
                                                                const QJsonObject&,
                                                                const QString& blockError) {
                                                                if (!current(generation))
                                                                    return;
                                                                if (!blockError.isEmpty())
                                                                {
                                                                    failSession(blockError);
                                                                    return;
                                                                }
                                                                cdp->call(
                                                                    QStringLiteral(
                                                                        "Network.setCacheDisabled"),
                                                                    QJsonObject{
                                                                        {QStringLiteral(
                                                                             "cacheDisabled"),
                                                                         true}},
                                                                    m_sessionId,
                                                                    kSetupTimeoutMs,
                                                                    [this, generation,
                                                                     cdp, failSession,
                                                                     ready](
                                                                        const QJsonObject&,
                                                                        const QString&
                                                                            cacheError) {
                                                                        if (!current(
                                                                                generation))
                                                                            return;
                                                                        if (!cacheError
                                                                                 .isEmpty())
                                                                        {
                                                                            failSession(
                                                                                cacheError);
                                                                            return;
                                                                        }
                                                                        // Deny downloads;
                                                                        // the sandboxed iframe
                                                                        // also omits
                                                                        // allow-downloads.
                                                                        cdp->call(
                                                                            QStringLiteral(
                                                                                "Page.setDownloadBehavior"),
                                                                            QJsonObject{
                                                                                {QStringLiteral(
                                                                                     "behavior"),
                                                                                 QStringLiteral(
                                                                                     "deny")}},
                                                                            m_sessionId, 2000,
                                                                            [](
                                                                                const QJsonObject&,
                                                                                const QString&) {});
                                                                        cdp->call(
                                                                            QStringLiteral(
                                                                                "Page.getFrameTree"),
                                                                            QJsonObject{},
                                                                            m_sessionId,
                                                                            kSetupTimeoutMs,
                                                                            [this, generation,
                                                                             failSession,
                                                                             ready](
                                                                                const QJsonObject&
                                                                                    treeResult,
                                                                                const QString&
                                                                                    treeError) {
                                                                                if (!current(
                                                                                        generation))
                                                                                    return;
                                                                                if (!treeError
                                                                                         .isEmpty())
                                                                                {
                                                                                    failSession(
                                                                                        treeError);
                                                                                    return;
                                                                                }
                                                                                m_frameId =
                                                                                    objectFromValue(
                                                                                        objectFromValue(
                                                                                            treeResult
                                                                                                .value(
                                                                                                    QStringLiteral(
                                                                                                        "frameTree")))
                                                                                            .value(
                                                                                                QStringLiteral(
                                                                                                    "frame")))
                                                                                        .value(
                                                                                            QStringLiteral(
                                                                                                "id"))
                                                                                        .toString();
                                                                                if (m_frameId
                                                                                        .isEmpty())
                                                                                {
                                                                                    failSession(
                                                                                        QStringLiteral(
                                                                                            "Page.getFrameTree returned no frame id"));
                                                                                    return;
                                                                                }
                                                                                ready();
                                                                            });
                                                                    });
                                                            });
                                                    });
                                            });
                                    });
                            });
              });
}

void HtmlRenderer::prepareAndCapture(quint64 generation, const FrameRequest& request)
{
    if (!current(generation))
        return;
    if (!m_browser || !m_browser->isRunning() || !m_browser->cdp())
    {
        finish(generation, QImage{}, QStringLiteral("HTML browser is not running"));
        return;
    }

    CdpClient* cdp = m_browser->cdp();
    const QString session = m_sessionId;
    const QHash<QString, double> vars = effectiveVariables(request);
    const int width = request.pageSize.width();
    const int height = request.pageSize.height();

    auto fail = [this, generation](const QString& error) {
        finish(generation, QImage{}, error);
    };

    auto call = [this, generation, cdp](const QString& method, const QJsonObject& params,
                                        const QString& target, int timeoutMs,
                                        std::function<void(const QJsonObject&)> next) {
        cdp->call(method, params, target, timeoutMs,
                  [this, generation, next](const QJsonObject& result,
                                           const QString& error) {
                      if (!current(generation))
                          return;
                      if (!error.isEmpty())
                      {
                          // The page session is no longer trustworthy (for
                          // example after a renderer crash). Rebuild it on
                          // the next request instead of failing forever.
                          resetSession(true);
                          finish(generation, QImage{}, error);
                          return;
                      }
                      next(result);
                  });
    };

    const QJsonObject metrics{
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("deviceScaleFactor"), 1},
        {QStringLiteral("mobile"), false},
        {QStringLiteral("screenWidth"), width},
        {QStringLiteral("screenHeight"), height},
    };

    call(QStringLiteral("Emulation.setDeviceMetricsOverride"), metrics, session,
         kSetupTimeoutMs, [=, this](const QJsonObject&) {
             QJsonObject background;
             if (request.asset.transparent)
             {
                 background = QJsonObject{
                     {QStringLiteral("color"),
                      QJsonObject{{QStringLiteral("r"), 0},
                                  {QStringLiteral("g"), 0},
                                  {QStringLiteral("b"), 0},
                                  {QStringLiteral("a"), 0}}}};
             }

             call(QStringLiteral("Emulation.setDefaultBackgroundColorOverride"), background,
                  session, kSetupTimeoutMs, [=, this](const QJsonObject&) {
                      call(QStringLiteral("Page.navigate"),
                           QJsonObject{{QStringLiteral("url"),
                                        QStringLiteral("about:blank")}},
                           session, kSetupTimeoutMs, [=, this](const QJsonObject& navigateResult) {
                               const QString navigateError =
                                   navigateResult.value(QStringLiteral("errorText")).toString();
                               if (!navigateError.isEmpty())
                               {
                                   fail(QStringLiteral("cannot reset the HTML page: %1")
                                            .arg(navigateError));
                                   return;
                               }

                               // The host shell is a fixed trusted document. The
                               // untrusted component lives in a sandboxed iframe
                               // without allow-same-origin.
                               call(QStringLiteral("Page.setDocumentContent"),
                                    QJsonObject{
                                        {QStringLiteral("frameId"), m_frameId},
                                        {QStringLiteral("html"), buildHostDocument()}},
                                    session, kDocumentTimeoutMs, [=, this](const QJsonObject&) {
                                        const QString boot =
                                            buildScript(request, vars, generation);
                                        const QString isolated =
                                            buildIsolatedDocument(request.asset.html, boot);
                                        const QString encoded =
                                            QString::fromUtf8(
                                                QJsonDocument(QJsonArray{isolated})
                                                    .toJson(QJsonDocument::Compact));
                                        const QString setSrcdoc =
                                            QStringLiteral(
                                                "window.__pvfReset('%1');"
                                                "document.getElementById('pvf-frame')"
                                                ".srcdoc = (%2)[0]; true")
                                                .arg(QString::number(generation), encoded);
                                        call(QStringLiteral("Runtime.evaluate"),
                                             QJsonObject{
                                                 {QStringLiteral("expression"), setSrcdoc},
                                                 {QStringLiteral("returnByValue"), true},
                                             },
                                             session, kDocumentTimeoutMs,
                                             [=, this](const QJsonObject& srcdocResult) {
                                                 if (srcdocResult.contains(
                                                         QStringLiteral("exceptionDetails")))
                                                 {
                                                     fail(QStringLiteral(
                                                              "cannot install the sandboxed "
                                                              "component: %1")
                                                              .arg(exceptionText(srcdocResult)));
                                                     return;
                                                 }

                                                 const int waitBudget =
                                                     qMax(100, request.timeoutMs - 50);
                                                 const QJsonObject evaluateParams{
                                                     {QStringLiteral("expression"),
                                                      buildWaitExpression(waitBudget)},
                                                     {QStringLiteral("awaitPromise"), true},
                                                     {QStringLiteral("returnByValue"), true},
                                                     {QStringLiteral("userGesture"), false},
                                                 };
                                                 call(QStringLiteral("Runtime.evaluate"),
                                                      evaluateParams, session,
                                                      request.timeoutMs + 2000,
                                                      [=, this](
                                                          const QJsonObject& evaluateResult) {
                                                          if (evaluateResult.contains(
                                                                  QStringLiteral(
                                                                      "exceptionDetails")))
                                                          {
                                                              fail(QStringLiteral(
                                                                       "HTML page render failed: "
                                                                       "%1")
                                                                       .arg(exceptionText(
                                                                           evaluateResult)));
                                                              return;
                                                          }

                                                          // Only one page can be in the
                                                          // foreground and produce a
                                                          // compositor frame. Serialise the
                                                          // activate + screenshot section
                                                          // across every layer.
                                                          m_browser->acquireCapture(
                                                              this, generation,
                                                              [=, this]() {
                                                                  if (!current(generation))
                                                                  {
                                                                      if (m_browser)
                                                                          m_browser
                                                                              ->releaseCapture(
                                                                                  this);
                                                                      return;
                                                                  }
                                                                  call(
                                                                      QStringLiteral(
                                                                          "Target.activateTarget"),
                                                                      QJsonObject{
                                                                          {QStringLiteral(
                                                                               "targetId"),
                                                                           m_targetId}},
                                                                      QString(), 5000,
                                                                      [=, this](
                                                                          const QJsonObject&) {
                                                                          call(
                                                                              QStringLiteral(
                                                                                  "Page.bringToFront"),
                                                                              QJsonObject{},
                                                                              session, 5000,
                                                                              [=, this](
                                                                                  const QJsonObject&) {
                                                                                  const QJsonObject clip{
                                                                                      {QStringLiteral(
                                                                                           "x"),
                                                                                       0},
                                                                                      {QStringLiteral(
                                                                                           "y"),
                                                                                       0},
                                                                                      {QStringLiteral(
                                                                                           "width"),
                                                                                       width},
                                                                                      {QStringLiteral(
                                                                                           "height"),
                                                                                       height},
                                                                                      {QStringLiteral(
                                                                                           "scale"),
                                                                                       1},
                                                                                  };
                                                                                  const QJsonObject
                                                                                      captureParams{
                                                                                          {QStringLiteral(
                                                                                               "format"),
                                                                                           QStringLiteral(
                                                                                               "png")},
                                                                                          {QStringLiteral(
                                                                                               "fromSurface"),
                                                                                           true},
                                                                                          {QStringLiteral(
                                                                                               "captureBeyondViewport"),
                                                                                           false},
                                                                                          {QStringLiteral(
                                                                                               "clip"),
                                                                                           clip},
                                                                                      };
                                                                                  call(
                                                                                      QStringLiteral(
                                                                                          "Page.captureScreenshot"),
                                                                                      captureParams,
                                                                                      session,
                                                                                      kCaptureTimeoutMs,
                                                                                      [=, this](
                                                                                          const QJsonObject&
                                                                                              captureResult) {
                                                                                          const QString
                                                                                              encoded =
                                                                                                  captureResult
                                                                                                      .value(
                                                                                                          QStringLiteral(
                                                                                                              "data"))
                                                                                                      .toString();
                                                                                          if (encoded
                                                                                                  .isEmpty())
                                                                                          {
                                                                                              fail(
                                                                                                  QStringLiteral(
                                                                                                      "browser returned an empty screenshot"));
                                                                                              return;
                                                                                          }
                                                                                          const QByteArray
                                                                                              png =
                                                                                                  QByteArray::
                                                                                                      fromBase64(
                                                                                                          encoded
                                                                                                              .toLatin1());
                                                                                          QImage image =
                                                                                              QImage::
                                                                                                  fromData(
                                                                                                      png,
                                                                                                      "PNG");
                                                                                          if (image
                                                                                                  .isNull())
                                                                                          {
                                                                                              fail(
                                                                                                  QStringLiteral(
                                                                                                      "cannot decode the browser screenshot PNG"));
                                                                                              return;
                                                                                          }
                                                                                          if (image
                                                                                                  .size()
                                                                                              != request
                                                                                                     .pageSize)
                                                                                          {
                                                                                              fail(
                                                                                                  QStringLiteral(
                                                                                                       "screenshot is %1x%2, expected %3x%4")
                                                                                                      .arg(
                                                                                                          image
                                                                                                              .width())
                                                                                                      .arg(
                                                                                                          image
                                                                                                              .height())
                                                                                                      .arg(
                                                                                                          width)
                                                                                                      .arg(
                                                                                                          height));
                                                                                              return;
                                                                                          }
                                                                                          finish(
                                                                                              generation,
                                                                                              image,
                                                                                              QString());
                                                                                      });
                                                                              });
                                                                      });
                                                              });
                                                      });
                                             });
                                    });
                           });
                  });
         });
}

void HtmlRenderer::finish(quint64 generation, const QImage& image, const QString& error)
{
    if (!current(generation))
        return;

    m_inFlight = false;
    m_deadline->stop();
    if (m_browser)
        m_browser->cancelCapture(this);
    FrameCallback done = std::move(m_activeDone);
    m_activeDone = nullptr;
    if (!error.isEmpty())
        m_lastError = error;

    m_finishing = true;
    if (done)
        done(image, error);
    m_finishing = false;

    startPending();
}

void HtmlRenderer::startPending()
{
    if (!m_hasPending || m_inFlight || m_finishing)
        return;
    FrameRequest request = m_pendingRequest;
    FrameCallback done = std::move(m_pendingDone);
    m_pendingDone = nullptr;
    m_hasPending = false;
    startRequest(request, std::move(done));
}

void HtmlRenderer::resetSession(bool closeTarget)
{
    if (closeTarget && m_browser && m_browser->isRunning() && m_browser->cdp()
        && !m_targetId.isEmpty())
    {
        m_browser->cdp()->call(QStringLiteral("Target.closeTarget"),
                               QJsonObject{{QStringLiteral("targetId"), m_targetId}},
                               QString(), 2000, [](const QJsonObject&, const QString&) {});
    }
    m_sessionId.clear();
    m_targetId.clear();
    m_frameId.clear();
}

void HtmlRenderer::abortPage(quint64 generation)
{
    Q_UNUSED(generation);
    if (m_sessionId.isEmpty() || !m_browser || !m_browser->isRunning()
        || !m_browser->cdp())
        return;
    const QString expression = QStringLiteral(
        "window.__pvfAbort=true;"
        "try{var f=document.getElementById('pvf-frame');"
        "if(f&&f.contentWindow)f.contentWindow.postMessage({type:'pvf-abort'},'*');}"
        "catch(e){}true");
    m_browser->cdp()->call(
        QStringLiteral("Runtime.evaluate"),
        QJsonObject{{QStringLiteral("expression"), expression},
                    {QStringLiteral("returnByValue"), true}},
        m_sessionId, 500, [](const QJsonObject&, const QString&) {});
}

QString HtmlRenderer::validate(const FrameRequest& request) const
{
    if (request.asset.html.isEmpty())
        return QStringLiteral("HTML asset has no document");
    if (!request.pageSize.isValid())
        return QStringLiteral("HTML frame size is invalid");
    if (request.pageSize.width() > Limits::kMaxEdge
        || request.pageSize.height() > Limits::kMaxEdge)
        return QStringLiteral("HTML frame size exceeds %1 px").arg(Limits::kMaxEdge);
    if (request.timeoutMs < 100)
        return QStringLiteral("HTML frame timeout is too short");
    return QString();
}

QHash<QString, double> HtmlRenderer::effectiveVariables(const FrameRequest& request) const
{
    QHash<QString, double> values;
    for (const HtmlComponent& component : request.asset.components)
    {
        for (const HtmlVariable& variable : component.variables)
        {
            const QString key = component.id + QLatin1Char('.') + variable.name;
            double value = request.vars.value(key, variable.defaultValue);
            if (!std::isfinite(value))
                value = variable.defaultValue;
            value = qBound(variable.min, value, variable.max);
            if (variable.integer)
                value = std::round(value);
            value = qBound(variable.min, value, variable.max);
            values.insert(key, value);
        }
    }
    return values;
}

QString HtmlRenderer::buildHostDocument() const
{
    // The host document is fully trusted and contains no asset markup. The
    // component is confined to a sandboxed iframe without allow-same-origin,
    // so window.parent access throws and the origin is opaque.
    return QStringLiteral(R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-src 'self' data:; img-src data:; connect-src 'none'; media-src 'none'; object-src 'none'; worker-src 'none'">
<style>
  html, body { margin: 0; padding: 0; width: 100%; height: 100%;
               background: transparent; overflow: hidden; }
  iframe { display: block; border: 0; width: 100%; height: 100%;
           background: transparent; }
</style>
</head>
<body>
<iframe id="pvf-frame" sandbox="allow-scripts" loading="eager"
        referrerpolicy="no-referrer"
        allow="accelerometer 'none'; camera 'none'; geolocation 'none'; microphone 'none'; midi 'none'; payment 'none'; usb 'none'; serial 'none'; bluetooth 'none'; clipboard-read 'none'; clipboard-write 'none'; fullscreen 'none'"></iframe>
<script>
(function () {
  "use strict";
  window.__pvfReady = false;
  window.__pvfRenderedCount = 0;
  window.__pvfAbort = false;
  window.__pvfError = null;
  window.__pvfToken = "";
  window.__pvfReset = function (token) {
    window.__pvfReady = false;
    window.__pvfRenderedCount = 0;
    window.__pvfAbort = false;
    window.__pvfError = null;
    window.__pvfToken = String(token);
  };
  var frame = document.getElementById("pvf-frame");
  window.__pvfFrame = frame;
  window.addEventListener("message", function (event) {
    if (event.source !== frame.contentWindow) return;
    if (event.origin !== "null") return;
    var data = event.data;
    if (!data || typeof data.type !== "string" || data.type.length > 32) return;
    if (data.token !== window.__pvfToken) return;
    if (data.type === "pvf-ready") {
      window.__pvfReady = true;
    } else if (data.type === "pvf-rendered") {
      window.__pvfRenderedCount = (window.__pvfRenderedCount | 0) + 1;
    } else if (data.type === "pvf-error") {
      if (!window.__pvfError) {
        window.__pvfError = String(data.message || "page error").slice(0, 1000);
      }
    }
  });
})();
</script>
</body>
</html>)HTML");
}

QString HtmlRenderer::buildIsolatedDocument(const QString& html,
                                            const QString& bootScript) const
{
    const QString csp =
        QStringLiteral("default-src 'none'; script-src 'unsafe-inline'; "
                       "style-src 'unsafe-inline'; img-src data:; font-src data:; "
                       "connect-src 'none'; media-src 'none'; object-src 'none'; "
                       "frame-src 'none'; child-src 'none'; worker-src 'none'; "
                       "manifest-src 'none'; base-uri 'none'; form-action 'none'; "
                       "navigate-to 'none'");
    QString script = bootScript;
    script.replace(QStringLiteral("</script"), QStringLiteral("<\\/script"),
                   Qt::CaseInsensitive);
    const QString payload =
        QStringLiteral("<meta http-equiv=\"Content-Security-Policy\" content=\"%1\">\n"
                       "<script>%2</script>\n")
            .arg(csp, script);

    static const QRegularExpression headPattern(
        QStringLiteral("<head(?:\\s[^>]*)?>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch head = headPattern.match(html);
    if (head.hasMatch())
    {
        const int at = head.capturedEnd();
        return html.left(at) + payload + html.mid(at);
    }

    static const QRegularExpression htmlPattern(
        QStringLiteral("<html(?:\\s[^>]*)?>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch htmlTag = htmlPattern.match(html);
    if (htmlTag.hasMatch())
    {
        const int at = htmlTag.capturedEnd();
        return html.left(at) + QStringLiteral("<head>") + payload + QStringLiteral("</head>")
               + html.mid(at);
    }

    static const QRegularExpression doctypePattern(
        QStringLiteral("^\\s*<!doctype[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch doctype = doctypePattern.match(html);
    if (doctype.hasMatch())
    {
        const int at = doctype.capturedEnd();
        return html.left(at) + QStringLiteral("<head>") + payload + QStringLiteral("</head>")
               + html.mid(at);
    }
    return QStringLiteral("<head>") + payload + QStringLiteral("</head>") + html;
}

QString HtmlRenderer::buildScript(const FrameRequest& request,
                                  const QHash<QString, double>& vars, quint64 token) const
{
    QJsonObject grouped;
    for (const HtmlComponent& component : request.asset.components)
    {
        QJsonObject values;
        for (const HtmlVariable& variable : component.variables)
        {
            const QString key = component.id + QLatin1Char('.') + variable.name;
            values.insert(variable.name, vars.value(key, variable.defaultValue));
        }
        grouped.insert(component.id, values);
    }

    const quint32 seed = quint32(request.frame) * 2654435761u
                         ^ quint32(qRound64(request.time * 1000.0));
    const QJsonObject config{
        {QStringLiteral("vars"), grouped},
        {QStringLiteral("frame"), request.frame},
        {QStringLiteral("time"), request.time},
        {QStringLiteral("width"), request.pageSize.width()},
        {QStringLiteral("height"), request.pageSize.height()},
        {QStringLiteral("seed"), double(seed)},
        {QStringLiteral("deterministic"), request.deterministic},
        {QStringLiteral("token"), QString::number(token)},
    };
    const QString configJson =
        QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));

    QString script = QStringLiteral(R"JS((function () {
  "use strict";
  var cfg = __PVF_CONFIG__;
  var realNow = Date.now.bind(Date);
  window.__pvfRealNow = realNow;
  window.__pvfReady = false;
  window.__pvfRenderedCount = 0;
  window.__pvfAbort = false;
  window.__pvfError = null;
  var post = function (type, extra) {
    var message = { type: type, token: String(cfg.token) };
    if (extra) {
      for (var key in extra) {
        if (Object.prototype.hasOwnProperty.call(extra, key)) message[key] = extra[key];
      }
    }
    try { window.parent.postMessage(message, "*"); } catch (ignored) {}
  };
  window.addEventListener("message", function (event) {
    if (event.source !== window.parent) return;
    if (event.data && event.data.type === "pvf-abort") window.__pvfAbort = true;
  });
  window.pvf = {
    vars: cfg.vars,
    frame: cfg.frame,
    time: cfg.time,
    width: cfg.width,
    height: cfg.height,
    asset: function () { return ""; },
    ready: function () {
      window.__pvfReady = true;
      post("pvf-ready");
    },
    rendered: function () {
      window.__pvfRenderedCount = (window.__pvfRenderedCount | 0) + 1;
      var signal = function () { post("pvf-rendered"); };
      try {
        if (document.fonts && document.fonts.ready) {
          document.fonts.ready.then(signal, signal);
        } else {
          signal();
        }
      } catch (ignored) {
        signal();
      }
    }
  };
  var installErrorHooks = function () {
    window.addEventListener("error", function (event) {
      if (!window.__pvfError) {
        window.__pvfError = String(event.message || event.error || "script error");
        post("pvf-error", { message: window.__pvfError });
      }
    }, true);
    window.addEventListener("unhandledrejection", function (event) {
      if (!window.__pvfError) {
        window.__pvfError = "unhandled rejection: " + String(event.reason);
        post("pvf-error", { message: window.__pvfError });
      }
    });
  };
  installErrorHooks();
  try {
    window.WebSocket = undefined;
    window.EventSource = undefined;
    window.RTCPeerConnection = undefined;
    window.webkitRTCPeerConnection = undefined;
    window.RTCSessionDescription = undefined;
    window.RTCIceCandidate = undefined;
    window.Worker = undefined;
    window.SharedWorker = undefined;
    window.OffscreenCanvas = undefined;
    window.open = function () { return null; };
    if (navigator.sendBeacon) navigator.sendBeacon = function () { return false; };
  } catch (ignored) {}
  try {
    var originalGetContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (type, attributes) {
      var name = String(type).toLowerCase();
      if (name === "webgl" || name === "webgl2" || name === "experimental-webgl")
        return null;
      return originalGetContext.call(this, type, attributes);
    };
  } catch (ignored) {}
  if (cfg.deterministic) {
    var fixedNow = Number(cfg.time) * 1000.0;
    var RealDate = Date;
    function PvfDate() {
      if (!(this instanceof PvfDate)) return new RealDate(fixedNow).toString();
      if (arguments.length === 0) return new RealDate(fixedNow);
      return new RealDate(...arguments);
    }
    PvfDate.prototype = RealDate.prototype;
    PvfDate.now = function () { return fixedNow; };
    PvfDate.parse = RealDate.parse;
    PvfDate.UTC = RealDate.UTC;
    window.Date = PvfDate;
    try {
      Object.defineProperty(window.performance, "now", {
        configurable: true,
        writable: true,
        value: function () { return fixedNow; }
      });
    } catch (error) {
      try { window.performance.now = function () { return fixedNow; }; } catch (ignored) {}
    }
    var seed = Number(cfg.seed) >>> 0;
    Math.random = function () {
      seed = (seed + 0x6D2B79F5) | 0;
      var t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    window.requestAnimationFrame = function (callback) {
      return window.setTimeout(function () { callback(fixedNow); }, 0);
    };
    window.cancelAnimationFrame = function (id) { window.clearTimeout(id); };
    var injectStyle = function () {
      var root = document.head || document.documentElement;
      if (!root || document.querySelector("style[data-pvf-deterministic]")) return;
      var style = document.createElement("style");
      style.setAttribute("data-pvf-deterministic", "1");
      style.textContent =
        "*,*::before,*::after{animation:none!important;transition:none!important}" +
        "html,body{overflow:hidden!important}";
      root.appendChild(style);
    };
    injectStyle();
    document.addEventListener("DOMContentLoaded", injectStyle, { once: true });
    if (!document.documentElement) {
      var observer = new MutationObserver(function () {
        if (document.documentElement) {
          injectStyle();
          observer.disconnect();
        }
      });
      observer.observe(document, { childList: true, subtree: true });
    }
  }
})();)JS");

    script.replace(QStringLiteral("__PVF_CONFIG__"), configJson);
    return script;
}

QString HtmlRenderer::buildWaitExpression(int timeoutMs) const
{
    QString expression = QStringLiteral(R"JS((async function () {
  var realNow = function () { return Date.now(); };
  var started = realNow();
  var budget = __PVF_MAX_WAIT__;
  function expired() { return realNow() - started > budget; }
  await new Promise(function (resolve, reject) {
    function tick() {
      try {
        if (window.__pvfError) {
          reject(new Error("page error: " + window.__pvfError));
          return;
        }
        if (window.__pvfAbort) {
          reject(new Error("render aborted"));
          return;
        }
        if (window.__pvfReady) {
          resolve();
          return;
        }
        if (expired()) {
          reject(new Error("timed out waiting for pvf.ready"));
          return;
        }
        setTimeout(tick, 5);
      } catch (error) {
        reject(error);
      }
    }
    tick();
  });
  await new Promise(function (resolve, reject) {
    function tick() {
      try {
        if (window.__pvfError) {
          reject(new Error("page error: " + window.__pvfError));
          return;
        }
        if (window.__pvfAbort) {
          reject(new Error("render aborted"));
          return;
        }
        if ((window.__pvfRenderedCount | 0) > 0) {
          resolve();
          return;
        }
        if (expired()) {
          reject(new Error("timed out waiting for pvf.rendered"));
          return;
        }
        setTimeout(tick, 5);
      } catch (error) {
        reject(error);
      }
    }
    tick();
  });
  // rAF is unreliable in headless Chrome. Let the rendered message land,
  // force one layout pass, and let the compositor pick up the iframe before
  // Page.captureScreenshot runs.
  await new Promise(function (resolve) { setTimeout(resolve, 0); });
  try { void document.body.offsetHeight; } catch (ignored) {}
  return "ok";
})())JS");
    expression.replace(QStringLiteral("__PVF_MAX_WAIT__"), QString::number(timeoutMs));
    return expression;
}

QString HtmlRenderer::exceptionText(const QJsonObject& result)
{
    const QJsonObject details = result.value(QStringLiteral("exceptionDetails")).toObject();
    QString text = details.value(QStringLiteral("exception"))
                       .toObject()
                       .value(QStringLiteral("description"))
                       .toString();
    if (text.isEmpty())
        text = details.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        text = QStringLiteral("unknown page exception");
    return text;
}

} // namespace html
} // namespace pvf
