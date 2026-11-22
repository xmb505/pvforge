#include "htmlcache/MemoryCache.h"

#include "framecache/ImageLru.h"

#include <algorithm>
#include <limits>

namespace pvf::htmlcache {
namespace {

qint64 effectiveMaxBytes(qint64 bytes)
{
    return std::clamp<qint64>(bytes, 0, std::numeric_limits<int>::max());
}

} // namespace

struct MemoryCache::Impl
{
    framecache::ImageLru lru;
    qint64 maxBytes = 0;
};

MemoryCache::MemoryCache(qint64 maxBytes)
    : d(std::make_unique<Impl>())
{
    d->maxBytes = effectiveMaxBytes(maxBytes);
    d->lru.setBudgetBytes(d->maxBytes);
}

MemoryCache::~MemoryCache() = default;

std::optional<QImage> MemoryCache::get(const FrameKey& key)
{
    return d->lru.get(canonicalKey(key));
}

void MemoryCache::put(const FrameKey& key, const QImage& image)
{
    d->lru.put(canonicalKey(key), image, key.assetHash);
}

void MemoryCache::clearAsset(const AssetHash& assetHash)
{
    d->lru.removeTag(assetHash);
}

// Sizing introspection has no production caller; only the htmlcache tests
// look at it, so it is compiled into test builds only.
#ifdef PVFORGE_TEST_HOOKS
qint64 MemoryCache::currentBytes() const
{
    return d->lru.bytes();
}

int MemoryCache::entryCount() const
{
    return d->lru.entryCount();
}

void MemoryCache::setMaxBytes(qint64 maxBytes)
{
    d->maxBytes = effectiveMaxBytes(maxBytes);
    d->lru.setBudgetBytes(d->maxBytes);
}

qint64 MemoryCache::maxBytes() const
{
    return d->maxBytes;
}
#endif

} // namespace pvf::htmlcache
