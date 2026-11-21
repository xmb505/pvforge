#include "workbench/Cache.h"
#include "framecache/CacheUsage.h"
#include "workbench/MetaBuilder.h"
#include "workbench/Qoi.h"
#include "workbench/tests/WorkbenchTestSupport.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QRunnable>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using namespace pvf;
using namespace pvf::workbench::testsupport;

class WorkbenchCacheTests : public QObject
{
    Q_OBJECT

private slots:
    void metaIsStable();
    void metaTracksActiveEntity();
    void metaIgnoresInactiveEntity();
    void metaTracksVisibilityAndContent();
    void metaTracksMissingAsset();
    void metaTracksCameraBackgroundAndTarget();
    void metaTracksHtmlVariables();
    void metaTracksHtmlTimeline();
    void metaTracksContentFields();
    void metaTracksContentTracks();
    void signatureAndSize();
    void renderRectAndExpandedSize();
    void metaTracksExpandAndTransparency();
    void signatureTracksExpandAndTransparency();
    void metaMarksRasterAssets();
    void qoiRoundtrip();
    void qoiRejectsInvalid();
};

void WorkbenchCacheTests::metaIsStable()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    project.settings.width = 1920;
    project.settings.height = 1080;
    project.settings.fps = 30;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    setWholeValue(entity, QStringLiteral("x"), 0, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 100, 200.0);
    project.entities.insert(entity.uuid, entity);
    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::MetaBuilder secondBuilder(project, assets);

    const workbench::FrameMeta first = builder.build(50, QString(), options);
    const workbench::FrameMeta second = secondBuilder.build(50, QString(), options);
    QCOMPARE(first.bytes, second.bytes);
    QCOMPARE(first.sha1, second.sha1);
    QVERIFY(first.equals(second));
}

void WorkbenchCacheTests::metaTracksActiveEntity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    setWholeValue(entity, QStringLiteral("x"), 0, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 50, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 100, 0.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta before50 = builder.build(50, QString(), options);
    const workbench::FrameMeta before150 = builder.build(150, QString(), options);

    project.entities[QStringLiteral("e1")]
        .keyframes[1]
        .x = 25.0;

    const workbench::FrameMeta after50 = builder.build(50, QString(), options);
    const workbench::FrameMeta after150 = builder.build(150, QString(), options);

    QVERIFY(!before50.equals(after50));
    QVERIFY(before150.equals(after150));
}

void WorkbenchCacheTests::metaIgnoresInactiveEntity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("later"), 200, 300);
    setWholeValue(entity, QStringLiteral("x"), 200, 0.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta before = builder.build(150, QString(), options);

    project.entities[QStringLiteral("later")].keyframes[0].x = 999.0;
    const workbench::FrameMeta after = builder.build(150, QString(), options);
    QVERIFY(before.equals(after));
}

void WorkbenchCacheTests::metaTracksVisibilityAndContent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const auto buildWith = [&](const model::Project& changed) {
        const workbench::MetaBuilder builder(changed, assets);
        return builder.build(50, QString(), options);
    };
    const workbench::FrameMeta baseline = buildWith(project);

    {
        model::Project changed = project;
        changed.entities[QStringLiteral("e1")].hidden = true;
        QVERIFY(!baseline.equals(buildWith(changed)));
    }
    {
        model::Project changed = project;
        changed.entities[QStringLiteral("e1")].visible = false;
        QVERIFY(!baseline.equals(buildWith(changed)));
    }
    {
        model::Project changed = project;
        changed.entities[QStringLiteral("e1")].transform.opacity = 0.25;
        QVERIFY(!baseline.equals(buildWith(changed)));
    }
    {
        model::Project changed = project;
        changed.entities[QStringLiteral("e1")].content.text.text =
            QStringLiteral("changed");
        QVERIFY(!baseline.equals(buildWith(changed)));
    }
    {
        model::Project changed = project;
        model::Entity& vector = changed.entities[QStringLiteral("e1")];
        vector.content.kind = ContentKind::Vector;
        vector.content.vector.fillColor = QStringLiteral("#ff0000");
        vector.content.vector.width = 123.0;
        QVERIFY(!baseline.equals(buildWith(changed)));
    }
}

