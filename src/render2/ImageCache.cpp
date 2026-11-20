#include "ImageCache.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <list>

namespace pvf::render2 {
namespace {

// Decoded pictures are large (a 4K RGBA frame is ~33 MB); keep a bounded
// working set instead of every image the project ever used.
constexpr qint64 kImageCacheBudgetBytes = 256LL * 1024 * 1024;

struct Entry
{
    Picture picture;
    qint64 bytes = 0;
    std::list<QString>::iterator order;
};

struct CacheState
{
    QMutex mutex;
    QHash<QString, Entry> entries;
    std::list<QString> order; // most recently used at the front
    qint64 bytes = 0;
    ImageCacheStats stats;
};

CacheState& state()
{
    static CacheState cache;
    return cache;
}

QString contentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

quint64 imageBytes(const QImage& image)
{
    return image.isNull() ? 0 : quint64(image.sizeInBytes());
}

void touch(CacheState& cache, QHash<QString, Entry>::iterator it)
{
    cache.order.splice(cache.order.begin(), cache.order, it->order);
}

void evictToBudget(CacheState& cache)
{
    while (cache.bytes > kImageCacheBudgetBytes && !cache.order.empty())
    {
        const QString key = cache.order.back();
        cache.order.pop_back();
        const auto it = cache.entries.find(key);
        if (it == cache.entries.end())
            continue;
        cache.bytes -= it->bytes;
        cache.entries.erase(it);
    }
}

} // namespace

Picture loadPicture(const assets::AssetStore& store, const AssetHash& hash)
{
    const auto stored = store.get(hash);
    if (!stored)
        return {};

    const QString path = contentFile(*stored);
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    const QString key = QStringList{
        hash,
        path,
        QString::number(info.size()),
        QString::number(info.lastModified().toMSecsSinceEpoch())}
                            .join(QChar(0x1f));

    CacheState& cache = state();
    {
        QMutexLocker locker(&cache.mutex);
        const auto it = cache.entries.find(key);
        if (it != cache.entries.end())
        {
            ++cache.stats.hits;
            touch(cache, it);
            return it->picture;
        }
        ++cache.stats.misses;
    }

    Picture picture;
    picture.image = QImage(path);
    ++cache.stats.decodes;
    if (picture.image.isNull())
        return {};

    const int width = stored->metadata.value(QStringLiteral("width")).toInt();
    const int height = stored->metadata.value(QStringLiteral("height")).toInt();
    picture.size = (width > 0 && height > 0) ? QSize(width, height)
                                             : picture.image.size();

    const qint64 bytes = qint64(imageBytes(picture.image));
    {
        QMutexLocker locker(&cache.mutex);
        if (cache.entries.contains(key))
            return picture;
        cache.order.push_front(key);
        cache.entries.insert(key, Entry{picture, bytes, cache.order.begin()});
        cache.bytes += bytes;
        evictToBudget(cache);
    }
    return picture;
}

ImageCacheStats imageCacheStats()
{
    CacheState& cache = state();
    QMutexLocker locker(&cache.mutex);
    ImageCacheStats stats = cache.stats;
    stats.bytes = cache.bytes;
    stats.entries = int(cache.entries.size());
    return stats;
}

void clearImageCache()
{
    CacheState& cache = state();
    QMutexLocker locker(&cache.mutex);
    cache.entries.clear();
    cache.order.clear();
    cache.bytes = 0;
    cache.stats = ImageCacheStats();
}

} // namespace pvf::render2
