#include "mcp2/Service.h"

#include "model/Model.h"
#include "store/Store.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <memory>
#include <optional>

using namespace pvf;

namespace {

QString lfsBinDir()
{
    return QStringLiteral("/tmp/opencode/gitlfs/root/usr/bin");
}

bool gitLfsAvailable()
{
    QProcess process;
    process.start(QStringLiteral("git"),
                  {QStringLiteral("lfs"), QStringLiteral("version")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
        return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

struct Fixture
{
    QTemporaryDir temp;
    std::optional<store::ProjectStore> store;
    std::unique_ptr<mcp2::Service> service;

    bool init()
    {
        if (!temp.isValid())
            return false;
        QString error;
        model::ProjectSettings settings;
        auto created = store::ProjectStore::create(temp.filePath("Demo.pvproj"),
                                                   settings, &error);
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

    int historySize() { return store->history(0).size(); }
};

QString errorCode(const QJsonObject& result)
{
    return result.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
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
                       const Uuid& parent = QString())
{
    QJsonObject args;
    args.insert(QStringLiteral("name"), name);
    args.insert(QStringLiteral("start"), start);
    args.insert(QStringLiteral("end"), end);
    if (!parent.isEmpty())
        args.insert(QStringLiteral("parentUuid"), parent);
    return args;
}

QJsonArray keys(std::initializer_list<QPair<int, double>> values)
{
    QJsonArray array;
    for (const auto& value : values)
    {
        QJsonObject key;
        key.insert(QStringLiteral("frame"), value.first);
        key.insert(QStringLiteral("value"), value.second);
        array.append(key);
    }
    return array;
}

constexpr char kSkipMessage[] = "git/git-lfs 不可用，跳过依赖 git 的测试";

} // namespace

class Mcp2Tests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void createTreeOneCommit();
    void keyframesSetRespectsDuration();
    void solverDrivenTrackRejectsNormalKeys();
    void trimSolverRules();
    void moveTrimReparentValidation();
    void reparentPreserveWorldKeepsPosition();
    void lockedEntityRejectsWrite();
    void deleteRequiresConfirm();
    void assetImportDeduplicates();
    void markersSetListRemove();
    void historyUndoRedo();
    void defaultRowIsHighestPlusOne();
    void twoCallsTwoCommits();
    void createManyRollsBack();
    void keyframesSetManyRollsBack();
    void assetAssignAndUnassign();
    void duplicateSubtreeOneCommit();
    void projectUpdateFpsNeedsConfirm();
    void deleteManyMergesHierarchy();
    void listToolsAndSchema();
    void solverCreateUpdateDelete();
    void solverValidationRejects();
    void viewStateReturnsInjected();
    void previewRenderFrameWritesPng();
    void previewRejectsOversize();
    void solverEvaluateNestedEntity();
};

void Mcp2Tests::initTestCase()
{
    const QFileInfo lfs(lfsBinDir());
    if (lfs.isDir())
    {
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", lfs.absoluteFilePath().toUtf8() + ':' + path);
    }
    if (!gitLfsAvailable())
        QSKIP(kSkipMessage);
}

void Mcp2Tests::createTreeOneCommit()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonArray children;
    children.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("背景")},
                                {QStringLiteral("start"), 0},
                                {QStringLiteral("end"), 60},
                                {QStringLiteral("row"), 0}});
    children.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("标题")},
                                {QStringLiteral("start"), 0},
                                {QStringLiteral("end"), 60},
                                {QStringLiteral("row"), 1}});
    children.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("进度条")},
                                {QStringLiteral("start"), 0},
                                {QStringLiteral("end"), 60},
                                {QStringLiteral("row"), 2},
                                {QStringLiteral("keyframes"),
                                 QJsonObject{{QStringLiteral("scaleX"),
                                              keys({{0, 0.0}, {59, 1.0}})}}}});

    QJsonObject root;
    root.insert(QStringLiteral("name"), QStringLiteral("卡片"));
    root.insert(QStringLiteral("start"), 0);
    root.insert(QStringLiteral("end"), 60);
    root.insert(QStringLiteral("children"), children);

    const QJsonObject result =
        fixture.call(QStringLiteral("entity.createTree"),
                     QJsonObject{{QStringLiteral("root"), root}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString()));

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("entityCount")).toInt(), 4);
    QCOMPARE(data.value(QStringLiteral("uuids")).toArray().size(), 4);
    QCOMPARE(data.value(QStringLiteral("mapping")).toArray().size(), 4);
    QVERIFY(!data.value(QStringLiteral("rootUuid")).toString().isEmpty());
    QCOMPARE(fixture.store->project().entities.size(), 4);
    QCOMPARE(fixture.historySize(), 2); // init + one tree commit
    QVERIFY(result.value(QStringLiteral("commit"))
                .toObject()
                .value(QStringLiteral("hash"))
                .toString()
                .size()
            == 40);
}

void Mcp2Tests::keyframesSetRespectsDuration()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString uuid = uuidOf(fixture.call(QStringLiteral("entity.create"),
                                             entityArgs(QStringLiteral("实体"), 0, 30)));
    QVERIFY(!uuid.isEmpty());

    QJsonObject args;
    args.insert(QStringLiteral("uuid"), uuid);
    args.insert(QStringLiteral("track"), QStringLiteral("x"));
    args.insert(QStringLiteral("keys"), keys({{0, 0.0}, {29, 5.0}}));
    QJsonObject result = fixture.call(QStringLiteral("keyframes.set"), args);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error"))
                            .toObject()
                            .value(QStringLiteral("message"))
                            .toString()));
    QCOMPARE(result.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             2);

    args.insert(QStringLiteral("keys"), keys({{0, 0.0}, {30, 5.0}}));
    result = fixture.call(QStringLiteral("keyframes.set"), args);
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    const QJsonObject range =
        result.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("details"))
            .toObject()
            .value(QStringLiteral("allowedRange"))
            .toObject();
    QCOMPARE(range.value(QStringLiteral("min")).toInt(), 0);
    QCOMPARE(range.value(QStringLiteral("max")).toInt(), 29);

    // Duplicate frames are rejected.
    args.insert(QStringLiteral("keys"), keys({{1, 0.0}, {1, 1.0}}));
    result = fixture.call(QStringLiteral("keyframes.set"), args);
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    // Trimming start shifts keyframes and drops the ones that fall outside.
    const QString trimUuid =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("裁剪"), 0, 60)));
    QJsonObject trimKeys;
    trimKeys.insert(QStringLiteral("uuid"), trimUuid);
    trimKeys.insert(QStringLiteral("track"), QStringLiteral("x"));
    trimKeys.insert(QStringLiteral("keys"), keys({{0, 0.0}, {10, 5.0}, {30, 9.0}}));
    QVERIFY(fixture.call(QStringLiteral("keyframes.set"), trimKeys)
                .value(QStringLiteral("ok"))
                .toBool());

    QJsonObject trim;
    trim.insert(QStringLiteral("uuid"), trimUuid);
    trim.insert(QStringLiteral("start"), 10);
    result = fixture.call(QStringLiteral("entity.trim"), trim);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("removedKeyframes"))
                 .toInt(),
             1);

    result = fixture.call(QStringLiteral("keyframes.get"),
                          QJsonObject{{QStringLiteral("uuid"), trimUuid},
                                      {QStringLiteral("track"), QStringLiteral("x")}});
    const QJsonArray remaining =
        result.value(QStringLiteral("data")).toObject().value(QStringLiteral("keys")).toArray();
    QCOMPARE(remaining.size(), 2);
    QCOMPARE(remaining.at(0).toObject().value(QStringLiteral("frame")).toInt(), 0);
    QCOMPARE(remaining.at(1).toObject().value(QStringLiteral("frame")).toInt(), 20);
}

