#include "html2/HtmlFrameService.h"
#include "html2/LocalBundleServer.h"
#include "html2/PvhtmlRenderer.h"

#include "html/HtmlBrowser.h"
#include "htmlcache/FrameKey.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;
    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3")
                               .arg(QString::fromLatin1(file))
                               .arg(line)
                               .arg(QString::fromLatin1(expression))
                        << '\n';
}

void checkDetail(bool condition, const char* expression, const char* file, int line,
                 const QString& detail)
{
    ++gChecks;
    if (condition)
        return;
    ++gFailures;
    QTextStream(stderr) << QStringLiteral("FAIL %1:%2  %3  [%4]")
                               .arg(QString::fromLatin1(file))
                               .arg(line)
                               .arg(QString::fromLatin1(expression), detail)
                        << '\n';
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)
#define CHECK_DETAIL(condition, detail) \
    checkDetail((condition), #condition, __FILE__, __LINE__, (detail))

bool writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(content) == content.size();
}

bool colorNear(const QImage& image, int x, int y, const QColor& expected, int tolerance = 8)
{
    if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return false;
    const QColor actual = image.pixelColor(x, y);
    return qAbs(actual.red() - expected.red()) <= tolerance
           && qAbs(actual.green() - expected.green()) <= tolerance
           && qAbs(actual.blue() - expected.blue()) <= tolerance
           && qAbs(actual.alpha() - expected.alpha()) <= tolerance;
}

struct HttpResponse
{
    int status = 0;
    QByteArray headers;
    QByteArray body;
    bool valid = false;
};

HttpResponse sendRequest(quint16 port, const QByteArray& request)
{
    HttpResponse response;
    QTcpSocket socket;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QByteArray data;
    bool finished = false;
    auto stop = [&]() {
        if (!finished)
        {
            finished = true;
            loop.quit();
        }
    };

    QObject::connect(&socket, &QTcpSocket::connected, &loop,
                     [&socket, &request]() { socket.write(request); });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop,
                     [&socket, &data]() { data += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, stop);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop,
                     [&stop](QAbstractSocket::SocketError) { stop(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, stop);

    timer.start(5000);
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!finished)
        loop.exec(QEventLoop::ExcludeUserInputEvents);

    data += socket.readAll();
    if (socket.state() != QAbstractSocket::UnconnectedState)
        socket.abort();

    const int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return response;

    response.headers = data.left(headerEnd);
    response.body = data.mid(headerEnd + 4);
    const QByteArray statusLine =
        response.headers.left(response.headers.indexOf("\r\n"));
    const QList<QByteArray> parts = statusLine.split(' ');
    if (parts.size() >= 2)
        response.status = parts.at(1).toInt();
    response.valid = response.status != 0;
    return response;
}

HttpResponse get(quint16 port, const QByteArray& target)
{
    QByteArray request = "GET " + target + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Connection: close\r\n\r\n";
    return sendRequest(port, request);
}

bool headerStartsWith(const HttpResponse& response, const QByteArray& prefix)
{
    return response.headers.contains("Content-Type: " + prefix);
}

QString buildRenderBundle(const QString& parentDir)
{
    const QString bundle = parentDir + QStringLiteral("/render-bundle");
    if (!QDir().mkpath(bundle))
        return QString();

    const QByteArray html = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<link rel="stylesheet" href="style.css">
</head>
<body>
<div id="box"></div>
<div id="marker"></div>
<div id="label"></div>
<script src="script.js"></script>
</body>
</html>
)HTML";
    const QByteArray css = R"CSS(html, body {
  margin: 0;
  padding: 0;
  width: 100%;
  height: 100%;
  overflow: hidden;
  background: #000000;
}
#box { position: fixed; inset: 0; }
#marker { position: fixed; left: 0; top: 0; width: 24px; height: 24px; }
#label { position: fixed; left: 0; bottom: 0; color: #ffffff; font: 12px monospace; }
)CSS";
    const QByteArray script = R"JS((function () {
  window.addEventListener("pvforgeframe", function (event) {
    var detail = event.detail || {};
    var vars = detail.vars || {};
    var box = document.getElementById("box");
    var marker = document.getElementById("marker");
    var label = document.getElementById("label");
    if (box) { box.style.background = vars.color || "#000000"; }
    if (marker) {
      marker.style.background =
        ((detail.frame | 0) % 2 === 0) ? "#ffffff" : "#000000";
    }
    if (label) {
      label.textContent = "frame=" + detail.frame + " time=" + detail.time;
    }
  });
})();
)JS";

    if (!writeFile(bundle + QStringLiteral("/index.html"), html))
        return QString();
    if (!writeFile(bundle + QStringLiteral("/style.css"), css))
        return QString();
    if (!writeFile(bundle + QStringLiteral("/script.js"), script))
        return QString();
    return bundle;
}

QString buildSimpleBundle(const QString& parentDir)
{
    const QString bundle = parentDir + QStringLiteral("/simple-bundle");
    if (!QDir().mkpath(bundle))
        return QString();
    const QByteArray html =
        "<!doctype html><html><body style=\"margin:0;background:#00ff00\"></body></html>";
    if (!writeFile(bundle + QStringLiteral("/index.html"), html))
        return QString();
    return bundle;
}

