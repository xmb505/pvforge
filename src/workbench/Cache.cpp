#include "workbench/Cache.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>

namespace pvf::workbench {
namespace {

constexpr int kWebpQuality = 80;

QString sha1Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

// Options-only directory key.  Project fps/size/background are part of
// FrameMeta, so a project change still invalidates every frame even when the
// directories are shared.  renderSignature() remains the project-wide key
// exposed to callers that want to nest the cache root per project.
QString directorySignature(const Options& options)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(1) << options.rendererVersion << qint32(options.scalePercent)
        << qint32(options.expandPercent)
        << quint8(options.transparentOutsideCanvas ? 1 : 0)
        << options.targetSize << options.chromeVersion;
    return QStringLiteral("wb1-") + sha1Hex(payload);
}

QString safeComponent(QString value)
{
    if (value.isEmpty())
        return QStringLiteral("nocamera");
    for (QChar& character : value)
    {
        if (!character.isLetterOrNumber() && character != QLatin1Char('.')
            && character != QLatin1Char('_') && character != QLatin1Char('-'))
            character = QLatin1Char('_');
    }
    if (value == QLatin1String(".") || value == QLatin1String(".."))
        value.prepend(QLatin1Char('_'));
    return value;
}

QByteArray encodeWebp(const QImage& image)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};

    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(kWebpQuality);
    if (!writer.write(image))
        return {};
    buffer.close();
    return data;
}

} // namespace

struct Cache::Impl
{
    struct Entry
    {
        QByteArray meta;
        QByteArray sha1;
        qint64 bytes = 0;
        qint64 lastAccess = 0;
    };

    QString root;
    QString cameraKey;
    QString directory;
    Options options;
    QString connectionName;
    QString error;
    bool valid = false;

    QHash<int, Entry> entries;
    QHash<int, QImage> images; // decoded memory LRU
    QHash<int, quint64> imageAccess;
    qint64 memoryBytes = 0;
    qint64 diskBytes = 0;
    qint64 clock = 0;

    CacheStats counters;

    QString framePath(int frame) const
    {
        return QDir(directory).filePath(QString::number(frame) + QStringLiteral(".webp"));
    }

    qint64 nextAccess()
    {
        // Monotonic even for several operations in the same millisecond, so
        // the LRU order stays deterministic.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        clock = std::max(now, clock + 1);
        return clock;
    }

    QSqlDatabase database() const
    {
        return QSqlDatabase::database(connectionName, false);
    }

    bool openIndex(const void* owner)
    {
        connectionName =
            QStringLiteral("pvf_workbench_%1")
                .arg(reinterpret_cast<quintptr>(owner), 0, 16);

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    connectionName);
        if (!db.isValid())
        {
            error = QStringLiteral("Qt SQLite driver is not available");
            return false;
        }

        db.setDatabaseName(QDir(directory).filePath(QStringLiteral("index.sqlite")));
        if (!db.open())
        {
            error = db.lastError().text();
            return false;
        }

