#include "framecache/CacheUsage.h"
#include "framecache/ImageLru.h"
#include "framecache/LegacyImport.h"
#include "framecache/Store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace pvf;
using namespace pvf::framecache;

namespace {

QByteArray payload(const char* text)
{
    return QByteArray(text);
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(data), qint64(data.size()));
}

void openLegacyDatabase(QTemporaryDir& directory)
{
    const QString name =
        QStringLiteral("pvf_framecache_legacy_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QDir(directory.path()).filePath(QStringLiteral("index.sqlite")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);

        // Workbench cache format v1: no codec column.
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE frames ("
            "frame INTEGER PRIMARY KEY, meta BLOB NOT NULL, "
            "sha1 BLOB NOT NULL, bytes INTEGER NOT NULL, "
            "last_access INTEGER NOT NULL)")));

        // htmlcache format: key/path/size/last_access/asset_hash.
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE entries ("
            "key TEXT PRIMARY KEY, path TEXT NOT NULL, size INTEGER NOT NULL, "
            "last_access INTEGER NOT NULL, asset_hash TEXT NOT NULL)")));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

void insertLegacyWorkbenchRow(const QTemporaryDir& directory, int frame,
                              const QByteArray& meta, const QByteArray& sha1,
                              qint64 bytes, qint64 lastAccess)
{
    const QString name =
        QStringLiteral("pvf_framecache_legacy_wb_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QDir(directory.path()).filePath(QStringLiteral("index.sqlite")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO frames(frame, meta, sha1, bytes, last_access) "
            "VALUES(?, ?, ?, ?, ?)"));
        query.addBindValue(frame);
        query.addBindValue(meta);
        query.addBindValue(sha1);
        query.addBindValue(bytes);
        query.addBindValue(lastAccess);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

void insertLegacyHtmlRow(const QTemporaryDir& directory, const QString& key,
                         const QString& path, qint64 size, qint64 lastAccess,
                         const QString& assetHash)
{
    const QString name =
        QStringLiteral("pvf_framecache_legacy_html_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QDir(directory.path()).filePath(QStringLiteral("index.sqlite")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO entries(key, path, size, last_access, asset_hash) "
            "VALUES(?, ?, ?, ?, ?)"));
        query.addBindValue(key);
        query.addBindValue(path);
        query.addBindValue(size);
        query.addBindValue(lastAccess);
        query.addBindValue(assetHash);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

QStringList databaseTables(const QString& directory)
{
    QStringList tables;
    const QString name =
        QStringLiteral("pvf_framecache_inspect_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QDir(directory).filePath(QStringLiteral("index.sqlite")));
        if (!db.open())
            return tables;
        QSqlQuery query(db);
        if (query.exec(QStringLiteral(
                "SELECT name FROM sqlite_master WHERE type = 'table' "
                "ORDER BY name")))
        {
            while (query.next())
                tables.append(query.value(0).toString());
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return tables;
}

int databaseUserVersion(const QString& directory)
{
    int version = -1;
    const QString name =
        QStringLiteral("pvf_framecache_version_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QDir(directory).filePath(QStringLiteral("index.sqlite")));
        if (!db.open())
            return version;
        QSqlQuery query(db);
        if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
            version = query.value(0).toInt();
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return version;
}

Options optionsFor(const QString& directory, qint64 budget)
{
    Options options;
    options.directory = directory;
    options.budgetBytes = budget;
    options.extensions = {QStringLiteral("webp"), QStringLiteral("qoi")};
    return options;
}

} // namespace

class FrameCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void schemaIsShared();
    void schemaMigratesWorkbenchLegacy();
    void schemaMigratesHtmlcacheLegacy();
    void putGetSurvivesRestart();
    void lruEvictsOldest();
    void orphanFilesSwept();
    void statsCountersShared();
    void updateRepairsSize();
    void rejectsUnsafeKeys();
    void memoryLruOrderAndBudget();
    void memoryLruTagClear();
    void usageSeparatesFramesFromIndex();
    void clearReportsFreedBytes();
    void clearReportsUndeletableFiles();
    void budgetShrinkEvictsOldestFirst();
    void budgetShrinkScansOnce();
};

void FrameCacheTest::schemaIsShared()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    {
        Store store(optionsFor(directory.path(), 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));
    }

    const QStringList tables = databaseTables(directory.path());
    QVERIFY2(tables.contains(QStringLiteral("frame_entries")),
             qPrintable(tables.join(QLatin1Char(','))));
    // The shared layer never creates the two historical table names.
    QVERIFY(!tables.contains(QStringLiteral("frames")));
    QVERIFY(!tables.contains(QStringLiteral("entries")));
    QCOMPARE(databaseUserVersion(directory.path()), 3);
}

