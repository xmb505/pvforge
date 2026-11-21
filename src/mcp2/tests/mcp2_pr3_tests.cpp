// PR3 acceptance tests: preview.render_range contact sheets, the standard MCP
// image content block in the HTTP transport, project.summary timeline rows and
// entity.list tree mode. Kept in their own binary so the historical mcp2 test
// file can stay under its size ratchet.

#include "mcp2/HttpServer.h"
#include "mcp2/Service.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
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
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("--version")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
        return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

constexpr char kSkipMessage[] = "git 不可用，跳过依赖 git 的测试";

QString errorCode(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString();
}

QByteArray errorMessage(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString()
        .toUtf8();
}

QJsonObject dataOf(const QJsonObject& result)
{
    return result.value(QStringLiteral("data")).toObject();
}

bool isOk(const QJsonObject& result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct Fixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;
    qint64 clock = 1000;

    bool init()
    {
        if (!temp.isValid())
            return false;
        QString error;
        auto created = store::ProjectStore::create(temp.filePath("Demo.pvproj"),
                                                   model::ProjectSettings(),
                                                   &error);
        if (!created)
        {
            qWarning("ProjectStore::create failed: %s", qPrintable(error));
            return false;
        }
        store = std::move(*created);
        service = std::make_unique<mcp2::Service>(&*store, &store->assets());
        return true;
    }

    QJsonObject call(const QString& tool, const QJsonObject& args = QJsonObject())
    {
        return service->execute(tool, args);
    }

    // Every preview render consumes one slot; tests that make several calls
    // advance the wall clock so each call starts a new 1s window.
    void advanceClockPerCall()
    {
        clock = 1000;
        service->setClock([this] { return clock += 1000; });
    }

    QString createEntity(const QString& name, int start, int end,
                         const QString& parent = QString(), int row = -1,
                         const QString& kind = QString())
    {
        QJsonObject args;
        args.insert(QStringLiteral("name"), name);
        args.insert(QStringLiteral("start"), start);
        args.insert(QStringLiteral("end"), end);
        if (!parent.isEmpty())
            args.insert(QStringLiteral("parentUuid"), parent);
        if (row >= 0)
            args.insert(QStringLiteral("row"), row);
        if (!kind.isEmpty())
        {
            args.insert(QStringLiteral("content"),
                        QJsonObject{{QStringLiteral("kind"), kind}});
        }
        const QJsonObject result =
            call(QStringLiteral("entity.create"), args);
        return dataOf(result).value(QStringLiteral("uuid")).toString();
    }
};

QJsonArray edgeFrames(int from, int to, int count)
{
    QJsonArray array;
    for (int index = 0; index < count; ++index)
    {
        const int frame =
            count == 1
                ? from
                : int(std::lround(double(from)
                                  + double(to - from) * index
                                        / double(count - 1)));
        array.append(frame);
    }
    return array;
}

bool writeJsonFile(const QString& path, const QJsonObject& object)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray data =
        QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    return file.write(data) == data.size() && file.flush();
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

struct HttpFixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;
    std::unique_ptr<mcp2::HttpServer> server;
    QNetworkAccessManager manager;

    bool init()
    {
        if (!temp.isValid())
            return false;
        QString error;
        auto created = store::ProjectStore::create(temp.filePath("Demo.pvproj"),
                                                   model::ProjectSettings(),
                                                   &error);
        if (!created)
        {
            qWarning("ProjectStore::create failed: %s", qPrintable(error));
            return false;
        }
        store = std::move(*created);
        service = std::make_unique<mcp2::Service>(&*store, &store->assets());
        const QString configPath = temp.filePath("config/mcp.json");
        if (!writeJsonFile(configPath,
                           QJsonObject{{QStringLiteral("host"),
                                        QStringLiteral("127.0.0.1")},
                                       {QStringLiteral("port"), 0}}))
            return false;
        server = std::make_unique<mcp2::HttpServer>(service.get());
        server->setConfigFilePath(configPath);
        server->setEndpointFilePath(temp.filePath("config/mcp-endpoint.json"));
        manager.setProxy(QNetworkProxy::NoProxy);
        return true;
    }
};

struct Response
{
    int status = 0;
    QByteArray body;
};

