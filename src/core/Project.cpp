#include "Project.h"

#include "media/VideoDecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QString typeToString(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("image");
    case EntityType::Video:
        return QStringLiteral("video");
    case EntityType::Text:
        return QStringLiteral("text");
    case EntityType::Vector:
        return QStringLiteral("vector");
    }
    return QStringLiteral("image");
}

EntityType typeFromString(const QString& text)
{
    if (text == QLatin1String("video"))
        return EntityType::Video;
    if (text == QLatin1String("text"))
        return EntityType::Text;
    if (text == QLatin1String("vector"))
        return EntityType::Vector;
    return EntityType::Image;
}

bool isColorKey(const QString& key)
{
    return key == QLatin1String("color") || key == QLatin1String("fill")
           || key == QLatin1String("stroke");
}

} // namespace

int Project::timelineLength() const
{
    // No fixed project duration, matching Mine-imator: length is the longest
    // media clip and the last keyframe. Zero means the timeline is open-ended.
    double last = 0.0;
    for (const Entity& e : m_entities)
    {
        const double media = e.data.value(QStringLiteral("duration"), 0.0).toDouble();
        if (media > 0.0)
            last = std::max(last, media * fps);
        for (auto it = e.tracks.constBegin(); it != e.tracks.constEnd(); ++it)
            if (!it->keys.isEmpty())
                last = std::max(last, it->keys.last().frame);
    }
    return std::max(0, int(std::ceil(last)));
}

double Track::evaluate(double frame, double fallback) const
{
    if (keys.isEmpty())
        return fallback;
    if (frame <= keys.first().frame)
        return keys.first().value;
    if (frame >= keys.last().frame)
        return keys.last().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (frame <= keys[i].frame)
        {
            const Keyframe& a = keys[i - 1];
            const Keyframe& b = keys[i];
            const double span = b.frame - a.frame;
            if (span <= 0.0)
                return b.value;
            const double t = (frame - a.frame) / span;
            return a.value + (b.value - a.value) * t;
        }
    }
    return keys.last().value;
}

void Track::setKey(double frame, double value)
{
    for (Keyframe& k : keys)
    {
        if (std::abs(k.frame - frame) < 1e-6)
        {
            k.value = value;
            return;
        }
    }
    keys.append({frame, value});
    std::sort(keys.begin(), keys.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
}

void Track::removeKey(double frame)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(),
                              [frame](const Keyframe& k) {
                                  return std::abs(k.frame - frame) < 1e-6;
                              }),
               keys.end());
}

double Entity::staticValue(const QString& prop) const
{
    return values.value(prop, Project::defaultValue(prop));
}

QString Entity::fontFamily() const
{
    const QString family = data.value(QStringLiteral("font")).toString();
    return family.isEmpty() ? QFont().family() : family;
}

QColor Entity::textColor() const
{
    const QColor color = data.value(QStringLiteral("color")).value<QColor>();
    return color.isValid() ? color : QColor(Qt::white);
}

QColor Entity::fillColor() const
{
    const QColor color = data.value(QStringLiteral("fill")).value<QColor>();
    return color.isValid() ? color : QColor(90, 170, 255);
}

QColor Entity::strokeColor() const
{
    const QColor color = data.value(QStringLiteral("stroke")).value<QColor>();
    return color.isValid() ? color : QColor(Qt::white);
}

Project::Project(QObject* parent)
    : QObject(parent)
{
}

double Project::defaultValue(const QString& prop)
{
    if (prop == "scaleX" || prop == "scaleY" || prop == "opacity")
        return 1.0;
    return 0.0;
}

Entity Project::makeBase(EntityType type, const QString& name) const
{
    Entity e;
    e.id = m_nextId;
    e.type = type;
    e.name = name;
    e.values["x"] = 0.0;
    e.values["y"] = 0.0;
    e.values["rotation"] = 0.0;
    e.values["scaleX"] = 1.0;
    e.values["scaleY"] = 1.0;
    e.values["opacity"] = 1.0;
    return e;
}

Entity* Project::append(Entity e)
{
    ++m_nextId;
    m_entities.append(std::move(e));
    emit changed();
    return &m_entities.last();
}