        bool created = true;
        {
            QSqlQuery query(db);
            query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
            query.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
            query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
            created = query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS frames ("
                "frame INTEGER PRIMARY KEY, "
                "meta BLOB NOT NULL, "
                "sha1 BLOB NOT NULL, "
                "bytes INTEGER NOT NULL, "
                "last_access INTEGER NOT NULL)"));
            if (!created)
                error = query.lastError().text();
        }
        if (!created)
        {
            db.close();
            return false;
        }
        return true;
    }

    void closeIndex()
    {
        if (connectionName.isEmpty())
            return;
        {
            QSqlDatabase db = database();
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        connectionName.clear();
    }

    bool load()
    {
        QSqlDatabase db = database();
        QVector<int> staleRows;

        {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral(
                    "SELECT frame, meta, sha1, bytes, last_access FROM frames")))
            {
                error = query.lastError().text();
                return false;
            }

            while (query.next())
            {
                const int frame = query.value(0).toInt();
                Entry entry;
                entry.meta = query.value(1).toByteArray();
                entry.sha1 = query.value(2).toByteArray();
                entry.lastAccess = query.value(4).toLongLong();

                const QString path = framePath(frame);
                const QFileInfo info(path);
                if (!info.exists() || !info.isFile() || info.size() <= 0)
                {
                    staleRows.append(frame);
                    continue;
                }

                entry.bytes = info.size();
                if (entry.lastAccess <= 0)
                    entry.lastAccess = nextAccess();
                clock = std::max(clock, entry.lastAccess);
                diskBytes += entry.bytes;
                entries.insert(frame, entry);
            }
        }

        for (const int frame : staleRows)
            deleteRow(frame);

        // A WebP without an index row is a leftover from an interrupted
        // store.  Nothing will ever look it up, so it would only escape the
        // disk accounting and the LRU budget.
        QSet<QString> indexedFiles;
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
            indexedFiles.insert(QFileInfo(framePath(it.key())).fileName());

        const QFileInfoList files = QDir(directory).entryInfoList(
            QStringList{QStringLiteral("*.webp")}, QDir::Files);
        for (const QFileInfo& info : files)
        {
            if (!indexedFiles.contains(info.fileName()))
                QFile::remove(info.absoluteFilePath());
        }

        enforceDisk();
        return true;
    }

    bool upsertRow(const Entry& entry, int frame)
    {
        QSqlDatabase db = database();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO frames(frame, meta, sha1, bytes, last_access) "
            "VALUES(?, ?, ?, ?, ?) "
            "ON CONFLICT(frame) DO UPDATE SET "
            "meta = excluded.meta, sha1 = excluded.sha1, "
            "bytes = excluded.bytes, last_access = excluded.last_access"));
        query.addBindValue(frame);
        query.addBindValue(entry.meta);
        query.addBindValue(entry.sha1);
        query.addBindValue(entry.bytes);
        query.addBindValue(entry.lastAccess);
        if (!query.exec())
        {
            error = query.lastError().text();
            return false;
        }
        return true;
    }

    bool deleteRow(int frame)
    {
        QSqlDatabase db = database();
        QSqlQuery query(db);
        query.prepare(QStringLiteral("DELETE FROM frames WHERE frame = ?"));
        query.addBindValue(frame);
        if (!query.exec())
        {
            error = query.lastError().text();
            return false;
        }
        return true;
    }

    void forgetImage(int frame)
    {
        auto it = images.find(frame);
        if (it == images.end())
            return;
        memoryBytes = std::max<qint64>(0, memoryBytes - it->sizeInBytes());
        images.erase(it);
        imageAccess.remove(frame);
    }

    void rememberImage(int frame, const QImage& image)
    {
        if (image.isNull())
            return;
        forgetImage(frame);
        images.insert(frame, image);
        imageAccess.insert(frame, nextAccess());
        memoryBytes += image.sizeInBytes();
        enforceMemory();
    }

    void removeEntry(int frame, bool deleteFile)
    {
        auto it = entries.find(frame);
        if (it == entries.end())
            return;

        if (deleteFile)
            QFile::remove(framePath(frame));
        diskBytes = std::max<qint64>(0, diskBytes - it->bytes);
        entries.erase(it);
        forgetImage(frame);
        deleteRow(frame);
    }

    void enforceMemory()
    {
        const qint64 budget = std::max<qint64>(0, options.memoryBudgetBytes);
        while (memoryBytes > budget && !images.isEmpty())
        {
            int victim = -1;
            quint64 oldest = std::numeric_limits<quint64>::max();
            for (auto it = imageAccess.constBegin(); it != imageAccess.constEnd(); ++it)
            {
                if (it.value() < oldest
                    || (it.value() == oldest && (victim < 0 || it.key() < victim)))
                {
                    oldest = it.value();
                    victim = it.key();
                }
            }
            if (victim < 0)
                break;
            forgetImage(victim);
        }
    }

    void enforceDisk()
    {
        const qint64 budget = std::max<qint64>(0, options.diskBudgetBytes);
        while (diskBytes > budget && !entries.isEmpty())
        {
            int victim = -1;
            qint64 oldest = std::numeric_limits<qint64>::max();
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
            {
                if (it->lastAccess < oldest
                    || (it->lastAccess == oldest && (victim < 0 || it.key() < victim)))
                {
                    oldest = it->lastAccess;
                    victim = it.key();
                }
            }
            if (victim < 0)
                break;
            removeEntry(victim, true);
            ++counters.evictions;
        }
    }
};

Cache::Cache(QString root, QString cameraKey, Options options, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->root = QDir(root).absolutePath();
    d->cameraKey = cameraKey;
    d->options = options;
    d->counters.budgetBytes = options.diskBudgetBytes;

    if (root.isEmpty())
    {
        d->error = QStringLiteral("cache root is empty");
        return;
    }

    d->directory =
        QDir(d->root).filePath(directorySignature(options) + QLatin1Char('/')
                               + safeComponent(cameraKey));
    if (!QDir().mkpath(d->directory))
    {
        d->error = QStringLiteral("cannot create cache directory: %1")
                       .arg(d->directory);
        return;
    }

    if (!d->openIndex(this))
    {
        d->closeIndex();
        return;
    }
    if (!d->load())
    {
        d->closeIndex();
        return;
    }
    d->valid = true;
}

Cache::~Cache()
{
    d->closeIndex();
}

bool Cache::isValid() const
{
    return d->valid;
}

QString Cache::error() const
{
    return d->error;
}

QString Cache::directory() const
{
    return d->directory;
}

QSize Cache::imageSize() const
{
    return d->options.targetSize;
}

