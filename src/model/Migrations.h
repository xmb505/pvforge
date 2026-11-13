#pragma once

#include "JsonUtil.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace pvf::model {

// The one and only on-disk format version. It covers project.json and every
// entities/<root-uuid>.json document. v3 stores one root subtree per entity
// file (recursive "children"); the old flat entity directory and single-file
// projects are intentionally not importable, so no production migration step
// exists any more.
inline constexpr int kProjectFormatVersion = 3;
inline constexpr char kProjectFormat[] = "pvforge-project";
// Entity documents carry their own format marker since v2.
inline constexpr char kEntityFormat[] = "pvforge-entity";

} // namespace pvf::model

namespace pvf::model::migrations {

// One step of the on-disk format evolution.
//
// `fromVersion` reads a document of that version and returns the same document
// in version fromVersion + 1. apply() is a pure JSON transform: no disk access,
// no globals, so every step can be tested on a literal document.
//
// New steps are added to registry() below, one entry point per version pair
// named vN_to_vN+1.
struct Migration
{
    int fromVersion = 0;
    QString name;
    std::function<QJsonObject(const QJsonObject&)> apply;
};

// The production chain. It is applied to both project.json and every
// entities/<root-uuid>.json document; a step therefore inspects the document
// shape itself. The v3 root-subtree format breaks the old flat directory, so
// the production registry is intentionally empty: older projects are refused,
// not imported.
const QVector<Migration>& registry();

// Applies `chain` from `fromVersion` up to `toVersion`, step by step, and
// stamps the result with the final version. Returns nullopt with a readable
// error when a step is missing, the range is invalid or the version field is
// inconsistent.
std::optional<QJsonObject> upgrade(const QJsonObject& document,
                                   int fromVersion, int toVersion,
                                   const QVector<Migration>& chain,
                                   QString* error = nullptr);

// upgrade() against registry().
std::optional<QJsonObject> upgrade(const QJsonObject& document,
                                   int fromVersion, int toVersion,
                                   QString* error = nullptr);

// The oldest version the production chain can read. With no production steps
// this is the current version: v3 projects only.
int minimumSupportedVersion();

// The integer "version" of one text document. A document without "version" and
// without a "format" marker but with a "uuid" is a v1 entity file (entities
// only gained their own version in v2) and reads as version 1. Returns nullopt
// with a readable reason when the field is present but not an integer.
std::optional<int> documentVersion(const QJsonObject& document,
                                   QString* error = nullptr);

// Parse-time version gate shared by projectFromJson() and entityFromJson(),
// `label` being the document kind ("工程" / "实体") used in messages. A
// document from a newer build or one that still needs migration is refused
// with a readable reason. In lenient mode a missing version means the current
// one and is reported as a warning.
bool checkDocumentVersion(const QJsonObject& document, const QString& label,
                          const ParseOptions& options,
                          QString* error = nullptr);

} // namespace pvf::model::migrations