void Mcp2Tests::solverDrivenTrackRejectsNormalKeys()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString target =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("地面"), 0, 60)));
    const QString ball =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("小球"), 0, 60)));
    QVERIFY(!target.isEmpty());
    QVERIFY(!ball.isEmpty());

    model::Entity& entity = fixture.store->mutableProject().entities[ball];
    model::Track xTrack;
    model::Keyframe xKey;
    xKey.frame = 0;
    xKey.value = -360;
    xTrack.keys.append(xKey);
    entity.tracks.insert(QStringLiteral("x"), xTrack);
    entity.tracks.insert(QStringLiteral("y"), xTrack);

    model::SolverSegment segment;
    segment.id = model::newUuid();
    segment.effect = SolverEffect::Gravity;
    segment.startFrame = 0;
    segment.endFrame = 30;
    segment.params.insert(QStringLiteral("targetUuid"), target);
    entity.solvers.append(segment);

    QString error;
    QVERIFY2(fixture.store->saveEntity(ball, &error), qPrintable(error));

    QJsonObject args;
    args.insert(QStringLiteral("uuid"), ball);
    args.insert(QStringLiteral("track"), QStringLiteral("x"));

    args.insert(QStringLiteral("keys"), keys({{0, -360.0}, {10, -300.0}}));
    QJsonObject result = fixture.call(QStringLiteral("keyframes.set"), args);
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
    QVERIFY(!result.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("details"))
                 .toObject()
                 .value(QStringLiteral("conflicts"))
                 .toArray()
                 .isEmpty());

    // Only the segment start frame is allowed.
    args.insert(QStringLiteral("keys"), keys({{0, -360.0}}));
    result = fixture.call(QStringLiteral("keyframes.set"), args);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    // Removing the start key destroys the initial state and is refused.
    result = fixture.call(QStringLiteral("keyframes.remove"),
                          QJsonObject{{QStringLiteral("uuid"), ball},
                                      {QStringLiteral("track"), QStringLiteral("x")},
                                      {QStringLiteral("frame"), 0}});
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
}

void Mcp2Tests::trimSolverRules()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString target =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("地面"), 0, 60)));
    const QString ball =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("小球"), 0, 60)));
    QVERIFY(!target.isEmpty() && !ball.isEmpty());

    {
        model::Entity& entity = fixture.store->mutableProject().entities[ball];
        model::Track track;
        model::Keyframe key;
        key.frame = 0;
        key.value = -360;
        track.keys.append(key);
        entity.tracks.insert(QStringLiteral("x"), track);
        entity.tracks.insert(QStringLiteral("y"), track);
        model::SolverSegment segment;
        segment.id = model::newUuid();
        segment.effect = SolverEffect::Gravity;
        segment.startFrame = 0;
        segment.endFrame = 40;
        segment.params.insert(QStringLiteral("targetUuid"), target);
        entity.solvers.append(segment);
        QString error;
        QVERIFY2(fixture.store->saveEntity(ball, &error), qPrintable(error));
    }

    QJsonObject result =
        fixture.call(QStringLiteral("entity.trim"),
                     QJsonObject{{QStringLiteral("uuid"), ball},
                                 {QStringLiteral("start"), 10}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString()));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("removedSolvers")).toArray().size(),
             1);

    const model::Entity& trimmed = fixture.store->project().entities.value(ball);
    QVERIFY(trimmed.solvers.isEmpty());
    const auto xTrack = trimmed.tracks.value(QStringLiteral("x")).keys;
    QCOMPARE(xTrack.size(), 1);
    QCOMPARE(xTrack.first().frame, 0);
    QCOMPARE(xTrack.first().value, -360.0);

    // A trim that cuts into the end keeps the segment but freezes it at the
    // new clip end instead of dropping the marker.
    const QString ball2 =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("小球二"), 0, 60)));
    {
        model::Entity& entity = fixture.store->mutableProject().entities[ball2];
        model::Track track;
        model::Keyframe key;
        key.frame = 0;
        key.value = 0;
        track.keys.append(key);
        entity.tracks.insert(QStringLiteral("x"), track);
        entity.tracks.insert(QStringLiteral("y"), track);
        model::SolverSegment segment;
        segment.id = model::newUuid();
        segment.effect = SolverEffect::Gravity;
        segment.startFrame = 0;
        segment.endFrame = -1;
        segment.params.insert(QStringLiteral("targetUuid"), target);
        entity.solvers.append(segment);
        QString error;
        QVERIFY2(fixture.store->saveEntity(ball2, &error), qPrintable(error));
    }

    result = fixture.call(QStringLiteral("entity.trim"),
                          QJsonObject{{QStringLiteral("uuid"), ball2},
                                      {QStringLiteral("end"), 30}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString()));
    const model::Entity& frozen = fixture.store->project().entities.value(ball2);
    QCOMPARE(frozen.solvers.size(), 1);
    QCOMPARE(frozen.solvers.first().endFrame, 29);
}

