#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QUrl;

namespace pvf {
namespace html {

// Minimal RFC 6455 client for the Chrome DevTools endpoint.
//
// The build machine may not have the Qt6 WebSockets development package, so
// this implements only the subset CDP needs: a masked text client over
// 127.0.0.1, fragmentation, ping/pong and close handshake. It deliberately
// refuses non-loopback URLs.
class WsClient : public QObject
{
    Q_OBJECT

public:
    explicit WsClient(QObject* parent = nullptr);
    ~WsClient() override;

    // Performs the HTTP Upgrade handshake. Bounded by timeoutMs; the browser
    // endpoint always lives on loopback.
    bool connectToUrl(const QUrl& url, int timeoutMs, QString* error);
    void close();
    bool isConnected() const { return m_connected; }

    void sendText(const QString& message);

signals:
    void textMessage(const QString& message);
    void disconnected(const QString& reason);
    void errorOccurred(const QString& error);

private:
    void onReadyRead();
    void onSocketDisconnected();
    void onSocketError();
    void parseFrames();
    void deliverMessage(quint8 opcode, const QByteArray& payload);
    void sendFrame(quint8 opcode, const QByteArray& payload);
    void fail(const QString& error);

    QTcpSocket* m_socket = nullptr;
    QByteArray m_rx;
    QByteArray m_fragment;
    quint8 m_fragmentOpcode = 0;
    bool m_connected = false;
    bool m_closeSent = false;
    QString m_disconnectReason;
    QString m_lastError;
};

} // namespace html
} // namespace pvf