Entity* Project::addImage(const QString& path)
{
    QImage img(path);
    if (img.isNull())
        return nullptr;

    Entity e = makeBase(EntityType::Image, QFileInfo(path).completeBaseName());
    e.sourcePath = path;
    e.image = img;

    // Fit oversized images to 60% of the shorter canvas edge.
    const double maxEdge = std::max(img.width(), img.height());
    const double target = std::min(width, height) * 0.6;
    const double fit = maxEdge > target ? target / maxEdge : 1.0;
    e.values["scaleX"] = fit;
    e.values["scaleY"] = fit;

    addTemplate(e);
    return append(std::move(e));
}

Entity* Project::addVideo(const QString& path, const QImage& firstFrame, double duration)
{
    Entity e = makeBase(EntityType::Video, QFileInfo(path).completeBaseName());
    e.sourcePath = path;
    e.image = firstFrame;
    e.data["duration"] = duration;

    if (!firstFrame.isNull())
    {
        const double maxEdge = std::max(firstFrame.width(), firstFrame.height());
        const double target = std::min(width, height) * 0.6;
        const double fit = maxEdge > target ? target / maxEdge : 1.0;
        e.values["scaleX"] = fit;
        e.values["scaleY"] = fit;
    }
    addTemplate(e);
    return append(std::move(e));
}

Entity* Project::addText(const QString& text, const QString& family, int pixelSize,
                         const QColor& color, int align)
{
    Entity e = makeBase(EntityType::Text, QStringLiteral("文字"));
    e.data["text"] = text;
    e.data["font"] = family;
    e.data["size"] = pixelSize;
    e.data["color"] = color;
    e.data["align"] = align;
    addTemplate(e);
    return append(std::move(e));
}

Entity* Project::addVector(int shape, double width, double height, const QColor& fill,
                           const QColor& stroke, double strokeWidth)
{
    const QString name = shape == 0 ? QStringLiteral("矩形")
                         : shape == 1 ? QStringLiteral("圆形")
                                      : QStringLiteral("直线");
    Entity e = makeBase(EntityType::Vector, name);
    e.data["shape"] = shape;
    e.data["width"] = width;
    e.data["height"] = height;
    e.data["fill"] = fill;
    e.data["stroke"] = stroke;
    e.data["strokeWidth"] = strokeWidth;
    addTemplate(e);
    return append(std::move(e));
}

Template* Project::addTemplate(const Entity& entity)
{
    Template tmpl;
    tmpl.type = entity.type;
    tmpl.name = entity.name;
    tmpl.path = entity.sourcePath;
    tmpl.duration = entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
    tmpl.thumbnail = entity.image;
    if (entity.type == EntityType::Text || entity.type == EntityType::Vector)
        tmpl.data = entity.data;

    for (Template& existing : m_templates)
    {
        if (existing.type == tmpl.type && existing.path == tmpl.path
            && existing.data == tmpl.data)
        {
            if (existing.thumbnail.isNull() && !tmpl.thumbnail.isNull())
                existing.thumbnail = tmpl.thumbnail;
            return &existing;
        }
    }

    m_templates.append(tmpl);
    return &m_templates.last();
}

Entity* Project::createFromTemplate(const Template& tmpl)
{
    switch (tmpl.type)
    {
    case EntityType::Image:
        return addImage(tmpl.path);
    case EntityType::Video:
        return addVideo(tmpl.path, tmpl.thumbnail, tmpl.duration);
    case EntityType::Text:
        return addText(tmpl.data.value(QStringLiteral("text")).toString(),
                       tmpl.data.value(QStringLiteral("font")).toString(),
                       tmpl.data.value(QStringLiteral("size"), 96).toInt(),
                       tmpl.data.value(QStringLiteral("color")).value<QColor>(),
                       tmpl.data.value(QStringLiteral("align"), int(Qt::AlignCenter)).toInt());
    case EntityType::Vector:
        return addVector(tmpl.data.value(QStringLiteral("shape"), 0).toInt(),
                         tmpl.data.value(QStringLiteral("width"), 400.0).toDouble(),
                         tmpl.data.value(QStringLiteral("height"), 300.0).toDouble(),
                         tmpl.data.value(QStringLiteral("fill")).value<QColor>(),
                         tmpl.data.value(QStringLiteral("stroke")).value<QColor>(),
                         tmpl.data.value(QStringLiteral("strokeWidth"), 0.0).toDouble());
    }
    return nullptr;
}

