#pragma once

// Read-only display DTOs for the timeline, built by TimelineAdapter from
// model::Project.
//
// These value types used to live in the retired single-file project model
// together with the `.pvproj` loader.  The delivery shell never writes them back:
// every edit goes through EditorSession/EditActions, so the painters only need
// the small read-only view of an entity.

#include "model/Easing.h"
#include "core/EntityId.h"
#include "media/AudioClip.h"

#include <QColor>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace pvf {

enum class EntityType
{
    Image,
    Video,
    Text,
    Vector,
    Camera,
    Audio,
    Folder,
    HtmlLayer      // one HTML component captured by the CDP browser
};

// One whole-frame keyframe resolved to an absolute project frame.  The painter
// draws exactly one diamond per entry; there are no per-property tracks.
struct Keyframe
{
    int frame = 0;
    model::EasingSpec ease;
    QString note;      // optional author note, never rendered
    // This frame is a solver segment start / structural end.  A key can be
    // both when the segment has zero length.
    bool solverStart = false;
    bool solverEnd = false;
};

struct Entity
{
    EntityId id = kInvalidEntity;
    EntityType type = EntityType::Image;
    EntityId parent = kInvalidEntity;   // coordinate system / layer parent
    bool hidden = false;
    bool locked = false;
    QString name;
    // Type-specific static data plus the clip range the painter reads.
    QVariantMap data;
    QVector<Keyframe> keyframes;
    std::shared_ptr<AudioClip> audio;   // decoded peaks for the waveform clip

    bool isCamera() const { return type == EntityType::Camera; }
    bool isAudio() const { return type == EntityType::Audio; }
    bool isFolder() const { return type == EntityType::Folder; }
};

enum class EntityFlag { Hidden, Locked };

struct TimelineMarkerView
{
    QString id;
    int frame = 0;
    QString title;
    QString note;
    QColor color = QColor(255, 190, 60);
};

struct TimelineSnapshot
{
    QVector<Entity> entities;   // parent before children
    QVector<TimelineMarkerView> markers;
    int fps = 30;
    int playhead = 0;
    int contentEnd = 0;         // furthest root entity end
};

} // namespace pvf
