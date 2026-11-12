#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

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

// The production chain. It is empty while v1 is the only format ever shipped;
// a future format change appends exactly one step here (for example
// {"v1_to_v2", v1_to_v2}).
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

// The oldest version the production chain can read. With an empty registry
// this is the current format version, so older documents are rejected with a
// readable message instead of being half-parsed.
int minimumSupportedVersion();

} // namespace pvf::model::migrations