void WorkbenchCacheTests::metaTracksMissingAsset()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QString source = QDir(root.path()).filePath(QStringLiteral("source.png"));
    QVERIFY(image.save(source));

    const auto stored = assets.importFile(source);
    QVERIFY2(stored.has_value(), "image import failed");

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("asset"), 0, 100);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = stored->hash;
    entity.content.asset.type = AssetType::Image;
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta present = builder.build(50, QString(), options);
    QVERIFY(assets.remove(stored->hash));
    const workbench::FrameMeta missing = builder.build(50, QString(), options);
    QVERIFY(!present.equals(missing));
}

void WorkbenchCacheTests::metaTracksCameraBackgroundAndTarget()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    const auto addCamera = [&](const QString& uuid, double x) {
        model::Entity camera;
        camera.uuid = uuid;
        camera.start = 0;
        camera.end = 1000;
        camera.content.kind = ContentKind::Camera;
        camera.transform.x = x;
        project.entities.insert(uuid, camera);
    };
    addCamera(QStringLiteral("camA"), 0.0);
    addCamera(QStringLiteral("camB"), 500.0);
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const auto buildWith = [&](const model::Project& changed) {
        const workbench::MetaBuilder builder(changed, assets);
        return builder.build(50, QStringLiteral("camA"), options);
    };

    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta cameraA = builder.build(50, QStringLiteral("camA"), options);
    const workbench::FrameMeta cameraB = builder.build(50, QStringLiteral("camB"), options);
    QVERIFY(!cameraA.equals(cameraB));

    {
        model::Project changed = project;
        changed.entities[QStringLiteral("camA")].transform.x = 100.0;
        QVERIFY(!cameraA.equals(buildWith(changed)));
    }
    {
        model::Project changed = project;
        changed.settings.background = QStringLiteral("#ffffff");
        QVERIFY(!cameraA.equals(buildWith(changed)));
    }
    {
        workbench::Options changed = options;
        changed.targetSize = QSize(320, 180);
        const workbench::MetaBuilder changedBuilder(project, assets);
        const workbench::FrameMeta other =
            changedBuilder.build(50, QStringLiteral("camA"), changed);
        QVERIFY(!cameraA.equals(other));
    }
}

void WorkbenchCacheTests::metaTracksHtmlVariables()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    const QString source = QDir(root.path()).filePath(QStringLiteral("html"));
    QVERIFY(QDir().mkpath(source));
    writeTextFile(QDir(source).filePath(QStringLiteral("index.html")),
                  QByteArrayLiteral("<html><body></body></html>"));
    writeTextFile(QDir(source).filePath(QStringLiteral("pvhtml.json")),
                  QByteArrayLiteral(
                      "{\"entry\":\"index.html\",\"variables\":{\"progress\":0}}"));

    const auto stored = assets.importPvhtml(source);
    QVERIFY2(stored.has_value(), "pvhtml import failed");

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("html"), 0, 100);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = stored->hash;
    entity.content.asset.type = AssetType::Html;
    setHtmlVar(entity, QStringLiteral("progress"), 0, 0.0);
    setHtmlVar(entity, QStringLiteral("progress"), 100, 100.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta before = builder.build(50, QString(), options);
    project.entities[QStringLiteral("html")]
        .keyframes[0]
        .htmlVars.insert(QStringLiteral("progress"), 50.0);
    const workbench::FrameMeta afterTrack = builder.build(50, QString(), options);
    QVERIFY(!before.equals(afterTrack));

    // Changing the manifest default keeps the same asset hash but must still
    // invalidate the frame.
    const QString metadataPath =
        QDir(stored->directory).filePath(QStringLiteral("asset.json"));
    QFile metadataFile(metadataPath);
    QVERIFY(metadataFile.open(QIODevice::ReadOnly));
    QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    metadataFile.close();
    QJsonObject variables = metadata.value(QStringLiteral("variables")).toObject();
    variables.insert(QStringLiteral("progress"), 42);
    metadata.insert(QStringLiteral("variables"), variables);
    QVERIFY(metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(metadataFile.write(QJsonDocument(metadata).toJson()) > 0);
    metadataFile.close();

    const workbench::FrameMeta afterDefault = builder.build(50, QString(), options);
    QVERIFY(!afterTrack.equals(afterDefault));
}

void WorkbenchCacheTests::metaTracksHtmlTimeline()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    const QString source = QDir(root.path()).filePath(QStringLiteral("html"));
    QVERIFY(QDir().mkpath(source));
    writeTextFile(QDir(source).filePath(QStringLiteral("index.html")),
                  QByteArrayLiteral("<html><body></body></html>"));
    writeTextFile(QDir(source).filePath(QStringLiteral("pvhtml.json")),
                  QByteArrayLiteral(
                      "{\"entry\":\"index.html\",\"variables\":{\"progress\":0}}"));

    const auto stored = assets.importPvhtml(source);
    QVERIFY2(stored.has_value(), "pvhtml import failed");

    model::Project project;
    project.settings.fps = 30;
    model::Entity entity = textEntity(QStringLiteral("html"), 0, 100);
    entity.content.kind = ContentKind::Asset;
    entity.content.asset.hash = stored->hash;
    entity.content.asset.type = AssetType::Html;
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);

    const QString metadataPath =
        QDir(stored->directory).filePath(QStringLiteral("asset.json"));
    const auto editMetadata =
        [&](const std::function<void(QJsonObject&)>& change) {
            QFile file(metadataPath);
            if (!file.open(QIODevice::ReadOnly))
                return false;
            QJsonObject metadata =
                QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            change(metadata);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            const bool written = file.write(QJsonDocument(metadata).toJson()) > 0;
            file.close();
            return written;
        };

    // timeDependent defaults to true: the entity-relative frame and time are
    // part of the meta, so moving the clip invalidates every frame.
    const workbench::FrameMeta atZero = builder.build(50, QString(), options);
    project.entities[QStringLiteral("html")].start = 10;
    const workbench::FrameMeta moved = builder.build(50, QString(), options);
    QVERIFY(!atZero.equals(moved));

    // timeDependent=false always requests localFrame 0/time 0, so the same
    // move must not change the meta (pose and variables are unchanged here).
    QVERIFY(editMetadata([](QJsonObject& metadata) {
        metadata.insert(QStringLiteral("timeDependent"), false);
    }));
    project.entities[QStringLiteral("html")].start = 0;
    const workbench::FrameMeta staticAtZero = builder.build(50, QString(), options);
    project.entities[QStringLiteral("html")].start = 10;
    const workbench::FrameMeta staticMoved = builder.build(50, QString(), options);
    QVERIFY(staticAtZero.equals(staticMoved));

    // A missing metadata field keeps the documented default of true.
    QVERIFY(editMetadata([](QJsonObject& metadata) {
        metadata.remove(QStringLiteral("timeDependent"));
    }));
    project.entities[QStringLiteral("html")].start = 0;
    const workbench::FrameMeta defaultAtZero = builder.build(50, QString(), options);
    project.entities[QStringLiteral("html")].start = 10;
    const workbench::FrameMeta defaultMoved = builder.build(50, QString(), options);
    QVERIFY(!defaultAtZero.equals(defaultMoved));
}