QString buildNeverReadyBundle(const QString& parentDir)
{
    const QString bundle = parentDir + QStringLiteral("/never-ready");
    if (!QDir().mkpath(bundle))
        return QString();
    const QByteArray html = R"HTML(<!doctype html>
<html><body>
<script>
window.addEventListener("pvforgeframe", function () {
  window.__PVFORGE_READY__ = new Promise(function () {});
});
</script>
</body></html>
)HTML";
    if (!writeFile(bundle + QStringLiteral("/index.html"), html))
        return QString();
    return bundle;
}

QString buildDelayedBundle(const QString& parentDir, int delayMs)
{
    const QString bundle = parentDir + QStringLiteral("/delayed-bundle");
    if (!QDir().mkpath(bundle))
        return QString();
    const QString html = QStringLiteral(R"HTML(<!doctype html>
<html><body style="margin:0;background:#0000ff">
<script>
window.addEventListener("pvforgeframe", function () {
  window.__PVFORGE_READY__ = new Promise(function (resolve) {
    setTimeout(resolve, %1);
  });
});
</script>
</body></html>
)HTML").arg(delayMs);
    if (!writeFile(bundle + QStringLiteral("/index.html"), html.toUtf8()))
        return QString();
    return bundle;
}

bool chromeAvailable()
{
    return !pvf::html::HtmlBrowser::detectExecutable().isEmpty();
}

bool waitForFuture(QFuture<QImage>& future, int timeoutMs)
{
    if (future.isFinished())
        return true;

    QEventLoop loop;
    QFutureWatcher<QImage> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QImage>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    return future.isFinished();
}

void waitBriefly(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

void drainEvents()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void serverServesAndRejects()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString root = temporary.path();
    const QString bundle = root + QStringLiteral("/bundle");
    CHECK(QDir().mkpath(bundle));

    CHECK(writeFile(bundle + QStringLiteral("/index.html"),
                    "<!doctype html><title>ok</title><p>hello</p>"));
    CHECK(writeFile(bundle + QStringLiteral("/style.css"), "body{color:#fff}\n"));
    CHECK(writeFile(bundle + QStringLiteral("/script.js"), "window.pvf=1;\n"));
    CHECK(writeFile(bundle + QStringLiteral("/real.txt"), "REAL\n"));
    CHECK(writeFile(bundle + QStringLiteral("/pixel.png"), "png"));
    CHECK(writeFile(bundle + QStringLiteral("/photo.jpg"), "jpg"));
    CHECK(writeFile(bundle + QStringLiteral("/image.webp"), "webp"));
    CHECK(writeFile(bundle + QStringLiteral("/icon.svg"), "<svg/>"));
    CHECK(writeFile(bundle + QStringLiteral("/font.woff2"), "woff2"));
    CHECK(writeFile(bundle + QStringLiteral("/data.json"), "{}"));
    CHECK(writeFile(root + QStringLiteral("/secret.txt"), "SECRET\n"));
    CHECK(QFile::link(bundle + QStringLiteral("/real.txt"),
                      bundle + QStringLiteral("/alias.txt")));
    CHECK(QFile::link(root + QStringLiteral("/secret.txt"),
                      bundle + QStringLiteral("/escape.txt")));

    pvf::html2::LocalBundleServer server;
    QString error;
    CHECK_DETAIL(server.start(bundle, &error), error);
    CHECK(server.isRunning());
    CHECK(server.port() != 0);
    CHECK(server.rootDir() == QFileInfo(bundle).canonicalFilePath());
    if (server.port() == 0)
        return;

    const HttpResponse index = get(server.port(), "/");
    CHECK(index.valid);
    CHECK(index.status == 200);
    CHECK(headerStartsWith(index, "text/html"));
    CHECK(index.body.contains("hello"));

    const HttpResponse css = get(server.port(), "/style.css");
    CHECK(css.status == 200);
    CHECK(headerStartsWith(css, "text/css"));

    const HttpResponse js = get(server.port(), "/script.js");
    CHECK(js.status == 200);
    CHECK(headerStartsWith(js, "text/javascript"));

    const HttpResponse png = get(server.port(), "/pixel.png");
    CHECK(png.status == 200);
    CHECK(headerStartsWith(png, "image/png"));

    const HttpResponse jpg = get(server.port(), "/photo.jpg");
    CHECK(jpg.status == 200);
    CHECK(headerStartsWith(jpg, "image/jpeg"));

    const HttpResponse webp = get(server.port(), "/image.webp");
    CHECK(webp.status == 200);
    CHECK(headerStartsWith(webp, "image/webp"));

    const HttpResponse svg = get(server.port(), "/icon.svg");
    CHECK(svg.status == 200);
    CHECK(headerStartsWith(svg, "image/svg+xml"));

    const HttpResponse woff2 = get(server.port(), "/font.woff2");
    CHECK(woff2.status == 200);
    CHECK(headerStartsWith(woff2, "font/woff2"));

    const HttpResponse jsonFile = get(server.port(), "/data.json");
    CHECK(jsonFile.status == 200);
    CHECK(headerStartsWith(jsonFile, "application/json"));

    const HttpResponse missing = get(server.port(), "/missing.json");
    CHECK(missing.status == 404);

    const HttpResponse internalLink = get(server.port(), "/alias.txt");
    CHECK(internalLink.status == 200);
    CHECK(internalLink.body.contains("REAL"));

    const HttpResponse traversal = get(server.port(), "/../secret.txt");
    CHECK(traversal.valid);
    CHECK(traversal.status == 403);
    CHECK(!traversal.body.contains("SECRET"));

    const HttpResponse encodedTraversal = get(server.port(), "/%2e%2e/secret.txt");
    CHECK(encodedTraversal.valid);
    CHECK(encodedTraversal.status == 403);
    CHECK(!encodedTraversal.body.contains("SECRET"));

    const HttpResponse absoluteForm = sendRequest(
        server.port(),
        "GET http://127.0.0.1/secret.txt HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    CHECK(absoluteForm.valid);
    CHECK(absoluteForm.status == 400);
    CHECK(!absoluteForm.body.contains("SECRET"));

    const HttpResponse symlinkEscape = get(server.port(), "/escape.txt");
    CHECK(symlinkEscape.valid);
    CHECK(symlinkEscape.status == 403);
    CHECK(!symlinkEscape.body.contains("SECRET"));

    server.stop();
    CHECK(!server.isRunning());
}

void renderFrameColors()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP: no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::PvhtmlRenderer renderer;
    QString error;
    CHECK_DETAIL(renderer.start(&error), error);
    CHECK(renderer.isRunning());
    CHECK(!renderer.chromeVersion().isEmpty());
    if (!renderer.isRunning())
        return;

    pvf::html2::FrameRequest request;
    request.bundleDir = bundle;
    request.size = QSize(320, 180);
    request.timeoutMs = 5000;
    request.localFrame = 0;
    request.time = 0.0;
    request.vars.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));

    error.clear();
    QImage first = renderer.renderFrame(request, &error);
    CHECK_DETAIL(!first.isNull(), error);
    if (!first.isNull())
    {
        CHECK(first.size() == QSize(320, 180));
        CHECK(colorNear(first, 160, 90, QColor(255, 0, 0)));
        // localFrame 0 paints a white parity marker at the top-left corner.
        CHECK(colorNear(first, 5, 5, QColor(255, 255, 255)));
    }

    request.localFrame = 1;
    request.time = 0.033;
    request.vars.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));

    error.clear();
    QImage second = renderer.renderFrame(request, &error);
    CHECK_DETAIL(!second.isNull(), error);
    if (!second.isNull())
    {
        CHECK(second.size() == QSize(320, 180));
        CHECK(colorNear(second, 160, 90, QColor(0, 255, 0)));
        // A different frame number must reach the page without a reload.
        CHECK(colorNear(second, 5, 5, QColor(0, 0, 0)));
    }

    renderer.stop();
    CHECK(!renderer.isRunning());
}

