#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace pvf {

// Connection information resolved from the command line, the environment or
// the endpoint file written by PVForge's MCP settings page.
struct McpEndpoint
{
    QUrl url;
    QByteArray token;
    qint64 pid = 0;
    QString source;
};

// Linux/macOS: $XDG_CONFIG_HOME/PVForge/mcp-endpoint.json (or ~/.config/...).
// Windows: the per-user application data directory.
QString defaultMcpEndpointPath();

// Reads and validates an endpoint file. On POSIX the file must be a regular
// file with mode 0600 and be owned by the current user. Token contents are
// never included in error messages.
bool loadMcpEndpointFile(const QString& path, McpEndpoint* endpoint, QString* error);

// Resolves each field independently with the precedence
// argument > environment > endpoint file. The endpoint file is only read when
// at least one field still needs it. endpointPath may be empty to use the
// platform default.
bool resolveMcpEndpoint(const QString& endpointPath,
                        const QString& argumentUrl,
                        const QByteArray& argumentToken,
                        const QString& environmentUrl,
                        const QByteArray& environmentToken,
                        McpEndpoint* endpoint,
                        QString* error);

// Transparent stdio -> Streamable HTTP MCP forwarder. It accepts one JSON-RPC
// message per submitLine() call, performs one POST per message, and emits the
// server's JSON-RPC messages (JSON or SSE) through messageReady(). It never
// writes to stdout/stderr itself: diagnostics are emitted as signals so the
// CLI can keep stdout clean.
class McpStdioBridge : public QObject
{
    Q_OBJECT

public:
    explicit McpStdioBridge(const McpEndpoint& endpoint, QObject* parent = nullptr);
    ~McpStdioBridge() override;

    const McpEndpoint& endpoint() const { return m_endpoint; }
    void setEndpoint(const McpEndpoint& endpoint) { m_endpoint = endpoint; }

    void setTimeoutMs(int milliseconds);
    int timeoutMs() const { return m_timeoutMs; }

    int inFlight() const;

    // Returns false only when the line is not a JSON object; a JSON-RPC parse
    // error is still emitted through messageReady().
    bool submitLine(const QByteArray& line);

    // Aborts every pending POST without emitting further messages.
    void abortAll();

signals:
    // One complete JSON-RPC message, already compacted to a single line.
    void messageReady(const QByteArray& message);
    // Human-readable log line; the CLI sends this to stderr.
    void diagnostic(const QString& message);
    // Emitted when the last in-flight POST finishes.
    void idle();

private:
    struct Transfer;

    void processEventStream(QNetworkReply* reply, Transfer* transfer);
    void flushEventStream(QNetworkReply* reply, Transfer* transfer);
    void dispatchEventData(QNetworkReply* reply, Transfer* transfer);
    void finishTransfer(QNetworkReply* reply);
    void cancelById(const QByteArray& idKey);
    void sendError(const QJsonValue& id, int code, const QString& message);
    static QByteArray idKey(const QJsonValue& id);

    QNetworkAccessManager* m_manager = nullptr;
    McpEndpoint m_endpoint;
    int m_timeoutMs = 60000;
    QHash<QNetworkReply*, Transfer*> m_transfers;
    QHash<QByteArray, QNetworkReply*> m_byId;
};

} // namespace pvf
