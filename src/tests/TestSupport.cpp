#include "TestSupport.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

namespace pvf::tests {

McpHttpResponse mcpExchange(quint16 port, const QByteArray& request,
                            int timeoutMs)
{
    McpHttpResponse response;
    QTcpSocket socket;
    QByteArray received;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&response, &loop] {
        response.timedOut = true;
        loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        socket.write(request);
        socket.flush();
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] {
        received += socket.readAll();
        const int headerEnd = received.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        if (response.status < 0)
        {
            const QByteArray head = received.left(headerEnd);
            response.headers = head;
            const QList<QByteArray> statusLine =
                head.split('\n').first().trimmed().split(' ');
            if (statusLine.size() >= 2)
                response.status = statusLine.at(1).toInt();
            for (const QByteArray& rawLine : head.split('\n'))
            {
                QByteArray line = rawLine;
                if (line.endsWith('\r'))
                    line.chop(1);
                const int colon = line.indexOf(':');
                if (colon > 0
                    && line.left(colon).trimmed().toLower()
                           == QByteArrayLiteral("content-length"))
                    response.expectedBody =
                        line.mid(colon + 1).trimmed().toInt();
            }
        }
        if (received.size() >= headerEnd + 4 + response.expectedBody)
            loop.quit();
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop,
                     [&loop](QAbstractSocket::SocketError) { loop.quit(); });

    timeout.start(timeoutMs);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();

    const int headerEnd = received.indexOf("\r\n\r\n");
    if (headerEnd >= 0)
        response.body = received.mid(headerEnd + 4);
    socket.abort();
    return response;
}

QByteArray mcpPostRequest(quint16 port, const QByteArray& payload,
                          const QByteArray& token, const QByteArray& contentType,
                          qint64 declaredLength, const QByteArray& extraHeaders,
                          const QByteArray& hostHeader)
{
    QByteArray request;
    request += "POST /mcp HTTP/1.1\r\n";
    request += "Host: "
               + (hostHeader.isEmpty()
                      ? QByteArrayLiteral("127.0.0.1:")
                            + QByteArray::number(port)
                      : hostHeader)
               + "\r\n";
    request += "Authorization: Bearer " + token + "\r\n";
    request += "Content-Type: " + contentType + "\r\n";
    request += "Accept: application/json\r\n";
    request += "Content-Length: "
               + QByteArray::number(declaredLength >= 0 ? declaredLength
                                                        : qint64(payload.size()))
               + "\r\n";
    request += extraHeaders;
    request += "Connection: close\r\n\r\n";
    request += payload;
    return request;
}

McpHttpResponse mcpHttpPost(quint16 port, const QByteArray& payload,
                            const QByteArray& token,
                            const QByteArray& contentType,
                            qint64 declaredLength,
                            const QByteArray& extraHeaders,
                            const QByteArray& hostHeader)
{
    return mcpExchange(port,
                       mcpPostRequest(port, payload, token, contentType,
                                      declaredLength, extraHeaders, hostHeader));
}

bool mcpWaitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs)
    {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

int mcpStatusFromRaw(const QByteArray& received)
{
    const int headerEnd = received.indexOf("\r\n\r\n");
    const QByteArray head =
        headerEnd >= 0 ? received.left(headerEnd) : received;
    const QList<QByteArray> statusLine =
        head.split('\n').first().trimmed().split(' ');
    return statusLine.size() >= 2 ? statusLine.at(1).toInt() : -1;
}

QJsonObject mcpRpc(quint16 port, const QByteArray& token, const QJsonObject& request,
                   int* statusOut)
{
    const McpHttpResponse response = mcpHttpPost(
        port, QJsonDocument(request).toJson(QJsonDocument::Compact), token);
    if (statusOut)
        *statusOut = response.status;
    return QJsonDocument::fromJson(response.body).object();
}

QJsonObject mcpStructuredResult(const McpHttpResponse& response)
{
    const QJsonObject body = QJsonDocument::fromJson(response.body).object();
    return body.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("structuredContent"))
        .toObject();
}

McpHttpResponse mcpCallTool(quint16 port, const QByteArray& token,
                            const QString& tool, const QJsonObject& args)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), tool);
    params.insert(QStringLiteral("arguments"), args);
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), 1);
    request.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
    request.insert(QStringLiteral("params"), params);
    return mcpHttpPost(port,
                       QJsonDocument(request).toJson(QJsonDocument::Compact),
                       token);
}


} // namespace pvf::tests