void timeoutDoesNotHang()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP: no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString neverReady = buildNeverReadyBundle(temporary.path());
    const QString simple = buildSimpleBundle(temporary.path());
    CHECK(!neverReady.isEmpty());
    CHECK(!simple.isEmpty());
    if (neverReady.isEmpty() || simple.isEmpty())
        return;

    pvf::html2::PvhtmlRenderer renderer;
    QString error;
    CHECK_DETAIL(renderer.start(&error), error);
    if (!renderer.isRunning())
        return;

    pvf::html2::FrameRequest request;
    request.bundleDir = neverReady;
    request.size = QSize(160, 90);
    request.timeoutMs = 500;

    QElapsedTimer timer;
    timer.start();
    error.clear();
    const QImage image = renderer.renderFrame(request, &error);
    const qint64 elapsed = timer.elapsed();

    CHECK(image.isNull());
    CHECK(!error.isEmpty());
    CHECK(elapsed < 6000);
    CHECK(renderer.isRunning());

    // A timed-out page must not poison the next frame. Switching bundles also
    // restarts the loopback server and navigates the reused browser.
    request.bundleDir = simple;
    request.timeoutMs = 5000;
    error.clear();
    const QImage recovered = renderer.renderFrame(request, &error);
    CHECK_DETAIL(!recovered.isNull(), error);
    if (!recovered.isNull())
        CHECK(colorNear(recovered, 80, 45, QColor(0, 255, 0)));

    renderer.stop();
    CHECK(!renderer.isRunning());
}

int cachedWebpCount(const QString& directory)
{
    return QDir(directory)
        .entryList(QStringList{QStringLiteral("*.webp")}, QDir::Files)
        .size();
}

