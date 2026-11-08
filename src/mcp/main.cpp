#include "mcp/StdioBridge.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

#ifndef PVFORGE_VERSION
#define PVFORGE_VERSION "0.2.0"
#endif

namespace {

struct CliOptions
{
    QString endpointPath;
    QString url;
    QString token;
    bool help = false;
    bool version = false;
    QString error;
};

QString helpText()
{
    return QStringLiteral(
        "pvforge-mcp - PVForge MCP stdio to Streamable HTTP bridge\n"
        "\n"
        "Usage: pvforge-mcp [options]\n"
        "\n"
        "Options:\n"
        "  --endpoint-file <path>  endpoint JSON written by PVForge (alias: --endpoint)\n"
        "  --url <url>             MCP URL; loopback only (http://127.0.0.1:<port>/mcp)\n"
        "  --token <token>         bearer token for --url\n"
        "  -h, --help              show this help\n"
        "  --version               show the bridge version\n"
        "\n"
        "Environment:\n"
        "  PVFORGE_MCP_ENDPOINT    endpoint JSON path\n"
        "  PVFORGE_MCP_URL         MCP URL\n"
        "  PVFORGE_MCP_TOKEN       bearer token\n"
        "\n"
        "Precedence: command line > environment > endpoint file.\n"
        "stdin/stdout use newline-delimited JSON-RPC; logs go to stderr.\n");
}

CliOptions parseArguments(const QStringList& arguments)
{
    CliOptions options;
    for (int index = 1; index < arguments.size(); ++index)
    {
        const QString argument = arguments.at(index);

        const auto valueFor = [&](const QString& option) -> QString {
            if (index + 1 >= arguments.size())
            {
                options.error = QStringLiteral("%1 requires a value").arg(option);
                return QString();
            }
            return arguments.at(++index);
        };

        if (argument == QLatin1String("-h") || argument == QLatin1String("--help"))
        {
            options.help = true;
        }
        else if (argument == QLatin1String("--version"))
        {
            options.version = true;
        }
        else if (argument == QLatin1String("--endpoint-file")
                 || argument == QLatin1String("--endpoint"))
        {
            options.endpointPath = valueFor(argument);
        }
        else if (argument.startsWith(QLatin1String("--endpoint-file=")))
        {
            options.endpointPath = argument.mid(int(std::strlen("--endpoint-file=")));
        }
        else if (argument.startsWith(QLatin1String("--endpoint=")))
        {
            options.endpointPath = argument.mid(int(std::strlen("--endpoint=")));
        }
        else if (argument == QLatin1String("--url"))
        {
            options.url = valueFor(argument);
        }
        else if (argument.startsWith(QLatin1String("--url=")))
        {
            options.url = argument.mid(int(std::strlen("--url=")));
        }
        else if (argument == QLatin1String("--token"))
        {
            options.token = valueFor(argument);
        }
        else if (argument.startsWith(QLatin1String("--token=")))
        {
            options.token = argument.mid(int(std::strlen("--token=")));
        }
        else
        {
            options.error = QStringLiteral("unknown argument: %1").arg(argument);
        }

        if (!options.error.isEmpty())
            return options;
    }
    return options;
}

#if defined(Q_OS_UNIX)
int gSignalWriteFd = -1;

void handleTerminationSignal(int)
{
    const char byte = 1;
    if (gSignalWriteFd >= 0)
    {
        const ssize_t ignored = ::write(gSignalWriteFd, &byte, 1);
        (void)ignored;
    }
}
#endif

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("pvforge-mcp"));
    app.setApplicationVersion(QStringLiteral(PVFORGE_VERSION));
    app.setOrganizationName(QStringLiteral("PVForge"));

    const CliOptions options = parseArguments(app.arguments());
    if (!options.error.isEmpty())
    {
        std::fprintf(stderr, "pvforge-mcp: %s\n\n", qPrintable(options.error));
        std::fputs(qPrintable(helpText()), stderr);
        return 5;
    }
    if (options.help)
    {
        std::fputs(qPrintable(helpText()), stdout);
        return 0;
    }
    if (options.version)
    {
        std::fprintf(stdout, "pvforge-mcp %s\n", PVFORGE_VERSION);
        return 0;
    }

    QString endpointPath = options.endpointPath;
    if (endpointPath.isEmpty())
        endpointPath = qEnvironmentVariable("PVFORGE_MCP_ENDPOINT");
    if (endpointPath.isEmpty())
        endpointPath = pvf::defaultMcpEndpointPath();

    pvf::McpEndpoint endpoint;
    QString resolveError;
    if (!pvf::resolveMcpEndpoint(endpointPath, options.url, options.token.toUtf8(),
                                 qEnvironmentVariable("PVFORGE_MCP_URL"),
                                 qgetenv("PVFORGE_MCP_TOKEN"), &endpoint,
                                 &resolveError))
    {
        std::fprintf(stderr, "pvforge-mcp: %s\n", qPrintable(resolveError));
        return 2;
    }

    pvf::McpStdioBridge bridge(endpoint);

    QObject::connect(&bridge, &pvf::McpStdioBridge::messageReady,
                     [](const QByteArray& message) {
                         std::fwrite(message.constData(), 1, size_t(message.size()), stdout);
                         std::fputc('\n', stdout);
                         std::fflush(stdout);
                     });
    QObject::connect(&bridge, &pvf::McpStdioBridge::diagnostic, [](const QString& message) {
        std::fprintf(stderr, "pvforge-mcp: %s\n", qPrintable(message));
        std::fflush(stderr);
    });

