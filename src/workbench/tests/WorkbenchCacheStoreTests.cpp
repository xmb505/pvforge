#include "framecache/CacheUsage.h"
#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"
#include "workbench/Qoi.h"
#include "workbench/tests/WorkbenchTestSupport.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QRunnable>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <array>
#include <memory>
#include <utility>
#include <vector>

using namespace pvf;
using namespace pvf::workbench::testsupport;

class WorkbenchCacheStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void cacheStoreLookup();
    void cacheEncodeStoreEncoded();
    void cacheEncodesWebpOnWorkerThread();
    void cacheEncodesWebpConcurrently();
    void cacheMetaMismatch();
    void cacheSurvivesRestart();
    void cacheDiskLru();
    void cacheMemoryBudget();
    void cacheClear();
    void cacheCleansOrphanFrames();
    void cacheStoresRasterAsWebp();
    void cacheMigratesLegacyIndexWithoutFalseHits();
    void cacheSwitchesCodecWhenRasterChanges();
    void cachePerCameraDirectories();
    void cacheUsageMatchesStats();
    void audioOnlyFramesShareOneRecord();
    void audioAncestorKeepsTransformWithoutPlaybackState();
    void staticEntityReusesFrameZero();
    void dynamicMediaDoesNotReuseFrameZero();
    void editAndUndoInvalidateThenHit();
    void emptyAndNonVisualFramesShareOneMeta();
};

void WorkbenchCacheStoreTests::cacheStoreLookup()
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
    const QString key = workbench::frameContentKey(meta);
    QCOMPARE(key.size(), 40);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QCOMPARE(cache.imageSize(), options.targetSize);

    QString error;
    QVERIFY2(cache.store(10, meta, testImage(QSize(1920, 1080)), &error),
             qPrintable(error));

    const QString path = contentPath(cache, meta, QStringLiteral("qoi"));
    QCOMPARE(path, QDir(cache.directory()).filePath(key + QStringLiteral(".qoi")));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(contentPath(cache, meta, QStringLiteral("webp"))));
    QFile qoiFile(path);
    QVERIFY(qoiFile.open(QIODevice::ReadOnly));
    const QByteArray qoiData = qoiFile.readAll();
    QCOMPARE(qoiData.left(4), QByteArrayLiteral("qoif"));
    const QImage decoded = workbench::decodeQoi(qoiData);
    QCOMPARE(decoded.size(), options.targetSize);
    QCOMPARE(decoded.pixelColor(0, 0), QColor(32, 120, 200));

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

