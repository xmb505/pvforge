#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

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
    QString name;
    QString sourcePath;        // image/video file on disk
    QImage image;              // image or decoded video frame, runtime cache
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
    Entity* addVideo(const QString& path, const QImage& firstFrame);
    Entity* addText(const QString& text, const QString& family, int pixelSize,
                    const QColor& color, int align);
    Entity* addVector(int shape, double width, double height, const QColor& fill,
                      const QColor& stroke, double strokeWidth);

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
    Entity makeBase(EntityType type, const QString& name) const;
    Entity* append(Entity e);

    QVector<Entity> m_entities;
    EntityId m_nextId = 1;
};

} // namespace pvf