void Mcp2Tests::moveTrimReparentValidation()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString root =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("组"), 0, 120)));
    const QString child =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("子"), 20, 80, root)));
    QVERIFY(!root.isEmpty());
    QVERIFY(!child.isEmpty());

    QJsonObject result = fixture.call(
        QStringLiteral("entity.move"),
        QJsonObject{{QStringLiteral("uuid"), root}, {QStringLiteral("delta"), 10}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("childrenMoved"))
                 .toArray()
                 .size(),
             1);

    result = fixture.call(QStringLiteral("entity.get"),
                          QJsonObject{{QStringLiteral("uuid"), child}});
    QCOMPARE(result.value(QStringLiteral("data")).toObject().value(QStringLiteral("start")).toInt(),
             30);

    result = fixture.call(QStringLiteral("entity.move"),
                          QJsonObject{{QStringLiteral("uuid"), root},
                                      {QStringLiteral("start"), 20},
                                      {QStringLiteral("delta"), 10}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    // Trimming the parent cascades into the child.
    result = fixture.call(QStringLiteral("entity.trim"),
                          QJsonObject{{QStringLiteral("uuid"), root},
                                      {QStringLiteral("end"), 40}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    result = fixture.call(QStringLiteral("entity.get"),
                          QJsonObject{{QStringLiteral("uuid"), child}});
    QCOMPARE(result.value(QStringLiteral("data")).toObject().value(QStringLiteral("start")).toInt(),
             30);
    QCOMPARE(result.value(QStringLiteral("data")).toObject().value(QStringLiteral("end")).toInt(),
             40);

    // A second trim would delete the child and therefore needs confirm.
    result = fixture.call(QStringLiteral("entity.trim"),
                          QJsonObject{{QStringLiteral("uuid"), root},
                                      {QStringLiteral("end"), 20}});
    QCOMPARE(errorCode(result), QStringLiteral("confirm_required"));
    QVERIFY(fixture.store->project().entities.contains(child));

    result = fixture.call(QStringLiteral("entity.trim"),
                          QJsonObject{{QStringLiteral("uuid"), root},
                                      {QStringLiteral("end"), 20},
                                      {QStringLiteral("confirm"), true}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QVERIFY(!fixture.store->project().entities.contains(child));

    // Reparent validation: cycle, missing mode, and a valid move.
    const QString a =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("A"), 0, 100)));
    const QString b =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("B"), 0, 100)));
    const QString c =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("C"), 10, 90, a)));
    const QString g =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("G"), 20, 30, c)));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty() && !g.isEmpty());

    result = fixture.call(QStringLiteral("entity.reparent"),
                          QJsonObject{{QStringLiteral("uuid"), c},
                                      {QStringLiteral("parentUuid"), g},
                                      {QStringLiteral("mode"), QStringLiteral("keepLocal")}});
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));

    result = fixture.call(QStringLiteral("entity.reparent"),
                          QJsonObject{{QStringLiteral("uuid"), c},
                                      {QStringLiteral("parentUuid"), b}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(QStringLiteral("entity.reparent"),
                          QJsonObject{{QStringLiteral("uuid"), c},
                                      {QStringLiteral("parentUuid"), b},
                                      {QStringLiteral("mode"), QStringLiteral("keepLocal")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().entities.value(c).parentUuid, b);
}

void Mcp2Tests::reparentPreserveWorldKeepsPosition()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonObject parentArgs = entityArgs(QStringLiteral("父"), 0, 100);
    parentArgs.insert(QStringLiteral("transform"),
                      QJsonObject{{QStringLiteral("x"), 100.0}});
    const QString parent = uuidOf(fixture.call(QStringLiteral("entity.create"), parentArgs));
    QVERIFY(!parent.isEmpty());

    QJsonObject childArgs = entityArgs(QStringLiteral("子"), 0, 100, parent);
    childArgs.insert(QStringLiteral("transform"),
                     QJsonObject{{QStringLiteral("x"), 10.0}});
    const QString child = uuidOf(fixture.call(QStringLiteral("entity.create"), childArgs));

    QJsonObject result = fixture.call(
        QStringLiteral("entity.reparent"),
        QJsonObject{{QStringLiteral("uuid"), child},
                    {QStringLiteral("parentUuid"), QJsonValue(QJsonValue::Null)},
                    {QStringLiteral("mode"), QStringLiteral("preserveWorld")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    const QJsonObject transform =
        result.value(QStringLiteral("data")).toObject().value(QStringLiteral("transform")).toObject();
    QVERIFY(std::abs(transform.value(QStringLiteral("x")).toDouble() - 110.0) < 1e-6);
    QCOMPARE(fixture.store->project().entities.value(child).parentUuid, QString());
}

void Mcp2Tests::lockedEntityRejectsWrite()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const auto expectLocked = [](const QJsonObject& result,
                                 const QString& source) {
        QCOMPARE(errorCode(result), QStringLiteral("conflict"));
        QCOMPARE(result.value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("details")).toObject()
                     .value(QStringLiteral("lockedBy")).toString(),
                 source);
    };

    QJsonObject args = entityArgs(QStringLiteral("锁住"), 0, 60);
    args.insert(QStringLiteral("locked"), true);
    const QString locked = uuidOf(fixture.call(QStringLiteral("entity.create"), args));
    QVERIFY(!locked.isEmpty());

    QJsonObject result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("uuid"), locked},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("name"), QStringLiteral("新名字")}}}});
    expectLocked(result, locked);

    result = fixture.call(QStringLiteral("entity.create"),
                          entityArgs(QStringLiteral("子"), 0, 60, locked));
    expectLocked(result, locked);

    result = fixture.call(QStringLiteral("keyframes.set"),
                          QJsonObject{{QStringLiteral("uuid"), locked},
                                      {QStringLiteral("track"), QStringLiteral("x")},
                                      {QStringLiteral("keys"), keys({{0, 1.0}})}});
    expectLocked(result, locked);

    result = fixture.call(QStringLiteral("entity.move"),
                          QJsonObject{{QStringLiteral("uuid"), locked},
                                      {QStringLiteral("delta"), 5}});
    expectLocked(result, locked);

    result = fixture.call(QStringLiteral("asset.unassign"),
                          QJsonObject{{QStringLiteral("entityUuid"), locked}});
    expectLocked(result, locked);

    result = fixture.call(QStringLiteral("entity.delete"),
                          QJsonObject{{QStringLiteral("uuid"), locked},
                                      {QStringLiteral("confirm"), true}});
    expectLocked(result, locked);
    QVERIFY(fixture.store->project().entities.contains(locked));

    // An unlocked child inherits the lock from its parent; lockedBy points at
    // the parent, not the child.
    const QString parent =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("父级"), 0, 120)));
    const QString child =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("子级"), 0, 120, parent)));
    QVERIFY(!parent.isEmpty() && !child.isEmpty());

    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("uuid"), parent},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("locked"), true}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    result = fixture.call(
        QStringLiteral("entity.update"),
        QJsonObject{{QStringLiteral("uuid"), child},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("name"), QStringLiteral("改子级")}}}});
    expectLocked(result, parent);

    result = fixture.call(QStringLiteral("keyframes.set"),
                          QJsonObject{{QStringLiteral("uuid"), child},
                                      {QStringLiteral("track"), QStringLiteral("x")},
                                      {QStringLiteral("keys"), keys({{0, 2.0}})}});
    expectLocked(result, parent);

    result = fixture.call(QStringLiteral("entity.create"),
                          entityArgs(QStringLiteral("孙级"), 0, 120, child));
    expectLocked(result, parent);
}

