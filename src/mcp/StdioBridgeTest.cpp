#include "mcp/StdioBridgeTest.h"

#include "mcp/StdioBridge.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVector>

#include <functional>
#include <memory>

namespace {

bool writeEndpointFile(const QString& path, const QByteArray& data, bool ownerOnly)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = file.write(data);
    file.close();
    if (written != data.size())
        return false;

    QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (!ownerOnly)
        permissions |= QFileDevice::ReadGroup | QFileDevice::ReadOther;
    return QFile::setPermissions(path, permissions);
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return predicate();
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
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    default:
        return QByteArrayLiteral("Error");
    }
}

// Minimal fake Streamable HTTP MCP server used by the bridge self-test.
class FakeHttpMcpServer : public QTcpServer
{
public:
    explicit FakeHttpMcpServer(QObject* parent = nullptr)
        : QTcpServer(parent)
    {
    }

    QByteArray token = QByteArrayLiteral("test-token");
    int notificationCount = 0;
    int requestCount = 0;
    int unauthorizedCount = 0;
    bool keepSseOpen = false;
    QByteArray lastAccept;
    QByteArray lastContentType;
    QByteArray lastAuthorization;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor))
        {
            socket->deleteLater();
            return;
        }

        struct State
        {
            QByteArray buffer;
            bool handled = false;
        };
        auto state = std::make_shared<State>();

        QObject::connect(socket, &QTcpSocket::readyRead, this,
                         [this, socket, state] {
                             if (state->handled)
                             {
                                 socket->readAll();
                                 return;
                             }
                             state->buffer.append(socket->readAll());
                             const int headerEnd = state->buffer.indexOf("\r\n\r\n");
                             if (headerEnd < 0)
                                 return;

                             QHash<QByteArray, QByteArray> headers;
                             const QByteArray head = state->buffer.left(headerEnd);
                             for (QByteArray line : head.split('\n'))
                             {
                                 if (line.endsWith('\r'))
                                     line.chop(1);
                                 const int colon = line.indexOf(':');
                                 if (colon <= 0)
                                     continue;
                                 headers.insert(line.left(colon).trimmed().toLower(),
                                                line.mid(colon + 1).trimmed());
                             }

                             const int contentLength =
                                 headers.value(QByteArrayLiteral("content-length")).toInt();
                             if (state->buffer.size()
                                 < headerEnd + 4 + contentLength)
                                 return;

                             state->handled = true;
                             const QByteArray body =
                                 state->buffer.mid(headerEnd + 4, contentLength);
                             state->buffer.clear();
                             handle(socket, headers, body);
                         });
        QObject::connect(socket, &QTcpSocket::disconnected, socket,
                         &QObject::deleteLater);
    }