void Project::removeTemplate(int index)
{
    if (index < 0 || index >= m_templates.size())
        return;
    m_templates.removeAt(index);
    emit changed();
}

Entity* Project::entity(EntityId id)
{
    for (Entity& e : m_entities)
        if (e.id == id)
            return &e;
    return nullptr;
}

const Entity* Project::entity(EntityId id) const
{
    for (const Entity& e : m_entities)
        if (e.id == id)
            return &e;
    return nullptr;
}

double Project::value(EntityId id, const QString& prop, double frame) const
{
    const Entity* e = entity(id);
    if (!e)
        return defaultValue(prop);

    const auto it = e->tracks.constFind(prop);
    if (it != e->tracks.constEnd() && !it->isEmpty())
        return it->evaluate(frame, e->staticValue(prop));
    return e->staticValue(prop);
}

void Project::setValue(EntityId id, const QString& prop, double frame, double value,
                       bool keyframe)
{
    Entity* e = entity(id);
    if (!e)
        return;

    if (keyframe)
    {
        e->tracks[prop].setKey(frame, value);
    }
    else
    {
        auto it = e->tracks.find(prop);
        if (it != e->tracks.end() && !it->isEmpty())
            it->setKey(frame, value);
        else
            e->values[prop] = value;
    }
    emit changed();
}

QJsonObject Project::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("pvforge");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("name")] = name;
    root[QStringLiteral("author")] = author;
    root[QStringLiteral("description")] = description;
    root[QStringLiteral("width")] = width;
    root[QStringLiteral("height")] = height;
    root[QStringLiteral("fps")] = fps;
    root[QStringLiteral("background")] = background.name(QColor::HexRgb);

    QJsonArray entities;
    for (const Entity& e : m_entities)
    {
        QJsonObject entity;
        entity[QStringLiteral("id")] = e.id;
        entity[QStringLiteral("type")] = typeToString(e.type);
        entity[QStringLiteral("name")] = e.name;
        entity[QStringLiteral("sourcePath")] = e.sourcePath;

        QJsonObject data;
        for (auto it = e.data.constBegin(); it != e.data.constEnd(); ++it)
        {
            if (isColorKey(it.key()))
                data[it.key()] = it.value().value<QColor>().name(QColor::HexArgb);
            else
                data[it.key()] = QJsonValue::fromVariant(it.value());
        }
        entity[QStringLiteral("data")] = data;

        QJsonObject values;
        for (auto it = e.values.constBegin(); it != e.values.constEnd(); ++it)
            values[it.key()] = it.value();
        entity[QStringLiteral("values")] = values;

        QJsonObject tracks;
        for (auto it = e.tracks.constBegin(); it != e.tracks.constEnd(); ++it)
        {
            QJsonArray keys;
            for (const Keyframe& key : it->keys)
            {
                QJsonObject keyframe;
                keyframe[QStringLiteral("f")] = key.frame;
                keyframe[QStringLiteral("v")] = key.value;
                keys.append(keyframe);
            }
            tracks[it.key()] = keys;
        }
        entity[QStringLiteral("tracks")] = tracks;

        entities.append(entity);
    }
    root[QStringLiteral("entities")] = entities;

    QJsonArray templates;
    for (const Template& t : m_templates)
    {
        QJsonObject object;
        object[QStringLiteral("type")] = typeToString(t.type);
        object[QStringLiteral("name")] = t.name;
        object[QStringLiteral("path")] = t.path;
        object[QStringLiteral("duration")] = t.duration;

        QJsonObject data;
        for (auto it = t.data.constBegin(); it != t.data.constEnd(); ++it)
        {
            if (isColorKey(it.key()))
                data[it.key()] = it.value().value<QColor>().name(QColor::HexArgb);
            else
                data[it.key()] = QJsonValue::fromVariant(it.value());
        }
        object[QStringLiteral("data")] = data;
        templates.append(object);
    }
    root[QStringLiteral("templates")] = templates;
    return root;
}

