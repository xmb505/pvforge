#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace pvf::mcp2 {

class Service;

// Loopback-only Streamable HTTP transport for the MCP tool service.
//
// The listener always binds 127.0.0.1. Requests must come from a loopback
// peer, carry a loopback Host (and Origin when present), and present the
// persistent Bearer token. CORS is never granted.
//
// Port and token live in a small persistent config file so restarting the
// application keeps the same endpoint. The token is generated once (32 random
// bytes, hex) and only changes through resetCredentials() or setToken().
class HttpServer : public QObject
{
    Q_OBJECT

public:
    // Default fixed port. A config file may store a different port.
    static constexpr quint16 kDefaultPort = 47821;

    explicit HttpServer(Service* service, QObject* parent = nullptr);
    ~HttpServer() override;

    // Empty generates a fresh token and writes it to the config file.
    void setToken(const QByteArray& token);
    QByteArray token() const { return m_token; }

    // Generates a new token, keeps the configured port, persists the config
    // and refreshes the endpoint file when the server is running. This is the
    // only way the token changes by itself.
    void resetCredentials();

    // Binds 127.0.0.1. A non-zero port overrides the configured one for this
    // run (and is persisted). Port 0 uses the configured port; a configured
    // port of 0 picks an ephemeral port once and persists it. A busy port is
    // reported through error and never silently replaced.
    quint16 start(quint16 port = 0, QString* error = nullptr);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }
    QString endpointUrl() const;

    // Defaults to $XDG_CONFIG_HOME/PVForge/mcp-endpoint.json.
    void setEndpointFilePath(const QString& path);
    QString endpointFilePath() const;

    // Defaults to $XDG_CONFIG_HOME/PVForge/mcp.json.
    void setConfigFilePath(const QString& path);
    QString configFilePath() const;

signals:
    void toolInvoked(const QString& name, const QString& argsJson,
                     const QString& resultJson, qint64 durationMs, bool ok);

private:
    struct PendingRequest;

    void acceptConnections();
    void attachSocket(QTcpSocket* socket);

    bool loadOrCreateConfig(QString* error);
    bool saveConfig(QString* error);
    bool writeEndpointFile(QString* error);
    void removeEndpointFile();

    bool isAllowedHost(const QByteArray& host) const;
    bool isAllowedOrigin(const QByteArray& origin) const;

    void handleRpc(QTcpSocket* socket, const QJsonObject& request);
    void sendRpcResult(QTcpSocket* socket, const QJsonValue& id,
                       const QJsonObject& result,
                       const QByteArray& extraHeaders = QByteArray());
    void sendRpcError(QTcpSocket* socket, const QJsonValue& id, int code,
                      const QString& message);

    static QByteArray generateToken();

    Service* m_service = nullptr;
    QByteArray m_token;
    QByteArray m_sessionId;
    QString m_endpointFilePath;
    QString m_configFilePath;
    quint16 m_configuredPort = kDefaultPort;
    bool m_endpointWritten = false;
    QSet<QTcpSocket*> m_connections;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
};

} // namespace pvf::mcp2
