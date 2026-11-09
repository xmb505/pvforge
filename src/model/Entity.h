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
    int endFrame = -1; // -1 = solve until the last frame of the clip
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
// state and is allowed.
bool validateSolvers(const Entity& entity, QString* error = nullptr);

bool validateEntity(const Entity& entity, QString* error = nullptr);

QJsonObject toJson(const SolverSegment& segment);
std::optional<SolverSegment> solverFromJson(const QJsonObject& object,
                                            QString* error = nullptr);

QJsonObject toJson(const Entity& entity);
std::optional<Entity> entityFromJson(const QJsonObject& object,
                                     QString* error = nullptr);

} // namespace pvf::model
