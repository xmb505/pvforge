#include "mcp2/HttpServer.h"

#include "mcp2/Service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>

#ifndef PVFORGE_VERSION
#define PVFORGE_VERSION "0.2.0"
#endif

namespace pvf::mcp2 {

namespace {

constexpr qint64 kMaxBodyBytes = 1024 * 1024;
// An oversized request is drained up to this bound before the 413 response is
// sent, so ordinary HTTP clients can finish uploading and read the status.
constexpr qint64 kMaxDrainBytes = 4 * 1024 * 1024;
constexpr int kMaxHeaderBytes = 16 * 1024;
constexpr int kMaxRequestLineBytes = 8 * 1024;
constexpr int kMaxConnections = 16;
constexpr int kHeaderTimeoutMs = 10 * 1000;
constexpr int kRequestTimeoutMs = 20 * 1000;
constexpr qint64 kMaxConfigBytes = 64 * 1024;

const char kProtocolVersion[] = "2025-06-18";

QString configDirectory()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/PVForge");
}

QString defaultConfigPath()
{
    return configDirectory() + QStringLiteral("/mcp.json");
}

QString defaultEndpointPath()
{
    return configDirectory() + QStringLiteral("/mcp-endpoint.json");
}

QFileDevice::Permissions privateFilePermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

QFileDevice::Permissions privateDirectoryPermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
}

// Writes a private file atomically (temp file in the target directory, then
// rename). The parent directory is created when missing and the file is left
// at mode 0600 because it contains the MCP token.
bool writePrivateFile(const QString& path, const QByteArray& data, QString* error)
{
    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    if (!QDir().mkpath(directory))
    {
        if (error)
            *error = QStringLiteral("无法创建目录 %1").arg(directory);
        return false;
    }
    QFile::setPermissions(directory, privateDirectoryPermissions());

    const QString tempPath =
        path + QStringLiteral(".tmp-%1").arg(QCoreApplication::applicationPid());
    QFile::remove(tempPath);
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (!file.setPermissions(privateFilePermissions()))
    {
        if (error)
            *error = QStringLiteral("无法设置临时文件权限");
        file.close();
        QFile::remove(tempPath);
        return false;
    }
    if (file.write(data) != data.size() || !file.flush())
    {
        if (error)
            *error = file.errorString();
        file.close();
        QFile::remove(tempPath);
        return false;
    }
    file.close();

#if defined(Q_OS_UNIX)
    if (std::rename(QFile::encodeName(tempPath).constData(),
                    QFile::encodeName(path).constData())
        != 0)
    {
        if (error)
            *error = QStringLiteral("无法替换文件 %1").arg(path);
        QFile::remove(tempPath);
        return false;
    }
#else
    QFile::remove(path);
    if (!QFile::rename(tempPath, path))
    {
        if (error)
            *error = QStringLiteral("无法替换文件 %1").arg(path);
        QFile::remove(tempPath);
        return false;
    }
#endif
    if (!QFile::setPermissions(path, privateFilePermissions()))
    {
        if (error)
            *error = QStringLiteral("无法设置文件权限 %1").arg(path);
        return false;
    }
    return true;
}

QByteArray reasonPhrase(int status)
{
    switch (status)
    {
    case 200:
        return QByteArrayLiteral("OK");
    case 202:
        return QByteArrayLiteral("Accepted");
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 401:
        return QByteArrayLiteral("Unauthorized");
    case 403:
        return QByteArrayLiteral("Forbidden");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 408:
        return QByteArrayLiteral("Request Timeout");
    case 411:
        return QByteArrayLiteral("Length Required");
    case 413:
        return QByteArrayLiteral("Content Too Large");
    case 415:
        return QByteArrayLiteral("Unsupported Media Type");
    case 431:
        return QByteArrayLiteral("Request Header Fields Too Large");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    case 503:
        return QByteArrayLiteral("Service Unavailable");
    default:
        return QByteArrayLiteral("Error");
    }
}

