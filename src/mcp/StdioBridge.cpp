#include "mcp/StdioBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pvf {

namespace {

constexpr qint64 kMaxEndpointBytes = 64 * 1024;
constexpr int kDefaultTimeoutMs = 60 * 1000;

QString endpointFileError(const QString& path, const QString& detail)
{
    return QStringLiteral("endpoint file %1: %2").arg(path, detail);
}

bool validateEndpointUrl(const QUrl& url, QString* error)
{
    if (!url.isValid() || url.scheme() != QLatin1String("http"))
    {
        *error = QStringLiteral("MCP endpoint URL must be a valid http:// URL");
        return false;
    }
    const QString host = url.host();
    if (host != QLatin1String("127.0.0.1") && host != QLatin1String("::1"))
    {
        *error = QStringLiteral("MCP endpoint URL must use 127.0.0.1 or [::1]");
        return false;
    }
    if (url.port() <= 0)
    {
        *error = QStringLiteral("MCP endpoint URL must include an explicit port");
        return false;
    }
    if (!url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
    {
        *error = QStringLiteral("MCP endpoint URL must not contain credentials, a query "
                                "or a fragment");
        return false;
    }
    if (url.path() != QLatin1String("/mcp") && url.path() != QLatin1String("/mcp/"))
    {
        *error = QStringLiteral("MCP endpoint URL path must be /mcp");
        return false;
    }
    return true;
}

bool isEventStream(const QByteArray& contentType)
{
    return contentType.trimmed().toLower().startsWith("text/event-stream");
}

} // namespace

QString defaultMcpEndpointPath()
{
#if defined(Q_OS_MACOS)
    return QDir::homePath()
           + QStringLiteral("/Library/Application Support/PVForge/mcp-endpoint.json");
#else
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/PVForge/mcp-endpoint.json");
#endif
}

bool loadMcpEndpointFile(const QString& path, McpEndpoint* endpoint, QString* error)
{
    if (!endpoint || !error)
        return false;

#if defined(Q_OS_UNIX)
    const QByteArray nativePath = QFile::encodeName(path);
    struct stat info;
    if (::stat(nativePath.constData(), &info) != 0)
    {
        *error = endpointFileError(
            path, QStringLiteral("cannot read endpoint file (%1); enable MCP in PVForge "
                                 "or pass --url and --token")
                      .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (!S_ISREG(info.st_mode))
    {
        *error = endpointFileError(path, QStringLiteral("not a regular file"));
        return false;
    }
    if ((info.st_mode & 0777) != 0600)
    {
        *error = endpointFileError(
            path, QStringLiteral("mode must be 0600 (found %1); run: chmod 600 %2")
                      .arg(QString::number(info.st_mode & 0777, 8).rightJustified(4, '0'),
                           path));
        return false;
    }
    if (info.st_uid != ::geteuid())
    {
        *error = endpointFileError(path, QStringLiteral("file is not owned by the current "
                                                        "user"));
        return false;
    }
#else
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        *error = endpointFileError(path, QStringLiteral("file does not exist; enable MCP "
                                                        "in PVForge or pass --url and "
                                                        "--token"));
        return false;
    }
#endif

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = endpointFileError(path, file.errorString());
        return false;
    }
    const QByteArray data = file.read(kMaxEndpointBytes + 1);
    if (data.size() > kMaxEndpointBytes)
    {
        *error = endpointFileError(path, QStringLiteral("file is larger than 64 KiB"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        *error = endpointFileError(
            path, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject())
    {
        *error = endpointFileError(path, QStringLiteral("top-level JSON value must be an "
                                                        "object"));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("format"))
        && root.value(QStringLiteral("format")).toString()
               != QLatin1String("pvforge-mcp-endpoint"))
    {
        *error = endpointFileError(
            path, QStringLiteral("unsupported format %1 (expected "
                                 "pvforge-mcp-endpoint)")
                      .arg(root.value(QStringLiteral("format")).toString()));
        return false;
    }
    if (root.contains(QStringLiteral("version")))
    {
        const QJsonValue version = root.value(QStringLiteral("version"));
        if (!version.isDouble() || version.toDouble() != 1.0)
        {
            *error = endpointFileError(path, QStringLiteral("unsupported version (expected 1)"));
            return false;
        }
    }

    const QJsonValue urlValue = root.value(QStringLiteral("url"));
    if (!urlValue.isString() || urlValue.toString().trimmed().isEmpty())
    {
        *error = endpointFileError(path, QStringLiteral("missing string field \"url\""));
        return false;
    }
    const QJsonValue tokenValue = root.value(QStringLiteral("token"));
    if (!tokenValue.isString() || tokenValue.toString().trimmed().isEmpty())
    {
        *error = endpointFileError(path, QStringLiteral("missing string field \"token\""));
        return false;
    }

    qint64 pid = 0;
    if (root.contains(QStringLiteral("pid")))
    {
        const QJsonValue pidValue = root.value(QStringLiteral("pid"));
        const double pidNumber = pidValue.toDouble();
        if (!pidValue.isDouble() || pidNumber < 0.0 || pidNumber > 2147483647.0
            || std::floor(pidNumber) != pidNumber)
        {
            *error = endpointFileError(path, QStringLiteral("field \"pid\" must be a "
                                                            "non-negative integer"));
            return false;
        }
        pid = qint64(pidNumber);
    }

    const QUrl url(urlValue.toString().trimmed());
    QString urlError;
    if (!validateEndpointUrl(url, &urlError))
    {
        *error = endpointFileError(path, urlError);
        return false;
    }

    endpoint->url = url;
    endpoint->token = tokenValue.toString().trimmed().toUtf8();
    endpoint->pid = pid;
    endpoint->source = QStringLiteral("endpoint file ") + path;
    return true;
}

bool resolveMcpEndpoint(const QString& endpointPath, const QString& argumentUrl,
                        const QByteArray& argumentToken, const QString& environmentUrl,
                        const QByteArray& environmentToken, McpEndpoint* endpoint,
                        QString* error)
{
    if (!endpoint || !error)
        return false;

    const bool argumentUrlSet = !argumentUrl.trimmed().isEmpty();
    const bool argumentTokenSet = !argumentToken.trimmed().isEmpty();
    QString url = argumentUrlSet ? argumentUrl.trimmed() : QString();
    QByteArray token = argumentTokenSet ? argumentToken.trimmed() : QByteArray();
    const bool fromArguments = argumentUrlSet || argumentTokenSet;

    const bool environmentUrlSet =
        url.isEmpty() && !environmentUrl.trimmed().isEmpty();
    const bool environmentTokenSet =
        token.isEmpty() && !environmentToken.trimmed().isEmpty();
    if (environmentUrlSet)
        url = environmentUrl.trimmed();
    if (environmentTokenSet)
        token = environmentToken.trimmed();
    const bool fromEnvironment = environmentUrlSet || environmentTokenSet;

    const QString path = endpointPath.isEmpty() ? defaultMcpEndpointPath() : endpointPath;
    bool fromFile = false;
    qint64 filePid = 0;
    if (url.isEmpty() || token.isEmpty())
    {
        McpEndpoint fileEndpoint;
        QString fileError;
        if (!loadMcpEndpointFile(path, &fileEndpoint, &fileError))
        {
            *error = fileError;
            return false;
        }
        fromFile = true;
        filePid = fileEndpoint.pid;
        if (url.isEmpty())
            url = fileEndpoint.url.toString();
        if (token.isEmpty())
            token = fileEndpoint.token;
    }

    if (url.isEmpty())
    {
        *error = QStringLiteral("no MCP endpoint URL; pass --url, set PVFORGE_MCP_URL, "
                                "or enable MCP in PVForge");
        return false;
    }
    if (token.isEmpty())
    {
        *error = QStringLiteral("no MCP token; pass --token, set PVFORGE_MCP_TOKEN, "
                                "or enable MCP in PVForge");
        return false;
    }

    const QUrl parsed(url);
    QString urlError;
    if (!validateEndpointUrl(parsed, &urlError))
    {
        *error = urlError;
        return false;
    }

    QStringList sources;
    if (fromArguments)
        sources.append(QStringLiteral("command line"));
    if (fromEnvironment)
        sources.append(QStringLiteral("environment"));
    if (fromFile)
        sources.append(QStringLiteral("endpoint file %1").arg(path));

    endpoint->url = parsed;
    endpoint->token = token;
    endpoint->pid = filePid;
    endpoint->source = sources.join(QStringLiteral(", "));
    return true;
}

struct McpStdioBridge::Transfer
{
    QNetworkReply* reply = nullptr;
    QTimer* timer = nullptr;
    QJsonValue id;
    QByteArray idKey;
    bool notification = false;
    bool finished = false;
    bool cancelled = false;
    bool timedOut = false;
    bool sse = false;
    QByteArray buffer;
    QByteArray data;
};

McpStdioBridge::McpStdioBridge(const McpEndpoint& endpoint, QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_endpoint(endpoint)
    , m_timeoutMs(kDefaultTimeoutMs)
{
    // The endpoint is always loopback; never route it through a system or
    // environment proxy.
    m_manager->setProxy(QNetworkProxy::NoProxy);
}

McpStdioBridge::~McpStdioBridge()
{
    abortAll();
}

void McpStdioBridge::setTimeoutMs(int milliseconds)
{
    m_timeoutMs = std::clamp(milliseconds, 1, 24 * 60 * 60 * 1000);
}

int McpStdioBridge::inFlight() const
{
    return int(m_transfers.size());
}

QByteArray McpStdioBridge::idKey(const QJsonValue& id)
{
    if (id.isString())
        return QByteArrayLiteral("s:") + id.toString().toUtf8();
    if (id.isDouble())
        return QByteArrayLiteral("n:") + QByteArray::number(id.toDouble());
    if (id.isBool())
        return id.toBool() ? QByteArrayLiteral("b:1") : QByteArrayLiteral("b:0");
    if (id.isNull())
        return QByteArrayLiteral("null");
    return QByteArrayLiteral("?");
}

bool McpStdioBridge::submitLine(const QByteArray& line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return true;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        sendError(QJsonValue::Null, -32700,
                  QStringLiteral("Parse error: stdin line is not a JSON-RPC object"));
        return false;
    }

    const QJsonObject object = document.object();
    const bool notification = !object.contains(QStringLiteral("id"));
    const QJsonValue id = object.value(QStringLiteral("id"));

    if (notification
        && object.value(QStringLiteral("method")).toString()
               == QLatin1String("notifications/cancelled"))
    {
        const QJsonObject params = object.value(QStringLiteral("params")).toObject();
        const QJsonValue requestId = params.value(QStringLiteral("requestId"));
        if (!requestId.isUndefined())
            cancelById(idKey(requestId));
    }

    QNetworkRequest request(m_endpoint.url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/json, text/event-stream"));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + m_endpoint.token);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = m_manager->post(request, trimmed);
    reply->setParent(this);

    auto* transfer = new Transfer;
    transfer->reply = reply;
    transfer->id = id;
    transfer->notification = notification;
    if (!notification)
        transfer->idKey = idKey(id);

    m_transfers.insert(reply, transfer);
    if (!notification)
        m_byId.insert(transfer->idKey, reply);

    QObject::connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        Transfer* current = m_transfers.value(reply);
        if (!current || current->finished)
        {
            if (reply->isOpen())
                reply->readAll();
            return;
        }
        if (!current->sse
            && isEventStream(reply->header(QNetworkRequest::ContentTypeHeader)
                                 .toByteArray()))
            current->sse = true;
        const QByteArray chunk = reply->readAll();
        current->buffer.append(chunk);
        if (current->sse)
            processEventStream(reply, current);
    });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply] {
        Transfer* current = m_transfers.value(reply);
        if (!current)
            return;
        if (current->timer)
            current->timer->stop();

        if (!current->finished)
        {
            if (!current->sse
                && isEventStream(reply->header(QNetworkRequest::ContentTypeHeader)
                                     .toByteArray()))
                current->sse = true;
            if (reply->isOpen())
                current->buffer.append(reply->readAll());
            if (current->sse)
                flushEventStream(reply, current);
        }

        if (!current->finished)
        {
            if (current->cancelled)
            {
                // The client cancelled this request; do not answer it.
            }
            else if (current->timedOut)
            {
                const QString message =
                    QStringLiteral("MCP request timed out after %1 ms").arg(m_timeoutMs);
                if (current->notification)
                    emit diagnostic(QStringLiteral("MCP notification failed: %1").arg(message));
                else
                    sendError(current->id, -32000, message);
            }
            else
            {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status < 200 || status >= 300)
                {
                    QString message;
                    if (status <= 0)
                        message = QStringLiteral("MCP endpoint connection failed: %1")
                                      .arg(reply->errorString());
                    else if (status == 401)
                        message = QStringLiteral(
                            "MCP authentication failed (HTTP 401); check the PVForge token");
                    else if (status == 403)
                        message = QStringLiteral("MCP endpoint rejected the request "
                                                 "(HTTP 403)");
                    else if (status == 404)
                        message = QStringLiteral(
                            "MCP endpoint was not found (HTTP 404); check the URL");
                    else if (status == 405)
                        message = QStringLiteral(
                            "MCP endpoint does not accept POST (HTTP 405)");
                    else if (status == 413)
                        message = QStringLiteral("MCP request was too large (HTTP 413)");
                    else
                        message = QStringLiteral("MCP endpoint returned HTTP %1").arg(status);

                    if (current->notification)
                        emit diagnostic(
                            QStringLiteral("MCP notification failed: %1").arg(message));
                    else
                        sendError(current->id, -32000, message);
                }
                else if (current->notification)
                {
                    // 202/200 is the normal acknowledgement for a notification.
                }
                else if (current->sse)
                {
                    sendError(current->id, -32000,
                              QStringLiteral("MCP event stream ended without a response"));
                }
                else if (current->buffer.trimmed().isEmpty())
                {
                    sendError(current->id, -32000,
                              QStringLiteral("MCP endpoint returned an empty response"));
                }
                else
                {
                    QJsonParseError responseError;
                    const QJsonDocument response =
                        QJsonDocument::fromJson(current->buffer, &responseError);
                    if (responseError.error != QJsonParseError::NoError
                        || !response.isObject())
                        sendError(current->id, -32000,
                                  QStringLiteral("MCP endpoint returned invalid JSON"));
                    else
                        emit messageReady(
                            QJsonDocument(response.object()).toJson(QJsonDocument::Compact));
                }
            }
        }

        finishTransfer(reply);
    });

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    transfer->timer = timer;
    QObject::connect(timer, &QTimer::timeout, this, [this, reply] {
        Transfer* current = m_transfers.value(reply);
        if (!current || current->finished)
            return;
        current->timedOut = true;
        reply->abort();
    });
    timer->start(m_timeoutMs);

    return true;
}

