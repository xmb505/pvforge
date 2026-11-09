#include "htmlcache/HtmlCache.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace pvf;
using namespace pvf::htmlcache;

namespace {

QImage makeImage(int width = 64, int height = 48)
{
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            image.setPixel(x, y, qRgb((x * 7 + 13) % 256, (y * 11 + 29) % 256,
                                      ((x + y) * 17) % 256));
        }
    }
    return image;
}

FrameKey makeKey(const QString& assetHash, int localFrame = 0)
{
    FrameKey key;
    key.assetHash = assetHash;
    key.localFrame = localFrame;
    key.size = QSize(64, 48);
    key.chromeVersion = QStringLiteral("120.0.6099.109");
    key.entryHash = QStringLiteral("entry-a");
    key.variables.insert(QStringLiteral("speed"), 1.5);
    key.variables.insert(QStringLiteral("title"), QStringLiteral("Hello"));
    return key;
}

QString repeatedHash(QChar character)
{
    return QString(64, character);
}

QString frameFilePath(const QString& directory, const FrameKey& key)
{
    return directory + QLatin1Char('/') + canonicalKey(key) + QStringLiteral(".webp");
}

} // namespace

class HtmlCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void canonicalKeyStable();
    void canonicalKeyDiffers();
    void webpRoundtrip();
    void memoryPutGet();
    void memoryLruEviction();
    void memoryClearAsset();
    void diskPutGet();
    void diskLruEviction();
    void diskClearAsset();
    void diskCorruptFileIsMiss();
    void diskIndexSurvivesRestart();
    void tieredColdHitBackfillsHot();
    void tieredPutAndClear();
};

void HtmlCacheTest::canonicalKeyStable()
{
    FrameKey first;
    first.assetHash = repeatedHash(QLatin1Char('a'));
    first.localFrame = 12;
    first.size = QSize(640, 360);
    first.chromeVersion = QStringLiteral("120.0.6099.109");
    first.entryHash = QStringLiteral("bundle-v1");
    first.variables.insert(QStringLiteral("speed"), 1.5);
    first.variables.insert(QStringLiteral("title"), QStringLiteral("Hello world"));

    FrameKey second;
    second.assetHash = first.assetHash;
    second.localFrame = first.localFrame;
    second.size = first.size;
    second.chromeVersion = first.chromeVersion;
    second.entryHash = first.entryHash;
    second.variables.insert(QStringLiteral("title"), QStringLiteral("Hello world"));
    second.variables.insert(QStringLiteral("speed"), 1.500000);

    const QString key = canonicalKey(first);
    QCOMPARE(key, canonicalKey(second));
    QVERIFY(key.contains(QStringLiteral("1.500000")));
    QVERIFY(!key.contains(QLatin1Char('/')));
    QVERIFY(!key.contains(QLatin1Char('\\')));
    QVERIFY(key.size() <= 220);
    QVERIFY(key.startsWith(QStringLiteral("html1-")));
}

void HtmlCacheTest::canonicalKeyDiffers()
{
    const FrameKey base = makeKey(repeatedHash(QLatin1Char('a')));
    const QString baseKey = canonicalKey(base);

    FrameKey changed = base;
    changed.localFrame = 1;
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.size = QSize(128, 48);
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.chromeVersion = QStringLiteral("121.0.0.0");
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.entryHash = QStringLiteral("bundle-v2");
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.assetHash = repeatedHash(QLatin1Char('b'));
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.variables.insert(QStringLiteral("speed"), 2.5);
    QVERIFY(canonicalKey(changed) != baseKey);

    changed = base;
    changed.variables.insert(QStringLiteral("speed"), 2);
    QVERIFY(canonicalKey(changed) != baseKey);
}

void HtmlCacheTest::webpRoundtrip()
{
    const QImage image = makeImage(96, 64);
    const QByteArray encoded = encodeWebp(image);
    QVERIFY(!encoded.isEmpty());

    const std::optional<QImage> decoded = decodeWebp(encoded);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->size(), image.size());

    const QByteArray reencoded = encodeWebp(*decoded);
    QVERIFY(!reencoded.isEmpty());
    const std::optional<QImage> redecoded = decodeWebp(reencoded);
    QVERIFY(redecoded.has_value());
    QCOMPARE(redecoded->size(), image.size());

    QVERIFY(!decodeWebp(QByteArrayLiteral("not a webp")).has_value());
    QVERIFY(!decodeWebp(QByteArray()).has_value());
}