void sendHttp(QTcpSocket* socket, int status, const QByteArray& body,
              const QByteArray& contentType = QByteArrayLiteral("application/json"),
              const QByteArray& extraHeaders = QByteArray())
{
    QByteArray head = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' '
                      + reasonPhrase(status) + QByteArrayLiteral("\r\n");
    if (!body.isEmpty() && !contentType.isEmpty())
        head += QByteArrayLiteral("Content-Type: ") + contentType
                + QByteArrayLiteral("\r\n");
    head += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\n");
    head += QByteArrayLiteral("Connection: close\r\n");
    head += extraHeaders;
    head += QByteArrayLiteral("\r\n");
    socket->write(head);
    if (!body.isEmpty())
        socket->write(body);
    socket->disconnectFromHost();
}

void sendHttpError(QTcpSocket* socket, int status, const QString& code,
                   const QString& message)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("error"), code);
    payload.insert(QStringLiteral("message"), message);
    const QByteArray body =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray extra;
    if (status == 401)
        extra = QByteArrayLiteral("WWW-Authenticate: Bearer realm=\"PVForge\"\r\n");
    else if (status == 405)
        extra = QByteArrayLiteral("Allow: POST\r\n");
    sendHttp(socket, status, body, QByteArrayLiteral("application/json"), extra);
}

// Sets a bool for the lifetime of the guard.  Used to make tools/call
// non-reentrant across the batch checkpoint's processEvents().
class BusyFlag
{
public:
    explicit BusyFlag(bool& flag) : m_flag(flag) { m_flag = true; }
    ~BusyFlag() { m_flag = false; }

    BusyFlag(const BusyFlag&) = delete;
    BusyFlag& operator=(const BusyFlag&) = delete;

private:
    bool& m_flag;
};

bool constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    // Hash both sides so the comparison always runs over a fixed length and
    // never reveals the secret length through an early return.
    const QByteArray left =
        QCryptographicHash::hash(a, QCryptographicHash::Sha256);
    const QByteArray right =
        QCryptographicHash::hash(b, QCryptographicHash::Sha256);
    unsigned char diff = 0;
    for (int i = 0; i < left.size(); ++i)
        diff |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    return diff == 0;
}

bool isHeaderNameToken(const QByteArray& name)
{
    if (name.isEmpty())
        return false;
    for (const char raw : name)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9'))
            continue;
        switch (c)
        {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            continue;
        default:
            return false;
        }
    }
    return true;
}

struct ParsedHead
{
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
};

