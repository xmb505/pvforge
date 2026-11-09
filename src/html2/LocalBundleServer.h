#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace pvf::html2 {

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
    QString m_rootDir;   // canonical bundle root
    quint16 m_port = 0;
    QString m_lastError;
};

} // namespace pvf::html2