void WorkbenchCacheStoreTests::cacheEncodeStoreEncoded()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta vectorMeta =
        builder.build(10, QString(), options);

    // A different picture with raster content: a distinct content key, so the
    // vector QOI and the raster WebP coexist as two records.
    model::Project rasterProject = project;
    const model::Entity image =
        assetEntity(QStringLiteral("image"), 0, 100, AssetType::Image);
    rasterProject.entities.insert(image.uuid, image);
    const workbench::MetaBuilder rasterBuilder(rasterProject, assets);
    const workbench::FrameMeta rasterMeta =
        rasterBuilder.build(11, QString(), options);
    QVERIFY(rasterMeta.hasRaster);
    QVERIFY(workbench::frameContentKey(vectorMeta)
            != workbench::frameContentKey(rasterMeta));

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));

    QImage source(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(40, 90, 160));

    const workbench::EncodedFrame vector =
        workbench::Cache::encode(source, options.targetSize, false);
    QVERIFY(!vector.bytes.isEmpty());
    QCOMPARE(vector.codec, QStringLiteral("qoi"));
    QCOMPARE(vector.image.size(), options.targetSize);
    QVERIFY(cache.storeEncoded(10, vectorMeta, vector));

    const workbench::EncodedFrame raster =
        workbench::Cache::encode(source, options.targetSize, true);
    QVERIFY(!raster.bytes.isEmpty());
    QCOMPARE(raster.codec, QStringLiteral("webp"));
    QCOMPARE(raster.image.size(), options.targetSize);
    QVERIFY(cache.storeEncoded(11, rasterMeta, raster));

    QVERIFY(QFileInfo::exists(contentPath(cache, vectorMeta, QStringLiteral("qoi"))));
    QVERIFY(QFileInfo::exists(contentPath(cache, rasterMeta, QStringLiteral("webp"))));
    QCOMPARE(cache.stats().entries, 2);
    QCOMPARE(cache.stats().stores, quint64(2));

    bool hit = false;
    QVERIFY(!cache.lookup(10, vectorMeta, &hit).isNull());
    QVERIFY(hit);
    QVERIFY(!cache.lookup(11, rasterMeta, &hit).isNull());
    QVERIFY(hit);

    // A failed encode never reaches the index or the disk.
    const workbench::EncodedFrame nullEncoded =
        workbench::Cache::encode(QImage(), options.targetSize, true);
    QVERIFY(nullEncoded.bytes.isEmpty());
    QString error;
    QVERIFY(!cache.storeEncoded(12, rasterMeta, nullEncoded, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(cache.stats().entries, 2);
}

void WorkbenchCacheStoreTests::cacheEncodesWebpOnWorkerThread()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    // The playback writer encodes WebP on a QThreadPool worker.  Guard the
    // plugin against a non-reentrant build and verify the bytes really decode.
    QImage source(640, 360, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&source);
        QLinearGradient gradient(0, 0, 640, 360);
        gradient.setColorAt(0.0, QColor(255, 0, 0));
        gradient.setColorAt(1.0, QColor(0, 0, 255));
        painter.fillRect(source.rect(), gradient);
    }
    const QSize target(320, 180);

    QThreadPool pool;
    pool.setMaxThreadCount(1);
    std::array<QByteArray, 4> encoded;
    std::array<QSize, 4> sizes;
    for (int i = 0; i < int(encoded.size()); ++i)
    {
        pool.start(QRunnable::create([&, i] {
            const workbench::EncodedFrame frame =
                workbench::Cache::encode(source, target, true);
            encoded[size_t(i)] = frame.bytes;
            sizes[size_t(i)] = frame.image.size();
        }));
    }
    QVERIFY(pool.waitForDone(30000));

    for (int i = 0; i < int(encoded.size()); ++i)
    {
        QVERIFY2(!encoded[size_t(i)].isEmpty(),
                 qPrintable(QStringLiteral("worker WebP %1 is empty").arg(i)));
        QCOMPARE(sizes[size_t(i)], target);
        QBuffer buffer(&encoded[size_t(i)]);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QImageReader reader(&buffer, QByteArrayLiteral("webp"));
        const QImage decoded = reader.read();
        QVERIFY2(!decoded.isNull(),
                 qPrintable(reader.errorString()));
        QCOMPARE(decoded.size(), target);
    }
}