bool parseHttpHead(const QByteArray& head, ParsedHead* parsed, QString* error)
{
    if (head.contains('\0'))
    {
        *error = QStringLiteral("malformed request");
        return false;
    }

    QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
    {
        *error = QStringLiteral("malformed request");
        return false;
    }
    for (QByteArray& line : lines)
        if (line.endsWith('\r'))
            line.chop(1);
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    const QByteArray requestLine = lines.first();
    if (requestLine.size() > kMaxRequestLineBytes)
    {
        *error = QStringLiteral("request line is too long");
        return false;
    }
    const QList<QByteArray> request = requestLine.split(' ');
    if (request.size() != 3 || request.at(0).isEmpty() || request.at(1).isEmpty())
    {
        *error = QStringLiteral("malformed request line");
        return false;
    }
    if (request.at(2) != QByteArrayLiteral("HTTP/1.1"))
    {
        *error = QStringLiteral("only HTTP/1.1 is supported");
        return false;
    }
    parsed->method = request.at(0).toUpper();
    parsed->target = request.at(1);

    for (int i = 1; i < lines.size(); ++i)
    {
        const QByteArray& line = lines.at(i);
        if (line.isEmpty() || line.startsWith(' ') || line.startsWith('\t'))
        {
            *error = QStringLiteral("malformed header line");
            return false;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0)
        {
            *error = QStringLiteral("malformed header line");
            return false;
        }
        const QByteArray name = line.left(colon);
        if (!isHeaderNameToken(name))
        {
            *error = QStringLiteral("malformed header name");
            return false;
        }
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (value.contains('\r'))
        {
            *error = QStringLiteral("malformed header value");
            return false;
        }
        const QByteArray key = name.toLower();
        if (parsed->headers.contains(key))
        {
            // Duplicate Host/Content-Length/Authorization are a smuggling
            // and authentication-bypass vector; reject every duplicate.
            *error = QStringLiteral("duplicate request header");
            return false;
        }
        parsed->headers.insert(key, value);
    }
    return true;
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

struct HttpServer::PendingRequest
{
    QByteArray buffer;
    bool headerDone = false;
    bool done = false;
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    qint64 contentLength = 0;
    bool oversize = false;
    qint64 drained = 0;
    qint64 drainLimit = 0;
};

HttpServer::HttpServer(Service* service, QObject* parent)
    : QObject(parent)
    , m_service(service)
    , m_token(generateToken())
    , m_sessionId(generateToken())
{
}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::setToken(const QByteArray& token)
{
    m_token = token.isEmpty() ? generateToken() : token;
    QString error;
    if (!saveConfig(&error))
        qWarning("mcpserver2: cannot write config %s: %s",
                 qPrintable(configFilePath()), qPrintable(error));
    if (isRunning())
    {
        if (!writeEndpointFile(&error))
            qWarning("mcpserver2: cannot refresh endpoint file %s: %s",
                     qPrintable(endpointFilePath()), qPrintable(error));
    }
}

void HttpServer::resetCredentials()
{
    m_token = generateToken();
    QString error;
    if (!saveConfig(&error))
        qWarning("mcpserver2: cannot write config %s: %s",
                 qPrintable(configFilePath()), qPrintable(error));
    if (isRunning())
    {
        if (!writeEndpointFile(&error))
            qWarning("mcpserver2: cannot refresh endpoint file %s: %s",
                     qPrintable(endpointFilePath()), qPrintable(error));
    }
}

QString HttpServer::endpointFilePath() const
{
    return m_endpointFilePath.isEmpty() ? defaultEndpointPath()
                                        : m_endpointFilePath;
}

void HttpServer::setEndpointFilePath(const QString& path)
{
    m_endpointFilePath = path;
}

QString HttpServer::configFilePath() const
{
    return m_configFilePath.isEmpty() ? defaultConfigPath() : m_configFilePath;
}

void HttpServer::setConfigFilePath(const QString& path)
{
    m_configFilePath = path;
}

void HttpServer::stopAi()
{
    if (m_service)
        m_service->requestStop();
    emit stoppedChanged(true);
}

void HttpServer::resumeAi()
{
    if (m_service)
        m_service->resumeWrites();
    emit stoppedChanged(false);
}

bool HttpServer::aiStopped() const
{
    return m_service && m_service->isStopped();
}

bool HttpServer::loadOrCreateConfig(QString* error)
{
    const QString path = configFilePath();
    if (!QFile::exists(path))
    {
        m_token = generateToken();
        m_configuredPort = kDefaultPort;
        return saveConfig(error);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("无法读取 MCP 配置 %1：%2")
                         .arg(path, file.errorString());
        return false;
    }
    const QByteArray data = file.read(kMaxConfigBytes + 1);
    file.close();
    if (data.size() > kMaxConfigBytes)
    {
        if (error)
            *error = QStringLiteral("MCP 配置 %1 超过 64 KiB").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = QStringLiteral("MCP 配置 %1 不是有效 JSON：%2")
                         .arg(path, parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();

    const QJsonValue hostValue = root.value(QStringLiteral("host"));
    if (!hostValue.isUndefined() && !hostValue.isNull())
    {
        const QString host = hostValue.toString().trimmed().toLower();
        if (host != QLatin1String("127.0.0.1") && host != QLatin1String("localhost"))
        {
            if (error)
                *error = QStringLiteral("MCP 配置 %1 只允许 127.0.0.1").arg(path);
            return false;
        }
    }

    quint16 port = kDefaultPort;
    const QJsonValue portValue = root.value(QStringLiteral("port"));
    if (!portValue.isUndefined() && !portValue.isNull())
    {
        const double number = portValue.toDouble(-1.0);
        if (!portValue.isDouble() || std::floor(number) != number || number < 0.0
            || number > 65535.0)
        {
            if (error)
                *error = QStringLiteral("MCP 配置 %1 的 port 必须是 0..65535 的整数")
                             .arg(path);
            return false;
        }
        port = quint16(number);
    }

    QByteArray token;
    const QJsonValue tokenValue = root.value(QStringLiteral("token"));
    if (!tokenValue.isUndefined() && !tokenValue.isNull())
    {
        if (!tokenValue.isString())
        {
            if (error)
                *error = QStringLiteral("MCP 配置 %1 的 token 必须是字符串").arg(path);
            return false;
        }
        const QString text = tokenValue.toString().trimmed();
        for (const QChar character : text)
        {
            const ushort code = character.unicode();
            if (code < 0x21 || code > 0x7e)
            {
                if (error)
                    *error = QStringLiteral("MCP 配置 %1 的 token 只能是可见 ASCII 字符")
                                 .arg(path);
                return false;
            }
        }
        token = text.toLatin1();
    }
    if (token.isEmpty())
        token = generateToken();

    m_configuredPort = port;
    m_token = token;

    // Persist immediately so a generated token survives even when the
    // configured port turns out to be busy.
    return saveConfig(error);
}

bool HttpServer::saveConfig(QString* error)
{
    QJsonObject root;
    root.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    root.insert(QStringLiteral("port"), int(m_configuredPort));
    root.insert(QStringLiteral("token"), QString::fromLatin1(m_token));

    const QByteArray data = compactJson(root) + '\n';
    return writePrivateFile(configFilePath(), data, error);
}

bool HttpServer::writeEndpointFile(QString* error)
{
    QJsonObject root;
    root.insert(QStringLiteral("url"), endpointUrl());
    root.insert(QStringLiteral("token"), QString::fromLatin1(m_token));
    root.insert(QStringLiteral("port"), int(m_port));
    root.insert(QStringLiteral("pid"), double(QCoreApplication::applicationPid()));

    const QByteArray data = compactJson(root) + '\n';
    if (!writePrivateFile(endpointFilePath(), data, error))
    {
        m_endpointWritten = false;
        return false;
    }
    m_endpointWritten = true;
    return true;
}

void HttpServer::removeEndpointFile()
{
    if (!m_endpointWritten)
        return;
    m_endpointWritten = false;

    const QString path = endpointFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = file.read(kMaxConfigBytes);
    file.close();

    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const qint64 pid = qint64(root.value(QStringLiteral("pid")).toDouble());
    const QString token = root.value(QStringLiteral("token")).toString();
    // Only remove a file this process wrote; another PVForge instance may have
    // replaced it while this one was shutting down.
    if (pid != QCoreApplication::applicationPid()
        || token != QString::fromLatin1(m_token))
        return;
    QFile::remove(path);
}

QByteArray HttpServer::generateToken()
{
    QByteArray token(32, '\0');
    QRandomGenerator* generator = QRandomGenerator::system();
    for (int offset = 0; offset < token.size(); offset += 8)
    {
        const quint64 value = generator->generate64();
        std::memcpy(token.data() + offset, &value,
                    size_t(std::min<qsizetype>(8, token.size() - offset)));
    }
    return token.toHex();
}

quint16 HttpServer::start(quint16 port, QString* error)
{
    stop();

    QString configError;
    if (!loadOrCreateConfig(&configError))
    {
        if (error)
            *error = configError;
        return 0;
    }

    const quint16 listenPort = port != 0 ? port : m_configuredPort;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::acceptConnections);
    if (!m_server->listen(QHostAddress::LocalHost, listenPort))
    {
        if (error)
            *error = QStringLiteral("无法监听 127.0.0.1:%1：%2")
                         .arg(listenPort)
                         .arg(m_server->errorString());
        m_server->deleteLater();
        m_server = nullptr;
        m_port = 0;
        return 0;
    }
    m_port = m_server->serverPort();
    m_configuredPort = m_port;

    QString saveError;
    if (!saveConfig(&saveError))
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
        m_port = 0;
        if (error)
            *error = saveError;
        return 0;
    }

    QString endpointError;
    if (!writeEndpointFile(&endpointError))
    {
        // Endpoint discovery is a convenience for the stdio bridge; a write
        // failure must never take down the authenticated HTTP server.
        qWarning("mcpserver2: cannot write endpoint file %s: %s",
                 qPrintable(endpointFilePath()), qPrintable(endpointError));
    }
    return m_port;
}

void HttpServer::stop()
{
    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    const QList<QTcpSocket*> sockets = findChildren<QTcpSocket*>();
    for (QTcpSocket* socket : sockets)
    {
        socket->abort();
        socket->deleteLater();
    }
    m_connections.clear();
    m_port = 0;
    removeEndpointFile();
}

bool HttpServer::isRunning() const
{
    return m_server && m_server->isListening();
}

QString HttpServer::endpointUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_port);
}