pvf::html2::FrameRequest lowResRequest(const QString& bundle, const QString& assetHash,
                                      int localFrame = 0)
{
    pvf::html2::FrameRequest request;
    request.assetHash = assetHash;
    request.bundleDir = bundle;
    request.size = QSize(160, 90);
    request.timeoutMs = 5000;
    request.localFrame = localFrame;
    request.time = localFrame / 30.0;
    request.vars.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    return request;
}

void frameCacheHits()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    CHECK(service.isRunning());
    CHECK(!service.chromeVersion().isEmpty());
    if (!service.isRunning())
        return;

    const pvf::html2::FrameRequest request =
        lowResRequest(bundle, QStringLiteral("frame-cache-asset"));

    error.clear();
    const QImage first = service.frame(request, &error);
    CHECK_DETAIL(!first.isNull(), error);
    const pvf::html2::Stats firstStats = service.stats();
    CHECK(firstStats.renders == 1);
    CHECK(firstStats.memoryHits + firstStats.diskHits == 0);

    error.clear();
    const QImage second = service.frame(request, &error);
    CHECK_DETAIL(!second.isNull(), error);
    CHECK(second.size() == QSize(160, 90));

    const pvf::html2::Stats secondStats = service.stats();
    CHECK(secondStats.renders == firstStats.renders);
    CHECK(secondStats.memoryHits + secondStats.diskHits
          > firstStats.memoryHits + firstStats.diskHits);

    // A different variable value is a different key and must render again.
    pvf::html2::FrameRequest other = request;
    other.vars.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));
    error.clear();
    const QImage different = service.frame(other, &error);
    CHECK_DETAIL(!different.isNull(), error);
    if (!different.isNull())
        CHECK(colorNear(different, 80, 45, QColor(0, 255, 0)));
    CHECK(service.stats().renders == firstStats.renders + 1);

    service.stop();
    CHECK(!service.isRunning());
}

void staticFramesShareCache()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    pvf::html2::FrameRequest request =
        lowResRequest(bundle, QStringLiteral("static-asset"), 0);
    request.timeDependent = false;

    error.clear();
    const QImage first = service.frame(request, &error);
    CHECK_DETAIL(!first.isNull(), error);
    CHECK(service.stats().renders == 1);

    // Even though the parity marker would turn black on frame 7, a static
    // bundle is normalised to frame 0 and served from the same cache entry.
    request.localFrame = 7;
    request.time = 7.0 / 30.0;
    error.clear();
    const QImage second = service.frame(request, &error);
    CHECK_DETAIL(!second.isNull(), error);
    if (!second.isNull())
        CHECK(colorNear(second, 5, 5, QColor(255, 255, 255)));

    const pvf::html2::Stats stats = service.stats();
    CHECK(stats.renders == 1);
    CHECK(stats.memoryHits + stats.diskHits >= 1);

    service.stop();
}

void prefetchFillsCache()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    const pvf::html2::FrameRequest first =
        lowResRequest(bundle, QStringLiteral("prefetch-asset"), 0);
    const pvf::html2::FrameRequest second =
        lowResRequest(bundle, QStringLiteral("prefetch-asset"), 1);

    int prefetched = 0;
    QString prefetchError;
    QEventLoop loop;
    QObject::connect(&service, &pvf::html2::HtmlFrameService::prefetchedFrame, &loop,
                     [&prefetched, &loop](const pvf::AssetHash&, int) {
                         ++prefetched;
                         if (prefetched >= 2)
                             loop.quit();
                     });
    QObject::connect(&service, &pvf::html2::HtmlFrameService::prefetchError, &loop,
                     [&prefetchError, &loop](const QString& message) {
                         prefetchError = message;
                         loop.quit();
                     });
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);

    service.prefetch(QVector<pvf::html2::FrameRequest>{first, second});
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    CHECK_DETAIL(prefetchError.isEmpty(), prefetchError);
    CHECK(prefetched == 2);
    const pvf::html2::Stats prefetchedStats = service.stats();
    CHECK(prefetchedStats.prefetched == 2);
    CHECK(prefetchedStats.renders == 2);

    // The cache thread filled both entries; the frontend must hit one.
    error.clear();
    const QImage image = service.frame(first, &error);
    CHECK_DETAIL(!image.isNull(), error);
    const pvf::html2::Stats hitStats = service.stats();
    CHECK(hitStats.renders == prefetchedStats.renders);
    CHECK(hitStats.memoryHits + hitStats.diskHits >= 1);

    // Prefetching the same frames again reports them but must not re-render.
    prefetched = 0;
    prefetchError.clear();
    service.prefetch(QVector<pvf::html2::FrameRequest>{first, second});
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    CHECK_DETAIL(prefetchError.isEmpty(), prefetchError);
    CHECK(prefetched == 2);
    const pvf::html2::Stats repeatStats = service.stats();
    CHECK(repeatStats.renders == prefetchedStats.renders);
    CHECK(repeatStats.prefetched == prefetchedStats.prefetched);

    service.stop();
}