void WorkbenchCacheStoreTests::cacheEncodesWebpConcurrently()
{
    // The playback writer may scale the pool past one thread, but the Qt WebP
    // plugin has no documented re-entrancy guarantee.  Encode several distinct
    // 1280x720 frames at the same time and prove every result still decodes to
    // its own picture (no shared encoder state, no cross-thread byte mixups).
    constexpr int kWidth = 1280;
    constexpr int kHeight = 720;
    constexpr int kRounds = 12;
    const QSize target(640, 360);
    const int tolerance = 12;

    const auto makeSource = [](const QColor& color) {
        QImage image(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        painter.fillRect(image.rect(), color);
        painter.fillRect(QRect(0, 0, 320, 180), QColor(0, 0, 0));
        return image;
    };
    const auto close = [tolerance](const QColor& actual, const QColor& wanted) {
        return qAbs(actual.red() - wanted.red()) <= tolerance
               && qAbs(actual.green() - wanted.green()) <= tolerance
               && qAbs(actual.blue() - wanted.blue()) <= tolerance;
    };
    const auto decode = [](const QByteArray& bytes) {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, QByteArrayLiteral("webp"));
        return reader.read();
    };

    for (const int threads : {2, 3})
    {
        QThreadPool pool;
        pool.setMaxThreadCount(threads);
        std::vector<QByteArray> encoded;
        std::vector<QSize> sizes;
        std::vector<QString> failures;
        encoded.resize(size_t(threads));
        sizes.resize(size_t(threads));
        failures.resize(size_t(threads));

        for (int round = 0; round < kRounds; ++round)
        {
            std::vector<QColor> colors;
            std::vector<QImage> sources;
            for (int i = 0; i < threads; ++i)
            {
                const QColor color =
                    QColor::fromHsv((round * 47 + i * 97) % 360, 200, 220);
                colors.push_back(color);
                sources.push_back(makeSource(color));
                encoded[size_t(i)].clear();
                sizes[size_t(i)] = QSize();
                failures[size_t(i)].clear();
            }

            for (int i = 0; i < threads; ++i)
            {
                pool.start(QRunnable::create([&, i] {
                    const workbench::EncodedFrame frame =
                        workbench::Cache::encode(sources[size_t(i)], target,
                                                 true);
                    encoded[size_t(i)] = frame.bytes;
                    sizes[size_t(i)] = frame.image.size();
                    if (frame.bytes.isEmpty())
                    {
                        failures[size_t(i)] = QStringLiteral("empty bytes");
                        return;
                    }
                    const QImage decoded = decode(frame.bytes);
                    if (decoded.isNull())
                    {
                        failures[size_t(i)] = QStringLiteral("decode failed");
                        return;
                    }
                    if (decoded.size() != target)
                    {
                        failures[size_t(i)] =
                            QStringLiteral("decoded %1x%2")
                                .arg(decoded.width())
                                .arg(decoded.height());
                        return;
                    }
                    if (!close(decoded.pixelColor(480, 270), colors[size_t(i)]))
                    {
                        failures[size_t(i)] =
                            QStringLiteral("pixel color %1 != %2")
                                .arg(decoded.pixelColor(480, 270).name(),
                                     colors[size_t(i)].name());
                        return;
                    }
                    if (!close(decoded.pixelColor(20, 20), QColor(0, 0, 0)))
                    {
                        failures[size_t(i)] = QStringLiteral(
                            "black patch turned %1")
                                                  .arg(decoded.pixelColor(20, 20)
                                                           .name());
                    }
                }));
            }
            QVERIFY2(pool.waitForDone(60000),
                     qPrintable(QStringLiteral("threads=%1 round=%2 timed out")
                                    .arg(threads)
                                    .arg(round)));

            for (int i = 0; i < threads; ++i)
            {
                QVERIFY2(failures[size_t(i)].isEmpty(),
                         qPrintable(QStringLiteral(
                                        "threads=%1 round=%2 image=%3: %4")
                                        .arg(threads)
                                        .arg(round)
                                        .arg(i)
                                        .arg(failures[size_t(i)])));
                QVERIFY(!encoded[size_t(i)].isEmpty());
                QCOMPARE(sizes[size_t(i)], target);
            }
            // Different pictures must never produce the same bytes; a shared
            // scratch buffer would show up as duplicates here.
            for (int a = 0; a < threads; ++a)
                for (int b = a + 1; b < threads; ++b)
                    QVERIFY2(encoded[size_t(a)] != encoded[size_t(b)],
                             qPrintable(QStringLiteral(
                                            "threads=%1 round=%2: pictures %3 "
                                            "and %4 encoded identically")
                                            .arg(threads)
                                            .arg(round)
                                            .arg(a)
                                            .arg(b)));
        }
    }
}

void WorkbenchCacheStoreTests::cacheMetaMismatch()
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

    const QString path = contentPath(cache, meta, QStringLiteral("qoi"));
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

void WorkbenchCacheStoreTests::cacheSurvivesRestart()
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

    // The disk index only knows content keys; a reopened process learns the
    // frame number again when it looks the frame up (or stores it).
    QVERIFY(cache.cachedFrames().isEmpty());
    bool hit = false;
    const QImage image = cache.lookup(7, meta, &hit);
    QVERIFY(hit);
    QVERIFY(!image.isNull());
    QCOMPARE(cache.stats().hits, quint64(1));
    QVERIFY(cache.cachedFrames().contains(7));
}

