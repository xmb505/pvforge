#include "html2/LocalBundleServer.h"
#include "html2/PvhtmlRenderer.h"

#include "html/HtmlBrowser.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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

bool chromeAvailable()
{
    return !pvf::html::HtmlBrowser::detectExecutable().isEmpty();
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
                               "timeoutDoesNotHang\n";
        return 2;
    }

    const QString name = QString::fromLocal8Bit(argv[1]);
    if (name == QStringLiteral("serverServesAndRejects"))
        serverServesAndRejects();
    else if (name == QStringLiteral("renderFrameColors"))
        renderFrameColors();
    else if (name == QStringLiteral("timeoutDoesNotHang"))
        timeoutDoesNotHang();
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