void WorkbenchCacheTests::metaTracksContentFields()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    using Mutation = std::function<void(model::Content&)>;

    // Every definition field that can change the picture must invalidate an
    // active frame and leave an inactive frame alone.  Animation fields are
    // covered by metaTracksContentTracks below.
    const auto verifyKind = [&](ContentKind kind, const QList<Mutation>& mutations) {
        model::Project project;
        model::Entity entity = textEntity(QStringLiteral("g"), 0, 100);
        entity.content.kind = kind;
        project.entities.insert(entity.uuid, entity);

        const workbench::Options options = metaOptions(project);
        const workbench::MetaBuilder builder(project, assets);
        const workbench::FrameMeta active = builder.build(50, QString(), options);
        const workbench::FrameMeta inactive = builder.build(150, QString(), options);

        const workbench::MetaBuilder repeat(project, assets);
        QCOMPARE(repeat.build(50, QString(), options).bytes, active.bytes);

        for (int i = 0; i < mutations.size(); ++i)
        {
            model::Project changed = project;
            mutations.at(i)(changed.entities[QStringLiteral("g")].content);
            const workbench::MetaBuilder changedBuilder(changed, assets);

            const QString label =
                QStringLiteral("kind %1 field %2").arg(int(kind)).arg(i);
            QVERIFY2(!active.equals(changedBuilder.build(50, QString(), options)),
                     qPrintable(label + QStringLiteral(" did not invalidate")));
            QVERIFY2(inactive.equals(changedBuilder.build(150, QString(), options)),
                     qPrintable(label
                                + QStringLiteral(" invalidated an inactive frame")));
        }
    };

    verifyKind(ContentKind::Text,
               {
                   [](model::Content& c) { c.text.text = QStringLiteral("boom"); },
                   [](model::Content& c) { c.text.fontFamily = QStringLiteral("Serif"); },
                   [](model::Content& c) { c.text.color = QStringLiteral("#ff0000"); },
                   [](model::Content& c) { c.text.strokeColor = QStringLiteral("#00ff00"); },
                   [](model::Content& c) { c.text.align = 1; },
                   [](model::Content& c) { c.text.bold = !c.text.bold; },
                   [](model::Content& c) { c.text.italic = !c.text.italic; },
                   [](model::Content& c) { c.text.underline = !c.text.underline; },
                   [](model::Content& c) { c.text.vertical = !c.text.vertical; },
                   [](model::Content& c) { c.text.verticalRtl = !c.text.verticalRtl; },
               });

    verifyKind(ContentKind::Vector,
               {
                   [](model::Content& c) { c.vector.shape += 1; },
                   [](model::Content& c) { c.vector.sides += 1; },
                   [](model::Content& c) { c.vector.innerRatio += 0.01; },
                   [](model::Content& c) { c.vector.fillColor = QStringLiteral("#0000ff"); },
                   [](model::Content& c) { c.vector.strokeColor = QStringLiteral("#123456"); },
               });
}