void WorkbenchCacheStoreTests::cacheDiskLru()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    setWholeValue(entity, QStringLiteral("x"), 0, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 100, 100.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(1, QString(), options);
    const workbench::FrameMeta second = builder.build(2, QString(), options);
    QVERIFY(!first.equals(second));
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
    bool hit = false;
    QVERIFY(!cache.lookup(1, first, &hit).isNull());
    QVERIFY(hit);
    QVERIFY(cache.cachedFrames().contains(1));

    QVERIFY(cache.store(2, second, image));

    const QSet<int> frames = cache.cachedFrames();
    QVERIFY(!frames.contains(1));
    QVERIFY(frames.contains(2));
    QCOMPARE(cache.stats().evictions, quint64(1));
    QVERIFY(!QFileInfo::exists(contentPath(cache, first, QStringLiteral("qoi"))));
    QVERIFY(QFileInfo::exists(contentPath(cache, second, QStringLiteral("qoi"))));
}

void WorkbenchCacheStoreTests::cacheMemoryBudget()
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

void WorkbenchCacheStoreTests::cacheClear()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    setWholeValue(entity, QStringLiteral("x"), 0, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 100, 100.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const QImage image = testImage(QSize(640, 360));

    const workbench::FrameMeta first = builder.build(1, QString(), options);
    const workbench::FrameMeta second = builder.build(2, QString(), options);
    QVERIFY(!first.equals(second));

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(1, first, image));
    QVERIFY(cache.store(2, second, image));
    QCOMPARE(cache.stats().entries, 2);

    const QString firstPath = contentPath(cache, first, QStringLiteral("qoi"));
    cache.clear();

    QVERIFY(cache.cachedFrames().isEmpty());
    const workbench::CacheStats stats = cache.stats();
    QCOMPARE(stats.entries, 0);
    QCOMPARE(stats.diskBytes, 0);
    QCOMPARE(stats.memoryFrames, 0);
    QVERIFY(!QFileInfo::exists(firstPath));

    bool hit = true;
    QVERIFY(cache.lookup(1, first, &hit).isNull());
    QVERIFY(!hit);
}

void WorkbenchCacheStoreTests::cacheCleansOrphanFrames()
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

    QString directory;
    {
        workbench::Cache cache(cacheRoot, QString(), options);
        QVERIFY2(cache.isValid(), qPrintable(cache.error()));
        QVERIFY(cache.store(3, meta, testImage(QSize(640, 360))));
        directory = cache.directory();
    }

    const QString keptPath =
        QDir(directory).filePath(workbench::frameContentKey(meta)
                                 + QStringLiteral(".qoi"));
    const QString orphanQoi = QDir(directory).filePath(QStringLiteral("99.qoi"));
    const QString orphanWebp = QDir(directory).filePath(QStringLiteral("98.webp"));
    QVERIFY(QFileInfo::exists(keptPath));
    writeTextFile(orphanQoi, QByteArrayLiteral("not in the index"));
    writeTextFile(orphanWebp, QByteArrayLiteral("not in the index"));

    workbench::Cache reopened(cacheRoot, QString(), options);
    QVERIFY2(reopened.isValid(), qPrintable(reopened.error()));
    QCOMPARE(reopened.stats().entries, 1);
    QVERIFY(reopened.cachedFrames().isEmpty());
    QVERIFY(reopened.matches(3, meta));
    bool hit = false;
    QVERIFY(!reopened.lookup(3, meta, &hit).isNull());
    QVERIFY(hit);
    QCOMPARE(reopened.cachedFrames(), QSet<int>{3});
    QVERIFY(QFileInfo::exists(keptPath));
    QVERIFY(!QFileInfo::exists(orphanQoi));
    QVERIFY(!QFileInfo::exists(orphanWebp));
    QVERIFY(reopened.stats().diskBytes > 0);
}

void WorkbenchCacheStoreTests::cacheStoresRasterAsWebp()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    const model::Entity entity =
        assetEntity(QStringLiteral("image"), 0, 100, AssetType::Image);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(10, QString(), options);
    QVERIFY(meta.hasRaster);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(10, meta, testImage(QSize(640, 360))));

    const QString webpPath = contentPath(cache, meta, QStringLiteral("webp"));
    const QString qoiPath = contentPath(cache, meta, QStringLiteral("qoi"));
    QVERIFY(QFileInfo::exists(webpPath));
    QVERIFY(!QFileInfo::exists(qoiPath));

    bool hit = false;
    const QImage image = cache.lookup(10, meta, &hit);
    QVERIFY(hit);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), options.targetSize);
}

