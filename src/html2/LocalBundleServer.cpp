#include "html2/LocalBundleServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>

namespace pvf::html2 {

namespace {

constexpr int kMaxRequestBytes = 64 * 1024;

QByteArray reasonPhrase(int status)
{
    switch (status)
    {
    case 200:
        return QByteArrayLiteral("OK");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 403:
        return QByteArrayLiteral("Forbidden");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 414:
        return QByteArrayLiteral("URI Too Long");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    default:
        return QByteArrayLiteral("Error");
    }
}

void sendResponse(QTcpSocket* socket, int status, const QByteArray& contentType,
                  const QByteArray& body, bool headOnly)
{
    QByteArray header;
    header += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status)
              + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    header += "Cache-Control: no-store\r\n";
    header += "X-Content-Type-Options: nosniff\r\n";
    header += "Connection: close\r\n\r\n";

    socket->write(header);
    if (!headOnly && !body.isEmpty())
        socket->write(body);
    socket->disconnectFromHost();
}

} // namespace

bool isSafeBundleRelativePath(const QString& path)
{
    if (path.isEmpty())
        return false;
    if (path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return false;
    if (path.contains(QChar(u'\0')) || path.contains(QLatin1Char('\\')))
        return false;
    if (QDir::isAbsolutePath(path) || QFileInfo(path).isAbsolute())
        return false;

    const QStringList segments = path.split(QLatin1Char('/'));
    for (const QString& segment : segments)
        if (segment == QStringLiteral(".."))
            return false;
    return true;
}

LocalBundleServer::LocalBundleServer(QObject* parent)
    : QObject(parent)
{
}

LocalBundleServer::~LocalBundleServer()
{
    stop();
}

bool LocalBundleServer::start(const QString& bundleDir, QString* error)
{
    stop();
    m_lastError.clear();

    const QFileInfo rootInfo(bundleDir);
    if (!rootInfo.exists() || !rootInfo.isDir())
    {
        m_lastError =
            QStringLiteral("HTML bundle directory does not exist: %1").arg(bundleDir);
        if (error)
            *error = m_lastError;
        return false;
    }

    const QString root = rootInfo.canonicalFilePath();
    if (root.isEmpty())
    {
        m_lastError =
            QStringLiteral("cannot resolve the HTML bundle directory: %1").arg(bundleDir);
        if (error)
            *error = m_lastError;
        return false;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &LocalBundleServer::onNewConnection);

    if (!m_server->listen(QHostAddress::LocalHost, 0))
    {
        m_lastError =
            QStringLiteral("cannot start the HTML bundle server: %1")
                .arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        if (error)
            *error = m_lastError;
        return false;
    }

    m_rootDir = root;
    m_port = m_server->serverPort();
    if (error)
        error->clear();
    return true;
}

void LocalBundleServer::stop()
{
    m_buffers.clear();
    if (m_server)
    {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_rootDir.clear();
    m_port = 0;
}

QUrl LocalBundleServer::baseUrl() const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    if (m_port != 0)
        url.setPort(m_port);
    url.setPath(QStringLiteral("/"));
    return url;
}

void LocalBundleServer::onNewConnection()
{
    if (!m_server)
        return;
    while (QTcpSocket* socket = m_server->nextPendingConnection())
    {
        socket->setParent(m_server);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalBundleServer::onReadyRead(QTcpSocket* socket)
{
    if (!socket || !m_server)
        return;
    if (socket->property("pvfRequestDone").toBool())
    {
        socket->readAll();
        return;
    }

    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaxRequestBytes)
    {
        m_buffers.remove(socket);
        socket->setProperty("pvfRequestDone", true);
        sendError(socket, 414, QStringLiteral("request headers are too large"));
        return;
    }

    const int end = buffer.indexOf("\r\n\r\n");
    if (end < 0)
        return;

    const QByteArray request = buffer.left(end + 4);
    m_buffers.remove(socket);
    socket->setProperty("pvfRequestDone", true);
    handleRequest(socket, request);
}

void LocalBundleServer::handleRequest(QTcpSocket* socket, const QByteArray& request)
{
    const int lineEnd = request.indexOf("\r\n");
    const QByteArray line = lineEnd >= 0 ? request.left(lineEnd) : request;
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 3)
    {
        sendError(socket, 400, QStringLiteral("malformed request line"));
        return;
    }

    const QByteArray method = parts.at(0);
    if (method != "GET" && method != "HEAD")
    {
        sendError(socket, 405, QStringLiteral("only GET and HEAD are supported"));
        return;
    }
    socket->setProperty("pvfHeadOnly", method == "HEAD");

    QByteArray target = parts.at(1);
    const int query = target.indexOf('?');
    if (query >= 0)
        target.truncate(query);
    const int fragment = target.indexOf('#');
    if (fragment >= 0)
        target.truncate(fragment);

    if (!target.startsWith('/'))
    {
        sendError(socket, 400, QStringLiteral("only origin-form request targets are allowed"));
        return;
    }

    QString relative = QUrl::fromPercentEncoding(target);
    if (relative.contains(QChar(u'\0')))
    {
        sendError(socket, 403, QStringLiteral("forbidden path"));
        return;
    }

    if (relative == QStringLiteral("/"))
        relative = QStringLiteral("index.html");
    else
    {
        if (relative.startsWith(QLatin1Char('/')))
            relative.remove(0, 1);
        if (relative.endsWith(QLatin1Char('/')))
            relative += QStringLiteral("index.html");
    }

    if (!isSafeBundleRelativePath(relative))
    {
        sendError(socket, 403, QStringLiteral("path escapes the HTML bundle"));
        return;
    }

    serveFile(socket, relative, method == "HEAD");
}

void LocalBundleServer::serveFile(QTcpSocket* socket, const QString& relativePath,
                                  bool headOnly)
{
    if (m_rootDir.isEmpty())
    {
        sendError(socket, 500, QStringLiteral("server has no bundle root"));
        return;
    }

    const QFileInfo candidate(m_rootDir + QLatin1Char('/') + relativePath);
    if (!candidate.exists() || !candidate.isFile())
    {
        sendError(socket, 404, QStringLiteral("file not found"));
        return;
    }

    const QString canonical = candidate.canonicalFilePath();
    const QString rootPrefix =
        m_rootDir.endsWith(QLatin1Char('/')) ? m_rootDir : m_rootDir + QLatin1Char('/');
    if (canonical.isEmpty() || !canonical.startsWith(rootPrefix))
    {
        sendError(socket, 403, QStringLiteral("path escapes the HTML bundle"));
        return;
    }

    const QFileInfo resolved(canonical);
    if (!resolved.isFile())
    {
        sendError(socket, 404, QStringLiteral("file not found"));
        return;
    }

    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly))
    {
        sendError(socket, 500, QStringLiteral("cannot read the requested file"));
        return;
    }

    const QByteArray body = file.readAll();
    sendResponse(socket, 200, mimeTypeForPath(canonical), body, headOnly);
}