void Mcp2Tests::deleteRequiresConfirm()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString parent =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("父"), 0, 60)));
    const QString child =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("子"), 0, 60, parent)));

    QJsonObject result = fixture.call(QStringLiteral("entity.delete"),
                                      QJsonObject{{QStringLiteral("uuid"), parent}});
    QCOMPARE(errorCode(result), QStringLiteral("confirm_required"));
    QCOMPARE(result.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("details"))
                 .toObject()
                 .value(QStringLiteral("affected"))
                 .toObject()
                 .value(QStringLiteral("deletedUuids"))
                 .toArray()
                 .size(),
             2);
    QVERIFY(fixture.store->project().entities.contains(parent));
    QVERIFY(fixture.store->project().entities.contains(child));

    result = fixture.call(QStringLiteral("entity.delete"),
                          QJsonObject{{QStringLiteral("uuid"), parent},
                                      {QStringLiteral("confirm"), true}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("deletedUuids"))
                 .toArray()
                 .size(),
             2);
    QVERIFY(fixture.store->project().entities.isEmpty());
}

void Mcp2Tests::assetImportDeduplicates()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString imagePath = fixture.temp.filePath(QStringLiteral("pic.png"));
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    QJsonObject result =
        fixture.call(QStringLiteral("asset.import"),
                     QJsonObject{{QStringLiteral("path"), imagePath},
                                 {QStringLiteral("name"), QStringLiteral("红图")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error"))
                            .toObject()
                            .value(QStringLiteral("message"))
                            .toString()));
    QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("deduplicated")).toBool(), false);
    const QString hash = data.value(QStringLiteral("asset")).toObject()
                             .value(QStringLiteral("hash")).toString();
    QCOMPARE(hash.size(), 64);
    QVERIFY(!result.value(QStringLiteral("commit")).toObject()
                 .value(QStringLiteral("hash")).toString().isEmpty());

    result = fixture.call(QStringLiteral("asset.import"),
                          QJsonObject{{QStringLiteral("path"), imagePath},
                                      {QStringLiteral("name"), QStringLiteral("另一个名字")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("deduplicated")).toBool(), true);
    QCOMPARE(data.value(QStringLiteral("asset")).toObject().value(QStringLiteral("hash")).toString(),
             hash);
    // Deduplication keeps the first name.
    QCOMPARE(data.value(QStringLiteral("asset")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("红图"));

    result = fixture.call(QStringLiteral("asset.list"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("assets"))
                 .toArray()
                 .size(),
             1);
}

void Mcp2Tests::markersSetListRemove()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonObject result = fixture.call(
        QStringLiteral("markers.set"),
        QJsonObject{{QStringLiteral("frame"), 48},
                    {QStringLiteral("title"), QStringLiteral("副歌")},
                    {QStringLiteral("note"), QStringLiteral("音乐进入副歌")},
                    {QStringLiteral("tags"), QJsonArray{QStringLiteral("music")}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QJsonObject marker = result.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("marker")).toObject();
    const QString id = marker.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("created")).toBool(),
             true);

    result = fixture.call(QStringLiteral("markers.list"));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("markers")).toArray().size(),
             1);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("page")).toObject()
                 .value(QStringLiteral("total")).toInt(),
             1);

    result = fixture.call(QStringLiteral("markers.set"),
                          QJsonObject{{QStringLiteral("id"), id},
                                      {QStringLiteral("frame"), 60},
                                      {QStringLiteral("title"), QStringLiteral("副歌 2")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("created")).toBool(),
             false);
    QCOMPARE(fixture.store->project().markers.first().frame, 60);
    QCOMPARE(fixture.store->project().markers.first().title, QStringLiteral("副歌 2"));

    result = fixture.call(QStringLiteral("markers.remove"),
                          QJsonObject{{QStringLiteral("id"), id}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QVERIFY(fixture.store->project().markers.isEmpty());
}

void Mcp2Tests::historyUndoRedo()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString uuid =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("甲"), 0, 30)));
    QVERIFY(!uuid.isEmpty());
    QCOMPARE(fixture.historySize(), 2);

    QJsonObject result = fixture.call(QStringLiteral("history.undo"),
                                      QJsonObject{{QStringLiteral("steps"), 1}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("reverted")).toArray().size(),
             1);
    QVERIFY(fixture.store->project().entities.isEmpty());
    QVERIFY(!result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("head")).toString().isEmpty());

    result = fixture.call(QStringLiteral("history.redo"),
                          QJsonObject{{QStringLiteral("steps"), 1}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().entities.size(), 1);
    QCOMPARE(fixture.store->project().entities.value(uuid).name, QStringLiteral("甲"));

    result = fixture.call(QStringLiteral("history.log"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QVERIFY(result.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("commits")).toArray().size()
            >= 3);
}

void Mcp2Tests::defaultRowIsHighestPlusOne()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    auto create = [&fixture](const QString& name, int start, int end, int row) {
        QJsonObject args = entityArgs(name, start, end);
        if (row >= 0)
            args.insert(QStringLiteral("row"), row);
        return fixture.call(QStringLiteral("entity.create"), args);
    };

    QJsonObject result = create(QStringLiteral("一"), 0, 30, -1);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("row")).toInt(),
             0);
    result = create(QStringLiteral("二"), 0, 30, -1);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("row")).toInt(),
             1);
    result = create(QStringLiteral("三"), 40, 60, -1);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("row")).toInt(),
             0);
    result = create(QStringLiteral("四"), 0, 30, 5);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("row")).toInt(),
             5);
    result = create(QStringLiteral("五"), 0, 30, -1);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("row")).toInt(),
             6);
}

void Mcp2Tests::twoCallsTwoCommits()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonObject first = fixture.call(QStringLiteral("entity.create"),
                                     entityArgs(QStringLiteral("一"), 0, 30));
    QJsonObject second = fixture.call(QStringLiteral("entity.create"),
                                      entityArgs(QStringLiteral("二"), 30, 60));
    QVERIFY2(first.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(first)));
    QVERIFY2(second.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(second)));

    const QString firstHash = first.value(QStringLiteral("commit")).toObject()
                                  .value(QStringLiteral("hash")).toString();
    const QString secondHash = second.value(QStringLiteral("commit")).toObject()
                                   .value(QStringLiteral("hash")).toString();
    QVERIFY(!firstHash.isEmpty());
    QVERIFY(!secondHash.isEmpty());
    QVERIFY(firstHash != secondHash);
    QCOMPARE(fixture.historySize(), 3); // init + two operations
}

void Mcp2Tests::createManyRollsBack()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonArray entities;
    entities.append(entityArgs(QStringLiteral("好"), 0, 30));
    entities.append(entityArgs(QStringLiteral("坏"), 10, 10));

    QJsonObject result = fixture.call(QStringLiteral("entity.createMany"),
                                      QJsonObject{{QStringLiteral("entities"), entities}});
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("index")).toInt(),
             1);
    QVERIFY(fixture.store->project().entities.isEmpty());
    QCOMPARE(fixture.historySize(), 1); // only init
}

