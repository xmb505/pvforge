#include "Project.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace pvf {

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

Entity* Project::addImage(const QString& path)
{
    QImage img(path);
    if (img.isNull())
        return nullptr;

    Entity e;
    e.id = m_nextId++;
    e.type = EntityType::Image;
    e.name = QFileInfo(path).completeBaseName();
    e.sourcePath = path;
    e.image = img;

    // Fit oversized images to 60% of the shorter canvas edge.
    const double maxEdge = std::max(img.width(), img.height());
    const double target = std::min(width, height) * 0.6;
    const double fit = maxEdge > target ? target / maxEdge : 1.0;

    e.values["x"] = 0.0;
    e.values["y"] = 0.0;
    e.values["rotation"] = 0.0;
    e.values["scaleX"] = fit;
    e.values["scaleY"] = fit;
    e.values["opacity"] = 1.0;

    m_entities.append(e);
    emit changed();
    return &m_entities.last();
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

} // namespace pvf
