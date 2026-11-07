#include "Project.h"

#include "media/VideoDecoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>
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

QString Project::assetDirectory() const
{
    if (filePath.isEmpty())
        return QString();
    return QDir(QFileInfo(filePath).absolutePath()).filePath(QStringLiteral("assets"));
}

QString Project::resolveAssetPath(const QString& stored) const
{
    if (stored.isEmpty())
        return stored;
    if (QFileInfo(stored).isAbsolute())
        return stored;
    if (filePath.isEmpty())
        return stored;
    return QDir(QFileInfo(filePath).absolutePath()).absoluteFilePath(stored);
}

QString Project::storeAssetPath(const QString& absolute) const
{
    if (absolute.isEmpty() || filePath.isEmpty())
        return absolute;
    const QString relative =
        QDir(QFileInfo(filePath).absolutePath()).relativeFilePath(absolute);
    if (relative.startsWith(QStringLiteral("..")))
        return absolute;   // outside the project folder, keep it absolute
    return relative;
}

QString Project::copyIntoProject(const QString& sourcePath) const
{
    if (sourcePath.isEmpty())
        return sourcePath;

    const QString directory = assetDirectory();
    if (directory.isEmpty())
        return sourcePath;

    QDir().mkpath(directory);

    const QFileInfo info(sourcePath);
    QString target = QDir(directory).filePath(info.fileName());
    int index = 1;
    while (QFile::exists(target))
    {
        const QString candidate = QDir(directory).filePath(
            QStringLiteral("%1_%2.%3").arg(info.completeBaseName()).arg(index++).arg(info.suffix()));
        if (QFileInfo(candidate).absoluteFilePath() == info.absoluteFilePath())
            return candidate;   // already inside the project folder
        target = candidate;
    }

    if (!QFile::copy(sourcePath, target))
        return sourcePath;
    return target;
}

QString Project::importAsset(const QString& sourcePath) const
{
    if (!packAssets)
        return sourcePath;
    return copyIntoProject(sourcePath);
}

Template* Project::templateAt(int index)
{
    if (index < 0 || index >= int(m_templates.size()))
        return nullptr;
    return &m_templates[index];
}

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
    modified = true;
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

    if (Template* tmpl = addTemplate(e))
        e.templateId = tmpl->id;
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
    if (Template* tmpl = addTemplate(e))
        e.templateId = tmpl->id;
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
    if (Template* tmpl = addTemplate(e))
        e.templateId = tmpl->id;
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
    e.data["cornerRadius"] = std::min(width, height) * 0.15;
    e.data["sides"] = 5;
    e.data["innerRatio"] = 0.45;
    if (Template* tmpl = addTemplate(e))
        e.templateId = tmpl->id;
    return append(std::move(e));
}

Template* Project::addTemplate(const Entity& entity)
{
    Template tmpl;
    tmpl.type = entity.type;
    tmpl.name = entity.name;
    tmpl.path = entity.sourcePath;
    tmpl.duration = entity.data.value(QStringLiteral("duration"), 0.0).toDouble();
    // Library previews only ever need a small image; keeping the full-size
    // bitmap here made every repaint rescale a multi-megapixel image.
    if (!entity.image.isNull())
        tmpl.thumbnail = entity.image.scaled(320, 240, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
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

    tmpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_templates.append(tmpl);
    return &m_templates.last();
}

Template* Project::addMediaTemplate(const QString& path, EntityType type,
                                    const QImage& thumbnail, double duration)
{
    for (Template& existing : m_templates)
    {
        if (existing.type == type && existing.path == path)
        {
            if (existing.thumbnail.isNull() && !thumbnail.isNull())
                existing.thumbnail = thumbnail.scaled(320, 240, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation);
            return &existing;
        }
    }

    Template tmpl;
    tmpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tmpl.type = type;
    tmpl.name = QFileInfo(path).completeBaseName();
    tmpl.path = path;
    tmpl.duration = duration;
    if (!thumbnail.isNull())
        tmpl.thumbnail = thumbnail.scaled(320, 240, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);

    m_templates.append(tmpl);
    modified = true;
    emit changed();
    return &m_templates.last();
}

QString Project::importMediaFile(const QString& path, QString* error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList imageExtensions = {QStringLiteral("png"), QStringLiteral("jpg"),
                                                QStringLiteral("jpeg"), QStringLiteral("webp"),
                                                QStringLiteral("bmp")};
    static const QStringList videoExtensions = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                                QStringLiteral("mov"), QStringLiteral("mkv"),
                                                QStringLiteral("avi"), QStringLiteral("m4v")};

    if (imageExtensions.contains(suffix))
    {
        const QImage image(path);
        if (image.isNull())
        {
            if (error)
                *error = QStringLiteral("无法读取图片");
            return QString();
        }
        const QString imported = importAsset(path);
        addMediaTemplate(imported, EntityType::Image, image);
        return imported;
    }

    if (videoExtensions.contains(suffix))
    {
        QString parseError;
        double duration = 0.0;
        const QImage frame = decodeVideoFirstFrame(path, &parseError, &duration);
        if (frame.isNull())
        {
            if (error)
                *error = parseError;
            return QString();
        }
        const QString imported = importAsset(path);
        addMediaTemplate(imported, EntityType::Video, frame, duration);
        return imported;
    }

    if (error)
        *error = QStringLiteral("不支持的文件类型");
    return QString();
}

