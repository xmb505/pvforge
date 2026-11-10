#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace pvf;

namespace {

model::Entity textEntity(const Uuid& uuid, int start, int end)
{
    model::Entity entity;
    entity.uuid = uuid;
    entity.name = uuid;
    entity.start = start;
    entity.end = end;
    entity.content.kind = ContentKind::Text;
    entity.content.text.text = QStringLiteral("hello");
    entity.content.text.pixelSize = 64;
    return entity;
}

void addKey(model::Entity& entity, const QString& track, int frame, double value)
{
    model::Keyframe key;
    key.frame = frame;
    key.value = value;
    entity.tracks[track].keys.append(key);
}

workbench::Options metaOptions(const model::Project& project,
                               int scalePercent = 50, int maxWidth = 1280)
{
    workbench::Options options;
    options.scalePercent = scalePercent;
    options.maxWidth = maxWidth;
    options.targetSize = workbench::cacheSizeFor(project, scalePercent, maxWidth);
    return options;
}

QImage testImage(const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(32, 120, 200));
    return image;
}

void writeTextFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

} // namespace

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
    void signatureAndSize();
    void cacheStoreLookup();
    void cacheMetaMismatch();
    void cacheSurvivesRestart();
    void cacheDiskLru();
    void cacheMemoryBudget();
    void cacheClear();
    void cachePerCameraDirectories();
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
    addKey(entity, QStringLiteral("x"), 0, 0.0);
    addKey(entity, QStringLiteral("x"), 100, 200.0);
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
    addKey(entity, QStringLiteral("x"), 0, 0.0);
    addKey(entity, QStringLiteral("x"), 50, 0.0);
    addKey(entity, QStringLiteral("x"), 100, 0.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta before50 = builder.build(50, QString(), options);
    const workbench::FrameMeta before150 = builder.build(150, QString(), options);

    project.entities[QStringLiteral("e1")].tracks[QStringLiteral("x")].keys[1].value =
        25.0;

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
    addKey(entity, QStringLiteral("x"), 200, 0.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta before = builder.build(150, QString(), options);

    project.entities[QStringLiteral("later")].tracks[QStringLiteral("x")].keys[0].value =
        999.0;
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
    addKey(entity, QStringLiteral("var.progress"), 0, 0.0);
    addKey(entity, QStringLiteral("var.progress"), 100, 100.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    workbench::MetaBuilder builder(project, assets);

    const workbench::FrameMeta before = builder.build(50, QString(), options);
    project.entities[QStringLiteral("html")]
        .tracks[QStringLiteral("var.progress")]
        .keys[0]
        .value = 50.0;
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

void WorkbenchCacheTests::cacheStoreLookup()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(10, QString(), options);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QCOMPARE(cache.imageSize(), options.targetSize);

    QString error;
    QVERIFY2(cache.store(10, meta, testImage(QSize(1920, 1080)), &error),
             qPrintable(error));

    const QString path =
        QDir(cache.directory()).filePath(QStringLiteral("10.webp"));
    QVERIFY(QFileInfo::exists(path));
    QImageReader reader(path, QByteArrayLiteral("webp"));
    QCOMPARE(reader.read().size(), options.targetSize);

    const workbench::CacheStats stored = cache.stats();
    QCOMPARE(stored.entries, 1);
    QCOMPARE(stored.stores, quint64(1));
    QVERIFY(stored.diskBytes > 0);
    QCOMPARE(stored.budgetBytes, options.diskBudgetBytes);

    bool hit = false;
    const QImage image = cache.lookup(10, meta, &hit);
    QVERIFY(hit);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), options.targetSize);
    QCOMPARE(cache.stats().hits, quint64(1));
    QVERIFY(cache.matches(10, meta));
    QVERIFY(cache.cachedFrames() == QSet<int>{10});
}

void WorkbenchCacheTests::cacheMetaMismatch()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(10, QString(), options);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(10, meta, testImage(QSize(640, 360))));

    const QString path =
        QDir(cache.directory()).filePath(QStringLiteral("10.webp"));
    QVERIFY(QFileInfo::exists(path));

    model::Project changed = project;
    changed.settings.background = QStringLiteral("#ffffff");
    const workbench::MetaBuilder changedBuilder(changed, assets);
    const workbench::FrameMeta other = changedBuilder.build(10, QString(), options);

    bool hit = true;
    const QImage image = cache.lookup(10, other, &hit);
    QVERIFY(!hit);
    QVERIFY(image.isNull());
    QCOMPARE(cache.stats().misses, quint64(1));
    QVERIFY(cache.matches(10, meta));
    QVERIFY(!cache.matches(10, other));
    // A miss must not delete the image; the user may undo the edit.
    QVERIFY(QFileInfo::exists(path));
}