bool HttpServer::isAllowedHost(const QByteArray& host) const
{
    const QByteArray value = host.trimmed();
    if (value.isEmpty() || value.contains(' ') || value.contains('\t')
        || value.contains(',') || value.contains('/'))
        return false;

    const int colon = value.lastIndexOf(':');
    if (colon <= 0 || colon == value.size() - 1)
        return false;

    const QByteArray name = value.left(colon).toLower();
    const QByteArray portText = value.mid(colon + 1);
    bool digits = true;
    for (const char c : portText)
        digits = digits && c >= '0' && c <= '9';
    if (!digits)
        return false;

    bool portOk = false;
    const int parsedPort = portText.toInt(&portOk);
    if (!portOk || parsedPort != int(m_port))
        return false;

    return name == QByteArrayLiteral("127.0.0.1")
           || name == QByteArrayLiteral("localhost");
}

bool HttpServer::isAllowedOrigin(const QByteArray& origin) const
{
    const QByteArray value = origin.trimmed();
    if (value.isEmpty())
        return false;
    const QByteArray lower = value.toLower();
    // Opaque origins (sandboxed pages, file://) are never allowed.
    if (lower == QByteArrayLiteral("null"))
        return false;

    const QByteArray prefix = QByteArrayLiteral("http://");
    if (!lower.startsWith(prefix))
        return false;

    // Reuse the Host rules: only the exact loopback authority is accepted,
    // so a trailing slash, userinfo, another scheme or another port fails.
    return isAllowedHost(lower.mid(prefix.size()));
}

