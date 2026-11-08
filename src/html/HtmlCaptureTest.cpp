#include "html/HtmlCaptureTest.h"

#include "html/HtmlAsset.h"
#include "html/HtmlBrowser.h"
#include "html/HtmlRenderer.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cmath>
#include <memory>

namespace pvf {
namespace html {

namespace {

struct CaptureResult
{
    QImage image;
    QString error;
    int callbacks = 0;
};

struct CaptureState
{
    bool done = false;
    QImage image;
    QString error;
    int callbacks = 0;
};

QString progressHtml()
{
    return QStringLiteral(R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  html, body { margin: 0; padding: 0; width: 320px; height: 180px;
               background: transparent; overflow: hidden; }
  #track { position: absolute; left: 20px; top: 70px; width: 280px;
           height: 40px; background: #202020; }
  #bar { position: absolute; left: 0; top: 0; height: 40px; width: 0;
         background: #00e676; }
  @keyframes pvfPulse {
    from { background: #ff0000; }
    to { background: #0000ff; }
  }
  #pulse { position: absolute; left: 300px; top: 5px; width: 10px;
           height: 10px; background: #ff0000;
           animation: pvfPulse 1s linear infinite alternate; }
</style>
</head>
<body>
<div id="track"><div id="bar"></div></div>
<div id="pulse"></div>
<script>
  var bar = document.getElementById("bar");
  function render(vars) {
    var value = Number(vars.progress.value);
    bar.style.width = (value / 100 * 280) + "px";
  }
  pvf.render = render;
  render(pvf.vars);
  pvf.ready();
  requestAnimationFrame(function () {
    requestAnimationFrame(function () { pvf.rendered(); });
  });
</script>
</body>
</html>)HTML");
}

QString jsStringLiteral(const QString& value)
{
    const QByteArray array =
        QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QString canaryHtml(const QString& httpBase, const QString& wsUrl)
{
    QString html = QStringLiteral(R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
  html, body { margin: 0; padding: 0; width: 320px; height: 180px;
               background: transparent; overflow: hidden; }
  .probe { position: absolute; top: 10px; width: 20px; height: 20px;
           background: #ff0000; }
</style>
</head>
<body>
<script>
(async function () {
  var httpUrl = __HTTP__;
  var wsUrl = __WS__;
  function attempt(run, timeoutMs) {
    return new Promise(function (resolve) {
      var settled = false;
      function finish(value) {
        if (settled) return;
        settled = true;
        resolve(value);
      }
      try { run(finish); } catch (error) { finish(false); }
      setTimeout(function () { finish(false); }, timeoutMs);
    });
  }
  var results = [];
  results.push(await attempt(function (done) {
    fetch(httpUrl + "/fetch").then(function () { done(true); },
                                function () { done(false); });
  }, 400));
  results.push(await attempt(function (done) {
    fetch("file:///etc/passwd").then(function () { done(true); },
                                     function () { done(false); });
  }, 400));
  results.push(await attempt(function (done) {
    try {
      var socket = new WebSocket(wsUrl);
      socket.onopen = function () {
        try { socket.close(); } catch (error) {}
        done(true);
      };
      socket.onerror = function () { done(false); };
      socket.onclose = function () { done(false); };
    } catch (error) {
      done(false);
    }
  }, 400));
  results.push(await attempt(function (done) {
    var image = new Image();
    image.onload = function () { done(true); };
    image.onerror = function () { done(false); };
    image.src = httpUrl + "/image.png";
  }, 400));
  try {
    results.push(document.createElement("canvas").getContext("webgl") !== null);
  } catch (error) {
    results.push(false);
  }
  try {
    results.push(!!(window.parent && window.parent.pvf));
  } catch (error) {
    results.push(false);
  }
  try {
    results.push(window.open("about:blank") !== null);
  } catch (error) {
    results.push(false);
  }
  for (var index = 0; index < results.length; ++index) {
    var block = document.createElement("div");
    block.className = "probe";
    block.style.left = (10 + index * 30) + "px";
    block.style.background = results[index] ? "#ff0000" : "#00ff00";
    document.body.appendChild(block);
  }
  pvf.ready();
  pvf.rendered();
})();
</script>
</body>
</html>)HTML");
    html.replace(QStringLiteral("__HTTP__"), jsStringLiteral(httpBase));
    html.replace(QStringLiteral("__WS__"), jsStringLiteral(wsUrl));
    return html;
}

QByteArray progressAssetJson(const QString& html)
{
    const QJsonObject variable{
        {QStringLiteral("name"), QStringLiteral("value")},
        {QStringLiteral("type"), QStringLiteral("number")},
        {QStringLiteral("min"), 0},
        {QStringLiteral("max"), 100},
        {QStringLiteral("default"), 0},
        {QStringLiteral("integer"), true},
    };
    const QJsonObject component{
        {QStringLiteral("id"), QStringLiteral("progress")},
        {QStringLiteral("name"), QStringLiteral("Progress")},
        {QStringLiteral("variables"), QJsonArray{variable}},
    };
    const QJsonObject manifest{
        {QStringLiteral("size"), QJsonArray{320, 180}},
        {QStringLiteral("transparent"), true},
        {QStringLiteral("timeDependent"), false},
        {QStringLiteral("components"), QJsonArray{component}},
    };
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("pvforge-html")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("html"), html},
        {QStringLiteral("manifest"), manifest},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

FrameRequest progressRequest(const HtmlAsset& asset, double value, int frame = 0)
{
    FrameRequest request;
    request.asset = asset;
    request.pageSize = QSize(320, 180);
    request.vars.insert(QStringLiteral("progress.value"), value);
    request.frame = frame;
    request.time = double(frame) / 30.0;
    request.timeoutMs = 3000;
    return request;
}

CaptureResult capture(HtmlRenderer& renderer, const FrameRequest& request, int guardMs = 15000)
{
    auto state = std::make_shared<CaptureState>();
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);

    renderer.requestFrame(request, [state, &loop](const QImage& image, const QString& error) {
        if (state->done)
            return;
        state->done = true;
        state->callbacks += 1;
        state->image = image;
        state->error = error;
        loop.quit();
    });

    if (!state->done)
    {
        guard.start(guardMs);
        loop.exec();
    }
    if (!state->done)
    {
        renderer.cancel(QStringLiteral("html-capture-test guard timeout"));
        state->error = QStringLiteral("test guard timeout");
    }

    CaptureResult result;
    result.image = state->image;
    result.error = state->error;
    result.callbacks = state->callbacks;
    return result;
}

QByteArray pixelHash(const QImage& image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < rgba.height(); ++y)
        hash.addData(QByteArrayView(
            reinterpret_cast<const char*>(rgba.constScanLine(y)), rgba.width() * 4));
    return hash.result();
}

bool colorNear(QRgb pixel, int red, int green, int blue, int tolerance = 3)
{
    return std::abs(qRed(pixel) - red) <= tolerance
           && std::abs(qGreen(pixel) - green) <= tolerance
           && std::abs(qBlue(pixel) - blue) <= tolerance;
}

int greenPixels(const QImage& image)
{
    if (image.isNull())
        return 0;
    int count = 0;
    const int y = 90;
    for (int x = 20; x < 300; ++x)
        if (colorNear(image.pixel(x, y), 0, 230, 118))
            ++count;
    return count;
}

} // namespace

int htmlCaptureTest()
{
    const QString browserPath = HtmlBrowser::detectExecutable();
    if (browserPath.isEmpty())
    {
        qInfo().noquote() << "html-capture-test: skipped (no Chrome, Edge or Chromium found)";
        return 0;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical().noquote() << "html-capture-test FAIL:" << message;
            ++failures;
        }
    };