void WorkbenchCacheTests::cacheSurvivesRestart()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(7, QString(), options);
    const QString cacheRoot = QDir(root.path()).filePath(QStringLiteral("cache"));

    {
        workbench::Cache cache(cacheRoot, QString(), options);
        QVERIFY2(cache.isValid(), qPrintable(cache.error()));
        QVERIFY(cache.store(7, meta, testImage(QSize(640, 360))));
    }

    workbench::Cache cache(cacheRoot, QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QCOMPARE(cache.stats().entries, 1);
    QVERIFY(cache.matches(7, meta));

    bool hit = false;
    const QImage image = cache.lookup(7, meta, &hit);
    QVERIFY(hit);
    QVERIFY(!image.isNull());
    QCOMPARE(cache.stats().hits, quint64(1));
}

void WorkbenchCacheTests::cacheDiskLru()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(1, QString(), options);
    const workbench::FrameMeta second = builder.build(2, QString(), options);
    const QImage image = testImage(QSize(640, 360));
    const QString cacheRoot = QDir(root.path()).filePath(QStringLiteral("cache"));

    workbench::Options generous = options;
    generous.diskBudgetBytes = 1LL << 40;
    qint64 frameBytes = 0;
    {
        workbench::Cache probe(cacheRoot, QString(), generous);
        QVERIFY2(probe.isValid(), qPrintable(probe.error()));
        QVERIFY(probe.store(1, first, image));
        frameBytes = probe.stats().diskBytes;
    }
    QVERIFY(frameBytes > 0);

    workbench::Options small = options;
    small.diskBudgetBytes = frameBytes + frameBytes / 2;
    workbench::Cache cache(cacheRoot, QString(), small);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QCOMPARE(cache.stats().entries, 1);
    QVERIFY(cache.cachedFrames().contains(1));

    QVERIFY(cache.store(2, second, image));

    const QSet<int> frames = cache.cachedFrames();
    QVERIFY(!frames.contains(1));
    QVERIFY(frames.contains(2));
    QCOMPARE(cache.stats().evictions, quint64(1));
    QVERIFY(!QFileInfo::exists(QDir(cache.directory()).filePath(QStringLiteral("1.webp"))));
    QVERIFY(QFileInfo::exists(QDir(cache.directory()).filePath(QStringLiteral("2.webp"))));
}

void WorkbenchCacheTests::cacheMemoryBudget()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    workbench::Options options = metaOptions(project);
    options.memoryBudgetBytes = 1; // too small for any decoded frame
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(4, QString(), options);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(4, meta, testImage(QSize(640, 360))));
    QCOMPARE(cache.stats().memoryFrames, 0);

    bool hit = false;
    const QImage image = cache.lookup(4, meta, &hit);
    QVERIFY(hit);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), options.targetSize);
    QCOMPARE(cache.stats().memoryFrames, 0); // decoded again on every read
    QCOMPARE(cache.stats().hits, quint64(1));
}

void WorkbenchCacheTests::cacheClear()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const QImage image = testImage(QSize(640, 360));

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(1, builder.build(1, QString(), options), image));
    QVERIFY(cache.store(2, builder.build(2, QString(), options), image));
    QCOMPARE(cache.stats().entries, 2);

    const QString firstPath =
        QDir(cache.directory()).filePath(QStringLiteral("1.webp"));
    cache.clear();

    QVERIFY(cache.cachedFrames().isEmpty());
    const workbench::CacheStats stats = cache.stats();
    QCOMPARE(stats.entries, 0);
    QCOMPARE(stats.diskBytes, 0);
    QCOMPARE(stats.memoryFrames, 0);
    QVERIFY(!QFileInfo::exists(firstPath));

    bool hit = true;
    QVERIFY(cache.lookup(1, builder.build(1, QString(), options), &hit).isNull());
    QVERIFY(!hit);
}

void WorkbenchCacheTests::cachePerCameraDirectories()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(3, QString(), options);
    const QString cacheRoot = QDir(root.path()).filePath(QStringLiteral("cache"));

    workbench::Cache cameraA(cacheRoot, QStringLiteral("camA"), options);
    workbench::Cache cameraB(cacheRoot, QStringLiteral("camB"), options);
    QVERIFY2(cameraA.isValid(), qPrintable(cameraA.error()));
    QVERIFY2(cameraB.isValid(), qPrintable(cameraB.error()));
    QVERIFY(cameraA.directory() != cameraB.directory());
    QVERIFY(cameraA.directory().endsWith(QStringLiteral("/camA")));
    QVERIFY(cameraB.directory().endsWith(QStringLiteral("/camB")));

    QVERIFY(cameraA.store(3, meta, testImage(QSize(640, 360))));
    QVERIFY(cameraB.cachedFrames().isEmpty());
    bool hit = true;
    QVERIFY(cameraB.lookup(3, meta, &hit).isNull());
    QVERIFY(!hit);
}

QTEST_MAIN(WorkbenchCacheTests)
#include "WorkbenchCacheTests.moc"