void HttpServer::acceptConnections()
{
    if (!m_server)
        return;
    while (QTcpSocket* socket = m_server->nextPendingConnection())
    {
        socket->setParent(this);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        if (m_connections.size() >= kMaxConnections)
        {
            sendHttpError(socket, 503, QStringLiteral("server_busy"),
                          QStringLiteral("Too many concurrent connections"));
            QTimer::singleShot(2000, socket, [socket] {
                socket->abort();
                socket->deleteLater();
            });
            continue;
        }

        m_connections.insert(socket);
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { m_connections.remove(socket); });
        attachSocket(socket);
    }
}

void HttpServer::attachSocket(QTcpSocket* socket)
{
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    if (!socket->peerAddress().isLoopback())
    {
        sendHttpError(socket, 403, QStringLiteral("loopback_only"),
                      QStringLiteral("MCP only accepts loopback clients"));
        return;
    }

    auto state = std::make_shared<PendingRequest>();
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [socket, state] {
        if (state->done)
            return;
        state->done = true;
        sendHttpError(socket, 408, QStringLiteral("request_timeout"),
                      QStringLiteral("Request timed out"));
    });
    timer->start(kHeaderTimeoutMs);

    connect(socket, &QTcpSocket::readyRead, this, [this, socket, state, timer] {
        if (state->done)
        {
            socket->readAll();
            return;
        }
        state->buffer.append(socket->readAll());

        if (state->oversize)
        {
            state->drained += state->buffer.size();
            state->buffer.clear();
            if (state->drained >= state->drainLimit)
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 413, QStringLiteral("payload_too_large"),
                              QStringLiteral("Request body exceeds %1 bytes")
                                  .arg(kMaxBodyBytes));
            }
            return;
        }

        if (!state->headerDone)
        {
            const int headerEnd = state->buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
            {
                if (state->buffer.size() > kMaxHeaderBytes)
                {
                    state->done = true;
                    timer->stop();
                    sendHttpError(socket, 431, QStringLiteral("header_too_large"),
                                  QStringLiteral("Request headers exceed the limit"));
                }
                return;
            }
            if (headerEnd > kMaxHeaderBytes)
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 431, QStringLiteral("header_too_large"),
                              QStringLiteral("Request headers exceed the limit"));
                return;
            }

            const QByteArray head = state->buffer.left(headerEnd);
            state->buffer.remove(0, headerEnd + 4);
            state->headerDone = true;
            timer->start(kRequestTimeoutMs);

            ParsedHead parsed;
            QString error;
            if (!parseHttpHead(head, &parsed, &error))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 400, QStringLiteral("bad_request"), error);
                return;
            }
            state->method = parsed.method;
            state->target = parsed.target;
            state->headers = parsed.headers;

            // Host is checked before anything else so a DNS-rebinding
            // request can never reach path, auth or body handling.
            if (!isAllowedHost(state->headers.value(QByteArrayLiteral("host"))))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 403, QStringLiteral("bad_host"),
                              QStringLiteral("Host must be 127.0.0.1 or localhost "
                                             "with the server port"));
                return;
            }

            if (state->headers.contains(QByteArrayLiteral("origin"))
                && !isAllowedOrigin(
                    state->headers.value(QByteArrayLiteral("origin"))))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 403, QStringLiteral("bad_origin"),
                              QStringLiteral("Origin is not allowed"));
                return;
            }

            const QByteArray fetchSite =
                state->headers.value(QByteArrayLiteral("sec-fetch-site")).toLower();
            if (!fetchSite.isEmpty() && fetchSite != "same-origin"
                && fetchSite != "same-site" && fetchSite != "none")
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 403, QStringLiteral("bad_origin"),
                              QStringLiteral("Cross-site requests are not allowed"));
                return;
            }

            if (!state->target.startsWith('/'))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 400, QStringLiteral("bad_request"),
                              QStringLiteral("Request target must be an absolute path"));
                return;
            }

            QByteArray path = state->target;
            const int query = path.indexOf('?');
            if (query >= 0)
                path.truncate(query);
            if (path != "/mcp" && path != "/mcp/")
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 404, QStringLiteral("not_found"),
                              QStringLiteral("MCP endpoint is /mcp"));
                return;
            }

            if (state->method != "POST")
            {
                state->done = true;
                timer->stop();
                if (state->method == "GET")
                    sendHttpError(socket, 405, QStringLiteral("method_not_allowed"),
                                  QStringLiteral("Use POST /mcp"));
                else
                    sendHttpError(socket, 404, QStringLiteral("not_found"),
                                  QStringLiteral("MCP endpoint is /mcp"));
                return;
            }

            const QByteArray authorization =
                state->headers.value(QByteArrayLiteral("authorization"));
            const QByteArray scheme = QByteArrayLiteral("bearer ");
            if (!authorization.toLower().startsWith(scheme)
                || !constantTimeEquals(authorization.mid(scheme.size()).trimmed(),
                                       m_token))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 401, QStringLiteral("unauthorized"),
                              QStringLiteral("Missing or invalid Bearer token"));
                return;
            }

            const QByteArray contentType =
                state->headers.value(QByteArrayLiteral("content-type"));
            const QByteArray mediaType =
                contentType.split(';').first().trimmed().toLower();
            if (mediaType != "application/json")
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 415, QStringLiteral("unsupported_media_type"),
                              QStringLiteral("Content-Type must be application/json"));
                return;
            }

            // Never accept chunked framing: it would bypass Content-Length
            // accounting and enables CL.TE request smuggling.
            if (state->headers.contains(QByteArrayLiteral("transfer-encoding")))
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 400, QStringLiteral("bad_request"),
                              QStringLiteral("Transfer-Encoding is not supported"));
                return;
            }

            const QByteArray contentLength =
                state->headers.value(QByteArrayLiteral("content-length"));
            if (contentLength.isEmpty())
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 411, QStringLiteral("length_required"),
                              QStringLiteral("Content-Length is required"));
                return;
            }

            bool lengthOk = true;
            for (const char c : contentLength)
                lengthOk = lengthOk && c >= '0' && c <= '9';
            const qint64 length =
                lengthOk ? contentLength.toLongLong(&lengthOk) : 0;
            if (!lengthOk)
            {
                state->done = true;
                timer->stop();
                sendHttpError(socket, 400, QStringLiteral("bad_request"),
                              QStringLiteral("Invalid Content-Length"));
                return;
            }
            if (length > kMaxBodyBytes || length > INT_MAX)
            {
                // Drain a bounded amount before answering so HTTP clients
                // that upload the body first still receive the 413 status.
                state->oversize = true;
                state->contentLength = length;
                state->drainLimit = std::min<qint64>(length, kMaxDrainBytes);
                state->drained = state->buffer.size();
                state->buffer.clear();
                if (state->drained >= state->drainLimit)
                {
                    state->done = true;
                    timer->stop();
                    sendHttpError(socket, 413, QStringLiteral("payload_too_large"),
                                  QStringLiteral("Request body exceeds %1 bytes")
                                      .arg(kMaxBodyBytes));
                }
                return;
            }
            state->contentLength = length;
        }

        if (state->buffer.size() < state->contentLength)
            return;

        state->done = true;
        timer->stop();
        const QByteArray body = state->buffer.left(int(state->contentLength));

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            sendRpcError(socket, QJsonValue::Null, -32700,
                         QStringLiteral("Parse error"));
            return;
        }
        if (!document.isObject())
        {
            sendRpcError(socket, QJsonValue::Null, -32600,
                         QStringLiteral("Invalid Request: expected a single JSON-RPC object"));
            return;
        }
        handleRpc(socket, document.object());
    });
}