void finalFrameUncached()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    const QString cacheRoot = temporary.path() + QStringLiteral("/cache");
    pvf::html2::HtmlFrameService service(cacheRoot);
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    const pvf::html2::FrameRequest low =
        lowResRequest(bundle, QStringLiteral("final-asset"));
    error.clear();
    const QImage lowImage = service.frame(low, &error);
    CHECK_DETAIL(!lowImage.isNull(), error);
    const int entriesBefore = cachedWebpCount(cacheRoot);
    const pvf::html2::Stats lowStats = service.stats();
    CHECK(lowStats.renders == 1);
    CHECK(entriesBefore == 1);

    pvf::html2::FrameRequest high = low;
    high.size = QSize(640, 360);
    error.clear();
    const QImage highImage = service.finalFrame(high, &error);
    CHECK_DETAIL(!highImage.isNull(), error);
    if (!highImage.isNull())
        CHECK(highImage.size() == QSize(640, 360));

    const pvf::html2::Stats finalStats = service.stats();
    CHECK(finalStats.renders == lowStats.renders + 1);
    CHECK(cachedWebpCount(cacheRoot) == entriesBefore);

    // The low-resolution cache is untouched and still hits.
    error.clear();
    const QImage cachedAgain = service.frame(low, &error);
    CHECK_DETAIL(!cachedAgain.isNull(), error);
    CHECK(service.stats().renders == finalStats.renders);
    CHECK(service.stats().memoryHits + service.stats().diskHits >= 1);

    service.stop();
}

void frameKeyVaries()
{
    using pvf::htmlcache::FrameKey;
    using pvf::htmlcache::canonicalKey;

    FrameKey base;
    base.assetHash = QStringLiteral("asset-a");
    base.localFrame = 3;
    base.size = QSize(320, 180);
    base.chromeVersion = QStringLiteral("Chrome/140.0.0.0");
    base.entryHash = QStringLiteral("entry-a");
    base.variables.insert(QStringLiteral("speed"), 1.5);
    base.variables.insert(QStringLiteral("label"), QStringLiteral("hello"));

    const QString baseline = canonicalKey(base);
    CHECK(canonicalKey(base) == baseline);

    FrameKey variant = base;
    variant.chromeVersion = QStringLiteral("Chrome/141.0.0.0");
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.variables.insert(QStringLiteral("speed"), 1.6);
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.variables.insert(QStringLiteral("speed"), 1.5000004);
    CHECK(canonicalKey(variant) == baseline); // six-decimal normalisation

    variant = base;
    variant.variables.insert(QStringLiteral("extra"), 1);
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.assetHash = QStringLiteral("asset-b");
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.localFrame = 4;
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.size = QSize(321, 180);
    CHECK(canonicalKey(variant) != baseline);

    variant = base;
    variant.entryHash = QStringLiteral("entry-b");
    CHECK(canonicalKey(variant) != baseline);
}

void asyncRequestLowRes()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    const pvf::html2::FrameRequest request =
        lowResRequest(bundle, QStringLiteral("async-asset"));

    int readyCount = 0;
    QObject::connect(&service, &pvf::html2::HtmlFrameService::frameReady, &service,
                     [&readyCount](const pvf::AssetHash&, int, const QImage&) {
                         ++readyCount;
                     });

    QFuture<QImage> future = service.requestLowRes(request);
    CHECK(waitForFuture(future, 20000));
    CHECK(future.isFinished());
    if (future.isFinished() && future.resultCount() > 0)
    {
        const QImage image = future.result();
        CHECK(!image.isNull());
        if (!image.isNull())
        {
            CHECK(image.size() == QSize(160, 90));
            CHECK(colorNear(image, 80, 45, QColor(255, 0, 0)));
        }
    }
    drainEvents();
    CHECK(readyCount == 1);
    CHECK(service.stats().renders == 1);

    // A second request is a cache hit: already finished, no new frameReady.
    QFuture<QImage> hit = service.requestLowRes(request);
    CHECK(hit.isFinished());
    if (hit.isFinished() && hit.resultCount() > 0)
        CHECK(!hit.result().isNull());
    drainEvents();
    CHECK(readyCount == 1);
    CHECK(service.stats().renders == 1);
    CHECK(service.stats().memoryHits + service.stats().diskHits >= 1);

    service.stop();
}

void lowResWaitTimeout()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildDelayedBundle(temporary.path(), 700);
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    const pvf::html2::FrameRequest request =
        lowResRequest(bundle, QStringLiteral("wait-timeout-asset"));

    int readyCount = 0;
    QImage readyImage;
    QObject::connect(&service, &pvf::html2::HtmlFrameService::frameReady, &service,
                     [&readyCount, &readyImage](const pvf::AssetHash&, int, const QImage& image) {
                         ++readyCount;
                         readyImage = image;
                     });

    QElapsedTimer timer;
    timer.start();
    error.clear();
    const QImage image = service.lowResWait(request, 300, &error);
    const qint64 elapsed = timer.elapsed();

    CHECK(image.isNull());
    CHECK(error == QStringLiteral("timeout"));
    CHECK(elapsed < 5000);
    CHECK(readyCount == 0);

    // The timed-out request keeps running and fills the cache.
    if (readyCount == 0)
    {
        QEventLoop loop;
        QObject::connect(&service, &pvf::html2::HtmlFrameService::frameReady, &loop,
                         [&loop](const pvf::AssetHash&, int, const QImage&) { loop.quit(); });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    CHECK(readyCount == 1);
    CHECK(!readyImage.isNull());
    CHECK(service.stats().renders == 1);

    QFuture<QImage> hit = service.requestLowRes(request);
    CHECK(hit.isFinished());
    if (hit.isFinished() && hit.resultCount() > 0)
        CHECK(!hit.result().isNull());
    CHECK(service.stats().renders == 1);

    service.stop();
}

