#include "htmlcache/DiskCache.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <utility>

#ifdef PVF_HTMLCACHE_HAVE_SQLITE
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#endif

namespace pvf::htmlcache {
namespace {

struct Entry
{
    QString key;
    QString path;
    qint64 size = 0;
    qint64 lastAccess = 0;
    AssetHash assetHash;
};

QString expectedPath(const QString& directory, const QString& key)
{
    return QDir(directory).filePath(key + QStringLiteral(".webp"));
}

bool safeKey(const QString& key)
{
    if (key.isEmpty() || key == QLatin1String(".") || key == QLatin1String(".."))
        return false;
    return !key.contains(QLatin1Char('/')) && !key.contains(QLatin1Char('\\'));
}

class IndexBackend
{
public:
    virtual ~IndexBackend() = default;

    virtual QString name() const = 0;
    virtual bool load(QVector<Entry>* entries, QString* error) = 0;
    virtual bool upsert(const Entry& entry, QString* error = nullptr) = 0;
    virtual bool remove(const QString& key, QString* error = nullptr) = 0;
};

class JsonIndex final : public IndexBackend
{
public:
    explicit JsonIndex(QString path)
        : m_path(std::move(path))
    {
    }

    QString name() const override
    {
        return QStringLiteral("JSON");
    }

    bool load(QVector<Entry>* entries, QString* error) override
    {
        entries->clear();
        m_entries.clear();

        QFile file(m_path);
        if (!file.exists())
            return true;
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                *error = file.errorString();
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            if (error)
                *error = QStringLiteral("invalid JSON index: %1").arg(parseError.errorString());
            return false;
        }

        const QJsonArray array = document.object().value(QStringLiteral("entries")).toArray();
        for (const QJsonValue& value : array)
        {
            if (!value.isObject())
                continue;
            const QJsonObject object = value.toObject();
            Entry entry;
            entry.key = object.value(QStringLiteral("key")).toString();
            entry.path = object.value(QStringLiteral("path")).toString();
            entry.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
            entry.lastAccess =
                static_cast<qint64>(object.value(QStringLiteral("lastAccess")).toDouble());
            entry.assetHash = object.value(QStringLiteral("assetHash")).toString();
            if (entry.key.isEmpty())
                continue;
            m_entries.insert(entry.key, entry);
            entries->append(entry);
        }
        return true;
    }

    bool upsert(const Entry& entry, QString* error) override
    {
        m_entries.insert(entry.key, entry);
        return save(error);
    }

    bool remove(const QString& key, QString* error) override
    {
        if (m_entries.remove(key) == 0)
            return true;
        return save(error);
    }

private:
    bool save(QString* error)
    {
        QJsonArray array;
        QStringList keys = m_entries.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString& key : keys)
        {
            const Entry& entry = m_entries.value(key);
            QJsonObject object;
            object.insert(QStringLiteral("key"), entry.key);
            object.insert(QStringLiteral("path"), entry.path);
            object.insert(QStringLiteral("size"), entry.size);
            object.insert(QStringLiteral("lastAccess"), entry.lastAccess);
            object.insert(QStringLiteral("assetHash"), entry.assetHash);
            array.append(object);
        }

        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("entries"), array);

        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly))
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size() || !file.commit())
        {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
    }

    QString m_path;
    QHash<QString, Entry> m_entries;
};

#ifdef PVF_HTMLCACHE_HAVE_SQLITE
class SqliteIndex final : public IndexBackend
{
public:
    explicit SqliteIndex(QString path)
        : m_path(std::move(path))
    {
    }

    ~SqliteIndex() override
    {
        close();
    }

    QString name() const override
    {
        return QStringLiteral("SQLite");
    }