private:
    void sendResponse(QTcpSocket* socket, int status,
                      const QByteArray& contentType, const QByteArray& body,
                      const QByteArray& extraHeaders = QByteArray())
    {
        QByteArray head = QByteArrayLiteral("HTTP/1.1 ")
                          + QByteArray::number(status) + ' ' + reasonPhrase(status)
                          + QByteArrayLiteral("\r\n");
        if (!contentType.isEmpty())
            head += QByteArrayLiteral("Content-Type: ") + contentType
                    + QByteArrayLiteral("\r\n");
        head += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
                + QByteArrayLiteral("\r\n");
        head += QByteArrayLiteral("Connection: close\r\n");
        head += extraHeaders;
        head += QByteArrayLiteral("\r\n");
        socket->write(head);
        socket->write(body);
        if (!(keepSseOpen && contentType == QByteArrayLiteral("text/event-stream")))
            socket->disconnectFromHost();
    }

    static QByteArray sseResponse(const QJsonObject& response)
    {
        return QByteArrayLiteral("event: message\ndata: ")
               + QJsonDocument(response).toJson(QJsonDocument::Compact)
               + QByteArrayLiteral("\n\n");
    }

    static QJsonObject envelope(const QJsonValue& id, const QJsonObject& result)
    {
        QJsonObject response;
        response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        response[QStringLiteral("id")] = id;
        response[QStringLiteral("result")] = result;
        return response;
    }

    void handle(QTcpSocket* socket, const QHash<QByteArray, QByteArray>& headers,
                const QByteArray& body)
    {
        lastAccept = headers.value(QByteArrayLiteral("accept"));
        lastContentType = headers.value(QByteArrayLiteral("content-type"));
        lastAuthorization = headers.value(QByteArrayLiteral("authorization"));

        if (lastAuthorization != QByteArrayLiteral("Bearer ") + token)
        {
            ++unauthorizedCount;
            QJsonObject payload;
            payload[QStringLiteral("error")] = QStringLiteral("unauthorized");
            payload[QStringLiteral("echo")] = QString::fromUtf8(lastAuthorization);
            sendResponse(socket, 401, QByteArrayLiteral("application/json"),
                         QJsonDocument(payload).toJson(QJsonDocument::Compact),
                         QByteArrayLiteral("WWW-Authenticate: Bearer\r\n"));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            sendResponse(socket, 400, QByteArrayLiteral("application/json"),
                         QByteArrayLiteral("{\"error\":\"bad request\"}"));
            return;
        }

        const QJsonObject request = document.object();
        const QJsonValue id = request.value(QStringLiteral("id"));
        const QString method = request.value(QStringLiteral("method")).toString();

        if (!request.contains(QStringLiteral("id")))
        {
            ++notificationCount;
            sendResponse(socket, 202, QByteArray(), QByteArray());
            return;
        }

        ++requestCount;

        if (method == QLatin1String("initialize"))
        {
            QJsonObject result;
            result[QStringLiteral("protocolVersion")] = QStringLiteral("2025-11-25");
            result[QStringLiteral("capabilities")] =
                QJsonObject{{QStringLiteral("tools"), QJsonObject()}};
            result[QStringLiteral("serverInfo")] =
                QJsonObject{{QStringLiteral("name"), QStringLiteral("FakePVForge")},
                            {QStringLiteral("version"), QStringLiteral("test")}};
            sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                         QJsonDocument(envelope(id, result))
                             .toJson(QJsonDocument::Compact));
            return;
        }

        if (method == QLatin1String("tools/list"))
        {
            QJsonArray tools;
            tools.append(QJsonObject{
                {QStringLiteral("name"), QStringLiteral("project.summary")},
                {QStringLiteral("description"), QStringLiteral("summary")},
                {QStringLiteral("inputSchema"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}});
            QJsonObject result;
            result[QStringLiteral("tools")] = tools;
            sendResponse(socket, 200, QByteArrayLiteral("text/event-stream"),
                         sseResponse(envelope(id, result)));
            return;
        }

        if (method == QLatin1String("tools/call"))
        {
            QJsonObject payload;
            payload[QStringLiteral("error")] = QStringLiteral("internal");
            payload[QStringLiteral("echo")] = QString::fromUtf8(lastAuthorization);
            sendResponse(socket, 500, QByteArrayLiteral("application/json"),
                         QJsonDocument(payload).toJson(QJsonDocument::Compact));
            return;
        }

        if (method == QLatin1String("blocked"))
        {
            sendResponse(socket, 405, QByteArrayLiteral("application/json"),
                         QByteArrayLiteral("{\"error\":\"method not allowed\"}"),
                         QByteArrayLiteral("Allow: POST\r\n"));
            return;
        }

        if (method == QLatin1String("slow"))
        {
            // Leave the connection open until the bridge times out or
            // cancels it.
            return;
        }

        if (method == QLatin1String("ping"))
        {
            sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                         QJsonDocument(envelope(id, QJsonObject()))
                             .toJson(QJsonDocument::Compact));
            return;
        }

        QJsonObject result;
        result[QStringLiteral("method")] = method;
        sendResponse(socket, 200, QByteArrayLiteral("application/json"),
                     QJsonDocument(envelope(id, result)).toJson(QJsonDocument::Compact));
    }
};

QProcessEnvironment cleanProcessEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("PVFORGE_MCP_ENDPOINT"));
    environment.remove(QStringLiteral("PVFORGE_MCP_URL"));
    environment.remove(QStringLiteral("PVFORGE_MCP_TOKEN"));
    return environment;
}

} // namespace

namespace pvf {

int mcpBridgeTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-bridge-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    QTemporaryDir tempDir;
    failIf(!tempDir.isValid(), QStringLiteral("cannot create a temporary directory"));
    const QString endpointPath = tempDir.filePath(QStringLiteral("mcp-endpoint.json"));

