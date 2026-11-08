#include "html/WsClient.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>

namespace pvf {
namespace html {

namespace {

constexpr int kMaxMessageBytes = 128 * 1024 * 1024;
constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QByteArray randomBytes(int count)
{
    QByteArray bytes;
    bytes.reserve(count);
    while (bytes.size() < count)
    {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int shift = 24; shift >= 0 && bytes.size() < count; shift -= 8)
            bytes.append(char((value >> shift) & 0xff));
    }
    return bytes;
}

QString headerValue(const QHash<QString, QString>& headers, const QString& name)
{
    return headers.value(name.toLower());
}

} // namespace

WsClient::WsClient(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    // readyRead is connected after the HTTP Upgrade completes; the blocking
    // handshake below reads the socket itself.
    connect(m_socket, &QTcpSocket::disconnected, this, &WsClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &WsClient::onSocketError);
}

WsClient::~WsClient()
{
    close();
}

bool WsClient::connectToUrl(const QUrl& url, int timeoutMs, QString* error)
{
    auto failWith = [&](const QString& message) {
        m_lastError = message;
        if (error)
            *error = message;
        m_socket->abort();
        m_connected = false;
        return false;
    };

    if (m_connected)
        return failWith(QStringLiteral("WebSocket is already connected"));
    if (url.scheme() != QStringLiteral("ws"))
        return failWith(QStringLiteral("CDP endpoint must use ws://"));
    const QString host = url.host();
    if (host != QStringLiteral("127.0.0.1") && host != QStringLiteral("localhost")
        && host != QStringLiteral("::1"))
        return failWith(QStringLiteral("refusing a non-loopback DevTools endpoint: %1").arg(host));

    const int port = url.port(80);
    if (port <= 0 || port > 65535)
        return failWith(QStringLiteral("invalid DevTools port"));
    QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (url.hasQuery())
        path += QLatin1Char('?') + url.query(QUrl::FullyEncoded);

    m_rx.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
    m_closeSent = false;
    m_disconnectReason.clear();
    m_lastError.clear();

    m_socket->connectToHost(host, quint16(port));
    if (!m_socket->waitForConnected(timeoutMs))
        return failWith(QStringLiteral("cannot connect to DevTools at %1:%2: %3")
                            .arg(host)
                            .arg(port)
                            .arg(m_socket->errorString()));

    const QByteArray key = randomBytes(16).toBase64();
    QByteArray request;
    request += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + host.toUtf8() + ':' + QByteArray::number(port) + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    m_socket->write(request);
    if (!m_socket->waitForBytesWritten(qMin(timeoutMs, 3000)))
        return failWith(QStringLiteral("cannot send DevTools handshake"));

    QElapsedTimer timer;
    timer.start();
    QByteArray reply;
    int headerEnd = -1;
    while (headerEnd < 0)
    {
        const int remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0)
            return failWith(QStringLiteral("DevTools handshake timed out"));
        if (!m_socket->waitForReadyRead(remaining))
        {
            if (m_socket->state() != QAbstractSocket::ConnectedState)
                return failWith(QStringLiteral("DevTools closed during handshake"));
            continue;
        }
        reply += m_socket->readAll();
        headerEnd = reply.indexOf("\r\n\r\n");
        if (reply.size() > 64 * 1024 && headerEnd < 0)
            return failWith(QStringLiteral("DevTools sent an oversized handshake"));
    }

    const QByteArray headerBlock = reply.left(headerEnd);
    const QByteArray remainder = reply.mid(headerEnd + 4);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty())
        return failWith(QStringLiteral("empty DevTools handshake"));
    const QByteArray statusLine = lines.first().trimmed();
    if (!statusLine.contains(" 101 "))
        return failWith(QStringLiteral("DevTools refused the WebSocket upgrade: %1")
                            .arg(QString::fromLatin1(statusLine)));

    QHash<QString, QString> headers;
    for (int i = 1; i < lines.size(); ++i)
    {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        headers.insert(QString::fromLatin1(line.left(colon)).toLower(),
                       QString::fromLatin1(line.mid(colon + 1)).trimmed());
    }
    if (headerValue(headers, QStringLiteral("upgrade")).compare(
            QStringLiteral("websocket"), Qt::CaseInsensitive)
        != 0)
        return failWith(QStringLiteral("DevTools handshake has no WebSocket upgrade"));
    const QByteArray expected = QCryptographicHash::hash(key + kWebSocketGuid,
                                                         QCryptographicHash::Sha1)
                                    .toBase64();
    if (headerValue(headers, QStringLiteral("sec-websocket-accept")).toLatin1() != expected)
        return failWith(QStringLiteral("DevTools handshake accept key mismatch"));

    m_connected = true;
    connect(m_socket, &QTcpSocket::readyRead, this, &WsClient::onReadyRead);
    if (remainder.isEmpty())
        m_rx.clear();
    else
    {
        m_rx = remainder;
        parseFrames();
    }
    if (m_socket->bytesAvailable() > 0)
        onReadyRead();
    return true;
}

void WsClient::close()
{
    if (!m_connected)
    {
        if (m_socket)
            m_socket->abort();
        return;
    }
    if (!m_closeSent)
    {
        m_closeSent = true;
        QByteArray payload;
        payload.append(char(0x03));
        payload.append(char(0xe8));   // normal closure, 1000
        sendFrame(0x8, payload);
        m_socket->waitForBytesWritten(200);
    }
    m_connected = false;
    m_disconnectReason.clear();
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_rx.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
}

void WsClient::sendText(const QString& message)
{
    if (!m_connected)
    {
        emit errorOccurred(QStringLiteral("WebSocket is not connected"));
        return;
    }
    sendFrame(0x1, message.toUtf8());
}