void HtmlCacheTest::memoryPutGet()
{
    MemoryCache cache;
    QCOMPARE(cache.maxBytes(), 256LL * 1024 * 1024);

    const FrameKey key = makeKey(repeatedHash(QLatin1Char('a')), 5);
    const QImage image = makeImage();
    cache.put(key, image);

    QCOMPARE(cache.entryCount(), 1);
    QCOMPARE(cache.currentBytes(), static_cast<qint64>(image.sizeInBytes()));

    const std::optional<QImage> loaded = cache.get(key);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), image.size());
    // Implicit sharing: the cache returns the same pixel buffer, not a copy.
    QCOMPARE(loaded->constBits(), image.constBits());

    FrameKey miss = key;
    miss.localFrame = 6;
    QVERIFY(!cache.get(miss).has_value());
    QCOMPARE(cache.entryCount(), 1);
}

void HtmlCacheTest::memoryLruEviction()
{
    const QImage image = makeImage();
    const qint64 imageCost = image.sizeInBytes();
    MemoryCache cache(imageCost * 2);

    const FrameKey first = makeKey(repeatedHash(QLatin1Char('a')), 0);
    const FrameKey second = makeKey(repeatedHash(QLatin1Char('a')), 1);
    const FrameKey third = makeKey(repeatedHash(QLatin1Char('a')), 2);

    cache.put(first, image);
    cache.put(second, image);
    QVERIFY(cache.get(first).has_value()); // refresh first, so second is oldest
    cache.put(third, image);

    QCOMPARE(cache.entryCount(), 2);
    QVERIFY(cache.currentBytes() <= cache.maxBytes());
    QVERIFY(!cache.get(second).has_value());
    QVERIFY(cache.get(first).has_value());
    QVERIFY(cache.get(third).has_value());
}

void HtmlCacheTest::memoryClearAsset()
{
    MemoryCache cache;
    const QString assetA = repeatedHash(QLatin1Char('a'));
    const QString assetB = repeatedHash(QLatin1Char('b'));
    const FrameKey a0 = makeKey(assetA, 0);
    const FrameKey a1 = makeKey(assetA, 1);
    const FrameKey b0 = makeKey(assetB, 0);
    const QImage image = makeImage();

    cache.put(a0, image);
    cache.put(a1, image);
    cache.put(b0, image);
    QCOMPARE(cache.entryCount(), 3);

    cache.clearAsset(assetA);
    QCOMPARE(cache.entryCount(), 1);
    QCOMPARE(cache.currentBytes(), static_cast<qint64>(image.sizeInBytes()));
    QVERIFY(!cache.get(a0).has_value());
    QVERIFY(!cache.get(a1).has_value());
    QVERIFY(cache.get(b0).has_value());
}

void HtmlCacheTest::diskPutGet()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DiskCache cache(directory.path());
    QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));

    const FrameKey key = makeKey(repeatedHash(QLatin1Char('a')), 5);
    const QImage image = makeImage();
    cache.put(key, image);

    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(cache.currentBytes() > 0);
    QVERIFY(QFileInfo::exists(frameFilePath(directory.path(), key)));

    const std::optional<QImage> loaded = cache.get(key);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), image.size());

    FrameKey miss = key;
    miss.localFrame = 6;
    QVERIFY(!cache.get(miss).has_value());
    QCOMPARE(cache.entryCount(), 1);
}

void HtmlCacheTest::diskLruEviction()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DiskCache cache(directory.path(), 1024LL * 1024 * 1024);
    QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));

    const FrameKey first = makeKey(repeatedHash(QLatin1Char('a')), 0);
    const FrameKey second = makeKey(repeatedHash(QLatin1Char('a')), 1);
    const FrameKey third = makeKey(repeatedHash(QLatin1Char('a')), 2);
    const QImage image = makeImage();

    cache.put(first, image);
    cache.put(second, image);
    cache.put(third, image);
    QCOMPARE(cache.entryCount(), 3);

    const qint64 firstSize = QFileInfo(frameFilePath(directory.path(), first)).size();
    const qint64 secondSize = QFileInfo(frameFilePath(directory.path(), second)).size();
    const qint64 thirdSize = QFileInfo(frameFilePath(directory.path(), third)).size();
    QVERIFY(firstSize > 0 && secondSize > 0 && thirdSize > 0);

    // Allow exactly the two newest entries; the oldest one must be evicted.
    cache.setMaxBytes(secondSize + thirdSize);
    QCOMPARE(cache.entryCount(), 2);
    QVERIFY(!cache.get(first).has_value());
    QVERIFY(cache.get(second).has_value());
    QVERIFY(QFileInfo::exists(frameFilePath(directory.path(), third)));

    // get(second) refreshes it (get(third) was not called), so third becomes
    // the next eviction victim.
    cache.setMaxBytes(std::max(secondSize, thirdSize));
    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(cache.get(second).has_value());
    QVERIFY(!QFileInfo::exists(frameFilePath(directory.path(), third)));
}