    QString assetError;
    const HtmlAsset asset = HtmlAsset::parse(progressAssetJson(progressHtml()), &assetError);
    check(!asset.html.isEmpty(), QStringLiteral("asset parse: %1").arg(assetError));
    check(asset.logicalSize == QSize(320, 180), QStringLiteral("manifest size parse"));
    check(asset.components.size() == 1 && asset.components.first().variables.size() == 1,
          QStringLiteral("manifest component parse"));
    {
        QString badError;
        const HtmlAsset bad = HtmlAsset::parse(QByteArrayLiteral("{}"), &badError);
        check(bad.html.isEmpty() && !badError.isEmpty(),
              QStringLiteral("invalid asset JSON must be rejected"));
    }
    if (failures > 0)
        return 1;

    // Explicit missing executable: the error must be clear and no process may
    // be spawned.
    {
        HtmlBrowser missing;
        QString error;
        const bool started = missing.start(QStringLiteral("/nonexistent/pvforge-browser"), &error);
        check(!started, QStringLiteral("missing browser path must not start"));
        check(!error.isEmpty(), QStringLiteral("missing browser path needs an error"));
        check(!missing.isRunning(), QStringLiteral("missing browser path left a process"));
        check(!missing.available(), QStringLiteral("missing explicit path must not be available"));
    }
    check(HtmlBrowser::detectExecutable(QStringLiteral("/nonexistent/pvforge-browser")).isEmpty(),
          QStringLiteral("explicit path detection"));

