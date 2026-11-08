#pragma once

#include <QObject>
#include <QString>

class QUrl;

namespace pvf {
namespace html {

// Transport seam for the Chrome DevTools Protocol.
//
// P0 (security review B4): production builds must use Chromium's
// `--remote-debugging-pipe` (fd 3 read / fd 4 write, NUL-delimited JSON).
// A loopback WebSocket is kept for this stage because it is already covered by
// tests and can be swapped behind this interface without touching CdpClient or
// HtmlRenderer. The current backend never uses `--remote-allow-origins`, never
// listens on a fixed port, and accepts only 127.0.0.1 endpoints.
//
// A future PipeCdpTransport will ignore the URL argument: HtmlBrowser creates
// the pipes in setChildProcessModifier() and calls connectTo() with an empty
// endpoint. Keep all transport-specific framing inside the implementation.
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

// Current stage backend. See the P0 note above.
CdpTransport* createLoopbackCdpTransport(QObject* parent);

} // namespace html
} // namespace pvf
