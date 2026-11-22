// PR2 contract tests: tool self-description (animation parameter matrix,
// error-code catalogue, pitfalls guide) and the removal of the dead
// project.update `confirm` argument.  Kept in its own binary so the historical
// mcp2 test file stays under its size ratchet.
#include "mcp2/Service.h"

#include "mcp2_test_fixture.h"

#include "store/Store.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QtTest>

using namespace pvf;

using pvf::mcp2test::errorCode;
using Fixture = pvf::mcp2test::Fixture;

namespace {

QString errorMessage(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

QJsonObject findTool(const QJsonObject& tools, const QString& name)
{
    const QJsonArray array = tools.value(QStringLiteral("tools")).toArray();
    for (const QJsonValue& value : array)
    {
        const QJsonObject tool = value.toObject();
        if (tool.value(QStringLiteral("name")).toString() == name)
            return tool;
    }
    return QJsonObject();
}

QJsonObject schemaOf(Fixture& fixture, const QString& topic)
{
    const QJsonObject result = fixture.call(
        QStringLiteral("schema.get"),
        QJsonObject{{QStringLiteral("topic"), topic}});
    if (!result.value(QStringLiteral("ok")).toBool())
        return QJsonObject();
    return result.value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("schema"))
        .toObject();
}

// Every animation parameter name that must appear in the keyframes.*
// descriptions.  strokeWidth is shared by text and vector but only needs to be
// present once in the prose; the list stays unique so a missing field anywhere
// still fails.
const QStringList& animationFieldNames()
{
    static const QStringList names = {
        QStringLiteral("pixelSize"),    QStringLiteral("lineHeight"),
        QStringLiteral("letterSpacing"), QStringLiteral("padding"),
        QStringLiteral("boxWidth"),     QStringLiteral("boxHeight"),
        QStringLiteral("strokeWidth"),  QStringLiteral("width"),
        QStringLiteral("height"),       QStringLiteral("cornerRadius"),
        QStringLiteral("volume"),
    };
    return names;
}

} // namespace

class Mcp2Pr2Tests : public QObject
{
    Q_OBJECT

private slots:
    void toolsListDocumentsKeyframeAnimationParams();
    void errorsSchemaListsAllCodes();
    void guidePitfallsTopicAvailable();
    void projectUpdateRejectsDeadConfirm();
};

void Mcp2Pr2Tests::toolsListDocumentsKeyframeAnimationParams()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QJsonObject tools = fixture.service->listTools();
    const QStringList setters = {QStringLiteral("keyframes.set"),
                                 QStringLiteral("keyframes.setMany")};
    for (const QString& name : setters)
    {
        const QJsonObject tool = findTool(tools, name);
        QVERIFY2(!tool.isEmpty(), qPrintable(QStringLiteral("缺少工具 %1").arg(name)));
        const QString description =
            tool.value(QStringLiteral("description")).toString();
        for (const QString& field : animationFieldNames())
        {
            QVERIFY2(description.contains(field),
                     qPrintable(QStringLiteral("%1 描述缺少动画参数 %2：%3")
                                    .arg(name, field, description)));
        }
        QVERIFY2(description.contains(QStringLiteral("text")),
                 qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("vector")),
                 qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("audio")),
                 qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("image/video/html/camera/empty")),
                 qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("htmlVars")),
                 qPrintable(description));
    }

    // schema.get must carry the same matrix: both the keyframes topic and the
    // entity schema's embedded keyframes array.
    const QJsonObject keyframesSchema = schemaOf(fixture, QStringLiteral("keyframes"));
    QVERIFY(!keyframesSchema.isEmpty());
    const QString contentDescription =
        keyframesSchema.value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("content")).toObject()
            .value(QStringLiteral("description")).toString();
    for (const QString& field : animationFieldNames())
        QVERIFY2(contentDescription.contains(field), qPrintable(contentDescription));

    const QJsonObject entitySchema = schemaOf(fixture, QStringLiteral("entity"));
    QVERIFY(!entitySchema.isEmpty());
    const QString entityKeyframesDescription =
        entitySchema.value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("keyframes")).toObject()
            .value(QStringLiteral("description")).toString();
    for (const QString& field : animationFieldNames())
    {
        QVERIFY2(entityKeyframesDescription.contains(field),
                 qPrintable(entityKeyframesDescription));
    }
}

