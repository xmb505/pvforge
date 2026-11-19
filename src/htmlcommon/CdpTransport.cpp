#include "htmlcommon/CdpTransport.h"

#include "htmlcommon/PipeCdpTransport.h"
#include "htmlcommon/WsClient.h"

#include <QUrl>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace pvf {
namespace html {

namespace {

class LoopbackWebSocketTransport : public CdpTransport
{
public:
    explicit LoopbackWebSocketTransport(QObject* parent = nullptr)
        : CdpTransport(parent)
        , m_ws(new WsClient(this))
    {
        connect(m_ws, &WsClient::textMessage, this, &CdpTransport::messageReceived);
        connect(m_ws, &WsClient::disconnected, this, &CdpTransport::disconnected);
        connect(m_ws, &WsClient::errorOccurred, this, &CdpTransport::errorOccurred);
    }

    bool connectTo(const QUrl& endpoint, int timeoutMs, QString* error) override
    {
        return m_ws->connectToUrl(endpoint, timeoutMs, error);
    }

    void close() override { m_ws->close(); }
    bool isConnected() const override { return m_ws->isConnected(); }
    void sendMessage(const QString& message) override { m_ws->sendText(message); }

private:
    WsClient* m_ws = nullptr;
};

// Per-thread hand-off.  HtmlBrowser starts Chrome and creates CdpClient on the
// same thread; the final export renderer lives on its own thread, so a
// process-global slot would let two browsers steal each other's descriptors
// (issue #126).
struct PendingPipe
{
    int readFd = -1;
    int writeFd = -1;
};

thread_local PendingPipe t_pendingPipe;

bool takePendingCdpPipe(int* readFd, int* writeFd)
{
    if (t_pendingPipe.readFd < 0 || t_pendingPipe.writeFd < 0)
        return false;
    *readFd = t_pendingPipe.readFd;
    *writeFd = t_pendingPipe.writeFd;
    t_pendingPipe = PendingPipe{};
    return true;
}

} // namespace

bool setPendingCdpPipe(int readFd, int writeFd, QString* error)
{
    if (readFd < 0 || writeFd < 0)
    {
        if (error)
            *error = QStringLiteral("invalid CDP pipe descriptors");
        return false;
    }
    if (t_pendingPipe.readFd >= 0 || t_pendingPipe.writeFd >= 0)
    {
        if (error)
            *error = QStringLiteral("a CDP pipe hand-off is already pending");
        return false;
    }
    t_pendingPipe.readFd = readFd;
    t_pendingPipe.writeFd = writeFd;
    return true;
}

void discardPendingCdpPipe()
{
#ifdef Q_OS_UNIX
    if (t_pendingPipe.readFd >= 0)
        ::close(t_pendingPipe.readFd);
    if (t_pendingPipe.writeFd >= 0)
        ::close(t_pendingPipe.writeFd);
#endif
    t_pendingPipe = PendingPipe{};
}

CdpTransport* createLoopbackCdpTransport(QObject* parent)
{
    int readFd = -1;
    int writeFd = -1;
    if (takePendingCdpPipe(&readFd, &writeFd))
        return createPipeCdpTransport(readFd, writeFd, parent);
    return new LoopbackWebSocketTransport(parent);
}

} // namespace html
} // namespace pvf
