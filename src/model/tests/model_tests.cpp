#include "model/Model.h"
#include "model/Migrations.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace pvf;
using namespace pvf::model;

void runEntityTreeTests(int* checks, int* failures);

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char* expression, const QString& detail,
           const char* file, int line)
{
    ++gChecks;
    if (condition)
        return;

    ++gFailures;
    qCritical().noquote()
        << QStringLiteral("FAIL %1:%2  %3  %4")
               .arg(QString::fromLatin1(file))
               .arg(line)
               .arg(QString::fromLatin1(expression), detail);
}

bool nearly(double a, double b, double epsilon = 1e-9)
{
    return std::abs(a - b) <= epsilon;
}

#define CHECK(condition) check((condition), #condition, QString(), __FILE__, __LINE__)
#define CHECK_DETAIL(condition, detail) \
    check((condition), #condition, (detail), __FILE__, __LINE__)

Entity makeEntity(int start = 0, int end = 10, int row = 0)
{
    Entity entity;
    entity.row = row;  entity.uuid = newUuid();
    entity.name = QStringLiteral("实体");
    entity.start = start;
    entity.end = end;
    return entity;
}

Keyframe keyAt(int frame, double value)
{
    Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.x = value;
    return keyframe;
}

// A whole-frame key with every public parameter explicitly set.
Keyframe wholeKey(int frame, double x, double y, double rotation,
                  double scaleX = 1.0, double scaleY = 1.0,
                  double opacity = 1.0)
{
    Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.x = x;
    keyframe.y = y;
    keyframe.rotation = rotation;
    keyframe.scaleX = scaleX;
    keyframe.scaleY = scaleY;
    keyframe.opacity = opacity;
    return keyframe;
}

SolverSegment makeSolver(SolverEffect effect, int startFrame, int endFrame)
{
    SolverSegment segment;
    segment.id = newUuid();
    segment.effect = effect;
    segment.startFrame = startFrame;
    segment.endFrame = endFrame;
    return segment;
}

SolverSegment makeTargetSolver(SolverEffect effect, const Uuid& target,
                               int startFrame = 0, int endFrame = 10)
{
    SolverSegment segment = makeSolver(effect, startFrame, endFrame);
    segment.params.insert(QStringLiteral("targetUuid"), target);
    return segment;
}

void testUuid()
{
    QSet<QString> seen;
    for (int i = 0; i < 100; ++i)
    {
        const Uuid uuid = newUuid();
        CHECK(isValidUuid(uuid));
        CHECK_DETAIL(uuid.size() == 36, uuid);
        CHECK_DETAIL(!uuid.contains(QLatin1Char('{')), uuid);
        CHECK_DETAIL(int(QUuid(uuid).version()) == 4, uuid);
        seen.insert(uuid);
    }
    CHECK(seen.size() == 100);

    CHECK(!isValidUuid(QString()));
    CHECK(!isValidUuid(QStringLiteral("not-a-uuid")));
    CHECK(!isValidUuid(QStringLiteral("0190f")));
    CHECK(!isValidUuid(QStringLiteral("{") + newUuid() + QStringLiteral("}")));
    CHECK(isValidUuid(newUuid().toUpper()));
}

void testContentJsonRoundTrip()
{
    const ContentKind kinds[] = {ContentKind::Empty, ContentKind::Text,
                                 ContentKind::Vector, ContentKind::Camera,
                                 ContentKind::Asset};

    for (ContentKind kind : kinds)
    {
        Entity entity = makeEntity(3, 53);
        entity.content.kind = kind;

        switch (kind)
        {
        case ContentKind::Empty:
            break;
        case ContentKind::Text:
            entity.content.text.text = QStringLiteral("Hello 世界");
            entity.content.text.fontFamily = QStringLiteral("Noto Sans SC");
            entity.content.text.pixelSize = 72;
            entity.content.text.color = QStringLiteral("#112233");
            entity.content.text.strokeColor = QStringLiteral("#445566");
            entity.content.text.strokeWidth = 3.5;
            entity.content.text.align = 129;
            entity.content.text.lineHeight = 1.4;
            entity.content.text.letterSpacing = 2.0;
            entity.content.text.padding = 12.0;
            entity.content.text.boxWidth = 800.0;
            entity.content.text.boxHeight = 200.0;
            entity.content.text.bold = true;
            entity.content.text.italic = true;
            entity.content.text.underline = true;
            entity.content.text.vertical = true;
            entity.content.text.verticalRtl = false;
            break;
        case ContentKind::Vector:
            entity.content.vector.shape = 3;
            entity.content.vector.width = 1200.0;
            entity.content.vector.height = 64.0;
            entity.content.vector.cornerRadius = 32.0;
            entity.content.vector.sides = 7;
            entity.content.vector.innerRatio = 0.6;
            entity.content.vector.strokeWidth = 4.0;
            entity.content.vector.fillColor = QStringLiteral("#abcdef");
            entity.content.vector.strokeColor = QStringLiteral("#010203");
            break;
        case ContentKind::Camera:
            break;
        case ContentKind::Asset:
            entity.content.asset.hash = QString(64, QLatin1Char('a'));
            entity.content.asset.type = AssetType::Video;
            entity.content.asset.sourceIn = 7;
            entity.content.asset.volume = 0.35;
            break;
        }

        const QJsonObject json = toJson(entity);
        QString error;
        const auto parsed = entityFromJson(json, &error);
        CHECK_DETAIL(parsed.has_value(), error);
        if (parsed)
        {
            CHECK(toJson(*parsed) == json);
            CHECK(parsed->content.kind == kind);
        }
    }

    Entity textEntity = makeEntity();
    textEntity.content.kind = ContentKind::Text;
    textEntity.content.text.text = QStringLiteral("x");
    const QJsonObject textJson = toJson(textEntity);
    const QJsonObject textContentJson =
        textJson.value(QStringLiteral("content")).toObject();
    CHECK(textContentJson.contains(QStringLiteral("text")));
    CHECK(textContentJson.value(QStringLiteral("text"))
              .toObject()
              .contains(QStringLiteral("fontFamily")));

    Entity assetEntity = makeEntity();
    assetEntity.content.kind = ContentKind::Asset;
    assetEntity.content.asset.hash = QStringLiteral("deadbeef");
    assetEntity.content.asset.type = AssetType::Audio;
    assetEntity.content.asset.sourceIn = 12;
    assetEntity.content.asset.volume = 0.5;
    const QJsonObject assetJson = toJson(assetEntity.content);
    CHECK(assetJson.value(QStringLiteral("assetHash")).toString()
          == QStringLiteral("deadbeef"));
    CHECK(assetJson.value(QStringLiteral("assetType")).toString()
          == QStringLiteral("audio"));
    CHECK(assetJson.contains(QStringLiteral("sourceIn")));
    CHECK(assetJson.contains(QStringLiteral("volume")));
}

void testUnsupportedContentKindRejected()
{
    QString error;
    QJsonObject comic;
    comic.insert(QStringLiteral("kind"), QStringLiteral("comicText"));
    const auto parsedComic = contentFromJson(comic, &error);
    CHECK(!parsedComic.has_value());
    CHECK(error.contains(QStringLiteral("未知内容类型")));

    // Every removed special kind is an unknown kind now, so an old project
    // fails to load instead of silently losing its artwork.
    for (const char* name : {"radialLines", "flowLines", "halftoneDots",
                             "brandWall"})
    {
        QJsonObject document;
        document.insert(QStringLiteral("kind"), QString::fromLatin1(name));
        CHECK(!contentFromJson(document, &error).has_value());
    }
}

void testEntityJsonRoundTrip()
{
    Entity entity = makeEntity(12, 112);
    entity.name = QStringLiteral("标题");
    entity.row = 4;
    entity.hidden = true;
    entity.locked = true;
    entity.inheritVisibility = false;
    entity.visible = false;
    entity.physicsInteract = true;
    entity.transform = Transform{1.5, -2.25, 33.0, -0.5, 2.0, 10.0, 20.0, 0.75};

    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("标题文字");
    entity.content.text.bold = true;
    entity.content.text.pixelSize = 96;
    entity.content.text.lineHeight = 1.25;

    Keyframe first = wholeKey(0, 10.0, -4.0, 0.0, 1.0, 2.0, 0.8);
    first.note = QStringLiteral("起点");
    first.content.insert(QStringLiteral("pixelSize"), 96.0);
    first.content.insert(QStringLiteral("lineHeight"), 1.25);
    first.htmlVars.insert(QStringLiteral("progress"), 0.0);
    first.easing.type = QStringLiteral("bezier");
    first.easing.inX = 0.2;
    first.easing.inY = 0.1;
    first.easing.outX = 0.7;
    first.easing.outY = 1.2;

    Keyframe second = wholeKey(50, 90.0, 20.0, 45.0, 1.5, 2.0, 0.4);
    second.content.insert(QStringLiteral("pixelSize"), 160.0);
    second.content.insert(QStringLiteral("lineHeight"), 2.0);
    second.htmlVars.insert(QStringLiteral("progress"), 1.0);
    second.easing.type = QStringLiteral("easeInOut");
    second.visible = false;
    second.physicsInteract = true;

    // frame == duration (100) is an ordinary keyframe at the end boundary.
    Keyframe boundary = wholeKey(100, 120.0, 30.0, 90.0);
    boundary.content.insert(QStringLiteral("pixelSize"), 200.0);
    boundary.content.insert(QStringLiteral("lineHeight"), 2.25);

    entity.keyframes = {first, second, boundary};

    SolverSegment gravity = makeSolver(SolverEffect::Gravity, 10, 40);
    gravity.initialVelocity = QPointF(2.0, -3.5);
    gravity.params.insert(QStringLiteral("strength"), 9.8);
    gravity.params.insert(QStringLiteral("elasticity"), 0.5);

    SolverSegment spin = makeSolver(SolverEffect::Spin, 60, 70);
    spin.version = 2;
    SolverSegment openEnded = makeSolver(SolverEffect::Gravity, 75, -1);
    entity.solvers = {gravity, spin, openEnded};

    const QJsonObject json = toJson(entity);
    CHECK(json.contains(QStringLiteral("keyframes")));
    CHECK(!json.contains(QStringLiteral("tracks")));
    CHECK(json.contains(QStringLiteral("solvers")));
    CHECK(json.value(QStringLiteral("parentUuid")).isNull());

    QString error;
    const auto parsed = entityFromJson(json, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (!parsed)
        return;

    CHECK(toJson(*parsed) == json);
    CHECK(parsed->uuid == entity.uuid);
    CHECK(parsed->name == entity.name);
    CHECK(parsed->row == 4);
    CHECK(parsed->hidden);
    CHECK(parsed->locked);
    CHECK(!parsed->inheritVisibility);
    CHECK(!parsed->visible);
    CHECK(parsed->physicsInteract);
    CHECK(nearly(parsed->transform.scaleX, -0.5));
    CHECK(nearly(parsed->transform.opacity, 0.75));
    CHECK(parsed->keyframes.size() == 3);
    CHECK(parsed->keyframes.at(0).frame == 0);
    CHECK(nearly(parsed->keyframes.at(0).x, 10.0));
    CHECK(nearly(parsed->keyframes.at(0).content.value(QStringLiteral("lineHeight")).toDouble(), 1.25));
    CHECK(nearly(parsed->keyframes.at(0).htmlVars.value(QStringLiteral("progress")).toDouble(), 0.0));
    CHECK(parsed->keyframes.at(0).easing.type == QStringLiteral("bezier"));
    CHECK(nearly(parsed->keyframes.at(0).easing.outY, 1.2));
    CHECK(parsed->keyframes.at(1).frame == 50);
    CHECK(!parsed->keyframes.at(1).visible);
    CHECK(parsed->keyframes.at(1).physicsInteract);
    CHECK(parsed->keyframes.at(2).frame == 100);
    CHECK(parsed->solvers.size() == 3);
    CHECK(parsed->solvers.at(0).effect == SolverEffect::Gravity);
    CHECK(parsed->solvers.at(0).endFrame == 40);
    CHECK(nearly(parsed->solvers.at(0).initialVelocity.x(), 2.0));
    CHECK(parsed->solvers.at(0).params.value(QStringLiteral("strength")).toDouble()
          == 9.8);
    CHECK(parsed->solvers.at(1).effect == SolverEffect::Spin);
    CHECK(parsed->solvers.at(1).endFrame == 70);
    CHECK(parsed->solvers.at(1).version == 2);
    CHECK(parsed->solvers.at(2).endFrame == -1);
}

void testComponentJsonRoundTrip()
{
    Keyframe keyframe = keyAt(12, 3.5);
    keyframe.y = -4.0;
    keyframe.rotation = 33.0;
    keyframe.content.insert(QStringLiteral("pixelSize"), 42.0);
    keyframe.htmlVars.insert(QStringLiteral("speed"), 2.5);
    keyframe.note = QStringLiteral("标记");
    keyframe.easing.type = QStringLiteral("bezier");
    keyframe.easing.inX = 0.15;
    keyframe.easing.inY = 0.05;
    keyframe.easing.outX = 0.85;
    keyframe.easing.outY = 0.95;

    const QJsonObject keyframeJson = toJson(keyframe);
    QString error;
    const auto parsedKeyframe = keyframeFromJson(keyframeJson, &error);
    CHECK_DETAIL(parsedKeyframe.has_value(), error);
    if (parsedKeyframe)
    {
        CHECK(*parsedKeyframe == keyframe);
        CHECK(toJson(*parsedKeyframe) == keyframeJson);
    }

    const QVector<Keyframe> keys = {keyframe, wholeKey(20, 9.0, 1.0, 5.0)};
    const QJsonArray keysJson = toJson(keys);
    CHECK(keysJson.size() == 2);
    const auto parsedKeys = keyframesFromJson(keysJson, &error);
    CHECK_DETAIL(parsedKeys.has_value(), error);
    if (parsedKeys)
    {
        CHECK(*parsedKeys == keys);
        CHECK(toJson(*parsedKeys) == keysJson);
    }

    SolverSegment segment = makeSolver(SolverEffect::Orbit, 5, 30);
    segment.version = 3;
    segment.initialVelocity = QPointF(-1.25, 4.5);
    segment.params.insert(QStringLiteral("aspect"), 0.6);
    segment.params.insert(QStringLiteral("periodFrames"), 90);

    const QJsonObject segmentJson = toJson(segment);
    const auto parsedSegment = solverFromJson(segmentJson, &error);
    CHECK_DETAIL(parsedSegment.has_value(), error);
    if (parsedSegment)
    {
        CHECK(toJson(*parsedSegment) == segmentJson);
        CHECK(parsedSegment->effect == SolverEffect::Orbit);
        CHECK(parsedSegment->endFrame == 30);
        CHECK(nearly(parsedSegment->initialVelocity.y(), 4.5));
        CHECK(parsedSegment->params.value(QStringLiteral("periodFrames")).toInt()
              == 90);
    }

    SolverSegment spiral = makeSolver(SolverEffect::Spiral, 2, 40);
    spiral.params.insert(QStringLiteral("targetUuid"), newUuid());
    spiral.params.insert(QStringLiteral("turns"), 4.5);
    spiral.params.insert(QStringLiteral("direction"), -1);
    spiral.params.insert(QStringLiteral("aspect"), 0.5);
    spiral.params.insert(QStringLiteral("angle"), 30.0);
    spiral.params.insert(QStringLiteral("speedDegPerSec"), 0.0);
    const QJsonObject spiralJson = toJson(spiral);
    CHECK(spiralJson.value(QStringLiteral("effect")).toString()
          == QStringLiteral("spiral"));
    const auto parsedSpiral = solverFromJson(spiralJson, &error);
    CHECK_DETAIL(parsedSpiral.has_value(), error);
    if (parsedSpiral)
    {
        CHECK(parsedSpiral->effect == SolverEffect::Spiral);
        CHECK(toJson(*parsedSpiral) == spiralJson);
    }
}

void testProjectJsonRoundTrip()
{
    Project project;
    project.settings.name = QStringLiteral("测试工程");
    project.settings.width = 1280;
    project.settings.height = 720;
    project.settings.fps = 24;
    project.settings.background = QStringLiteral("#000000");

    Marker intro;
    intro.id = newUuid();
    intro.frame = 0;
    intro.title = QStringLiteral("前奏");
    intro.note = QStringLiteral("**Markdown** 备注");
    intro.tags = {QStringLiteral("music"), QStringLiteral("intro")};
    intro.color = QStringLiteral("#ffbe3c");

    Marker chorus;
    chorus.id = newUuid();
    chorus.frame = 120;
    chorus.title = QStringLiteral("副歌");
    chorus.tags = {QStringLiteral("chorus")};
    chorus.color = QStringLiteral("#3cff88");

    project.markers = {intro, chorus};

    const QJsonObject json = toJson(project);
    CHECK(json.value(QStringLiteral("format")).toString()
          == QStringLiteral("pvforge-project"));
    CHECK(json.value(QStringLiteral("version")).toInt()
          == kProjectFormatVersion);
    CHECK(json.value(QStringLiteral("markers")).toArray().size() == 2);
    CHECK(!json.contains(QStringLiteral("entities")));

    QString error;
    const auto parsed = projectFromJson(json, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (!parsed)
        return;

    CHECK(toJson(*parsed) == json);
    CHECK(parsed->entities.isEmpty());
    CHECK(parsed->markers.size() == 2);
    CHECK(parsed->markers.at(0).tags.size() == 2);
    CHECK(parsed->markers.at(1).frame == 120);
    CHECK(parsed->settings.fps == 24);
    CHECK(parsed->settings.background == QStringLiteral("#000000"));
}

void testWholeFrameEvaluation()
{
    KeyframeSample fallback;
    fallback.x = 7.0;
    fallback.y = 9.0;
    fallback.rotation = 3.0;
    fallback.scaleX = 1.0;
    fallback.scaleY = 1.0;
    fallback.opacity = 0.4;
    fallback.visible = true;
    fallback.content.insert(QStringLiteral("pixelSize"), 96.0);
    fallback.content.insert(QStringLiteral("lineHeight"), 1.25);
    // The manifest default of a declared numeric HTML variable is the static
    // fallback the project author sees before adding any keyframe.
    fallback.htmlVars.insert(QStringLiteral("progress"), 0.0);

    // Empty keyframes fall back to the static sample unchanged.
    const KeyframeSample empty = evaluateKeyframes({}, fallback, 42.5);
    CHECK(nearly(empty.x, 7.0));
    CHECK(nearly(empty.y, 9.0));
    CHECK(nearly(empty.rotation, 3.0));
    CHECK(nearly(empty.opacity, 0.4));
    CHECK(nearly(empty.content.value(QStringLiteral("pixelSize")).toDouble(), 96.0));
    CHECK(empty.visible);

    Keyframe a;
    a.frame = 0;
    a.x = 0.0;
    a.y = 0.0;
    a.rotation = 0.0;
    a.scaleX = 1.0;
    a.scaleY = 1.0;
    a.opacity = 1.0;
    a.visible = true;
    a.physicsInteract = false;
    a.content.insert(QStringLiteral("pixelSize"), 20.0);
    a.content.insert(QStringLiteral("lineHeight"), 1.0);
    a.htmlVars.insert(QStringLiteral("progress"), 0.0);

    Keyframe b = a;
    b.frame = 10;
    b.x = 100.0;
    b.y = 50.0;
    b.rotation = 90.0;
    b.scaleX = 2.0;
    b.scaleY = 3.0;
    b.opacity = 0.5;
    b.visible = false;
    b.physicsInteract = true;
    b.content.insert(QStringLiteral("pixelSize"), 120.0);
    b.content.insert(QStringLiteral("lineHeight"), 2.0);
    b.htmlVars.insert(QStringLiteral("progress"), 10.0);

    const QVector<Keyframe> keys = {a, b};

    // Every numeric field shares one eased progress value.
    const KeyframeSample mid = evaluateKeyframes(keys, fallback, 5.0);
    CHECK(nearly(mid.x, 50.0));
    CHECK(nearly(mid.y, 25.0));
    CHECK(nearly(mid.rotation, 45.0));
    CHECK(nearly(mid.scaleX, 1.5));
    CHECK(nearly(mid.scaleY, 2.0));
    CHECK(nearly(mid.opacity, 0.75));
    CHECK(nearly(mid.content.value(QStringLiteral("pixelSize")).toDouble(), 70.0));
    CHECK(nearly(mid.content.value(QStringLiteral("lineHeight")).toDouble(), 1.5));
    CHECK(nearly(mid.htmlVars.value(QStringLiteral("progress")).toDouble(), 5.0));

    // Boolean parameters are step values from the left key.
    CHECK(mid.visible);
    CHECK(!mid.physicsInteract);
    CHECK(!evaluateKeyframes(keys, fallback, 10.0).visible);
    CHECK(evaluateKeyframes(keys, fallback, 9.9).visible);
    CHECK(evaluateKeyframes(keys, fallback, 10.0).physicsInteract);

    // Fractional frames sample between the same two keys.
    const KeyframeSample quarter = evaluateKeyframes(keys, fallback, 2.5);
    CHECK(nearly(quarter.x, 25.0));
    CHECK(nearly(quarter.content.value(QStringLiteral("pixelSize")).toDouble(), 45.0));
    CHECK(nearly(quarter.htmlVars.value(QStringLiteral("progress")).toDouble(), 2.5));

    // Values are held before the first and after the last key.
    CHECK(nearly(evaluateKeyframes(keys, fallback, -5.0).x, 0.0));
    CHECK(nearly(evaluateKeyframes(keys, fallback, 50.0).opacity, 0.5));
    CHECK(nearly(evaluateKeyframes(keys, fallback, 50.0).htmlVars
                     .value(QStringLiteral("progress")).toDouble(),
                 10.0));

    // The left key's easing controls the whole frame.
    QVector<Keyframe> eased = keys;
    eased[0].easing.type = QStringLiteral("easeIn");
    CHECK(nearly(evaluateKeyframes(eased, fallback, 5.0).x, 25.0));
    eased[0].easing.type = QStringLiteral("easeOut");
    CHECK(nearly(evaluateKeyframes(eased, fallback, 5.0).x, 75.0));
    eased[0].easing.type = QStringLiteral("linear");
    eased[0].easing.type = QStringLiteral("bogus");
    CHECK(nearly(evaluateKeyframes(eased, fallback, 5.0).x, 50.0));

    // Missing content / htmlVars fields fall back to the static defaults while
    // a field present only on one key is held.
    Keyframe sparse = a;
    sparse.content = QJsonObject{};
    sparse.htmlVars = QJsonObject{};
    QVector<Keyframe> sparseKeys = {sparse, b};
    const KeyframeSample sparseMid = evaluateKeyframes(sparseKeys, fallback, 5.0);
    CHECK(nearly(sparseMid.content.value(QStringLiteral("pixelSize")).toDouble(), 108.0));
    CHECK(nearly(sparseMid.htmlVars.value(QStringLiteral("progress")).toDouble(), 5.0));

    // A single key is constant at every query, fractional or step.
    const QVector<Keyframe> single = {a};
    CHECK(nearly(evaluateKeyframes(single, fallback, -3.0).x, 0.0));
    CHECK(nearly(evaluateKeyframes(single, fallback, 7.5).x, 0.0));

    // frame == duration is an ordinary interpolation target at the end
    // boundary; it does not clamp the sample to the clip's last rendered frame.
    Keyframe endKey = b;
    endKey.frame = 100;
    QVector<Keyframe> boundaryKeys = {a, endKey};
    CHECK(nearly(evaluateKeyframes(boundaryKeys, fallback, 50.0).x, 50.0));
}

void testKeyframeJsonAndValidation()
{
    QString error;

    // JSON round trip keeps every public parameter and both numeric objects.
    Keyframe key = wholeKey(12, 3.5, -2.0, 15.0, 1.2, 0.8, 0.6);
    key.content.insert(QStringLiteral("pixelSize"), 42.0);
    key.content.insert(QStringLiteral("lineHeight"), 1.5);
    key.htmlVars.insert(QStringLiteral("speed"), 2.5);
    key.visible = false;
    key.physicsInteract = true;
    key.note = QStringLiteral("注释");
    key.easing.type = QStringLiteral("bezier");

    const QJsonObject json = toJson(key);
    const auto parsed = keyframeFromJson(json, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
    {
        CHECK(*parsed == key);
        CHECK(toJson(*parsed) == json);
    }

    // A duplicate frame is rejected both while parsing and by validation.
    const QJsonArray duplicateJson = QJsonArray{toJson(key), toJson(key)};
    CHECK(!keyframesFromJson(duplicateJson, &error).has_value());
    CHECK(error.contains(QStringLiteral("重复")));

    Entity duplicateEntity = makeEntity(0, 10);
    duplicateEntity.keyframes = {keyAt(0, 1.0), keyAt(0, 2.0)};
    CHECK(!validateEntity(duplicateEntity, &error));
    CHECK(error.contains(QStringLiteral("重复")));

    // frame == duration is legal; frame > duration is rejected.
    Entity boundaryEntity = makeEntity(0, 10);
    boundaryEntity.keyframes = {keyAt(0, 1.0), keyAt(10, 2.0)};
    CHECK_DETAIL(validateEntity(boundaryEntity, &error), error);

    Entity beyond = makeEntity(0, 10);
    beyond.keyframes = {keyAt(11, 2.0)};
    CHECK(!validateEntity(beyond, &error));
    CHECK(error.contains(QStringLiteral("0..10")));

    // Unsorted programmatic keys are an invariant violation; the parser sorts
    // a hand-authored array instead of failing.
    Entity unsortedEntity = makeEntity(0, 10);
    unsortedEntity.keyframes = {keyAt(5, 1.0), keyAt(1, 2.0)};
    CHECK(!validateEntity(unsortedEntity, &error));
    CHECK(error.contains(QStringLiteral("升序")));

    const QJsonArray unsortedJson = QJsonArray{toJson(keyAt(5, 1.0)),
                                               toJson(keyAt(1, 2.0))};
    const auto sorted = keyframesFromJson(unsortedJson, &error);
    CHECK_DETAIL(sorted.has_value(), error);
    if (sorted)
    {
        CHECK(sorted->at(0).frame == 1);
        CHECK(sorted->at(1).frame == 5);
    }

    // Old per-property track objects / non-array keyframes fail loudly.
    QJsonObject oldEntityJson = toJson(makeEntity(0, 10));
    QJsonObject oldTracks;
    oldTracks.insert(QStringLiteral("x"), QJsonArray{toJson(keyAt(0, 1.0))});
    oldEntityJson.insert(QStringLiteral("keyframes"), oldTracks);
    CHECK(!entityFromJson(oldEntityJson, &error).has_value());
    CHECK(error.contains(QStringLiteral("旧分轨工程格式已废弃")));

    oldEntityJson.insert(QStringLiteral("keyframes"), QStringLiteral("nope"));
    CHECK(!entityFromJson(oldEntityJson, &error).has_value());
    CHECK(error.contains(QStringLiteral("旧分轨工程格式已废弃")));

    QJsonObject legacyTracksJson = toJson(makeEntity(0, 10));
    legacyTracksJson.remove(QStringLiteral("keyframes"));
    legacyTracksJson.insert(QStringLiteral("tracks"), QJsonObject{});
    CHECK(!entityFromJson(legacyTracksJson, &error).has_value());
    CHECK(error.contains(QStringLiteral("旧分轨工程格式已废弃")));

    // Content keys must be animation parameters of the entity type.
    Entity text = makeEntity(0, 100);
    text.content.kind = ContentKind::Text;
    text.keyframes = {keyAt(0, 0.0)};
    text.keyframes[0].content.insert(QStringLiteral("fontSize"), 20.0);
    CHECK(!validateEntity(text, &error));
    CHECK(error.contains(QStringLiteral("fontSize")));

    text.keyframes[0].content = QJsonObject{};
    text.keyframes[0].content.insert(QStringLiteral("pixelSize"), 20.0);
    CHECK_DETAIL(validateEntity(text, &error), error);

    // Definition parameters can never be keyframed.
    text.keyframes[0].content.insert(QStringLiteral("text"),
                                     QStringLiteral("x"));
    CHECK(!validateEntity(text, &error));

    // htmlVars must be finite numbers.
    text.keyframes[0].content = QJsonObject{};
    text.keyframes[0].htmlVars.insert(QStringLiteral("count"), true);
    CHECK(!validateEntity(text, &error));

    // Editing helpers keep the vector sorted and replace the same frame.
    QVector<Keyframe> editKeys;
    upsertKeyframe(editKeys, keyAt(5, 1.0));
    upsertKeyframe(editKeys, keyAt(1, 2.0));
    upsertKeyframe(editKeys, keyAt(5, 3.0));
    CHECK(editKeys.size() == 2);
    CHECK(editKeys.at(0).frame == 1);
    CHECK(editKeys.at(1).frame == 5);
    CHECK(nearly(editKeys.at(1).x, 3.0));
    CHECK(keyframeAt(editKeys, 5) != nullptr);
    CHECK(keyframeAt(editKeys, 4) == nullptr);
    CHECK(removeKeyframe(editKeys, 5));
    CHECK(!removeKeyframe(editKeys, 5));
}


void testEasingPresets()
{
    const QStringList presets = easingPresetNames();
    CHECK(presets.size() == 30);
    CHECK(presets.first() == QStringLiteral("easeinquad"));
    CHECK(presets.last() == QStringLiteral("easeinoutbounce"));
    CHECK(presets.indexOf(QStringLiteral("easeoutsine")) == 13);

    EasingSpec spec;
    for (const QString& name : presets)
    {
        spec.type = name;
        CHECK_DETAIL(nearly(easedProgress(spec, 0.0), 0.0), name);
        CHECK_DETAIL(nearly(easedProgress(spec, 1.0), 1.0), name);

        for (const double t : {0.001, 0.25, 0.5, 0.75, 0.999})
        {
            const double value = easedProgress(spec, t);
            CHECK_DETAIL(std::isfinite(value), name);
            // Elastic/back/bounce overshoot, but only modestly.
            CHECK_DETAIL(value >= -0.6 && value <= 1.6, name);
        }
    }

    // Non-overshooting families must be monotonic. Elastic/back/bounce dip or
    // overshoot on purpose, so they are excluded.
    const QStringList monotonic = {
        QStringLiteral("easeinquad"),        QStringLiteral("easeoutquad"),
        QStringLiteral("easeinoutquad"),     QStringLiteral("easeincubic"),
        QStringLiteral("easeoutcubic"),      QStringLiteral("easeinoutcubic"),
        QStringLiteral("easeinquart"),       QStringLiteral("easeoutquart"),
        QStringLiteral("easeinoutquart"),    QStringLiteral("easeinquint"),
        QStringLiteral("easeoutquint"),      QStringLiteral("easeinoutquint"),
        QStringLiteral("easeinsine"),        QStringLiteral("easeoutsine"),
        QStringLiteral("easeinoutsine"),     QStringLiteral("easeinexpo"),
        QStringLiteral("easeoutexpo"),       QStringLiteral("easeinoutexpo"),
        QStringLiteral("easeincirc"),        QStringLiteral("easeoutcirc"),
        QStringLiteral("easeinoutcirc"),
    };
    for (const QString& name : monotonic)
    {
        spec.type = name;
        double previous = 0.0;
        bool nonDecreasing = true;
        for (int i = 1; i <= 200; ++i)
        {
            const double value = easedProgress(spec, i / 200.0);
            if (value + 1e-12 < previous)
                nonDecreasing = false;
            previous = value;
        }
        CHECK_DETAIL(nonDecreasing, name);
    }

    // Representative curves, values taken from the legacy implementation.
    struct Sample
    {
        const char* name;
        double t;
        double expected;
    };
    const Sample samples[] = {
        {"linear", 0.25, 0.25},
        {"easeinquad", 0.5, 0.25},
        {"easeoutcubic", 0.5, 0.875},
        {"easeinoutquad", 0.25, 0.125},
        {"easeinoutsine", 0.25, 0.14644660940672624},
        {"easeinexpo", 0.5, 0.03125},
        {"easeoutcirc", 0.5, 0.86602540378443865},
        {"easeoutbounce", 0.5, 0.765625},
        {"easeoutelastic", 0.5, 1.015625},
        {"easeinback", 0.1, -0.01431422},
        {"easeoutback", 0.7, 1.08019954},
    };
    for (const Sample& sample : samples)
    {
        spec.type = QString::fromLatin1(sample.name);
        CHECK_DETAIL(nearly(easedProgress(spec, sample.t), sample.expected, 1e-7),
                     QString::fromLatin1(sample.name));
    }

    // Legacy aliases map onto the same curves.
    EasingSpec legacy;
    EasingSpec canonical;
    for (const auto& pair : {std::pair{"easeIn", "easeinquad"},
                             std::pair{"easeOut", "easeoutquad"},
                             std::pair{"easeInOut", "easeinoutquad"}})
    {
        legacy.type = QString::fromLatin1(pair.first);
        canonical.type = QString::fromLatin1(pair.second);
        CHECK_DETAIL(nearly(easedProgress(legacy, 0.31),
                            easedProgress(canonical, 0.31)),
                     QString::fromLatin1(pair.first));
    }

    spec.type = QStringLiteral("instant");
    CHECK(nearly(easedProgress(spec, 0.5), 0.0));
    spec.type = QStringLiteral("step");
    CHECK(nearly(easedProgress(spec, 0.5), 0.0));
    spec.type = QStringLiteral("bogus");
    CHECK(nearly(easedProgress(spec, 0.31), 0.31));
}

void testEasingJson()
{
    QString error;

    for (const QString& name : easingPresetNames())
    {
        Keyframe keyframe = keyAt(0, 1.0);
        keyframe.easing.type = name;

        const QJsonObject json = toJson(keyframe);
        CHECK_DETAIL(json.value(QStringLiteral("easing")).toString() == name,
                     name);

        const auto parsed = keyframeFromJson(json, &error);
        CHECK_DETAIL(parsed.has_value(), error);
        if (parsed)
        {
            CHECK_DETAIL(parsed->easing.type == name, name);
            CHECK_DETAIL(toJson(*parsed) == json, name);
        }
    }

    // Camel-case legacy names normalise to the lowercase quadrant presets.
    Keyframe camel = keyAt(0, 1.0);
    camel.easing.type = QStringLiteral("easeIn");
    CHECK(toJson(camel).value(QStringLiteral("easing")).toString()
          == QStringLiteral("easeinquad"));
    camel.easing.type = QStringLiteral("easeInOut");
    CHECK(toJson(camel).value(QStringLiteral("easing")).toString()
          == QStringLiteral("easeinoutquad"));
    camel.easing.type = QStringLiteral("EaseOutBounce");
    CHECK(toJson(camel).value(QStringLiteral("easing")).toString()
          == QStringLiteral("easeoutbounce"));

    // Reading old lowercase JSON is accepted unchanged.
    QJsonObject legacy = toJson(keyAt(0, 0.0));
    legacy.insert(QStringLiteral("easing"), QStringLiteral("easeinquart"));
    auto parsed = keyframeFromJson(legacy, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
        CHECK(parsed->easing.type == QStringLiteral("easeinquart"));

    // Any casing is accepted and rewritten in canonical form.
    legacy.insert(QStringLiteral("easing"), QStringLiteral("EASEOUTSINE"));
    parsed = keyframeFromJson(legacy, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
        CHECK(parsed->easing.type == QStringLiteral("easeoutsine"));

    // step is an alias for instant.
    Keyframe step = keyAt(0, 0.0);
    step.easing.type = QStringLiteral("step");
    CHECK(toJson(step).value(QStringLiteral("easing")).toString()
          == QStringLiteral("instant"));
    legacy.insert(QStringLiteral("easing"), QStringLiteral("step"));
    parsed = keyframeFromJson(legacy, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
        CHECK(parsed->easing.type == QStringLiteral("instant"));

    // Empty types are written as linear.
    Keyframe empty = keyAt(0, 0.0);
    empty.easing.type.clear();
    CHECK(toJson(empty).value(QStringLiteral("easing")).toString()
          == QStringLiteral("linear"));

    // Equality compares canonical curves, not the stored spelling.
    Keyframe aliasA = keyAt(0, 0.0);
    aliasA.easing.type = QStringLiteral("easeInOut");
    Keyframe aliasB = keyAt(0, 0.0);
    aliasB.easing.type = QStringLiteral("easeinoutquad");
    CHECK(aliasA == aliasB);

    // Unknown names are preserved but evaluate as linear.
    Keyframe unknown = keyAt(0, 0.0);
    unknown.easing.type = QStringLiteral("bogus");
    CHECK(toJson(unknown).value(QStringLiteral("easing")).toString()
          == QStringLiteral("bogus"));
    CHECK(nearly(easedProgress(unknown.easing, 0.37), 0.37));

    // Bezier keeps its object form and default handles.
    EasingSpec bezier;
    bezier.type = QStringLiteral("bezier");
    CHECK(nearly(easedProgress(bezier, 0.5), 0.5, 1e-6));
}

void testEntityValidation()
{
    Entity valid = makeEntity(0, 10);
    CHECK(validateEntity(valid));

    QString error;
    Entity bad = valid;
    bad.uuid.clear();
    CHECK(!validateEntity(bad, &error));
    CHECK(!error.isEmpty());

    bad = valid;
    bad.uuid = QStringLiteral("not-a-uuid");
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.start = -1;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.end = bad.start - 1;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.end = bad.start;
    CHECK(validateEntity(bad, &error));

    bad = valid;
    bad.row = -1;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.transform.opacity = 1.5;
    CHECK(!validateEntity(bad, &error));
    CHECK(error.contains(QStringLiteral("opacity")));

    bad = valid;
    bad.transform.opacity = -0.1;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.transform.opacity = std::nan("");
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.keyframes = {keyAt(11, 5.0)};
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.keyframes = {keyAt(-1, 5.0)};
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.keyframes = {keyAt(0, 5.0), keyAt(5, 6.0), keyAt(10, 7.0)};
    CHECK_DETAIL(validateEntity(bad, &error), error);

    bad.keyframes.append(keyAt(5, 8.0));
    CHECK(!validateEntity(bad, &error));
    CHECK(error.contains(QStringLiteral("重复")));

    bad = valid;
    bad.content.kind = ContentKind::Asset;
    CHECK(!validateEntity(bad, &error));

    bad.content.asset.hash = QStringLiteral("abc");
    CHECK(validateEntity(bad));
}

void testSolverValidation()
{
    Entity entity = makeEntity(0, 100);
    SolverSegment first = makeSolver(SolverEffect::Gravity, 0, 20);
    SolverSegment second = makeSolver(SolverEffect::Spin, 21, 40);
    entity.solvers = {first, second};
    CHECK(validateEntity(entity));

    QString error;
    Entity overlapping = entity;
    overlapping.solvers[1].startFrame = 20;
    CHECK(!validateEntity(overlapping, &error));
    CHECK(!error.isEmpty());

    overlapping.solvers[1].startFrame = 19;
    CHECK(!validateEntity(overlapping, &error));

    overlapping = entity;
    overlapping.solvers[1].endFrame = 19;
    CHECK(!validateEntity(overlapping, &error));

    overlapping = entity;
    overlapping.solvers[0].startFrame = 100;
    CHECK(!validateEntity(overlapping, &error));

    overlapping = entity;
    overlapping.solvers[0].endFrame = 100;
    CHECK(!validateEntity(overlapping, &error));

    overlapping = entity;
    overlapping.solvers[0].id.clear();
    CHECK(!validateEntity(overlapping, &error));

    overlapping = entity;
    overlapping.solvers[1].id = overlapping.solvers[0].id;
    CHECK(!validateEntity(overlapping, &error));

    Entity openEnded = makeEntity(0, 100);
    openEnded.solvers = {makeSolver(SolverEffect::BlackHole, 10, -1)};
    CHECK(validateEntity(openEnded));

    openEnded.solvers.append(makeSolver(SolverEffect::Orbit, 101, -1));
    CHECK(!validateEntity(openEnded, &error));

    Entity gravity = makeEntity(0, 100);
    gravity.solvers = {makeSolver(SolverEffect::Gravity, 20, 40)};
    gravity.keyframes = {keyAt(20, 0.0)};
    CHECK(validateEntity(gravity));

    gravity.keyframes = {keyAt(20, 0.0), keyAt(30, 5.0)};
    CHECK(!validateEntity(gravity, &error));

    gravity.keyframes = {keyAt(20, 0.0), keyAt(41, 5.0)};
    CHECK_DETAIL(validateEntity(gravity, &error), error);

    gravity.keyframes.append(keyAt(40, 5.0));
    CHECK(!validateEntity(gravity, &error));

    Entity spin = makeEntity(0, 100);
    spin.solvers = {makeSolver(SolverEffect::Spin, 20, 40)};
    spin.keyframes = {keyAt(20, 0.0)};
    CHECK(validateEntity(spin));

    spin.keyframes = {keyAt(20, 0.0), keyAt(30, 45.0)};
    CHECK(!validateEntity(spin, &error));

    Entity openDriven = makeEntity(0, 100);
    openDriven.solvers = {makeSolver(SolverEffect::Orbit, 10, -1)};
    openDriven.keyframes = {keyAt(99, 1.0)};
    // Automatic segments derive their end from the next key: the key at 99
    // is the end marker (solver structurally ends at 98), not an illegal key
    // inside the segment.
    CHECK(validateEntity(openDriven));
    CHECK(solverStructuralEnd(openDriven, openDriven.solvers.first()) == 98);

    // A keyframe at the end boundary (frame == duration) is an ordinary
    // keyframe after the automatic segment; the segment ends at duration-1.
    Entity endBoundary = makeEntity(0, 100);
    endBoundary.solvers = {makeSolver(SolverEffect::Orbit, 10, -1)};
    endBoundary.keyframes = {keyAt(100, 1.0)};
    CHECK_DETAIL(validateEntity(endBoundary, &error), error);
    CHECK(solverStructuralEnd(endBoundary, endBoundary.solvers.first()) == 99);
    CHECK(isLastKeyframeInClip(endBoundary, 100));

    // Only a key with no later key is the last one; an earlier key in the same
    // clip must report false.
    Entity twoKeys = makeEntity(0, 100);
    twoKeys.keyframes = {keyAt(10, 0.0), keyAt(20, 1.0)};
    CHECK(!isLastKeyframeInClip(twoKeys, 10));
    CHECK(isLastKeyframeInClip(twoKeys, 20));
    CHECK(!isLastKeyframeInClip(twoKeys, 15));

    Entity explicitDriven = makeEntity(0, 100);
    explicitDriven.solvers = {makeSolver(SolverEffect::Orbit, 10, 50)};
    explicitDriven.keyframes = {keyAt(40, 1.0)};
    CHECK(!validateEntity(explicitDriven, &error));
}

void testSolverTargets()
{
    QString error;

    Entity source = makeEntity(0, 100);
    source.solvers = {makeSolver(SolverEffect::Gravity, 0, 20)};

    Project project;
    project.entities.insert(source.uuid, source);

    // Missing targetUuid.
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(source.uuid));
    CHECK(error.contains(source.solvers.at(0).id));
    CHECK(error.contains(QStringLiteral("gravity")));
    CHECK(error.contains(QStringLiteral("targetUuid")));

    Entity target = makeEntity(0, 100, 1);

    // Empty string.
    source.solvers[0].params.insert(QStringLiteral("targetUuid"), QString());
    project.entities.insert(source.uuid, source);
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("不能为空")));

    // Invalid UUID.
    source.solvers[0].params.insert(QStringLiteral("targetUuid"),
                                    QStringLiteral("not-a-uuid"));
    project.entities.insert(source.uuid, source);
    CHECK(!validateProject(project, &error));

    // Non-string target.
    source.solvers[0].params.insert(QStringLiteral("targetUuid"), 42);
    project.entities.insert(source.uuid, source);
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("字符串")));

    // Target entity does not exist.
    source.solvers[0].params.insert(QStringLiteral("targetUuid"),
                                    newUuid());
    project.entities.insert(source.uuid, source);
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("不存在")));

    // Target points at the entity itself.
    source.solvers[0].params.insert(QStringLiteral("targetUuid"), source.uuid);
    project.entities.insert(source.uuid, source);
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("自己")));

    // A valid target passes.
    project.entities.insert(target.uuid, target);
    source.solvers[0].params.insert(QStringLiteral("targetUuid"), target.uuid);
    project.entities.insert(source.uuid, source);
    CHECK_DETAIL(validateProject(project, &error), error);

    // Spin does not need a target.
    source.solvers = {makeSolver(SolverEffect::Spin, 30, 40)};
    project.entities.insert(source.uuid, source);
    CHECK_DETAIL(validateProject(project, &error), error);

    // Two entities targeting each other.
    Entity second = makeEntity(0, 100, 2);
    source.solvers = {
        makeTargetSolver(SolverEffect::Gravity, second.uuid, 0, 20)};
    second.solvers = {
        makeTargetSolver(SolverEffect::Orbit, source.uuid, 0, 20)};
    project.entities.insert(source.uuid, source);
    project.entities.insert(second.uuid, second);

    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("循环")));
    CHECK(error.contains(source.uuid));
    CHECK(error.contains(second.uuid));

    // Three-entity cycle.
    Entity third = makeEntity(0, 100, 3);
    source.solvers = {
        makeTargetSolver(SolverEffect::Gravity, second.uuid, 0, 20)};
    second.solvers = {
        makeTargetSolver(SolverEffect::Orbit, third.uuid, 0, 20)};
    third.solvers = {
        makeTargetSolver(SolverEffect::BlackHole, source.uuid, 0, 20)};
    project.entities.insert(source.uuid, source);
    project.entities.insert(second.uuid, second);
    project.entities.insert(third.uuid, third);

    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("循环")));
    CHECK(error.contains(source.uuid));
    CHECK(error.contains(second.uuid));
    CHECK(error.contains(third.uuid));

    // Multiple solvers creating the same edge must not break detection or
    // report the same pair twice.
    second.solvers.append(
        makeTargetSolver(SolverEffect::BlackHole, third.uuid, 30, 40));
    project.entities.insert(second.uuid, second);
    CHECK(!validateProject(project, &error));
    CHECK(error.contains(QStringLiteral("循环")));
}