void Mcp2Pr2Tests::errorsSchemaListsAllCodes()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QJsonObject schema = schemaOf(fixture, QStringLiteral("errors"));
    QVERIFY(!schema.isEmpty());
    const QJsonArray codeArray = schema.value(QStringLiteral("codes")).toArray();
    QStringList actual;
    for (const QJsonValue& value : codeArray)
        actual << value.toString();
    QCOMPARE(actual.size(), 9);

    const QStringList expected = {
        QStringLiteral("invalid_argument"),   QStringLiteral("conflict"),
        QStringLiteral("internal_error"),     QStringLiteral("limit_exceeded"),
        QStringLiteral("confirm_required"),   QStringLiteral("not_available"),
        QStringLiteral("permission_denied"),  QStringLiteral("stopped_by_human"),
        QStringLiteral("request_timeout"),
    };
    QStringList sortedActual = actual;
    QStringList sortedExpected = expected;
    sortedActual.sort();
    sortedExpected.sort();
    QCOMPARE(sortedActual, sortedExpected);
    QVERIFY2(actual.contains(QStringLiteral("request_timeout")),
             qPrintable(actual.join(QStringLiteral(", "))));

    // schema.get topic=all must embed the same complete list.
    const QJsonObject all = schemaOf(fixture, QStringLiteral("all"));
    QVERIFY(!all.isEmpty());
    QCOMPARE(all.value(QStringLiteral("errors")).toObject()
                 .value(QStringLiteral("codes")).toArray().size(),
             9);
}

void Mcp2Pr2Tests::guidePitfallsTopicAvailable()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    // The topic is advertised by tools/list so an agent can discover it.
    const QJsonObject guideTool =
        findTool(fixture.service->listTools(), QStringLiteral("guide.get"));
    QVERIFY(!guideTool.isEmpty());
    const QString topicDescription =
        guideTool.value(QStringLiteral("inputSchema")).toObject()
            .value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("topic")).toObject()
            .value(QStringLiteral("description")).toString();
    QVERIFY2(topicDescription.contains(QStringLiteral("pitfalls")),
             qPrintable(topicDescription));

    const QJsonObject result = fixture.call(
        QStringLiteral("guide.get"),
        QJsonObject{{QStringLiteral("topic"), QStringLiteral("pitfalls")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const QString markdown = result.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("markdown")).toString();
    QVERIFY(!markdown.trimmed().isEmpty());
    QVERIFY(markdown.size() > 200);

    // Each documented pitfall must actually be covered; these keywords match
    // the post-PR1 behaviour the guide promises.
    for (const QString& keyword :
         {QStringLiteral("entity.update"), QStringLiteral("assetType"),
          QStringLiteral("audioAsset"), QStringLiteral("fadeIn"),
          QStringLiteral("fadeOut"), QStringLiteral("project.update"),
          QStringLiteral("keyframes.setMany"), QStringLiteral("asset.assign"),
          QStringLiteral("not_available"), QStringLiteral("request_timeout")})
    {
        QVERIFY2(markdown.contains(keyword),
                 qPrintable(QStringLiteral("pitfalls 缺少 %1").arg(keyword)));
    }
    QVERIFY2(markdown.contains(QStringLiteral("row")), qPrintable(markdown));

    // Unknown topics still fail cleanly.
    const QJsonObject unknown = fixture.call(
        QStringLiteral("guide.get"),
        QJsonObject{{QStringLiteral("topic"), QStringLiteral("no-such-topic")}});
    QCOMPARE(errorCode(unknown), QStringLiteral("invalid_argument"));
}

void Mcp2Pr2Tests::projectUpdateRejectsDeadConfirm()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    // The dead argument is gone from both the tool schema and the data schema.
    const QJsonObject tool =
        findTool(fixture.service->listTools(), QStringLiteral("project.update"));
    QVERIFY(!tool.isEmpty());
    const QJsonObject toolProperties =
        tool.value(QStringLiteral("inputSchema")).toObject()
            .value(QStringLiteral("properties")).toObject();
    QVERIFY(!toolProperties.contains(QStringLiteral("confirm")));

    const QJsonObject projectSchema = schemaOf(fixture, QStringLiteral("project"));
    QVERIFY(!projectSchema.value(QStringLiteral("properties")).toObject()
                 .contains(QStringLiteral("confirm")));

    // Passing it is an argument error, even alongside an otherwise valid field.
    for (const QJsonObject& args :
         {QJsonObject{{QStringLiteral("confirm"), true}},
          QJsonObject{{QStringLiteral("name"), QStringLiteral("改名")},
                      {QStringLiteral("confirm"), true}}})
    {
        const QJsonObject result =
            fixture.call(QStringLiteral("project.update"), args);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
        QVERIFY2(errorMessage(result).contains(QStringLiteral("confirm")),
                 qPrintable(errorMessage(result)));
    }
    // Rejected calls must not have changed the project name.
    QCOMPARE(fixture.store->project().settings.name,
             model::ProjectSettings().name);

    // The whitelist still accepts the supported fields.
    const QJsonObject renamed = fixture.call(
        QStringLiteral("project.update"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("新名字")}});
    QVERIFY2(renamed.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(renamed)));
    QCOMPARE(fixture.store->project().settings.name, QStringLiteral("新名字"));
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    Mcp2Pr2Tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mcp2_pr2_tests.moc"
