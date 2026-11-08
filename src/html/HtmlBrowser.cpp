#include "html/HtmlBrowser.h"

#include "html/CdpClient.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace pvf {
namespace html {

namespace {

const QStringList& probeNames()
{
    static const QStringList names{
        QStringLiteral("google-chrome"),
        QStringLiteral("google-chrome-stable"),
        QStringLiteral("microsoft-edge"),
        QStringLiteral("microsoft-edge-stable"),
        QStringLiteral("chromium"),
        QStringLiteral("chromium-browser")};
    return names;
}

QStringList knownPaths()
{
    return {
        QStringLiteral("/opt/google/chrome/google-chrome"),
        QStringLiteral("/opt/google/chrome/chrome"),
        QStringLiteral("/opt/microsoft/msedge/msedge"),
        QStringLiteral("/snap/bin/chromium"),
    };
}

bool hasArguments(const QStringList& arguments, const QString& flag)
{
    for (const QString& argument : arguments)
        if (argument == flag || argument.startsWith(flag + QLatin1Char('=')))
            return true;
    return false;
}

} // namespace

HtmlBrowser::HtmlBrowser(QObject* parent)
    : QObject(parent)
{
}

HtmlBrowser::~HtmlBrowser()
{
    stop();
}

QStringList HtmlBrowser::candidateExecutables()
{
    return probeNames();
}

QString HtmlBrowser::detectExecutable(const QString& explicitPath)
{
    if (!explicitPath.isEmpty())
    {
        const QFileInfo info(explicitPath);
        if (info.exists() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
        return QString();
    }

    for (const QString& name : probeNames())
    {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    for (const QString& path : knownPaths())
    {
        const QFileInfo info(path);
        if (info.exists() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QString();
}

QString HtmlBrowser::installHint()
{
    return QStringLiteral(
        "No Chromium-based browser found. Install Google Chrome, Microsoft Edge or "
        "Chromium, or set an explicit executable path in Settings > HTML.");
}

bool HtmlBrowser::start(const QString& executable, QString* error)
{
    BrowserStartOptions options;
    options.executable = executable;
    return start(options, error);
}

bool HtmlBrowser::start(const BrowserStartOptions& options, QString* error)
{
    auto fail = [&](const QString& message) {
        m_lastError = message;
        if (error)
            *error = message;
        return false;
    };

    if (isRunning())
    {
        if (error)
            error->clear();
        return true;
    }
    stop();

    if (!options.executable.isEmpty())
        m_requestedExecutable = options.executable;
    const QString requested =
        options.executable.isEmpty() ? m_requestedExecutable : options.executable;
    const QString executable = detectExecutable(requested);
    if (executable.isEmpty())
        return fail(requested.isEmpty()
                        ? installHint()
                        : QStringLiteral("browser is not executable: %1").arg(requested));

    m_executable = executable;
    m_version.clear();
    m_lastError.clear();
    m_wsUrl.clear();

    QProcess versionProcess;
    versionProcess.start(executable, {QStringLiteral("--version")});
    if (versionProcess.waitForFinished(3000))
    {
        QByteArray versionOutput = versionProcess.readAllStandardOutput();
        versionOutput += versionProcess.readAllStandardError();
        m_version = QString::fromUtf8(versionOutput).trimmed();
    }

    m_userDataDir = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/pvforge-html-XXXXXX"));
    if (!m_userDataDir->isValid())
    {
        m_userDataDir.reset();
        return fail(QStringLiteral("cannot create a temporary Chrome profile directory"));
    }

    QStringList arguments{
        QStringLiteral("--headless=new"),
        QStringLiteral("--remote-debugging-port=0"),
        QStringLiteral("--user-data-dir=") + m_userDataDir->path(),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--hide-scrollbars"),
        QStringLiteral("--force-device-scale-factor=1"),
        QStringLiteral("--force-color-profile=srgb"),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-extensions"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--disable-dev-shm-usage"),
        QStringLiteral("--disable-backgrounding-occluded-windows"),
        QStringLiteral("--disable-renderer-backgrounding"),
        QStringLiteral("--disable-background-timer-throttling"),
        QStringLiteral("--disable-client-side-phishing-detection"),
        QStringLiteral("--disable-domain-reliability"),
        QStringLiteral("--disable-default-apps"),
        QStringLiteral("--mute-audio"),
        // P0 transport note: production must switch to --remote-debugging-pipe
        // (fd 3/4). This stage still uses a loopback WebSocket and therefore
        // deliberately does NOT pass --remote-allow-origins=*.
        // Network layers: dead proxy for everything, no DNS, no 3D APIs, no
        // crash dumps. Page CSP and CDP URL blocking are applied per page.
        QStringLiteral("--proxy-server=http://127.0.0.1:9"),
        QStringLiteral("--proxy-bypass-list=<-loopback>"),
        QStringLiteral("--dns-prefetch-disable"),
        QStringLiteral("--force-webrtc-ip-handling-policy=disable_non_proxied_udp"),
        QStringLiteral("--disable-3d-apis"),
        QStringLiteral("--disable-webgl"),
        QStringLiteral("--disable-breakpad"),
        QStringLiteral("--disable-crash-reporter"),
    };
    if (options.offline && !hasArguments(arguments, QStringLiteral("--host-resolver-rules")))
    {
        arguments << QStringLiteral("--host-resolver-rules=MAP * ~NOTFOUND")
                  << QStringLiteral("--no-pings");
    }
    for (const QString& extra : options.extraArguments)
        if (!arguments.contains(extra))
            arguments.append(extra);
    if (!hasArguments(arguments, QStringLiteral("--window-size")))
        arguments << QStringLiteral("--window-size=1920,1080");
    arguments << QStringLiteral("about:blank");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(QDir::tempPath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString& key :
         {QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
          QStringLiteral("ALL_PROXY"), QStringLiteral("NO_PROXY"),
          QStringLiteral("http_proxy"), QStringLiteral("https_proxy"),
          QStringLiteral("all_proxy"), QStringLiteral("no_proxy")})
        environment.remove(key);
    m_process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    // Chrome forks renderer and GPU children. Put them in a dedicated process
    // group so stop() can guarantee no leftover browser processes.
    m_process->setChildProcessModifier([]() { ::setpgid(0, 0); });
#endif
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (m_stopping || !m_process)
                    return;
                const QString reason =
                    status == QProcess::CrashExit
                        ? QStringLiteral("browser process crashed")
                        : QStringLiteral("browser process exited with code %1").arg(exitCode);
                m_lastError = reason;
                emit crashed(reason);
            });

    m_process->start(executable, arguments);
    if (!m_process->waitForStarted(5000))
    {
        const QString message = QStringLiteral("cannot start %1: %2")
                                    .arg(executable, m_process->errorString());
        cleanupProcess();
        return fail(message);
    }

    QString endpointError;
    if (!waitForEndpoint(qMax(1000, options.timeoutMs), &endpointError))
    {
        cleanupProcess();
        return fail(endpointError);
    }

    m_cdp = new CdpClient(this);
    QString connectError;
    const int connectTimeout = qMax(1000, qMin(options.timeoutMs, 5000));
    if (!m_cdp->connectTo(QUrl(m_wsUrl), connectTimeout, &connectError))
    {
        delete m_cdp;
        m_cdp = nullptr;
        cleanupProcess();
        return fail(QStringLiteral("cannot connect to the browser DevTools endpoint: %1")
                        .arg(connectError));
    }

    connect(m_cdp, &CdpClient::disconnected, this, [this](const QString& reason) {
        if (!m_stopping && !reason.isEmpty())
            m_lastError = reason;
    });

    if (error)
        error->clear();
    return true;
}

void HtmlBrowser::acquireCapture(const void* owner, quint64 token, CaptureGrant grant)
{
    if (!owner || !grant)
        return;

    // A renderer has at most one capture request in flight. Replace an older
    // queued grant with the newest token so a superseded frame cannot activate
    // the page later.
    for (int i = m_captureQueue.size() - 1; i >= 0; --i)
        if (m_captureQueue.at(i).owner == owner)
            m_captureQueue.removeAt(i);

    if (m_captureOwner == nullptr)
    {
        m_captureOwner = owner;
        QTimer::singleShot(0, this, [this, owner, token, grant]() {
            if (m_captureOwner != owner)
                return;
            Q_UNUSED(token);
            grant();
        });
        return;
    }
    m_captureQueue.append(CaptureWaiter{owner, token, std::move(grant)});
}

void HtmlBrowser::releaseCapture(const void* owner)
{
    if (!owner || m_captureOwner != owner)
        return;
    m_captureOwner = nullptr;
    grantNextCapture();
}

void HtmlBrowser::cancelCapture(const void* owner)
{
    if (!owner)
        return;
    if (m_captureOwner == owner)
    {
        m_captureOwner = nullptr;
        grantNextCapture();
        return;
    }
    for (int i = m_captureQueue.size() - 1; i >= 0; --i)
        if (m_captureQueue.at(i).owner == owner)
            m_captureQueue.removeAt(i);
}

void HtmlBrowser::grantNextCapture()
{
    if (m_captureOwner != nullptr || m_captureQueue.isEmpty())
        return;
    const CaptureWaiter waiter = m_captureQueue.takeFirst();
    m_captureOwner = waiter.owner;
    qulonglong token = waiter.token;
    CaptureGrant grant = waiter.grant;
    QTimer::singleShot(0, this, [this, owner = waiter.owner, token, grant]() {
        if (m_captureOwner != owner)
            return;
        Q_UNUSED(token);
        grant();
    });
}

void HtmlBrowser::stop()
{
    m_stopping = true;
    m_captureOwner = nullptr;
    m_captureQueue.clear();
    if (m_cdp)
    {
        m_cdp->close();
        delete m_cdp;
        m_cdp = nullptr;
    }
    cleanupProcess();
    m_executable.clear();
    m_version.clear();
    m_wsUrl.clear();
    m_stopping = false;
}

bool HtmlBrowser::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning && m_cdp
           && m_cdp->isConnected();
}

bool HtmlBrowser::available() const
{
    return !detectExecutable(m_requestedExecutable).isEmpty();
}

bool HtmlBrowser::waitForEndpoint(int timeoutMs, QString* error)
{
    static const QRegularExpression endpointPattern(
        QStringLiteral("DevTools listening on (ws://[^\\s]+)"));

    QElapsedTimer timer;
    timer.start();
    QByteArray output;
    while (timer.elapsed() < timeoutMs)
    {
        if (!m_process || m_process->state() == QProcess::NotRunning)
        {
            readProcessOutput(&output);
            if (error)
                *error = QStringLiteral("browser exited before DevTools was ready: %1")
                             .arg(QString::fromUtf8(output.right(2000)).trimmed());
            return false;
        }
        m_process->waitForReadyRead(100);
        readProcessOutput(&output);
        if (output.size() > 256 * 1024)
            output = output.right(64 * 1024);

        const QRegularExpressionMatch match = endpointPattern.match(QString::fromUtf8(output));
        if (match.hasMatch())
        {
            m_wsUrl = match.captured(1).trimmed();
            return true;
        }
    }

    if (error)
        *error = QStringLiteral("timed out waiting for the browser DevTools endpoint");
    return false;
}

void HtmlBrowser::readProcessOutput(QByteArray* buffer)
{
    if (!m_process)
        return;
    buffer->append(m_process->readAllStandardError());
    buffer->append(m_process->readAllStandardOutput());
}

void HtmlBrowser::cleanupProcess()
{
    if (!m_process)
        return;

    if (m_process->state() != QProcess::NotRunning)
    {
#ifdef Q_OS_UNIX
        const qint64 pid = m_process->processId();
        if (pid > 0)
            ::kill(-pid, SIGKILL);
#endif
        m_process->kill();
        if (!m_process->waitForFinished(3000))
        {
            m_process->terminate();
            m_process->waitForFinished(1000);
        }
    }
    delete m_process;
    m_process = nullptr;

    if (m_userDataDir)
    {
        m_userDataDir->remove();
        m_userDataDir.reset();
    }
}

} // namespace html
} // namespace pvf