void WorkbenchCacheTests::metaTracksContentTracks()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    // Every content animation parameter that can be keyed must reach the frame
    // meta as its evaluated value.  A field dropped from animatedContent shows
    // up here as a mutation that no longer invalidates the active frame.
    const auto verifyTrack = [&](ContentKind kind, const QString& property,
                                 double first, double second,
                                 double changedSecond) {
        model::Project project;
        model::Entity entity = textEntity(QStringLiteral("c"), 0, 100);
        entity.content.kind = kind;
        setContentValue(entity, property, 0, first);
        setContentValue(entity, property, 100, second);
        project.entities.insert(entity.uuid, entity);

        const workbench::Options options = metaOptions(project);
        const workbench::MetaBuilder builder(project, assets);
        const workbench::FrameMeta active = builder.build(50, QString(), options);
        const workbench::FrameMeta inactive =
            builder.build(150, QString(), options);

        const QString label = property;
        QVERIFY2(!active.equals(builder.build(60, QString(), options)),
                 qPrintable(label + QStringLiteral(" made two frames identical")));

        model::Project mutated = project;
        mutated.entities[QStringLiteral("c")]
            .keyframes[1]
            .content.insert(property, changedSecond);
        const workbench::MetaBuilder changed(mutated, assets);
        QVERIFY2(!active.equals(changed.build(50, QString(), options)),
                 qPrintable(label + QStringLiteral(" did not invalidate")));
        QVERIFY2(inactive.equals(changed.build(150, QString(), options)),
                 qPrintable(label
                            + QStringLiteral(" invalidated an inactive frame")));
    };

    verifyTrack(ContentKind::Text, QStringLiteral("pixelSize"), 20.0, 120.0,
                400.0);
    verifyTrack(ContentKind::Text, QStringLiteral("lineHeight"), 1.0, 2.0, 3.0);
    verifyTrack(ContentKind::Text, QStringLiteral("letterSpacing"), 0.0, 10.0,
                30.0);
    verifyTrack(ContentKind::Text, QStringLiteral("padding"), 2.0, 22.0, 42.0);
    verifyTrack(ContentKind::Text, QStringLiteral("boxWidth"), 100.0, 300.0,
                500.0);
    verifyTrack(ContentKind::Text, QStringLiteral("boxHeight"), 50.0, 150.0,
                250.0);
    verifyTrack(ContentKind::Text, QStringLiteral("strokeWidth"), 1.0, 3.0, 5.0);

    verifyTrack(ContentKind::Vector, QStringLiteral("width"), 20.0, 220.0,
                420.0);
    verifyTrack(ContentKind::Vector, QStringLiteral("height"), 10.0, 110.0,
                210.0);
    verifyTrack(ContentKind::Vector, QStringLiteral("cornerRadius"), 0.0, 40.0,
                80.0);
    verifyTrack(ContentKind::Vector, QStringLiteral("strokeWidth"), 0.0, 8.0,
                16.0);
}