void testProjectValidation()
{
    Project project;

    Entity parent = makeEntity(10, 100);
    parent.name = QStringLiteral("父级");
    Entity child = makeEntity(20, 50);
    child.parentUuid = parent.uuid;

    project.entities.insert(parent.uuid, parent);
    project.entities.insert(child.uuid, child);
    CHECK(validateProject(project));

    QString error;
    Project bad = project;
    bad.entities[child.uuid].start = 5;
    CHECK(!validateProject(bad, &error));
    CHECK(!error.isEmpty());

    bad = project;
    bad.entities[child.uuid].end = 101;
    CHECK(!validateProject(bad, &error));

    bad = project;
    bad.entities[child.uuid].parentUuid = newUuid();
    CHECK(!validateProject(bad, &error));

    bad = project;
    const Uuid extraKey = newUuid();
    bad.entities.insert(extraKey, parent);
    CHECK(!validateProject(bad, &error));

    Project cycle;
    Entity a = makeEntity(0, 100);
    Entity b = makeEntity(0, 100);
    a.parentUuid = b.uuid;
    b.parentUuid = a.uuid;
    cycle.entities.insert(a.uuid, a);
    cycle.entities.insert(b.uuid, b);
    CHECK(!validateProject(cycle, &error));

    Project selfParent;
    Entity self = makeEntity(0, 100);
    self.parentUuid = self.uuid;
    selfParent.entities.insert(self.uuid, self);
    CHECK(!validateProject(selfParent, &error));

    bad = project;
    bad.settings.fps = 0;
    CHECK(!validateProject(bad, &error));

    bad = project;
    bad.settings.width = 0;
    CHECK(!validateProject(bad, &error));

    Marker marker;
    marker.id = newUuid();
    marker.frame = 0;
    bad = project;
    bad.markers.append(marker);
    CHECK(validateProject(bad));

    bad.markers[0].frame = -1;
    CHECK(!validateProject(bad, &error));

    bad.markers[0].frame = 0;
    bad.markers[0].id.clear();
    CHECK(!validateProject(bad, &error));
}