void WsClient::sendFrame(quint8 opcode, const QByteArray& payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray frame;
    frame.reserve(payload.size() + 14);
    frame.append(char(0x80 | opcode));
    const quint64 length = quint64(payload.size());
    if (length <= 125)
        frame.append(char(0x80 | quint8(length)));
    else if (length <= 0xffff)
    {
        frame.append(char(0x80 | 126));
        frame.append(char((length >> 8) & 0xff));
        frame.append(char(length & 0xff));
    }
    else
    {
        frame.append(char(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.append(char((length >> shift) & 0xff));
    }

    const QByteArray mask = randomBytes(4);
    frame.append(mask);
    const int headerSize = frame.size();
    frame.resize(headerSize + payload.size());
    char* out = frame.data() + headerSize;
    const char* in = payload.constData();
    for (int i = 0; i < payload.size(); ++i)
        out[i] = char(quint8(in[i]) ^ quint8(mask.at(i & 3)));
    m_socket->write(frame);
}

void WsClient::onReadyRead()
{
    if (!m_socket)
        return;
    m_rx.append(m_socket->readAll());
    if (m_connected)
        parseFrames();
}

void WsClient::parseFrames()
{
    while (m_rx.size() >= 2)
    {
        const quint8 first = quint8(m_rx.at(0));
        const quint8 second = quint8(m_rx.at(1));
        const bool fin = (first & 0x80) != 0;
        const quint8 opcode = first & 0x0f;
        const bool masked = (second & 0x80) != 0;
        quint64 length = second & 0x7f;
        int offset = 2;

        if (length == 126)
        {
            if (m_rx.size() < 4)
                return;
            length = (quint64(quint8(m_rx.at(2))) << 8) | quint8(m_rx.at(3));
            offset = 4;
        }
        else if (length == 127)
        {
            if (m_rx.size() < 10)
                return;
            length = 0;
            for (int i = 0; i < 8; ++i)
                length = (length << 8) | quint8(m_rx.at(2 + i));
            offset = 10;
        }

        if (length > quint64(kMaxMessageBytes))
        {
            fail(QStringLiteral("DevTools WebSocket message is too large"));
            return;
        }

        QByteArray mask;
        if (masked)
        {
            if (m_rx.size() < offset + 4)
                return;
            mask = m_rx.mid(offset, 4);
            offset += 4;
        }
        if (m_rx.size() < offset + int(length))
            return;

        QByteArray payload = m_rx.mid(offset, int(length));
        m_rx.remove(0, offset + int(length));
        if (masked)
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = char(quint8(payload.at(i)) ^ quint8(mask.at(i & 3)));

        if (opcode == 0x8)   // close
        {
            if (!m_closeSent)
            {
                m_closeSent = true;
                sendFrame(0x8, payload.left(2));
            }
            const QString reason = QStringLiteral("DevTools closed the WebSocket");
            m_disconnectReason.clear();
            m_connected = false;
            m_socket->disconnectFromHost();
            emit disconnected(reason);
            return;
        }
        if (opcode == 0x9)   // ping
        {
            sendFrame(0xa, payload);
            continue;
        }
        if (opcode == 0xa)   // pong
            continue;

        if (opcode == 0x0)   // continuation
        {
            if (m_fragmentOpcode == 0)
            {
                fail(QStringLiteral("unexpected WebSocket continuation frame"));
                return;
            }
            m_fragment += payload;
            if (m_fragment.size() > kMaxMessageBytes)
            {
                fail(QStringLiteral("DevTools WebSocket message is too large"));
                return;
            }
            if (fin)
            {
                const quint8 messageOpcode = m_fragmentOpcode;
                QByteArray message;
                message.swap(m_fragment);
                m_fragmentOpcode = 0;
                deliverMessage(messageOpcode, message);
            }
            continue;
        }

        if (opcode == 0x1 || opcode == 0x2)
        {
            if (!fin)
            {
                if (m_fragmentOpcode != 0)
                {
                    fail(QStringLiteral("overlapping WebSocket fragments"));
                    return;
                }
                m_fragmentOpcode = opcode;
                m_fragment = payload;
                if (m_fragment.size() > kMaxMessageBytes)
                {
                    fail(QStringLiteral("DevTools WebSocket message is too large"));
                    return;
                }
            }
            else
                deliverMessage(opcode, payload);
            continue;
        }

        fail(QStringLiteral("unsupported WebSocket opcode %1").arg(opcode));
        return;
    }
}

void WsClient::deliverMessage(quint8 opcode, const QByteArray& payload)
{
    if (opcode == 0x1)
        emit textMessage(QString::fromUtf8(payload));
    // Binary CDP messages are not used by the renderer.
}

void WsClient::onSocketDisconnected()
{
    const bool wasConnected = m_connected;
    m_connected = false;
    const QString reason = m_disconnectReason;
    m_disconnectReason.clear();
    if (!reason.isEmpty())
        emit disconnected(reason);
    else if (wasConnected)
        emit disconnected(QStringLiteral("DevTools connection closed"));
}

void WsClient::onSocketError()
{
    m_lastError = m_socket->errorString();
    if (m_connected)
    {
        m_connected = false;
        const QString reason =
            QStringLiteral("DevTools socket error: %1").arg(m_lastError);
        emit disconnected(reason);
    }
    else
        emit errorOccurred(m_lastError);
}

void WsClient::fail(const QString& error)
{
    m_lastError = error;
    emit errorOccurred(error);
    m_connected = false;
    m_socket->abort();
    emit disconnected(error);
}

} // namespace html
} // namespace pvf