void McpStdioBridge::processEventStream(QNetworkReply* reply, Transfer* transfer)
{
    while (!transfer->finished)
    {
        const int newline = transfer->buffer.indexOf('\n');
        if (newline < 0)
            break;

        QByteArray line = transfer->buffer.left(newline);
        transfer->buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.isEmpty())
        {
            dispatchEventData(reply, transfer);
            continue;
        }
        if (!line.startsWith(QByteArrayLiteral("data:")))
            continue;

        QByteArray value = line.mid(5);
        if (value.startsWith(' '))
            value.remove(0, 1);
        if (!transfer->data.isEmpty())
            transfer->data.append('\n');
        transfer->data.append(value);
    }
}

void McpStdioBridge::flushEventStream(QNetworkReply* reply, Transfer* transfer)
{
    if (!transfer->buffer.isEmpty())
    {
        QByteArray line = transfer->buffer;
        transfer->buffer.clear();
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.startsWith(QByteArrayLiteral("data:")))
        {
            QByteArray value = line.mid(5);
            if (value.startsWith(' '))
                value.remove(0, 1);
            if (!transfer->data.isEmpty())
                transfer->data.append('\n');
            transfer->data.append(value);
        }
    }
    if (!transfer->finished)
        dispatchEventData(reply, transfer);
}