void testProjectValidationWithoutRemoval()
{
    // validateProjectWithout() has to decide exactly like validating a copy of
    // the project with the removed entities erased. These cases mirror what
    // ProjectDocument does for a delete batch.
    Project project;
    Entity parent = makeEntity(0, 100);
    Entity child = makeEntity(0, 50);
    child.parentUuid = parent.uuid;
    Entity bystander = makeEntity(0, 100, 1);
    project.entities.insert(parent.uuid, parent);
    project.entities.insert(child.uuid, child);
    project.entities.insert(bystander.uuid, bystander);
    CHECK(validateProject(project));

    QString error;

    // Erasing an unrelated entity keeps the project valid.
    CHECK_DETAIL(validateProjectWithout(project, {bystander.uuid}, &error),
                 error);

    // Erasing a parent leaves its child dangling, exactly like the erased copy.
    CHECK(!validateProjectWithout(project, {parent.uuid}, &error));
    CHECK(error.contains(QStringLiteral("父实体")));

    // Erasing the whole tree is fine.
    CHECK_DETAIL(
        validateProjectWithout(project, {parent.uuid, child.uuid}, &error),
        error);

    // A removed entity is not validated itself, even when it is broken: the
    // copy-based check removed it before validating.
    Project broken = project;
    broken.entities[child.uuid].end = 101;
    broken.entities[child.uuid].keyframes = {keyAt(500, 1.0)};
    CHECK(!validateProject(broken, &error));
    CHECK_DETAIL(validateProjectWithout(broken, {child.uuid}, &error), error);

    // An index key that disagrees with the entity UUID is only an error while
    // the entity stays in the project.
    Project mismatched = project;
    const Uuid otherKey = newUuid();
    mismatched.entities.insert(otherKey, parent);
    mismatched.entities[otherKey].row = 1;  // keep row 0 free for the original
    CHECK(!validateProject(mismatched, &error));
    CHECK_DETAIL(validateProjectWithout(mismatched, {otherKey}, &error), error);

    // A solver target that is being deleted counts as missing.
    Project solved;
    Entity source = makeEntity(0, 100);
    Entity target = makeEntity(0, 100, 1);
    source.solvers = {makeTargetSolver(SolverEffect::Gravity, target.uuid)};
    solved.entities.insert(source.uuid, source);
    solved.entities.insert(target.uuid, target);
    CHECK_DETAIL(validateProject(solved, &error), error);
    CHECK(!validateProjectWithout(solved, {target.uuid}, &error));
    CHECK(error.contains(QStringLiteral("targetUuid")));
    CHECK_DETAIL(validateProjectWithout(solved, {source.uuid}, &error), error);

    // Settings and markers are still validated against the removal set.
    Project badFps = project;
    badFps.settings.fps = 0;
    CHECK(!validateProjectWithout(badFps, {bystander.uuid}, &error));

    // An empty removal set is exactly validateProject().
    CHECK_DETAIL(validateProjectWithout(project, {}, &error), error);
    CHECK(!validateProjectWithout(broken, {}, &error));
}