void WorkbenchCacheStoreTests::cacheMigratesLegacyIndexWithoutFalseHits()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    const model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta meta = builder.build(3, QString(), options);
    workbench::FrameMeta rasterMeta = meta;
    rasterMeta.hasRaster = true;
    const QString cacheRoot = QDir(root.path()).filePath(QStringLiteral("cache"));

    const QString contentKey = workbench::frameContentKey(meta);
    QString directory;
    {
        workbench::Cache cache(cacheRoot, QString(), options);
        QVERIFY2(cache.isValid(), qPrintable(cache.error()));
        QVERIFY(cache.store(3, rasterMeta, testImage(QSize(640, 360))));
        directory = cache.directory();
    }

    // Replace the shared index with the pre-framecache WebP-only schema.  The
    // legacy table keyed frames by number and the old file lived at 3.webp.
    const QString normalPath = QDir(directory).filePath(
        contentKey + QStringLiteral(".webp"));
    const QString legacyPath = QDir(directory).filePath(QStringLiteral("3.webp"));
    QVERIFY(QFileInfo::exists(normalPath));
    QVERIFY(QFile::copy(normalPath, legacyPath));

    const QString connectionName =
        QStringLiteral("pvf_legacy_%1").arg(quintptr(&root), 0, 16);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(QDir(directory).filePath(QStringLiteral("index.sqlite")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));

        QByteArray metaBytes;
        QByteArray sha1;
        qint64 bytes = 0;
        qint64 lastAccess = 0;
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT payload, tag, bytes, last_access "
                "FROM frame_entries WHERE key = ?"));
            query.addBindValue(contentKey);
            QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
            QVERIFY(query.next());
            metaBytes = query.value(0).toByteArray();
            sha1 = query.value(1).toByteArray();
            bytes = query.value(2).toLongLong();
            lastAccess = query.value(3).toLongLong();
        }
        {
            QSqlQuery query(db);
            QVERIFY2(query.exec(QStringLiteral("DROP TABLE frame_entries")),
                     qPrintable(query.lastError().text()));
            QVERIFY2(query.exec(QStringLiteral(
                         "CREATE TABLE frames ("
                         "frame INTEGER PRIMARY KEY, "
                         "meta BLOB NOT NULL, "
                         "sha1 BLOB NOT NULL, "
                         "bytes INTEGER NOT NULL, "
                         "last_access INTEGER NOT NULL)")),
                     qPrintable(query.lastError().text()));
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO frames(frame, meta, sha1, bytes, last_access) "
                "VALUES(3, ?, ?, ?, ?)"));
            insert.addBindValue(metaBytes);
            insert.addBindValue(sha1);
            insert.addBindValue(bytes);
            insert.addBindValue(lastAccess);
            QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
            QVERIFY2(query.exec(QStringLiteral("PRAGMA user_version=2")),
                     qPrintable(query.lastError().text()));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    // The legacy row is imported (so the file stays accounted for), but the
    // frame-number key can never match a content key: content addressing must
    // not resurrect a stale picture.  Storing the same frame again adds the
    // content-addressed record and then hits.
    workbench::Cache reopened(cacheRoot, QString(), options);
    QVERIFY2(reopened.isValid(), qPrintable(reopened.error()));
    QCOMPARE(reopened.stats().entries, 1);
    QVERIFY(!reopened.matches(3, meta));
    bool hit = true;
    QVERIFY(reopened.lookup(3, meta, &hit).isNull());
    QVERIFY(!hit);

    QVERIFY(reopened.store(3, rasterMeta, testImage(QSize(640, 360))));
    QCOMPARE(reopened.stats().entries, 2);
    QVERIFY(QFileInfo::exists(
        contentPath(reopened, rasterMeta, QStringLiteral("webp"))));
    QVERIFY(!reopened.lookup(3, meta, &hit).isNull());
    QVERIFY(hit);
}

