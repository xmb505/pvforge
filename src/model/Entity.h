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

// Solver start/end frames must be inside the clip, segments must not overlap,
// and driven properties (x/y or rotation) must not have normal keyframes
// inside a segment. A keyframe at the segment start frame is the initial
// state and is allowed.  endFrame == -1 is automatic: validation uses the
// clip end as its structural end because the physical stop frame needs a
// World and cannot be known here.  An explicit endFrame is validated and used
// verbatim.
bool validateSolvers(const Entity& entity, QString* error = nullptr);

bool validateEntity(const Entity& entity, QString* error = nullptr);

QJsonObject toJson(const SolverSegment& segment);
std::optional<SolverSegment> solverFromJson(const QJsonObject& object,
                                            QString* error = nullptr);

QJsonObject toJson(const Entity& entity);
std::optional<Entity> entityFromJson(const QJsonObject& object,
                                     QString* error = nullptr);

} // namespace pvf::model
