#include "LegacyTestSupport.h"
#include "McpWriteTests.h"
#include "TestRegistry.h"

namespace {

// The small HTTP/MCP helpers live in TestSupport.cpp; tests here call them
// without the pvf::tests qualifier, mirroring the old single-file layout.
using namespace pvf::tests;

int mcpReadonlyTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-readonly-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP 测试工程");
    project->width = 1280;
    project->height = 720;
    project->fps = 24;
    pvf::Entity* folder = project->addFolder(QStringLiteral("组"));
    pvf::Entity* child = project->addVector(0, 100, 100, Qt::red, Qt::white, 0);
    child->name = QStringLiteral("移动矩形");
    const pvf::EntityId childId = child->id;
    project->setEntityParent(childId, folder->id);
    project->setValue(childId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(childId, QStringLiteral("x"), 48, 480.0, true);
    pvf::Entity* camera = project->addCamera(QStringLiteral("主摄像机"));
    project->activeCamera = camera->id;
    const int entityCount = project->entities().size();

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/mcp-endpoint.json"));

    int signalled = 0;
    int signalledFailures = 0;
    QString lastSignalName;
    QString lastArgsJson;
    QString lastResultJson;
    qint64 lastDuration = -1;
    bool lastSignalOk = false;
    QObject::connect(
        &server, &pvf::McpServer::toolInvoked,
        [&](const QString& name, const QString& argsJson, const QString& resultJson,
            qint64 durationMs, bool ok) {
            ++signalled;
            if (!ok)
                ++signalledFailures;
            lastSignalName = name;
            lastArgsJson = argsJson;
            lastResultJson = resultJson;
            lastDuration = durationMs;
            lastSignalOk = ok;
        });

    QString error;
    const quint16 startedPort = server.start(0, &error);
    if (startedPort == 0)
    {
        qCritical() << "mcp-readonly-test: start failed" << error;
        delete project;
        return 1;
    }
    failIf(server.port() != startedPort,
           QStringLiteral("start() port and port() disagree"));
    failIf(server.port() == 0, QStringLiteral("start(0) did not report a port"));
    failIf(!server.isRunning(), QStringLiteral("server is not running after start"));
    failIf(server.endpointUrl().isEmpty(), QStringLiteral("endpoint URL is empty"));

    const quint16 port = startedPort;
    const QByteArray token = server.token();
    failIf(token.isEmpty(), QStringLiteral("token is empty"));

    // initialize
    QJsonObject initParams;
    initParams[QStringLiteral("protocolVersion")] = QStringLiteral("2025-06-18");
    initParams[QStringLiteral("capabilities")] = QJsonObject();
    initParams[QStringLiteral("clientInfo")] =
        QJsonObject{{QStringLiteral("name"), QStringLiteral("test-client")},
                    {QStringLiteral("version"), QStringLiteral("0.0")}};
    QJsonObject initRequest;
    initRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    initRequest[QStringLiteral("id")] = 1;
    initRequest[QStringLiteral("method")] = QStringLiteral("initialize");
    initRequest[QStringLiteral("params")] = initParams;

    const McpHttpResponse initHttp = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact), token);
    failIf(initHttp.status != 200,
           QStringLiteral("initialize HTTP status %1").arg(initHttp.status));
    failIf(!initHttp.headers.toLower().contains(
               QByteArrayLiteral("content-type: application/json")),
           QStringLiteral("initialize response is not application/json"));
    const QJsonObject initResult =
        QJsonDocument::fromJson(initHttp.body).object().value(QStringLiteral("result"))
            .toObject();
    failIf(initResult.value(QStringLiteral("protocolVersion")).toString().isEmpty(),
           QStringLiteral("initialize did not return protocolVersion"));
    const QJsonObject capabilities =
        initResult.value(QStringLiteral("capabilities")).toObject();
    failIf(!capabilities.contains(QStringLiteral("tools")),
           QStringLiteral("initialize missing tools capability"));
    failIf(!capabilities.contains(QStringLiteral("resources")),
           QStringLiteral("initialize missing resources capability"));
    failIf(initResult.value(QStringLiteral("serverInfo"))
                   .toObject()
                   .value(QStringLiteral("name"))
                   .toString()
               != QStringLiteral("PVForge"),
           QStringLiteral("initialize serverInfo.name mismatch"));

    // tools/list
    QJsonObject listRequest;
    listRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    listRequest[QStringLiteral("id")] = 2;
    listRequest[QStringLiteral("method")] = QStringLiteral("tools/list");
    const QJsonObject listResponse = mcpRpc(port, token, listRequest);
    const QJsonArray tools =
        listResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("tools"))
            .toArray();
    QStringList toolNames;
    for (const QJsonValue& value : tools)
        toolNames.append(value.toObject().value(QStringLiteral("name")).toString());
    for (const QString& required :
         {QStringLiteral("project.summary"), QStringLiteral("timeline.get"),
          QStringLiteral("entity.get"), QStringLiteral("assets.list"),
          QStringLiteral("guide.get")})
        failIf(!toolNames.contains(required),
               QStringLiteral("tools/list missing %1").arg(required));
    failIf(toolNames.contains(QStringLiteral("entity.create")),
           QStringLiteral("tools/list advertises a write tool"));

    const auto callTool = [&](int id, const QString& name, const QJsonObject& args,
                              int* statusOut = nullptr) {
        QJsonObject params;
        params[QStringLiteral("name")] = name;
        params[QStringLiteral("arguments")] = args;
        QJsonObject request;
        request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        request[QStringLiteral("id")] = id;
        request[QStringLiteral("method")] = QStringLiteral("tools/call");
        request[QStringLiteral("params")] = params;
        return mcpRpc(port, token, request, statusOut);
    };

    // project.summary
    int status = 0;
    const QJsonObject summaryResponse =
        callTool(3, QStringLiteral("project.summary"), QJsonObject(), &status);
    failIf(status != 200, QStringLiteral("project.summary HTTP %1").arg(status));
    const QJsonObject summaryResult =
        summaryResponse.value(QStringLiteral("result")).toObject();
    failIf(summaryResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("project.summary returned isError"));
    const QJsonObject summaryStructured =
        summaryResult.value(QStringLiteral("structuredContent")).toObject();
    failIf(!summaryStructured.value(QStringLiteral("ok")).toBool(),
           QStringLiteral("project.summary structured ok is false"));
    const QJsonObject summaryData =
        summaryStructured.value(QStringLiteral("data")).toObject();
    failIf(summaryData.value(QStringLiteral("width")).toInt() != 1280,
           QStringLiteral("project.summary width mismatch"));
    failIf(summaryData.value(QStringLiteral("height")).toInt() != 720,
           QStringLiteral("project.summary height mismatch"));
    failIf(summaryData.value(QStringLiteral("fps")).toInt() != 24,
           QStringLiteral("project.summary fps mismatch"));
    failIf(summaryData.value(QStringLiteral("durationFrames")).toInt()
               != project->timelineLength(),
           QStringLiteral("project.summary duration mismatch"));
    failIf(summaryData.value(QStringLiteral("activeCamera"))
                   .toObject()
                   .value(QStringLiteral("id"))
                   .toInt()
               != camera->id,
           QStringLiteral("project.summary active camera mismatch"));
    failIf(summaryData.value(QStringLiteral("scenesAvailable")).toBool(),
           QStringLiteral("project.summary pretends scenes exist"));
    failIf(summaryResult.value(QStringLiteral("content")).toArray().isEmpty(),
           QStringLiteral("project.summary content is empty"));

    // timeline.get
    QJsonObject timelineArgs;
    timelineArgs[QStringLiteral("limit")] = 10;
    const QJsonObject timelineResponse =
        callTool(4, QStringLiteral("timeline.get"), timelineArgs);
    const QJsonObject timelineStructured =
        timelineResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject timelineData =
        timelineStructured.value(QStringLiteral("data")).toObject();
    const QJsonArray timelineEntities =
        timelineData.value(QStringLiteral("entities")).toArray();
    failIf(timelineEntities.size() != entityCount,
           QStringLiteral("timeline.get entity count %1 != %2")
               .arg(timelineEntities.size())
               .arg(entityCount));
    failIf(timelineStructured.value(QStringLiteral("page"))
                   .toObject()
                   .value(QStringLiteral("total"))
                   .toInt()
               != entityCount,
           QStringLiteral("timeline.get page total mismatch"));
    if (!timelineEntities.isEmpty())
    {
        const QJsonObject first = timelineEntities.first().toObject();
        failIf(!first.contains(QStringLiteral("tracks")),
               QStringLiteral("timeline.get omitted tracks"));
        failIf(!first.contains(QStringLiteral("clipStart"))
                   || !first.contains(QStringLiteral("clipEnd"))
                   || !first.contains(QStringLiteral("row")),
               QStringLiteral("timeline.get omitted clip/row fields"));
    }

    bool foundChild = false;
    for (const QJsonValue& value : timelineEntities)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("id")).toInt() != childId)
            continue;
        foundChild = true;
        const QJsonObject tracks = object.value(QStringLiteral("tracks")).toObject();
        failIf(tracks.value(QStringLiteral("x")).toArray().size() != 2,
               QStringLiteral("timeline.get x track key count mismatch"));
    }
    failIf(!foundChild, QStringLiteral("timeline.get did not include the child"));

    // entity.get
    QJsonObject entityArgs;
    entityArgs[QStringLiteral("id")] = childId;
    entityArgs[QStringLiteral("frame")] = 24;
    const QJsonObject entityResponse =
        callTool(5, QStringLiteral("entity.get"), entityArgs);
    const QJsonObject entityStructured =
        entityResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject();
    const QJsonObject entityData =
        entityStructured.value(QStringLiteral("data")).toObject();
    failIf(entityData.value(QStringLiteral("name")).toString()
               != QStringLiteral("移动矩形"),
           QStringLiteral("entity.get name mismatch"));
    failIf(entityData.value(QStringLiteral("tracks"))
                   .toObject()
                   .value(QStringLiteral("x"))
                   .toArray()
                   .size()
               != 2,
           QStringLiteral("entity.get track mismatch"));
    failIf(std::abs(entityData.value(QStringLiteral("evaluated"))
                            .toObject()
                            .value(QStringLiteral("x"))
                            .toDouble()
                        - 240.0)
               > 1e-6,
           QStringLiteral("entity.get x at frame 24 is not 240"));

    // Write tools are rejected, and nothing changes.
    const QJsonObject writeResponse = callTool(
        6, QStringLiteral("entity.create"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
    const QJsonObject writeResult =
        writeResponse.value(QStringLiteral("result")).toObject();
    failIf(!writeResult.value(QStringLiteral("isError")).toBool(),
           QStringLiteral("entity.create was not rejected"));
    failIf(writeResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("permission_denied"),
           QStringLiteral("entity.create did not return permission_denied"));
    failIf(project->entities().size() != entityCount,
           QStringLiteral("rejected write tool changed the project"));

    // Missing required id -> invalid_argument.
    const QJsonObject invalidResponse =
        callTool(7, QStringLiteral("entity.get"), QJsonObject());
    const QJsonObject invalidResult =
        invalidResponse.value(QStringLiteral("result")).toObject();
    failIf(invalidResult.value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("invalid_argument"),
           QStringLiteral("entity.get without id did not return invalid_argument"));

    // Deferred scene tools -> not_available.
    const QJsonObject sceneResponse =
        callTool(8, QStringLiteral("scene.list"), QJsonObject());
    failIf(sceneResponse.value(QStringLiteral("result"))
                   .toObject()
                   .value(QStringLiteral("structuredContent"))
                   .toObject()
                   .value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toString()
               != QStringLiteral("not_available"),
           QStringLiteral("scene.list did not return not_available"));

    // guide.get
    const QJsonObject guideResponse =
        callTool(9, QStringLiteral("guide.get"), QJsonObject());
    const QString guideText =
        guideResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("structuredContent"))
            .toObject()
            .value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("markdown"))
            .toString();
    failIf(guideText.isEmpty(), QStringLiteral("guide.get returned empty text"));

    // resources/list + resources/read
    QJsonObject resourcesRequest;
    resourcesRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    resourcesRequest[QStringLiteral("id")] = 10;
    resourcesRequest[QStringLiteral("method")] = QStringLiteral("resources/list");
    const QJsonObject resourcesResponse = mcpRpc(port, token, resourcesRequest);
    const QJsonArray resources =
        resourcesResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("resources"))
            .toArray();
    QStringList resourceUris;
    for (const QJsonValue& value : resources)
        resourceUris.append(value.toObject().value(QStringLiteral("uri")).toString());
    failIf(!resourceUris.contains(QStringLiteral("pvforge://guide/authoring")),
           QStringLiteral("resources/list missing authoring guide"));
    failIf(!resourceUris.contains(QStringLiteral("pvforge://project/schema")),
           QStringLiteral("resources/list missing project schema"));

    QJsonObject readParams;
    readParams[QStringLiteral("uri")] = QStringLiteral("pvforge://guide/authoring");
    QJsonObject readRequest;
    readRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    readRequest[QStringLiteral("id")] = 11;
    readRequest[QStringLiteral("method")] = QStringLiteral("resources/read");
    readRequest[QStringLiteral("params")] = readParams;
    const QJsonObject readResponse = mcpRpc(port, token, readRequest);
    const QJsonArray contents =
        readResponse.value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("contents"))
            .toArray();
    failIf(contents.isEmpty()
               || contents.first()
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString()
                      .isEmpty(),
           QStringLiteral("resources/read returned no text"));

    // ping
    QJsonObject pingRequest;
    pingRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    pingRequest[QStringLiteral("id")] = 12;
    pingRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const QJsonObject pingResponse = mcpRpc(port, token, pingRequest);
    failIf(!pingResponse.value(QStringLiteral("result")).isObject(),
           QStringLiteral("ping did not return an object result"));
    failIf(!pingResponse.value(QStringLiteral("result")).toObject().isEmpty(),
           QStringLiteral("ping result is not empty"));

    // Notifications are accepted with 202 and no body.
    QJsonObject noteRequest;
    noteRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    noteRequest[QStringLiteral("method")] = QStringLiteral("notifications/cancelled");
    noteRequest[QStringLiteral("params")] =
        QJsonObject{{QStringLiteral("requestId"), 99},
                    {QStringLiteral("reason"), QStringLiteral("test")}};
    const McpHttpResponse noteResponse = mcpHttpPost(
        port, QJsonDocument(noteRequest).toJson(QJsonDocument::Compact), token);
    failIf(noteResponse.status != 202,
           QStringLiteral("notification HTTP %1 != 202").arg(noteResponse.status));
    failIf(!noteResponse.body.isEmpty(),
           QStringLiteral("notification returned a body"));

    // Authentication and transport rejection.
    const McpHttpResponse badToken = mcpHttpPost(
        port, QJsonDocument(initRequest).toJson(QJsonDocument::Compact),
        QByteArrayLiteral("wrong-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(!badToken.headers.toLower().contains(QByteArrayLiteral("www-authenticate")),
           QStringLiteral("401 response lacks WWW-Authenticate"));

    QJsonObject simpleRequest;
    simpleRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    simpleRequest[QStringLiteral("id")] = 13;
    simpleRequest[QStringLiteral("method")] = QStringLiteral("ping");
    const McpHttpResponse wrongType = mcpHttpPost(
        port, QJsonDocument(simpleRequest).toJson(QJsonDocument::Compact), token,
        QByteArrayLiteral("text/plain"));
    failIf(wrongType.status != 415,
           QStringLiteral("wrong content type HTTP %1 != 415").arg(wrongType.status));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, QByteArrayLiteral("{}"), token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    const McpHttpResponse malformed =
        mcpHttpPost(port, QByteArrayLiteral("{"), token);
    failIf(malformed.status != 200,
           QStringLiteral("malformed JSON HTTP %1 != 200").arg(malformed.status));
    const QJsonObject malformedResponse =
        QJsonDocument::fromJson(malformed.body).object();
    failIf(malformedResponse.value(QStringLiteral("error"))
                   .toObject()
                   .value(QStringLiteral("code"))
                   .toInt()
               != -32700,
           QStringLiteral("malformed JSON did not return -32700"));

    // Every tools/call emitted one log signal with the required fields.
    failIf(signalled < 7,
           QStringLiteral("toolInvoked emitted %1 times, expected at least 7")
               .arg(signalled));
    failIf(signalledFailures < 3,
           QStringLiteral("toolInvoked did not report the rejected calls"));
    failIf(lastSignalName != QStringLiteral("guide.get"),
           QStringLiteral("toolInvoked last name is %1").arg(lastSignalName));
    failIf(lastArgsJson.isEmpty() || lastResultJson.isEmpty(),
           QStringLiteral("toolInvoked JSON payloads are empty"));
    failIf(lastDuration < 0, QStringLiteral("toolInvoked duration is negative"));
    failIf(!lastSignalOk, QStringLiteral("toolInvoked last call is not ok"));

    server.stop();
    failIf(server.isRunning(), QStringLiteral("server still running after stop"));
    failIf(server.port() != 0, QStringLiteral("port not cleared after stop"));

    delete project;
    qInfo() << "mcp-readonly-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpHttpTest()
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-http-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->name = QStringLiteral("MCP HTTP 测试工程");
    project->width = 640;
    project->height = 360;
    project->fps = 24;

    pvf::McpServer server(project);
    server.setToken(QByteArrayLiteral("http-test-token"));
    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-http-endpoint-XXXXXX"));
    server.setEndpointFilePath(endpointTemp.path()
                               + QStringLiteral("/server.json"));

    const QByteArray pingBody =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");

    QString error;
    const quint16 port = server.start(0, &error);
    if (port == 0)
    {
        qCritical() << "mcp-http-test: start failed" << error;
        delete project;
        return 1;
    }

    const QByteArray token = server.token();
    const QByteArray host =
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(port);
    const QByteArray localhostHost =
        QByteArrayLiteral("localhost:") + QByteArray::number(port);
    const QByteArray allowedOrigin = QByteArrayLiteral("http://") + host;

    failIf(server.endpointUrl().contains(QString::fromLatin1(token)),
           QStringLiteral("endpointUrl leaks the token"));

    // Correct Host + Bearer token works and never grants CORS.
    const McpHttpResponse normal = mcpHttpPost(port, pingBody, token);
    failIf(normal.status != 200,
           QStringLiteral("normal request HTTP %1 != 200").arg(normal.status));
    failIf(normal.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("normal response contains Access-Control-Allow-Origin"));

    const McpHttpResponse localhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost);
    failIf(localhost.status != 200,
           QStringLiteral("localhost Host HTTP %1 != 200").arg(localhost.status));

    const McpHttpResponse upperLocalhost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), localhostHost.toUpper());
    failIf(upperLocalhost.status != 200,
           QStringLiteral("uppercase localhost Host HTTP %1 != 200")
               .arg(upperLocalhost.status));

    // DNS rebinding: foreign or mismatched Host is rejected.
    const McpHttpResponse evilHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(), QByteArrayLiteral("evil.com"));
    failIf(evilHost.status != 403,
           QStringLiteral("evil Host HTTP %1 != 403").arg(evilHost.status));
    failIf(evilHost.body.contains(token),
           QStringLiteral("rejected Host response leaks the token"));
    failIf(evilHost.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Host response echoes the Host header"));

    const quint16 otherPort = port == 65535 ? quint16(port - 1)
                                            : quint16(port + 1);
    const McpHttpResponse wrongPortHost = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArray(),
        QByteArrayLiteral("127.0.0.1:") + QByteArray::number(otherPort));
    failIf(wrongPortHost.status != 403,
           QStringLiteral("mismatched Host port HTTP %1 != 403")
               .arg(wrongPortHost.status));

    const QByteArray noHost =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nAuthorization: Bearer ")
        + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: ")
        + QByteArray::number(pingBody.size()) + QByteArrayLiteral("\r\n\r\n")
        + pingBody;
    failIf(mcpExchange(port, noHost).status != 403,
           QStringLiteral("missing Host was not rejected with 403"));

    // Origin rules: foreign and opaque origins are rejected, loopback is
    // accepted, and clients without Origin (non-browser) keep working.
    const McpHttpResponse evilOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://evil.com\r\n"));
    failIf(evilOrigin.status != 403,
           QStringLiteral("evil Origin HTTP %1 != 403").arg(evilOrigin.status));
    failIf(evilOrigin.body.contains(QByteArrayLiteral("evil.com")),
           QStringLiteral("rejected Origin response echoes the Origin header"));
    failIf(evilOrigin.body.contains(token),
           QStringLiteral("rejected Origin response leaks the token"));

    const McpHttpResponse nullOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: null\r\n"));
    failIf(nullOrigin.status != 403,
           QStringLiteral("Origin: null HTTP %1 != 403").arg(nullOrigin.status));

    const McpHttpResponse httpsOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: https://127.0.0.1:") + QByteArray::number(port)
            + QByteArrayLiteral("\r\n"));
    failIf(httpsOrigin.status != 403,
           QStringLiteral("https Origin HTTP %1 != 403").arg(httpsOrigin.status));

    const McpHttpResponse loopbackOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: ") + allowedOrigin + QByteArrayLiteral("\r\n"));
    failIf(loopbackOrigin.status != 200,
           QStringLiteral("loopback Origin HTTP %1 != 200")
               .arg(loopbackOrigin.status));

    const McpHttpResponse localhostOrigin = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Origin: http://") + localhostHost
            + QByteArrayLiteral("\r\n"));
    failIf(localhostOrigin.status != 200,
           QStringLiteral("localhost Origin HTTP %1 != 200")
               .arg(localhostOrigin.status));

    const McpHttpResponse crossSite = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"), -1,
        QByteArrayLiteral("Sec-Fetch-Site: cross-site\r\n"));
    failIf(crossSite.status != 403,
           QStringLiteral("cross-site Sec-Fetch-Site HTTP %1 != 403")
               .arg(crossSite.status));

    // Preflight is refused and never answered with CORS headers.
    const QByteArray optionsRequest =
        QByteArrayLiteral("OPTIONS /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAccess-Control-Request-Method: POST\r\n"
                            "Content-Length: 0\r\n\r\n");
    const McpHttpResponse options = mcpExchange(port, optionsRequest);
    failIf(options.status != 405,
           QStringLiteral("OPTIONS HTTP %1 != 405").arg(options.status));
    failIf(options.headers.toLower().contains(
               QByteArrayLiteral("access-control-allow-origin")),
           QStringLiteral("OPTIONS response contains Access-Control-Allow-Origin"));

    // Request framing and size limits.
    const QByteArray oversizedHeader =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nX-Filler: ") + QByteArray(17000, 'a')
        + QByteArrayLiteral("\r\n\r\n");
    const McpHttpResponse bigHeader = mcpExchange(port, oversizedHeader);
    failIf(bigHeader.status != 431,
           QStringLiteral("oversized header HTTP %1 != 431")
               .arg(bigHeader.status));

    const QByteArray duplicateLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}");
    const McpHttpResponse doubleLength = mcpExchange(port, duplicateLength);
    failIf(doubleLength.status != 400,
           QStringLiteral("duplicate Content-Length HTTP %1 != 400")
               .arg(doubleLength.status));

    const QByteArray bothFramings =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Content-Length: 2\r\n\r\n{}");
    const McpHttpResponse smuggling = mcpExchange(port, bothFramings);
    failIf(smuggling.status != 400,
           QStringLiteral("Content-Length + Transfer-Encoding HTTP %1 != 400")
               .arg(smuggling.status));

    const QByteArray chunkedOnly =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    failIf(mcpExchange(port, chunkedOnly).status != 400,
           QStringLiteral("chunked body was not rejected with 400"));

    const QByteArray negativeLength =
        QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + host
        + QByteArrayLiteral("\r\nAuthorization: Bearer ") + token
        + QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                            "Content-Length: -1\r\n\r\n");
    failIf(mcpExchange(port, negativeLength).status != 400,
           QStringLiteral("negative Content-Length was not rejected with 400"));

    const McpHttpResponse tooLarge = mcpHttpPost(
        port, pingBody, token, QByteArrayLiteral("application/json"),
        server.maxBodyBytes() + 1);
    failIf(tooLarge.status != 413,
           QStringLiteral("oversized body HTTP %1 != 413").arg(tooLarge.status));

    // Authentication failures never echo either token.
    const McpHttpResponse badToken =
        mcpHttpPost(port, pingBody, QByteArrayLiteral("wrong-secret-token"));
    failIf(badToken.status != 401,
           QStringLiteral("bad token HTTP %1 != 401").arg(badToken.status));
    failIf(badToken.body.contains(QByteArrayLiteral("wrong-secret-token")),
           QStringLiteral("401 response echoes the submitted token"));
    failIf(badToken.body.contains(token),
           QStringLiteral("401 response leaks the configured token"));

    // An empty token must never leave the server unauthenticated.
    pvf::McpServer generated(project);
    generated.setToken(QByteArray());
    failIf(generated.token().isEmpty(),
           QStringLiteral("empty token was not replaced with a random token"));
    failIf(generated.token().size() < 32,
           QStringLiteral("generated token is too short"));

    // Connection cap: the third concurrent connection is refused.
    pvf::McpServer limited(project);
    limited.setToken(QByteArrayLiteral("limit-token"));
    limited.setEndpointFilePath(endpointTemp.path()
                                + QStringLiteral("/limited.json"));
    pvf::McpServer::HttpLimits limits = limited.httpLimits();
    limits.maxConnections = 2;
    limits.headerTimeoutMs = 5000;
    limits.requestTimeoutMs = 5000;
    limited.setHttpLimits(limits);
    const quint16 limitedPort = limited.start(0, &error);
    failIf(limitedPort == 0,
           QStringLiteral("limited server failed to start: %1").arg(error));
    if (limitedPort != 0)
    {
        QTcpSocket first;
        QTcpSocket second;
        first.connectToHost(QHostAddress::LocalHost, limitedPort);
        second.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&limited] { return limited.activeConnectionCount() == 2; },
                   3000),
               QStringLiteral("connection limit server did not accept two peers"));

        QTcpSocket third;
        third.connectToHost(QHostAddress::LocalHost, limitedPort);
        failIf(!mcpWaitUntil(
                   [&third] {
                       return third.bytesAvailable() > 0
                              || third.state() == QAbstractSocket::UnconnectedState;
                   },
                   3000),
               QStringLiteral("third connection was neither refused nor answered"));
        const int thirdStatus = mcpStatusFromRaw(third.readAll());
        failIf(thirdStatus != 503,
               QStringLiteral("third connection HTTP %1 != 503")
                   .arg(thirdStatus));

        first.abort();
        second.abort();
        third.abort();
        limited.stop();
        failIf(limited.activeConnectionCount() != 0,
               QStringLiteral("connection counter is not empty after stop"));
    }

    // Header and body deadlines are configurable so the test stays fast.
    pvf::McpServer slow(project);
    slow.setToken(QByteArrayLiteral("slow-token"));
    slow.setEndpointFilePath(endpointTemp.path() + QStringLiteral("/slow.json"));
    pvf::McpServer::HttpLimits slowLimits = slow.httpLimits();
    slowLimits.headerTimeoutMs = 150;
    slowLimits.requestTimeoutMs = 150;
    slow.setHttpLimits(slowLimits);
    const quint16 slowPort = slow.start(0, &error);
    failIf(slowPort == 0,
           QStringLiteral("timeout server failed to start: %1").arg(error));
    if (slowPort != 0)
    {
        const QByteArray slowHost =
            QByteArrayLiteral("127.0.0.1:") + QByteArray::number(slowPort);
        const QByteArray partialHeader =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n");
        const McpHttpResponse headerTimeout =
            mcpExchange(slowPort, partialHeader, 3000);
        failIf(headerTimeout.timedOut,
               QStringLiteral("client timed out before the header deadline"));
        failIf(headerTimeout.status != 408,
               QStringLiteral("header timeout HTTP %1 != 408")
                   .arg(headerTimeout.status));

        const QByteArray partialBody =
            QByteArrayLiteral("POST /mcp HTTP/1.1\r\nHost: ") + slowHost
            + QByteArrayLiteral("\r\nAuthorization: Bearer slow-token\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: 100\r\n\r\n");
        const McpHttpResponse bodyTimeout =
            mcpExchange(slowPort, partialBody, 3000);
        failIf(bodyTimeout.timedOut,
               QStringLiteral("client timed out before the body deadline"));
        failIf(bodyTimeout.status != 408,
               QStringLiteral("body timeout HTTP %1 != 408")
                   .arg(bodyTimeout.status));

        slow.stop();
    }

    server.stop();
    delete project;
    qInfo() << "mcp-http-test failures" << failures;
    return failures == 0 ? 0 : 1;
}