void HtmlCacheTest::diskClearAsset()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DiskCache cache(directory.path());
    QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));

    const QString assetA = repeatedHash(QLatin1Char('a'));
    const QString assetB = repeatedHash(QLatin1Char('b'));
    const FrameKey a0 = makeKey(assetA, 0);
    const FrameKey a1 = makeKey(assetA, 1);
    const FrameKey b0 = makeKey(assetB, 0);
    const QImage image = makeImage();

    cache.put(a0, image);
    cache.put(a1, image);
    cache.put(b0, image);
    QCOMPARE(cache.entryCount(), 3);

    cache.clearAsset(assetA);
    QCOMPARE(cache.entryCount(), 1);
    QVERIFY(!cache.get(a0).has_value());
    QVERIFY(!cache.get(a1).has_value());
    QVERIFY(cache.get(b0).has_value());
    QVERIFY(!QFileInfo::exists(frameFilePath(directory.path(), a0)));
    QVERIFY(!QFileInfo::exists(frameFilePath(directory.path(), a1)));
}

void HtmlCacheTest::diskCorruptFileIsMiss()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DiskCache cache(directory.path());
    QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));

    const FrameKey key = makeKey(repeatedHash(QLatin1Char('a')), 7);
    cache.put(key, makeImage());
    QCOMPARE(cache.entryCount(), 1);

    const QString path = frameFilePath(directory.path(), key);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("definitely not a webp image");
    file.close();

    QVERIFY(!cache.get(key).has_value());
    QCOMPARE(cache.entryCount(), 0);
    QVERIFY(!QFileInfo::exists(path));
}

void HtmlCacheTest::diskIndexSurvivesRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const FrameKey first = makeKey(repeatedHash(QLatin1Char('a')), 0);
    const FrameKey second = makeKey(repeatedHash(QLatin1Char('a')), 1);

    {
        DiskCache cache(directory.path());
        QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));
        cache.put(first, makeImage());
        cache.put(second, makeImage());
        QCOMPARE(cache.entryCount(), 2);
    }

    {
        DiskCache cache(directory.path());
        QVERIFY2(cache.isReady(), qPrintable(cache.lastError()));
        QCOMPARE(cache.entryCount(), 2);
        QVERIFY(cache.get(first).has_value());
        QVERIFY(cache.get(second).has_value());
    }

    // Delete one file behind the index: startup must drop that row.
    QVERIFY(QFile::remove(frameFilePath(directory.path(), second)));
    {
        DiskCache cache(directory.path());
        QCOMPARE(cache.entryCount(), 1);
        QVERIFY(cache.get(first).has_value());
        QVERIFY(!cache.get(second).has_value());
    }

    // Corrupt the remaining file: startup must discard it as well.
    QFile corrupt(frameFilePath(directory.path(), first));
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("broken");
    corrupt.close();
    {
        DiskCache cache(directory.path());
        QCOMPARE(cache.entryCount(), 0);
    }
}

void HtmlCacheTest::tieredColdHitBackfillsHot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    MemoryCache hot;
    DiskCache cold(directory.path());
    QVERIFY2(cold.isReady(), qPrintable(cold.lastError()));
    TieredCache cache(&hot, &cold);

    const FrameKey key = makeKey(repeatedHash(QLatin1Char('a')), 3);
    const QImage image = makeImage();
    cold.put(key, image);

    QCOMPARE(hot.entryCount(), 0);
    const std::optional<QImage> loaded = cache.get(key);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), image.size());
    QCOMPARE(hot.entryCount(), 1);

    // Removing the cold copy proves the second read is served by hot.
    cold.clearAsset(key.assetHash);
    QCOMPARE(cold.entryCount(), 0);
    const std::optional<QImage> second = cache.get(key);
    QVERIFY(second.has_value());
    QCOMPARE(second->size(), image.size());
}

void HtmlCacheTest::tieredPutAndClear()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    MemoryCache hot;
    DiskCache cold(directory.path());
    QVERIFY2(cold.isReady(), qPrintable(cold.lastError()));
    TieredCache cache(&hot, &cold);

    const QString asset = repeatedHash(QLatin1Char('c'));
    const FrameKey key = makeKey(asset, 0);
    cache.put(key, makeImage());
    QCOMPARE(hot.entryCount(), 1);
    QCOMPARE(cold.entryCount(), 1);

    cache.clearAsset(asset);
    QCOMPARE(hot.entryCount(), 0);
    QCOMPARE(cold.entryCount(), 0);
}

QTEST_MAIN(HtmlCacheTest)

#include "htmlcache_tests.moc"