Response sendRpc(HttpFixture& fixture, const QJsonObject& request)
{
    QNetworkRequest networkRequest(
        QUrl(QStringLiteral("http://127.0.0.1:%1/mcp")
                 .arg(fixture.server->port())));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QByteArrayLiteral("application/json"));
    networkRequest.setRawHeader(
        QByteArrayLiteral("Authorization"),
        QByteArrayLiteral("Bearer ") + fixture.server->token());

    QNetworkReply* reply = fixture.manager.post(networkRequest, compact(request));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20000);
    if (!reply->isFinished())
        loop.exec();
    if (!reply->isFinished())
        reply->abort();

    Response response;
    response.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    reply->deleteLater();
    return response;
}

QJsonObject toolCallRequest(const QString& name, const QJsonObject& arguments)
{
    return QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 7},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), name},
                     {QStringLiteral("arguments"), arguments}}}};
}

QJsonObject rpcResult(const Response& response)
{
    return QJsonDocument::fromJson(response.body)
        .object()
        .value(QStringLiteral("result"))
        .toObject();
}

} // namespace

class Mcp2Pr3Tests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void renderRangeFromFrames();
    void renderRangeFromToCount();
    void renderRangeRejectsInvalidArguments();
    void renderRangeCountsAsOnePreviewSlot();
    void renderRangeBase64Output();
    void httpImageContentBlock();
    void summaryTimelineRows();
    void entityListTreeNesting();
    void entityListTreeCycleGuard();
};

void Mcp2Pr3Tests::initTestCase()
{
    if (!gitAvailable())
        QSKIP(kSkipMessage);
}

void Mcp2Pr3Tests::renderRangeFromFrames()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    fixture.advanceClockPerCall();

    const QJsonObject result = fixture.call(
        QStringLiteral("preview.render_range"),
        QJsonObject{
            {QStringLiteral("frames"), QJsonArray{0, 5, 10}},
            {QStringLiteral("width"), 96},
            {QStringLiteral("cols"), 2},
            {QStringLiteral("showFrameLabel"), false},
        });
    QVERIFY2(isOk(result), errorMessage(result));
    const QJsonObject data = dataOf(result);

    QCOMPARE(data.value(QStringLiteral("mimeType")).toString(),
             QStringLiteral("image/png"));
    QCOMPARE(data.value(QStringLiteral("cellWidth")).toInt(), 96);
    // Project 1920x1080 keeps its aspect ratio: 96 * 1080 / 1920 = 54.
    QCOMPARE(data.value(QStringLiteral("cellHeight")).toInt(), 54);
    QCOMPARE(data.value(QStringLiteral("cols")).toInt(), 2);
    QCOMPARE(data.value(QStringLiteral("rows")).toInt(), 2);
    QCOMPARE(data.value(QStringLiteral("width")).toInt(), 192);
    QCOMPARE(data.value(QStringLiteral("height")).toInt(), 108);
    QCOMPARE(data.value(QStringLiteral("frames")), QJsonArray({0, 5, 10}));
    QVERIFY(data.value(QStringLiteral("cameraUuid")).isNull());
    QVERIFY(!data.contains(QStringLiteral("base64")));

    const QString path = data.value(QStringLiteral("path")).toString();
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(192, 108));
#ifdef Q_OS_UNIX
    const QFile::Permissions foreign =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther;
    QCOMPARE(QFile::permissions(path) & foreign, QFile::Permissions{});
#endif
}

