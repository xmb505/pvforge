#pragma once

#include <QObject>
#include <QString>

class QUrl;

namespace pvf {
namespace html {

// Transport seam for the Chrome DevTools Protocol.
//
// P0 (security review B4): production uses Chromium's
// `--remote-debugging-pipe`: the browser reads commands from fd 3 and writes
// replies to fd 4 (verified against Chrome 157 on Linux). Messages are
// UTF-8 JSON followed by one NUL byte. No TCP port is opened, so other local
// processes cannot reach /json or the DevTools socket.
//
// The loopback WebSocket backend remains only as a debugging fallback
// (`PVFORGE_CDP_TRANSPORT=ws`). It still never passes `--remote-allow-origins`
// and only accepts 127.0.0.1 endpoints.
//
// CdpClient constructs its own transport, so HtmlBrowser publishes the parent
// ends of the child's fd 3/4 through setPendingCdpPipe() immediately before
// constructing CdpClient. The factory below consumes that hand-off and returns
// a PipeCdpTransport; without one it returns the loopback transport. Keep all
// transport-specific framing inside the implementation.
class CdpTransport : public QObject
{
    Q_OBJECT

public:
    explicit CdpTransport(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~CdpTransport() override = default;

    virtual bool connectTo(const QUrl& endpoint, int timeoutMs, QString* error) = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    virtual void sendMessage(const QString& message) = 0;

signals:
    void messageReceived(const QString& message);
    void disconnected(const QString& reason);
    void errorOccurred(const QString& error);
};

// Returns a pipe transport when HtmlBrowser handed off Chrome's fd 3/4 parent
// ends, otherwise the loopback WebSocket transport.
CdpTransport* createLoopbackCdpTransport(QObject* parent);

} // namespace html
} // namespace pvf