void lowResPreemption()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString slowBundle = buildNeverReadyBundle(temporary.path());
    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!slowBundle.isEmpty());
    CHECK(!bundle.isEmpty());
    if (slowBundle.isEmpty() || bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    int readyCount = 0;
    int failedCount = 0;
    QObject::connect(&service, &pvf::html2::HtmlFrameService::frameReady, &service,
                     [&readyCount](const pvf::AssetHash&, int, const QImage&) {
                         ++readyCount;
                     });
    QObject::connect(&service, &pvf::html2::HtmlFrameService::frameFailed, &service,
                     [&failedCount](const pvf::AssetHash&, int, const QString&) {
                         ++failedCount;
                     });

    // Occupy the frontend worker with a frame that only fails after 800 ms.
    pvf::html2::FrameRequest slow =
        lowResRequest(slowBundle, QStringLiteral("preempt-slow"));
    slow.timeoutMs = 800;
    QFuture<QImage> slowFuture = service.requestLowRes(slow);
    waitBriefly(100);

    pvf::html2::FrameRequest queued =
        lowResRequest(bundle, QStringLiteral("preempt-fast"));
    queued.vars.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
    pvf::html2::FrameRequest newest = queued;
    newest.vars.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));

    QFuture<QImage> preempted = service.requestLowRes(queued);
    QFuture<QImage> latest = service.requestLowRes(newest);

    CHECK(preempted.isFinished());
    if (preempted.isFinished())
        CHECK(preempted.resultCount() == 0);

    CHECK(waitForFuture(latest, 20000));
    if (latest.isFinished() && latest.resultCount() > 0)
    {
        const QImage image = latest.result();
        CHECK(!image.isNull());
        if (!image.isNull())
            CHECK(colorNear(image, 80, 45, QColor(0, 255, 0)));
    }
    CHECK(waitForFuture(slowFuture, 20000));

    drainEvents();
    CHECK(readyCount == 1);
    CHECK(failedCount == 1);
    CHECK(service.stats().renders == 1);

    service.stop();
}

void cancelPendingLowRes()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString slowBundle = buildNeverReadyBundle(temporary.path());
    const QString bundle = buildRenderBundle(temporary.path());
    CHECK(!slowBundle.isEmpty());
    CHECK(!bundle.isEmpty());
    if (slowBundle.isEmpty() || bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    int readyCount = 0;
    QObject::connect(&service, &pvf::html2::HtmlFrameService::frameReady, &service,
                     [&readyCount](const pvf::AssetHash&, int, const QImage&) {
                         ++readyCount;
                     });

    pvf::html2::FrameRequest slow =
        lowResRequest(slowBundle, QStringLiteral("cancel-slow"));
    slow.timeoutMs = 800;
    QFuture<QImage> slowFuture = service.requestLowRes(slow);
    waitBriefly(100);

    pvf::html2::FrameRequest queued =
        lowResRequest(bundle, QStringLiteral("cancel-queued"));
    queued.vars.insert(QStringLiteral("color"), QStringLiteral("#0000ff"));
    QFuture<QImage> cancelled = service.requestLowRes(queued);
    CHECK(!cancelled.isFinished());

    service.cancelPendingLowRes();
    CHECK(cancelled.isFinished());
    if (cancelled.isFinished())
        CHECK(cancelled.resultCount() == 0);

    CHECK(waitForFuture(slowFuture, 20000));
    drainEvents();
    CHECK(readyCount == 0);

    // Cancelling queued work must not break the running service.
    pvf::html2::FrameRequest after =
        lowResRequest(bundle, QStringLiteral("cancel-after"));
    after.vars.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));
    QFuture<QImage> afterFuture = service.requestLowRes(after);
    CHECK(waitForFuture(afterFuture, 20000));
    if (afterFuture.isFinished() && afterFuture.resultCount() > 0)
    {
        const QImage image = afterFuture.result();
        CHECK(!image.isNull());
        if (!image.isNull())
            CHECK(colorNear(image, 80, 45, QColor(0, 255, 0)));
    }
    drainEvents();
    CHECK(readyCount == 1);

    service.stop();
}