    QJsonObject fileObject;
    fileObject[QStringLiteral("format")] = QStringLiteral("pvforge-mcp-endpoint");
    fileObject[QStringLiteral("version")] = 1;
    fileObject[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:39321/mcp");
    fileObject[QStringLiteral("token")] = QStringLiteral("file-token");
    fileObject[QStringLiteral("pid")] = 123;
    const QByteArray fileJson =
        QJsonDocument(fileObject).toJson(QJsonDocument::Compact);

    failIf(!writeEndpointFile(endpointPath, fileJson, true),
           QStringLiteral("cannot write the endpoint file with mode 0600"));

    McpEndpoint fileEndpoint;
    QString error;
    failIf(!loadMcpEndpointFile(endpointPath, &fileEndpoint, &error),
           QStringLiteral("valid endpoint file rejected: %1").arg(error));
    failIf(fileEndpoint.url.toString() != QStringLiteral("http://127.0.0.1:39321/mcp"),
           QStringLiteral("endpoint URL parsed as %1").arg(fileEndpoint.url.toString()));
    failIf(fileEndpoint.token != QByteArrayLiteral("file-token"),
           QStringLiteral("endpoint token was not parsed"));
    failIf(fileEndpoint.pid != 123, QStringLiteral("endpoint pid was not parsed"));

    failIf(!writeEndpointFile(endpointPath, fileJson, false),
           QStringLiteral("cannot change the endpoint file to mode 0644"));
    McpEndpoint insecureEndpoint;
    error.clear();
    failIf(loadMcpEndpointFile(endpointPath, &insecureEndpoint, &error),
           QStringLiteral("endpoint file with mode 0644 was accepted"));
    failIf(!error.contains(QStringLiteral("0600")),
           QStringLiteral("mode error does not mention 0600: %1").arg(error));
    failIf(error.contains(QStringLiteral("file-token")),
           QStringLiteral("mode error leaked the endpoint token"));
    failIf(!writeEndpointFile(endpointPath, fileJson, true),
           QStringLiteral("cannot restore endpoint file mode 0600"));

    error.clear();
    failIf(loadMcpEndpointFile(tempDir.filePath(QStringLiteral("missing.json")),
                               &insecureEndpoint, &error),
           QStringLiteral("missing endpoint file was accepted"));
    failIf(!error.contains(QStringLiteral("endpoint file")),
           QStringLiteral("missing endpoint error is not explicit: %1").arg(error));

    const QString invalidPath = tempDir.filePath(QStringLiteral("invalid.json"));
    failIf(!writeEndpointFile(invalidPath, QByteArrayLiteral("{not json"), true),
           QStringLiteral("cannot write invalid JSON endpoint file"));
    error.clear();
    failIf(loadMcpEndpointFile(invalidPath, &insecureEndpoint, &error),
           QStringLiteral("invalid JSON endpoint file was accepted"));
    failIf(!error.contains(QStringLiteral("invalid JSON")),
           QStringLiteral("invalid JSON error is not explicit: %1").arg(error));

    QJsonObject wrongFormat = fileObject;
    wrongFormat[QStringLiteral("format")] = QStringLiteral("something-else");
    const QString wrongFormatPath = tempDir.filePath(QStringLiteral("format.json"));
    failIf(!writeEndpointFile(
               wrongFormatPath,
               QJsonDocument(wrongFormat).toJson(QJsonDocument::Compact), true),
           QStringLiteral("cannot write a wrong-format endpoint file"));
    error.clear();
    failIf(loadMcpEndpointFile(wrongFormatPath, &insecureEndpoint, &error),
           QStringLiteral("endpoint file with an unknown format was accepted"));
    failIf(!error.contains(QStringLiteral("format")),
           QStringLiteral("format error is not explicit: %1").arg(error));

    QJsonObject remoteObject = fileObject;
    remoteObject[QStringLiteral("url")] =
        QStringLiteral("http://192.168.1.9:39321/mcp");
    const QString remotePath = tempDir.filePath(QStringLiteral("remote.json"));
    failIf(!writeEndpointFile(
               remotePath, QJsonDocument(remoteObject).toJson(QJsonDocument::Compact),
               true),
           QStringLiteral("cannot write a non-loopback endpoint file"));
    error.clear();
    failIf(loadMcpEndpointFile(remotePath, &insecureEndpoint, &error),
           QStringLiteral("non-loopback endpoint URL was accepted"));
    failIf(!error.contains(QStringLiteral("127.0.0.1")),
           QStringLiteral("loopback error is not explicit: %1").arg(error));

    // Precedence: command line > environment > endpoint file, per field.
    McpEndpoint resolved;
    QString resolveError;
    failIf(!resolveMcpEndpoint(endpointPath,
                               QStringLiteral("http://127.0.0.1:1111/mcp"),
                               QByteArrayLiteral("arg-token"),
                               QStringLiteral("http://127.0.0.1:2222/mcp"),
                               QByteArrayLiteral("env-token"), &resolved,
                               &resolveError),
           QStringLiteral("argument precedence failed: %1").arg(resolveError));
    failIf(resolved.url.toString() != QStringLiteral("http://127.0.0.1:1111/mcp"),
           QStringLiteral("argument URL did not win: %1").arg(resolved.url.toString()));
    failIf(resolved.token != QByteArrayLiteral("arg-token"),
           QStringLiteral("argument token did not win"));

    resolved = McpEndpoint();
    resolveError.clear();
    failIf(!resolveMcpEndpoint(endpointPath, QString(),
                               QByteArrayLiteral(""),
                               QStringLiteral("http://127.0.0.1:2222/mcp"),
                               QByteArrayLiteral("env-token"), &resolved,
                               &resolveError),
           QStringLiteral("environment precedence failed: %1").arg(resolveError));
    failIf(resolved.url.toString() != QStringLiteral("http://127.0.0.1:2222/mcp"),
           QStringLiteral("environment URL did not win: %1").arg(resolved.url.toString()));
    failIf(resolved.token != QByteArrayLiteral("env-token"),
           QStringLiteral("environment token did not win"));

    resolved = McpEndpoint();
    resolveError.clear();
    failIf(!resolveMcpEndpoint(endpointPath, QString(), QByteArrayLiteral(""), QString(),
                               QByteArrayLiteral(""), &resolved, &resolveError),
           QStringLiteral("endpoint file fallback failed: %1").arg(resolveError));
    failIf(resolved.url.toString() != QStringLiteral("http://127.0.0.1:39321/mcp"),
           QStringLiteral("endpoint file URL was not used: %1")
               .arg(resolved.url.toString()));
    failIf(resolved.token != QByteArrayLiteral("file-token"),
           QStringLiteral("endpoint file token was not used"));

    resolved = McpEndpoint();
    resolveError.clear();
    failIf(!resolveMcpEndpoint(endpointPath,
                               QStringLiteral("http://127.0.0.1:3333/mcp"),
                               QByteArrayLiteral(""), QString(),
                               QByteArrayLiteral(""), &resolved, &resolveError),
           QStringLiteral("per-field fallback failed: %1").arg(resolveError));
    failIf(resolved.url.toString() != QStringLiteral("http://127.0.0.1:3333/mcp"),
           QStringLiteral("per-field URL was not used"));
    failIf(resolved.token != QByteArrayLiteral("file-token"),
           QStringLiteral("per-field token did not fall back to the endpoint file"));

    resolveError.clear();
    failIf(resolveMcpEndpoint(tempDir.filePath(QStringLiteral("missing.json")),
                              QStringLiteral("http://127.0.0.1:3333/mcp"),
                              QByteArrayLiteral(""), QString(),
                              QByteArrayLiteral(""), &resolved, &resolveError),
           QStringLiteral("URL without any token was accepted"));
    failIf(!resolveError.contains(QStringLiteral("endpoint file")),
           QStringLiteral("missing token error is not explicit: %1").arg(resolveError));

    failIf(resolveMcpEndpoint(endpointPath,
                              QStringLiteral("https://127.0.0.1:3333/mcp"),
                              QByteArrayLiteral("token"), QString(),
                              QByteArrayLiteral(""), &resolved, &resolveError),
           QStringLiteral("https endpoint URL was accepted"));
    failIf(resolveMcpEndpoint(endpointPath,
                              QStringLiteral("http://127.0.0.1:3333/not-mcp"),
                              QByteArrayLiteral("token"), QString(),
                              QByteArrayLiteral(""), &resolved, &resolveError),
           QStringLiteral("non-/mcp endpoint path was accepted"));

    // A connection failure must become a JSON-RPC error, not a crash.
    quint16 unusedPort = 0;
    {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0))
            unusedPort = probe.serverPort();
        probe.close();
    }
    if (unusedPort != 0)
    {
        McpEndpoint deadEndpoint;
        deadEndpoint.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                                    .arg(unusedPort));
        deadEndpoint.token = QByteArrayLiteral("dead-token");
        McpStdioBridge deadBridge(deadEndpoint);
        deadBridge.setTimeoutMs(3000);
        QVector<QByteArray> deadMessages;
        QObject::connect(&deadBridge, &McpStdioBridge::messageReady,
                         [&deadMessages](const QByteArray& message) {
                             deadMessages.append(message);
                         });
        failIf(!deadBridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":99,"method":"initialize","params":{}})")),
               QStringLiteral("connection-failure request was rejected locally"));
        failIf(!waitFor([&deadMessages] { return deadMessages.size() >= 1; }, 3000),
               QStringLiteral("connection failure produced no JSON-RPC error"));
        const QByteArray deadResponse = deadMessages.value(0);
        const QJsonObject deadObject =
            QJsonDocument::fromJson(deadResponse).object();
        failIf(deadObject.value(QStringLiteral("error"))
                       .toObject()
                       .value(QStringLiteral("code"))
                       .toInt()
                   != -32000,
               QStringLiteral("connection failure error code is not -32000"));
        failIf(!deadObject.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("connection failed")),
               QStringLiteral("connection failure error is not explicit"));
        failIf(QString::fromUtf8(deadResponse).contains(QStringLiteral("dead-token")),
               QStringLiteral("connection failure error leaked the token"));
        failIf(!waitFor([&deadBridge] { return deadBridge.inFlight() == 0; }, 1000),
               QStringLiteral("connection failure request is still in flight"));
    }

    failIf(!defaultMcpEndpointPath().endsWith(
               QStringLiteral("/PVForge/mcp-endpoint.json")),
           QStringLiteral("unexpected default endpoint path: %1")
               .arg(defaultMcpEndpointPath()));

    FakeHttpMcpServer server;
    failIf(!server.listen(QHostAddress::LocalHost, 0),
           QStringLiteral("fake MCP server failed to listen: %1")
               .arg(server.errorString()));

    McpEndpoint liveEndpoint;
    liveEndpoint.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                                .arg(server.serverPort()));
    liveEndpoint.token = QByteArrayLiteral("test-token");
    liveEndpoint.source = QStringLiteral("test");

    const QByteArray initializeRequest = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"mcp-bridge-test","version":"1"}}})");
    const QByteArray toolsListRequest = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
    const QByteArray initializedNotification = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    const QByteArray cancelledNotification = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":7,"reason":"test"}})");

    {
        McpStdioBridge bridge(liveEndpoint);
        bridge.setTimeoutMs(5000);

        QVector<QByteArray> messages;
        QStringList diagnostics;
        QObject::connect(&bridge, &McpStdioBridge::messageReady,
                         [&messages](const QByteArray& message) {
                             messages.append(message);
                         });
        QObject::connect(&bridge, &McpStdioBridge::diagnostic,
                         [&diagnostics](const QString& message) {
                             diagnostics.append(message);
                         });

        failIf(!bridge.submitLine(initializeRequest),
               QStringLiteral("initialize request was rejected locally"));
        failIf(!waitFor([&messages] { return messages.size() >= 1; }),
               QStringLiteral("no initialize response from the fake server"));
        const auto objectAt = [&messages](int index) {
            return QJsonDocument::fromJson(messages.value(index)).object();
        };
        const QJsonObject initializeResponse = objectAt(0);
        failIf(initializeResponse.value(QStringLiteral("jsonrpc")).toString()
                   != QStringLiteral("2.0"),
               QStringLiteral("initialize response is not JSON-RPC 2.0"));
        failIf(initializeResponse.value(QStringLiteral("id")).toInt() != 1,
               QStringLiteral("initialize response id was rewritten"));
        failIf(initializeResponse.value(QStringLiteral("result"))
                       .toObject()
                       .value(QStringLiteral("serverInfo"))
                       .toObject()
                       .value(QStringLiteral("name"))
                       .toString()
                   != QStringLiteral("FakePVForge"),
               QStringLiteral("initialize response lost serverInfo"));
        failIf(server.lastContentType != QByteArrayLiteral("application/json"),
               QStringLiteral("POST Content-Type was %1")
                   .arg(QString::fromUtf8(server.lastContentType)));
        failIf(server.lastAccept
                   != QByteArrayLiteral("application/json, text/event-stream"),
               QStringLiteral("POST Accept was %1")
                   .arg(QString::fromUtf8(server.lastAccept)));
        failIf(server.lastAuthorization != QByteArrayLiteral("Bearer test-token"),
               QStringLiteral("POST Authorization header was wrong"));

        server.keepSseOpen = true;
        failIf(!bridge.submitLine(toolsListRequest),
               QStringLiteral("tools/list request was rejected locally"));
        failIf(!waitFor([&messages] { return messages.size() >= 2; }),
               QStringLiteral("no SSE tools/list response from the fake server"));
        failIf(!waitFor([&bridge] { return bridge.inFlight() == 0; }, 2000),
               QStringLiteral("bridge kept an open SSE connection after the response"));
        server.keepSseOpen = false;
        const QJsonObject toolsListResponse = objectAt(1);
        failIf(toolsListResponse.value(QStringLiteral("id")).toInt() != 2,
               QStringLiteral("SSE response id was rewritten"));
        failIf(toolsListResponse.value(QStringLiteral("result"))
                       .toObject()
                       .value(QStringLiteral("tools"))
                       .toArray()
                       .isEmpty(),
               QStringLiteral("SSE response lost the tools array"));

        const int messagesAfterList = messages.size();
        const int notificationsBefore = server.notificationCount;
        failIf(!bridge.submitLine(initializedNotification),
               QStringLiteral("initialized notification was rejected locally"));
        failIf(!waitFor([&server, notificationsBefore] {
                   return server.notificationCount > notificationsBefore;
               }),
               QStringLiteral("initialized notification was not forwarded"));
        for (int i = 0; i < 5; ++i)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        failIf(messages.size() != messagesAfterList,
               QStringLiteral("a notification produced stdout output"));

        const int messagesBeforeParseError = messages.size();
        failIf(bridge.submitLine(QByteArrayLiteral("{not json")),
               QStringLiteral("malformed stdin line was accepted"));
        failIf(messages.size() != messagesBeforeParseError + 1,
               QStringLiteral("malformed stdin line did not produce one error"));
        const QJsonObject parseErrorResponse = objectAt(messagesBeforeParseError);
        failIf(parseErrorResponse.value(QStringLiteral("error"))
                       .toObject()
                       .value(QStringLiteral("code"))
                       .toInt()
                   != -32700,
               QStringLiteral("malformed stdin line did not return -32700"));
        failIf(!parseErrorResponse.value(QStringLiteral("id")).isNull(),
               QStringLiteral("parse error id is not null"));

        const int messagesBeforeCancel = messages.size();
        failIf(!bridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":7,"method":"slow"})")),
               QStringLiteral("slow request was rejected locally"));
        failIf(!waitFor([&bridge] { return bridge.inFlight() == 1; }),
               QStringLiteral("slow request did not enter flight"));
        const int notificationsBeforeCancel = server.notificationCount;
        failIf(!bridge.submitLine(cancelledNotification),
               QStringLiteral("cancelled notification was rejected locally"));
        failIf(!waitFor([&bridge, &server, notificationsBeforeCancel] {
                   return bridge.inFlight() == 0
                          && server.notificationCount > notificationsBeforeCancel;
               }),
               QStringLiteral("cancelled notification did not abort the request"));
        for (int i = 0; i < 5; ++i)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        failIf(messages.size() != messagesBeforeCancel,
               QStringLiteral("a cancelled request produced stdout output"));

        bridge.setTimeoutMs(150);
        const int messagesBeforeTimeout = messages.size();
        failIf(!bridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":4,"method":"slow"})")),
               QStringLiteral("timeout request was rejected locally"));
        failIf(!waitFor([&messages, messagesBeforeTimeout] {
                   return messages.size() > messagesBeforeTimeout;
               }),
               QStringLiteral("a timed out request produced no error"));
        const QJsonObject timeoutResponse = objectAt(messagesBeforeTimeout);
        failIf(timeoutResponse.value(QStringLiteral("id")).toInt() != 4,
               QStringLiteral("timeout error lost the request id"));
        const QJsonObject timeoutError =
            timeoutResponse.value(QStringLiteral("error")).toObject();
        failIf(timeoutError.value(QStringLiteral("code")).toInt() != -32000,
               QStringLiteral("timeout error code is not -32000"));
        failIf(!timeoutError.value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("timed out")),
               QStringLiteral("timeout error does not mention the timeout"));
        failIf(!waitFor([&bridge] { return bridge.inFlight() == 0; }, 1000),
               QStringLiteral("timed out request is still in flight"));
        bridge.setTimeoutMs(5000);

        const int messagesBeforeServerError = messages.size();
        failIf(!bridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"x"}})")),
               QStringLiteral("tools/call request was rejected locally"));
        failIf(!waitFor([&messages, messagesBeforeServerError] {
                   return messages.size() > messagesBeforeServerError;
               }),
               QStringLiteral("HTTP 500 produced no JSON-RPC error"));
        const QJsonObject serverErrorResponse = objectAt(messagesBeforeServerError);
        failIf(serverErrorResponse.value(QStringLiteral("id")).toInt() != 3,
               QStringLiteral("HTTP 500 error lost the request id"));
        failIf(serverErrorResponse.value(QStringLiteral("error"))
                       .toObject()
                       .value(QStringLiteral("message"))
                       .toString()
                       .contains(QStringLiteral("test-token")),
               QStringLiteral("HTTP 500 error leaked the token"));

        const int messagesBeforeMethodNotAllowed = messages.size();
        failIf(!bridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":5,"method":"blocked"})")),
               QStringLiteral("405 request was rejected locally"));
        failIf(!waitFor([&messages, messagesBeforeMethodNotAllowed] {
                   return messages.size() > messagesBeforeMethodNotAllowed;
               }),
               QStringLiteral("HTTP 405 produced no JSON-RPC error"));
        const QJsonObject methodNotAllowedResponse =
            objectAt(messagesBeforeMethodNotAllowed);
        failIf(!methodNotAllowedResponse.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("405")),
               QStringLiteral("HTTP 405 error is not explicit"));

        failIf(!diagnostics.isEmpty(), QStringLiteral("unexpected diagnostics: %1")
                                          .arg(diagnostics.join(QStringLiteral(" | "))));
    }

    {
        McpEndpoint wrongEndpoint = liveEndpoint;
        wrongEndpoint.token = QByteArrayLiteral("wrong-token");
        McpStdioBridge wrongBridge(wrongEndpoint);
        wrongBridge.setTimeoutMs(5000);

        QVector<QByteArray> wrongMessages;
        QObject::connect(&wrongBridge, &McpStdioBridge::messageReady,
                         [&wrongMessages](const QByteArray& message) {
                             wrongMessages.append(message);
                         });
        failIf(!wrongBridge.submitLine(QByteArrayLiteral(
                   R"({"jsonrpc":"2.0","id":9,"method":"initialize","params":{}})")),
               QStringLiteral("wrong-token initialize was rejected locally"));
        failIf(!waitFor([&wrongMessages] { return wrongMessages.size() >= 1; }),
               QStringLiteral("HTTP 401 produced no JSON-RPC error"));
        const QByteArray wrongResponse = wrongMessages.value(0);
        const QJsonObject wrongObject =
            QJsonDocument::fromJson(wrongResponse).object();
        failIf(wrongObject.value(QStringLiteral("id")).toInt() != 9,
               QStringLiteral("HTTP 401 error lost the request id"));
        failIf(wrongObject.value(QStringLiteral("error"))
                       .toObject()
                       .value(QStringLiteral("code"))
                       .toInt()
                   != -32000,
               QStringLiteral("HTTP 401 error code is not -32000"));
        failIf(!wrongObject.value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("401")),
               QStringLiteral("HTTP 401 error is not explicit"));
        failIf(QString::fromUtf8(wrongResponse).contains(QStringLiteral("wrong-token")),
               QStringLiteral("HTTP 401 error leaked the token"));
        failIf(QString::fromUtf8(wrongResponse).contains(QStringLiteral("test-token")),
               QStringLiteral("HTTP 401 error leaked the server token"));
        failIf(!waitFor([&wrongBridge] { return wrongBridge.inFlight() == 0; }),
               QStringLiteral("HTTP 401 request is still in flight"));
    }

