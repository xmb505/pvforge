#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include <memory>
#include <optional>

using namespace pvf;

namespace {

bool gitAvailable()
{
    // Production only needs `git`; git-lfs is not a runtime dependency.
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("--version")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
        return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeJsonFile(const QString& path, const QJsonObject& object)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    return file.write(data) == data.size() && file.flush();
}

QJsonObject readJsonFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

constexpr char kSkipMessage[] = "git 不可用，跳过依赖 git 的测试";

struct Fixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;
    std::unique_ptr<mcp2::HttpServer> server;
    QNetworkAccessManager manager;
    QString configPath;
    QString endpointPath;

    bool init(bool seedConfig = true, int configPort = 0)
    {
        if (!temp.isValid())
            return false;

        QString error;
        auto created = store::ProjectStore::create(temp.filePath("Demo.pvproj"),
                                                   model::ProjectSettings(), &error);
        if (!created)
        {
            qWarning("ProjectStore::create failed: %s", qPrintable(error));
            return false;
        }
        store = std::move(*created);
        service = std::make_unique<mcp2::Service>(&*store, &store->assets());

        configPath = temp.filePath("config/mcp.json");
        endpointPath = temp.filePath("config/mcp-endpoint.json");
        if (seedConfig)
        {
            const QJsonObject config{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                                     {QStringLiteral("port"), configPort}};
            if (!writeJsonFile(configPath, config))
                return false;
        }

        server = std::make_unique<mcp2::HttpServer>(service.get());
        server->setConfigFilePath(configPath);
        server->setEndpointFilePath(endpointPath);
        manager.setProxy(QNetworkProxy::NoProxy);
        return true;
    }
};

struct Response
{
    int status = 0;
    QByteArray body;
    QByteArray sessionId;
    QByteArray allowOrigin;
};

Response sendRequest(Fixture& fixture, const QByteArray& method, const QString& path,
                     const QByteArray& body, const QByteArray& token)
{
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                 .arg(fixture.server->port())
                 .arg(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json"));
    if (!token.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + token);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = method == "GET" ? fixture.manager.get(request)
                                           : fixture.manager.post(request, body);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    if (!reply->isFinished())
        loop.exec();
    if (!reply->isFinished())
        reply->abort();

    Response response;
    response.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    response.sessionId = reply->rawHeader(QByteArrayLiteral("Mcp-Session-Id"));
    response.allowOrigin =
        reply->rawHeader(QByteArrayLiteral("Access-Control-Allow-Origin"));
    reply->deleteLater();
    return response;
}

Response sendRpc(Fixture& fixture, const QByteArray& token,
                 const QJsonObject& request)
{
    return sendRequest(fixture, "POST", QStringLiteral("/mcp"), compact(request),
                       token);
}

QJsonObject rpcResult(const Response& response)
{
    return QJsonDocument::fromJson(response.body)
        .object()
        .value(QStringLiteral("result"))
        .toObject();
}

QJsonObject rpcError(const Response& response)
{
    return QJsonDocument::fromJson(response.body)
        .object()
        .value(QStringLiteral("error"))
        .toObject();
}

bool containsTool(const QJsonArray& tools, const QString& name)
{
    for (const QJsonValue& value : tools)
        if (value.toObject().value(QStringLiteral("name")).toString() == name)
            return true;
    return false;
}

QJsonObject toolCallRequest(const QString& name, const QJsonObject& arguments,
                            int id = 7)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), name},
                     {QStringLiteral("arguments"), arguments}}}};
}

QJsonObject createEntityArgs(const QString& name, int start, int end)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("start"), start},
                       {QStringLiteral("end"), end}};
}

} // namespace

class HttpServerTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void httpInitialize();
    void httpToolsList();
    void httpToolsCallProjectGet();
    void httpSecurityAndLimits();
    void httpNotificationsAndErrors();
    void httpCredentialsPersistAcrossRestart();
    void httpDefaultConfigUsesFixedPort();
    void httpPortConflictFailsClearly();
    void httpStopAiBlocksWrites();
};

void HttpServerTests::initTestCase()
{
    if (!gitAvailable())
        QSKIP(kSkipMessage);
}