void Mcp2Pr3Tests::renderRangeFromToCount()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    fixture.advanceClockPerCall();

    QJsonObject result = fixture.call(
        QStringLiteral("preview.render_range"),
        QJsonObject{
            {QStringLiteral("from"), 0},
            {QStringLiteral("to"), 20},
            {QStringLiteral("count"), 6},
            {QStringLiteral("width"), 64},
            {QStringLiteral("cols"), 3},
            {QStringLiteral("showFrameLabel"), false},
        });
    QVERIFY2(isOk(result), errorMessage(result));
    QJsonObject data = dataOf(result);
    QCOMPARE(data.value(QStringLiteral("frames")),
             edgeFrames(0, 20, 6));
    QCOMPARE(data.value(QStringLiteral("cols")).toInt(), 3);
    QCOMPARE(data.value(QStringLiteral("rows")).toInt(), 2);
    QCOMPARE(data.value(QStringLiteral("cellWidth")).toInt(), 64);
    QCOMPARE(data.value(QStringLiteral("cellHeight")).toInt(), 36);
    QCOMPARE(data.value(QStringLiteral("width")).toInt(), 192);
    QCOMPARE(data.value(QStringLiteral("height")).toInt(), 72);
    const QString path = data.value(QStringLiteral("path")).toString();
    QCOMPARE(QImage(path).size(), QSize(192, 72));

    // count == 1 always renders `from`, even for a single-frame interval.
    result = fixture.call(
        QStringLiteral("preview.render_range"),
        QJsonObject{{QStringLiteral("from"), 7},
                    {QStringLiteral("to"), 7},
                    {QStringLiteral("count"), 1},
                    {QStringLiteral("width"), 64}});
    QVERIFY2(isOk(result), errorMessage(result));
    data = dataOf(result);
    QCOMPARE(data.value(QStringLiteral("frames")), QJsonArray({7}));
    QCOMPARE(data.value(QStringLiteral("cols")).toInt(), 1);
    QCOMPARE(data.value(QStringLiteral("rows")).toInt(), 1);
}

void Mcp2Pr3Tests::renderRangeRejectsInvalidArguments()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    fixture.advanceClockPerCall();

    const QString textEntity =
        fixture.createEntity(QStringLiteral("文本"), 0, 30, QString(), 0,
                             QStringLiteral("text"));
    QVERIFY(!textEntity.isEmpty());

    const auto expectInvalid = [&fixture](const QJsonObject& args,
                                          const QString& code) {
        const QJsonObject result =
            fixture.call(QStringLiteral("preview.render_range"), args);
        QCOMPARE(errorCode(result), code);
    };

    // Frame selector is mandatory and mutually exclusive.
    expectInvalid(QJsonObject{}, QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("frames"), QJsonArray{}},
                              {QStringLiteral("width"), 64}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("frames"), QJsonArray{0, 1}},
                              {QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("frames"), QJsonArray{1.5}}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("frames"), QJsonArray{-1}}},
                  QStringLiteral("invalid_argument"));
    QJsonArray tooMany;
    for (int frame = 0; frame < 17; ++frame)
        tooMany.append(frame);
    expectInvalid(QJsonObject{{QStringLiteral("frames"), tooMany}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 10},
                              {QStringLiteral("to"), 5}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("count"), 17}},
                  QStringLiteral("invalid_argument"));

    // Layout bounds.
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("cols"), 0}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("cols"), 7}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("width"), 63}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("width"), 1025}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("bogus"), 1}},
                  QStringLiteral("invalid_argument"));

    // A non-camera UUID and an invalid CSS colour are rejected before render.
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("cameraUuid"), textEntity}},
                  QStringLiteral("invalid_argument"));
    expectInvalid(QJsonObject{{QStringLiteral("from"), 0},
                              {QStringLiteral("to"), 10},
                              {QStringLiteral("background"),
                               QStringLiteral("not-a-color")}},
                  QStringLiteral("invalid_argument"));

    // 6 columns * 1024px exceeds the 4096 composite-side limit.
    expectInvalid(QJsonObject{{QStringLiteral("frames"),
                               QJsonArray{0, 1, 2, 3, 4, 5}},
                              {QStringLiteral("cols"), 6},
                              {QStringLiteral("width"), 1024}},
                  QStringLiteral("limit_exceeded"));
}

void Mcp2Pr3Tests::renderRangeCountsAsOnePreviewSlot()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    qint64 now = 1000;
    fixture.service->setClock([&now] { return now; });

    QJsonObject result = fixture.call(QStringLiteral("preview.render_frame"),
                                      QJsonObject{{QStringLiteral("frame"), 0}});
    QVERIFY2(isOk(result), errorMessage(result));

    // The whole contact sheet consumes the second slot, not one slot per cell.
    result = fixture.call(QStringLiteral("preview.render_range"),
                          QJsonObject{{QStringLiteral("frames"),
                                       QJsonArray{0, 1, 2, 3}},
                                      {QStringLiteral("width"), 64}});
    QVERIFY2(isOk(result), errorMessage(result));

    result = fixture.call(QStringLiteral("preview.render_range"),
                          QJsonObject{{QStringLiteral("frames"),
                                       QJsonArray{0, 1}},
                                      {QStringLiteral("width"), 64}});
    QCOMPARE(errorCode(result), QStringLiteral("limit_exceeded"));

    now += 1000;
    result = fixture.call(QStringLiteral("preview.render_range"),
                          QJsonObject{{QStringLiteral("frames"),
                                       QJsonArray{0, 1}},
                                      {QStringLiteral("width"), 64}});
    QVERIFY2(isOk(result), errorMessage(result));
}