bool Project::save(const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    const QByteArray json = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    file.write(json);
    if (!file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    filePath = path;
    return true;
}

bool Project::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pvforge"))
    {
        if (error)
            *error = QStringLiteral("不是 PVForge 工程文件");
        return false;
    }

    name = root.value(QStringLiteral("name")).toString(QStringLiteral("未命名项目"));
    author = root.value(QStringLiteral("author")).toString();
    description = root.value(QStringLiteral("description")).toString();
    width = std::max(1, root.value(QStringLiteral("width")).toInt(1920));
    height = std::max(1, root.value(QStringLiteral("height")).toInt(1080));
    fps = std::clamp(root.value(QStringLiteral("fps")).toInt(30), 1, 240);
    background = QColor(root.value(QStringLiteral("background")).toString(QStringLiteral("#101014")));
    if (!background.isValid())
        background = QColor(0x10, 0x10, 0x14);

    m_entities.clear();
    m_nextId = 1;

    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    for (const QJsonValue& value : entities)
    {
        const QJsonObject object = value.toObject();
        Entity e;
        e.id = object.value(QStringLiteral("id")).toInt();
        e.type = typeFromString(object.value(QStringLiteral("type")).toString());
        e.name = object.value(QStringLiteral("name")).toString();
        e.sourcePath = object.value(QStringLiteral("sourcePath")).toString();

        const QJsonObject data = object.value(QStringLiteral("data")).toObject();
        for (auto it = data.constBegin(); it != data.constEnd(); ++it)
        {
            if (isColorKey(it.key()))
                e.data[it.key()] = QColor(it.value().toString());
            else
                e.data[it.key()] = it.value().toVariant();
        }

        const QJsonObject values = object.value(QStringLiteral("values")).toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            e.values[it.key()] = it.value().toDouble();

        const QJsonObject tracks = object.value(QStringLiteral("tracks")).toObject();
        for (auto it = tracks.constBegin(); it != tracks.constEnd(); ++it)
        {
            Track track;
            for (const QJsonValue& keyValue : it.value().toArray())
            {
                const QJsonObject key = keyValue.toObject();
                track.keys.append({key.value(QStringLiteral("f")).toDouble(),
                                   key.value(QStringLiteral("v")).toDouble()});
            }
            std::sort(track.keys.begin(), track.keys.end(),
                      [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
            e.tracks[it.key()] = track;
        }

        if (!e.sourcePath.isEmpty())
        {
            if (e.type == EntityType::Video)
            {
                double duration = e.data.value(QStringLiteral("duration"), 0.0).toDouble();
                e.image = decodeVideoFirstFrame(e.sourcePath, nullptr, &duration);
                if (duration > 0.0)
                    e.data[QStringLiteral("duration")] = duration;
            }
            else if (e.type == EntityType::Image)
            {
                e.image = QImage(e.sourcePath);
            }
        }

        m_nextId = std::max(m_nextId, e.id + 1);
        m_entities.append(e);
    }

    m_templates.clear();
    const QJsonArray templates = root.value(QStringLiteral("templates")).toArray();
    for (const QJsonValue& value : templates)
    {
        const QJsonObject object = value.toObject();
        Template tmpl;
        tmpl.type = typeFromString(object.value(QStringLiteral("type")).toString());
        tmpl.name = object.value(QStringLiteral("name")).toString();
        tmpl.path = object.value(QStringLiteral("path")).toString();
        tmpl.duration = object.value(QStringLiteral("duration")).toDouble();

        const QJsonObject data = object.value(QStringLiteral("data")).toObject();
        for (auto it = data.constBegin(); it != data.constEnd(); ++it)
        {
            if (isColorKey(it.key()))
                tmpl.data[it.key()] = QColor(it.value().toString());
            else
                tmpl.data[it.key()] = it.value().toVariant();
        }

        if (!tmpl.path.isEmpty())
        {
            for (const Entity& e : m_entities)
            {
                if (e.sourcePath == tmpl.path && !e.image.isNull())
                {
                    tmpl.thumbnail = e.image;
                    break;
                }
            }
            if (tmpl.thumbnail.isNull())
            {
                if (tmpl.type == EntityType::Video)
                    tmpl.thumbnail = decodeVideoFirstFrame(tmpl.path, nullptr, nullptr);
                else if (tmpl.type == EntityType::Image)
                    tmpl.thumbnail = QImage(tmpl.path);
            }
        }

        m_templates.append(tmpl);
    }

    filePath = path;
    emit changed();
    return true;
}

} // namespace pvf