    HtmlBrowser browser;
    QString startError;
    if (!browser.start(QString(), &startError))
    {
        qCritical().noquote() << "html-capture-test FAIL: browser start:" << startError;
        return 1;
    }
    check(browser.available(), QStringLiteral("detected browser must be available"));
    qInfo().noquote() << "html-capture-test: browser" << browser.executablePath()
                      << browser.version();

    {
        HtmlRenderer renderer(&browser);

        // 0% -> no green pixels, 100% -> full 280 px bar.
        const CaptureResult zero = capture(renderer, progressRequest(asset, 0.0, 0));
        check(zero.error.isEmpty(), QStringLiteral("0%% capture: %1").arg(zero.error));
        check(zero.callbacks == 1, QStringLiteral("0%% callback count"));
        check(zero.image.size() == QSize(320, 180), QStringLiteral("0%% image size"));
        check(zero.image.hasAlphaChannel(), QStringLiteral("0%% image keeps alpha"));
        check(qAlpha(zero.image.pixel(1, 1)) == 0, QStringLiteral("0%% corner is transparent"));
        check(greenPixels(zero.image) == 0, QStringLiteral("0%% bar is empty"));

        const CaptureResult full = capture(renderer, progressRequest(asset, 100.0, 1));
        check(full.error.isEmpty(), QStringLiteral("100%% capture: %1").arg(full.error));
        check(full.image.size() == QSize(320, 180), QStringLiteral("100%% image size"));
        check(greenPixels(full.image) >= 275, QStringLiteral("100%% bar is full"));
        check(!colorNear(full.image.pixel(2, 2), 0, 230, 118),
              QStringLiteral("100%% corner stays outside the bar"));
        // The animated element must stay at its base style; otherwise CSS
        // animation would make repeated captures differ.
        check(colorNear(full.image.pixel(305, 10), 255, 0, 0, 8),
              QStringLiteral("CSS animation was not disabled"));

        // Same frame twice must be byte-identical after PNG decode.
        const FrameRequest repeat = progressRequest(asset, 37.0, 9);
        const CaptureResult first = capture(renderer, repeat);
        const CaptureResult second = capture(renderer, repeat);
        check(first.error.isEmpty() && second.error.isEmpty(),
              QStringLiteral("determinism captures succeeded"));
        check(!first.image.isNull() && !second.image.isNull(),
              QStringLiteral("determinism images decoded"));
        check(pixelHash(first.image) == pixelHash(second.image),
              QStringLiteral("same frame pixel hash differs"));

        // Back-to-back different variables must not return the previous
        // compositor frame.
        const CaptureResult low = capture(renderer, progressRequest(asset, 12.0, 20));
        const CaptureResult high = capture(renderer, progressRequest(asset, 88.0, 21));
        check(low.error.isEmpty() && high.error.isEmpty(),
              QStringLiteral("adjacent variable captures succeeded"));
        const int lowPixels = greenPixels(low.image);
        const int highPixels = greenPixels(high.image);
        check(lowPixels < 80, QStringLiteral("12%% bar width: %1").arg(lowPixels));
        check(highPixels > 220, QStringLiteral("88%% bar width: %1").arg(highPixels));
        check(pixelHash(low.image) != pixelHash(high.image),
              QStringLiteral("adjacent frames returned the same pixels"));
        const CaptureResult lowAgain = capture(renderer, progressRequest(asset, 12.0, 20));
        check(lowAgain.error.isEmpty() && pixelHash(lowAgain.image) == pixelHash(low.image),
              QStringLiteral("repeat of the first adjacent frame differs"));

        // Network/file/WebGL/isolation canaries. The trap server only counts
        // direct connections; CSP and the dead proxy must keep it at zero.
        {
            QTcpServer trap;
            check(trap.listen(QHostAddress::LocalHost, 0),
                  QStringLiteral("cannot start the canary trap server"));
            int trapConnections = 0;
            QObject::connect(&trap, &QTcpServer::newConnection, [&trap, &trapConnections]() {
                while (QTcpSocket* socket = trap.nextPendingConnection())
                {
                    ++trapConnections;
                    socket->deleteLater();
                }
            });
            const QString httpBase =
                QStringLiteral("http://127.0.0.1:%1").arg(trap.serverPort());
            const QString wsUrl =
                QStringLiteral("ws://127.0.0.1:%1/canary").arg(trap.serverPort());
            QString canaryError;
            const HtmlAsset canaryAsset =
                HtmlAsset::parse(progressAssetJson(canaryHtml(httpBase, wsUrl)),
                                 &canaryError);
            check(!canaryAsset.html.isEmpty(),
                  QStringLiteral("canary asset parse: %1").arg(canaryError));

            FrameRequest canaryRequest;
            canaryRequest.asset = canaryAsset;
            canaryRequest.pageSize = QSize(320, 180);
            canaryRequest.timeoutMs = 6000;
            const CaptureResult canary = capture(renderer, canaryRequest, 20000);
            check(canary.error.isEmpty(),
                  QStringLiteral("canary capture: %1").arg(canary.error));
            check(canary.image.size() == QSize(320, 180),
                  QStringLiteral("canary image size"));
            for (int index = 0; index < 7; ++index)
            {
                const QRgb pixel = canary.image.pixel(10 + index * 30 + 10, 20);
                check(colorNear(pixel, 0, 255, 0, 8),
                      QStringLiteral("canary probe %1 leaked (pixel %2,%3,%4)")
                          .arg(index)
                          .arg(qRed(pixel))
                          .arg(qGreen(pixel))
                          .arg(qBlue(pixel)));
            }
            trap.close();
            check(trapConnections == 0,
                  QStringLiteral("canary trap received %1 connection(s)")
                      .arg(trapConnections));
        }

        // Two pages must be captured serially with the foreground activated;
        // otherwise the second page's screenshot hangs forever.
        {
            HtmlRenderer second(&browser);
            const CaptureResult secondFrame =
                capture(second, progressRequest(asset, 70.0, 30));
            check(secondFrame.error.isEmpty(),
                  QStringLiteral("second page capture: %1").arg(secondFrame.error));
            check(greenPixels(secondFrame.image) > 170,
                  QStringLiteral("second page bar width"));
            const CaptureResult firstAfterSecond =
                capture(renderer, progressRequest(asset, 30.0, 31));
            check(firstAfterSecond.error.isEmpty(),
                  QStringLiteral("first page after second: %1").arg(firstAfterSecond.error));
            check(greenPixels(firstAfterSecond.image) < 120,
                  QStringLiteral("first page after second bar width"));
        }

        // A component that never calls pvf.ready() must time out, then the
        // page must recover for the next request.
        {
            // Reuse the manifest but replace the document with one that never
            // calls pvf.ready() or pvf.rendered().
            QString timeoutAssetError;
            const HtmlAsset timeoutAsset = HtmlAsset::parse(
                progressAssetJson(QStringLiteral(
                    "<!doctype html><html><body>"
                    "<script>setTimeout(function(){}, 60000);</script>"
                    "</body></html>")),
                &timeoutAssetError);
            check(!timeoutAsset.html.isEmpty(),
                  QStringLiteral("timeout asset parse: %1").arg(timeoutAssetError));

            FrameRequest timeoutRequest = progressRequest(timeoutAsset, 50.0, 2);
            timeoutRequest.timeoutMs = 400;
            const CaptureResult timedOut = capture(renderer, timeoutRequest, 8000);
            check(!timedOut.error.isEmpty(), QStringLiteral("stuck page must fail"));
            check(timedOut.error.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                      || timedOut.error.contains(QStringLiteral("timeout"), Qt::CaseInsensitive),
                  QStringLiteral("timeout error should say timed out: %1").arg(timedOut.error));
            check(timedOut.callbacks == 1, QStringLiteral("timeout callback count"));

            const CaptureResult recovered = capture(renderer, progressRequest(asset, 50.0, 3));
            check(recovered.error.isEmpty(),
                  QStringLiteral("recovery after timeout: %1").arg(recovered.error));
            check(greenPixels(recovered.image) >= 135,
                  QStringLiteral("recovery bar width"));
        }

        // Newer request replaces the in-flight one; the old callback gets one
        // explicit error and the newest frame still renders.
        {
            auto firstState = std::make_shared<CaptureState>();
            auto secondState = std::make_shared<CaptureState>();
            QEventLoop loop;
            QTimer guard;
            guard.setSingleShot(true);
            QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
            guard.start(15000);

            renderer.requestFrame(progressRequest(asset, 10.0, 10),
                                  [firstState](const QImage& image, const QString& error) {
                                      firstState->done = true;
                                      firstState->callbacks += 1;
                                      firstState->image = image;
                                      firstState->error = error;
                                  });
            renderer.requestFrame(progressRequest(asset, 90.0, 11),
                                  [secondState, &loop](const QImage& image, const QString& error) {
                                      if (secondState->done)
                                          return;
                                      secondState->done = true;
                                      secondState->callbacks += 1;
                                      secondState->image = image;
                                      secondState->error = error;
                                      loop.quit();
                                  });
            if (!secondState->done)
                loop.exec();

            check(firstState->callbacks == 1, QStringLiteral("superseded callback count"));
            check(firstState->error.contains(QStringLiteral("superseded")),
                  QStringLiteral("superseded error text: %1").arg(firstState->error));
            check(secondState->callbacks == 1, QStringLiteral("latest callback count"));
            check(secondState->error.isEmpty(),
                  QStringLiteral("latest request error: %1").arg(secondState->error));
            check(greenPixels(secondState->image) >= 245,
                  QStringLiteral("latest request bar width"));
        }

        // 30 sequential captures with varying variables and frame/time.
        {
            QElapsedTimer timer;
            timer.start();
            int captured = 0;
            for (int index = 0; index < 30; ++index)
            {
                const FrameRequest request =
                    progressRequest(asset, double(index) / 29.0 * 100.0, 100 + index);
                const CaptureResult result = capture(renderer, request);
                if (result.error.isEmpty() && !result.image.isNull())
                    ++captured;
                else
                    check(false, QStringLiteral("30-frame capture %1: %2")
                                     .arg(index)
                                     .arg(result.error));
            }
            const qint64 elapsed = timer.elapsed();
            qInfo().noquote() << "html-capture-test: 30 frames in" << elapsed << "ms ("
                              << (elapsed / 30.0) << "ms/frame), captured" << captured;
            check(captured == 30, QStringLiteral("30-frame capture count"));
        }

    }

    browser.stop();
    check(!browser.isRunning(), QStringLiteral("browser stop left the process running"));
    qInfo().noquote() << "html-capture-test: failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace html
} // namespace pvf
