#pragma once

// Shared declarations for the entity.* tool family.  EntityTools.cpp was split
// by tool group (create / read / edit / trim / delete) plus one content-parsing
// support file; these helpers stayed shared instead of being duplicated.
//
// Nothing here is part of the MCP surface: the handlers keep their declarations
// in mcp2/Internal.h and only use this header between translation units.

#include "mcp2/Internal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace pvf::mcp2::detail::entity {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxEntities = 5000;
constexpr int kMaxBatch = 200;
constexpr int kMaxTreeNodes = 200;
constexpr int kMaxDepth = 64;
constexpr int kMaxFrame = 1000000;

// ---- content parsing (EntityContentSupport.cpp) -------------------------
std::optional<ContentKind> specialContentKindFromName(const QString& name);
Result validateContentFieldsJson(const QJsonObject& object,
                                 const QString& kindName);
int assetDurationFrames(const assets::StoredAsset& asset);
Result checkAssetDuration(const assets::AssetStore& assetStore,
                          const model::Content& content, int duration);
Result parseContentForCreate(const assets::AssetStore& assetStore,
                             const QJsonValue& value, model::Content* out);
QString defaultNameForContent(const assets::AssetStore& assetStore,
                              const model::Content& content);

// ---- solver dependencies (EntityDeleteTools.cpp) ------------------------
// Solver segments that point at one of `referenced`; used by delete (report
// what would break) and reparent (confirm before moving the target).
QJsonArray dependentSolvers(const model::Project& project,
                            const QSet<Uuid>& referenced);

} // namespace pvf::mcp2::detail::entity
