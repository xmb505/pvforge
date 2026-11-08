#include "html/CdpTransport.h"

#include "html/WsClient.h"

#include <QUrl>

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

} // namespace

CdpTransport* createLoopbackCdpTransport(QObject* parent)
{
    return new LoopbackWebSocketTransport(parent);
}

} // namespace html
} // namespace pvf