    bool open(QString* error)
    {
        m_connection =
            QStringLiteral("pvf_htmlcache_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        if (!database.isValid())
        {
            if (error)
                *error = QStringLiteral("Qt SQLite driver is not available");
            close();
            return false;
        }

        database.setDatabaseName(m_path);
        if (!database.open())
        {
            if (error)
                *error = database.lastError().text();
            close();
            return false;
        }

        QSqlQuery query(database);
        query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        query.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS entries ("
                "key TEXT PRIMARY KEY, "
                "path TEXT NOT NULL, "
                "size INTEGER NOT NULL, "
                "last_access INTEGER NOT NULL, "
                "asset_hash TEXT NOT NULL)")))
        {
            if (error)
                *error = query.lastError().text();
            close();
            return false;
        }
        query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS entries_asset ON entries(asset_hash)"));
        query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS entries_access ON entries(last_access)"));
        return true;
    }

    bool load(QVector<Entry>* entries, QString* error) override
    {
        entries->clear();
        QSqlDatabase database = connection();
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral(
                "SELECT key, path, size, last_access, asset_hash FROM entries")))
        {
            if (error)
                *error = query.lastError().text();
            return false;
        }

        while (query.next())
        {
            Entry entry;
            entry.key = query.value(0).toString();
            entry.path = query.value(1).toString();
            entry.size = query.value(2).toLongLong();
            entry.lastAccess = query.value(3).toLongLong();
            entry.assetHash = query.value(4).toString();
            if (!entry.key.isEmpty())
                entries->append(entry);
        }
        return true;
    }

    bool upsert(const Entry& entry, QString* error) override
    {
        QSqlDatabase database = connection();
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "INSERT INTO entries(key, path, size, last_access, asset_hash) "
            "VALUES(?, ?, ?, ?, ?) "
            "ON CONFLICT(key) DO UPDATE SET "
            "path = excluded.path, size = excluded.size, "
            "last_access = excluded.last_access, asset_hash = excluded.asset_hash"));
        query.addBindValue(entry.key);
        query.addBindValue(entry.path);
        query.addBindValue(entry.size);
        query.addBindValue(entry.lastAccess);
        query.addBindValue(entry.assetHash);
        if (!query.exec())
        {
            if (error)
                *error = query.lastError().text();
            return false;
        }
        return true;
    }

    bool remove(const QString& key, QString* error) override
    {
        QSqlDatabase database = connection();
        QSqlQuery query(database);
        query.prepare(QStringLiteral("DELETE FROM entries WHERE key = ?"));
        query.addBindValue(key);
        if (!query.exec())
        {
            if (error)
                *error = query.lastError().text();
            return false;
        }
        return true;
    }

private:
    QSqlDatabase connection() const
    {
        return QSqlDatabase::database(m_connection, false);
    }

    void close()
    {
        if (m_connection.isEmpty())
            return;
        {
            QSqlDatabase database = QSqlDatabase::database(m_connection, false);
            if (database.isOpen())
                database.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
        m_connection.clear();
    }

    QString m_path;
    QString m_connection;
};
#endif

} // namespace

struct DiskCache::Impl
{
    QString directory;
    qint64 maxBytes = 0;
    qint64 totalBytes = 0;
    qint64 clock = 0;
    bool ready = false;
    QString lastError;
    QHash<QString, Entry> entries;
    std::unique_ptr<IndexBackend> index;

    qint64 nextAccessTime()
    {
        // Monotonic even when several operations happen in the same
        // millisecond; the LRU order must stay deterministic.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        clock = std::max(now, clock + 1);
        return clock;
    }

    void noteError(const QString& message)
    {
        lastError = message;
        qWarning().noquote() << QStringLiteral("htmlcache:") << message;
    }

    void removeEntry(const QString& key, bool deleteFile)
    {
        auto it = entries.find(key);
        if (it == entries.end())
            return;

        if (deleteFile)
            QFile::remove(it->path);
        totalBytes = std::max<qint64>(0, totalBytes - it->size);
        entries.erase(it);

        QString error;
        if (!index->remove(key, &error) && !error.isEmpty())
            noteError(error);
    }

    void enforceLimit()
    {
        while (totalBytes > maxBytes && !entries.isEmpty())
        {
            QString oldestKey;
            qint64 oldestAccess = std::numeric_limits<qint64>::max();
            for (auto it = entries.cbegin(); it != entries.cend(); ++it)
            {
                if (it->lastAccess < oldestAccess
                    || (it->lastAccess == oldestAccess
                        && (oldestKey.isEmpty() || it.key() < oldestKey)))
                {
                    oldestAccess = it->lastAccess;
                    oldestKey = it.key();
                }
            }
            if (oldestKey.isEmpty())
                break;
            removeEntry(oldestKey, true);
        }
    }

    void reload()
    {
        QVector<Entry> loaded;
        QString error;
        if (!index->load(&loaded, &error))
            noteError(error);

        entries.clear();
        totalBytes = 0;
        clock = 0;

        for (Entry entry : loaded)
        {
            if (!safeKey(entry.key))
            {
                const QString path = expectedPath(directory, entry.key);
                QFile::remove(path);
                index->remove(entry.key);
                continue;
            }

            const QString path = expectedPath(directory, entry.key);
            const bool pathChanged = entry.path != path;
            entry.path = path;

            QFileInfo fileInfo(path);
            if (!fileInfo.exists() || !fileInfo.isFile())
            {
                // Index entry without a file: drop the row.
                index->remove(entry.key);
                continue;
            }

            bool changed = pathChanged;
            if (fileInfo.size() != entry.size || entry.size <= 0)
            {
                entry.size = fileInfo.size();
                changed = true;
            }

            QImageReader reader(path, QByteArrayLiteral("webp"));
            if (!reader.canRead())
            {
                QFile::remove(path);
                index->remove(entry.key);
                continue;
            }

            if (entry.lastAccess <= 0)
            {
                entry.lastAccess = QDateTime::currentMSecsSinceEpoch();
                changed = true;
            }

            if (changed)
                index->upsert(entry);

            clock = std::max(clock, entry.lastAccess);
            totalBytes += entry.size;
            entries.insert(entry.key, entry);
        }

        // Files that are not in the index (for example left over after a
        // corrupt index) would otherwise escape size accounting forever.
        const QFileInfoList files =
            QDir(directory).entryInfoList(QStringList{QStringLiteral("*.webp")}, QDir::Files);
        for (const QFileInfo& fileInfo : files)
        {
            if (!entries.contains(fileInfo.completeBaseName()))
                QFile::remove(fileInfo.absoluteFilePath());
        }

        enforceLimit();
    }
};