void Mcp2Tests::keyframesSetManyRollsBack()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString uuid =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("实体"), 0, 30)));
    QVERIFY(!uuid.isEmpty());

    QJsonArray items;
    items.append(QJsonObject{{QStringLiteral("uuid"), uuid},
                             {QStringLiteral("track"), QStringLiteral("opacity")},
                             {QStringLiteral("keys"), keys({{0, 1.0}})}});
    items.append(QJsonObject{{QStringLiteral("uuid"), uuid},
                             {QStringLiteral("track"), QStringLiteral("x")},
                             {QStringLiteral("keys"), keys({{100, 1.0}})}});

    const QJsonObject result =
        fixture.call(QStringLiteral("keyframes.setMany"),
                     QJsonObject{{QStringLiteral("items"), items}});
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("index")).toInt(),
             1);
    QVERIFY(fixture.store->project().entities.value(uuid).tracks.isEmpty());
}

void Mcp2Tests::assetAssignAndUnassign()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString imagePath = fixture.temp.filePath(QStringLiteral("assign.png"));
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));

    QJsonObject result =
        fixture.call(QStringLiteral("asset.import"),
                     QJsonObject{{QStringLiteral("path"), imagePath}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString hash = result.value(QStringLiteral("data")).toObject()
                             .value(QStringLiteral("asset")).toObject()
                             .value(QStringLiteral("hash")).toString();
    QVERIFY(!hash.isEmpty());

    const QString uuid = uuidOf(fixture.call(QStringLiteral("entity.create"),
                                             entityArgs(QStringLiteral("实体"), 0, 60)));

    result = fixture.call(QStringLiteral("asset.assign"),
                          QJsonObject{{QStringLiteral("entityUuid"), uuid},
                                      {QStringLiteral("assetHash"), hash}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("entity")).toObject()
                 .value(QStringLiteral("contentKind")).toString(),
             QStringLiteral("asset"));
    QCOMPARE(fixture.store->project().entities.value(uuid).content.kind,
             ContentKind::Asset);

    result = fixture.call(QStringLiteral("asset.get"),
                          QJsonObject{{QStringLiteral("hash"), hash}});
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("usedBy")).toArray().size(),
             1);

    result = fixture.call(QStringLiteral("asset.unassign"),
                          QJsonObject{{QStringLiteral("entityUuid"), uuid}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("previousAssetHash")).toString(),
             hash);
    QCOMPARE(fixture.store->project().entities.value(uuid).content.kind,
             ContentKind::Empty);
}

void Mcp2Tests::duplicateSubtreeOneCommit()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonArray children;
    children.append(entityArgs(QStringLiteral("一"), 0, 60));
    children.append(entityArgs(QStringLiteral("二"), 0, 60));
    QJsonObject root;
    root.insert(QStringLiteral("name"), QStringLiteral("组"));
    root.insert(QStringLiteral("start"), 0);
    root.insert(QStringLiteral("end"), 60);
    root.insert(QStringLiteral("children"), children);

    QJsonObject result = fixture.call(QStringLiteral("entity.createTree"),
                                      QJsonObject{{QStringLiteral("root"), root}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString rootUuid = result.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("rootUuid")).toString();
    const int before = fixture.historySize();

    result = fixture.call(QStringLiteral("entity.duplicate"),
                          QJsonObject{{QStringLiteral("uuid"), rootUuid},
                                      {QStringLiteral("deep"), true},
                                      {QStringLiteral("offsetFrames"), 10}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toObject()
                            .value(QStringLiteral("message")).toString()));
    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("entityCount")).toInt(), 3);
    QCOMPARE(data.value(QStringLiteral("uuidMap")).toArray().size(), 3);
    QCOMPARE(fixture.store->project().entities.size(), 6);
    QCOMPARE(fixture.historySize(), before + 1);

    const QString newRoot = data.value(QStringLiteral("rootUuid")).toString();
    QVERIFY(newRoot != rootUuid);
    const model::Entity& copy = fixture.store->project().entities.value(newRoot);
    QCOMPARE(copy.start, 10);
    QCOMPARE(copy.end, 70);
}

void Mcp2Tests::projectUpdateFpsNeedsConfirm()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QVERIFY(!uuidOf(fixture.call(QStringLiteral("entity.create"),
                                 entityArgs(QStringLiteral("实体"), 0, 30)))
                 .isEmpty());

    QJsonObject result =
        fixture.call(QStringLiteral("project.update"),
                     QJsonObject{{QStringLiteral("fps"), 24}});
    QCOMPARE(errorCode(result), QStringLiteral("confirm_required"));
    QCOMPARE(fixture.store->project().settings.fps, 30);

    result = fixture.call(QStringLiteral("project.update"),
                          QJsonObject{{QStringLiteral("fps"), 24},
                                      {QStringLiteral("confirm"), true}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(fixture.store->project().settings.fps, 24);
    QVERIFY(result.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("changed")).toArray()
                .contains(QStringLiteral("fps")));

    result = fixture.call(QStringLiteral("project.get"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("settings")).toObject()
                 .value(QStringLiteral("fps")).toInt(),
             24);
}

void Mcp2Tests::deleteManyMergesHierarchy()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString parent =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("父"), 0, 60)));
    const QString child =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("子"), 0, 60, parent)));
    QVERIFY(!parent.isEmpty() && !child.isEmpty());

    QJsonObject result =
        fixture.call(QStringLiteral("entity.deleteMany"),
                     QJsonObject{{QStringLiteral("uuids"),
                                  QJsonArray{parent, child}},
                                 {QStringLiteral("confirm"), true}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("deletedUuids")).toArray().size(),
             2);
    QVERIFY(fixture.store->project().entities.isEmpty());
}

