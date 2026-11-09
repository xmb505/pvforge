#pragma once

#include "htmlcache/FrameCache.h"

#include <QString>

#include <memory>

namespace pvf::htmlcache {

// Local disk LRU cache. Frames live in <directory>/<canonicalKey>.webp; the
// index is SQLite when Qt6::Sql is available, otherwise a JSON file. The
// directory is disposable: startup removes orphaned or corrupt entries.
class DiskCache : public FrameCache
{
public:
    explicit DiskCache(QString directory, qint64 maxBytes = 2LL * 1024 * 1024 * 1024);
    ~DiskCache() override;

    DiskCache(const DiskCache&) = delete;
    DiskCache& operator=(const DiskCache&) = delete;

    std::optional<QImage> get(const FrameKey& key) override;
    void put(const FrameKey& key, const QImage& image) override;
    void clearAsset(const AssetHash& assetHash) override;

    qint64 currentBytes() const;
    int entryCount() const;
    void setMaxBytes(qint64 maxBytes);
    qint64 maxBytes() const;

    QString directory() const;
    QString indexBackend() const; // "SQLite" or "JSON"
    bool isReady() const;
    QString lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pvf::htmlcache
