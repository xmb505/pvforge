#pragma once

#include "common/Types.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace pvf::htmlcache {

// Identity of one rendered HTML frame. Entity UUIDs and global timeline
// positions are deliberately absent: the cache must be reusable across
// entities that show the same asset at the same local frame.
struct FrameKey
{
    AssetHash assetHash;
    int localFrame = 0;
    QVariantMap variables;
    QSize size;
    QString chromeVersion;
    QString entryHash;
};

// Deterministic, filesystem-safe and human readable key. Variable names are
// sorted and floating point values are formatted with exactly six decimals.
QString canonicalKey(const FrameKey& key);

} // namespace pvf::htmlcache
