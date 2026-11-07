#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

namespace pvf {

using EntityId = int;
constexpr EntityId kInvalidEntity = -1;

enum class EntityType { Image, Group };

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
    QString name;
    QString sourcePath;        // image file on disk
    QImage image;              // runtime cache, never serialized
    QHash<QString, Track> tracks;
    QHash<QString, double> values;

    double staticValue(const QString& prop) const;
};

class Project : public QObject
{
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);

    int width = 1920;
    int height = 1080;
    int fps = 30;
    int duration = 300;                 // frames
    bool loop = true;
    QColor background{0x10, 0x10, 0x14};
    bool autoKey = true;

    static double defaultValue(const QString& prop);

    Entity* addImage(const QString& path);
    Entity* entity(EntityId id);
    const Entity* entity(EntityId id) const;
    const QVector<Entity>& entities() const { return m_entities; }
    EntityId nextId() const { return m_nextId; }

    double value(EntityId id, const QString& prop, double frame) const;
    void setValue(EntityId id, const QString& prop, double frame, double value, bool keyframe);

    void notifyChanged() { emit changed(); }

signals:
    void changed();

private:
    QVector<Entity> m_entities;
    EntityId m_nextId = 1;
};

} // namespace pvf