void Mcp2Pr3Tests::renderRangeBase64Output()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    fixture.advanceClockPerCall();

    const QJsonObject result = fixture.call(
        QStringLiteral("preview.render_range"),
        QJsonObject{{QStringLiteral("frames"), QJsonArray{0, 4}},
                    {QStringLiteral("width"), 64},
                    {QStringLiteral("includeBase64"), true}});
    QVERIFY2(isOk(result), errorMessage(result));
    const QJsonObject data = dataOf(result);
    const QByteArray png = QByteArray::fromBase64(
        data.value(QStringLiteral("base64")).toString().toLatin1());
    QVERIFY2(png.startsWith("\x89PNG"), "base64 must be the rendered PNG");
    QCOMPARE(data.value(QStringLiteral("frames")), QJsonArray({0, 4}));
}

void Mcp2Pr3Tests::httpImageContentBlock()
{
    HttpFixture fixture;
    QVERIFY(fixture.init());
    QString error;
    QVERIFY2(fixture.server->start(0, &error) > 0, qPrintable(error));

    const Response response = sendRpc(
        fixture,
        toolCallRequest(
            QStringLiteral("preview.render_range"),
            QJsonObject{{QStringLiteral("frames"), QJsonArray{3}},
                        {QStringLiteral("width"), 64},
                        {QStringLiteral("includeBase64"), true}}));
    QCOMPARE(response.status, 200);
    const QJsonObject result = rpcResult(response);
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);

    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));

    const QJsonObject textPayload =
        QJsonDocument::fromJson(content.at(0)
                                    .toObject()
                                    .value(QStringLiteral("text"))
                                    .toString()
                                    .toUtf8())
            .object();
    const QJsonObject textData = textPayload.value(QStringLiteral("data")).toObject();
    QVERIFY2(!textData.contains(QStringLiteral("base64")),
             "the text block must not repeat the base64 payload");
    QVERIFY(textData.contains(QStringLiteral("path")));

    const QJsonObject image = content.at(1).toObject();
    QCOMPARE(image.value(QStringLiteral("type")).toString(),
             QStringLiteral("image"));
    QCOMPARE(image.value(QStringLiteral("mimeType")).toString(),
             QStringLiteral("image/png"));
    const QString imageBase64 = image.value(QStringLiteral("data")).toString();
    QVERIFY(!imageBase64.isEmpty());

    // structuredContent keeps the complete result for existing callers.
    const QJsonObject structured =
        result.value(QStringLiteral("structuredContent")).toObject();
    const QJsonObject structuredData =
        structured.value(QStringLiteral("data")).toObject();
    QCOMPARE(structuredData.value(QStringLiteral("base64")).toString(),
             imageBase64);

    // Non-image success results keep the single text block.
    const Response plain = sendRpc(
        fixture, toolCallRequest(QStringLiteral("project.get"), QJsonObject()));
    const QJsonArray plainContent =
        rpcResult(plain).value(QStringLiteral("content")).toArray();
    QCOMPARE(plainContent.size(), 1);
    QCOMPARE(plainContent.at(0).toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("text"));

    fixture.server->stop();
}