Entity* Project::createFromTemplate(const Template& tmpl){
    switch (tmpl.type)
    {
    case EntityType::Image:
        return addImage(tmpl.path);
    case EntityType::Video:
    {
        double duration = tmpl.duration;
        QString error;
        const QImage frame = decodeVideoFirstFrame(tmpl.path, &error, &duration);
        return addVideo(tmpl.path, frame.isNull() ? tmpl.thumbnail : frame, duration);
    }
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
    modified = true;
    emit changed();
}

Entity* Project::entity(EntityId id)
{
    for (Entity& e : m_entities)
        if (e.id == id)
            return &e;
    return nullptr;
}

int Project::removeEntity(EntityId id)
{
    const auto it = std::find_if(m_entities.begin(), m_entities.end(),
                                 [id](const Entity& e) { return e.id == id; });
    if (it == m_entities.end())
        return -1;
    const int index = int(it - m_entities.begin());
    m_entities.erase(it);
    modified = true;
    emit changed();
    return index;
}

void Project::insertEntity(const Entity& entity, int index)
{
    const int position = std::clamp(index, 0, int(m_entities.size()));
    m_entities.insert(position, entity);
    m_nextId = std::max(m_nextId, entity.id + 1);
    modified = true;
    emit changed();
}

void Project::setEntityName(EntityId id, const QString& name)
{
    Entity* e = entity(id);
    if (!e || e->name == name)
        return;
    e->name = name;
    modified = true;
    emit changed();
}

void Project::setEntityFlag(EntityId id, EntityFlag flag, bool value)
{
    Entity* e = entity(id);
    if (!e)
        return;
    if (flag == EntityFlag::Hidden)
        e->hidden = value;
    else
        e->locked = value;
    modified = true;
    emit changed();
}

QVector<KeyframeRecord> Project::removeKeyframesAt(EntityId id, double frame)
{
    QVector<KeyframeRecord> removed;
    Entity* e = entity(id);
    if (!e || e->locked)
        return removed;   // locked entities keep their keyframes

    for (auto it = e->tracks.begin(); it != e->tracks.end();)
    {
        Track& track = it.value();
        for (int i = int(track.keys.size()) - 1; i >= 0; --i)
        {
            if (std::abs(track.keys[i].frame - frame) < 1e-6)
            {
                removed.append({id, it.key(), track.keys[i].frame, track.keys[i].value});
                track.keys.removeAt(i);
            }
        }
        if (track.keys.isEmpty())
            it = e->tracks.erase(it);
        else
            ++it;
    }

    if (!removed.isEmpty())
        modified = true;
    emit changed();
    return removed;
}

void Project::addKeyframe(EntityId id, const QString& property, double frame, double value)
{
    Entity* e = entity(id);
    if (!e)
        return;
    e->tracks[property].setKey(frame, value);
    modified = true;
    emit changed();
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
    if (!e || e->locked)
        return;   // locked entities are frozen

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
    modified = true;
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
        entity[QStringLiteral("templateId")] = e.templateId;
        entity[QStringLiteral("hidden")] = e.hidden;
        entity[QStringLiteral("locked")] = e.locked;
        entity[QStringLiteral("name")] = e.name;
        entity[QStringLiteral("sourcePath")] = storeAssetPath(e.sourcePath);

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
        object[QStringLiteral("id")] = t.id;
        object[QStringLiteral("type")] = typeToString(t.type);
        object[QStringLiteral("name")] = t.name;
        object[QStringLiteral("path")] = storeAssetPath(t.path);
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
    modified = false;
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

    // Set the project path first so relative asset paths can be resolved.
    filePath = path;

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
        e.templateId = object.value(QStringLiteral("templateId")).toString();
        e.hidden = object.value(QStringLiteral("hidden")).toBool();
        e.locked = object.value(QStringLiteral("locked")).toBool();
        e.name = object.value(QStringLiteral("name")).toString();
        e.sourcePath = resolveAssetPath(object.value(QStringLiteral("sourcePath")).toString());

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
        tmpl.id = object.value(QStringLiteral("id")).toString();
        if (tmpl.id.isEmpty())
            tmpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        tmpl.type = typeFromString(object.value(QStringLiteral("type")).toString());
        tmpl.name = object.value(QStringLiteral("name")).toString();
        tmpl.path = resolveAssetPath(object.value(QStringLiteral("path")).toString());
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
                    tmpl.thumbnail = e.image.scaled(320, 240, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
                    break;
                }
            }
            if (tmpl.thumbnail.isNull())
            {
                if (tmpl.type == EntityType::Video)
                {
                    const QImage frame = decodeVideoFirstFrame(tmpl.path, nullptr, nullptr);
                    if (!frame.isNull())
                        tmpl.thumbnail = frame.scaled(320, 240, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation);
                }
                else if (tmpl.type == EntityType::Image)
                {
                    const QImage image(tmpl.path);
                    if (!image.isNull())
                        tmpl.thumbnail = image.scaled(320, 240, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation);
                }
            }
        }

        m_templates.append(tmpl);
    }

    filePath = path;
    modified = false;
    emit changed();
    return true;
}

} // namespace pvf