#if defined(Q_OS_UNIX)
    std::signal(SIGPIPE, SIG_IGN);

    int signalPipe[2] = {-1, -1};
    if (::pipe(signalPipe) == 0)
    {
        gSignalWriteFd = signalPipe[1];
        auto* signalNotifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(signalNotifier, &QSocketNotifier::activated, &app, [&] {
            char buffer[16];
            const ssize_t ignored = ::read(signalPipe[0], buffer, sizeof buffer);
            (void)ignored;
            bridge.abortAll();
            QCoreApplication::quit();
        });
        std::signal(SIGINT, handleTerminationSignal);
        std::signal(SIGTERM, handleTerminationSignal);
    }

    bool eof = false;
    bool quitting = false;
    const auto quitWhenIdle = [&] {
        if (eof && bridge.inFlight() == 0 && !quitting)
        {
            quitting = true;
            QCoreApplication::quit();
        }
    };
    QObject::connect(&bridge, &pvf::McpStdioBridge::idle, &app, quitWhenIdle);

    QByteArray pending;
    const auto processLines = [&] {
        int newline = -1;
        while ((newline = pending.indexOf('\n')) >= 0)
        {
            QByteArray line = pending.left(newline);
            pending.remove(0, newline + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (!line.trimmed().isEmpty())
                bridge.submitLine(line);
        }
    };

    auto* stdinNotifier =
        new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QObject::connect(stdinNotifier, &QSocketNotifier::activated, &app, [&] {
        char buffer[64 * 1024];
        ssize_t count = -1;
        do
        {
            count = ::read(STDIN_FILENO, buffer, sizeof buffer);
        } while (count < 0 && errno == EINTR);

        if (count > 0)
        {
            pending.append(buffer, int(count));
            processLines();
            return;
        }
        if (count == 0)
        {
            processLines();
            if (!pending.trimmed().isEmpty())
                bridge.submitLine(pending);
            pending.clear();
            eof = true;
            stdinNotifier->setEnabled(false);
            quitWhenIdle();
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        std::fprintf(stderr, "pvforge-mcp: cannot read stdin: %s\n",
                     std::strerror(errno));
        eof = true;
        stdinNotifier->setEnabled(false);
        quitWhenIdle();
    });

    const int result = app.exec();
    bridge.abortAll();
    return result;
#else
    std::fprintf(stderr, "pvforge-mcp: the stdio bridge currently requires a POSIX "
                         "platform\n");
    return 4;
#endif
}
