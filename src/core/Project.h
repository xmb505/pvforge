#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QJsonObject;

namespace pvf {

using EntityId = int;
constexpr EntityId kInvalidEntity = -1;

enum class EntityType { Image, Video, Text, Vector };

struct Keyframe
{
    double frame = 0.0;
    double value = 0.0;
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
    QString templateId;        // template this instance was created from
    bool hidden = false;
    bool locked = false;
    QString name;
    QString sourcePath;        // image/video file on disk
    QImage image;              // image or decoded video frame, runtime cache
    mutable QPixmap pixmap;    // painter cache, built on first draw
    QVariantMap data;          // type-specific static data (text, font, shape, ...)
    QHash<QString, Track> tracks;
    QHash<QString, double> values;

    double staticValue(const QString& prop) const;

    QString textContent() const { return data.value(QStringLiteral("text")).toString(); }
    QString fontFamily() const;
    int fontSize() const { return data.value(QStringLiteral("size"), 96).toInt(); }
    QColor textColor() const;
    int textAlign() const { return data.value(QStringLiteral("align"), int(Qt::AlignCenter)).toInt(); }

    int vectorShape() const { return data.value(QStringLiteral("shape"), 0).toInt(); }
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
};

// A removed keyframe, kept so it can be restored by undo.
struct KeyframeRecord
{
    EntityId entity = kInvalidEntity;
    QString property;
    double frame = 0.0;
    double value = 0.0;
};

enum class EntityFlag { Hidden, Locked };

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

    // Length follows content: last keyframe + one second of tail, minimum 2s.
    int timelineLength() const;

    static double defaultValue(const QString& prop);

    Entity* addImage(const QString& path);
    Entity* addVideo(const QString& path, const QImage& firstFrame, double duration = 0.0);
    Entity* addText(const QString& text, const QString& family, int pixelSize,
                    const QColor& color, int align);
    Entity* addVector(int shape, double width, double height, const QColor& fill,
                      const QColor& stroke, double strokeWidth);

    Entity* entity(EntityId id);
    const Entity* entity(EntityId id) const;
    int removeEntity(EntityId id);
    void insertEntity(const Entity& entity, int index);
    void setEntityFlag(EntityId id, EntityFlag flag, bool value);
    void setEntityName(EntityId id, const QString& name);
    QVector<KeyframeRecord> removeKeyframesAt(EntityId id, double frame);
    void addKeyframe(EntityId id, const QString& property, double frame, double value);
    const QVector<Entity>& entities() const { return m_entities; }
    QVector<Entity>& mutableEntities() { return m_entities; }
    EntityId nextId() const { return m_nextId; }

    const QVector<Template>& templates() const { return m_templates; }
    Template* templateAt(int index);
    int templateCount() const { return int(m_templates.size()); }
    Template* addTemplate(const Entity& entity);
    Entity* createFromTemplate(const Template& tmpl);
    void removeTemplate(int index);

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
    QJsonObject toJson() const;

    QVector<Entity> m_entities;
    QVector<Template> m_templates;
    EntityId m_nextId = 1;
};

} // namespace pvf