void WorkbenchCacheStoreTests::cacheSwitchesCodecWhenRasterChanges()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    const model::Entity vector = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(vector.uuid, vector);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta vectorMeta = builder.build(5, QString(), options);
    QVERIFY(!vectorMeta.hasRaster);
    workbench::FrameMeta rasterMeta = vectorMeta;
    rasterMeta.hasRaster = true;

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    const QString qoiPath = contentPath(cache, vectorMeta, QStringLiteral("qoi"));
    const QString webpPath = contentPath(cache, vectorMeta, QStringLiteral("webp"));

    QVERIFY(cache.store(5, vectorMeta, testImage(QSize(640, 360))));
    QVERIFY(QFileInfo::exists(qoiPath));
    QVERIFY(!QFileInfo::exists(webpPath));

    QVERIFY(cache.store(5, rasterMeta, testImage(QSize(640, 360))));
    QVERIFY(QFileInfo::exists(webpPath));
    QVERIFY(!QFileInfo::exists(qoiPath));

    QVERIFY(cache.store(5, vectorMeta, testImage(QSize(640, 360))));
    QVERIFY(QFileInfo::exists(qoiPath));
    QVERIFY(!QFileInfo::exists(webpPath));

    bool hit = false;
    QVERIFY(!cache.lookup(5, vectorMeta, &hit).isNull());
    QVERIFY(hit);
    QCOMPARE(cache.stats().entries, 1);
}

void WorkbenchCacheStoreTests::cachePerCameraDirectories()
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

void WorkbenchCacheStoreTests::cacheUsageMatchesStats()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    setWholeValue(entity, QStringLiteral("x"), 0, 0.0);
    setWholeValue(entity, QStringLiteral("x"), 100, 100.0);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const QString cacheRoot = QDir(root.path()).filePath(QStringLiteral("cache"));

    workbench::Cache cache(cacheRoot, QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(1, builder.build(1, QString(), options),
                        testImage(QSize(640, 360))));
    QVERIFY(cache.store(2, builder.build(2, QString(), options),
                        testImage(QSize(640, 360))));

    const workbench::CacheStats stats = cache.stats();
    const framecache::CacheUsage usage =
        framecache::scanDirectory(cache.directory(),
                                  {QStringLiteral("webp"), QStringLiteral("qoi")});

    // The service stats only count indexed frame bytes; the shared scanner
    // must not fold index.sqlite/WAL into that number.
    QCOMPARE(usage.frameCount, stats.entries);
    QCOMPARE(usage.frameCount, 2);
    QCOMPARE(usage.frameBytes, stats.diskBytes);
    QVERIFY(usage.frameBytes > 0);
    QVERIFY(usage.indexBytes > 0);
    QVERIFY(usage.fileCount >= 3); // two frames plus the SQLite index
    QCOMPARE(usage.totalBytes(), usage.frameBytes + usage.indexBytes);

    // A fresh scan of the same tree is identical.
    const framecache::CacheUsage again =
        framecache::scanDirectory(cache.directory(),
                                  {QStringLiteral("webp"), QStringLiteral("qoi")});
    QCOMPARE(again.frameBytes, usage.frameBytes);
    QCOMPARE(again.indexBytes, usage.indexBytes);
    QCOMPARE(again.frameCount, usage.frameCount);
    QCOMPARE(again.fileCount, usage.fileCount);
}

void WorkbenchCacheStoreTests::audioOnlyFramesShareOneRecord()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    project.settings.width = 320;
    project.settings.height = 180;
    project.settings.fps = 30;
    model::Entity audio =
        assetEntity(QStringLiteral("audio"), 0, 50, AssetType::Audio);
    audio.content.asset.sourceIn = 123;
    audio.content.asset.volume = 0.5;
    project.entities.insert(audio.uuid, audio);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(0, QString(), options);
    QVERIFY(!first.hasRaster);

    for (int frame = 1; frame <= 50; ++frame)
    {
        const workbench::FrameMeta meta = builder.build(frame, QString(), options);
        QVERIFY2(first.equals(meta),
                 qPrintable(QStringLiteral("audio-only frame %1 differs")
                                .arg(frame)));
        QCOMPARE(meta.sha1, first.sha1);
    }

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(0, first, testImage(QSize(640, 360))));
    QCOMPARE(cache.stats().entries, 1);

    for (int frame = 1; frame <= 50; ++frame)
    {
        bool hit = false;
        const QImage image = cache.lookup(frame, first, &hit);
        QVERIFY2(hit, qPrintable(QStringLiteral("audio-only frame %1 missed")
                                    .arg(frame)));
        QVERIFY(!image.isNull());
    }

    QCOMPARE(cache.stats().entries, 1);
    QCOMPARE(cache.stats().misses, quint64(0));
    QCOMPARE(frameFileCount(cache.directory()), 1);
    QVERIFY(cache.cachedFrames().contains(50));
}

