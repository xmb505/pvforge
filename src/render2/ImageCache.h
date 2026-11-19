#pragma once

#include "assets/AssetStore.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace pvf::render2 {

// One decoded image asset: the pixels plus the model-space size declared in
// the asset metadata (falling back to the decoded size).
struct Picture
{
    QImage image;
    QSize size;

    bool isValid() const { return !image.isNull(); }
};

// Decoded, content-addressed image cache shared by the viewport, the camera
// monitor and the export worker (issue #131).
//
// A miss decodes the content file once; every following frame of the same
// asset reuses the QImage.  The key includes the content path, size and
// modification time, so re-importing/replacing the file invalidates the entry.
// The cache is thread-safe and bounded by decoded bytes (LRU).
Picture loadPicture(const assets::AssetStore& store, const AssetHash& hash);

struct ImageCacheStats
{
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 decodes = 0;
    qint64 bytes = 0;
    int entries = 0;
};

ImageCacheStats imageCacheStats();

// Drops every cached picture (tests and explicit cache resets).
void clearImageCache();

} // namespace pvf::render2
