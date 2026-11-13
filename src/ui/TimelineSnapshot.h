#pragma once

// Read-only display DTOs for the timeline, built by TimelineAdapter from
// model::Project.
//
// These value types used to live in the retired single-file project model
// together with the `.pvproj` loader.  The delivery shell never writes them back:
// every edit goes through EditorSession/EditActions, so the painters only need
// the small read-only view of an entity.

#include "core/Easing.h"
#include "core/EntityId.h"
#include "media/AudioClip.h"

#include <QColor>
#include <QHash>
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
    ComicText,     // comic balloon with a shaped border and a tail
    RadialLines,   // hand drawn rays bursting from a point
    FlowLines,     // parallel streaks flowing across the scene
    BrandWall,     // checkerboard tile wall with brand logos
    HalftoneDots,  // manga screentone with a fading dot grid
    HtmlLayer      // one HTML component captured by the CDP browser
};

struct Keyframe
{
    double frame = 0.0;
    double value = 0.0;
    EasingSpec ease;
    QString note;      // optional author note, never rendered
};

// One animated property of an entity, resolved to absolute project frames.
struct Track
{
    QVector<Keyframe> keys;

    bool isEmpty() const { return keys.isEmpty(); }
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
    QHash<QString, Track> tracks;
    std::shared_ptr<AudioClip> audio;   // decoded peaks for the waveform clip

    bool isCamera() const { return type == EntityType::Camera; }
    bool isAudio() const { return type == EntityType::Audio; }
    bool isFolder() const { return type == EntityType::Folder; }
};

// A removed keyframe, kept so it can be restored by undo.
struct KeyframeRecord
{
    EntityId entity = kInvalidEntity;
    QString property;
    double frame = 0.0;
    double value = 0.0;
    EasingSpec ease;
    QString note;
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