void HttpServerTests::httpInitialize()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    const quint16 port = fixture.server->start(0, &error);
    QVERIFY2(port > 0, qPrintable(error));
    QVERIFY(fixture.server->isRunning());
    QCOMPARE(fixture.server->port(), port);

    const QByteArray token = fixture.server->token();
    QCOMPARE(token.size(), 64);

    const QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("protocolVersion"),
                      QStringLiteral("2025-06-18")},
                     {QStringLiteral("capabilities"), QJsonObject()},
                     {QStringLiteral("clientInfo"),
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("test")},
                                  {QStringLiteral("version"),
                                   QStringLiteral("1.0")}}}}}};

    const Response response = sendRpc(fixture, token, request);
    QCOMPARE(response.status, 200);
    QVERIFY(!response.sessionId.isEmpty());
    QVERIFY2(!response.body.contains(token), "response must not echo the token");
    QVERIFY(response.allowOrigin.isEmpty());

    const QJsonObject envelope =
        QJsonDocument::fromJson(response.body).object();
    QCOMPARE(envelope.value(QStringLiteral("jsonrpc")).toString(),
             QStringLiteral("2.0"));
    QCOMPARE(envelope.value(QStringLiteral("id")).toInt(), 1);

    const QJsonObject result = rpcResult(response);
    QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2025-06-18"));
    QVERIFY(result.value(QStringLiteral("capabilities"))
                .toObject()
                .contains(QStringLiteral("tools")));
    const QJsonObject serverInfo =
        result.value(QStringLiteral("serverInfo")).toObject();
    QCOMPARE(serverInfo.value(QStringLiteral("name")).toString(),
             QStringLiteral("PVForge"));
    QVERIFY(!serverInfo.value(QStringLiteral("version")).toString().isEmpty());

    fixture.server->stop();
    QVERIFY(!fixture.server->isRunning());
    QCOMPARE(fixture.server->port(), quint16(0));
}

void HttpServerTests::httpToolsList()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));

    const QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 2},
        {QStringLiteral("method"), QStringLiteral("tools/list")}};

    const Response response = sendRpc(fixture, fixture.server->token(), request);
    QCOMPARE(response.status, 200);

    const QJsonArray tools =
        rpcResult(response).value(QStringLiteral("tools")).toArray();
    QVERIFY(!tools.isEmpty());
    QVERIFY(containsTool(tools, QStringLiteral("project.get")));
    QVERIFY(containsTool(tools, QStringLiteral("entity.create")));
}

void HttpServerTests::httpToolsCallProjectGet()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));
    const QByteArray token = fixture.server->token();

    QVERIFY(QFileInfo::exists(fixture.endpointPath));
    const QJsonObject endpoint = readJsonFile(fixture.endpointPath);
    QCOMPARE(endpoint.value(QStringLiteral("url")).toString(),
             fixture.server->endpointUrl());
    QCOMPARE(endpoint.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(token));
    QCOMPARE(endpoint.value(QStringLiteral("port")).toInt(), int(fixture.server->port()));
    QCOMPARE(qint64(endpoint.value(QStringLiteral("pid")).toDouble()),
             QCoreApplication::applicationPid());

    QSignalSpy spy(fixture.server.get(), &mcp2::HttpServer::toolInvoked);

    const QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 3},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("project.get")},
                     {QStringLiteral("arguments"), QJsonObject()}}}};

    const Response response = sendRpc(fixture, token, request);
    QCOMPARE(response.status, 200);

    const QJsonObject result = rpcResult(response);
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);
    const QJsonArray content =
        result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));

    const QJsonObject payload =
        QJsonDocument::fromJson(content.at(0)
                                    .toObject()
                                    .value(QStringLiteral("text"))
                                    .toString()
                                    .toUtf8())
            .object();
    QCOMPARE(payload.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(payload.value(QStringLiteral("data"))
                .toObject()
                .contains(QStringLiteral("settings")));

    const QJsonObject structured =
        result.value(QStringLiteral("structuredContent")).toObject();
    QCOMPARE(structured, payload);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> arguments = spy.at(0);
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("project.get"));
    QCOMPARE(arguments.at(1).toString(), QStringLiteral("{}"));
    QCOMPARE(arguments.at(4).toBool(), true);
    QVERIFY(arguments.at(3).toLongLong() >= 0);
    const QJsonObject logged =
        QJsonDocument::fromJson(arguments.at(2).toString().toUtf8()).object();
    QCOMPARE(logged.value(QStringLiteral("ok")).toBool(), true);

    fixture.server->stop();
    QVERIFY(!QFileInfo::exists(fixture.endpointPath));
}