void Mcp2Tests::listToolsAndSchema()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QJsonObject tools = fixture.service->listTools();
    const QJsonArray array = tools.value(QStringLiteral("tools")).toArray();
    QCOMPARE(array.size(), 45);

    QStringList names;
    for (const QJsonValue& value : array)
        names << value.toObject().value(QStringLiteral("name")).toString();
    QVERIFY(names.contains(QStringLiteral("project.get")));
    QVERIFY(names.contains(QStringLiteral("entity.createTree")));
    QVERIFY(names.contains(QStringLiteral("keyframes.setMany")));
    QVERIFY(names.contains(QStringLiteral("solver.create")));
    QVERIFY(names.contains(QStringLiteral("solver.evaluate")));
    QVERIFY(names.contains(QStringLiteral("preview.render_frame")));
    QVERIFY(names.contains(QStringLiteral("view.state")));
    QVERIFY(names.contains(QStringLiteral("asset.import")));
    QVERIFY(names.contains(QStringLiteral("markers.set")));
    QVERIFY(names.contains(QStringLiteral("history.undo")));

    for (const QJsonValue& value : array)
    {
        const QJsonObject tool = value.toObject();
        QVERIFY(!tool.value(QStringLiteral("description")).toString().isEmpty());
        QVERIFY(tool.value(QStringLiteral("inputSchema")).isObject());
    }

    QJsonObject result = fixture.call(QStringLiteral("schema.get"),
                                      QJsonObject{{QStringLiteral("topic"), QStringLiteral("all")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QJsonObject schema = result.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("schema")).toObject();
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("version")).toInt(),
             1);
    QVERIFY(schema.contains(QStringLiteral("entity")));
    QVERIFY(schema.contains(QStringLiteral("keyframes")));
    QVERIFY(schema.contains(QStringLiteral("solver")));
    QVERIFY(schema.contains(QStringLiteral("preview")));
    QVERIFY(schema.contains(QStringLiteral("view")));
    QVERIFY(!schema.value(QStringLiteral("solver")).toObject()
                 .value(QStringLiteral("available"))
                 .toBool());

    result = fixture.call(QStringLiteral("guide.get"),
                          QJsonObject{{QStringLiteral("topic"), QStringLiteral("preview")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QVERIFY(!result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("markdown")).toString().isEmpty());

    result = fixture.call(QStringLiteral("guide.get"),
                          QJsonObject{{QStringLiteral("topic"), QStringLiteral("workflow")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QVERIFY(!result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("markdown")).toString().isEmpty());

    result = fixture.call(QStringLiteral("schema.get"),
                          QJsonObject{{QStringLiteral("topic"), QStringLiteral("nope")}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
}

void Mcp2Tests::solverCreateUpdateDelete()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString ground =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("地面"), 0, 120)));
    QJsonObject ballArgs = entityArgs(QStringLiteral("小球"), 0, 120);
    ballArgs.insert(QStringLiteral("keyframes"),
                    QJsonObject{{QStringLiteral("x"), keys({{0, -300.0}})},
                                {QStringLiteral("y"), keys({{0, -400.0}})}});
    const QString ball =
        uuidOf(fixture.call(QStringLiteral("entity.create"), ballArgs));
    QVERIFY(!ground.isEmpty() && !ball.isEmpty());

    const int before = fixture.historySize();
    QJsonObject createArgs;
    createArgs.insert(QStringLiteral("entityUuid"), ball);
    createArgs.insert(QStringLiteral("effect"), QStringLiteral("gravity"));
    createArgs.insert(QStringLiteral("startFrame"), 0);
    createArgs.insert(QStringLiteral("endFrame"), 59);
    createArgs.insert(QStringLiteral("initialVelocity"),
                      QJsonObject{{QStringLiteral("x"), 50.0},
                                  {QStringLiteral("y"), -100.0}});
    createArgs.insert(
        QStringLiteral("params"),
        QJsonObject{{QStringLiteral("targetUuid"), ground},
                    {QStringLiteral("strength"), 980.0},
                    {QStringLiteral("elasticity"), 0.4},
                    {QStringLiteral("friction"), 0.2},
                    {QStringLiteral("stopThreshold"), 2.0}});

    QJsonObject result =
        fixture.call(QStringLiteral("solver.create"), createArgs);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error"))
                            .toObject()
                            .value(QStringLiteral("message"))
                            .toString()));
    const QJsonObject solver = result.value(QStringLiteral("data"))
                                   .toObject()
                                   .value(QStringLiteral("solver"))
                                   .toObject();
    const QString solverId = solver.value(QStringLiteral("id")).toString();
    QVERIFY(!solverId.isEmpty());
    QCOMPARE(solver.value(QStringLiteral("effect")).toString(),
             QStringLiteral("gravity"));
    QCOMPARE(fixture.historySize(), before + 1);
    QVERIFY(!result.value(QStringLiteral("commit"))
                 .toObject()
                 .value(QStringLiteral("hash"))
                 .toString()
                 .isEmpty());

    result = fixture.call(QStringLiteral("solver.list"),
                          QJsonObject{{QStringLiteral("entityUuid"), ball}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("duration")).toInt(), 120);
    QCOMPARE(data.value(QStringLiteral("solvers")).toArray().size(), 1);
    QCOMPARE(data.value(QStringLiteral("solvers")).toArray().at(0).toObject()
                 .value(QStringLiteral("startFrame")).toInt(),
             0);

    result = fixture.call(QStringLiteral("solver.evaluate"),
                          QJsonObject{{QStringLiteral("entityUuid"), ball},
                                      {QStringLiteral("solverId"), solverId}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("framesRelativeTo")).toString(),
             QStringLiteral("entityStart"));
    QCOMPARE(data.value(QStringLiteral("results")).toArray().size(), 1);
    const QJsonObject evaluation =
        data.value(QStringLiteral("results")).toArray().at(0).toObject();
    const QJsonArray samples = evaluation.value(QStringLiteral("samples")).toArray();
    QCOMPARE(samples.size(), 60);
    QCOMPARE(samples.at(0).toObject().value(QStringLiteral("frame")).toInt(), 0);
    QVERIFY(std::abs(samples.at(0).toObject().value(QStringLiteral("x")).toDouble()
                     + 300.0)
            < 1e-6);
    QVERIFY(std::abs(samples.at(0).toObject().value(QStringLiteral("y")).toDouble()
                     + 400.0)
            < 1e-6);
    QCOMPARE(samples.at(59).toObject().value(QStringLiteral("frame")).toInt(), 59);
    QCOMPARE(samples.at(59).toObject().value(QStringLiteral("stopped")).toBool(),
             true);

    result = fixture.call(
        QStringLiteral("solver.update"),
        QJsonObject{{QStringLiteral("entityUuid"), ball},
                    {QStringLiteral("solverId"), solverId},
                    {QStringLiteral("patch"),
                     QJsonObject{{QStringLiteral("endFrame"), 30},
                                 {QStringLiteral("params"),
                                  QJsonObject{{QStringLiteral("strength"), 1400.0},
                                              {QStringLiteral("elasticity"), 0.5}}}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QJsonObject updated = result.value(QStringLiteral("data"))
                                    .toObject()
                                    .value(QStringLiteral("solver"))
                                    .toObject();
    QCOMPARE(updated.value(QStringLiteral("endFrame")).toInt(), 30);
    QCOMPARE(updated.value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("strength")).toDouble(),
             1400.0);

    result = fixture.call(QStringLiteral("solver.delete"),
                          QJsonObject{{QStringLiteral("entityUuid"), ball},
                                      {QStringLiteral("solverId"), solverId}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("deleted")).toBool(),
             true);
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("solverId")).toString(),
             solverId);
    result = fixture.call(QStringLiteral("solver.list"),
                          QJsonObject{{QStringLiteral("entityUuid"), ball}});
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("solvers")).toArray().size(),
             0);
    QCOMPARE(fixture.historySize(), before + 3);

    // entity.create accepts inline solvers, and orbit segments expose the
    // reverse-solved ellipse in solver.list.
    QJsonObject inlineArgs = entityArgs(QStringLiteral("轨道球"), 0, 120);
    inlineArgs.insert(QStringLiteral("keyframes"),
                      QJsonObject{{QStringLiteral("x"), keys({{0, -300.0}})},
                                  {QStringLiteral("y"), keys({{0, -400.0}})}});
    inlineArgs.insert(
        QStringLiteral("solvers"),
        QJsonArray{QJsonObject{
            {QStringLiteral("effect"), QStringLiteral("orbit")},
            {QStringLiteral("startFrame"), 0},
            {QStringLiteral("endFrame"), 60},
            {QStringLiteral("params"),
             QJsonObject{{QStringLiteral("targetUuid"), ground},
                         {QStringLiteral("aspect"), 0.5},
                         {QStringLiteral("periodFrames"), 60}}}}});
    result = fixture.call(QStringLiteral("entity.create"), inlineArgs);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString orbitBall = uuidOf(result);
    QCOMPARE(fixture.store->project().entities.value(orbitBall).solvers.size(), 1);

    result = fixture.call(QStringLiteral("solver.list"),
                          QJsonObject{{QStringLiteral("entityUuid"), orbitBall}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QJsonObject orbit =
        result.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("solvers")).toArray().at(0).toObject();
    QCOMPARE(orbit.value(QStringLiteral("effect")).toString(),
             QStringLiteral("orbit"));
    const QJsonObject computed = orbit.value(QStringLiteral("computed")).toObject();
    QCOMPARE(computed.value(QStringLiteral("targetUuid")).toString(), ground);
    QVERIFY(computed.value(QStringLiteral("semiMajor")).toDouble() > 0.0);
    QVERIFY(computed.value(QStringLiteral("semiMinor")).toDouble() > 0.0);
}