QImage Cache::lookup(int frame, const FrameMeta& current, bool* hit)
{
    if (hit)
        *hit = false;
    if (!d->valid)
    {
        ++d->counters.misses;
        return {};
    }

    auto it = d->entries.find(frame);
    if (it == d->entries.end())
    {
        ++d->counters.misses;
        return {};
    }

    const FrameMeta stored{it->meta, it->sha1};
    if (!stored.equals(current))
    {
        // The old image deliberately stays on disk: it may still match after
        // the user undoes the edit.
        ++d->counters.misses;
        return {};
    }

    const auto memory = d->images.constFind(frame);
    if (memory != d->images.constEnd())
    {
        it->lastAccess = d->nextAccess();
        d->upsertRow(*it, frame);
        ++d->counters.hits;
        if (hit)
            *hit = true;
        return memory.value();
    }

    QImageReader reader(d->framePath(frame), QByteArrayLiteral("webp"));
    reader.setAutoTransform(false);
    const QImage image = reader.read();
    if (image.isNull())
    {
        // The index still matched but the file is gone/corrupt; drop the row.
        d->removeEntry(frame, true);
        ++d->counters.misses;
        return {};
    }

    it->lastAccess = d->nextAccess();
    d->upsertRow(*it, frame);
    d->rememberImage(frame, image);
    ++d->counters.hits;
    if (hit)
        *hit = true;
    return image;
}

bool Cache::store(int frame, const FrameMeta& meta, const QImage& image,
                  QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                      : d->error);
    if (image.isNull())
        return fail(QStringLiteral("cannot store a null image"));

    QImage scaled = image;
    const QSize target = d->options.targetSize;
    if (target.isValid() && scaled.size() != target)
        scaled = scaled.scaled(target, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    if (scaled.isNull())
        return fail(QStringLiteral("image scaling failed"));

    const QByteArray data = encodeWebp(scaled);
    if (data.isEmpty())
        return fail(QStringLiteral("WebP encoding failed"));

    const QString path = d->framePath(frame);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    if (file.write(data) != data.size() || !file.commit())
        return fail(file.errorString());

    Impl::Entry entry;
    entry.meta = meta.bytes;
    entry.sha1 = meta.sha1;
    entry.bytes = data.size();
    entry.lastAccess = d->nextAccess();

    const auto old = d->entries.constFind(frame);
    const qint64 oldBytes = old == d->entries.constEnd() ? 0 : old->bytes;
    if (!d->upsertRow(entry, frame))
    {
        // Keep index and disk consistent: the old image has already been
        // replaced, so neither the old row nor a half-written state survives.
        QFile::remove(path);
        d->deleteRow(frame);
        d->entries.remove(frame);
        d->forgetImage(frame);
        d->diskBytes = std::max<qint64>(0, d->diskBytes - oldBytes);
        return fail(d->error);
    }

    d->diskBytes = std::max<qint64>(0, d->diskBytes - oldBytes);
    d->entries.insert(frame, entry);
    d->diskBytes += entry.bytes;
    d->rememberImage(frame, scaled);
    ++d->counters.stores;

    d->enforceDisk();
    return true;
}

bool Cache::matches(int frame, const FrameMeta& current) const
{
    if (!d->valid)
        return false;
    const auto it = d->entries.constFind(frame);
    if (it == d->entries.constEnd())
        return false;
    return FrameMeta{it->meta, it->sha1}.equals(current);
}

QSet<int> Cache::cachedFrames() const
{
    QSet<int> frames;
    for (auto it = d->entries.constBegin(); it != d->entries.constEnd(); ++it)
        frames.insert(it.key());
    return frames;
}

CacheStats Cache::stats() const
{
    CacheStats result = d->counters;
    result.diskBytes = d->diskBytes;
    result.budgetBytes = d->options.diskBudgetBytes;
    result.entries = d->entries.size();
    result.memoryFrames = d->images.size();
    return result;
}

void Cache::clear()
{
    if (!d->valid)
        return;

    {
        QSqlDatabase db = d->database();
        QSqlQuery query(db);
        query.exec(QStringLiteral("DELETE FROM frames"));
    }

    for (auto it = d->entries.constBegin(); it != d->entries.constEnd(); ++it)
        QFile::remove(d->framePath(it.key()));

    // Orphaned images from an interrupted earlier run would otherwise escape
    // the byte accounting forever.
    const QFileInfoList files = QDir(d->directory).entryInfoList(
        QStringList{QStringLiteral("*.webp")}, QDir::Files);
    for (const QFileInfo& info : files)
        QFile::remove(info.absoluteFilePath());

    d->entries.clear();
    d->images.clear();
    d->imageAccess.clear();
    d->memoryBytes = 0;
    d->diskBytes = 0;
}

} // namespace pvf::workbench