void HttpServerTests::httpSecurityAndLimits()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));
    const QByteArray token = fixture.server->token();

    const QJsonObject initialize{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("initialize")}};

    const Response unauthorized =
        sendRpc(fixture, QByteArrayLiteral("wrong-token"), initialize);
    QCOMPARE(unauthorized.status, 401);
    QVERIFY(unauthorized.allowOrigin.isEmpty());
    QVERIFY(!unauthorized.body.contains(token));

    const Response notFound =
        sendRequest(fixture, "POST", QStringLiteral("/not-mcp"),
                    compact(initialize), token);
    QCOMPARE(notFound.status, 404);

    const Response getMcp =
        sendRequest(fixture, "GET", QStringLiteral("/mcp"), QByteArray(), token);
    QCOMPARE(getMcp.status, 405);

    QByteArray oversized = QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ping\",\"padding\":\"");
    oversized.append(QByteArray(1024 * 1024, 'a'));
    oversized.append(QByteArrayLiteral("\"}"));
    QVERIFY(oversized.size() > 1024 * 1024);
    const Response tooLarge =
        sendRequest(fixture, "POST", QStringLiteral("/mcp"), oversized, token);
    QCOMPARE(tooLarge.status, 413);
}

void HttpServerTests::httpNotificationsAndErrors()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));
    const QByteArray token = fixture.server->token();

    const QJsonObject notification{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("notifications/initialized")}};
    const Response accepted = sendRpc(fixture, token, notification);
    QCOMPARE(accepted.status, 202);
    QVERIFY(accepted.body.isEmpty());

    const QJsonObject ping{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), 10},
                           {QStringLiteral("method"), QStringLiteral("ping")}};
    const Response pingResponse = sendRpc(fixture, token, ping);
    QCOMPARE(pingResponse.status, 200);
    QVERIFY(rpcResult(pingResponse).isEmpty());

    const QJsonObject unknown{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), 11},
                              {QStringLiteral("method"), QStringLiteral("unknown/method")}};
    const Response unknownResponse = sendRpc(fixture, token, unknown);
    QCOMPARE(unknownResponse.status, 200);
    QCOMPARE(rpcError(unknownResponse).value(QStringLiteral("code")).toInt(),
             -32601);

    const Response malformed = sendRequest(fixture, "POST", QStringLiteral("/mcp"),
                                           QByteArrayLiteral("{"), token);
    QCOMPARE(malformed.status, 200);
    QCOMPARE(rpcError(malformed).value(QStringLiteral("code")).toInt(), -32700);

    const QJsonObject badVersion{
        {QStringLiteral("jsonrpc"), QStringLiteral("1.0")},
        {QStringLiteral("id"), 12},
        {QStringLiteral("method"), QStringLiteral("ping")}};
    const Response badVersionResponse = sendRpc(fixture, token, badVersion);
    QCOMPARE(badVersionResponse.status, 200);
    QCOMPARE(rpcError(badVersionResponse).value(QStringLiteral("code")).toInt(),
             -32600);

    const QJsonObject badArgs{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 13},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("project.get")},
                     {QStringLiteral("arguments"), QStringLiteral("nope")}}}};
    const Response badArgsResponse = sendRpc(fixture, token, badArgs);
    QCOMPARE(badArgsResponse.status, 200);
    QCOMPARE(rpcError(badArgsResponse).value(QStringLiteral("code")).toInt(),
             -32602);
}