void FrameCacheTest::schemaMigratesWorkbenchLegacy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    openLegacyDatabase(directory);

    const QByteArray meta = payload("meta-v1");
    const QByteArray sha1(20, 'a');
    const QByteArray data = payload("qoi-ish-bytes");
    writeFile(QDir(directory.path()).filePath(QStringLiteral("7.webp")),
              data);
    insertLegacyWorkbenchRow(directory, 7, meta, sha1, data.size(), 1234);

    {
        // Store no longer knows historical layouts; the caller imports first.
        const LegacyImportResult imported = importLegacyIndex(
            directory.path(), {QStringLiteral("frames")});
        QVERIFY2(imported.ok(), qPrintable(imported.error));
        QCOMPARE(imported.records, 1);

        Store store(optionsFor(directory.path(), 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));

        const auto record = store.record(QStringLiteral("7"));
        QVERIFY(record.has_value());
        QCOMPARE(record->codec, QStringLiteral("webp"));
        QCOMPARE(record->file, QStringLiteral("7.webp"));
        QCOMPARE(record->payload, meta);
        QCOMPARE(record->tag, sha1);
        QCOMPARE(record->bytes, qint64(data.size()));
    }

    // The legacy table is kept as a read-only backup, and the import marker
    // prevents a second import.
    QVERIFY(databaseTables(directory.path()).contains(QStringLiteral("frames")));
    QCOMPARE(databaseUserVersion(directory.path()), 3);

    const LegacyImportResult again =
        importLegacyIndex(directory.path(), {QStringLiteral("frames")});
    QVERIFY2(again.ok(), qPrintable(again.error));
    QCOMPARE(again.records, 0);
}

void FrameCacheTest::schemaMigratesHtmlcacheLegacy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    openLegacyDatabase(directory);

    const QString key = QStringLiteral("html1-a-f3-64x36-c120-eentry-abcdef");
    const QByteArray data = payload("webp-ish-bytes");
    writeFile(QDir(directory.path()).filePath(key + QStringLiteral(".webp")),
              data);
    insertLegacyHtmlRow(directory, key,
                        QStringLiteral("/old/absolute/path/") + key
                            + QStringLiteral(".webp"),
                        data.size(), 99, QStringLiteral("asset-a"));

    {
        const LegacyImportResult imported = importLegacyIndex(
            directory.path(), {QStringLiteral("entries")});
        QVERIFY2(imported.ok(), qPrintable(imported.error));

        Store store(optionsFor(directory.path(), 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));

        const auto record = store.record(key);
        QVERIFY(record.has_value());
        // The stale absolute path is repaired to <key>.webp.
        QCOMPARE(record->file, key + QStringLiteral(".webp"));
        QCOMPARE(record->payload, QByteArrayLiteral("asset-a"));
        QCOMPARE(record->codec, QStringLiteral("webp"));
        QVERIFY(QFileInfo::exists(store.pathFor(*record)));
    }
    QVERIFY(databaseTables(directory.path()).contains(QStringLiteral("entries")));
}

void FrameCacheTest::putGetSurvivesRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Record stored;
    qint64 access = 0;
    {
        Store store(optionsFor(directory.path(), 64 * 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));

        Record record;
        record.key = QStringLiteral("42");
        record.codec = QStringLiteral("qoi");
        record.payload = payload("frame-meta");
        record.tag = QByteArray(20, 'b');
        QVERIFY(store.put(record, payload("encoded-frame")));
        access = store.record(record.key)->lastAccess;
        stored = *store.record(record.key);
    }

    {
        Store store(optionsFor(directory.path(), 64 * 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));
        const auto record = store.record(QStringLiteral("42"));
        QVERIFY(record.has_value());
        QCOMPARE(record->payload, stored.payload);
        QCOMPARE(record->tag, stored.tag);
        QCOMPARE(record->codec, QStringLiteral("qoi"));
        QCOMPARE(record->lastAccess, access);
        QCOMPARE(store.stats().entries, 1);
    }
}

