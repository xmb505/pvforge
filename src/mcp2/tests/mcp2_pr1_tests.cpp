// PR1 regressions for issue #146: flat entity.update content, aggregated
// missing keyframe animation parameters, case-insensitive easing names and
// negative rows on private timelines.  Kept in its own binary/file so the
// historical mcp2_tests.cpp size ratchet does not have to grow.
#include "mcp2/Service.h"

#include "mcp2_test_fixture.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace pvf;

namespace {

using pvf::mcp2test::errorCode;
using pvf::mcp2test::gitAvailable;

// The temp project keeps its historical name so failure output still names the
// suite that produced it.
struct Fixture : pvf::mcp2test::Fixture
{
    bool init()
    {
        return pvf::mcp2test::Fixture::init(QStringLiteral("Pr1.pvproj"));
    }
};

QJsonObject errorDetails(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("details"))
        .toObject();
}

QString errorMessage(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

QString uuidOf(const QJsonObject& result)
{
    return result.value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("uuid"))
        .toString();
}

QJsonObject entityArgs(const QString& name, int start, int end,
                       const QString& parent = QString())
{
    QJsonObject args;
    args.insert(QStringLiteral("name"), name);
    args.insert(QStringLiteral("start"), start);
    args.insert(QStringLiteral("end"), end);
    if (!parent.isEmpty())
        args.insert(QStringLiteral("parentUuid"), parent);
    return args;
}

QJsonObject vectorContent(int shape)
{
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("vector")},
        {QStringLiteral("vector"),
         QJsonObject{{QStringLiteral("shape"), shape},
                     {QStringLiteral("width"), 100.0},
                     {QStringLiteral("height"), 80.0},
                     {QStringLiteral("cornerRadius"), 0.0},
                     {QStringLiteral("strokeWidth"), 0.0}}}};
}

QJsonObject textContent()
{
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("text")},
        {QStringLiteral("text"),
         QJsonObject{{QStringLiteral("text"), QStringLiteral("A")},
                     {QStringLiteral("pixelSize"), 32}}}};
}

QJsonObject wholeKey(int frame, const QJsonObject& content,
                     const QString& easing = QStringLiteral("linear"))
{
    return QJsonObject{
        {QStringLiteral("frame"), frame},
        {QStringLiteral("x"), 0.0},
        {QStringLiteral("y"), 0.0},
        {QStringLiteral("rotation"), 0.0},
        {QStringLiteral("scaleX"), 1.0},
        {QStringLiteral("scaleY"), 1.0},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("visible"), true},
        {QStringLiteral("physicsInteract"), false},
        {QStringLiteral("content"), content},
        {QStringLiteral("easing"), easing},
    };
}

QJsonArray wholeKeys(std::initializer_list<QJsonObject> keyframes)
{
    QJsonArray array;
    for (const QJsonObject& keyframe : keyframes)
        array.append(keyframe);
    return array;
}

QJsonObject vectorAnimationParams()
{
    return QJsonObject{{QStringLiteral("width"), 120.0},
                       {QStringLiteral("height"), 90.0},
                       {QStringLiteral("cornerRadius"), 0.0},
                       {QStringLiteral("strokeWidth"), 0.0}};
}

QStringList textAnimationKeys()
{
    return {QStringLiteral("pixelSize"),    QStringLiteral("lineHeight"),
            QStringLiteral("letterSpacing"), QStringLiteral("padding"),
            QStringLiteral("boxWidth"),     QStringLiteral("boxHeight"),
            QStringLiteral("strokeWidth")};
}

QStringList arrayToStringList(const QJsonArray& array)
{
    QStringList list;
    for (const QJsonValue& value : array)
        list.append(value.toString());
    return list;
}

QString createVector(Fixture& fixture, int start = 0, int end = 60,
                     const QString& parent = QString())
{
    QJsonObject args = entityArgs(QStringLiteral("矢量"), start, end, parent);
    args.insert(QStringLiteral("content"), vectorContent(0));
    return uuidOf(fixture.call(QStringLiteral("entity.create"), args));
}

QString createText(Fixture& fixture, int start = 0, int end = 60,
                   const QString& parent = QString())
{
    QJsonObject args = entityArgs(QStringLiteral("文字"), start, end, parent);
    args.insert(QStringLiteral("content"), textContent());
    return uuidOf(fixture.call(QStringLiteral("entity.create"), args));
}

} // namespace

