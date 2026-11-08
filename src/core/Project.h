#pragma once

#include "core/Easing.h"
#include "core/BrandWall.h"
#include "core/TextStyle.h"
#include "media/AudioClip.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <memory>

class QJsonObject;

namespace pvf {

using EntityId = int;
constexpr EntityId kInvalidEntity = -1;
constexpr int kWorkCameraView = -2;
constexpr int kWholeCanvasView = -3;

// Current on-disk project format. The loader also accepts 1..kProjectVersion
// so older projects open and get migrated with a default scene.
constexpr int kProjectVersion = 4;

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
    HalftoneDots   // manga screentone with a fading dot grid
};

struct Keyframe
{
    double frame = 0.0;
    double value = 0.0;
    EasingSpec ease;
    QString note;      // optional author note, never rendered
};

// One animated property of an entity. Values are scalars in v1.
struct Track
{
    QVector<Keyframe> keys;

    double evaluate(double frame, double fallback = 0.0) const;
    void setKey(double frame, double value);
    void removeKey(double frame);
    bool isEmpty() const { return keys.isEmpty(); }
};

struct Entity
{
    EntityId id = kInvalidEntity;
    EntityType type = EntityType::Image;
    EntityId parent = kInvalidEntity;   // coordinate system / layer parent
    QString templateId;        // template this instance was created from
    bool hidden = false;
    bool locked = false;
    QString name;
    QString sourcePath;        // image/video file on disk
    QImage image;              // image or decoded video frame, runtime cache
    QVector<QImage> wallLogos; // brand wall tile logos, runtime cache
    mutable QImage thumbnail;  // downscaled cache for draft previews
    mutable QPixmap pixmap;    // painter cache, built on first draw
    mutable QPixmap halftonePixmap;  // rasterised halftone pattern cache
    mutable QVariantMap halftoneData; // halftone params used for that cache
    std::shared_ptr<AudioClip> audio;   // decoded PCM/waveform, runtime cache
    QVariantMap data;          // type-specific static data (text, font, shape, ...)
    QHash<QString, Track> tracks;
    QHash<QString, double> values;

    double staticValue(const QString& prop) const;

    bool isCamera() const { return type == EntityType::Camera; }
    bool isAudio() const { return type == EntityType::Audio; }
    bool isFolder() const { return type == EntityType::Folder; }
    QString textContent() const { return data.value(QStringLiteral("text")).toString(); }
    bool rotPointCustom() const
    {
        return data.value(QStringLiteral("rotPointCustom"), false).toBool();
    }
    QString fontFamily() const;
    int fontSize() const { return data.value(QStringLiteral("size"), 96).toInt(); }
    QColor textColor() const;
    int textAlign() const { return data.value(QStringLiteral("align"), int(Qt::AlignCenter)).toInt(); }

    int vectorShape() const { return data.value(QStringLiteral("shape"), 0).toInt(); }
    double cornerRadius() const
    {
        return data.value(QStringLiteral("cornerRadius"), 40.0).toDouble();
    }
    int vectorSides() const { return data.value(QStringLiteral("sides"), 5).toInt(); }
    double vectorInnerRatio() const
    {
        return data.value(QStringLiteral("innerRatio"), 0.45).toDouble();
    }
    double vectorWidth() const { return data.value(QStringLiteral("width"), 400.0).toDouble(); }
    double vectorHeight() const { return data.value(QStringLiteral("height"), 300.0).toDouble(); }
    QColor fillColor() const;
    QColor strokeColor() const;
    double strokeWidth() const { return data.value(QStringLiteral("strokeWidth"), 0.0).toDouble(); }
};

// A reusable template captured from the workbench, matching Mine-imator's
// library entries: media path or text/vector parameters, plus a preview.
struct Template
{
    QString id;
    EntityType type = EntityType::Image;
    QString name;
    QString path;
    double duration = 0.0;   // seconds, video only
    QVariantMap data;        // text/vector parameters
    QImage thumbnail;        // runtime cache
    std::shared_ptr<AudioClip> audio;   // audio template, or a video's extracted track
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

// A named frame range that owns a root folder. Scenes never overlap but may
// leave gaps; startFrame/endFrame are inclusive. The folder is a normal
// EntityType::Folder entity, so its children keep the same coordinate system
// when the folder transform is identity.
struct Scene
{
    QString id;
    QString name;
    int startFrame = 0;
    int endFrame = 0;
    EntityId folderId = kInvalidEntity;
    QColor color{90, 170, 255};
    QString note;
};

// A global timeline marker. Annotations are authoring metadata only: they do
// not render and do not extend timelineLength().
struct Annotation
{
    QString id;
    int frame = 0;
    QString title;
    QString note;      // Markdown
    QStringList tags;
    QColor color{255, 190, 60};
};

enum class EntityFlag { Hidden, Locked };

struct MediaImportResult;

class Project : public QObject
{
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);