void WorkbenchCacheStoreTests::audioAncestorKeepsTransformWithoutPlaybackState()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity audio =
        assetEntity(QStringLiteral("audio"), 0, 50, AssetType::Audio);
    audio.content.asset.sourceIn = 7;
    audio.content.asset.volume = 0.25;
    audio.transform.x = 40.0;
    project.entities.insert(audio.uuid, audio);

    // A visible child that does not inherit visibility still needs the audio
    // parent's transform, so the ancestor must stay in the meta.
    model::Entity child = textEntity(QStringLiteral("child"), 0, 50);
    child.parentUuid = audio.uuid;
    child.inheritVisibility = false;
    child.transform.x = 10.0;
    project.entities.insert(child.uuid, child);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(0, QString(), options);
    const workbench::FrameMeta second = builder.build(1, QString(), options);
    QVERIFY(first.equals(second));

    model::Project moved = project;
    moved.entities[QStringLiteral("audio")].transform.x = 80.0;
    const workbench::MetaBuilder movedBuilder(moved, assets);
    QVERIFY(!first.equals(movedBuilder.build(0, QString(), options)));
}

void WorkbenchCacheStoreTests::staticEntityReusesFrameZero()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    project.settings.width = 320;
    project.settings.height = 180;
    model::Entity vector = textEntity(QStringLiteral("vector"), 0, 60);
    vector.content.kind = ContentKind::Vector;
    vector.content.vector.width = 120.0;
    vector.content.vector.height = 80.0;
    project.entities.insert(vector.uuid, vector);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(0, QString(), options);
    const workbench::FrameMeta atThirty = builder.build(30, QString(), options);
    QVERIFY(first.equals(atThirty));

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(0, first, testImage(QSize(640, 360))));

    bool hit = false;
    QVERIFY(!cache.lookup(30, atThirty, &hit).isNull());
    QVERIFY(hit);
    QCOMPARE(cache.stats().entries, 1);
    QCOMPARE(frameFileCount(cache.directory()), 1);

    // Moving the entity at frame 30 changes the picture and must miss.
    model::Project moved = project;
    setWholeValue(moved.entities[QStringLiteral("vector")], QStringLiteral("x"),
                  0, 0.0);
    setWholeValue(moved.entities[QStringLiteral("vector")], QStringLiteral("x"),
                  60, 120.0);
    const workbench::MetaBuilder movedBuilder(moved, assets);
    const workbench::FrameMeta changed = movedBuilder.build(30, QString(), options);
    QVERIFY(!first.equals(changed));

    hit = true;
    QVERIFY(cache.lookup(30, changed, &hit).isNull());
    QVERIFY(!hit);
    QCOMPARE(cache.stats().entries, 1); // the old picture is kept for undo
    QCOMPARE(frameFileCount(cache.directory()), 1);
}