void testInvalidJson()
{
    QString error;
    CHECK(!entityFromJson(QJsonObject{}, &error).has_value());
    CHECK(!error.isEmpty());

    QJsonObject badUuid = toJson(makeEntity());
    badUuid.insert(QStringLiteral("uuid"), QStringLiteral("nope"));
    CHECK(!entityFromJson(badUuid, &error).has_value());

    QJsonObject badRange = toJson(makeEntity(0, 10));
    badRange.insert(QStringLiteral("end"), -1);
    CHECK(!entityFromJson(badRange, &error).has_value());

    Entity badKeyframe = makeEntity(0, 10);
    badKeyframe.keyframes = {keyAt(11, 1.0)};
    CHECK(!entityFromJson(toJson(badKeyframe), &error).has_value());

    QJsonObject unknownContent = toJson(makeEntity());
    QJsonObject content;
    content.insert(QStringLiteral("kind"), QStringLiteral("nope"));
    unknownContent.insert(QStringLiteral("content"), content);
    CHECK(!entityFromJson(unknownContent, &error).has_value());

    QJsonObject badParent = toJson(makeEntity());
    badParent.insert(QStringLiteral("parentUuid"), 42);
    CHECK(!entityFromJson(badParent, &error).has_value());

    QJsonObject badProject = toJson(Project{});
    badProject.insert(QStringLiteral("format"), QStringLiteral("legacy"));
    CHECK(!projectFromJson(badProject, &error).has_value());

    badProject = toJson(Project{});
    badProject.insert(QStringLiteral("version"), kProjectFormatVersion + 1);
    CHECK(!projectFromJson(badProject, &error).has_value());

    badProject = toJson(Project{});
    badProject.insert(QStringLiteral("markers"), QStringLiteral("bad"));
    CHECK(!projectFromJson(badProject, &error).has_value());

    QJsonObject badMarkerProject = toJson(Project{});
    QJsonArray markers;
    QJsonObject marker;
    marker.insert(QStringLiteral("id"), newUuid());
    marker.insert(QStringLiteral("frame"), -1);
    markers.append(marker);
    badMarkerProject.insert(QStringLiteral("markers"), markers);
    CHECK(!projectFromJson(badMarkerProject, &error).has_value());
}