int mcpUiTest(QApplication& app)
{
    int failures = 0;
    const auto fail = [&failures](const QString& message) {
        qCritical() << "mcp-ui-test:" << message;
        ++failures;
    };
    const auto failIf = [&fail](bool condition, const QString& message) {
        if (condition)
            fail(message);
    };

    auto* project = new pvf::Project;
    project->fps = 30;
    pvf::Entity* entity =
        project->addVector(0, 100, 80, Qt::red, Qt::white, 0);
    entity->name = QStringLiteral("MCP 测试矩形");
    const pvf::EntityId entityId = entity->id;
    project->setValue(entityId, QStringLiteral("x"), 0, 0.0, true);
    project->setValue(entityId, QStringLiteral("x"), 30, 300.0, true);

    pvf::MainWindow window(project);
    window.resize(1200, 800);
    window.show();

    QTemporaryDir endpointTemp(QDir::tempPath()
                               + QStringLiteral("/pvforge-mcp-ui-endpoint-XXXXXX"));
    window.mcpServerForTest()->setEndpointFilePath(
        endpointTemp.path() + QStringLiteral("/mcp-endpoint.json"));

    QTimer::singleShot(
        300, &window, [&app, &window, &failures, entityId, fail, failIf] {
            auto* server = window.mcpServerForTest();
            auto* panel = window.mcpLogPanelForTest();
            auto* statusButton =
                window.findChild<QToolButton*>(QStringLiteral("mcpStatusButton"));
            if (!server || !panel || !statusButton)
            {
                qCritical() << "mcp-ui-test: server/log/status missing" << server
                            << panel << statusButton;
                app.exit(1);
                return;
            }

            auto treeText = [panel] {
                QString text;
                auto* tree =
                    panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
                if (!tree)
                    return text;
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                {
                    QTreeWidgetItem* item = tree->topLevelItem(i);
                    for (int column = 0; column < item->columnCount(); ++column)
                        text += item->text(column) + QLatin1Char('\n');
                    for (int childIndex = 0; childIndex < item->childCount();
                         ++childIndex)
                        for (int column = 0;
                             column < item->child(childIndex)->columnCount();
                             ++column)
                            text += item->child(childIndex)->text(column)
                                    + QLatin1Char('\n');
                }
                return text;
            };

            failIf(server->isRunning(), QStringLiteral("server auto-started"));
            failIf(panel->isVisible(),
                   QStringLiteral("log visible before MCP was enabled"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry is hidden at startup"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry has no MCP text"));

            auto* dialog = window.openSettingsDialogForTest();
            if (!dialog)
            {
                fail(QStringLiteral("settings dialog was not created"));
                app.exit(1);
                return;
            }
            QApplication::processEvents();

            auto* categories =
                dialog->findChild<QListWidget*>(QStringLiteral("settingsCategories"));
            bool hasMcpCategory = false;
            if (categories)
            {
                for (int i = 0; i < categories->count(); ++i)
                    if (categories->item(i)->text() == QStringLiteral("MCP"))
                    {
                        hasMcpCategory = true;
                        categories->setCurrentRow(i);
                    }
            }
            failIf(!hasMcpCategory, QStringLiteral("settings has no MCP page"));

            auto* enabledCheck =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpEnableCheck"));
            auto* autoPort =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpAutoPort"));
            auto* portSpin =
                dialog->findChild<QSpinBox*>(QStringLiteral("mcpPortSpin"));
            auto* tokenEdit =
                dialog->findChild<QLineEdit*>(QStringLiteral("mcpTokenEdit"));
            auto* statusLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpStatusLabel"));
            auto* errorLabel =
                dialog->findChild<QLabel*>(QStringLiteral("mcpErrorLabel"));
            auto* startButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStartButton"));
            auto* stopButton =
                dialog->findChild<QPushButton*>(QStringLiteral("mcpStopButton"));
            auto* logToggle =
                dialog->findChild<QCheckBox*>(QStringLiteral("mcpLogEnabled"));
            auto* permissionCombo =
                dialog->findChild<QComboBox*>(QStringLiteral("mcpPermissionCombo"));
            auto* copyConfig = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpCopyConfigButton"));
            auto* resetToken = dialog->findChild<QPushButton*>(
                QStringLiteral("mcpResetTokenButton"));
            if (!enabledCheck || !autoPort || !portSpin || !tokenEdit
                || !statusLabel || !errorLabel || !startButton || !stopButton
                || !logToggle || !permissionCombo || !copyConfig || !resetToken)
            {
                qCritical() << "mcp-ui-test: MCP controls missing";
                app.exit(1);
                return;
            }

            failIf(enabledCheck->isChecked(),
                   QStringLiteral("MCP enable switch defaults to on"));
            failIf(!autoPort->isChecked(),
                   QStringLiteral("port should default to automatic"));
            failIf(!(portSpin->minimum() <= 1024 && portSpin->maximum() >= 65535),
                   QStringLiteral("manual port range is too narrow"));
            failIf(permissionCombo->currentIndex() != 0,
                   QStringLiteral("permission mode is not read-only"));
            failIf(!logToggle->isChecked(),
                   QStringLiteral("log window should be enabled by default"));
            failIf(tokenEdit->text()
                       != QString::fromLatin1(server->token()),
                   QStringLiteral("settings token does not match the server"));
            failIf(statusLabel->text().contains(
                       QString::fromLatin1(server->token())),
                   QStringLiteral("token is echoed in the settings status"));
            failIf(!errorLabel->isHidden(),
                   QStringLiteral("error label is visible before an error"));

            // Only a human action starts the server.
            enabledCheck->setChecked(true);
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("enable switch did not start the server"));
            failIf(server->port() == 0,
                   QStringLiteral("enabled server has no listening port"));
            const quint16 port = server->port();
            const QByteArray token = server->token();
            failIf(!statusLabel->text().contains(QStringLiteral("监听中")),
                   QStringLiteral("settings status does not show listening"));
            failIf(!statusLabel->text().contains(QString::number(port)),
                   QStringLiteral("settings status does not show the actual port"));
            failIf(statusButton->text().contains(QStringLiteral("关闭")),
                   QStringLiteral("status bar was not updated after start"));
            failIf(!tokenEdit->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token field did not refresh after start"));

            stopButton->click();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("dialog stop button did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after dialog stop"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status not reset after stop"));
            startButton->click();
            QApplication::processEvents();
            failIf(!server->isRunning(),
                   QStringLiteral("dialog start button did not start the server"));

            copyConfig->click();
            QApplication::processEvents();
            const QJsonObject config =
                QJsonDocument::fromJson(
                    QApplication::clipboard()->text().toUtf8())
                    .object();
            const QJsonObject configuredServer =
                config.value(QStringLiteral("mcpServers"))
                    .toObject()
                    .value(QStringLiteral("pvforge"))
                    .toObject();
            failIf(!configuredServer.value(QStringLiteral("url"))
                        .toString()
                        .contains(QString::number(server->port())),
                   QStringLiteral("copied config has no live endpoint"));
            failIf(!configuredServer.value(QStringLiteral("headers"))
                        .toObject()
                        .value(QStringLiteral("Authorization"))
                        .toString()
                        .contains(QString::fromLatin1(token)),
                   QStringLiteral("copied config has no bearer token"));

            const quint16 livePort = server->port();
            QJsonObject summaryParams;
            summaryParams[QStringLiteral("name")] =
                QStringLiteral("project.summary");
            summaryParams[QStringLiteral("arguments")] = QJsonObject();
            QJsonObject summaryRequest;
            summaryRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            summaryRequest[QStringLiteral("id")] = 100;
            summaryRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            summaryRequest[QStringLiteral("params")] = summaryParams;
            const QJsonObject summaryResponse =
                mcpRpc(livePort, token, summaryRequest);
            failIf(summaryResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("project.summary returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 1,
                   QStringLiteral("toolInvoked did not add a log row"));
            failIf(!panel->isVisible(),
                   QStringLiteral("first tool call did not unfold the log"));
            failIf(!statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar entry does not show MCP enabled"));
            dialog->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_settings.png"));
            panel->grab().toImage().save(
                QStringLiteral("/tmp/pvf_mcp_log.png"));

            QJsonObject entityArgs;
            entityArgs[QStringLiteral("id")] = entityId;
            entityArgs[QStringLiteral("frame")] = 12;
            QJsonObject entityParams;
            entityParams[QStringLiteral("name")] = QStringLiteral("entity.get");
            entityParams[QStringLiteral("arguments")] = entityArgs;
            QJsonObject entityRequest;
            entityRequest[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
            entityRequest[QStringLiteral("id")] = 101;
            entityRequest[QStringLiteral("method")] = QStringLiteral("tools/call");
            entityRequest[QStringLiteral("params")] = entityParams;
            const QJsonObject entityResponse =
                mcpRpc(livePort, token, entityRequest);
            failIf(entityResponse.value(QStringLiteral("result")).isUndefined(),
                   QStringLiteral("entity.get returned no result"));
            QApplication::processEvents();

            failIf(panel->entryCountForTest() < 2,
                   QStringLiteral("second tool call did not add a log row"));
            const QString target = panel->entryTargetForTest(1);
            failIf(!target.contains(QStringLiteral("实体 #")),
                   QStringLiteral("log target has no entity: %1").arg(target));
            failIf(!target.contains(QStringLiteral("帧 12")),
                   QStringLiteral("log target has no frame: %1").arg(target));
            const QString detail = panel->entryDetailForTest(1);
            failIf(!detail.contains(QStringLiteral("参数")),
                   QStringLiteral("expanded row has no args"));
            failIf(!detail.contains(QStringLiteral("结果")),
                   QStringLiteral("expanded row has no result"));

            auto* tree =
                panel->findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
            if (tree && tree->topLevelItem(1))
            {
                tree->topLevelItem(1)->setExpanded(true);
                failIf(!tree->topLevelItem(1)->isExpanded(),
                       QStringLiteral("log row cannot expand"));
            }
            else
            {
                fail(QStringLiteral("log tree row missing"));
            }

            auto* detachButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogDetachButton"));
            if (detachButton)
            {
                detachButton->click();
                QApplication::processEvents();
                failIf(!panel->isDetached(),
                       QStringLiteral("detach button did not pop the log out"));
                detachButton->click();
                QApplication::processEvents();
                failIf(panel->isDetached(),
                       QStringLiteral("detach button did not dock the log"));
            }
            else
            {
                fail(QStringLiteral("detach button missing"));
            }

            const QString logText = treeText();
            failIf(logText.contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the log"));
            failIf(statusButton->text().contains(QString::fromLatin1(token))
                       || statusButton->toolTip().contains(
                           QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the status entry"));
            failIf(statusLabel->text().contains(QString::fromLatin1(token)),
                   QStringLiteral("token is echoed in the settings status"));

            panel->activateEntryForTest(1);
            QApplication::processEvents();
            failIf(window.selectedEntityForTest() != entityId,
                   QStringLiteral("clicking the log row did not select the entity"));
            failIf(window.currentFrameForTest() != 12,
                   QStringLiteral("clicking the log row did not jump to the frame"));

            bool stopSignalled = false;
            QObject::connect(panel, &pvf::McpLogPanel::stopAiRequested,
                             [&stopSignalled] { stopSignalled = true; });
            auto* stopAi = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogStopButton"));
            failIf(!stopAi || !stopAi->isEnabled(),
                   QStringLiteral("stop AI button is missing or disabled"));
            if (stopAi)
                stopAi->click();
            QApplication::processEvents();
            failIf(!stopSignalled,
                   QStringLiteral("stop AI button did not emit the signal"));

            const QByteArray oldToken = server->token();
            resetToken->click();
            QApplication::processEvents();
            failIf(server->token().isEmpty() || server->token() == oldToken,
                   QStringLiteral("token reset did not generate a new token"));
            failIf(tokenEdit->text() != QString::fromLatin1(server->token()),
                   QStringLiteral("token field did not update after reset"));
            failIf(treeText().contains(QString::fromLatin1(server->token())),
                   QStringLiteral("new token is echoed in the log"));

            auto* clearButton = panel->findChild<QToolButton*>(
                QStringLiteral("mcpLogClearButton"));
            if (clearButton)
                clearButton->click();
            QApplication::processEvents();
            failIf(panel->entryCountForTest() != 0,
                   QStringLiteral("clear did not empty the log"));

            enabledCheck->setChecked(false);
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("disabling MCP did not stop the server"));
            failIf(server->port() != 0,
                   QStringLiteral("port not cleared after disabling MCP"));
            failIf(statusButton->text().contains(QStringLiteral("已启用")),
                   QStringLiteral("status bar still says MCP is enabled"));
            failIf(!statusLabel->text().contains(QStringLiteral("未启用")),
                   QStringLiteral("settings status is not stopped"));

            // A busy manual port shows an error without leaking the token,
            // and retrying on the same port works once it is free.
            QTcpServer busyPortServer;
            if (!busyPortServer.listen(QHostAddress::LocalHost, 0))
            {
                fail(QStringLiteral("could not reserve a busy port"));
            }
            else
            {
                const quint16 busyPort = busyPortServer.serverPort();
                autoPort->setChecked(false);
                portSpin->setValue(busyPort);
                enabledCheck->setChecked(true);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("server started on an occupied port"));
                failIf(errorLabel->isHidden() || errorLabel->text().isEmpty(),
                       QStringLiteral("start failure did not show an error"));
                failIf(errorLabel->text().contains(
                           QString::fromLatin1(server->token())),
                       QStringLiteral("token is echoed in the error text"));
                failIf(!statusButton->text().contains(QStringLiteral("错误")),
                       QStringLiteral("status bar does not show the MCP error"));
                busyPortServer.close();
                QApplication::processEvents();
                startButton->click();
                QApplication::processEvents();
                failIf(!server->isRunning(),
                       QStringLiteral("retry after freeing the port failed"));
                failIf(server->port() != busyPort,
                       QStringLiteral("manual port was not honoured: %1 != %2")
                           .arg(server->port())
                           .arg(busyPort));
                failIf(!errorLabel->isHidden(),
                       QStringLiteral("error text did not clear on success"));
                enabledCheck->setChecked(false);
                QApplication::processEvents();
                failIf(server->isRunning(),
                       QStringLiteral("manual-port server did not stop"));
            }

            panel->showFromMenu();
            QApplication::processEvents();
            panel->close();
            QApplication::processEvents();
            failIf(panel->isVisible(),
                   QStringLiteral("closing the log window did not hide it"));
            failIf(!statusButton->isVisible(),
                   QStringLiteral("status entry disappeared after closing the log"));
            failIf(!statusButton->text().contains(QStringLiteral("MCP")),
                   QStringLiteral("status entry lost its MCP label"));
            statusButton->click();
            QApplication::processEvents();
            failIf(!panel->isVisible(),
                   QStringLiteral("status entry did not reopen the log"));
            panel->close();
            QApplication::processEvents();

            dialog->close();
            QApplication::processEvents();
            failIf(server->isRunning(),
                   QStringLiteral("closing settings started the server"));

            qInfo() << "mcp-ui-test failures" << failures;
            app.exit(failures == 0 ? 0 : 1);
        });
    return app.exec();
}



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
          QStringLiteral("one or more legacy panels are missing"));
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

    // A write locks every old write entry point.
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
    registry.add(QStringLiteral("--mcp-readonly-test"),
                 [](QApplication&) { return mcpReadonlyTest(); });
    registry.add(QStringLiteral("--mcp-http-test"),
                 [](QApplication&) { return mcpHttpTest(); });
    registry.add(QStringLiteral("--mcp-ui-test"),
                 [](QApplication& app) { return mcpUiTest(app); });
    registry.add(QStringLiteral("--mcp-write-test"),
                 [](QApplication&) { return runMcpWriteTest(); });
    // `--mcp-integration-test` was retired with issue #72: it drove the old
    // `McpServer` through a scene-armed `McpCommandService`, and scenes are not
    // part of the current project model. Its coverage now lives in
    // `--mcp-ui-test` (settings MCP panel, permission combo, emergency stop,
    // stdio config), `--mcp-test` (session-level write tool path) and the
    // `mcp2.*` CTest suite (write gating, batch rollback, keyframe writes,
    // endpoint file contract). Remaining gap: nothing asserts the endpoint
    // JSON file/directory permissions any more (`mcp2` writes them 0600/0700
    // but has no test for it).
    registry.add(QStringLiteral("--mcp-bridge-test"),
                 [](QApplication&) { return pvf::mcpBridgeTest(); });
}

} // namespace pvf::tests