void FrameCacheTest::lruEvictsOldest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));

    const QByteArray data(4000, 'x');
    for (const QString& key : {QStringLiteral("1"), QStringLiteral("2"),
                               QStringLiteral("3")})
    {
        Record record;
        record.key = key;
        QVERIFY2(store.put(record, data), qPrintable(store.error()));
    }
    QCOMPARE(store.stats().entries, 3);

    // Refresh 1, so 2 is the oldest when the budget only holds two entries.
    QVERIFY(store.touch(QStringLiteral("1")));

    store.setBudgetBytes(2 * data.size());
    QCOMPARE(store.stats().entries, 2);
    QVERIFY(!store.record(QStringLiteral("2")).has_value());
    QVERIFY(store.record(QStringLiteral("1")).has_value());
    QVERIFY(store.record(QStringLiteral("3")).has_value());
    QCOMPARE(store.stats().evictions, quint64(1));
    QVERIFY(!QFileInfo::exists(
        QDir(directory.path()).filePath(QStringLiteral("2.webp"))));
    QCOMPARE(store.stats().diskBytes, 2 * qint64(data.size()));
}

void FrameCacheTest::orphanFilesSwept()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    {
        Store store(optionsFor(directory.path(), 1024 * 1024));
        Record record;
        record.key = QStringLiteral("kept");
        QVERIFY(store.put(record, payload("kept-frame")));
    }

    const QString orphanWebp =
        QDir(directory.path()).filePath(QStringLiteral("orphan.webp"));
    const QString orphanQoi =
        QDir(directory.path()).filePath(QStringLiteral("orphan.qoi"));
    const QString unrelated =
        QDir(directory.path()).filePath(QStringLiteral("notes.txt"));
    writeFile(orphanWebp, payload("orphan"));
    writeFile(orphanQoi, payload("orphan"));
    writeFile(unrelated, payload("keep me"));

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));
    QCOMPARE(store.stats().entries, 1);
    QVERIFY(!QFileInfo::exists(orphanWebp));
    QVERIFY(!QFileInfo::exists(orphanQoi));
    QVERIFY(QFileInfo::exists(unrelated));
}

void FrameCacheTest::statsCountersShared()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Store store(optionsFor(directory.path(), 5000));
    QVERIFY2(store.isValid(), qPrintable(store.error()));
    QCOMPARE(store.stats().budgetBytes, 5000);

    Record record;
    record.key = QStringLiteral("stat");
    QVERIFY2(store.put(record, QByteArray(4000, 's')), qPrintable(store.error()));
    store.noteHit();
    store.noteMiss();
    store.noteMiss();

    const Stats stats = store.stats();
    QCOMPARE(stats.entries, 1);
    QCOMPARE(stats.stores, quint64(1));
    QCOMPARE(stats.hits, quint64(1));
    QCOMPARE(stats.misses, quint64(2));
    QCOMPARE(stats.diskBytes, qint64(4000));
    QCOMPARE(stats.budgetBytes, qint64(5000));

    // The same counters govern both facades' eviction decisions.
    store.setBudgetBytes(0);
    QCOMPARE(store.stats().entries, 0);
    QCOMPARE(store.stats().evictions, quint64(1));
    QCOMPARE(store.stats().diskBytes, qint64(0));
}

void FrameCacheTest::updateRepairsSize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QString key = QStringLiteral("size");
    {
        Store store(optionsFor(directory.path(), 1024 * 1024));
        Record record;
        record.key = key;
        QVERIFY2(store.put(record, QByteArray(3000, 'z')), qPrintable(store.error()));

        Record repaired = *store.record(key);
        repaired.bytes = 1; // a reader observed a stale index value
        QVERIFY(store.update(repaired));
        QCOMPARE(store.stats().diskBytes, qint64(1));

        // Reopening trusts the file again.
    }

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));
    QCOMPARE(store.record(key)->bytes, qint64(3000));
}

void FrameCacheTest::rejectsUnsafeKeys()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTemporaryDir outside;

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));

    for (const QString& key : {QStringLiteral("../escape"),
                               QStringLiteral("a/b"),
                               QStringLiteral("a\\b"), QString(),
                               QStringLiteral("..")})
    {
        Record record;
        record.key = key;
        QString error;
        QVERIFY2(!store.put(record, payload("nope"), &error),
                 qPrintable(QStringLiteral("key accepted: ") + key));
        QVERIFY(!error.isEmpty());
    }

    QVERIFY(!QFileInfo::exists(
        QDir(directory.path()).filePath(QStringLiteral("escape.webp"))));
    QVERIFY(!QFileInfo::exists(
        QDir(directory.path()).filePath(QStringLiteral("escape.qoi"))));
}

