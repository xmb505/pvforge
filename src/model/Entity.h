#pragma once

#include "Content.h"
#include "JsonUtil.h"
#include "Keyframe.h"
#include "common/Types.h"

#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pvf::model {

// Where an entity's transform lives. Parent space follows the existing
// behaviour (position/rotation/scale are relative to the parent); Canvas space
// is relative to the canvas and does not inherit the parent transform. The
// timeline range is still constrained by the parent in both spaces.
enum class EntitySpace
{
    Parent,
    Canvas,
};

QString entitySpaceName(EntitySpace space);
std::optional<EntitySpace> entitySpaceFromName(const QString& name);

struct SolverSegment
{
    Uuid id;
    SolverEffect effect = SolverEffect::Gravity;
    int startFrame = 0;
    // -1 = automatic: stop at the first physical stop/collision, or at the
    // last clip frame when nothing stops the motion.  >= 0 = explicit advanced
    // override: solve (and freeze) through that frame.
    int endFrame = -1;
    int version = 1;
    QPointF initialVelocity;
    QVariantMap params;
    QJsonObject extras;
};

struct Entity
{
    Uuid uuid;
    QString name;
    Uuid parentUuid; // empty = root
    // True only on a child: the entity range must equal its direct parent's
    // range. The parent itself is never followed through.
    bool followParentRange = false;
    EntitySpace space = EntitySpace::Parent;
    int start = 0;
    int end = 1;
    int row = 0;
    bool hidden = false;
    bool locked = false;
    bool inheritVisibility = true;
    bool visible = true;
    bool physicsInteract = false;
    Transform transform;
    Content content;
    // Whole-frame keyframes, relative to start, strictly ascending by frame.
    // Serialized as the "keyframes" array.
    QVector<Keyframe> keyframes;
    QVector<SolverSegment> solvers;
    // Unknown keys, preserved verbatim so fields written by a newer version or
    // an external tool survive a load/save cycle.
    QJsonObject extras;

    // Closed range [start, end]: both boundary frames are rendered, so a
    // single-frame entity (start == end) has duration 1.
    int duration() const { return end - start + 1; }
    bool isRoot() const { return parentUuid.isEmpty(); }
};

// One root-subtree document: entities/<root-uuid>.json. Children are stored
// recursively, but every child keeps its parentUuid so the nesting and the
// flat model can be cross-checked.
struct EntityTreeNode
{
    Entity entity;
    QVector<EntityTreeNode> children;
};

// Whole-frame sample at an entity-relative frame.  Empty keyframes fall back to
// the static transform / visible / physicsInteract / content values.  `content`
// holds the content animation defaults, so callers can apply it directly.
KeyframeSample entitySampleAt(const Entity& entity, double relativeFrame);

// Static whole-frame values (no keyframes): the fallback used by
// entitySampleAt() and the starting point for creating a new keyframe.
KeyframeSample staticEntitySample(const Entity& entity);

// True when the entity-relative frame holds at least one whole-frame keyframe
// and no later keyframe exists.  This is the keyframe that may become a solver
// start: the first key created after it (frame N) becomes the end marker and
// the solver structurally ends at N-1.
bool isLastKeyframeInClip(const Entity& entity, int relativeFrame);

// Inclusive structural end of a segment.  For endFrame == -1 this is the
// earliest of: one frame before the next whole-frame keyframe, one frame
// before the next solver segment, and the clip's last frame.  The physical
// stop/collision frame is a World-dependent concern and is applied on top by
// anim; this helper stays model-only.  An explicit endFrame is returned
// verbatim (clamped to the clip).
int solverStructuralEnd(const Entity& entity, const SolverSegment& segment);

// Solver start/end frames must be inside the clip, segments must not overlap,
// and no ordinary whole-frame keyframe may sit inside an explicitly ended
// segment (startFrame < frame <= endFrame).  A keyframe at the segment start
// frame is the initial state and is allowed.  For endFrame == -1 the structural
// end is derived from the next keyframe / next segment / clip end, so a newly
// created key automatically becomes the end marker instead of an invalid key
// inside the segment.
bool validateSolvers(const Entity& entity, QString* error = nullptr);

bool validateEntity(const Entity& entity, QString* error = nullptr);

QJsonObject toJson(const SolverSegment& segment);
std::optional<SolverSegment> solverFromJson(const QJsonObject& object,
                                            const ParseOptions& options,
                                            QString* error = nullptr);

QJsonObject toJson(const Entity& entity);
// Entity documents carry their own "format" ("pvforge-entity") and "version"
// (kProjectFormatVersion). A document from an older flat layout or a newer
// build is refused by ProjectStore instead of being half-parsed. Use
// entityTreeFromJson() for a root-subtree file; this parser is for one entity
// object and rejects non-empty "children" so nesting cannot be dropped.
std::optional<Entity> entityFromJson(const QJsonObject& object,
                                     const ParseOptions& options,
                                     QString* error = nullptr);

// Root-subtree document helpers. toJson() always writes a "children" array
// (possibly empty); entityTreeFromJson() accepts a missing array as an empty
// leaf for v3 documents written before the field existed in some fixtures.
QJsonObject toJson(const EntityTreeNode& node);
std::optional<EntityTreeNode> entityTreeFromJson(const QJsonObject& object,
                                                 const ParseOptions& options,
                                                 QString* error = nullptr);

// Flattens one tree into `entities` and checks:
// - the root parentUuid is empty; every child parentUuid equals its enclosing
//   node
// - UUIDs are unique within the tree
// - every entity passes validateEntity()
// - a child range is inside its direct parent's range
// - followParentRange=true means the range equals the direct parent's range
// Errors name the JSON-like path ($ / $.children[i]) and the entity UUID.
bool flattenEntityTree(const EntityTreeNode& root,
                       QHash<Uuid, Entity>* entities,
                       QString* error = nullptr);

} // namespace pvf::model
