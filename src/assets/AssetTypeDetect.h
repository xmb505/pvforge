#pragma once

#include "common/Types.h"

#include <QString>

#include <optional>

namespace pvf::assets {

// Detects an asset type from the file extension first and falls back to
// container magic bytes. Directories containing `index.html` or
// `pvhtml.json` are HTML bundles. Returns std::nullopt for unsupported or
// missing paths.
std::optional<AssetType> detectType(const QString& path);

} // namespace pvf::assets
