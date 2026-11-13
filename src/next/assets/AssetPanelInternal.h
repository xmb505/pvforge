#pragma once

#include <QSize>
#include <QString>

namespace pvf::next::detail {

// Shared by the split AssetPanel sources: item data roles of the asset rows,
// the drag MIME type and the thumbnail size.

constexpr int kHashRole = Qt::UserRole;
constexpr int kMissingRole = Qt::UserRole + 1;
constexpr int kNameRole = Qt::UserRole + 2;
constexpr int kTagsRole = Qt::UserRole + 3;

constexpr char kAssetMimeType[] = "application/x-pvforge-asset";

inline QSize thumbSize()
{
    return QSize(56, 40);
}

// Short hash shown in list rows and tooltips.
inline QString shortHash(const QString& hash)
{
    return hash.left(8);
}

} // namespace pvf::next::detail