void FrameCacheTest::memoryLruOrderAndBudget()
{
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const qint64 cost = image.sizeInBytes();

    ImageLru lru(cost * 2);
    QVERIFY(lru.put(QStringLiteral("a"), image, QStringLiteral("asset-a")));
    QVERIFY(lru.put(QStringLiteral("b"), image, QStringLiteral("asset-b")));
    QCOMPARE(lru.entryCount(), 2);

    // Refresh a so b becomes the victim.
    QVERIFY(lru.get(QStringLiteral("a")).has_value());
    QVERIFY(lru.put(QStringLiteral("c"), image, QStringLiteral("asset-c")));

    QCOMPARE(lru.entryCount(), 2);
    QCOMPARE(lru.bytes(), cost * 2);
    QVERIFY(!lru.contains(QStringLiteral("b")));
    QVERIFY(lru.contains(QStringLiteral("a")));
    QVERIFY(lru.contains(QStringLiteral("c")));
    QCOMPARE(lru.evictions(), quint64(1));

    lru.setBudgetBytes(0);
    QCOMPARE(lru.entryCount(), 0);
    QCOMPARE(lru.bytes(), qint64(0));
}

void FrameCacheTest::memoryLruTagClear()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);

    ImageLru lru(1024 * 1024);
    QVERIFY(lru.put(QStringLiteral("a0"), image, QStringLiteral("asset-a")));
    QVERIFY(lru.put(QStringLiteral("a1"), image, QStringLiteral("asset-a")));
    QVERIFY(lru.put(QStringLiteral("b0"), image, QStringLiteral("asset-b")));

    QCOMPARE(lru.removeTag(QStringLiteral("asset-a")), 2);
    QCOMPARE(lru.entryCount(), 1);
    QVERIFY(!lru.contains(QStringLiteral("a0")));
    QVERIFY(lru.contains(QStringLiteral("b0")));
    QCOMPARE(lru.bytes(), qint64(image.sizeInBytes()));
}

void FrameCacheTest::usageSeparatesFramesFromIndex()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    writeFile(QDir(directory.path()).filePath(QStringLiteral("1.qoi")),
              QByteArray(100, 'a'));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("2.WEBP")),
              QByteArray(200, 'b'));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("index.sqlite")),
              QByteArray(300, 'c'));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("index.sqlite-wal")),
              QByteArray(50, 'd'));

    const CacheUsage usage = scanDirectory(directory.path());
    QCOMPARE(usage.frameBytes, qint64(300));
    QCOMPARE(usage.indexBytes, qint64(350));
    QCOMPARE(usage.frameCount, 2);
    QCOMPARE(usage.fileCount, 4);
    QCOMPARE(usage.totalBytes(), qint64(650));

    // The measurement is a pure scan: repeating it must give the same buckets.
    const CacheUsage again = scanDirectory(directory.path());
    QCOMPARE(again.frameBytes, usage.frameBytes);
    QCOMPARE(again.indexBytes, usage.indexBytes);
    QCOMPARE(again.frameCount, usage.frameCount);
    QCOMPARE(again.fileCount, usage.fileCount);
}

void FrameCacheTest::clearReportsFreedBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    writeFile(QDir(directory.path()).filePath(QStringLiteral("7.qoi")),
              QByteArray(120, 'a'));
    const QString nested =
        QDir(directory.path()).filePath(QStringLiteral("wb/cam"));
    QVERIFY(QDir().mkpath(nested));
    writeFile(QDir(nested).filePath(QStringLiteral("8.webp")),
              QByteArray(80, 'b'));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("index.sqlite")),
              QByteArray(40, 'c'));

    const CacheUsage before = scanDirectory(directory.path());
    const ClearResult result = clearDirectory(directory.path());

    QVERIFY(result.ok);
    QVERIFY(result.failedPaths.isEmpty());
    QCOMPARE(result.before.frameBytes, before.frameBytes);
    QCOMPARE(result.before.indexBytes, before.indexBytes);
    QCOMPARE(result.after.totalBytes(), qint64(0));
    QCOMPARE(result.freedBytes, before.totalBytes());
    QCOMPARE(result.removedFrames, before.frameCount);
    QCOMPARE(result.removedFiles, before.fileCount);
    QVERIFY(!QDir(directory.path()).exists());
}