void Mcp2Tests::solverValidationRejects()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString ground =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("地面"), 0, 120)));
    QJsonObject ballArgs = entityArgs(QStringLiteral("小球"), 0, 120);
    ballArgs.insert(QStringLiteral("keyframes"),
                    QJsonObject{{QStringLiteral("x"), keys({{0, 0.0}})},
                                {QStringLiteral("y"), keys({{0, 0.0}})}});
    const QString ball =
        uuidOf(fixture.call(QStringLiteral("entity.create"), ballArgs));
    const QString noKeys =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("无关键帧"), 0, 120)));
    QVERIFY(!ground.isEmpty() && !ball.isEmpty() && !noKeys.isEmpty());

    const auto gravityArgs = [](const QString& entity, const QString& target,
                                int start, int end) {
        QJsonObject args;
        args.insert(QStringLiteral("entityUuid"), entity);
        args.insert(QStringLiteral("effect"), QStringLiteral("gravity"));
        args.insert(QStringLiteral("startFrame"), start);
        args.insert(QStringLiteral("endFrame"), end);
        args.insert(QStringLiteral("params"),
                    QJsonObject{{QStringLiteral("targetUuid"), target}});
        return args;
    };

    QJsonObject result = fixture.call(
        QStringLiteral("solver.create"), gravityArgs(noKeys, ground, 0, 30));
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("missingTracks")).toArray().size(),
             2);

    result = fixture.call(QStringLiteral("solver.create"),
                          gravityArgs(ball, QString(), 0, 30));
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(
        QStringLiteral("solver.create"),
        gravityArgs(ball, QStringLiteral("0190f2c4-7a1e-7c3b-9d2a-1f4e6b8c0d12"),
                    0, 30));
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(QStringLiteral("solver.create"),
                          gravityArgs(ball, ball, 0, 30));
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(QStringLiteral("solver.create"),
                          gravityArgs(ball, ground, 0, 0));
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));

    result = fixture.call(QStringLiteral("solver.create"),
                          gravityArgs(ball, ground, 0, 40));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    // Rotation is not driven by the gravity segment, so a spin segment can use
    // a start key inside the gravity range and then collide with it.
    result = fixture.call(QStringLiteral("keyframes.set"),
                          QJsonObject{{QStringLiteral("uuid"), ball},
                                      {QStringLiteral("track"), QStringLiteral("rotation")},
                                      {QStringLiteral("keys"), keys({{20, 0.0}})}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    result = fixture.call(
        QStringLiteral("solver.create"),
        QJsonObject{{QStringLiteral("entityUuid"), ball},
                    {QStringLiteral("effect"), QStringLiteral("spin")},
                    {QStringLiteral("startFrame"), 20},
                    {QStringLiteral("endFrame"), 60},
                    {QStringLiteral("params"),
                     QJsonObject{{QStringLiteral("angularVelocity"), 6.0}}}});
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
    QVERIFY(!result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("conflicts")).toArray().isEmpty());
    QCOMPARE(fixture.store->project().entities.value(ball).solvers.size(), 1);

    QJsonObject lockedArgs = entityArgs(QStringLiteral("锁住的球"), 0, 120);
    lockedArgs.insert(QStringLiteral("locked"), true);
    lockedArgs.insert(QStringLiteral("keyframes"),
                      QJsonObject{{QStringLiteral("x"), keys({{0, 0.0}})},
                                  {QStringLiteral("y"), keys({{0, 0.0}})}});
    const QString locked = uuidOf(fixture.call(QStringLiteral("entity.create"),
                                               lockedArgs));
    QVERIFY(!locked.isEmpty());
    result = fixture.call(
        QStringLiteral("solver.create"), gravityArgs(locked, ground, 0, 30));
    QCOMPARE(errorCode(result), QStringLiteral("conflict"));
    QCOMPARE(result.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("details")).toObject()
                 .value(QStringLiteral("lockedBy")).toString(),
             locked);
}

