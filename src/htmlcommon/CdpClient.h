#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QUrl;

namespace pvf {
namespace html {

class CdpTransport;

// Small JSON-RPC layer over the browser-level DevTools transport. Page
// commands are routed with a flattened sessionId, so one browser process can
// host one page per HTML layer.
class CdpClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QJsonObject& result, const QString& error)>;

    explicit CdpClient(QObject* parent = nullptr);
    ~CdpClient() override;

    bool connectTo(const QUrl& endpoint, int timeoutMs, QString* error);
    void close();
    bool isConnected() const;

    // Every call completes exactly once, unless the client is destroyed. A
    // timed-out id is forgotten, so a late browser response is ignored.
    void call(const QString& method, const QJsonObject& params, const QString& sessionId,
              int timeoutMs, Callback done);

signals:
    void eventReceived(const QString& method, const QJsonObject& params,
                       const QString& sessionId);
    void disconnected(const QString& reason);

private:
    struct Pending
    {
        QString method;
        Callback done;
    };

    void onMessage(const QString& message);
    void onDisconnected(const QString& reason);
    void failAll(const QString& reason);

    CdpTransport* m_transport = nullptr;
    QHash<int, Pending> m_pending;
    int m_nextId = 1;
};

} // namespace html
} // namespace pvf