void finalFrameSizeLimits()
{
    using pvf::html2::validateFrameSize;

    // Pure size validation: no browser needed.
    CHECK(validateFrameSize(QSize(1, 1)).isEmpty());
    CHECK(validateFrameSize(QSize(4000, 100)).isEmpty());
    CHECK(validateFrameSize(QSize(8191, 8192)).isEmpty());
    CHECK(validateFrameSize(QSize(8192, 8192)).isEmpty());
    CHECK(!validateFrameSize(QSize(8193, 100)).isEmpty());
    CHECK(validateFrameSize(QSize(8193, 100)).contains(QStringLiteral("8192")));
    CHECK(!validateFrameSize(QSize(100, 8193)).isEmpty());
    CHECK(!validateFrameSize(QSize(0, 0)).isEmpty());
    CHECK(!validateFrameSize(QSize(-1, 100)).isEmpty());

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildSimpleBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));

    // An oversized request is rejected before any browser starts, and the
    // message names the 8192 limit.
    pvf::html2::FrameRequest oversized;
    oversized.bundleDir = bundle;
    oversized.size = QSize(8193, 100);
    QString error;
    const QImage rejected = service.finalFrame(oversized, &error);
    CHECK(rejected.isNull());
    CHECK(error.contains(QStringLiteral("8192")));
    CHECK(!service.isRunning());
    CHECK(!service.finalRunning());

    // The renderer itself applies the same rule before starting Chrome.
    pvf::html2::PvhtmlRenderer renderer;
    pvf::html2::FrameRequest huge;
    huge.bundleDir = bundle;
    huge.size = QSize(8193, 8192);
    QString rendererError;
    CHECK(renderer.renderFrame(huge, &rendererError).isNull());
    CHECK(rendererError.contains(QStringLiteral("8192")));
    CHECK(!renderer.isRunning());

    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP: no Chromium-based browser found\n";
        return;
    }

    QString startError;
    CHECK_DETAIL(service.start(&startError), startError);
    if (!service.isRunning())
        return;

    // 4000x100 stays below the old 4096 cap only on one axis; both axes are
    // well within 8192 and a real render must still work.
    pvf::html2::FrameRequest wide;
    wide.bundleDir = bundle;
    wide.size = QSize(4000, 100);
    wide.timeoutMs = 15000;
    error.clear();
    const QImage image = service.finalFrame(wide, &error);
    CHECK_DETAIL(!image.isNull(), error);
    if (!image.isNull())
    {
        CHECK(image.size() == QSize(4000, 100));
        CHECK(colorNear(image, 2000, 50, QColor(0, 255, 0)));
    }
    CHECK(service.finalRunning());
    CHECK(service.stats().renders == 1);

    service.stopFinal();
    CHECK(!service.finalRunning());
    service.stop();
}

void finalRendererRestart()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP: no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildSimpleBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    CHECK(!service.finalRunning());
    service.stopFinal(); // no-op before the final renderer exists
    CHECK(!service.finalRunning());

    QString error;
    CHECK_DETAIL(service.start(&error), error);
    if (!service.isRunning())
        return;

    pvf::html2::FrameRequest request;
    request.bundleDir = bundle;
    request.size = QSize(100, 100);
    request.timeoutMs = 10000;

    error.clear();
    const QImage first = service.finalFrame(request, &error);
    CHECK_DETAIL(!first.isNull(), error);
    CHECK(service.finalRunning());
    CHECK(service.stats().renders == 1);

    // stopFinal() releases the final browser but keeps the service (frontend
    // and cache browsers) running.
    service.stopFinal();
    CHECK(!service.finalRunning());
    CHECK(service.isRunning());

    // The next finalFrame() lazily creates a fresh final renderer.
    error.clear();
    const QImage second = service.finalFrame(request, &error);
    CHECK_DETAIL(!second.isNull(), error);
    if (!second.isNull())
        CHECK(colorNear(second, 50, 50, QColor(0, 255, 0)));
    CHECK(service.finalRunning());
    CHECK(service.stats().renders == 2);

    service.stopFinal();
    CHECK(!service.finalRunning());
    service.stop();
    CHECK(!service.isRunning());
}

void customCacheBudgets()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildSimpleBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    // Tiny explicit budgets must be forwarded to MemoryCache/DiskCache.
    const QString smallRoot = temporary.path() + QStringLiteral("/cache-small");
    {
        pvf::html2::HtmlFrameService service(smallRoot, 1024LL * 1024, 2LL * 1024 * 1024);
        QString error;
        CHECK_DETAIL(service.start(&error), error);
        if (service.isRunning())
        {
            pvf::html2::FrameRequest request =
                lowResRequest(bundle, QStringLiteral("budget-small"));
            request.size = QSize(64, 36);
            error.clear();
            const QImage image = service.frame(request, &error);
            CHECK_DETAIL(!image.isNull(), error);
            if (!image.isNull())
                CHECK(image.size() == QSize(64, 36));
            CHECK(cachedWebpCount(smallRoot) == 1);
            CHECK(service.stats().renders == 1);
        }
        service.stop();
    }

    // A different budget pair is equally valid.
    const QString largeRoot = temporary.path() + QStringLiteral("/cache-large");
    {
        pvf::html2::HtmlFrameService service(largeRoot, 16LL * 1024 * 1024,
                                            64LL * 1024 * 1024);
        QString error;
        CHECK_DETAIL(service.start(&error), error);
        if (service.isRunning())
        {
            pvf::html2::FrameRequest request =
                lowResRequest(bundle, QStringLiteral("budget-large"));
            request.size = QSize(64, 36);
            error.clear();
            const QImage image = service.frame(request, &error);
            CHECK_DETAIL(!image.isNull(), error);
            if (!image.isNull())
                CHECK(image.size() == QSize(64, 36));
            CHECK(cachedWebpCount(largeRoot) == 1);
            CHECK(service.stats().renders == 1);
        }
        service.stop();
    }
}

