#pragma once

#include "htmlcommon/CdpTransport.h"

#include <QByteArray>
#include <QString>

class QSocketNotifier;
class QUrl;

namespace pvf {
namespace html {

// CDP over Chromium's --remote-debugging-pipe.
//
// The browser reads commands from inherited fd 3 and writes replies to fd 4
// (verified against Chrome 157 on Linux). Each message is a single UTF-8 JSON
// object followed by one NUL byte; there is no WebSocket handshake and no
// length prefix. HtmlBrowser creates the pipes before exec, dup2()s them onto
// fd 3/4 in the child, and hands the parent ends to this transport.
//
// Both parent ends are non-blocking: sendMessage() queues when the pipe is
// full (a Page.setDocumentContent command can be megabytes) and drains from a
// writable notifier, so the UI thread never blocks on the browser.
class PipeCdpTransport : public CdpTransport
{
    Q_OBJECT

public:
    // Takes ownership of both descriptors.
    PipeCdpTransport(int readFd, int writeFd, QObject* parent = nullptr);
    ~PipeCdpTransport() override;

    bool connectTo(const QUrl& endpoint, int timeoutMs, QString* error) override;
    void close() override;
    bool isConnected() const override { return m_connected; }
    void sendMessage(const QString& message) override;

private:
    void onReadable();
    void onWritable();
    void flushWrites();
    void fail(const QString& reason);

    int m_readFd = -1;
    int m_writeFd = -1;
    QSocketNotifier* m_readNotifier = nullptr;
    QSocketNotifier* m_writeNotifier = nullptr;
    QByteArray m_rx;
    QByteArray m_tx;
    bool m_connected = false;
    bool m_closing = false;
    bool m_failed = false;
};

// True when this platform has the fork/exec descriptor plumbing that the pipe
// transport needs. Non-Unix builds keep the loopback WebSocket fallback.
bool cdpPipeTransportSupported();

// Publishes the parent ends of the pipes that were dup2()ed onto Chrome's
// fd 3/4 for the next createLoopbackCdpTransport() call. Takes ownership of
// both descriptors on success. CdpClient constructs its own transport, so this
// is the injection seam that keeps CdpClient transport-agnostic.
bool setPendingCdpPipe(int readFd, int writeFd, QString* error);

// Closes and forgets a pending hand-off that was never consumed.
void discardPendingCdpPipe();

// Takes ownership of both descriptors.
CdpTransport* createPipeCdpTransport(int readFd, int writeFd, QObject* parent);

} // namespace html
} // namespace pvf
