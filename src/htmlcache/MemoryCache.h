#pragma once

#include "htmlcache/FrameCache.h"

#include <QString>

#include <memory>

namespace pvf::htmlcache {

// Process-local LRU cache. Images are kept as QImage values, so callers and
// the cache share pixels through Qt's implicit sharing. Nothing is persisted:
// destroying the cache (or restarting the app) clears it.
class MemoryCache : public FrameCache
{
public:
    explicit MemoryCache(qint64 maxBytes = 256LL * 1024 * 1024);
    ~MemoryCache() override;

    MemoryCache(const MemoryCache&) = delete;
    MemoryCache& operator=(const MemoryCache&) = delete;

    std::optional<QImage> get(const FrameKey& key) override;
    void put(const FrameKey& key, const QImage& image) override;
    void clearAsset(const AssetHash& assetHash) override;

    qint64 currentBytes() const;
    int entryCount() const;
    void setMaxBytes(qint64 maxBytes);
    qint64 maxBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pvf::htmlcache