void HttpServerTests::httpCredentialsPersistAcrossRestart()
{
    Fixture fixture;
    QVERIFY(fixture.init(true, 0));

    QString error;
    const quint16 firstPort = fixture.server->start(0, &error);
    QVERIFY2(firstPort > 0, qPrintable(error));
    const QByteArray firstToken = fixture.server->token();
    QCOMPARE(firstToken.size(), 64);
    const QJsonObject firstEndpoint = readJsonFile(fixture.endpointPath);
    fixture.server->stop();

    QVERIFY(!QFileInfo::exists(fixture.endpointPath));

    QJsonObject config = readJsonFile(fixture.configPath);
    QCOMPARE(config.value(QStringLiteral("host")).toString(),
             QStringLiteral("127.0.0.1"));
    QCOMPARE(config.value(QStringLiteral("port")).toInt(), int(firstPort));
    QCOMPARE(config.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(firstToken));

    const quint16 secondPort = fixture.server->start(0, &error);
    QVERIFY2(secondPort > 0, qPrintable(error));
    QCOMPARE(secondPort, firstPort);
    QCOMPARE(fixture.server->token(), firstToken);

    const QJsonObject secondEndpoint = readJsonFile(fixture.endpointPath);
    QCOMPARE(secondEndpoint, firstEndpoint);
    QCOMPARE(secondEndpoint.value(QStringLiteral("url")).toString(),
             QStringLiteral("http://127.0.0.1:%1/mcp").arg(firstPort));
    QCOMPARE(secondEndpoint.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(firstToken));
    QCOMPARE(secondEndpoint.value(QStringLiteral("port")).toInt(), int(firstPort));

    config = readJsonFile(fixture.configPath);
    QCOMPARE(config.value(QStringLiteral("port")).toInt(), int(firstPort));
    QCOMPARE(config.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(firstToken));

    fixture.server->stop();
    QVERIFY(!QFileInfo::exists(fixture.endpointPath));

    fixture.server->resetCredentials();
    const QByteArray resetToken = fixture.server->token();
    QVERIFY(resetToken != firstToken);
    QCOMPARE(resetToken.size(), 64);

    config = readJsonFile(fixture.configPath);
    QCOMPARE(config.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(resetToken));
    QCOMPARE(config.value(QStringLiteral("port")).toInt(), int(firstPort));

    const quint16 thirdPort = fixture.server->start(0, &error);
    QVERIFY2(thirdPort > 0, qPrintable(error));
    QCOMPARE(thirdPort, firstPort);
    QCOMPARE(fixture.server->token(), resetToken);
    QCOMPARE(readJsonFile(fixture.endpointPath)
                 .value(QStringLiteral("token"))
                 .toString(),
             QString::fromLatin1(resetToken));

    fixture.server->stop();
}

void HttpServerTests::httpDefaultConfigUsesFixedPort()
{
    Fixture fixture;
    QVERIFY(fixture.init(false));

    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, mcp2::HttpServer::kDefaultPort))
            QSKIP("默认端口 47821 被占用，跳过固定端口测试");
    }

    QString error;
    const quint16 port = fixture.server->start(0, &error);
    QVERIFY2(port > 0, qPrintable(error));
    QCOMPARE(port, mcp2::HttpServer::kDefaultPort);

    const QJsonObject config = readJsonFile(fixture.configPath);
    QCOMPARE(config.value(QStringLiteral("host")).toString(),
             QStringLiteral("127.0.0.1"));
    QCOMPARE(config.value(QStringLiteral("port")).toInt(),
             int(mcp2::HttpServer::kDefaultPort));
    QCOMPARE(config.value(QStringLiteral("token")).toString().size(), 64);
    QCOMPARE(config.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(fixture.server->token()));

    const QJsonObject endpoint = readJsonFile(fixture.endpointPath);
    QCOMPARE(endpoint.value(QStringLiteral("port")).toInt(),
             int(mcp2::HttpServer::kDefaultPort));
    QCOMPARE(endpoint.value(QStringLiteral("token")).toString(),
             QString::fromLatin1(fixture.server->token()));

    fixture.server->stop();
}

void HttpServerTests::httpPortConflictFailsClearly()
{
    Fixture fixture;
    QVERIFY(fixture.init(false));

    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
    const quint16 busyPort = blocker.serverPort();
    QVERIFY(busyPort > 0);

    const QJsonObject config{
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), int(busyPort)}};
    QVERIFY(writeJsonFile(fixture.configPath, config));

    QString error;
    QCOMPARE(fixture.server->start(0, &error), quint16(0));
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QString::number(busyPort)),
             qPrintable(QStringLiteral("error should name the busy port: %1").arg(error)));
    QVERIFY(!fixture.server->isRunning());
    QVERIFY(!QFileInfo::exists(fixture.endpointPath));

    blocker.close();
}