class Mcp2Pr1Tests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void pr1UpdateRejectsFlatContent();
    void pr1KeyframesMissingFieldsListedAtOnce();
    void pr1EasingNamesAreCaseInsensitive();
    void pr1PrivateTimelineAllowsNegativeRow();
    void pr1ReparentToMainTimelineDropsNegativeRow();
};

void Mcp2Pr1Tests::initTestCase()
{
    if (!gitAvailable())
        QSKIP(mcp2test::kSkipMessage);
}

void Mcp2Pr1Tests::pr1UpdateRejectsFlatContent()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    const QString vector = createVector(fixture);
    QVERIFY(!vector.isEmpty());

    QJsonObject result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{
            {QStringLiteral("uuid"), vector},
            {QStringLiteral("patch"),
             QJsonObject{{QStringLiteral("content"),
                          QJsonObject{{QStringLiteral("kind"),
                                       QStringLiteral("vector")},
                                      {QStringLiteral("shape"), 5}}}}}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    const QJsonObject details = errorDetails(result);
    QCOMPARE(details.value(QStringLiteral("expectedContentPath")).toString(),
             QStringLiteral("vector"));
    QCOMPARE(arrayToStringList(
                 details.value(QStringLiteral("unexpectedKeys")).toArray()),
             QStringList{QStringLiteral("shape")});
    QVERIFY(errorMessage(result).contains(QStringLiteral("content.vector")));
    // Rejected before the model merge, so the entity is untouched.
    QCOMPARE(fixture.store->project().entities.value(vector).content.vector.shape,
             0);

    // The nested form still updates the field.
    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{
            {QStringLiteral("uuid"), vector},
            {QStringLiteral("patch"),
             QJsonObject{{QStringLiteral("content"),
                          QJsonObject{{QStringLiteral("vector"),
                                       QJsonObject{{QStringLiteral("shape"), 5},
                                                   {QStringLiteral("width"),
                                                    123.0}}}}}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const model::Content& stored =
        fixture.store->project().entities.value(vector).content;
    QCOMPARE(stored.vector.shape, 5);
    QCOMPARE(stored.vector.width, 123.0);

    // Text content has the same nested contract.
    const QString text = createText(fixture);
    QVERIFY(!text.isEmpty());
    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{
            {QStringLiteral("uuid"), text},
            {QStringLiteral("patch"),
             QJsonObject{{QStringLiteral("content"),
                          QJsonObject{{QStringLiteral("kind"),
                                       QStringLiteral("text")},
                                      {QStringLiteral("pixelSize"),
                                       64.0}}}}}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QCOMPARE(errorDetails(result)
                 .value(QStringLiteral("expectedContentPath"))
                 .toString(),
             QStringLiteral("text"));
    QCOMPARE(arrayToStringList(errorDetails(result)
                                   .value(QStringLiteral("unexpectedKeys"))
                                   .toArray()),
             QStringList{QStringLiteral("pixelSize")});
    QCOMPARE(fixture.store->project()
                 .entities.value(text)
                 .content.text.pixelSize,
             32);

    // Non-object nested values are rejected as well.
    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{
            {QStringLiteral("uuid"), vector},
            {QStringLiteral("patch"),
             QJsonObject{{QStringLiteral("content"),
                          QJsonObject{{QStringLiteral("vector"), 5}}}}}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
}

void Mcp2Pr1Tests::pr1KeyframesMissingFieldsListedAtOnce()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    const QString text = createText(fixture);
    QVERIFY(!text.isEmpty());

    const QStringList required = textAnimationKeys();
    QJsonObject result = fixture.call(
        QStringLiteral("keyframes.set"),
        QJsonObject{{QStringLiteral("uuid"), text},
                    {QStringLiteral("keyframes"),
                     wholeKeys({wholeKey(0, QJsonObject())})}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QJsonObject details = errorDetails(result);
    QCOMPARE(arrayToStringList(
                 details.value(QStringLiteral("missingFields")).toArray()),
             required);
    QCOMPARE(arrayToStringList(
                 details.value(QStringLiteral("requiredFields")).toArray()),
             required);
    QVERIFY(errorMessage(result).contains(QStringLiteral("pixelSize")));
    QVERIFY(errorMessage(result).contains(QStringLiteral("strokeWidth")));
    QVERIFY(fixture.store->project().entities.value(text).keyframes.isEmpty());

    // One field supplied: the remaining six are reported together.
    QJsonObject partial;
    partial.insert(QStringLiteral("pixelSize"), 48.0);
    result = fixture.call(
        QStringLiteral("keyframes.setMany"),
        QJsonObject{{QStringLiteral("items"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("uuid"), text},
                         {QStringLiteral("keyframes"),
                          wholeKeys({wholeKey(0, partial)})}}}}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    details = errorDetails(result);
    const QStringList missing = arrayToStringList(
        details.value(QStringLiteral("missingFields")).toArray());
    QCOMPARE(missing.size(), required.size() - 1);
    QVERIFY(!missing.contains(QStringLiteral("pixelSize")));
    QVERIFY(missing.contains(QStringLiteral("lineHeight")));
    QVERIFY(missing.contains(QStringLiteral("strokeWidth")));
    QCOMPARE(arrayToStringList(
                 details.value(QStringLiteral("requiredFields")).toArray()),
             required);

    // Omitting content entirely reports the same full list.
    QJsonObject withoutContent = wholeKey(0, QJsonObject());
    withoutContent.remove(QStringLiteral("content"));
    result = fixture.call(
        QStringLiteral("keyframes.set"),
        QJsonObject{{QStringLiteral("uuid"), text},
                    {QStringLiteral("keyframes"),
                     wholeKeys({withoutContent})}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QCOMPARE(arrayToStringList(errorDetails(result)
                                   .value(QStringLiteral("missingFields"))
                                   .toArray()),
             required);
}

void Mcp2Pr1Tests::pr1EasingNamesAreCaseInsensitive()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    const QString vector = createVector(fixture);
    QVERIFY(!vector.isEmpty());

    const struct
    {
        const char* input;
        const char* canonical;
    } cases[] = {
        {"EaseOutCubic", "easeoutcubic"},
        {"easeoutcubic", "easeoutcubic"},
        {"easeInOut", "easeinoutquad"},
        {"easeinout", "easeinoutquad"},
        {"step", "instant"},
        {"Step", "instant"},
        {"EaseIn", "easeinquad"},
        {"linear", "linear"},
    };

    for (const auto& test : cases)
    {
        const QString input = QString::fromLatin1(test.input);
        QJsonObject result = fixture.call(
            QStringLiteral("keyframes.set"),
            QJsonObject{
                {QStringLiteral("uuid"), vector},
                {QStringLiteral("keyframes"),
                 wholeKeys({wholeKey(0, vectorAnimationParams(), input)})}});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(QStringLiteral("%1: %2").arg(input, errorCode(result))));
        result = fixture.call(QStringLiteral("keyframes.get"),
                              QJsonObject{{QStringLiteral("uuid"), vector}});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(errorCode(result)));
        const QJsonObject keyframe = result.value(QStringLiteral("data"))
                                         .toObject()
                                         .value(QStringLiteral("keyframes"))
                                         .toArray()
                                         .at(0)
                                         .toObject();
        QCOMPARE(keyframe.value(QStringLiteral("easing")).toString(),
                 QString::fromLatin1(test.canonical));
    }

    QJsonObject result = fixture.call(
        QStringLiteral("keyframes.set"),
        QJsonObject{
            {QStringLiteral("uuid"), vector},
            {QStringLiteral("keyframes"),
             wholeKeys({wholeKey(0, vectorAnimationParams(),
                                 QStringLiteral("EaseOutCubicX"))})}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    // schema.get and guide.get must expose the same complete whitelist.
    result = fixture.call(QStringLiteral("schema.get"),
                          QJsonObject{{QStringLiteral("topic"),
                                       QStringLiteral("keyframes")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const QString easingDescription = result.value(QStringLiteral("data"))
                                         .toObject()
                                         .value(QStringLiteral("schema"))
                                         .toObject()
                                         .value(QStringLiteral("properties"))
                                         .toObject()
                                         .value(QStringLiteral("easing"))
                                         .toObject()
                                         .value(QStringLiteral("description"))
                                         .toString();
    QVERIFY(easingDescription.contains(QStringLiteral("easeoutcubic")));
    QVERIFY(easingDescription.contains(QStringLiteral("easeinoutbounce")));

    result = fixture.call(QStringLiteral("guide.get"),
                          QJsonObject{{QStringLiteral("topic"),
                                       QStringLiteral("authoring")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const QString markdown = result.value(QStringLiteral("data"))
                                 .toObject()
                                 .value(QStringLiteral("markdown"))
                                 .toString();
    QVERIFY(markdown.contains(QStringLiteral("easeoutcubic")));
    QVERIFY(markdown.contains(QStringLiteral("easeinoutbounce")));
    QVERIFY(markdown.contains(QStringLiteral("easeinout")));
}

void Mcp2Pr1Tests::pr1PrivateTimelineAllowsNegativeRow()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    const QString parent = createVector(fixture, 0, 60);
    QVERIFY(!parent.isEmpty());

    // A child of a private timeline may sit below its parent.
    QJsonObject childArgs = entityArgs(QStringLiteral("负行子级"), 0, 60, parent);
    childArgs.insert(QStringLiteral("content"), vectorContent(0));
    childArgs.insert(QStringLiteral("row"), -2);
    QJsonObject result =
        fixture.call(QStringLiteral("entity.create"), childArgs);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const QString child = uuidOf(result);
    QCOMPARE(fixture.store->project().entities.value(child).row, -2);

    // The main timeline stays one-sided.
    QJsonObject rootArgs = entityArgs(QStringLiteral("负行根实体"), 0, 60);
    rootArgs.insert(QStringLiteral("content"), vectorContent(0));
    rootArgs.insert(QStringLiteral("row"), -1);
    result = fixture.call(QStringLiteral("entity.create"), rootArgs);
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    // move / update follow the same rule.
    result = fixture.call(QStringLiteral("entity.move"),
                          QJsonObject{{QStringLiteral("uuid"), child},
                                      {QStringLiteral("row"), -3}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().entities.value(child).row, -3);

    result = fixture.call(QStringLiteral("entity.move"),
                          QJsonObject{{QStringLiteral("uuid"), parent},
                                      {QStringLiteral("row"), -1}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("uuid"), child},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("row"), -4}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().entities.value(child).row, -4);

    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("uuid"), parent},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("row"), -1}}}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    // Out-of-range rows are still rejected on private timelines.
    result = fixture.call(QStringLiteral("entity.move"),
                          QJsonObject{{QStringLiteral("uuid"), child},
                                      {QStringLiteral("row"), -1000001}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
}

void Mcp2Pr1Tests::pr1ReparentToMainTimelineDropsNegativeRow()
{
    Fixture fixture;
    QVERIFY(fixture.init());
    const QString parent = createVector(fixture, 0, 60);
    QVERIFY(!parent.isEmpty());

    QJsonObject childArgs = entityArgs(QStringLiteral("负行子级"), 0, 60, parent);
    childArgs.insert(QStringLiteral("content"), vectorContent(0));
    childArgs.insert(QStringLiteral("row"), -5);
    const QString child = uuidOf(
        fixture.call(QStringLiteral("entity.create"), childArgs));
    QVERIFY(!child.isEmpty());
    QCOMPARE(fixture.store->project().entities.value(child).row, -5);

    // No explicit row: the main timeline default must not keep -5.
    QJsonObject result = fixture.call(
        QStringLiteral("entity.reparent"),
        QJsonObject{
            {QStringLiteral("uuid"), child},
            {QStringLiteral("parentUuid"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("mode"), QStringLiteral("keepLocal")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    const model::Entity& moved =
        fixture.store->project().entities.value(child);
    QCOMPARE(moved.parentUuid, QString());
    QVERIFY(moved.row >= 0);
    QVERIFY(moved.row != -5);

    // An explicit negative row on the main timeline is rejected and leaves the
    // entity on its private parent.
    QJsonObject secondArgs = entityArgs(QStringLiteral("另一个子级"), 0, 60, parent);
    secondArgs.insert(QStringLiteral("content"), vectorContent(0));
    secondArgs.insert(QStringLiteral("row"), -2);
    const QString second = uuidOf(
        fixture.call(QStringLiteral("entity.create"), secondArgs));
    QVERIFY(!second.isEmpty());

    result = fixture.call(
        QStringLiteral("entity.reparent"),
        QJsonObject{
            {QStringLiteral("uuid"), second},
            {QStringLiteral("parentUuid"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("mode"), QStringLiteral("keepLocal")},
            {QStringLiteral("row"), -1}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QCOMPARE(fixture.store->project().entities.value(second).parentUuid, parent);
    QCOMPARE(fixture.store->project().entities.value(second).row, -2);

    // A different private parent still accepts negative rows.
    result = fixture.call(
        QStringLiteral("entity.reparent"),
        QJsonObject{
            {QStringLiteral("uuid"), second},
            {QStringLiteral("parentUuid"), child},
            {QStringLiteral("mode"), QStringLiteral("keepLocal")},
            {QStringLiteral("row"), -7}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().entities.value(second).row, -7);
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    Mcp2Pr1Tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mcp2_pr1_tests.moc"