void McpStdioBridge::dispatchEventData(QNetworkReply* reply, Transfer* transfer)
{
    if (transfer->finished || transfer->data.isEmpty())
        return;

    const QByteArray payload = transfer->data;
    transfer->data.clear();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        emit diagnostic(QStringLiteral("ignored malformed SSE data from MCP endpoint"));
        return;
    }

    const QJsonObject object = document.object();
    emit messageReady(QJsonDocument(object).toJson(QJsonDocument::Compact));

    if (transfer->notification || object.contains(QStringLiteral("method")))
        return;
    if (!object.contains(QStringLiteral("id")))
        return;
    if (idKey(object.value(QStringLiteral("id"))) != transfer->idKey)
        return;

    // The response is complete. Stop reading the stream even if the server
    // keeps the connection open. Abort from the event loop so a synchronous
    // finished() cannot delete the transfer while its parser is on the stack.
    transfer->finished = true;
    if (transfer->timer)
        transfer->timer->stop();
    if (!reply->isFinished())
        QTimer::singleShot(0, reply, [reply] {
            if (!reply->isFinished())
                reply->abort();
        });
}

void McpStdioBridge::finishTransfer(QNetworkReply* reply)
{
    Transfer* transfer = m_transfers.take(reply);
    if (!transfer)
        return;

    if (transfer->timer)
        transfer->timer->stop();
    if (!transfer->idKey.isEmpty())
    {
        const auto it = m_byId.find(transfer->idKey);
        if (it != m_byId.end() && it.value() == reply)
            m_byId.erase(it);
    }

    reply->deleteLater();
    delete transfer;

    if (m_transfers.isEmpty())
        emit idle();
}

void McpStdioBridge::cancelById(const QByteArray& key)
{
    const auto it = m_byId.constFind(key);
    if (it == m_byId.constEnd())
        return;

    QNetworkReply* reply = it.value();
    Transfer* transfer = m_transfers.value(reply);
    if (!transfer || transfer->finished)
        return;

    transfer->cancelled = true;
    if (transfer->timer)
        transfer->timer->stop();
    reply->abort();
}

void McpStdioBridge::sendError(const QJsonValue& id, int code, const QString& message)
{
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;

    QJsonObject envelope;
    envelope[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    envelope[QStringLiteral("id")] = id;
    envelope[QStringLiteral("error")] = error;
    emit messageReady(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

void McpStdioBridge::abortAll()
{
    const QList<QNetworkReply*> replies = m_transfers.keys();
    for (QNetworkReply* reply : replies)
    {
        Transfer* transfer = m_transfers.take(reply);
        if (!transfer)
            continue;
        if (transfer->timer)
            transfer->timer->stop();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        delete transfer;
    }
    m_byId.clear();
}

} // namespace pvf