#ifdef PVFORGE_MCP_BINARY
    const QString binary = QString::fromUtf8(PVFORGE_MCP_BINARY);
    const QProcessEnvironment environment = cleanProcessEnvironment();

    {
        QProcess versionProcess;
        versionProcess.setProcessEnvironment(environment);
        versionProcess.start(binary, QStringList{QStringLiteral("--version")});
        if (!versionProcess.waitForFinished(3000))
        {
            versionProcess.kill();
            fail(QStringLiteral("--version did not exit"));
        }
        else
        {
            failIf(versionProcess.exitCode() != 0,
                   QStringLiteral("--version exit code %1").arg(versionProcess.exitCode()));
            const QString output =
                QString::fromUtf8(versionProcess.readAllStandardOutput());
            failIf(!output.contains(QStringLiteral("pvforge-mcp")),
                   QStringLiteral("--version output is %1").arg(output.trimmed()));
        }
    }

    {
        QProcess helpProcess;
        helpProcess.setProcessEnvironment(environment);
        helpProcess.start(binary, QStringList{QStringLiteral("--help")});
        if (!helpProcess.waitForFinished(3000))
        {
            helpProcess.kill();
            fail(QStringLiteral("--help did not exit"));
        }
        else
        {
            failIf(helpProcess.exitCode() != 0,
                   QStringLiteral("--help exit code %1").arg(helpProcess.exitCode()));
            const QString output =
                QString::fromUtf8(helpProcess.readAllStandardOutput());
            failIf(!output.contains(QStringLiteral("--endpoint-file")),
                   QStringLiteral("--help output does not document --endpoint-file"));
        }
    }

    {
        QProcess missingProcess;
        missingProcess.setProcessEnvironment(environment);
        missingProcess.start(binary,
                             QStringList{QStringLiteral("--endpoint-file"),
                                         tempDir.filePath(
                                             QStringLiteral("does-not-exist.json"))});
        if (!missingProcess.waitForFinished(3000))
        {
            missingProcess.kill();
            fail(QStringLiteral("missing endpoint process did not exit"));
        }
        else
        {
            failIf(missingProcess.exitCode() != 2,
                   QStringLiteral("missing endpoint exit code is %1 (expected 2)")
                       .arg(missingProcess.exitCode()));
            failIf(!QString::fromUtf8(missingProcess.readAllStandardError())
                        .contains(QStringLiteral("endpoint file")),
                   QStringLiteral("missing endpoint error is not explicit"));
        }
    }

    QJsonObject liveFileObject;
    liveFileObject[QStringLiteral("format")] = QStringLiteral("pvforge-mcp-endpoint");
    liveFileObject[QStringLiteral("version")] = 1;
    liveFileObject[QStringLiteral("url")] = liveEndpoint.url.toString();
    liveFileObject[QStringLiteral("token")] =
        QString::fromUtf8(liveEndpoint.token);
    liveFileObject[QStringLiteral("pid")] =
        double(QCoreApplication::applicationPid());
    failIf(!writeEndpointFile(
               endpointPath,
               QJsonDocument(liveFileObject).toJson(QJsonDocument::Compact), true),
           QStringLiteral("cannot write the live endpoint file"));

    {
        QProcess process;
        process.setProcessEnvironment(environment);
        QByteArray standardOutput;
        QByteArray standardError;
        QObject::connect(&process, &QProcess::readyReadStandardOutput, [&] {
            standardOutput += process.readAllStandardOutput();
        });
        QObject::connect(&process, &QProcess::readyReadStandardError, [&] {
            standardError += process.readAllStandardError();
        });

        QEventLoop loop;
        QObject::connect(&process, &QProcess::finished, &loop,
                         [&loop](int, QProcess::ExitStatus) { loop.quit(); });
        QObject::connect(&process, &QProcess::errorOccurred, &loop,
                         [&loop](QProcess::ProcessError processError) {
                             if (processError == QProcess::FailedToStart)
                                 loop.quit();
                         });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        process.start(binary, QStringList{QStringLiteral("--endpoint-file"),
                                          endpointPath});
        if (!process.waitForStarted(3000))
        {
            fail(QStringLiteral("pvforge-mcp failed to start: %1")
                     .arg(process.errorString()));
        }
        else
        {
            process.write(QByteArrayLiteral(
                              R"({"jsonrpc":"2.0","id":10,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"cli-test","version":"1"}}})")
                          + '\n');
            process.write(initializedNotification + '\n');
            process.write(QByteArrayLiteral(
                              R"({"jsonrpc":"2.0","id":11,"method":"tools/list","params":{}})")
                          + '\n');
            process.closeWriteChannel();

            timeout.start(8000);
            loop.exec();

            if (process.state() != QProcess::NotRunning)
            {
                process.kill();
                process.waitForFinished(1000);
                fail(QStringLiteral("pvforge-mcp did not exit after stdin EOF"));
            }
            standardOutput += process.readAllStandardOutput();
            standardError += process.readAllStandardError();

            failIf(process.exitStatus() != QProcess::NormalExit,
                   QStringLiteral("pvforge-mcp crashed: %1")
                       .arg(QString::fromUtf8(standardError)));
            failIf(process.exitCode() != 0,
                   QStringLiteral("pvforge-mcp exit code %1: %2")
                       .arg(process.exitCode())
                       .arg(QString::fromUtf8(standardError)));
            failIf(standardOutput.contains(QByteArrayLiteral("pvforge-mcp:")),
                   QStringLiteral("a log line polluted stdout"));

            QList<QByteArray> lines = standardOutput.split('\n');
            lines.removeAll(QByteArray());
            failIf(lines.size() != 2,
                   QStringLiteral("CLI stdout has %1 lines: %2")
                       .arg(lines.size())
                       .arg(QString::fromUtf8(standardOutput)));

            QSet<int> ids;
            for (const QByteArray& line : lines)
            {
                QJsonParseError lineError;
                const QJsonDocument document =
                    QJsonDocument::fromJson(line, &lineError);
                failIf(lineError.error != QJsonParseError::NoError
                           || !document.isObject(),
                       QStringLiteral("non-JSON stdout line: %1")
                           .arg(QString::fromUtf8(line)));
                if (document.isObject())
                {
                    const QJsonObject object = document.object();
                    failIf(object.value(QStringLiteral("jsonrpc")).toString()
                               != QStringLiteral("2.0"),
                           QStringLiteral("CLI response is not JSON-RPC 2.0"));
                    ids.insert(object.value(QStringLiteral("id")).toInt());
                }
            }
            failIf(!ids.contains(10) || !ids.contains(11),
                   QStringLiteral("CLI did not forward both responses"));
        }
    }
#else
    qWarning() << "mcp-bridge-test: PVFORGE_MCP_BINARY is not defined; skipping CLI "
                  "checks";
#endif

    qInfo() << "mcp-bridge-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace pvf
