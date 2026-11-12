#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

class QProcess;
class QTemporaryDir;

namespace pvf {
namespace html {

class CdpClient;

struct BrowserStartOptions
{
    QString executable;            // empty: auto-detect
    int timeoutMs = 15000;         // browser startup deadline
    bool offline = true;           // block DNS/background traffic
    QStringList extraArguments;
};

// Owns one headless Chromium process. All HTML layers share this process and
// create one CDP page each.
class HtmlBrowser : public QObject
{
    Q_OBJECT

public:
    // Stage 0 spike memory measurement: two pages cost ~1.6 GiB. v1 keeps at
    // most two HTML layers and captures them serially.
    static constexpr int kMaxLayers = 2;

    using CaptureGrant = std::function<void()>;

    explicit HtmlBrowser(QObject* parent = nullptr);
    ~HtmlBrowser() override;

    // Executable names in probe order.
    static QStringList candidateExecutables();
    // Returns an absolute executable path, or an empty string. When
    // explicitPath is non-empty it must itself be executable.
    static QString detectExecutable(const QString& explicitPath = QString());
    static QString installHint();

    // Production transport is Chromium's --remote-debugging-pipe (fd 3/4).
    // Setting PVFORGE_CDP_TRANSPORT=ws selects the loopback WebSocket backend
    // for debugging; production never uses it.
    static bool pipeTransportSupported();

    bool start(const QString& executable = QString(), QString* error = nullptr);
    bool start(const BrowserStartOptions& options, QString* error = nullptr);
    void stop();

    // Manual path from Settings > HTML. An empty path means auto-detection.
    void setExecutablePath(const QString& path) { m_requestedExecutable = path; }
    QString requestedExecutable() const { return m_requestedExecutable; }

    bool isRunning() const;
    bool available() const;
    QString executablePath() const { return m_executable; }
    QString version() const { return m_version; }
    QString devToolsUrl() const { return m_wsUrl; }
    QString lastError() const { return m_lastError; }
    CdpClient* cdp() const { return m_cdp; }
    // True when the current process was started with --remote-debugging-pipe.
    bool usesPipe() const { return m_usingPipe; }
    qint64 browserProcessId() const;

    // Headless Chrome only composites the foreground tab. Every renderer asks
    // for this browser-wide capture slot before activating its page and taking
    // a screenshot; the slot is released in HtmlRenderer::finish().
    void acquireCapture(const void* owner, quint64 token, CaptureGrant grant);
    void releaseCapture(const void* owner);
    void cancelCapture(const void* owner);

signals:
    void crashed(const QString& reason);

private:
    struct CaptureWaiter
    {
        const void* owner = nullptr;
        quint64 token = 0;
        CaptureGrant grant;
    };

    void grantNextCapture();
    bool waitForEndpoint(int timeoutMs, QString* error);
    bool waitForPipeReady(int timeoutMs, QString* error);
    bool openPipeDescriptors(QString* error);
    void closePipeDescriptors();
    void cleanupProcess();
    void readProcessOutput(QByteArray* buffer);

    QProcess* m_process = nullptr;
    std::unique_ptr<QTemporaryDir> m_userDataDir;
    CdpClient* m_cdp = nullptr;
    QString m_requestedExecutable;
    QString m_executable;
    QString m_version;
    QString m_wsUrl;
    QString m_lastError;
    bool m_stopping = false;
    bool m_usingPipe = false;
    // [0] is the child end mapped to fd 3 (commands); [1] is the parent end.
    int m_pipeToBrowser[2] = {-1, -1};
    // [0] is the parent end; [1] is the child end mapped to fd 4 (replies).
    int m_pipeFromBrowser[2] = {-1, -1};

    const void* m_captureOwner = nullptr;
    QVector<CaptureWaiter> m_captureQueue;
};

} // namespace html
} // namespace pvf