void WorkbenchCacheTests::signatureAndSize()
{
    model::Project project;
    project.settings.width = 1920;
    project.settings.height = 1080;
    project.settings.fps = 30;

    QCOMPARE(workbench::cacheSizeFor(project, 50, 1280), QSize(960, 540));
    QCOMPARE(workbench::cacheSizeFor(project, 100, 1280), QSize(1280, 720));
    QCOMPARE(workbench::cacheSizeFor(project, 25, 0), QSize(480, 270));

    workbench::Options options;
    options.scalePercent = 50;
    options.chromeVersion = QStringLiteral("chrome-1");
    const QString signature = workbench::renderSignature(project, options);
    QVERIFY(!signature.isEmpty());

    workbench::Options otherScale = options;
    otherScale.scalePercent = 75;
    QVERIFY(signature != workbench::renderSignature(project, otherScale));

    // Chrome only participates when the project actually contains HTML.
    workbench::Options noChrome = options;
    noChrome.chromeVersion.clear();
    QCOMPARE(signature, workbench::renderSignature(project, noChrome));

    model::Entity html = textEntity(QStringLiteral("html"), 0, 100);
    html.content.kind = ContentKind::Asset;
    html.content.asset.type = AssetType::Html;
    project.entities.insert(html.uuid, html);
    QVERIFY(signature != workbench::renderSignature(project, noChrome));
    QVERIFY(workbench::renderSignature(project, options)
            != workbench::renderSignature(project, noChrome));
}

void WorkbenchCacheTests::renderRectAndExpandedSize()
{
    model::Project project;
    project.settings.width = 1920;
    project.settings.height = 1080;

    // Model space is centred on (0,0), including no expansion.
    QCOMPARE(workbench::renderRect(project, 0),
             QRectF(-960, -540, 1920, 1080));
    QCOMPARE(workbench::renderRect(project, 50),
             QRectF(-1920, -1080, 3840, 2160));
    QCOMPARE(workbench::renderRect(project, 200),
             QRectF(-4800, -2700, 9600, 5400));
    QCOMPARE(workbench::renderRect(project, -10),
             QRectF(-960, -540, 1920, 1080));
    QCOMPARE(workbench::renderRect(project, 500),
             workbench::renderRect(project, 200));

    // The 3-argument overload keeps the canvas-only size.
    QCOMPARE(workbench::cacheSizeFor(project, 50, 0), QSize(960, 540));
    QCOMPARE(workbench::cacheSizeFor(project, 50, 0, 0), QSize(960, 540));
    QCOMPARE(workbench::cacheSizeFor(project, 50, 0, 50), QSize(1920, 1080));
    QCOMPARE(workbench::cacheSizeFor(project, 50, 1280, 50), QSize(1280, 720));
    QCOMPARE(workbench::cacheSizeFor(project, 25, 0, 200), QSize(2400, 1350));
}

void WorkbenchCacheTests::metaTracksExpandAndTransparency()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    project.settings.width = 320;
    project.settings.height = 180;
    project.settings.fps = 30;
    const model::Entity entity = textEntity(QStringLiteral("e1"), 0, 30);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options canvas = metaOptions(project, 50, 1280, 0);
    const workbench::Options expanded = metaOptions(project, 50, 1280, 50);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta canvasMeta = builder.build(5, QString(), canvas);
    const workbench::FrameMeta expandedMeta =
        builder.build(5, QString(), expanded);
    QVERIFY(!canvasMeta.equals(expandedMeta));

    workbench::Options transparent = expanded;
    transparent.transparentOutsideCanvas = true;
    const workbench::FrameMeta transparentMeta =
        builder.build(5, QString(), transparent);
    QVERIFY(!expandedMeta.equals(transparentMeta));

    workbench::Options wider = metaOptions(project, 50, 1280, 100);
    QVERIFY(!expandedMeta.equals(builder.build(5, QString(), wider)));
}

void WorkbenchCacheTests::signatureTracksExpandAndTransparency()
{
    model::Project project;
    project.settings.width = 320;
    project.settings.height = 180;
    project.settings.fps = 30;

    workbench::Options options;
    options.scalePercent = 50;
    options.expandPercent = 0;
    const QString canvas = workbench::renderSignature(project, options);

    workbench::Options expanded = options;
    expanded.expandPercent = 50;
    QVERIFY(canvas != workbench::renderSignature(project, expanded));

    workbench::Options transparent = expanded;
    transparent.transparentOutsideCanvas = true;
    QVERIFY(workbench::renderSignature(project, expanded)
            != workbench::renderSignature(project, transparent));
}