    QString name = QStringLiteral("未命名项目");
    bool modified = false;               // unsaved changes
    bool packAssets = false;             // copy imported media into the project folder
    QString author;
    QString description;
    QString filePath;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    bool loop = true;
    QColor background{0x10, 0x10, 0x14};
    bool autoKey = true;

    // Camera view state. Scene cameras are normal entities; the work camera
    // is the live main viewport and the whole-canvas view is a fixed fallback.
    EntityId activeCamera = kInvalidEntity;
    int monitorView = kWorkCameraView; // work camera, whole canvas, or a camera id

    // Length follows content: last keyframe + one second of tail, minimum 2s.
    int timelineLength() const;

    static double defaultValue(const QString& prop);

    Entity* addImage(const QString& path, const QString& displayName = QString());
    Entity* addVideo(const QString& path, const QImage& firstFrame, double duration = 0.0,
                     const QString& displayName = QString());
    // New entities stay at the root. Scene placement is opt-in for now: ask
    // sceneFolderAt(frame) and pass the returned id to addFolder/setEntityParent
    // (or a future UI/MCP layer) when the caller wants scene ownership.
    Entity* addEntity(EntityType type, const QString& name,
                       const QVariantMap& data);
    Entity* addBrandWall(const BrandWallParams& params);
    void refreshBrandWallImages(Entity& entity);
    Entity* addText(const TextStyle& style,
                     const QString& name = QStringLiteral("文字"));
    Entity* addText(const QString& text, const QString& family, int pixelSize,
                    const QColor& color, int align);
    Entity* addVector(int shape, double width, double height, const QColor& fill,
                      const QColor& stroke, double strokeWidth);
    Entity* addCamera(const QString& name = QString());
    Entity* addAudio(const QString& path, const std::shared_ptr<AudioClip>& audio,
                     const QString& displayName = QString());
    Entity* addFolder(const QString& name = QString(),
                      EntityId parent = kInvalidEntity);

    bool setEntityParent(EntityId id, EntityId parent);
    bool moveEntity(EntityId id, EntityId parent, int index);
    // Moves several entities into parent as one block. index is the target
    // position in the list with the moved entities already removed.
    bool moveEntities(const QVector<EntityId>& ids, EntityId parent, int index);
    bool isDescendantOf(EntityId id, EntityId ancestor) const;
    bool hasChildren(EntityId id) const;
    bool isEffectivelyHidden(EntityId id) const;
    bool isEffectivelyLocked(EntityId id) const;
    // Opacity multiplied down the parent chain at the given frame.
    double effectiveOpacity(EntityId id, double frame) const;
    // Pre-order layer order: roots in vector order, children after their parent.
    QVector<int> entityOrder() const;

    Entity* entity(EntityId id);
    const Entity* entity(EntityId id) const;
    int removeEntity(EntityId id);
    void insertEntity(const Entity& entity, int index);
    void setEntityFlag(EntityId id, EntityFlag flag, bool value);
    void setEntityName(EntityId id, const QString& name);
    void setEntityData(EntityId id, const QString& key, const QVariant& value);
    void removeKeyframe(EntityId id, const QString& prop, double frame);
    // Swaps the media file behind an image/video/audio entity, keeping its
    // animation. Returns every entity that changed (video plus paired audio).
    QVector<EntityId> replaceEntitySource(EntityId id, const MediaImportResult& result,
                                          QString* error);
    void replaceEntity(const Entity& entity);
    void setEntities(const QVector<Entity>& entities);
    void setEntityDataMap(EntityId id, const QVariantMap& data);
    QVector<KeyframeRecord> removeKeyframesAt(EntityId id, double frame);
    void addKeyframe(EntityId id, const QString& property, double frame, double value);
    void addKeyframe(EntityId id, const QString& property, const Keyframe& key);
    // Shifts the given keyframes by delta frames, clamping at frame 0.
    void moveKeyframes(const QVector<KeyframeRecord>& records, double delta);
    bool setKeyframeEasing(EntityId id, const QString& property, double frame,
                           const EasingSpec& ease);
    const Keyframe* keyframe(EntityId id, const QString& property, double frame) const;
    double previousKeyframeFrame(EntityId id, double frame) const;
    const QVector<Entity>& entities() const { return m_entities; }
    // Field mutation only; structural edits must go through the Project
    // methods so the id index stays valid.
    QVector<Entity>& mutableEntities() { return m_entities; }
    EntityId nextId() const { return m_nextId; }