void HttpServer::handleRpc(QTcpSocket* socket, const QJsonObject& request)
{
    const bool notification = !request.contains(QStringLiteral("id"));
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString version = request.value(QStringLiteral("jsonrpc")).toString();
    const QString method = request.value(QStringLiteral("method")).toString();

    if (version != QLatin1String("2.0") || method.isEmpty())
    {
        if (notification)
        {
            sendHttp(socket, 202, QByteArray());
            return;
        }
        sendRpcError(socket, id, -32600, QStringLiteral("Invalid Request"));
        return;
    }

    if (notification)
    {
        // notifications/initialized, notifications/cancelled and other
        // notifications have no response body.
        sendHttp(socket, 202, QByteArray());
        return;
    }

    try
    {
        if (method == QLatin1String("initialize"))
        {
            QJsonObject capabilities;
            capabilities.insert(QStringLiteral("tools"),
                                QJsonObject{{QStringLiteral("listChanged"), false}});

            QJsonObject serverInfo;
            serverInfo.insert(QStringLiteral("name"), QStringLiteral("PVForge"));
            serverInfo.insert(QStringLiteral("version"),
                              QStringLiteral(PVFORGE_VERSION));

            QJsonObject result;
            result.insert(QStringLiteral("protocolVersion"),
                          QString::fromLatin1(kProtocolVersion));
            result.insert(QStringLiteral("capabilities"), capabilities);
            result.insert(QStringLiteral("serverInfo"), serverInfo);
            result.insert(
                QStringLiteral("instructions"),
                QStringLiteral("PVForge MCP：写工具默认可用；工具结果是结构化 JSON，"
                               "失败时返回 {ok:false,error:{code,message}}。"
                               "人类急停期间写工具返回 stopped_by_human，"
                               "只读工具仍可用；等人类恢复后再继续。"
                               "没有启用 Git 时 MCP 只读，写工具返回 permission_denied"
                               "（details.reason=git_disabled）。"));

            const QByteArray extra =
                QByteArrayLiteral("Mcp-Session-Id: ") + m_sessionId
                + QByteArrayLiteral("\r\n");
            sendRpcResult(socket, id, result, extra);
            return;
        }

        if (method == QLatin1String("ping"))
        {
            sendRpcResult(socket, id, QJsonObject());
            return;
        }

        if (method == QLatin1String("tools/list"))
        {
            QJsonObject result;
            if (m_service)
                result = m_service->listTools();
            if (!result.contains(QStringLiteral("tools")))
                result.insert(QStringLiteral("tools"), QJsonArray());
            sendRpcResult(socket, id, result);
            return;
        }

        if (method == QLatin1String("tools/call"))
        {
            if (m_toolCallBusy)
            {
                sendRpcError(socket, id, -32000,
                             QStringLiteral("Server busy: a tool call is already running"));
                return;
            }

            const QJsonValue paramsValue = request.value(QStringLiteral("params"));
            if (!paramsValue.isObject())
            {
                sendRpcError(socket, id, -32602,
                             QStringLiteral("Invalid params: expected an object"));
                return;
            }
            const QJsonObject params = paramsValue.toObject();
            const QString name = params.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
            {
                sendRpcError(socket, id, -32602, QStringLiteral("Missing tool name"));
                return;
            }

            const QJsonValue argsValue = params.value(QStringLiteral("arguments"));
            if (!argsValue.isUndefined() && !argsValue.isNull()
                && !argsValue.isObject())
            {
                sendRpcError(socket, id, -32602,
                             QStringLiteral("Invalid params: arguments must be an object"));
                return;
            }
            const QJsonObject args = argsValue.toObject();

            // Hold the serial-write guard until the response has been
            // produced.  The guard is a stack object so a thrown tool also
            // releases it.
            BusyFlag busy(m_toolCallBusy);

            QElapsedTimer timer;
            timer.start();

            const bool write = Service::isWriteTool(name);
            const QString argsJson = QString::fromUtf8(compactJson(args));
            emit toolCallStarted(name, argsJson, write);

            QJsonObject serviceResult;
            if (m_service)
            {
                serviceResult = m_service->execute(name, args);
            }
            else
            {
                serviceResult.insert(QStringLiteral("ok"), false);
                serviceResult.insert(
                    QStringLiteral("error"),
                    QJsonObject{{QStringLiteral("code"),
                                 QStringLiteral("not_available")},
                                {QStringLiteral("message"),
                                 QStringLiteral("工程未打开")}});
            }

            const bool ok = serviceResult.value(QStringLiteral("ok")).toBool();
            const QByteArray serviceJson = compactJson(serviceResult);
            const qint64 elapsed = timer.elapsed();

            QJsonObject textContent;
            textContent.insert(QStringLiteral("type"), QStringLiteral("text"));
            textContent.insert(QStringLiteral("text"),
                               QString::fromUtf8(serviceJson));
            QJsonArray content;
            content.append(textContent);

            QJsonObject result;
            result.insert(QStringLiteral("content"), content);
            result.insert(QStringLiteral("structuredContent"), serviceResult);
            result.insert(QStringLiteral("isError"), !ok);

            emit toolCallFinished(name, QString::fromUtf8(serviceJson), elapsed, ok);
            emit toolInvoked(name, argsJson, QString::fromUtf8(serviceJson),
                             elapsed, ok);

            sendRpcResult(socket, id, result);
            return;
        }

        sendRpcError(socket, id, -32601, QStringLiteral("Method not found"));
    }
    catch (const std::exception&)
    {
        sendRpcError(socket, id, -32603, QStringLiteral("Internal error"));
    }
    catch (...)
    {
        sendRpcError(socket, id, -32603, QStringLiteral("Internal error"));
    }
}

void HttpServer::sendRpcResult(QTcpSocket* socket, const QJsonValue& id,
                               const QJsonObject& result,
                               const QByteArray& extraHeaders)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    envelope.insert(QStringLiteral("id"), id);
    envelope.insert(QStringLiteral("result"), result);
    sendHttp(socket, 200, compactJson(envelope), QByteArrayLiteral("application/json"),
             extraHeaders);
}

void HttpServer::sendRpcError(QTcpSocket* socket, const QJsonValue& id, int code,
                              const QString& message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    QJsonObject envelope;
    envelope.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    envelope.insert(QStringLiteral("id"), id);
    envelope.insert(QStringLiteral("error"), error);
    sendHttp(socket, 200, compactJson(envelope));
}

} // namespace pvf::mcp2
