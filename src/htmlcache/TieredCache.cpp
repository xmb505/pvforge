#include "htmlcache/TieredCache.h"

namespace pvf::htmlcache {

TieredCache::TieredCache(FrameCache* hot, FrameCache* cold)
    : m_hot(hot)
    , m_cold(cold)
{
}

std::optional<QImage> TieredCache::get(const FrameKey& key)
{
    if (m_hot)
    {
        if (std::optional<QImage> image = m_hot->get(key))
            return image;
    }

    if (!m_cold)
        return std::nullopt;

    std::optional<QImage> image = m_cold->get(key);
    if (image && m_hot)
        m_hot->put(key, *image);
    return image;
}

void TieredCache::put(const FrameKey& key, const QImage& image)
{
    if (m_hot)
        m_hot->put(key, image);
    if (m_cold)
        m_cold->put(key, image);
}

void TieredCache::clearAsset(const AssetHash& assetHash)
{
    if (m_hot)
        m_hot->clearAsset(assetHash);
    if (m_cold)
        m_cold->clearAsset(assetHash);
}

} // namespace pvf::htmlcache
