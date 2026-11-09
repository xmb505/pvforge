#include "htmlcache/MemoryCache.h"

#include <QCache>

#include <algorithm>
#include <limits>

namespace pvf::htmlcache {
namespace {

struct Item
{
    QImage image;
    AssetHash assetHash;
};

int costFor(const QImage& image)
{
    const qint64 bytes = std::max<qint64>(1, image.sizeInBytes());
    return static_cast<int>(std::min<qint64>(bytes, std::numeric_limits<int>::max()));
}

qint64 effectiveMaxBytes(qint64 bytes)
{
    return std::clamp<qint64>(bytes, 0, std::numeric_limits<int>::max());
}

} // namespace

struct MemoryCache::Impl
{
    QCache<QString, Item> items;
    qint64 maxBytes = 0;
};

MemoryCache::MemoryCache(qint64 maxBytes)
    : d(std::make_unique<Impl>())
{
    setMaxBytes(maxBytes);
}

MemoryCache::~MemoryCache() = default;

std::optional<QImage> MemoryCache::get(const FrameKey& key)
{
    Item* item = d->items.object(canonicalKey(key));
    if (!item)
        return std::nullopt;
    // QImage is implicitly shared, so this copy does not duplicate pixels.
    return item->image;
}

void MemoryCache::put(const FrameKey& key, const QImage& image)
{
    if (image.isNull())
        return;

    d->items.insert(canonicalKey(key), new Item{image, key.assetHash}, costFor(image));
}

void MemoryCache::clearAsset(const AssetHash& assetHash)
{
    const QList<QString> keys = d->items.keys();
    for (const QString& key : keys)
    {
        const Item* item = d->items.object(key);
        if (item && item->assetHash == assetHash)
            d->items.remove(key);
    }
}

qint64 MemoryCache::currentBytes() const
{
    return static_cast<qint64>(d->items.totalCost());
}

int MemoryCache::entryCount() const
{
    return static_cast<int>(d->items.size());
}

void MemoryCache::setMaxBytes(qint64 maxBytes)
{
    d->maxBytes = effectiveMaxBytes(maxBytes);
    d->items.setMaxCost(static_cast<int>(d->maxBytes));
}

qint64 MemoryCache::maxBytes() const
{
    return d->maxBytes;
}

} // namespace pvf::htmlcache
