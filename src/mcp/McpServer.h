#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;

namespace pvf {

class McpCommandService;
class Project;

// Minimal Streamable HTTP MCP server for the read-only stage. It speaks
// JSON-RPC 2.0 over POST /mcp on a loopback-only QTcpServer, authenticates
// with a Bearer token and exposes the read tools plus two guide resources.
// Everything runs on the GUI thread; a future log window can connect to
// toolInvoked() without touching the HTTP layer.
//
// The HTTP boundary is fail-closed: only loopback peers, loopback Host and
// loopback Origin headers are accepted, CORS is never granted, connections
// and requests are bounded in size, count and time.
class McpServer : public QObject
{
    Q_OBJECT

public:
    struct HttpLimits
    {
        qint64 maxBodyBytes = 1024 * 1024;
        int maxHeaderBytes = 16 * 1024;
        int maxConnections = 16;
        int headerTimeoutMs = 10 * 1000;
        int requestTimeoutMs = 20 * 1000;
    };

    explicit McpServer(const Project* project, QObject* parent = nullptr);
    ~McpServer() override;

    // An empty token is replaced with a fresh random one; the server never
    // accepts requests without a configured token.
    void setToken(const QByteArray& token);
    QByteArray token() const { return m_token; }

    // Human-armed write service. The server never owns it and never interprets
    // write arguments: every write tool is forwarded to
    // McpCommandService::execute(). While the service is not armed, tools/list
    // stays read-only and direct write calls are forwarded so a stopped
    // service can still answer stopped_by_human. A null service means the
    // server is permanently read-only for this launch.
    void setCommandService(McpCommandService* service);
    McpCommandService* commandService() const { return m_commandService; }
    bool writeToolsAvailable() const;

    // Names added to tools/list while a human has armed the command service.
    static QStringList writeToolNames();

    // Endpoint discovery file read by the pvforge-mcp stdio bridge. An empty
    // override (the default) uses $XDG_CONFIG_HOME/PVForge/mcp-endpoint.json.
    // Must be set before start(); the file is removed by stop().
    static QString defaultEndpointPath();
    void setEndpointFilePath(const QString& path);
    QString endpointFilePath() const;

    void setMaxBodyBytes(qint64 bytes);
    qint64 maxBodyBytes() const { return m_limits.maxBodyBytes; }
    void setHttpLimits(const HttpLimits& limits);
    const HttpLimits& httpLimits() const { return m_limits; }

    // Number of sockets currently counted against maxConnections.
    int activeConnectionCount() const { return m_connections.size(); }

    // Listens on 127.0.0.1 only. Port 0 picks a free port; the actual port is
    // returned (and also available through port()/endpointUrl()). Returns 0
    // when the server could not start.
    quint16 start(quint16 port = 0, QString* error = nullptr);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }
    QString endpointUrl() const;

    // Names advertised by tools/list, in the order they are returned.
    static QStringList readOnlyToolNames();

signals:
    void toolInvoked(const QString& name, const QString& argsJson,
                     const QString& resultJson, qint64 durationMs, bool ok);
    // Forwarded notifications (for example notifications/cancelled) so the
    // UI can record them. The server never blocks a call on a notification.
    void notificationReceived(const QString& method, const QString& paramsJson);

private:
    void acceptConnections();
    void attachSocket(QTcpSocket* socket);
    void handleRequest(QTcpSocket* socket, const QByteArray& method,
                       const QByteArray& target, const QByteArray& headers,
                       const QByteArray& body);
    void handleRpc(QTcpSocket* socket, const QJsonObject& request);

    bool isAllowedHost(const QByteArray& host) const;
    bool isAllowedOrigin(const QByteArray& origin) const;

    QJsonObject executeTool(const QString& name, const QJsonObject& args, bool* ok);

    bool writeEndpointFile(QString* error);
    void removeEndpointFile();

    static QByteArray generateToken();

    const Project* m_project = nullptr;
    McpCommandService* m_commandService = nullptr;
    QByteArray m_token;
    QString m_endpointFilePath;
    HttpLimits m_limits;
    QSet<QTcpSocket*> m_connections;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
};

} // namespace pvf