void LocalBundleServer::sendError(QTcpSocket* socket, int status, const QString& reason)
{
    const QByteArray body = reason.toUtf8() + '\n';
    const bool headOnly = socket->property("pvfHeadOnly").toBool();
    sendResponse(socket, status, QByteArrayLiteral("text/plain; charset=utf-8"), body,
                 headOnly);
}

QByteArray LocalBundleServer::mimeTypeForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm"))
        return QByteArrayLiteral("text/html; charset=utf-8");
    if (suffix == QStringLiteral("css"))
        return QByteArrayLiteral("text/css; charset=utf-8");
    if (suffix == QStringLiteral("js") || suffix == QStringLiteral("mjs"))
        return QByteArrayLiteral("text/javascript; charset=utf-8");
    if (suffix == QStringLiteral("json"))
        return QByteArrayLiteral("application/json; charset=utf-8");
    if (suffix == QStringLiteral("png"))
        return QByteArrayLiteral("image/png");
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        return QByteArrayLiteral("image/jpeg");
    if (suffix == QStringLiteral("webp"))
        return QByteArrayLiteral("image/webp");
    if (suffix == QStringLiteral("svg"))
        return QByteArrayLiteral("image/svg+xml");
    if (suffix == QStringLiteral("gif"))
        return QByteArrayLiteral("image/gif");
    if (suffix == QStringLiteral("woff2"))
        return QByteArrayLiteral("font/woff2");
    if (suffix == QStringLiteral("woff"))
        return QByteArrayLiteral("font/woff");
    if (suffix == QStringLiteral("ttf"))
        return QByteArrayLiteral("font/ttf");
    if (suffix == QStringLiteral("otf"))
        return QByteArrayLiteral("font/otf");
    if (suffix == QStringLiteral("wasm"))
        return QByteArrayLiteral("application/wasm");
    return QByteArrayLiteral("application/octet-stream");
}

} // namespace pvf::html2
