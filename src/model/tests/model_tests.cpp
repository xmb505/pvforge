#include "model/Model.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <utility>

using namespace pvf;
using namespace pvf::model;

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

Entity makeEntity(int start = 0, int end = 10)
{
    Entity entity;
    entity.uuid = newUuid();
    entity.name = QStringLiteral("实体");
    entity.start = start;
    entity.end = end;
    return entity;
}

Keyframe keyAt(int frame, double value)
{
    Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.value = value;
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

    Track xTrack;
    Keyframe x0 = keyAt(0, 10.0);
    x0.note = QStringLiteral("起点");
    Keyframe x1 = keyAt(50, 90.0);
    x1.easing.type = QStringLiteral("easeInOut");
    xTrack.keys = {x0, x1};

    Track rotationTrack;
    Keyframe rotation0 = keyAt(0, 0.0);
    rotation0.easing.type = QStringLiteral("bezier");
    rotation0.easing.inX = 0.2;
    rotation0.easing.inY = 0.1;
    rotation0.easing.outX = 0.7;
    rotation0.easing.outY = 1.2;
    rotationTrack.keys = {rotation0, keyAt(90, 180.0)};

    Track visibleTrack;
    visibleTrack.keys = {keyAt(0, 1.0), keyAt(20, 0.0), keyAt(40, 1.0)};

    entity.tracks.insert(QStringLiteral("x"), xTrack);
    entity.tracks.insert(QStringLiteral("rotation"), rotationTrack);
    entity.tracks.insert(QStringLiteral("visible"), visibleTrack);

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
    CHECK(parsed->tracks.size() == 3);
    CHECK(parsed->tracks.value(QStringLiteral("x")).keys.size() == 2);
    CHECK(parsed->tracks.value(QStringLiteral("rotation"))
              .keys.at(0)
              .easing.type
          == QStringLiteral("bezier"));
    CHECK(nearly(parsed->tracks.value(QStringLiteral("rotation"))
                     .keys.at(0)
                     .easing.outY,
                 1.2));
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

    Track track;
    track.keys = {keyframe, keyAt(20, 9.0)};
    const QJsonArray trackJson = toJson(track);
    const auto parsedTrack = trackFromJson(trackJson, &error);
    CHECK_DETAIL(parsedTrack.has_value(), error);
    if (parsedTrack)
    {
        CHECK(*parsedTrack == track);
        CHECK(toJson(*parsedTrack) == trackJson);
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
    CHECK(json.value(QStringLiteral("version")).toInt() == 1);
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

void testTrackEvaluation()
{
    Track emptyTrack;
    CHECK(!emptyTrack.valueAt(0).has_value());

    Track linear;
    linear.keys = {keyAt(0, 0.0), keyAt(10, 100.0)};

    auto value = linear.valueAt(-5);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = linear.valueAt(0);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = linear.valueAt(5);
    CHECK(value.has_value() && nearly(*value, 50.0));
    value = linear.valueAt(10);
    CHECK(value.has_value() && nearly(*value, 100.0));
    value = linear.valueAt(50);
    CHECK(value.has_value() && nearly(*value, 100.0));

    Track easeIn;
    Keyframe easeIn0 = keyAt(0, 0.0);
    easeIn0.easing.type = QStringLiteral("easeIn");
    easeIn.keys = {easeIn0, keyAt(8, 100.0)};
    value = easeIn.valueAt(4);
    CHECK(value.has_value() && nearly(*value, 25.0));

    Track easeOut;
    Keyframe easeOut0 = keyAt(0, 0.0);
    easeOut0.easing.type = QStringLiteral("easeOut");
    easeOut.keys = {easeOut0, keyAt(8, 100.0)};
    value = easeOut.valueAt(4);
    CHECK(value.has_value() && nearly(*value, 75.0));

    Track easeInOut;
    Keyframe easeInOut0 = keyAt(0, 0.0);
    easeInOut0.easing.type = QStringLiteral("easeInOut");
    easeInOut.keys = {easeInOut0, keyAt(8, 100.0)};
    value = easeInOut.valueAt(2);
    CHECK(value.has_value() && nearly(*value, 12.5));
    value = easeInOut.valueAt(4);
    CHECK(value.has_value() && nearly(*value, 50.0));

    Track bezier;
    Keyframe bezier0 = keyAt(0, 0.0);
    bezier0.easing.type = QStringLiteral("bezier");
    bezier.keys = {bezier0, keyAt(100, 100.0)};
    value = bezier.valueAt(50);
    CHECK(value.has_value() && nearly(*value, 50.0, 1e-6));
    value = bezier.valueAt(25);
    CHECK(value.has_value() && *value > 0.0 && *value < 25.0);
    value = bezier.valueAt(75);
    CHECK(value.has_value() && *value > 75.0 && *value < 100.0);

    Track stepTrack;
    stepTrack.keys = {keyAt(0, 0.0), keyAt(10, 1.0)};
    value = stepTrack.valueAt(5, true);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = stepTrack.valueAt(9, true);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = stepTrack.valueAt(10, true);
    CHECK(value.has_value() && nearly(*value, 1.0));
    value = stepTrack.valueAt(5, false);
    CHECK(value.has_value() && nearly(*value, 0.5));

    Track unsorted;
    unsorted.keys = {keyAt(10, 100.0), keyAt(0, 0.0)};
    value = unsorted.valueAt(5);
    CHECK(value.has_value() && nearly(*value, 50.0));

    Track unknownEasing;
    Keyframe unknown0 = keyAt(0, 0.0);
    unknown0.easing.type = QStringLiteral("bogus");
    unknownEasing.keys = {unknown0, keyAt(10, 100.0)};
    value = unknownEasing.valueAt(5);
    CHECK(value.has_value() && nearly(*value, 50.0));
}

void testFractionalTrackEvaluation()
{
    Track linear;
    linear.keys = {keyAt(0, 0.0), keyAt(10, 10.0)};

    auto value = evaluateTrack(linear, 5.5);
    CHECK(value.has_value() && nearly(*value, 5.5));
    value = evaluateTrack(linear, 2.25);
    CHECK(value.has_value() && nearly(*value, 2.25));

    // The int overload keeps its old behaviour and clamps outside the keys.
    value = evaluateTrack(linear, 5);
    CHECK(value.has_value() && nearly(*value, 5.0));
    value = evaluateTrack(linear, -0.5);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = evaluateTrack(linear, 20.5);
    CHECK(value.has_value() && nearly(*value, 10.0));

    // Ease-in applies to the fractional position inside the segment.
    Track easeIn;
    Keyframe easeIn0 = keyAt(0, 0.0);
    easeIn0.easing.type = QStringLiteral("easeIn");
    easeIn.keys = {easeIn0, keyAt(8, 100.0)};

    value = evaluateTrack(easeIn, 4.0);
    CHECK(value.has_value() && nearly(*value, 25.0));
    value = evaluateTrack(easeIn, 2.5); // t = 0.3125 -> eased t^2
    CHECK(value.has_value() && nearly(*value, 100.0 * 0.3125 * 0.3125));

    // Step tracks take the left value of floor(frame).
    Track step;
    step.keys = {keyAt(0, 1.0), keyAt(5, 0.0), keyAt(10, 1.0)};

    value = evaluateTrack(step, 5.9, true);
    CHECK(value.has_value() && nearly(*value, 0.0));
    value = evaluateTrack(step, 4.9, true);
    CHECK(value.has_value() && nearly(*value, 1.0));
    value = evaluateTrack(step, 10.0, true);
    CHECK(value.has_value() && nearly(*value, 1.0));
    value = evaluateTrack(step, 10.9, true);
    CHECK(value.has_value() && nearly(*value, 1.0));
    value = evaluateTrack(step, 2.5, false);
    CHECK(value.has_value() && nearly(*value, 0.5));
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
    bad.end = bad.start;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.end = bad.start - 3;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.row = -1;
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.tracks.insert(QStringLiteral("x"), Track{{keyAt(10, 5.0)}});
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.tracks.insert(QStringLiteral("x"), Track{{keyAt(-1, 5.0)}});
    CHECK(!validateEntity(bad, &error));

    bad = valid;
    bad.tracks.insert(QStringLiteral("x"), Track{{keyAt(9, 5.0)}});
    CHECK(validateEntity(bad));

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

    openEnded.solvers.append(makeSolver(SolverEffect::Orbit, 100, -1));
    CHECK(!validateEntity(openEnded, &error));

    Entity gravity = makeEntity(0, 100);
    gravity.solvers = {makeSolver(SolverEffect::Gravity, 20, 40)};
    gravity.tracks.insert(QStringLiteral("x"), Track{{keyAt(20, 0.0)}});
    CHECK(validateEntity(gravity));

    gravity.tracks.insert(QStringLiteral("x"),
                          Track{{keyAt(20, 0.0), keyAt(30, 5.0)}});
    CHECK(!validateEntity(gravity, &error));

    gravity.tracks.insert(QStringLiteral("x"),
                          Track{{keyAt(20, 0.0), keyAt(41, 5.0)}});
    CHECK(validateEntity(gravity));

    gravity.tracks.insert(QStringLiteral("y"), Track{{keyAt(40, 5.0)}});
    CHECK(!validateEntity(gravity, &error));

    gravity.tracks.remove(QStringLiteral("y"));
    gravity.tracks.insert(QStringLiteral("rotation"),
                          Track{{keyAt(30, 45.0)}});
    CHECK(validateEntity(gravity));

    Entity spin = makeEntity(0, 100);
    spin.solvers = {makeSolver(SolverEffect::Spin, 20, 40)};
    spin.tracks.insert(QStringLiteral("rotation"), Track{{keyAt(20, 0.0)}});
    CHECK(validateEntity(spin));

    spin.tracks.insert(QStringLiteral("rotation"),
                       Track{{keyAt(20, 0.0), keyAt(30, 45.0)}});
    CHECK(!validateEntity(spin, &error));

    spin.tracks.insert(QStringLiteral("rotation"), Track{{keyAt(20, 0.0)}});
    spin.tracks.insert(QStringLiteral("x"), Track{{keyAt(30, 3.0)}});
    CHECK(validateEntity(spin));

    Entity openDriven = makeEntity(0, 100);
    openDriven.solvers = {makeSolver(SolverEffect::Orbit, 10, -1)};
    openDriven.tracks.insert(QStringLiteral("y"), Track{{keyAt(99, 1.0)}});
    CHECK(!validateEntity(openDriven, &error));
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

    Entity target = makeEntity(0, 100);

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
    Entity second = makeEntity(0, 100);
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
    Entity third = makeEntity(0, 100);
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

void testInvalidJson()
{
    QString error;
    CHECK(!entityFromJson(QJsonObject{}, &error).has_value());
    CHECK(!error.isEmpty());

    QJsonObject badUuid = toJson(makeEntity());
    badUuid.insert(QStringLiteral("uuid"), QStringLiteral("nope"));
    CHECK(!entityFromJson(badUuid, &error).has_value());

    QJsonObject badRange = toJson(makeEntity(0, 10));
    badRange.insert(QStringLiteral("end"), 0);
    CHECK(!entityFromJson(badRange, &error).has_value());

    Entity badKeyframe = makeEntity(0, 10);
    badKeyframe.tracks.insert(QStringLiteral("x"), Track{{keyAt(10, 1.0)}});
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
    badProject.insert(QStringLiteral("version"), 2);
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
    CHECK(parsed->tracks.isEmpty());
    CHECK(parsed->solvers.isEmpty());
    CHECK(parsed->parentUuid.isEmpty());

    QJsonObject missingEnd;
    missingEnd.insert(QStringLiteral("uuid"), newUuid());
    missingEnd.insert(QStringLiteral("start"), 25);
    const auto parsedEnd = entityFromJson(missingEnd, &error);
    CHECK_DETAIL(parsedEnd.has_value(), error);
    if (parsedEnd)
        CHECK(parsedEnd->end == 26);
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
    // Git-friendly output: track storage order must not leak into JSON text.
    Entity a = makeEntity(0, 100);
    a.uuid = QStringLiteral("11111111-1111-4111-8111-111111111111");
    Track xTrack;
    xTrack.keys = {keyAt(0, 1.0), keyAt(10, 2.0)};
    Track yTrack;
    yTrack.keys = {keyAt(0, 3.0)};
    Track visibleTrack;
    visibleTrack.keys = {keyAt(0, 1.0), keyAt(5, 0.0)};

    Entity b = a;
    a.tracks.insert(QStringLiteral("x"), xTrack);
    a.tracks.insert(QStringLiteral("y"), yTrack);
    a.tracks.insert(QStringLiteral("visible"), visibleTrack);
    b.tracks.insert(QStringLiteral("visible"), visibleTrack);
    b.tracks.insert(QStringLiteral("y"), yTrack);
    b.tracks.insert(QStringLiteral("x"), xTrack);

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

struct TestCase
{
    const char* name;
    void (*run)();
};

const TestCase kTests[] = {
    {"uuid", testUuid},
    {"content-json-roundtrip", testContentJsonRoundTrip},
    {"entity-json-roundtrip", testEntityJsonRoundTrip},
    {"json-determinism", testJsonDeterminism},
    {"component-json-roundtrip", testComponentJsonRoundTrip},
    {"project-json-roundtrip", testProjectJsonRoundTrip},
    {"track-evaluation", testTrackEvaluation},
    {"track-evaluation-fractional", testFractionalTrackEvaluation},
    {"easing-presets", testEasingPresets},
    {"easing-json", testEasingJson},
    {"entity-validation", testEntityValidation},
    {"solver-validation", testSolverValidation},
    {"solver-targets", testSolverTargets},
    {"project-validation", testProjectValidation},
    {"invalid-json", testInvalidJson},
    {"defaults", testDefaults},
    {"children-and-root-sorting", testChildrenAndRootSorting},
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
