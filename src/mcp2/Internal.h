#pragma once

#include "mcp2/Service.h"

#include "assets/Assets.h"
#include "model/Model.h"
#include "store/Store.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <functional>

namespace pvf::mcp2::detail {

// One tool call works on a private copy of the project. Nothing reaches the
// store until finish() validates the whole result, so any early failure simply
// drops the copy and leaves the project untouched.
struct Tx
{
    explicit Tx(store::ProjectStore* projectStore);

    store::ProjectStore* store = nullptr;
    model::Project backup;
    model::Project project;
    ViewState viewState;
    QSet<Uuid> dirty;
    QSet<Uuid> deleted;
    bool projectDirty = false;
    // Set by Service::execute. Batch loops call stopRequested(tx) between
    // items; a landing human stop drops the whole transaction.
    std::function<bool()> stopCheck;

    model::Project& p() { return project; }
    const model::Project& p() const { return project; }
    void touch(const Uuid& uuid) { dirty.insert(uuid); }
    void drop(const Uuid& uuid);
    void touchProject() { projectDirty = true; }

    // Validates, writes the touched files, commits once and returns the success
    // envelope. A validation failure returns invalid_argument without writing.
    Result finish(const QJsonObject& data, const QString& message);
};

Result failure(const QString& code, const QString& message,
               const QJsonObject& details = QJsonObject());
Result success(const QJsonObject& data = QJsonObject());

// Human stop helpers. stopRequested() is false when no stop source is wired.
// Long batch loops call it between items and bail out with stoppedByHuman(),
// which drops the transaction copy without writing anything.
bool stopRequested(Tx& tx);
Result stoppedByHuman();

// Argument readers.
bool readInt(const QJsonObject& args, const char* key, int* out, Result* error,
             bool required = false);
bool readDouble(const QJsonObject& args, const char* key, double* out,
                Result* error, bool required = false);
bool readBool(const QJsonObject& args, const char* key, bool* out,
              Result* error, bool required = false);
bool readString(const QJsonObject& args, const char* key, QString* out,
                Result* error, bool required = false);
bool readUuid(const QJsonObject& args, const char* key, Uuid* out,
              Result* error, bool required = false, bool allowNull = false);
bool confirmed(const QJsonObject& args);

// JSON views shared by the tool families.
QJsonObject settingsJson(const model::ProjectSettings& settings);
QJsonObject entityBrief(const model::Entity& entity);
QJsonObject entityFull(const model::Entity& entity);
QJsonObject pageJson(int offset, int limit, int total);
QJsonObject assetJson(const assets::AssetStore& assetStore,
                      const model::Project& project,
                      const assets::StoredAsset& asset);

// Model helpers.
model::Entity* findEntity(model::Project& project, const Uuid& uuid);
const model::Entity* findEntity(const model::Project& project, const Uuid& uuid);
Uuid lockedBy(const model::Project& project, const Uuid& uuid);
// Returns conflict with details.lockedBy when the entity or an ancestor is
// locked. This is the single write-guard used by every tool family.
bool ensureUnlocked(const model::Project& project, const Uuid& uuid, Result* error);
QVector<Uuid> subtree(const model::Project& project, const Uuid& uuid);
int depthOf(const model::Project& project, const Uuid& uuid);
QStringList drivenTracks(SolverEffect effect);
int solverEffectiveEnd(const model::Entity& entity, const model::SolverSegment& segment);
QStringList allowedTracks(const model::Project& project,
                          const assets::AssetStore& assetStore,
                          const model::Entity& entity);
int defaultRow(const model::Project& project, const Uuid& parentUuid, int start, int end);
bool isFiniteNumber(double value);

struct TrimSummary
{
    int removedKeyframes = 0;
    QStringList removedSolvers;
    QStringList deletedUuids;
};

// Trims [newStart, newEnd), cascading to descendants. Callers that must not
// silently delete children inspect summary.deletedUuids and fail the call.
Result trimEntityRange(Tx& tx, const Uuid& uuid, int newStart, int newEnd,
                       TrimSummary* summary);

// Easing helpers accept both the lowercase presets from docs/MCP_TOOLS.md and
// the canonical camelCase names used by model::EasingSpec.
bool parseEasing(const QJsonValue& value, model::EasingSpec* out,
                 QJsonValue* normalized, Result* error);
QJsonValue easingJson(const model::EasingSpec& easing);

// Tool handlers. Each one operates on the transaction copy and leaves the
// commit to Tx::finish().
Result projectGet(Tx& tx, const QJsonObject& args);
Result projectSummary(Tx& tx, const QJsonObject& args);
Result projectUpdate(Tx& tx, const QJsonObject& args);

Result entityCreate(Tx& tx, const QJsonObject& args);
Result entityCreateTree(Tx& tx, const QJsonObject& args);
Result entityCreateMany(Tx& tx, const QJsonObject& args);
Result entityGet(Tx& tx, const QJsonObject& args);
Result entityList(Tx& tx, const QJsonObject& args);
Result entityUpdate(Tx& tx, const QJsonObject& args);
Result entityUpdateMany(Tx& tx, const QJsonObject& args);
Result entityMove(Tx& tx, const QJsonObject& args);
Result entityTrim(Tx& tx, const QJsonObject& args);
Result entityReparent(Tx& tx, const QJsonObject& args);
Result entityDuplicate(Tx& tx, const QJsonObject& args);
Result entityDelete(Tx& tx, const QJsonObject& args);
Result entityDeleteMany(Tx& tx, const QJsonObject& args);

Result keyframesGet(Tx& tx, const QJsonObject& args);
Result keyframesSet(Tx& tx, const QJsonObject& args);
Result keyframesSetMany(Tx& tx, const QJsonObject& args);
Result keyframesRemove(Tx& tx, const QJsonObject& args);
Result keyframesMove(Tx& tx, const QJsonObject& args);
Result keyframesClear(Tx& tx, const QJsonObject& args);
Result keyframesSetEasing(Tx& tx, const QJsonObject& args);

Result solverCreate(Tx& tx, const QJsonObject& args);
Result solverUpdate(Tx& tx, const QJsonObject& args);
Result solverDelete(Tx& tx, const QJsonObject& args);
Result solverList(Tx& tx, const QJsonObject& args);
Result solverEvaluate(Tx& tx, const QJsonObject& args);

Result previewRenderFrame(Tx& tx, const QJsonObject& args);
Result viewStateGet(Tx& tx, const QJsonObject& args);

// Parses one `solvers[]` element (fields same as solver.create without
// entityUuid) against an entity that may not be stored in the project yet.
Result parseSolverCreate(const model::Project& project,
                         const model::Entity& entity,
                         const QJsonObject& object,
                         model::SolverSegment* segment);

Result assetImport(Tx& tx, const QJsonObject& args);
Result assetList(Tx& tx, const QJsonObject& args);
Result assetGet(Tx& tx, const QJsonObject& args);
Result assetAssign(Tx& tx, const QJsonObject& args);
Result assetUnassign(Tx& tx, const QJsonObject& args);
Result assetRename(Tx& tx, const QJsonObject& args);
Result assetSetTags(Tx& tx, const QJsonObject& args);
Result assetDelete(Tx& tx, const QJsonObject& args);

Result markersList(Tx& tx, const QJsonObject& args);
Result markersSet(Tx& tx, const QJsonObject& args);
Result markersRemove(Tx& tx, const QJsonObject& args);

Result historyUndo(Tx& tx, const QJsonObject& args);
Result historyRedo(Tx& tx, const QJsonObject& args);
Result historyLog(Tx& tx, const QJsonObject& args);

Result schemaGet(Tx& tx, const QJsonObject& args);
Result guideGet(Tx& tx, const QJsonObject& args);

// MCP tools/list payload builder.
QJsonObject listTools();

} // namespace pvf::mcp2::detail