void HttpServerTests::httpStopAiBlocksWrites()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));
    const QByteArray token = fixture.server->token();

    QSignalSpy started(fixture.server.get(),
                       &mcp2::HttpServer::toolCallStarted);
    QSignalSpy finished(fixture.server.get(),
                        &mcp2::HttpServer::toolCallFinished);
    QSignalSpy stopped(fixture.server.get(),
                       &mcp2::HttpServer::stoppedChanged);

    // A normal write call is flagged as a write and succeeds.
    Response response = sendRpc(
        fixture, token,
        toolCallRequest(QStringLiteral("entity.create"),
                        createEntityArgs(QStringLiteral("第一个"), 0, 30)));
    QCOMPARE(response.status, 200);
    QCOMPARE(rpcResult(response).value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.at(0).at(0).toString(), QStringLiteral("entity.create"));
    QCOMPARE(started.at(0).at(2).toBool(), true);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(3).toBool(), true);
    QVERIFY(finished.at(0).at(2).toLongLong() >= 0);

    // A read-only call is flagged as read.
    response = sendRpc(fixture, token,
                       toolCallRequest(QStringLiteral("project.get"), QJsonObject()));
    QCOMPARE(response.status, 200);
    QCOMPARE(rpcResult(response).value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(started.count(), 2);
    QCOMPARE(started.at(1).at(0).toString(), QStringLiteral("project.get"));
    QCOMPARE(started.at(1).at(2).toBool(), false);
    QCOMPARE(finished.at(1).at(3).toBool(), true);

    const int entitiesBefore = fixture.store->project().entities.size();
    const int historyBefore = fixture.store->history(0).size();

    fixture.server->stopAi();
    QVERIFY(fixture.server->aiStopped());
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(stopped.at(0).at(0).toBool(), true);

    // tools/list keeps returning every tool while stopped.
    response = sendRpc(fixture, token,
                       QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                   {QStringLiteral("id"), 20},
                                   {QStringLiteral("method"), QStringLiteral("tools/list")}});
    QCOMPARE(response.status, 200);
    QVERIFY(containsTool(rpcResult(response).value(QStringLiteral("tools")).toArray(),
                         QStringLiteral("entity.create")));

    // A stopped write is a 200 JSON-RPC result with isError=true, not HTTP 500.
    response = sendRpc(
        fixture, token,
        toolCallRequest(QStringLiteral("entity.create"),
                        createEntityArgs(QStringLiteral("拒绝"), 0, 30)));
    QCOMPARE(response.status, 200);
    const QJsonObject denied = rpcResult(response);
    QCOMPARE(denied.value(QStringLiteral("isError")).toBool(), true);
    const QJsonObject deniedBody =
        denied.value(QStringLiteral("structuredContent")).toObject();
    QCOMPARE(deniedBody.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(deniedBody.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toString(),
             QStringLiteral("stopped_by_human"));
    QCOMPARE(started.count(), 3);
    QCOMPARE(started.at(2).at(2).toBool(), true);
    QCOMPARE(finished.count(), 3);
    QCOMPARE(finished.at(2).at(3).toBool(), false);

    QCOMPARE(fixture.store->project().entities.size(), entitiesBefore);
    QCOMPARE(fixture.store->history(0).size(), historyBefore);

    // Read-only tools still work while stopped.
    response = sendRpc(fixture, token,
                       toolCallRequest(QStringLiteral("project.get"), QJsonObject()));
    QCOMPARE(response.status, 200);
    QCOMPARE(rpcResult(response).value(QStringLiteral("isError")).toBool(), false);

    // Resume re-enables writes and reports the state change.
    fixture.server->resumeAi();
    QVERIFY(!fixture.server->aiStopped());
    QCOMPARE(stopped.count(), 2);
    QCOMPARE(stopped.at(1).at(0).toBool(), false);

    response = sendRpc(
        fixture, token,
        toolCallRequest(QStringLiteral("entity.create"),
                        createEntityArgs(QStringLiteral("恢复"), 0, 30)));
    QCOMPARE(response.status, 200);
    QCOMPARE(rpcResult(response).value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(fixture.store->project().entities.size(), entitiesBefore + 1);

    fixture.server->stop();
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    HttpServerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "http_server_tests.moc"
