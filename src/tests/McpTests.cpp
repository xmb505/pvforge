#include "TestApp.h"
#include "TestSupport.h"
#include "TestRegistry.h"

namespace {

// The small HTTP/MCP helpers live in TestSupport.cpp; tests here call them
// without the pvf::tests qualifier, mirroring the old single-file layout.
using namespace pvf::tests;

int mcpSessionTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "mcp-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString configPath = QDir(temp.path()).filePath(QStringLiteral("mcp.json"));
    {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly))
            return 1;
        config.write("{\"port\":0}");
    }
    pvf::EditorSession::setMcpConfigPathForTest(configPath);
    pvf::EditorSession::setMcpPortOverrideForTest(0);

    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p6-mcp.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P6 MCP");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;

    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "mcp-test: create failed" << error;
        return 1;
    }
    auto* window = new pvf::MainWindow(session.release());
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* server = live ? live->mcpServer() : nullptr;
    if (!check(live && server, QStringLiteral("session has no MCP server")))
        return 1;
    check(server->isRunning(), QStringLiteral("MCP server is not running"));
    check(server->port() > 0, QStringLiteral("MCP port is not assigned"));
    check(!server->token().isEmpty(), QStringLiteral("MCP token is empty"));

    // The token and port live in the persisted config file.
    {
        QFile config(configPath);
        if (config.open(QIODevice::ReadOnly))
        {
            const QJsonObject stored =
                QJsonDocument::fromJson(config.readAll()).object();
            check(stored.value(QStringLiteral("token")).toString()
                      == QString::fromLatin1(server->token()),
                  QStringLiteral("token was not persisted"));
        }
        else
        {
            check(false, QStringLiteral("cannot read MCP config"));
        }
    }

    // tools/list over HTTP with the persisted Bearer token.
    QJsonObject listRequest;
    listRequest.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    listRequest.insert(QStringLiteral("id"), 1);
    listRequest.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
    const McpHttpResponse listResponse = mcpHttpPost(
        server->port(),
        QJsonDocument(listRequest).toJson(QJsonDocument::Compact),
        server->token());
    check(listResponse.status == 200,
          QStringLiteral("tools/list HTTP %1").arg(listResponse.status));
    const QJsonArray tools =
        QJsonDocument::fromJson(listResponse.body)
            .object()
            .value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("tools"))
            .toArray();
    check(tools.size() == 46,
          QStringLiteral("tools/list returned %1 tools, expected 46")
              .arg(tools.size()));

    // Read tools work without Git.
    const McpHttpResponse readResponse =
        mcpCallTool(server->port(), server->token(),
                    QStringLiteral("project.get"), {});
    check(mcpStructuredResult(readResponse)
              .value(QStringLiteral("ok"))
              .toBool(),
          QStringLiteral("project.get failed without Git"));

    // Write tools are denied with permission_denied/git_disabled.
    const McpHttpResponse deniedResponse =
        mcpCallTool(server->port(), server->token(),
                    QStringLiteral("entity.create"),
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("denied")},
                                {QStringLiteral("start"), 0},
                                {QStringLiteral("end"), 30}});
    const QJsonObject denied = mcpStructuredResult(deniedResponse);
    check(!denied.value(QStringLiteral("ok")).toBool(true),
          QStringLiteral("write succeeded with Git disabled"));
    check(denied.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString()
              == QStringLiteral("permission_denied"),
          QStringLiteral("write error is not permission_denied"));
    check(denied.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("details"))
                  .toObject()
                  .value(QStringLiteral("reason"))
                  .toString()
              == QStringLiteral("git_disabled"),
          QStringLiteral("write denial reason is not git_disabled"));

    const bool gitAvailable = pvf::store::ProjectStore::gitBinaryAvailable();
    if (gitAvailable)
    {
        pvf::next::AppSettings appSettings = live->appSettings();
        appSettings.gitEnabled = true;
        appSettings.onboardingDone = true;
        QString applyError;
        check(live->applyAppSettings(appSettings, &applyError),
              QStringLiteral("enabling Git failed: %1").arg(applyError));
        check(live->document()->store()->gitEnabled(),
              QStringLiteral("store did not enable Git"));

        const int beforeEntities = live->project().entities.size();
        const McpHttpResponse writeResponse =
            mcpCallTool(server->port(), server->token(),
                        QStringLiteral("entity.create"),
                        QJsonObject{{QStringLiteral("name"),
                                     QStringLiteral("AI 写入")},
                                    {QStringLiteral("start"), 0},
                                    {QStringLiteral("end"), 30}});
        const QJsonObject written = mcpStructuredResult(writeResponse);
        check(written.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("entity.create failed after enabling Git"));
        check(!written.value(QStringLiteral("commit"))
                   .toObject()
                   .value(QStringLiteral("shortHash"))
                   .toString()
                   .isEmpty(),
              QStringLiteral("write result has no commit hash"));
        QApplication::processEvents();
        check(live->project().entities.size() == beforeEntities + 1,
              QStringLiteral("document did not reload the AI-written entity"));
        check(window->timelineForTest()
                  && window->timelineForTest()->entityRowCountForTest()
                         == live->project().entities.size(),
              QStringLiteral("timeline was not refreshed after the AI write"));
        check(window->mcpLogPanelForTest()
                  && window->mcpLogPanelForTest()->entryCountForTest() > 0,
              QStringLiteral("MCP log has no tool row"));
        if (window->mcpLogPanelForTest())
        {
            const int row =
                window->mcpLogPanelForTest()->entryCountForTest() - 1;
            check(window->mcpLogPanelForTest()->entryDetailForTest(row)
                      .contains(QStringLiteral("提交：")),
                  QStringLiteral("MCP log row does not show the commit"));
        }

        window->showMcpLogForTest();
        QApplication::processEvents();
        QDir().mkpath(QStringLiteral("/tmp/opencode"));
        check(window->grab().save(
                  QStringLiteral("/tmp/opencode/pvforge-p6-mcp.png")),
              QStringLiteral("screenshot failed"));

        // Emergency stop: reads keep working, writes return stopped_by_human.
        server->stopAi();
        QApplication::processEvents();
        check(server->aiStopped(), QStringLiteral("stopAi did not latch"));
        check(window->humanEditingEnabledForTest(),
              QStringLiteral("human editing stayed locked after stop"));
        const QJsonObject stoppedWrite =
            mcpStructuredResult(mcpCallTool(
                server->port(), server->token(),
                QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("stopped")},
                            {QStringLiteral("start"), 0},
                            {QStringLiteral("end"), 10}}));
        check(stoppedWrite.value(QStringLiteral("error"))
                      .toObject()
                      .value(QStringLiteral("code"))
                      .toString()
                  == QStringLiteral("stopped_by_human"),
              QStringLiteral("write after stop is not stopped_by_human"));
        check(mcpStructuredResult(mcpCallTool(
                  server->port(), server->token(),
                  QStringLiteral("project.get"), {}))
                  .value(QStringLiteral("ok"))
                  .toBool(),
              QStringLiteral("read tool stopped working after stop"));

        server->resumeAi();
        QApplication::processEvents();
        const QJsonObject resumedWrite =
            mcpStructuredResult(mcpCallTool(
                server->port(), server->token(),
                QStringLiteral("entity.create"),
                QJsonObject{{QStringLiteral("name"),
                             QStringLiteral("resumed")},
                            {QStringLiteral("start"), 0},
                            {QStringLiteral("end"), 10}}));
        check(resumedWrite.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("write failed after resume"));
    }

    // Restarting the session keeps the token from the config file.
    const QByteArray tokenBefore = server->token();
    window->close();
    QApplication::processEvents();
    delete window;
    QApplication::processEvents();

    auto reopened = pvf::EditorSession::openProject(projectDir, &error, false);
    if (check(reopened != nullptr,
              QStringLiteral("reopen failed: %1").arg(error)))
    {
        check(reopened->mcpServer()
                  && reopened->mcpServer()->token() == tokenBefore,
              QStringLiteral("token changed across restart"));
        check(reopened->mcpServer()
                  && reopened->mcpServer()->isRunning(),
              QStringLiteral("MCP server did not restart"));

        // Switching projects must stop the old listener before the next
        // session binds the same configured port.
        const quint16 oldPort = reopened->mcpServer()->port();
        auto* reopenedWindow = new pvf::MainWindow(reopened.release());
        reopenedWindow->resize(1200, 800);
        reopenedWindow->show();
        QApplication::processEvents();

        const QString secondDir =
            QDir(temp.path()).filePath(QStringLiteral("p6-mcp-second.pvproj"));
        pvf::model::ProjectSettings secondSettings;
        secondSettings.name = QStringLiteral("P6 MCP 2");
        secondSettings.width = 640;
        secondSettings.height = 360;
        secondSettings.fps = 30;
        QString secondError;
        auto secondStore = pvf::store::ProjectStore::create(
            secondDir, secondSettings, &secondError, false);
        check(secondStore.has_value(),
              QStringLiteral("second project create failed: %1")
                  .arg(secondError));
        check(reopenedWindow->openSessionProjectForTest(secondDir, &secondError),
              QStringLiteral("project switch failed: %1").arg(secondError));
        QApplication::processEvents();

        pvf::MainWindow* switchedWindow = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* candidate = qobject_cast<pvf::MainWindow*>(widget);
            if (candidate && candidate != reopenedWindow
                && candidate->editorSessionForTest())
            {
                switchedWindow = candidate;
                break;
            }
        }
        check(switchedWindow != nullptr,
              QStringLiteral("switch did not open a new window"));
        check(switchedWindow && switchedWindow->editorSessionForTest()->mcpServer()
                  && switchedWindow->editorSessionForTest()->mcpServer()->isRunning(),
              QStringLiteral("MCP did not restart after the project switch"));
        check(switchedWindow
                  && switchedWindow->editorSessionForTest()->mcpServer()->port()
                         == oldPort,
              QStringLiteral("MCP port changed after the project switch"));
        if (switchedWindow)
            switchedWindow->close();
        delete reopenedWindow;
        QApplication::processEvents();
    }

    qInfo() << "mcp-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpLockTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (!condition)
        {
            qCritical() << "mcp-lock-test:" << message;
            ++failures;
        }
        return condition;
    };

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString projectDir =
        QDir(temp.path()).filePath(QStringLiteral("p6-lock.pvproj"));
    pvf::model::ProjectSettings settings;
    settings.name = QStringLiteral("P6 锁");
    settings.width = 640;
    settings.height = 360;
    settings.fps = 30;
    QString error;
    auto session =
        pvf::EditorSession::createProject(projectDir, settings, &error, false);
    if (!session)
    {
        qCritical() << "mcp-lock-test: create failed" << error;
        return 1;
    }
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* server = live ? live->mcpServer() : nullptr;
    if (!check(live && server, QStringLiteral("no MCP server")))
        return 1;
    auto* timeline = window->timelineForTest();
    auto* props = window->propertiesForTest();
    auto* library = window->assetLibraryForTest();
    auto* workbench = window->workbenchForTest();
    auto* viewport = window->viewportForTest();
    auto* log = window->mcpLogPanelForTest();
    check(timeline && props && library && workbench && viewport && log,
          QStringLiteral("one or more panels are missing"));
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("human editing is locked before any AI write"));

    // A read-only tool never locks the UI.
    server->toolCallStarted(QStringLiteral("project.get"),
                            QStringLiteral("{}"), false);
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("read-only call locked human editing"));
    check(timeline && timeline->isEnabled(),
          QStringLiteral("read-only call disabled the timeline"));
    server->toolCallFinished(QStringLiteral("project.get"),
                             QStringLiteral("{\"ok\":true}"), 1, true);
    QApplication::processEvents();

    // A write locks every write entry point.
    server->toolCallStarted(QStringLiteral("entity.create"),
                            QStringLiteral("{}"), true);
    QApplication::processEvents();
    check(!window->humanEditingEnabledForTest(),
          QStringLiteral("write did not lock human editing"));
    check(timeline && !timeline->isEnabled(),
          QStringLiteral("timeline is still enabled"));
    check(props && !props->isEnabled(),
          QStringLiteral("properties panel is still enabled"));
    check(library && !library->isEnabled(),
          QStringLiteral("asset bin is still enabled"));
    check(workbench && !workbench->isEnabled(),
          QStringLiteral("workbench is still enabled"));
    check(viewport && !viewport->editingEnabled(),
          QStringLiteral("viewport editing is still enabled"));
    check(log && log->isEnabled(),
          QStringLiteral("MCP log is disabled while AI writes"));
    if (auto* save = window->findChild<QAction*>(QStringLiteral("saveAction")))
        check(!save->isEnabled(), QStringLiteral("save action is enabled"));
    if (auto* settingsAction =
            window->findChild<QAction*>(QStringLiteral("settingsAction")))
        check(!settingsAction->isEnabled(),
              QStringLiteral("settings action is enabled"));

    QString lockError;
    pvf::EditPlan emptyPlan;
    check(!live->editActions()->apply(emptyPlan, &lockError),
          QStringLiteral("EditActions accepted a write while locked"));
    check(lockError.contains(QStringLiteral("AI")),
          QStringLiteral("EditActions lock error is unclear: %1").arg(lockError));
    lockError.clear();
    check(!live->document()->commitProject(QStringLiteral("locked"), &lockError),
          QStringLiteral("Document accepted a write while locked"));
    check(lockError.contains(QStringLiteral("AI")),
          QStringLiteral("Document lock error is unclear: %1").arg(lockError));

    // Emergency stop releases human editing even before the tool unwinds.
    auto* stopButton = log
                           ? log->findChild<QToolButton*>(
                                 QStringLiteral("mcpLogStopButton"))
                           : nullptr;
    auto* resumeButton = log
                             ? log->findChild<QToolButton*>(
                                   QStringLiteral("mcpLogResumeButton"))
                             : nullptr;
    check(stopButton && stopButton->isEnabled(),
          QStringLiteral("MCP log stop button is not enabled"));
    if (stopButton)
        stopButton->click();
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("stop did not release human editing"));
    check(timeline && timeline->isEnabled(),
          QStringLiteral("timeline stayed disabled after stop"));
    check(viewport && viewport->editingEnabled(),
          QStringLiteral("viewport stayed locked after stop"));
    check(resumeButton && resumeButton->isEnabled(),
          QStringLiteral("MCP log resume button is not enabled"));
    server->toolCallFinished(QStringLiteral("entity.create"),
                             QStringLiteral("{\"ok\":false}"), 1, false);
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("tool finish re-locked the stopped editor"));

    if (resumeButton)
        resumeButton->click();
    QApplication::processEvents();
    check(window->humanEditingEnabledForTest(),
          QStringLiteral("resume locked human editing"));
    check(!server->aiStopped(), QStringLiteral("resume did not clear the stop"));

    qInfo() << "mcp-lock-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace pvf::tests {

void registerMcpTests(Registry& registry)
{
    registry.add(QStringLiteral("--mcp-test"),
                 [](QApplication& app) { return mcpSessionTest(app); });
    registry.add(QStringLiteral("--mcp-lock-test"),
                 [](QApplication& app) { return mcpLockTest(app); });
    registry.add(QStringLiteral("--mcp-bridge-test"),
                 [](QApplication&) { return pvf::mcpBridgeTest(); });
}

} // namespace pvf::tests