    const QVector<Template>& templates() const { return m_templates; }
    Template* templateAt(int index);
    int templateCount() const { return int(m_templates.size()); }
    Template* addTemplate(const Entity& entity);
    Template* addMediaTemplate(const QString& path, EntityType type, const QImage& thumbnail,
                               double duration = 0.0,
                               const QString& displayName = QString(),
                               const std::shared_ptr<AudioClip>& audio = nullptr);
    // Adds the imported asset. A video with an audio track also becomes an
    // audio template that shares the same source file.
    bool addImportedMedia(const MediaImportResult& result);
    QString importMediaFile(const QString& path, QString* error = nullptr);
    Entity* createFromTemplate(const Template& tmpl);
    void removeTemplate(int index);

    // ---- Scenes ---------------------------------------------------------
    // A scene owns one root folder. addScene creates that Folder entity with
    // an identity transform (x/y/rotation/rotPoint = 0, scale/opacity = 1) so
    // wrapping existing entities cannot move them on screen.
    //
    // Ranges are inclusive and must satisfy 0 <= startFrame <= endFrame <=
    // timelineLength(); overlapping scenes or out-of-range frames produce an
    // error instead of being clamped. Gaps between scenes are allowed.
    const QVector<Scene>& scenes() const { return m_scenes; }
    Scene* addScene(const QString& name, int startFrame, int endFrame,
                    const QColor& color = QColor(), const QString& note = QString(),
                    QString* error = nullptr);
    // Updates name/range/color/note. The folder id is fixed for the life of
    // the scene, so the scene's entity ownership cannot become inconsistent.
    bool updateScene(const QString& id, const QString& name, int startFrame,
                     int endFrame, const QColor& color, const QString& note,
                     QString* error = nullptr);
    // Removes only the scene record. The folder entity and every entity inside
    // it stay in the project as ordinary folders; nothing is deleted.
    bool removeScene(const QString& id);
    // Returns the scene whose inclusive range contains frame, or nullptr in a
    // gap / outside every scene.
    const Scene* sceneAt(int frame) const;
    const Scene* sceneById(const QString& id) const;
    // Convenience for future UI/MCP callers: the folder a new entity would
    // belong to at this frame. addEntity does not reparent automatically yet.
    EntityId sceneFolderAt(int frame) const;

    // ---- Annotations ----------------------------------------------------
    // Global markers, always kept sorted by frame. Adding or updating a marker
    // does not touch timelineLength() or any entity.
    const QVector<Annotation>& annotations() const { return m_annotations; }
    Annotation* addAnnotation(int frame, const QString& title,
                              const QString& note = QString(),
                              const QStringList& tags = QStringList(),
                              const QColor& color = QColor(),
                              QString* error = nullptr);
    bool updateAnnotation(const Annotation& annotation, QString* error = nullptr);
    bool removeAnnotation(const QString& id);
    const Annotation* annotationById(const QString& id) const;

    // Optional author note on an existing keyframe. setKeyframeNote returns
    // false when the keyframe does not exist; an empty note clears it.
    bool setKeyframeNote(EntityId id, const QString& property, double frame,
                         const QString& note);
    QString keyframeNote(EntityId id, const QString& property, double frame) const;

    double value(EntityId id, const QString& prop, double frame) const;
    void setValue(EntityId id, const QString& prop, double frame, double value, bool keyframe);

    // Asset packaging, following Blender's pack resources workflow.
    QString assetDirectory() const;
    QString resolveAssetPath(const QString& stored) const;
    QString storeAssetPath(const QString& absolute) const;
    QString copyIntoProject(const QString& sourcePath) const;
    QString importAsset(const QString& sourcePath) const;

    bool save(const QString& path, QString* error = nullptr);
    bool load(const QString& path, QString* error = nullptr);

    void notifyChanged() { emit changed(); }

signals:
    void changed();

private:
    Entity makeBase(EntityType type, const QString& name) const;
    Entity* append(Entity e);
    bool validateSceneRange(int startFrame, int endFrame, const QString& ignoreId,
                            QString* error) const;
    QJsonObject toJson() const;
    void rebuildEntityIndex();

    QVector<Entity> m_entities;
    QHash<EntityId, int> m_entityIndex;
    QVector<Template> m_templates;
    QVector<Scene> m_scenes;
    QVector<Annotation> m_annotations;
    EntityId m_nextId = 1;
};

} // namespace pvf