void FrameCacheTest::clearReportsUndeletableFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString locked =
        QDir(directory.path()).filePath(QStringLiteral("locked"));
    QVERIFY(QDir().mkpath(locked));
    writeFile(QDir(locked).filePath(QStringLiteral("5.webp")),
              QByteArray(64, 'x'));

    // A read-only directory cannot lose its files.  The guard restores write
    // access before any assertion so a failure cannot leak into teardown.
    const QFile::Permissions original = QFile::permissions(locked);
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner
                                             | QFileDevice::ExeOwner));
    const ClearResult result = clearDirectory(directory.path());
    QVERIFY(QFile::setPermissions(locked, original));

    if (result.ok)
        QSKIP("the test process may delete files in a read-only directory");
    QCOMPARE(result.after.fileCount, 1);
    QCOMPARE(result.after.frameCount, 1);
    QCOMPARE(result.freedBytes, qint64(0));
    QCOMPARE(result.removedFrames, 0);
    QCOMPARE(result.failedPaths.size(), 1);
    QVERIFY(!result.error.isEmpty());
}

void FrameCacheTest::budgetShrinkEvictsOldestFirst()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QByteArray data(100, 'x');
    {
        Store store(optionsFor(directory.path(), 1024 * 1024));
        QVERIFY2(store.isValid(), qPrintable(store.error()));
        for (const QString& key : {QStringLiteral("a"), QStringLiteral("b"),
                                   QStringLiteral("c"), QStringLiteral("d")})
        {
            Record record;
            record.key = key;
            QVERIFY2(store.put(record, data), qPrintable(store.error()));
        }
    }

    // Flatten the LRU clock: b/c/d share the oldest stamp, so the key is the
    // only tie-break and the sweep must still be deterministic.
    const QString connection =
        QStringLiteral("pvf_framecache_flatten_%1")
            .arg(quintptr(&directory), 0, 16);
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(
            QDir(directory.path()).filePath(QStringLiteral("index.sqlite")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY2(query.exec(QStringLiteral(
                     "UPDATE frame_entries SET last_access = 100 "
                     "WHERE key <> 'a'")),
                 qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                     "UPDATE frame_entries SET last_access = 200 "
                     "WHERE key = 'a'")),
                 qPrintable(query.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));
    QCOMPARE(store.stats().entries, 4);
    QCOMPARE(store.record(QStringLiteral("a"))->lastAccess, qint64(200));

    // Keeping a single row evicts the tied trio in key order and spares "a".
    store.setBudgetBytes(100);
    QCOMPARE(store.stats().entries, 1);
    QCOMPARE(store.stats().evictions, quint64(3));
    QCOMPARE(store.stats().diskBytes, qint64(100));
    QVERIFY(store.record(QStringLiteral("a")).has_value());
    for (const QString& key :
         {QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")})
        QVERIFY(!store.record(key).has_value());

    // One ordered pass, not one full scan per victim.
    QVERIFY(store.stats().evictionRowsVisited <= quint64(4));
}

void FrameCacheTest::budgetShrinkScansOnce()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    constexpr int kCount = 300;
    constexpr int kKeep = 20;
    const QByteArray data(16, 'x');

    Store store(optionsFor(directory.path(), 1024 * 1024));
    QVERIFY2(store.isValid(), qPrintable(store.error()));
    for (int i = 0; i < kCount; ++i)
    {
        Record record;
        record.key = QString::number(i);
        QVERIFY2(store.put(record, data), qPrintable(store.error()));
    }
    QCOMPARE(store.stats().entries, kCount);

    store.setBudgetBytes(data.size() * kKeep);
    QCOMPARE(store.stats().entries, kKeep);
    QCOMPARE(store.stats().diskBytes, qint64(data.size()) * kKeep);
    QCOMPARE(store.stats().evictions, quint64(kCount - kKeep));

    // The survivors are the newest frames, in insertion (= LRU) order.
    QVERIFY(!store.record(QStringLiteral("0")).has_value());
    for (int i = kCount - kKeep; i < kCount; ++i)
        QVERIFY2(store.record(QString::number(i)).has_value(),
                 qPrintable(QString::number(i)));

    // A sweep visits each victim once.  The retired O(n²) loop visited
    // entries * victims (here 300 * 280 = 84000).
    QVERIFY(store.stats().evictionRowsVisited
            <= quint64(kCount + kKeep));

    // Evicted rows left the index too, not just the in-memory map.
    {
        Store reopened(optionsFor(directory.path(), 1024 * 1024));
        QVERIFY2(reopened.isValid(), qPrintable(reopened.error()));
        QCOMPARE(reopened.stats().entries, kKeep);
        QVERIFY(!reopened.record(QStringLiteral("0")).has_value());
        QVERIFY(!QFileInfo::exists(
            QDir(directory.path()).filePath(QStringLiteral("0.webp"))));
        QCOMPARE(reopened.record(QString::number(kCount - 1))->bytes,
                 qint64(data.size()));
    }
}

QTEST_MAIN(FrameCacheTest)

#include "framecache_tests.moc"