void WorkbenchCacheTests::metaMarksRasterAssets()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    const workbench::Options options = metaOptions(model::Project{});

    const auto buildWith = [&](model::Entity entity) {
        model::Project project;
        project.entities.insert(entity.uuid, entity);
        const workbench::MetaBuilder builder(project, assets);
        return builder.build(50, QString(), options);
    };

    QVERIFY(!buildWith(textEntity(QStringLiteral("text"), 0, 100)).hasRaster);
    QVERIFY(!buildWith(assetEntity(QStringLiteral("audio"), 0, 100,
                                   AssetType::Audio))
                 .hasRaster);
    for (const AssetType type :
         {AssetType::Image, AssetType::Video, AssetType::Html})
        QVERIFY2(buildWith(assetEntity(QStringLiteral("raster"), 0, 100, type))
                     .hasRaster,
                 qPrintable(QStringLiteral("type %1 not marked raster")
                                .arg(int(type))));

    // Only entities visible at the requested frame count.
    QVERIFY(!buildWith(assetEntity(QStringLiteral("later"), 200, 300))
                 .hasRaster);
    model::Entity hidden = assetEntity(QStringLiteral("hidden"), 0, 100);
    hidden.hidden = true;
    QVERIFY(!buildWith(hidden).hasRaster);
    model::Entity invisible = assetEntity(QStringLiteral("invisible"), 0, 100);
    invisible.visible = false;
    QVERIFY(!buildWith(invisible).hasRaster);
}

void WorkbenchCacheTests::qoiRoundtrip()
{
    // Solid colour: long runs and the end-marker path.
    QImage solid(96, 64, QImage::Format_RGBA8888);
    solid.fill(QColor(24, 130, 210, 255));
    const QByteArray solidData = workbench::encodeQoi(solid);
    QVERIFY(!solidData.isEmpty());
    QCOMPARE(solidData.left(4), QByteArrayLiteral("qoif"));
    QVERIFY(samePixels(workbench::decodeQoi(solidData), solid));

    // Gradient with alpha, including transparent pixels.
    QImage gradient(320, 180, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&gradient);
        QLinearGradient linear(0, 0, 320, 180);
        linear.setColorAt(0.0, QColor(255, 0, 0, 0));
        linear.setColorAt(0.5, QColor(0, 255, 0, 128));
        linear.setColorAt(1.0, QColor(0, 0, 255, 255));
        painter.fillRect(gradient.rect(), linear);
    }
    const QImage gradientData =
        gradient.convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(samePixels(workbench::decodeQoi(workbench::encodeQoi(gradient)),
                       gradientData));

    // Random pixels exercise RGB/RGBA/DIFF/LUMA/INDEX/RUN.
    QImage random(73, 41, QImage::Format_RGBA8888);
    QRandomGenerator generator(0x51u);
    for (int y = 0; y < random.height(); ++y)
    {
        uchar* line = random.scanLine(y);
        for (int x = 0; x < random.width(); ++x)
        {
            line[x * 4] = uchar(generator.bounded(256));
            line[x * 4 + 1] = uchar(generator.bounded(256));
            line[x * 4 + 2] = uchar(generator.bounded(256));
            line[x * 4 + 3] = uchar(generator.bounded(256));
        }
    }
    const QByteArray randomData = workbench::encodeQoi(random);
    QVERIFY(!randomData.isEmpty());
    QVERIFY(samePixels(workbench::decodeQoi(randomData), random));
}

void WorkbenchCacheTests::qoiRejectsInvalid()
{
    QVERIFY(workbench::encodeQoi(QImage()).isEmpty());
    QVERIFY(workbench::decodeQoi({}).isNull());
    QVERIFY(workbench::decodeQoi(QByteArrayLiteral("not a qoi stream")).isNull());

    QImage image(37, 19, QImage::Format_RGBA8888);
    QRandomGenerator generator(7u);
    for (int y = 0; y < image.height(); ++y)
    {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < image.width() * 4; ++x)
            line[x] = uchar(generator.bounded(256));
    }
    const QByteArray data = workbench::encodeQoi(image);
    QVERIFY(!data.isEmpty());

    QByteArray badMagic = data;
    badMagic[0] = 'x';
    QVERIFY(workbench::decodeQoi(badMagic).isNull());

    QByteArray badChannels = data;
    badChannels[12] = 2;
    QVERIFY(workbench::decodeQoi(badChannels).isNull());

    QByteArray badColorspace = data;
    badColorspace[13] = 2;
    QVERIFY(workbench::decodeQoi(badColorspace).isNull());

    QVERIFY(workbench::decodeQoi(data.left(14)).isNull());
    QVERIFY(workbench::decodeQoi(data.left(data.size() / 2)).isNull());
    QVERIFY(workbench::decodeQoi(data.left(data.size() - 1)).isNull());
}

QTEST_MAIN(WorkbenchCacheTests)
#include "WorkbenchCacheTests.moc"
