#pragma once

#include "htmlcache/FrameCache.h"

namespace pvf::htmlcache {

// Hot in-memory cache in front of a cold persistent cache. A cold hit is
// written back to hot. Pointers are borrowed; the caller owns both caches.
// Typical setup: TieredCache(memoryCache, diskCache).
class TieredCache : public FrameCache
{
public:
    TieredCache(FrameCache* hot, FrameCache* cold);
    ~TieredCache() override = default;

    std::optional<QImage> get(const FrameKey& key) override;
    void put(const FrameKey& key, const QImage& image) override;
    void clearAsset(const AssetHash& assetHash) override;

private:
    FrameCache* m_hot = nullptr;
    FrameCache* m_cold = nullptr;
};

} // namespace pvf::htmlcache
