#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace pvf::html2 {

// Dedicated hostname for the one origin a rendered HTML layer may use. Chrome
// resolves it to 127.0.0.1 through --host-resolver-rules; literal loopback
// names stay behind the dead proxy and are additionally blocked by CDP Fetch.
inline constexpr char kBundleHost[] = "pvforge-bundle.invalid";

// True when `path` is a non-empty relative path that cannot escape a bundle:
// no absolute prefix, no ".." segment, no NUL and no backslash.
bool isSafeBundleRelativePath(const QString& path);

// Loopback-only static file server for one .pvhtml bundle directory.
//
// Security rules:
//  - binds 127.0.0.1 with an ephemeral port, never a public interface;
//  - serves only regular files whose canonical path stays inside the bundle;
//  - rejects "..", absolute paths, percent-encoded traversal and symlinks that
//    resolve outside the bundle;
//  - no directory listing; "/" maps to index.html.
//
// Linux cannot report a TCP peer's UID: SO_PEERCRED is AF_UNIX-only and
// returns pid 0 for TCP. The ephemeral port plus page CSP/CDP request blocking
// keep the bundle away from the page, but another same-machine process can
// still fetch files if it discovers the port. A UID boundary would require an
// AF_UNIX listener with SO_PEERCRED plus a browser-side HTTP-over-UDS bridge;
// that is not implemented because Chromium has no supported UDS proxy client.
//
// The server keeps the port and the canonical root so the renderer can reuse
// one origin for every frame of a bundle.
class LocalBundleServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalBundleServer(QObject* parent = nullptr);
    ~LocalBundleServer() override;

    bool start(const QString& bundleDir, QString* error = nullptr);
    void stop();

    bool isRunning() const { return m_server != nullptr; }
    QString rootDir() const { return m_rootDir; }
    quint16 port() const { return m_port; }
    QUrl baseUrl() const;
    QString lastError() const { return m_lastError; }

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void serveFile(QTcpSocket* socket, const QString& relativePath, bool headOnly);
    void sendError(QTcpSocket* socket, int status, const QString& reason);

    static QByteArray mimeTypeForPath(const QString& path);

    QTcpServer* m_server = nullptr;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QSet<QTcpSocket*> m_connections;
    QString m_rootDir;   // canonical bundle root
    quint16 m_port = 0;
    QString m_lastError;
};

} // namespace pvf::html2