void Mcp2Pr3Tests::summaryTimelineRows()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString rowTwoLate = fixture.createEntity(
        QStringLiteral("晚"), 16, 20, QString(), 2);
    const QString rowZero =
        fixture.createEntity(QStringLiteral("底层"), 0, 5, QString(), 0);
    const QString rowTwoEarly = fixture.createEntity(
        QStringLiteral("早"), 5, 15, QString(), 2);
    const QString child = fixture.createEntity(QStringLiteral("子"), 17, 18,
                                               rowTwoLate, 0);
    QVERIFY(!rowTwoLate.isEmpty() && !rowZero.isEmpty()
            && !rowTwoEarly.isEmpty() && !child.isEmpty());

    const QJsonObject result =
        fixture.call(QStringLiteral("project.summary"));
    QVERIFY2(isOk(result), errorMessage(result));
    const QJsonObject data = dataOf(result);

    // Existing top-level fields stay untouched.
    QCOMPARE(data.value(QStringLiteral("entityCount")).toInt(), 4);
    QCOMPARE(data.value(QStringLiteral("durationFrames")).toInt(), 21);

    const QJsonArray rows =
        data.value(QStringLiteral("timeline")).toObject().value(
            QStringLiteral("rows")).toArray();
    QCOMPARE(rows.size(), 2);

    const QJsonObject first = rows.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("row")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("minStart")).toInt(), 0);
    QCOMPARE(first.value(QStringLiteral("maxEnd")).toInt(), 5);
    QCOMPARE(first.value(QStringLiteral("clips")).toArray().size(), 1);

    const QJsonObject second = rows.at(1).toObject();
    QCOMPARE(second.value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(second.value(QStringLiteral("minStart")).toInt(), 5);
    QCOMPARE(second.value(QStringLiteral("maxEnd")).toInt(), 20);
    const QJsonArray clips = second.value(QStringLiteral("clips")).toArray();
    QCOMPARE(clips.size(), 2);
    // Clips are sorted by start, not insertion order.
    QCOMPARE(clips.at(0).toObject().value(QStringLiteral("uuid")).toString(),
             rowTwoEarly);
    QCOMPARE(clips.at(1).toObject().value(QStringLiteral("uuid")).toString(),
             rowTwoLate);
    const QJsonObject lateClip = clips.at(1).toObject();
    QCOMPARE(lateClip.value(QStringLiteral("name")).toString(),
             QStringLiteral("晚"));
    QCOMPARE(lateClip.value(QStringLiteral("kind")).toString(),
             QStringLiteral("empty"));
    QCOMPARE(lateClip.value(QStringLiteral("start")).toInt(), 16);
    QCOMPARE(lateClip.value(QStringLiteral("end")).toInt(), 20);
    QCOMPARE(lateClip.value(QStringLiteral("hidden")).toBool(), false);
    QCOMPARE(lateClip.value(QStringLiteral("locked")).toBool(), false);
    QCOMPARE(lateClip.value(QStringLiteral("childCount")).toInt(), 1);

    // Nested entities stay inside their parent clip, never in main rows.
    for (const QJsonValue& row : rows)
    {
        for (const QJsonValue& clip :
             row.toObject().value(QStringLiteral("clips")).toArray())
        {
            QVERIFY(clip.toObject().value(QStringLiteral("uuid")).toString()
                    != child);
        }
    }
}

void Mcp2Pr3Tests::entityListTreeNesting()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString root = fixture.createEntity(QStringLiteral("组"), 0, 100,
                                              QString(), 0);
    const QString middle = fixture.createEntity(QStringLiteral("中"), 5, 50,
                                                root, 0);
    const QString leaf = fixture.createEntity(QStringLiteral("叶"), 6, 10,
                                              middle, 0);
    const QString other = fixture.createEntity(QStringLiteral("另一根"), 0, 100,
                                               QString(), 1);
    QVERIFY(!root.isEmpty() && !middle.isEmpty() && !leaf.isEmpty()
            && !other.isEmpty());

    // Default flat mode is unchanged: no nested children array.
    QJsonObject flat = fixture.call(QStringLiteral("entity.list"));
    QVERIFY2(isOk(flat), errorMessage(flat));
    QJsonArray flatEntities =
        dataOf(flat).value(QStringLiteral("entities")).toArray();
    QCOMPARE(flatEntities.size(), 4);
    for (const QJsonValue& value : flatEntities)
        QVERIFY(!value.toObject().contains(QStringLiteral("children")));

    QJsonObject tree = fixture.call(
        QStringLiteral("entity.list"),
        QJsonObject{{QStringLiteral("tree"), true}});
    QVERIFY2(isOk(tree), errorMessage(tree));
    QCOMPARE(dataOf(tree).value(QStringLiteral("page"))
                 .toObject()
                 .value(QStringLiteral("total"))
                 .toInt(),
             2);

    QJsonArray roots =
        dataOf(tree).value(QStringLiteral("entities")).toArray();
    QCOMPARE(roots.size(), 2);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("uuid")).toString(),
             root);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("empty"));

    const QJsonObject rootNode = roots.at(0).toObject();
    QCOMPARE(rootNode.value(QStringLiteral("children")).toArray().size(), 1);
    const QJsonObject middleNode =
        rootNode.value(QStringLiteral("children")).toArray().at(0).toObject();
    QCOMPARE(middleNode.value(QStringLiteral("uuid")).toString(), middle);
    QCOMPARE(middleNode.value(QStringLiteral("children")).toArray().size(), 1);
    const QJsonObject leafNode =
        middleNode.value(QStringLiteral("children")).toArray().at(0).toObject();
    QCOMPARE(leafNode.value(QStringLiteral("uuid")).toString(), leaf);
    QCOMPARE(leafNode.value(QStringLiteral("children")).toArray().size(), 0);

    // parentUuid starts the tree from that node's direct children.
    tree = fixture.call(
        QStringLiteral("entity.list"),
        QJsonObject{{QStringLiteral("tree"), true},
                    {QStringLiteral("parentUuid"), middle}});
    QVERIFY2(isOk(tree), errorMessage(tree));
    roots = dataOf(tree).value(QStringLiteral("entities")).toArray();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("uuid")).toString(),
             leaf);
}

