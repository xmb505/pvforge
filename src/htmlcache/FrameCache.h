#pragma once

#include "htmlcache/FrameKey.h"

#include <QByteArray>
#include <QImage>

#include <optional>

namespace pvf::htmlcache {

// WebP codec shared by every backend. Encoding quality defaults to 80.
// An empty byte array means the image could not be encoded.
QByteArray encodeWebp(const QImage& image, int quality = 80);
std::optional<QImage> decodeWebp(const QByteArray& data);

class FrameCache
{
public:
    virtual ~FrameCache() = default;

    // Returns nullopt on miss. Implementations must never throw for a miss.
    virtual std::optional<QImage> get(const FrameKey& key) = 0;

    // Encodes and stores an image. Failures are silent.
    virtual void put(const FrameKey& key, const QImage& image) = 0;

    // Removes every cached frame that belongs to the asset.
    virtual void clearAsset(const AssetHash& assetHash) = 0;
};

} // namespace pvf::htmlcache
