#pragma once

#include "Content.h"
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
};

struct Entity
{
    Uuid uuid;
    QString name;
    Uuid parentUuid; // empty = root
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
    QHash<QString, Track> tracks; // serialized as "keyframes"
    QVector<SolverSegment> solvers;

    int duration() const { return end - start; }
    bool isRoot() const { return parentUuid.isEmpty(); }
};

// Keyframes must stay in 0 .. duration-1.
bool validateTrack(const Track& track, int duration, const QString& trackName,
                   QString* error = nullptr);

// True when the entity-relative frame holds at least one keyframe and no
// track has a later key.  This is the keyframe that may become a solver start:
// the first key created after it (frame N) becomes the end marker and the
// solver structurally ends at N-1.
bool isLastKeyframeInClip(const Entity& entity, int relativeFrame);

// Inclusive structural end of a segment.  For endFrame == -1 this is the
// earliest of: one frame before the next keyframe on any track, one frame
// before the next solver segment, and the clip's last frame.  The physical
// stop/collision frame is a World-dependent concern and is applied on top by
// anim; this helper stays model-only.  An explicit endFrame is returned
// verbatim (clamped to the clip).
int solverStructuralEnd(const Entity& entity, const SolverSegment& segment);

// Solver start/end frames must be inside the clip, segments must not overlap,
// and driven properties (x/y or rotation) must not have normal keyframes
// inside an explicitly ended segment. A keyframe at the segment start frame
// is the initial state and is allowed.  For endFrame == -1 the structural end
// is derived from the next keyframe / next segment / clip end, so a newly
// created key automatically becomes the end marker instead of an invalid
// key inside the segment.
bool validateSolvers(const Entity& entity, QString* error = nullptr);

bool validateEntity(const Entity& entity, QString* error = nullptr);

QJsonObject toJson(const SolverSegment& segment);
std::optional<SolverSegment> solverFromJson(const QJsonObject& object,
                                            QString* error = nullptr);

QJsonObject toJson(const Entity& entity);
std::optional<Entity> entityFromJson(const QJsonObject& object,
                                     QString* error = nullptr);

} // namespace pvf::model