void Mcp2Tests::viewStateReturnsInjected()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString selection =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("选中"), 0, 60)));
    const QString parent =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("父级"), 0, 60)));
    QJsonObject cameraArgs = entityArgs(QStringLiteral("摄像机"), 0, 60);
    cameraArgs.insert(QStringLiteral("content"),
                      QJsonObject{{QStringLiteral("kind"), QStringLiteral("camera")}});
    const QString camera =
        uuidOf(fixture.call(QStringLiteral("entity.create"), cameraArgs));
    QVERIFY(!selection.isEmpty() && !parent.isEmpty() && !camera.isEmpty());

    pvf::mcp2::ViewState state;
    state.playhead = 42;
    state.selection = selection;
    state.timelineParent = parent;
    state.cameraUuid = camera;
    state.viewRect = QRect(10, 20, 300, 200);
    fixture.service->setViewStateProvider([state] { return state; });

    QJsonObject result = fixture.call(QStringLiteral("view.state"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("playhead")).toInt(), 42);
    QCOMPARE(data.value(QStringLiteral("selection")).toString(), selection);
    QCOMPARE(data.value(QStringLiteral("timelineParent")).toString(), parent);
    QCOMPARE(data.value(QStringLiteral("cameraUuid")).toString(), camera);
    const QJsonObject rect =
        data.value(QStringLiteral("viewRect")).toObject();
    QCOMPARE(rect.value(QStringLiteral("x")).toInt(), 10);
    QCOMPARE(rect.value(QStringLiteral("y")).toInt(), 20);
    QCOMPARE(rect.value(QStringLiteral("width")).toInt(), 300);
    QCOMPARE(rect.value(QStringLiteral("height")).toInt(), 200);

    QCOMPARE(data.value(QStringLiteral("fps")).toInt(), 30);
    QCOMPARE(data.value(QStringLiteral("width")).toInt(), 1920);
    QCOMPARE(data.value(QStringLiteral("height")).toInt(), 1080);
    QCOMPARE(data.value(QStringLiteral("root")).toString(), fixture.store->root());
    QCOMPARE(data.value(QStringLiteral("project")).toObject()
                 .value(QStringLiteral("fps")).toInt(),
             30);

    result = fixture.call(QStringLiteral("project.summary"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("activeCameraUuid")).toString(),
             camera);

    fixture.service->setViewStateProvider(pvf::mcp2::ViewStateProvider());
    result = fixture.call(QStringLiteral("view.state"));
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    QCOMPARE(result.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("playhead")).toInt(),
             0);
    QVERIFY(result.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("selection")).isNull());
    result = fixture.call(QStringLiteral("project.summary"));
    QVERIFY(result.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("activeCameraUuid")).isNull());
}

void Mcp2Tests::previewRenderFrameWritesPng()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    const QString uuid =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("图案"), 0, 60)));
    QVERIFY(!uuid.isEmpty());

    pvf::mcp2::ViewState state;
    state.playhead = 5;
    state.selection = uuid;
    fixture.service->setViewStateProvider([state] { return state; });

    const int before = fixture.historySize();
    QJsonObject result = fixture.call(
        QStringLiteral("preview.render_frame"),
        QJsonObject{{QStringLiteral("frame"), 7},
                    {QStringLiteral("width"), 320},
                    {QStringLiteral("height"), 180},
                    {QStringLiteral("background"), QStringLiteral("#112233")}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(result.value(QStringLiteral("error"))
                            .toObject()
                            .value(QStringLiteral("message"))
                            .toString()));
    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    const QString path = data.value(QStringLiteral("path")).toString();
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(data.value(QStringLiteral("width")).toInt(), 320);
    QCOMPARE(data.value(QStringLiteral("height")).toInt(), 180);
    QCOMPARE(data.value(QStringLiteral("frame")).toInt(), 7);
    QCOMPARE(data.value(QStringLiteral("playhead")).toInt(), 5);
    QCOMPARE(data.value(QStringLiteral("selection")).toString(), uuid);

    const QImage image(path);
    QCOMPARE(image.size(), QSize(320, 180));
    QCOMPARE(image.pixelColor(0, 0), QColor(QStringLiteral("#112233")));
    QCOMPARE(fixture.historySize(), before);

    result = fixture.call(QStringLiteral("preview.render_frame"),
                          QJsonObject{{QStringLiteral("frame"), 0}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QString defaultPath = result.value(QStringLiteral("data")).toObject()
                                    .value(QStringLiteral("path")).toString();
    QCOMPARE(QImage(defaultPath).size(), QSize(1920, 1080));
}

void Mcp2Tests::previewRejectsOversize()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonObject result = fixture.call(
        QStringLiteral("preview.render_frame"),
        QJsonObject{{QStringLiteral("width"), 4097}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    result = fixture.call(
        QStringLiteral("preview.render_frame"),
        QJsonObject{{QStringLiteral("height"), 5000}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
    result = fixture.call(
        QStringLiteral("preview.render_frame"),
        QJsonObject{{QStringLiteral("width"), 0}});
    QCOMPARE(errorCode(result), QStringLiteral("invalid_argument"));
}

void Mcp2Tests::solverEvaluateNestedEntity()
{
    Fixture fixture;
    QVERIFY(fixture.init());

    QJsonObject parentArgs = entityArgs(QStringLiteral("父组"), 0, 120);
    parentArgs.insert(QStringLiteral("transform"),
                      QJsonObject{{QStringLiteral("x"), 100.0},
                                  {QStringLiteral("y"), 50.0}});
    const QString parent =
        uuidOf(fixture.call(QStringLiteral("entity.create"), parentArgs));
    QJsonObject childArgs = entityArgs(QStringLiteral("子球"), 0, 60, parent);
    childArgs.insert(QStringLiteral("keyframes"),
                     QJsonObject{{QStringLiteral("x"), keys({{0, 0.0}})},
                                 {QStringLiteral("y"), keys({{0, 0.0}})}});
    const QString child =
        uuidOf(fixture.call(QStringLiteral("entity.create"), childArgs));
    const QString ground =
        uuidOf(fixture.call(QStringLiteral("entity.create"),
                            entityArgs(QStringLiteral("地面"), 0, 120)));
    QVERIFY(!parent.isEmpty() && !child.isEmpty() && !ground.isEmpty());

    QJsonObject createArgs;
    createArgs.insert(QStringLiteral("entityUuid"), child);
    createArgs.insert(QStringLiteral("effect"), QStringLiteral("gravity"));
    createArgs.insert(QStringLiteral("startFrame"), 0);
    createArgs.insert(QStringLiteral("endFrame"), 29);
    createArgs.insert(QStringLiteral("params"),
                      QJsonObject{{QStringLiteral("targetUuid"), ground},
                                  {QStringLiteral("strength"), 980.0}});
    QJsonObject result =
        fixture.call(QStringLiteral("solver.create"), createArgs);
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));

    result = fixture.call(
        QStringLiteral("solver.evaluate"),
        QJsonObject{{QStringLiteral("entityUuid"), child},
                    {QStringLiteral("frames"), QJsonArray{0, 10, 29}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorCode(result)));
    const QJsonArray results = result.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 1);
    const QJsonArray samples = results.at(0).toObject()
                                   .value(QStringLiteral("samples")).toArray();
    QCOMPARE(samples.size(), 3);
    QCOMPARE(samples.at(0).toObject().value(QStringLiteral("frame")).toInt(), 0);
    QVERIFY(std::abs(samples.at(0).toObject().value(QStringLiteral("x")).toDouble())
            < 1e-6);
    QVERIFY(std::abs(samples.at(0).toObject().value(QStringLiteral("y")).toDouble())
            < 1e-6);
    QCOMPARE(samples.at(1).toObject().value(QStringLiteral("frame")).toInt(), 10);
    QCOMPARE(samples.at(2).toObject().value(QStringLiteral("frame")).toInt(), 29);
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    Mcp2Tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "mcp2_tests.moc"