void chromeExecutableOverride()
{
    if (!chromeAvailable())
    {
        QTextStream(stdout) << "SKIP (QSKIP): no Chromium-based browser found\n";
        return;
    }

    const QString missing = QStringLiteral("/definitely/missing/pvf-chrome");

    // The renderer must honour an explicit executable on start().
    pvf::html2::PvhtmlRenderer renderer;
    renderer.setChromeExecutable(missing);
    CHECK(renderer.chromeExecutable() == missing);
    QString rendererError;
    CHECK(!renderer.start(&rendererError));
    CHECK(!rendererError.isEmpty());
    CHECK(!renderer.isRunning());

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid())
        return;

    const QString bundle = buildSimpleBundle(temporary.path());
    CHECK(!bundle.isEmpty());
    if (bundle.isEmpty())
        return;

    pvf::html2::HtmlFrameService service(temporary.path() + QStringLiteral("/cache"));
    service.setChromeExecutable(missing);
    QString error;
    CHECK(!service.start(&error));
    CHECK(!error.isEmpty());
    CHECK(!service.isRunning());

    // Empty means auto-detection; a stopped service can be restarted.
    service.setChromeExecutable(QString());
    CHECK(service.chromeExecutable().isEmpty());
    error.clear();
    CHECK_DETAIL(service.start(&error), error);
    CHECK(service.isRunning());
    CHECK(!service.chromeVersion().isEmpty());
    if (service.isRunning())
    {
        pvf::html2::FrameRequest request =
            lowResRequest(bundle, QStringLiteral("chrome-override-asset"));
        request.size = QSize(64, 36);
        error.clear();
        const QImage image = service.frame(request, &error);
        CHECK_DETAIL(!image.isNull(), error);
        if (!image.isNull())
            CHECK(colorNear(image, 32, 18, QColor(0, 255, 0)));
    }
    service.stop();
    CHECK(!service.isRunning());
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication application(argc, argv);
    Q_UNUSED(application);

    if (argc < 2)
    {
        QTextStream(stderr) << "usage: pvf_html2_tests <test-case>\n"
                            << "cases: serverServesAndRejects renderFrameColors "
                               "timeoutDoesNotHang frameCacheHits "
                               "staticFramesShareCache prefetchFillsCache "
                               "finalFrameUncached frameKeyVaries "
                               "asyncRequestLowRes lowResWaitTimeout "
                               "lowResPreemption cancelPendingLowRes "
                               "customCacheBudgets chromeExecutableOverride "
                               "finalFrameSizeLimits finalRendererRestart\n";
        return 2;
    }

    const QString name = QString::fromLocal8Bit(argv[1]);
    if (name == QStringLiteral("serverServesAndRejects"))
        serverServesAndRejects();
    else if (name == QStringLiteral("renderFrameColors"))
        renderFrameColors();
    else if (name == QStringLiteral("timeoutDoesNotHang"))
        timeoutDoesNotHang();
    else if (name == QStringLiteral("frameCacheHits"))
        frameCacheHits();
    else if (name == QStringLiteral("staticFramesShareCache"))
        staticFramesShareCache();
    else if (name == QStringLiteral("prefetchFillsCache"))
        prefetchFillsCache();
    else if (name == QStringLiteral("finalFrameUncached"))
        finalFrameUncached();
    else if (name == QStringLiteral("frameKeyVaries"))
        frameKeyVaries();
    else if (name == QStringLiteral("asyncRequestLowRes"))
        asyncRequestLowRes();
    else if (name == QStringLiteral("lowResWaitTimeout"))
        lowResWaitTimeout();
    else if (name == QStringLiteral("lowResPreemption"))
        lowResPreemption();
    else if (name == QStringLiteral("cancelPendingLowRes"))
        cancelPendingLowRes();
    else if (name == QStringLiteral("customCacheBudgets"))
        customCacheBudgets();
    else if (name == QStringLiteral("chromeExecutableOverride"))
        chromeExecutableOverride();
    else if (name == QStringLiteral("finalFrameSizeLimits"))
        finalFrameSizeLimits();
    else if (name == QStringLiteral("finalRendererRestart"))
        finalRendererRestart();
    else
    {
        QTextStream(stderr) << "unknown test case: " << name << '\n';
        return 2;
    }

    QTextStream out(stdout);
    out << (gFailures == 0 ? "PASS " : "FAIL ") << (gChecks - gFailures) << '/'
        << gChecks << " checks\n";
    return gFailures == 0 ? 0 : 1;
}