void WorkbenchCacheStoreTests::dynamicMediaDoesNotReuseFrameZero()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    const workbench::Options options = metaOptions(model::Project{});

    model::Project videoProject;
    videoProject.settings.fps = 30;
    model::Entity video =
        assetEntity(QStringLiteral("video"), 0, 60, AssetType::Video);
    video.content.asset.sourceIn = 100;
    videoProject.entities.insert(video.uuid, video);
    const workbench::MetaBuilder videoBuilder(videoProject, assets);
    const workbench::FrameMeta videoZero =
        videoBuilder.build(0, QString(), options);
    const workbench::FrameMeta videoOne =
        videoBuilder.build(1, QString(), options);
    QVERIFY(!videoZero.equals(videoOne));

    // timeDependent defaults to true for a missing manifest: the entity-
    // relative HTML frame is part of the picture key.
    model::Project htmlProject;
    htmlProject.settings.fps = 30;
    model::Entity html =
        assetEntity(QStringLiteral("html"), 0, 60, AssetType::Html);
    htmlProject.entities.insert(html.uuid, html);
    const workbench::MetaBuilder htmlBuilder(htmlProject, assets);
    const workbench::FrameMeta htmlZero =
        htmlBuilder.build(0, QString(), options);
    const workbench::FrameMeta htmlOne =
        htmlBuilder.build(1, QString(), options);
    QVERIFY(!htmlZero.equals(htmlOne));

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(0, videoZero, testImage(QSize(640, 360))));
    QVERIFY(cache.store(0, htmlZero, testImage(QSize(640, 360))));

    bool hit = true;
    QVERIFY(cache.lookup(1, videoOne, &hit).isNull());
    QVERIFY(!hit);
    hit = true;
    QVERIFY(cache.lookup(1, htmlOne, &hit).isNull());
    QVERIFY(!hit);
    QCOMPARE(cache.stats().entries, 2);
}

void WorkbenchCacheStoreTests::editAndUndoInvalidateThenHit()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity entity = textEntity(QStringLiteral("e1"), 0, 100);
    project.entities.insert(entity.uuid, entity);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta original = builder.build(10, QString(), options);

    workbench::Cache cache(QDir(root.path()).filePath(QStringLiteral("cache")),
                           QString(), options);
    QVERIFY2(cache.isValid(), qPrintable(cache.error()));
    QVERIFY(cache.store(10, original, testImage(QSize(640, 360))));
    const QString originalPath =
        contentPath(cache, original, QStringLiteral("qoi"));
    QVERIFY(QFileInfo::exists(originalPath));

    model::Project edited = project;
    edited.entities[QStringLiteral("e1")].content.text.text =
        QStringLiteral("changed");
    const workbench::MetaBuilder editedBuilder(edited, assets);
    const workbench::FrameMeta changed = editedBuilder.build(10, QString(), options);
    QVERIFY(!original.equals(changed));

    bool hit = true;
    QVERIFY(cache.lookup(10, changed, &hit).isNull());
    QVERIFY(!hit);
    QVERIFY(QFileInfo::exists(originalPath)); // kept for undo
    QCOMPARE(cache.stats().entries, 1);

    // Undo: the original meta matches the untouched record without a store.
    hit = false;
    QVERIFY(!cache.lookup(10, original, &hit).isNull());
    QVERIFY(hit);
    QCOMPARE(cache.stats().entries, 1);

    // Redo and actually cache it: both pictures stay addressable.
    QVERIFY(cache.store(10, changed, testImage(QSize(640, 360))));
    QCOMPARE(cache.stats().entries, 2);
    QVERIFY(QFileInfo::exists(originalPath));
    QVERIFY(QFileInfo::exists(contentPath(cache, changed, QStringLiteral("qoi"))));
    hit = false;
    QVERIFY(!cache.lookup(10, original, &hit).isNull());
    QVERIFY(hit);
    hit = false;
    QVERIFY(!cache.lookup(10, changed, &hit).isNull());
    QVERIFY(hit);
}

void WorkbenchCacheStoreTests::emptyAndNonVisualFramesShareOneMeta()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    assets::AssetStore assets(root.path());

    model::Project project;
    model::Entity camera;
    camera.uuid = QStringLiteral("camera");
    camera.start = 0;
    camera.end = 100;
    camera.content.kind = ContentKind::Camera;
    project.entities.insert(camera.uuid, camera);

    model::Entity group;
    group.uuid = QStringLiteral("group");
    group.start = 0;
    group.end = 100;
    group.content.kind = ContentKind::Empty;
    project.entities.insert(group.uuid, group);

    const workbench::Options options = metaOptions(project);
    const workbench::MetaBuilder builder(project, assets);
    const workbench::FrameMeta first = builder.build(0, QString(), options);
    for (const int frame : {1, 2, 25, 100, 400})
        QVERIFY(first.equals(builder.build(frame, QString(), options)));
    QVERIFY(!first.hasRaster);
}

QTEST_MAIN(WorkbenchCacheStoreTests)
#include "WorkbenchCacheStoreTests.moc"