DiskCache::DiskCache(QString directory, qint64 maxBytes)
    : d(std::make_unique<Impl>())
{
    if (directory.isEmpty())
    {
        d->lastError = QStringLiteral("cache directory is empty");
        return;
    }

    d->directory = QDir(directory).absolutePath();
    if (!QDir().mkpath(d->directory))
    {
        d->lastError = QStringLiteral("cannot create cache directory: %1").arg(d->directory);
        return;
    }

    d->maxBytes = std::max<qint64>(0, maxBytes);

#ifdef PVF_HTMLCACHE_HAVE_SQLITE
    auto sqlite = std::make_unique<SqliteIndex>(QDir(d->directory).filePath(
        QStringLiteral("index.sqlite")));
    QString sqliteError;
    if (sqlite->open(&sqliteError))
        d->index = std::move(sqlite);
    else
        qWarning().noquote() << QStringLiteral("htmlcache: SQLite index unavailable: %1")
                                    .arg(sqliteError);
#endif

    if (!d->index)
    {
        d->index = std::make_unique<JsonIndex>(
            QDir(d->directory).filePath(QStringLiteral("index.json")));
    }

    d->ready = true;
    d->reload();
}

DiskCache::~DiskCache() = default;

std::optional<QImage> DiskCache::get(const FrameKey& key)
{
    if (!d->ready)
        return std::nullopt;

    const QString canonical = canonicalKey(key);
    auto it = d->entries.find(canonical);
    if (it == d->entries.end())
        return std::nullopt;

    QFile file(it->path);
    if (!file.open(QIODevice::ReadOnly))
    {
        d->removeEntry(canonical, false);
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    file.close();

    std::optional<QImage> image = decodeWebp(data);
    if (!image)
    {
        // Corrupt WebP: delete it and report a miss.
        d->removeEntry(canonical, true);
        return std::nullopt;
    }

    if (data.size() != it->size)
    {
        d->totalBytes = std::max<qint64>(0, d->totalBytes - it->size + data.size());
        it->size = data.size();
    }
    it->lastAccess = d->nextAccessTime();

    QString error;
    if (!d->index->upsert(*it, &error) && !error.isEmpty())
        d->noteError(error);

    return image;
}

void DiskCache::put(const FrameKey& key, const QImage& image)
{
    if (!d->ready || image.isNull())
        return;

    const QByteArray data = encodeWebp(image);
    if (data.isEmpty())
    {
        d->noteError(QStringLiteral("WebP encoding failed"));
        return;
    }

    const QString canonical = canonicalKey(key);
    const QString path = expectedPath(d->directory, canonical);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        d->noteError(file.errorString());
        return;
    }
    if (file.write(data) != data.size() || !file.commit())
    {
        d->noteError(file.errorString());
        return;
    }

    Entry entry;
    entry.key = canonical;
    entry.path = path;
    entry.size = data.size();
    entry.lastAccess = d->nextAccessTime();
    entry.assetHash = key.assetHash;

    auto existing = d->entries.constFind(canonical);
    if (existing != d->entries.cend())
        d->totalBytes = std::max<qint64>(0, d->totalBytes - existing->size);
    d->entries.insert(canonical, entry);
    d->totalBytes += entry.size;

    QString error;
    if (!d->index->upsert(entry, &error) && !error.isEmpty())
        d->noteError(error);

    d->enforceLimit();
}

void DiskCache::clearAsset(const AssetHash& assetHash)
{
    if (!d->ready)
        return;

    QStringList keys;
    for (auto it = d->entries.cbegin(); it != d->entries.cend(); ++it)
    {
        if (it->assetHash == assetHash)
            keys.append(it.key());
    }
    for (const QString& key : keys)
        d->removeEntry(key, true);
}

qint64 DiskCache::currentBytes() const
{
    return d->totalBytes;
}

int DiskCache::entryCount() const
{
    return d->entries.size();
}

void DiskCache::setMaxBytes(qint64 maxBytes)
{
    d->maxBytes = std::max<qint64>(0, maxBytes);
    d->enforceLimit();
}

qint64 DiskCache::maxBytes() const
{
    return d->maxBytes;
}

QString DiskCache::directory() const
{
    return d->directory;
}

QString DiskCache::indexBackend() const
{
    return d->index ? d->index->name() : QString();
}

bool DiskCache::isReady() const
{
    return d->ready;
}

QString DiskCache::lastError() const
{
    return d->lastError;
}

} // namespace pvf::htmlcache