void testDefaults()
{
    QJsonObject minimal;
    minimal.insert(QStringLiteral("uuid"), newUuid());
    minimal.insert(QStringLiteral("name"), QStringLiteral("minimal"));
    minimal.insert(QStringLiteral("start"), 0);
    minimal.insert(QStringLiteral("end"), 10);

    QString error;
    const auto parsed = entityFromJson(minimal, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (!parsed)
        return;

    CHECK(parsed->row == 0);
    CHECK(!parsed->hidden);
    CHECK(!parsed->locked);
    CHECK(parsed->inheritVisibility);
    CHECK(parsed->visible);
    CHECK(!parsed->physicsInteract);
    CHECK(nearly(parsed->transform.x, 0.0));
    CHECK(nearly(parsed->transform.rotation, 0.0));
    CHECK(nearly(parsed->transform.scaleX, 1.0));
    CHECK(nearly(parsed->transform.scaleY, 1.0));
    CHECK(nearly(parsed->transform.opacity, 1.0));
    CHECK(parsed->content.kind == ContentKind::Empty);
    CHECK(parsed->keyframes.isEmpty());
    CHECK(parsed->solvers.isEmpty());
    CHECK(parsed->parentUuid.isEmpty());

    QJsonObject missingEnd;
    missingEnd.insert(QStringLiteral("uuid"), newUuid());
    missingEnd.insert(QStringLiteral("start"), 25);
    const auto parsedEnd = entityFromJson(missingEnd, &error);
    CHECK_DETAIL(parsedEnd.has_value(), error);
    if (parsedEnd)
        CHECK(parsedEnd->end == 25);
}

void testChildrenAndRootSorting()
{
    Project project;

    Entity high = makeEntity(0, 10);
    high.row = 1;

    Entity low = makeEntity(10, 20);
    low.row = 0;

    Entity early = makeEntity(5, 8);
    early.row = 0;

    Entity child = makeEntity(10, 12);
    child.row = 0;
    child.parentUuid = low.uuid;

    project.entities.insert(high.uuid, high);
    project.entities.insert(low.uuid, low);
    project.entities.insert(early.uuid, early);
    project.entities.insert(child.uuid, child);

    CHECK(validateProject(project));

    const QVector<Uuid> roots = rootEntities(project);
    CHECK(roots.size() == 3);
    if (roots.size() == 3)
    {
        CHECK(roots.at(0) == early.uuid);
        CHECK(roots.at(1) == low.uuid);
        CHECK(roots.at(2) == high.uuid);
    }

    const QVector<Uuid> lowChildren = childrenOf(project, low.uuid);
    CHECK(lowChildren.size() == 1);
    if (lowChildren.size() == 1)
        CHECK(lowChildren.at(0) == child.uuid);

    CHECK(childrenOf(project, early.uuid).isEmpty());
    CHECK(childrenOf(project, newUuid()).isEmpty());
    CHECK(childrenOf(project, Uuid{}) == roots);

    Entity tieA = makeEntity(3, 4);
    tieA.row = 2;
    Entity tieB = makeEntity(3, 4);
    tieB.row = 2;
    project.entities.insert(tieA.uuid, tieA);
    project.entities.insert(tieB.uuid, tieB);

    const QVector<Uuid> tiedRoots = rootEntities(project);
    const int indexA = tiedRoots.indexOf(tieA.uuid);
    const int indexB = tiedRoots.indexOf(tieB.uuid);
    CHECK(indexA >= 0);
    CHECK(indexB >= 0);
    if (tieA.uuid < tieB.uuid)
        CHECK(indexA < indexB);
    else
        CHECK(indexB < indexA);
}

void testJsonDeterminism()
{
    // Git-friendly output: keyframes are a sorted array, so identical content
    // always serialises to identical bytes.
    Entity a = makeEntity(0, 100);
    a.uuid = QStringLiteral("11111111-1111-4111-8111-111111111111");
    a.keyframes = {wholeKey(0, 1.0, 3.0, 0.0), wholeKey(10, 2.0, 3.0, 0.0)};
    a.keyframes[1].visible = false;

    Entity b = a;

    const QByteArray ja = QJsonDocument(toJson(a)).toJson(QJsonDocument::Compact);
    const QByteArray jb = QJsonDocument(toJson(b)).toJson(QJsonDocument::Compact);
    CHECK_DETAIL(ja == jb, QString::fromUtf8(ja));

    // Parsing and re-serializing must also be byte-identical.
    QString error;
    const auto parsed = entityFromJson(toJson(a), &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (!parsed)
        return;
    const QByteArray jc = QJsonDocument(toJson(*parsed)).toJson(QJsonDocument::Compact);
    CHECK_DETAIL(jc == ja, QString::fromUtf8(jc));
}

void testFormatVersion()
{
    CHECK(kProjectFormatVersion == 3);

    QString error;
    const QJsonObject current = toJson(Project{});
    CHECK(current.value(QStringLiteral("version")).toInt()
          == kProjectFormatVersion);
    CHECK(projectFromJson(current, &error).has_value());

    // A file from a newer build is refused with a readable reason instead of
    // being half-parsed.
    QJsonObject future = current;
    future.insert(QStringLiteral("version"), kProjectFormatVersion + 1);
    error.clear();
    CHECK(!projectFromJson(future, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("高于")), error);
    CHECK_DETAIL(error.contains(QStringLiteral("不会被修改")), error);

    // An older file must be migrated first, not read with today's rules.
    QJsonObject older = current;
    older.insert(QStringLiteral("version"), kProjectFormatVersion - 1);
    error.clear();
    CHECK(!projectFromJson(older, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("低于")), error);

    // Strict mode (project opening) requires the version field.
    QJsonObject noVersion = current;
    noVersion.remove(QStringLiteral("version"));
    ParseOptions strict;
    strict.mode = ParseMode::Strict;
    error.clear();
    CHECK(!projectFromJson(noVersion, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("version")), error);

    // Lenient mode assumes the current version so old test fixtures stay
    // readable, and reports the missing key as a warning.
    QStringList warnings;
    ParseOptions lenient;
    lenient.warnings = &warnings;
    error.clear();
    const auto parsed = projectFromJson(noVersion, lenient, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    CHECK(!warnings.isEmpty());

    // Entity files carry their own format marker and version, in the same
    // numbering as project.json.
    Entity entity = makeEntity(0, 10);
    entity.keyframes = {keyAt(0, 1.0)};
    const QJsonObject currentEntity = toJson(entity);
    CHECK(currentEntity.value(QStringLiteral("format")).toString()
          == QLatin1String(kEntityFormat));
    CHECK(currentEntity.value(QStringLiteral("version")).toInt()
          == kProjectFormatVersion);

    // Unknown keys must not swallow the format/version fields.
    Entity entityWithExtras = entity;
    entityWithExtras.extras.insert(QStringLiteral("futureEntity"), 1);
    const QJsonObject entityWithFuture = toJson(entityWithExtras);
    CHECK(entityWithFuture.contains(QStringLiteral("futureEntity")));
    CHECK(entityWithFuture.value(QStringLiteral("version")).toInt()
          == kProjectFormatVersion);

    // A newer entity file is refused with the same readable reason.
    QJsonObject futureEntity = currentEntity;
    futureEntity.insert(QStringLiteral("version"), kProjectFormatVersion + 1);
    error.clear();
    CHECK(!entityFromJson(futureEntity, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("实体版本")), error);
    CHECK_DETAIL(error.contains(QStringLiteral("高于")), error);
    CHECK_DETAIL(error.contains(QStringLiteral("不会被修改")), error);

    // Strict opening rejects an entity that still needs migration: without
    // version and format it reads as v1 and must go through the chain first.
    QJsonObject legacyEntity = currentEntity;
    legacyEntity.remove(QStringLiteral("version"));
    legacyEntity.remove(QStringLiteral("format"));
    error.clear();
    CHECK(!entityFromJson(legacyEntity, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("实体版本 1")), error);
    CHECK_DETAIL(error.contains(QStringLiteral("迁移")), error);

    // A v2-sized document with a stripped version is corrupt, not legacy.
    QJsonObject strippedEntity = currentEntity;
    strippedEntity.remove(QStringLiteral("version"));
    error.clear();
    CHECK(!entityFromJson(strippedEntity, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("version")), error);

    // Lenient mode keeps hand-written fixtures readable.
    error.clear();
    CHECK_DETAIL(entityFromJson(legacyEntity, &error).has_value(), error);
}

void testRequiredFieldsStrict()
{
    ParseOptions strict;
    strict.mode = ParseMode::Strict;
    QString error;

    Entity base = makeEntity(0, 10);
    base.name = QStringLiteral("base");
    const QJsonObject entityJson = toJson(base);

    const auto requireEntityKey = [&](const QString& key) {
        QJsonObject object = entityJson;
        object.remove(key);
        error.clear();
        CHECK(!entityFromJson(object, strict, &error).has_value());
        CHECK_DETAIL(error.contains(key), error);
    };
    requireEntityKey(QStringLiteral("uuid"));
    requireEntityKey(QStringLiteral("start"));
    requireEntityKey(QStringLiteral("end"));

    // Nested objects report their full JSON path.
    Entity keyed = makeEntity(0, 10);
    keyed.keyframes = {keyAt(3, 7.0)};
    QJsonObject keyedJson = toJson(keyed);
    QJsonArray keyframes =
        keyedJson.value(QStringLiteral("keyframes")).toArray();
    QJsonObject key = keyframes.at(0).toObject();
    key.remove(QStringLiteral("x"));
    keyframes.replace(0, key);
    keyedJson.insert(QStringLiteral("keyframes"), keyframes);
    error.clear();
    CHECK(!entityFromJson(keyedJson, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("keyframes[0].x")), error);

    Entity solved = makeEntity(0, 10);
    solved.solvers.append(makeSolver(SolverEffect::Gravity, 0, 5));
    QJsonObject solvedJson = toJson(solved);
    QJsonObject segment =
        solvedJson.value(QStringLiteral("solvers")).toArray().at(0).toObject();
    segment.remove(QStringLiteral("effect"));
    solvedJson.insert(QStringLiteral("solvers"), QJsonArray{segment});
    error.clear();
    CHECK(!entityFromJson(solvedJson, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("solvers[0].effect")), error);

    QJsonObject projectJson = toJson(Project{});
    QJsonObject noFps = projectJson;
    noFps.remove(QStringLiteral("fps"));
    error.clear();
    CHECK(!projectFromJson(noFps, strict, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("fps")), error);

    // Lenient mode keeps the default and records a warning.
    QStringList warnings;
    ParseOptions lenient;
    lenient.warnings = &warnings;
    QJsonObject keyNoFrame;
    keyNoFrame.insert(QStringLiteral("x"), 2.0);
    error.clear();
    const auto parsedKey = keyframeFromJson(keyNoFrame, lenient, &error);
    CHECK_DETAIL(parsedKey.has_value(), error);
    if (parsedKey)
        CHECK(parsedKey->frame == 0);
    CHECK_DETAIL(warnings.join(QLatin1Char(';')).contains(QStringLiteral("frame")),
                 warnings.join(QLatin1Char(';')));
}

void testUnknownFieldsRoundTrip()
{
    QRandomGenerator rng(0xC0FFEEu);
    const auto randomValue = [&rng]() -> QJsonValue {
        switch (rng.bounded(6))
        {
        case 0:
            return QStringLiteral("s%1").arg(rng.bounded(1000));
        case 1:
            return rng.bounded(1000);
        case 2:
            return double(rng.bounded(1000)) / 7.0;
        case 3:
            return rng.bounded(2) != 0;
        case 4:
        {
            QJsonObject nested;
            nested.insert(QStringLiteral("n"), rng.bounded(100));
            return nested;
        }
        default:
        {
            QJsonArray array;
            array.append(rng.bounded(10));
            array.append(QStringLiteral("x"));
            return array;
        }
        }
    };
    const auto fill = [&](QJsonObject object, const QString& prefix, int count) {
        QStringList names;
        for (int i = 0; i < count; ++i)
        {
            const QString name = prefix + QString::number(i);
            object.insert(name, randomValue());
            names << name;
        }
        return std::make_pair(object, names);
    };

    Entity entity = makeEntity(0, 24);
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("hi");
    entity.keyframes = {keyAt(5, 3.0)};
    entity.solvers.append(makeSolver(SolverEffect::Gravity, 0, 3));

    Project project;
    Marker marker;
    marker.id = newUuid();
    marker.frame = 3;
    marker.title = QStringLiteral("m");
    project.markers.append(marker);

    const auto entityFill =
        fill(toJson(entity), QStringLiteral("futureEntity"), 5);
    QJsonObject entityJson = entityFill.first;
    QStringList entityNames = entityFill.second;

    const auto contentFill = fill(
        entityJson.value(QStringLiteral("content")).toObject(),
        QStringLiteral("futureContent"), 5);
    entityJson.insert(QStringLiteral("content"), contentFill.first);
    entityNames += contentFill.second;

    QJsonArray keyframes = entityJson.value(QStringLiteral("keyframes")).toArray();
    const auto keyFill = fill(
        keyframes.at(0).toObject(),
        QStringLiteral("futureKey"), 5);
    keyframes.replace(0, keyFill.first);
    entityJson.insert(QStringLiteral("keyframes"), keyframes);
    entityNames += keyFill.second;

    const auto solverFill = fill(
        entityJson.value(QStringLiteral("solvers")).toArray().at(0).toObject(),
        QStringLiteral("futureSolver"), 5);
    entityJson.insert(QStringLiteral("solvers"),
                      QJsonArray{solverFill.first});
    entityNames += solverFill.second;

    QString error;
    const auto parsed = entityFromJson(entityJson, &error);
    CHECK_DETAIL(parsed.has_value(), error);
    if (parsed)
    {
        const QJsonObject written = toJson(*parsed);
        for (const QString& name : entityNames)
        {
            const bool atTop = written.contains(name);
            const bool inContent = written.value(QStringLiteral("content"))
                                       .toObject()
                                       .contains(name);
            const bool inKey = written.value(QStringLiteral("keyframes"))
                                   .toArray()
                                   .at(0)
                                   .toObject()
                                   .contains(name);
            const bool inSolver = written.value(QStringLiteral("solvers"))
                                      .toArray()
                                      .at(0)
                                      .toObject()
                                      .contains(name);
            CHECK_DETAIL(atTop || inContent || inKey || inSolver, name);
        }

        // A second pass is stable: no duplicates, no drift.
        const auto again = entityFromJson(written, &error);
        CHECK_DETAIL(again.has_value(), error);
        if (again)
            CHECK(toJson(*again) == written);
    }

    // Project-level unknown keys (including marker extras) survive as well.
    const auto projectFill =
        fill(toJson(project), QStringLiteral("futureProject"), 5);
    QJsonObject projectJson = projectFill.first;
    const auto markerFill = fill(
        projectJson.value(QStringLiteral("markers")).toArray().at(0).toObject(),
        QStringLiteral("futureMarker"), 5);
    projectJson.insert(QStringLiteral("markers"),
                       QJsonArray{markerFill.first});

    const auto parsedProject = projectFromJson(projectJson, &error);
    CHECK_DETAIL(parsedProject.has_value(), error);
    if (parsedProject)
    {
        const QJsonObject written = toJson(*parsedProject);
        for (const QString& name : projectFill.second)
            CHECK_DETAIL(written.contains(name), name);
        for (const QString& name : markerFill.second)
        {
            CHECK_DETAIL(written.value(QStringLiteral("markers"))
                             .toArray()
                             .at(0)
                             .toObject()
                             .contains(name),
                         name);
        }
        const auto again = projectFromJson(written, &error);
        CHECK_DETAIL(again.has_value(), error);
        if (again)
            CHECK(toJson(*again) == written);
    }

    // A stale extras entry can never shadow a real field.
    Entity shadowed = makeEntity(4, 9);
    shadowed.name = QStringLiteral("real");
    shadowed.extras.insert(QStringLiteral("name"), QStringLiteral("evil"));
    CHECK(toJson(shadowed).value(QStringLiteral("name")).toString()
          == QStringLiteral("real"));
}

void testMigrationFramework()
{
    using namespace migrations;

    QVector<Migration> chain;
    chain.append(Migration{0, QStringLiteral("v0_to_v1"),
                           [](const QJsonObject& document) {
                               QJsonObject next = document;
                               next.remove(QStringLiteral("legacyScene"));
                               next.insert(QStringLiteral("scenes"),
                                           QJsonArray{});
                               return next;
                           }});
    chain.append(Migration{1, QStringLiteral("v1_to_v2"),
                           [](const QJsonObject& document) {
                               QJsonObject next = document;
                               next.insert(QStringLiteral("format"),
                                           QStringLiteral("pvforge-project"));
                               return next;
                           }});

    QJsonObject v0;
    v0.insert(QStringLiteral("version"), 0);
    v0.insert(QStringLiteral("legacyScene"), QJsonObject{});

    QString error;
    const auto upgraded = upgrade(v0, 0, 2, chain, &error);
    CHECK_DETAIL(upgraded.has_value(), error);
    if (upgraded)
    {
        CHECK(upgraded->value(QStringLiteral("version")).toInt() == 2);
        CHECK(!upgraded->contains(QStringLiteral("legacyScene")));
        CHECK(upgraded->contains(QStringLiteral("scenes")));
        CHECK(upgraded->value(QStringLiteral("format")).toString()
              == QStringLiteral("pvforge-project"));
    }
    // Steps are pure: the input document is untouched.
    CHECK(v0.contains(QStringLiteral("legacyScene")));

    // A gap in the chain is a readable error, not a silent skip.
    QVector<Migration> gapped;
    gapped.append(Migration{0, QStringLiteral("v0_to_v1"),
                            [](const QJsonObject& document) {
                                return document;
                            }});
    error.clear();
    CHECK(!upgrade(v0, 0, 2, gapped, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("缺少")), error);

    error.clear();
    CHECK(!upgrade(v0, 2, 1, chain, &error).has_value());

    // Production registry: v3 is the first root-subtree format and old flat
    // projects are not imported, so there is no production step.
    CHECK(registry().isEmpty());
    CHECK(minimumSupportedVersion() == kProjectFormatVersion);

    const QJsonObject current = toJson(Project{});
    error.clear();
    const auto same = upgrade(current, kProjectFormatVersion,
                              kProjectFormatVersion, &error);
    CHECK_DETAIL(same.has_value(), error);
    if (same)
        CHECK(*same == current);

    QJsonObject old = current;
    old.insert(QStringLiteral("version"), 0);
    error.clear();
    CHECK(!upgrade(old, 0, kProjectFormatVersion, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("缺少")), error);

    // A v2 document cannot reach v3 through the production chain: ProjectStore
    // refuses the old flat layout instead of importing it.
    QJsonObject v2Project = current;
    v2Project.insert(QStringLiteral("version"), 2);
    error.clear();
    CHECK(!upgrade(v2Project, 2, kProjectFormatVersion, &error).has_value());
    CHECK_DETAIL(error.contains(QStringLiteral("缺少")), error);
}

struct TestCase
{
    const char* name;
    void (*run)();
};
const TestCase kTests[] = {
    {"uuid", testUuid},
    {"content-json-roundtrip", testContentJsonRoundTrip},
    {"removed-content-kind", testUnsupportedContentKindRejected},
    {"entity-json-roundtrip", testEntityJsonRoundTrip},
    {"json-determinism", testJsonDeterminism},
    {"component-json-roundtrip", testComponentJsonRoundTrip},
    {"project-json-roundtrip", testProjectJsonRoundTrip},
    {"whole-frame-evaluation", testWholeFrameEvaluation},
    {"keyframe-json-and-validation", testKeyframeJsonAndValidation},
    {"easing-presets", testEasingPresets},
    {"easing-json", testEasingJson},
    {"entity-validation", testEntityValidation},
    {"solver-validation", testSolverValidation},
    {"solver-targets", testSolverTargets},
    {"project-validation", testProjectValidation},
    {"project-validation-without", testProjectValidationWithoutRemoval},
    {"invalid-json", testInvalidJson},
    {"defaults", testDefaults},
    {"children-and-root-sorting", testChildrenAndRootSorting},
    {"format-version", testFormatVersion},
    {"required-fields-strict", testRequiredFieldsStrict},
    {"unknown-fields-roundtrip", testUnknownFieldsRoundTrip},
    {"migration-framework", testMigrationFramework},
    {"entity-tree-nested", [] { runEntityTreeTests(&gChecks, &gFailures); }},
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    for (const TestCase& test : kTests)
    {
        const int failuresBefore = gFailures;
        test.run();
        qInfo().noquote() << (gFailures == failuresBefore ? "PASS" : "FAIL")
                          << test.name;
    }
    qInfo().noquote() << QStringLiteral("%1 checks, %2 failures")
                             .arg(gChecks)
                             .arg(gFailures);
    return gFailures == 0 ? 0 : 1;
}