void Mcp2Pr3Tests::entityListTreeCycleGuard()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    // A 40-level chain must come back fully nested in a single call.
    QJsonObject node{{QStringLiteral("name"), QStringLiteral("n39")},
                     {QStringLiteral("start"), 0},
                     {QStringLiteral("end"), 100}};
    for (int depth = 38; depth >= 0; --depth)
    {
        node = QJsonObject{
            {QStringLiteral("name"), QStringLiteral("n%1").arg(depth)},
            {QStringLiteral("start"), 0},
            {QStringLiteral("end"), 100},
            {QStringLiteral("children"), QJsonArray{node}}};
    }
    const QJsonObject created = fixture.call(
        QStringLiteral("entity.createTree"),
        QJsonObject{{QStringLiteral("root"), node}});
    QVERIFY2(isOk(created), errorMessage(created));

    QJsonObject tree = fixture.call(
        QStringLiteral("entity.list"),
        QJsonObject{{QStringLiteral("tree"), true}});
    QVERIFY2(isOk(tree), errorMessage(tree));
    QJsonObject current =
        dataOf(tree).value(QStringLiteral("entities")).toArray().at(0).toObject();
    int depth = 1;
    while (!current.value(QStringLiteral("children")).toArray().isEmpty())
    {
        current = current.value(QStringLiteral("children"))
                      .toArray()
                      .at(0)
                      .toObject();
        ++depth;
        QVERIFY2(depth <= 40, "tree walk did not terminate");
    }
    QCOMPARE(depth, 40);

    // Dirty data can contain a parent cycle. Starting the tree from one node of
    // the cycle must terminate and emit every node at most once.
    const QString a = fixture.createEntity(QStringLiteral("A"), 0, 100, QString(), 5);
    const QString b = fixture.createEntity(QStringLiteral("B"), 10, 90, a, 0);
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    fixture.store->mutableProject().entities[a].parentUuid = b;

    const QJsonObject cyclic = fixture.call(
        QStringLiteral("entity.list"),
        QJsonObject{{QStringLiteral("tree"), true},
                    {QStringLiteral("parentUuid"), b}});
    QVERIFY2(isOk(cyclic), errorMessage(cyclic));
    const QJsonArray cyclicRoots =
        dataOf(cyclic).value(QStringLiteral("entities")).toArray();
    QCOMPARE(cyclicRoots.size(), 1);
    const QJsonObject cyclicA = cyclicRoots.at(0).toObject();
    QCOMPARE(cyclicA.value(QStringLiteral("uuid")).toString(), a);
    // A -> B is emitted once; B's back-edge to A is dropped by visited instead
    // of recursing forever.
    const QJsonArray cyclicChildren =
        cyclicA.value(QStringLiteral("children")).toArray();
    QCOMPARE(cyclicChildren.size(), 1);
    QCOMPARE(cyclicChildren.at(0).toObject().value(QStringLiteral("uuid")).toString(),
             b);
    QCOMPARE(cyclicChildren.at(0).toObject()
                 .value(QStringLiteral("children"))
                 .toArray()
                 .size(),
             0);
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    Mcp2Pr3Tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mcp2_pr3_tests.moc"
