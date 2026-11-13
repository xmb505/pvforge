#include "htmlcache/DiskCache.h"

#include "framecache/LegacyImport.h"
#include "framecache/Store.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImageReader>

#include <algorithm>
#include <utility>

namespace pvf::htmlcache {

struct DiskCache::Impl
{
    QString directory;
    qint64 maxBytes = 0;
    bool ready = false;
    QString lastError;
    std::unique_ptr<framecache::Store> store;

    void noteError(const QString& message)
    {
        lastError = message;
        qWarning().noquote() << QStringLiteral("htmlcache:") << message;
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
    d->maxBytes = std::max<qint64>(0, maxBytes);

    // Historical layouts: an "entries" table and, for the no-SQLite fallback
    // build, an index.json sidecar.  Import them before the Store so its
    // orphan sweep never treats an unindexed legacy frame as garbage.
    const framecache::LegacyImportResult imported =
        framecache::importLegacyIndex(
            d->directory, {QStringLiteral("entries")},
            QStringLiteral("index.json"));
    if (!imported.ok())
    {
        d->lastError = imported.error;
        return;
    }

    framecache::Options options;
    options.directory = d->directory;
    options.budgetBytes = d->maxBytes;
    options.extensions = {QStringLiteral("webp")};
    options.validate = [](const QString& path, const framecache::Record&) {
        QImageReader reader(path, QByteArrayLiteral("webp"));
        return reader.canRead();
    };

    d->store = std::make_unique<framecache::Store>(std::move(options));
    if (!d->store->isValid())
    {
        d->lastError = d->store->error();
        return;
    }
    d->ready = true;
}

DiskCache::~DiskCache() = default;

std::optional<QImage> DiskCache::get(const FrameKey& key)
{
    if (!d->ready)
        return std::nullopt;

    const QString canonical = canonicalKey(key);
    const auto record = d->store->record(canonical);
    if (!record)
    {
        d->store->noteMiss();
        return std::nullopt;
    }

    QFile file(d->store->pathFor(*record));
    if (!file.open(QIODevice::ReadOnly))
    {
        d->store->remove(canonical, false);
        d->store->noteMiss();
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    file.close();

    std::optional<QImage> image = decodeWebp(data);
    if (!image)
    {
        // Corrupt WebP: delete it and report a miss.
        d->store->remove(canonical, true);
        d->store->noteMiss();
        return std::nullopt;
    }

    if (data.size() != record->bytes)
    {
        framecache::Record repaired = *record;
        repaired.bytes = data.size();
        d->store->update(repaired);
    }
    d->store->touch(canonical);
    d->store->noteHit();
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

    framecache::Record record;
    record.key = canonicalKey(key);
    record.codec = QStringLiteral("webp");
    record.tag = key.assetHash.toUtf8();

    QString error;
    if (!d->store->put(record, data, &error) && !error.isEmpty())
        d->noteError(error);
}

void DiskCache::clearAsset(const AssetHash& assetHash)
{
    if (!d->ready)
        return;

    const QByteArray tag = assetHash.toUtf8();
    QStringList keys;
    for (auto it = d->store->records().cbegin();
         it != d->store->records().cend(); ++it)
    {
        if (it->tag == tag)
            keys.append(it.key());
    }
    for (const QString& key : keys)
        d->store->remove(key, true);
}

qint64 DiskCache::currentBytes() const
{
    return d->ready ? d->store->stats().diskBytes : 0;
}

int DiskCache::entryCount() const
{
    return d->ready ? d->store->stats().entries : 0;
}

void DiskCache::setMaxBytes(qint64 maxBytes)
{
    d->maxBytes = std::max<qint64>(0, maxBytes);
    if (d->ready)
        d->store->setBudgetBytes(d->maxBytes);
}

qint64 DiskCache::maxBytes() const
{
    return d->maxBytes;
}

QString DiskCache::directory() const
{
    return d->directory;
}

bool DiskCache::isReady() const
{
    return d->ready;
}

QString DiskCache::lastError() const
{
    return d->lastError;
}

framecache::Stats DiskCache::stats() const
{
    framecache::Stats stats;
    stats.budgetBytes = d->maxBytes;
    if (d->ready)
        stats = d->store->stats();
    return stats;
}

} // namespace pvf::htmlcache
