#include "Project.h"

#include "media/AudioDecoder.h"
#include "media/MediaImporter.h"
#include "media/VideoDecoder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>

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
    case EntityType::Camera:
        return QStringLiteral("camera");
    case EntityType::Audio:
        return QStringLiteral("audio");
    case EntityType::Folder:
        return QStringLiteral("folder");
    case EntityType::ComicText:
        return QStringLiteral("comicText");
    case EntityType::RadialLines:
        return QStringLiteral("radialLines");
    case EntityType::FlowLines:
        return QStringLiteral("flowLines");
    case EntityType::BrandWall:
        return QStringLiteral("brandWall");
    case EntityType::HalftoneDots:
        return QStringLiteral("halftoneDots");
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
    if (text == QLatin1String("camera"))
        return EntityType::Camera;
    if (text == QLatin1String("audio"))
        return EntityType::Audio;
    if (text == QLatin1String("folder"))
        return EntityType::Folder;
    if (text == QLatin1String("comicText"))
        return EntityType::ComicText;
    if (text == QLatin1String("radialLines"))
        return EntityType::RadialLines;
    if (text == QLatin1String("flowLines"))
        return EntityType::FlowLines;
    if (text == QLatin1String("brandWall"))
        return EntityType::BrandWall;
    if (text == QLatin1String("halftoneDots"))
        return EntityType::HalftoneDots;
    return EntityType::Image;
}

bool filesIdentical(const QString& a, const QString& b)
{
    QFile fileA(a);
    QFile fileB(b);
    if (!fileA.open(QIODevice::ReadOnly) || !fileB.open(QIODevice::ReadOnly))
        return false;
    if (fileA.size() != fileB.size())
        return false;

    constexpr qint64 chunkSize = 1 << 20;
    QByteArray chunkA = fileA.read(chunkSize);
    QByteArray chunkB = fileB.read(chunkSize);
    while (!chunkA.isEmpty() && !chunkB.isEmpty())
    {
        if (chunkA != chunkB)
            return false;
        chunkA = fileA.read(chunkSize);
        chunkB = fileB.read(chunkSize);
    }
    return chunkA.isEmpty() && chunkB.isEmpty();
}

// Colour fields are recognised by their name so a new one does not have to
// be registered here: "color", "fill", "stroke" and anything ending with
// them, such as "bubbleFill".
bool isColorKey(const QString& key)
{
    const QString lower = key.toLower();
    return lower.endsWith(QLatin1String("color")) || lower.endsWith(QLatin1String("fill"))
           || lower.endsWith(QLatin1String("stroke"));
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
    return copyIntoAssetDirectory(sourcePath, assetDirectory());
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
        {
            if (e.type == EntityType::Audio)
            {
                const double start = e.data.value(QStringLiteral("startFrame"), 0.0).toDouble();
                const double trimStart =
                    e.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
                const double trimEnd =
                    e.data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
                last = std::max(last, start + std::max(0.0, media - trimStart - trimEnd) * fps);
            }
            else
            {
                last = std::max(last, media * fps);
            }
        }
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
            const double eased = applyEasing(b.ease, t);
            return a.value + (b.value - a.value) * eased;
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
    const EntityId id = e.id;
    m_entities.append(std::move(e));
    m_entityIndex.insert(id, int(m_entities.size()) - 1);
    modified = true;
    emit changed();
    return &m_entities.last();
}

Entity* Project::addImage(const QString& path, const QString& displayName)
{
    QImage img(path);
    if (img.isNull())
        return nullptr;

    Entity e = makeBase(EntityType::Image,
                        displayName.isEmpty() ? QFileInfo(path).completeBaseName()
                                              : displayName);
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

Entity* Project::addVideo(const QString& path, const QImage& firstFrame, double duration,
                          const QString& displayName)
{
    Entity e = makeBase(EntityType::Video,
                        displayName.isEmpty() ? QFileInfo(path).completeBaseName()
                                              : displayName);
    e.sourcePath = path;
    e.image = firstFrame;
    e.data["duration"] = duration;
    if (!firstFrame.isNull())
    {
        e.data["sourceWidth"] = firstFrame.width();
        e.data["sourceHeight"] = firstFrame.height();
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

Entity* Project::addEntity(EntityType type, const QString& name,
                            const QVariantMap& data)
{
    Entity e = makeBase(type, name);
    e.data = data;
    if (type == EntityType::BrandWall)
        refreshBrandWallImages(e);
    if (Template* tmpl = addTemplate(e))
        e.templateId = tmpl->id;
    return append(std::move(e));
}

void Project::refreshBrandWallImages(Entity& e)
{
    e.wallLogos.clear();
    for (const QString& path : e.data.value(QStringLiteral("logos")).toStringList())
        e.wallLogos.append(BrandWall::loadLogo(path));
}

Entity* Project::addBrandWall(const BrandWallParams& params)
{
    BrandWallParams copy = params;
    copy.width = std::max(16.0, params.width);
    copy.height = std::max(16.0, params.height);
    copy.columns = std::clamp(params.columns, 1, 64);
    copy.rows = std::clamp(params.rows, 1, 64);
    copy.gap = std::clamp(params.gap, 0.0, 64.0);
    copy.logoOpacity = std::clamp(params.logoOpacity, 0.0, 1.0);
    QVariantMap data;
    copy.writeTo(data);
    return addEntity(EntityType::BrandWall, QStringLiteral("品牌瓷砖墙"), data);
}

Entity* Project::addText(const TextStyle& style, const QString& name)
{
    QVariantMap data;
    style.writeTo(data);
    return addEntity(EntityType::Text,
                     name.isEmpty() ? QStringLiteral("文字") : name, data);
}

Entity* Project::addText(const QString& text, const QString& family, int pixelSize,
                         const QColor& color, int align)
{
    TextStyle style;
    style.text = text;
    style.fontFamily = family;
    style.pixelSize = pixelSize;
    style.color = color;
    style.align = align;
    return addText(style);
}

Entity* Project::addCamera(const QString& name)
{
    Entity e = makeBase(EntityType::Camera,
                        name.isEmpty() ? QStringLiteral("摄像机") : name);
    e.values["scaleX"] = 1.0;
    e.values["scaleY"] = 1.0;
    return append(std::move(e));
}

Entity* Project::addFolder(const QString& name, EntityId parent)
{
    const QString base = name.isEmpty() ? QStringLiteral("文件夹") : name;
    QString unique = base;
    int suffix = 2;
    const auto nameTaken = [this](const QString& candidate) {
        for (const Entity& e : m_entities)
            if (e.isFolder() && e.name == candidate)
                return true;
        return false;
    };
    while (nameTaken(unique))
        unique = QStringLiteral("%1 %2").arg(base).arg(suffix++);

    Entity e = makeBase(EntityType::Folder, unique);
    if (parent != kInvalidEntity && entity(parent) && parent != e.id)
        e.parent = parent;
    return append(std::move(e));
}

bool Project::validateSceneRange(int startFrame, int endFrame, const QString& ignoreId,
                                 QString* error) const
{
    if (startFrame < 0)
    {
        if (error)
            *error = QStringLiteral("场景起始帧不能为负");
        return false;
    }
    if (endFrame < startFrame)
    {
        if (error)
            *error = QStringLiteral("场景结束帧不能早于起始帧");
        return false;
    }
    if (endFrame > timelineLength())
    {
        if (error)
            *error = QStringLiteral("场景结束帧 %1 超出时间轴长度 %2")
                         .arg(endFrame)
                         .arg(timelineLength());
        return false;
    }
    for (const Scene& scene : m_scenes)
    {
        if (!ignoreId.isEmpty() && scene.id == ignoreId)
            continue;
        if (startFrame <= scene.endFrame && scene.startFrame <= endFrame)
        {
            if (error)
                *error = QStringLiteral("场景区间与「%1」重叠").arg(scene.name);
            return false;
        }
    }
    return true;
}

Scene* Project::addScene(const QString& name, int startFrame, int endFrame,
                         const QColor& color, const QString& note, QString* error)
{
    if (!validateSceneRange(startFrame, endFrame, QString(), error))
        return nullptr;

    QString sceneName = name.trimmed();
    if (sceneName.isEmpty())
        sceneName = QStringLiteral("场景 %1").arg(m_scenes.size() + 1);

    Entity* folder = addFolder(sceneName);
    if (!folder)
    {
        if (error)
            *error = QStringLiteral("无法创建场景文件夹");
        return nullptr;
    }

    Scene scene;
    scene.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    scene.name = sceneName;
    scene.startFrame = startFrame;
    scene.endFrame = endFrame;
    scene.folderId = folder->id;
    scene.color = color.isValid() ? color : QColor(90, 170, 255);
    scene.note = note;

    int index = 0;
    while (index < int(m_scenes.size())
           && m_scenes[index].startFrame < scene.startFrame)
    {
        ++index;
    }
    m_scenes.insert(index, scene);
    modified = true;
    emit changed();
    return &m_scenes[index];
}

bool Project::updateScene(const QString& id, const QString& name, int startFrame,
                          int endFrame, const QColor& color, const QString& note,
                          QString* error)
{
    for (Scene& scene : m_scenes)
    {
        if (scene.id != id)
            continue;
        if (!validateSceneRange(startFrame, endFrame, id, error))
            return false;

        if (!name.trimmed().isEmpty())
            scene.name = name.trimmed();
        scene.startFrame = startFrame;
        scene.endFrame = endFrame;
        if (color.isValid())
            scene.color = color;
        scene.note = note;
        std::stable_sort(m_scenes.begin(), m_scenes.end(),
                         [](const Scene& a, const Scene& b) {
                             return a.startFrame < b.startFrame;
                         });
        modified = true;
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("场景不存在");
    return false;
}

bool Project::removeScene(const QString& id)
{
    for (int i = 0; i < int(m_scenes.size()); ++i)
    {
        if (m_scenes[i].id != id)
            continue;
        m_scenes.removeAt(i);
        modified = true;
        emit changed();
        return true;
    }
    return false;
}

const Scene* Project::sceneAt(int frame) const
{
    for (const Scene& scene : m_scenes)
        if (frame >= scene.startFrame && frame <= scene.endFrame)
            return &scene;
    return nullptr;
}

const Scene* Project::sceneById(const QString& id) const
{
    for (const Scene& scene : m_scenes)
        if (scene.id == id)
            return &scene;
    return nullptr;
}

EntityId Project::sceneFolderAt(int frame) const
{
    const Scene* scene = sceneAt(frame);
    return scene ? scene->folderId : kInvalidEntity;
}

Annotation* Project::addAnnotation(int frame, const QString& title, const QString& note,
                                   const QStringList& tags, const QColor& color,
                                   QString* error)
{
    if (frame < 0)
    {
        if (error)
            *error = QStringLiteral("注释帧不能为负");
        return nullptr;
    }

    Annotation annotation;
    annotation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    annotation.frame = frame;
    annotation.title = title;
    annotation.note = note;
    annotation.tags = tags;
    annotation.color = color.isValid() ? color : QColor(255, 190, 60);

    int index = 0;
    while (index < int(m_annotations.size())
           && m_annotations[index].frame <= annotation.frame)
    {
        ++index;
    }
    m_annotations.insert(index, annotation);
    modified = true;
    emit changed();
    return &m_annotations[index];
}

bool Project::updateAnnotation(const Annotation& annotation, QString* error)
{
    if (annotation.frame < 0)
    {
        if (error)
            *error = QStringLiteral("注释帧不能为负");
        return false;
    }
    for (Annotation& existing : m_annotations)
    {
        if (existing.id != annotation.id)
            continue;
        existing.frame = annotation.frame;
        existing.title = annotation.title;
        existing.note = annotation.note;
        existing.tags = annotation.tags;
        if (annotation.color.isValid())
            existing.color = annotation.color;
        std::stable_sort(m_annotations.begin(), m_annotations.end(),
                         [](const Annotation& a, const Annotation& b) {
                             return a.frame < b.frame;
                         });
        modified = true;
        emit changed();
        return true;
    }
    if (error)
        *error = QStringLiteral("注释不存在");
    return false;
}

bool Project::removeAnnotation(const QString& id)
{
    for (int i = 0; i < int(m_annotations.size()); ++i)
    {
        if (m_annotations[i].id != id)
            continue;
        m_annotations.removeAt(i);
        modified = true;
        emit changed();
        return true;
    }
    return false;
}

const Annotation* Project::annotationById(const QString& id) const
{
    for (const Annotation& annotation : m_annotations)
        if (annotation.id == id)
            return &annotation;
    return nullptr;
}

bool Project::setEntityParent(EntityId id, EntityId parent)
{
    Entity* e = entity(id);
    if (!e || e->parent == parent)
        return false;
    if (parent != kInvalidEntity)
    {
        if (!entity(parent) || parent == id || isDescendantOf(parent, id))
            return false;
    }
    e->parent = parent;
    modified = true;
    emit changed();
    return true;
}
bool Project::moveEntity(EntityId id, EntityId parent, int index)
{
    const auto it = std::find_if(m_entities.begin(), m_entities.end(),
                                 [id](const Entity& e) { return e.id == id; });
    if (it == m_entities.end())
        return false;
    if (parent != kInvalidEntity)
    {
        if (!entity(parent) || parent == id || isDescendantOf(parent, id))
            return false;
    }

    const int oldIndex = int(it - m_entities.begin());
    Entity moved = *it;
    m_entities.erase(it);
    int target = std::clamp(index, 0, int(m_entities.size()));
    if (target > oldIndex)
        --target;
    moved.parent = parent;
    m_entities.insert(target, moved);
    rebuildEntityIndex();
    modified = true;
    emit changed();
    return true;
}

bool Project::moveEntities(const QVector<EntityId>& ids, EntityId parent, int index)
{
    if (ids.isEmpty())
        return false;
    if (parent != kInvalidEntity)
    {
        if (!entity(parent))
            return false;
        for (EntityId id : ids)
            if (parent == id || isDescendantOf(parent, id))
                return false;
    }

    QVector<Entity> moved;
    for (int i = int(m_entities.size()) - 1; i >= 0; --i)
    {
        if (!ids.contains(m_entities[i].id))
            continue;
        moved.prepend(m_entities[i]);
        m_entities.removeAt(i);
    }
    if (moved.isEmpty())
        return false;

    const int target = std::clamp(index, 0, int(m_entities.size()));
    for (Entity& entity : moved)
        entity.parent = parent;
    for (int i = 0; i < int(moved.size()); ++i)
        m_entities.insert(target + i, moved[i]);
    rebuildEntityIndex();
    modified = true;
    emit changed();
    return true;
}

bool Project::isDescendantOf(EntityId id, EntityId ancestor) const
{
    if (id == kInvalidEntity || ancestor == kInvalidEntity)
        return false;
    QSet<EntityId> seen;
    EntityId current = id;
    while (current != kInvalidEntity && !seen.contains(current))
    {
        if (current == ancestor)
            return true;
        seen.insert(current);
        const Entity* e = entity(current);
        if (!e)
            break;
        current = e->parent;
    }
    return false;
}

bool Project::hasChildren(EntityId id) const
{
    for (const Entity& e : m_entities)
        if (e.parent == id)
            return true;
    return false;
}

namespace {
// The parent chain is kept acyclic by setEntityParent/moveEntity and the
// loader; the depth cap only guards against a corrupt file. Avoiding a QSet
// keeps the render loop allocation-free for scenes with many entities.
constexpr int kMaxParentDepth = 64;
} // namespace

bool Project::isEffectivelyHidden(EntityId id) const
{
    EntityId current = id;
    for (int depth = 0; depth < kMaxParentDepth && current != kInvalidEntity;
         ++depth)
    {
        const Entity* e = entity(current);
        if (!e)
            break;
        if (e->hidden)
            return true;
        current = e->parent;
    }
    return false;
}

double Project::effectiveOpacity(EntityId id, double frame) const
{
    double opacity = 1.0;
    EntityId current = id;
    for (int depth = 0; depth < kMaxParentDepth && current != kInvalidEntity;
         ++depth)
    {
        const Entity* e = entity(current);
        if (!e)
            break;
        opacity *= std::clamp(value(current, QStringLiteral("opacity"), frame), 0.0, 1.0);
        current = e->parent;
    }
    return std::clamp(opacity, 0.0, 1.0);
}

bool Project::isEffectivelyLocked(EntityId id) const
{
    EntityId current = id;
    for (int depth = 0; depth < kMaxParentDepth && current != kInvalidEntity;
         ++depth)
    {
        const Entity* e = entity(current);
        if (!e)
            break;
        if (e->locked)
            return true;
        current = e->parent;
    }
    return false;
}

QVector<int> Project::entityOrder() const
{
    QHash<EntityId, QVector<int>> children;
    QVector<int> roots;
    for (int i = 0; i < int(m_entities.size()); ++i)
    {
        const Entity& e = m_entities[i];
        if (e.parent != kInvalidEntity && e.parent != e.id && entity(e.parent))
            children[e.parent].append(i);
        else
            roots.append(i);
    }

    QVector<int> order;
    QSet<EntityId> visited;
    std::function<void(int)> visit = [&](int index) {
        const Entity& e = m_entities[index];
        if (visited.contains(e.id))
            return;
        visited.insert(e.id);
        order.append(index);
        for (int child : children.value(e.id))
            visit(child);
    };
    for (int root : roots)
        visit(root);
    for (int i = 0; i < int(m_entities.size()); ++i)
        if (!visited.contains(m_entities[i].id))
            order.append(i);
    return order;
}

Entity* Project::addAudio(const QString& path, const std::shared_ptr<AudioClip>& audio,
                          const QString& displayName)
{
    Entity e = makeBase(EntityType::Audio,
                        displayName.isEmpty() ? QFileInfo(path).completeBaseName()
                                              : displayName);
    e.sourcePath = path;
    e.audio = audio;
    e.data["duration"] = audio ? audio->duration : 0.0;
    e.data["startFrame"] = 0.0;
    e.data["trimStart"] = 0.0;
    e.data["trimEnd"] = 0.0;
    e.data["volume"] = 1.0;
    e.data["fadeIn"] = 0.0;
    e.data["fadeOut"] = 0.0;
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
    if (entity.isFolder())
        return nullptr;
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
    if (entity.type == EntityType::Audio && entity.audio)
    {
        tmpl.thumbnail = renderAudioThumbnail(*entity.audio, QSize(320, 80));
        tmpl.audio = entity.audio;
    }
    // Media templates are identified by their file; everything else keeps
    // the parameters it was created with.
    if (entity.sourcePath.isEmpty())
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
                                    const QImage& thumbnail, double duration,
                                    const QString& displayName,
                                    const std::shared_ptr<AudioClip>& audio)
{
    for (Template& existing : m_templates)
    {
        if (existing.type == type && existing.path == path)
        {
            if (existing.thumbnail.isNull() && !thumbnail.isNull())
                existing.thumbnail = thumbnail.scaled(320, 240, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation);
            if (!existing.audio && audio)
                existing.audio = audio;
            return &existing;
        }
    }

    Template tmpl;
    tmpl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tmpl.type = type;
    tmpl.name = displayName.isEmpty() ? QFileInfo(path).completeBaseName() : displayName;
    tmpl.path = path;
    tmpl.duration = duration;
    if (!thumbnail.isNull())
        tmpl.thumbnail = thumbnail.scaled(320, 240, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
    tmpl.audio = audio;

    m_templates.append(tmpl);
    modified = true;
    emit changed();
    return &m_templates.last();
}

bool Project::addImportedMedia(const MediaImportResult& result)
{
    if (!result.error.isEmpty() || result.importedPath.isEmpty())
        return false;

    if (!addMediaTemplate(result.importedPath, result.type, result.thumbnail,
                          result.duration, result.displayName, result.audio))
    {
        return false;
    }

    if (result.type == EntityType::Video && result.audio)
    {
        const QImage thumbnail =
            renderAudioThumbnail(*result.audio, QSize(320, 80));
        addMediaTemplate(result.importedPath, EntityType::Audio, thumbnail,
                         result.audio->duration,
                         result.displayName + QStringLiteral(" · 音频"),
                         result.audio);
    }
    return true;
}

QString Project::importMediaFile(const QString& path, QString* error)
{
    const MediaImportResult result = processMediaFile(path, assetDirectory());
    if (!result.error.isEmpty() || result.importedPath.isEmpty())
    {
        if (error)
            *error = result.error;
        return QString();
    }

    addImportedMedia(result);
    return result.importedPath;
}

Entity* Project::createFromTemplate(const Template& tmpl){
    switch (tmpl.type)
    {
    case EntityType::Image:
        return addImage(tmpl.path, tmpl.name);
    case EntityType::Video:
    {
        double duration = tmpl.duration;
        QString error;
        const QImage frame = decodeVideoFirstFrame(tmpl.path, &error, &duration);
        Entity* video = addVideo(tmpl.path, frame.isNull() ? tmpl.thumbnail : frame,
                                 duration, tmpl.name);
        if (!video)
            return nullptr;
        const EntityId videoId = video->id;
        if (tmpl.audio)
        {
            // A video with an extracted audio track creates the paired audio
            // entity as well, so the timeline gets picture and sound together.
            if (Entity* audio =
                    addAudio(tmpl.path, tmpl.audio,
                             tmpl.name + QStringLiteral(" · 音频")))
            {
                audio->data[QStringLiteral("pairedVideo")] = videoId;
            }
        }
        return entity(videoId);
    }
    case EntityType::Text:
    case EntityType::ComicText:
    case EntityType::RadialLines:
    case EntityType::FlowLines:
    case EntityType::BrandWall:
    case EntityType::HalftoneDots:
    case EntityType::Vector:
        // Templates keep every parameter they were created with.
        return addEntity(tmpl.type, tmpl.name, tmpl.data);
    case EntityType::Camera:
        return nullptr;
    case EntityType::Folder:
        return nullptr;
    case EntityType::Audio:
    {
        std::shared_ptr<AudioClip> clip = tmpl.audio;
        if (!clip)
        {
            const AudioDecodeResult decoded = decodeAudioFile(tmpl.path);
            clip = decoded.clip;
        }
        if (!clip)
            return nullptr;
        return addAudio(tmpl.path, clip, tmpl.name);
    }
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

void Project::rebuildEntityIndex()
{
    m_entityIndex.clear();
    m_entityIndex.reserve(m_entities.size());
    for (int index = 0; index < int(m_entities.size()); ++index)
        m_entityIndex.insert(m_entities[index].id, index);
}

Entity* Project::entity(EntityId id)
{
    const int index = m_entityIndex.value(id, -1);
    if (index < 0 || index >= int(m_entities.size())
        || m_entities[index].id != id)
        return nullptr;
    return &m_entities[index];
}

int Project::removeEntity(EntityId id)
{
    const auto it = std::find_if(m_entities.begin(), m_entities.end(),
                                 [id](const Entity& e) { return e.id == id; });
    if (it == m_entities.end())
        return -1;
    const int index = int(it - m_entities.begin());
    m_entities.erase(it);
    rebuildEntityIndex();
    if (activeCamera == id || monitorView == id)
    {
        EntityId fallback = kInvalidEntity;
        for (const Entity& e : m_entities)
        {
            if (e.isCamera() && !isEffectivelyHidden(e.id))
            {
                fallback = e.id;
                break;
            }
        }
        if (activeCamera == id)
            activeCamera = fallback;
        if (monitorView == id)
            monitorView = fallback != kInvalidEntity ? fallback : kWorkCameraView;
    }
    modified = true;
    emit changed();
    return index;
}

void Project::insertEntity(const Entity& entity, int index)
{
    const int position = std::clamp(index, 0, int(m_entities.size()));
    m_entities.insert(position, entity);
    rebuildEntityIndex();
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

QVector<EntityId> Project::replaceEntitySource(EntityId id,
                                               const MediaImportResult& result,
                                               QString* error)
{
    QVector<EntityId> updated;
    Entity* e = entity(id);
    if (!e)
    {
        if (error)
            *error = QStringLiteral("实体不存在");
        return updated;
    }
    if (e->type != result.type)
    {
        if (error)
            *error = QStringLiteral("素材类型不匹配");
        return updated;
    }
    if (e->locked)
    {
        if (error)
            *error = QStringLiteral("实体已锁定");
        return updated;
    }

    switch (e->type)
    {
    case EntityType::Image:
    {
        const QImage image(result.importedPath);
        if (image.isNull())
        {
            if (error)
                *error = QStringLiteral("无法读取图片");
            return updated;
        }
        e->sourcePath = result.importedPath;
        e->image = image;
        e->thumbnail = QImage();
        e->pixmap = QPixmap();
        updated.append(id);
        break;
    }
    case EntityType::Video:
    {
        QString decodeError;
        double duration = 0.0;
        const QImage frame =
            decodeVideoFirstFrame(result.importedPath, &decodeError, &duration);
        if (frame.isNull())
        {
            if (error)
                *error = decodeError;
            return updated;
        }
        e->sourcePath = result.importedPath;
        e->image = frame;
        e->thumbnail = QImage();
        e->pixmap = QPixmap();
        e->data[QStringLiteral("duration")] = duration;
        e->data[QStringLiteral("sourceWidth")] = frame.width();
        e->data[QStringLiteral("sourceHeight")] = frame.height();
        updated.append(id);
        // The paired audio entity uses the same file; swap it too.
        for (Entity& other : m_entities)
        {
            if (other.type != EntityType::Audio
                || other.data.value(QStringLiteral("pairedVideo")).toInt() != id)
                continue;
            other.sourcePath = result.importedPath;
            other.audio = result.audio;
            other.data[QStringLiteral("duration")] =
                result.audio ? result.audio->duration : 0.0;
            updated.append(other.id);
        }
        break;
    }
    case EntityType::Audio:
    {
        if (!result.audio)
        {
            if (error)
                *error = QStringLiteral("无法解码音频");
            return updated;
        }
        e->sourcePath = result.importedPath;
        e->audio = result.audio;
        e->data[QStringLiteral("duration")] = result.audio->duration;
        updated.append(id);
        break;
    }
    default:
        if (error)
            *error = QStringLiteral("这个实体没有可更换的素材");
        return updated;
    }

    modified = true;
    emit changed();
    return updated;
}

void Project::setEntities(const QVector<Entity>& entities)
{
    m_entities = entities;
    rebuildEntityIndex();
    modified = true;
    emit changed();
}

void Project::replaceEntity(const Entity& entity)
{
    for (Entity& e : m_entities)
    {
        if (e.id != entity.id)
            continue;
        e = entity;
        modified = true;
        emit changed();
        return;
    }
}

void Project::removeKeyframe(EntityId id, const QString& prop, double frame)
{
    Entity* e = entity(id);
    if (!e)
        return;
    auto it = e->tracks.find(prop);
    if (it == e->tracks.end())
        return;
    const int before = it->keys.size();
    it->removeKey(frame);
    if (it->keys.size() == before)
        return;
    if (it->keys.isEmpty())
        e->tracks.erase(it);
    modified = true;
    emit changed();
}

void Project::setEntityData(EntityId id, const QString& key, const QVariant& value)
{
    Entity* e = entity(id);
    if (!e || e->data.value(key) == value)
        return;
    e->data[key] = value;
    modified = true;
    emit changed();
}

void Project::setEntityDataMap(EntityId id, const QVariantMap& data)
{
    Entity* e = entity(id);
    if (!e)
        return;
    bool updated = false;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it)
    {
        if (e->data.value(it.key()) == it.value())
            continue;
        e->data[it.key()] = it.value();
        updated = true;
    }
    if (!updated)
        return;
    if (e->type == EntityType::BrandWall)
        refreshBrandWallImages(*e);
    modified = true;
    emit changed();
}

void Project::setEntityFlag(EntityId id, EntityFlag flag, bool value)
{
    Entity* e = entity(id);
    if (!e)
        return;
    if (flag == EntityFlag::Hidden)
    {
        e->hidden = value;
        if (value)
        {
            const auto visibleCamera = [this](EntityId cameraId) {
                const Entity* camera = entity(cameraId);
                return camera && camera->isCamera()
                       && !isEffectivelyHidden(cameraId);
            };
            EntityId fallback = kInvalidEntity;
            for (const Entity& other : m_entities)
            {
                if (other.isCamera() && !isEffectivelyHidden(other.id))
                {
                    fallback = other.id;
                    break;
                }
            }
            if (activeCamera != kInvalidEntity && !visibleCamera(activeCamera))
                activeCamera = fallback;
            if (monitorView >= 0 && !visibleCamera(monitorView))
                monitorView = fallback != kInvalidEntity ? fallback : kWorkCameraView;
        }
    }
    else
    {
        e->locked = value;
    }
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
                removed.append({id, it.key(), track.keys[i].frame, track.keys[i].value,
                                track.keys[i].ease, track.keys[i].note});
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
    Keyframe key;
    key.frame = frame;
    key.value = value;
    addKeyframe(id, property, key);
}

void Project::addKeyframe(EntityId id, const QString& property, const Keyframe& key)
{
    Entity* e = entity(id);
    if (!e)
        return;

    Track& track = e->tracks[property];
    for (Keyframe& existing : track.keys)
    {
        if (std::abs(existing.frame - key.frame) < 1e-6)
        {
            existing.value = key.value;
            existing.ease = key.ease;
            existing.note = key.note;
            modified = true;
            emit changed();
            return;
        }
    }
    track.keys.append(key);
    std::sort(track.keys.begin(), track.keys.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
    modified = true;
    emit changed();
}

void Project::moveKeyframes(const QVector<KeyframeRecord>& records, double delta)
{
    if (std::abs(delta) < 1e-9)
        return;

    struct Move
    {
        EntityId entity = kInvalidEntity;
        QString property;
        Keyframe key;
        double target = 0.0;
    };

    // Collect first: removing and re-adding one by one would let a moved key
    // land on a selected neighbour that has not moved yet.
    QVector<Move> moves;
    for (const KeyframeRecord& record : records)
    {
        Entity* e = entity(record.entity);
        if (!e || e->locked)
            continue;
        auto track = e->tracks.find(record.property);
        if (track == e->tracks.end())
            continue;

        const QVector<Keyframe>& keys = track->keys;
        int index = -1;
        for (int i = 0; i < int(keys.size()); ++i)
            if (std::abs(keys[i].frame - record.frame) < 1e-6)
            {
                index = i;
                break;
            }
        if (index < 0)
            continue;

        const double target = std::max(0.0, record.frame + delta);
        if (std::abs(target - keys[index].frame) < 1e-6)
            continue;
        moves.append({record.entity, record.property, keys[index], target});
    }

    for (const Move& move : moves)
    {
        Entity* e = entity(move.entity);
        if (!e)
            continue;
        auto track = e->tracks.find(move.property);
        if (track == e->tracks.end())
            continue;
        QVector<Keyframe>& keys = track->keys;
        for (int i = 0; i < int(keys.size()); ++i)
            if (std::abs(keys[i].frame - move.key.frame) < 1e-6)
            {
                keys.removeAt(i);
                break;
            }
    }

    for (const Move& move : moves)
    {
        Entity* e = entity(move.entity);
        if (!e)
            continue;
        auto track = e->tracks.find(move.property);
        if (track == e->tracks.end())
            continue;
        QVector<Keyframe>& keys = track->keys;
        Keyframe key = move.key;
        key.frame = move.target;
        int insert = 0;
        while (insert < int(keys.size()) && keys[insert].frame < key.frame)
            ++insert;
        if (insert < int(keys.size())
            && std::abs(keys[insert].frame - key.frame) < 1e-6)
            keys[insert] = key;
        else
            keys.insert(insert, key);
    }

    const bool moved = !moves.isEmpty();

    if (moved)
    {
        modified = true;
        emit changed();
    }
}

bool Project::setKeyframeEasing(EntityId id, const QString& property, double frame,
                                const EasingSpec& ease)
{
    Entity* e = entity(id);
    if (!e || e->locked)
        return false;

    auto it = e->tracks.find(property);
    if (it == e->tracks.end())
        return false;

    for (Keyframe& key : it->keys)
    {
        if (std::abs(key.frame - frame) < 1e-6)
        {
            if (key.ease == ease)
                return true;
            key.ease = ease;
            modified = true;
            emit changed();
            return true;
        }
    }
    return false;
}

bool Project::setKeyframeNote(EntityId id, const QString& property, double frame,
                              const QString& note)
{
    Entity* e = entity(id);
    if (!e)
        return false;
    auto it = e->tracks.find(property);
    if (it == e->tracks.end())
        return false;
    for (Keyframe& key : it->keys)
    {
        if (std::abs(key.frame - frame) >= 1e-6)
            continue;
        if (key.note == note)
            return true;
        key.note = note;
        modified = true;
        emit changed();
        return true;
    }
    return false;
}

QString Project::keyframeNote(EntityId id, const QString& property, double frame) const
{
    const Keyframe* key = keyframe(id, property, frame);
    return key ? key->note : QString();
}

const Keyframe* Project::keyframe(EntityId id, const QString& property, double frame) const
{
    const Entity* e = entity(id);
    if (!e)
        return nullptr;
    auto it = e->tracks.constFind(property);
    if (it == e->tracks.constEnd())
        return nullptr;
    for (const Keyframe& key : it->keys)
        if (std::abs(key.frame - frame) < 1e-6)
            return &key;
    return nullptr;
}

double Project::previousKeyframeFrame(EntityId id, double frame) const
{
    const Entity* e = entity(id);
    if (!e)
        return -1.0;
    double previous = -1.0;
    for (auto it = e->tracks.constBegin(); it != e->tracks.constEnd(); ++it)
    {
        for (const Keyframe& key : it->keys)
        {
            if (key.frame < frame - 1e-6 && key.frame > previous)
                previous = key.frame;
        }
    }
    return previous;
}

const Entity* Project::entity(EntityId id) const
{
    const int index = m_entityIndex.value(id, -1);
    if (index < 0 || index >= int(m_entities.size())
        || m_entities[index].id != id)
        return nullptr;
    return &m_entities[index];
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
    root[QStringLiteral("version")] = kProjectVersion;
    root[QStringLiteral("name")] = name;
    root[QStringLiteral("author")] = author;
    root[QStringLiteral("description")] = description;
    root[QStringLiteral("width")] = width;
    root[QStringLiteral("height")] = height;
    root[QStringLiteral("fps")] = fps;
    root[QStringLiteral("background")] = background.name(QColor::HexRgb);
    root[QStringLiteral("activeCamera")] = activeCamera;
    root[QStringLiteral("monitorView")] = monitorView;

    QJsonArray entities;
    for (const Entity& e : m_entities)
    {
        QJsonObject entity;
        entity[QStringLiteral("id")] = e.id;
        entity[QStringLiteral("type")] = typeToString(e.type);
        entity[QStringLiteral("parent")] = e.parent;
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
            else if (e.type == EntityType::BrandWall
                     && it.key() == QStringLiteral("logos"))
            {
                QJsonArray array;
                for (const QString& path : it.value().toStringList())
                    array.append(storeAssetPath(path));
                data[it.key()] = array;
            }
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
                if (key.ease.easing != Easing::Linear)
                    keyframe[QStringLiteral("ease")] = easingName(key.ease.easing);
                if (key.ease.easing == Easing::Bezier)
                {
                    QJsonArray handles;
                    handles.append(key.ease.inX);
                    handles.append(key.ease.inY);
                    handles.append(key.ease.outX);
                    handles.append(key.ease.outY);
                    keyframe[QStringLiteral("e")] = handles;
                }
                if (!key.note.isEmpty())
                    keyframe[QStringLiteral("note")] = key.note;
                keys.append(keyframe);
            }
            tracks[it.key()] = keys;
        }
        entity[QStringLiteral("tracks")] = tracks;

        entities.append(entity);
    }
    root[QStringLiteral("entities")] = entities;

    QJsonArray scenes;
    for (const Scene& scene : m_scenes)
    {
        QJsonObject object;
        object[QStringLiteral("id")] = scene.id;
        object[QStringLiteral("name")] = scene.name;
        object[QStringLiteral("startFrame")] = scene.startFrame;
        object[QStringLiteral("endFrame")] = scene.endFrame;
        object[QStringLiteral("folderId")] = scene.folderId;
        object[QStringLiteral("color")] = scene.color.name(QColor::HexArgb);
        object[QStringLiteral("note")] = scene.note;
        scenes.append(object);
    }
    root[QStringLiteral("scenes")] = scenes;

    QJsonArray annotations;
    for (const Annotation& annotation : m_annotations)
    {
        QJsonObject object;
        object[QStringLiteral("id")] = annotation.id;
        object[QStringLiteral("frame")] = annotation.frame;
        object[QStringLiteral("title")] = annotation.title;
        object[QStringLiteral("note")] = annotation.note;
        QJsonArray tags;
        for (const QString& tag : annotation.tags)
            tags.append(tag);
        object[QStringLiteral("tags")] = tags;
        object[QStringLiteral("color")] = annotation.color.name(QColor::HexArgb);
        annotations.append(object);
    }
    root[QStringLiteral("annotations")] = annotations;

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
            else if (t.type == EntityType::BrandWall
                     && it.key() == QStringLiteral("logos"))
            {
                QJsonArray array;
                for (const QString& path : it.value().toStringList())
                    array.append(storeAssetPath(path));
                data[it.key()] = array;
            }
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

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pvforge"))
    {
        if (error)
            *error = QStringLiteral("不是 PVForge 工程文件");
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt(1);
    if (version < 1)
    {
        if (error)
            *error = QStringLiteral("工程版本 %1 无效").arg(version);
        return false;
    }
    if (version > kProjectVersion)
    {
        if (error)
            *error = QStringLiteral("工程版本 %1 过新，当前最高支持 %2")
                         .arg(version)
                         .arg(kProjectVersion);
        return false;
    }

    // Set the project path first so relative asset paths can be resolved.
    filePath = path;

    name = root.value(QStringLiteral("name")).toString(QStringLiteral("未命名项目"));
    author = root.value(QStringLiteral("author")).toString();
    description = root.value(QStringLiteral("description")).toString();
    width = std::max(1, root.value(QStringLiteral("width")).toInt(1920));
    height = std::max(1, root.value(QStringLiteral("height")).toInt(1080));
    fps = std::clamp(root.value(QStringLiteral("fps")).toInt(30), 1, 240);
    background = QColor(root.value(QStringLiteral("background")).toString(QStringLiteral("#101014")));
    if (!background.isValid())
        background = QColor(0x10, 0x10, 0x14);
    activeCamera = root.value(QStringLiteral("activeCamera")).toInt(kInvalidEntity);
    monitorView = root.value(QStringLiteral("monitorView")).toInt(kWorkCameraView);

    m_entities.clear();
    m_entityIndex.clear();
    m_nextId = 1;

    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    QHash<QString, std::shared_ptr<AudioClip>> audioCache;
    for (const QJsonValue& value : entities)
    {
        const QJsonObject object = value.toObject();
        Entity e;
        e.id = object.value(QStringLiteral("id")).toInt();
        e.type = typeFromString(object.value(QStringLiteral("type")).toString());
        e.parent = object.value(QStringLiteral("parent")).toInt(kInvalidEntity);
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
            else if (e.type == EntityType::BrandWall
                     && it.key() == QStringLiteral("logos"))
            {
                QStringList paths;
                for (const QJsonValue& path : it.value().toArray())
                    paths.append(resolveAssetPath(path.toString()));
                e.data[it.key()] = paths;
            }
            else
                e.data[it.key()] = it.value().toVariant();
        }
        if (e.type == EntityType::BrandWall)
            refreshBrandWallImages(e);

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
                Keyframe frame;
                frame.frame = key.value(QStringLiteral("f")).toDouble();
                frame.value = key.value(QStringLiteral("v")).toDouble();
                frame.ease.easing = easingFromName(
                    key.value(QStringLiteral("ease")).toString(QStringLiteral("linear")));
                const QJsonArray handles = key.value(QStringLiteral("e")).toArray();
                if (handles.size() == 4)
                {
                    frame.ease.inX = handles.at(0).toDouble(1.0);
                    frame.ease.inY = handles.at(1).toDouble(0.0);
                    frame.ease.outX = handles.at(2).toDouble(0.0);
                    frame.ease.outY = handles.at(3).toDouble(1.0);
                }
                frame.note = key.value(QStringLiteral("note")).toString();
                track.keys.append(frame);
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
                if (!e.image.isNull())
                {
                    e.data[QStringLiteral("sourceWidth")] = e.image.width();
                    e.data[QStringLiteral("sourceHeight")] = e.image.height();
                }
            }
            else if (e.type == EntityType::Image)
            {
                e.image = QImage(e.sourcePath);
            }
            else if (e.type == EntityType::Audio)
            {
                auto cached = audioCache.constFind(e.sourcePath);
                if (cached != audioCache.constEnd())
                {
                    e.audio = cached.value();
                }
                else
                {
                    const AudioDecodeResult decoded = decodeAudioFile(e.sourcePath);
                    e.audio = decoded.clip;
                    audioCache.insert(e.sourcePath, e.audio);
                    if (e.audio)
                        e.data[QStringLiteral("duration")] = e.audio->duration;
                }
            }
        }

        m_nextId = std::max(m_nextId, e.id + 1);
        m_entities.append(e);
        m_entityIndex.insert(e.id, int(m_entities.size()) - 1);
    }

    // Validate parent references now that every entity has been loaded.
    for (Entity& e : m_entities)
    {
        if (e.parent == e.id || (e.parent != kInvalidEntity && !entity(e.parent)))
        {
            e.parent = kInvalidEntity;
        }
    }
    for (Entity& e : m_entities)
    {
        QSet<EntityId> seen;
        EntityId current = e.id;
        while (current != kInvalidEntity && !seen.contains(current))
        {
            seen.insert(current);
            const Entity* currentEntity = entity(current);
            if (!currentEntity)
                break;
            if (currentEntity->parent == e.id && current != e.id)
            {
                e.parent = kInvalidEntity;
                break;
            }
            current = currentEntity->parent;
        }
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
            // A video template remembers its extracted audio track so that
            // creating it after loading the project still adds both entities.
            if (tmpl.type == EntityType::Video || tmpl.type == EntityType::Audio)
            {
                auto cached = audioCache.constFind(tmpl.path);
                if (cached != audioCache.constEnd())
                {
                    tmpl.audio = cached.value();
                }
                else
                {
                    const AudioDecodeResult decoded = decodeAudioFile(tmpl.path);
                    tmpl.audio = decoded.clip;
                    if (tmpl.audio)
                        audioCache.insert(tmpl.path, tmpl.audio);
                }
            }

            for (const Entity& e : m_entities)
            {
                if (e.sourcePath != tmpl.path)
                    continue;
                if (tmpl.type == EntityType::Audio && e.audio)
                {
                    tmpl.audio = e.audio;
                    tmpl.thumbnail = renderAudioThumbnail(*e.audio, QSize(320, 80));
                    break;
                }
                if ((tmpl.type == EntityType::Image || tmpl.type == EntityType::Video)
                    && !e.image.isNull())
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
                else if (tmpl.type == EntityType::Audio)
                {
                    if (!tmpl.audio)
                    {
                        const AudioDecodeResult decoded = decodeAudioFile(tmpl.path);
                        if (decoded.clip)
                        {
                            tmpl.audio = decoded.clip;
                            audioCache.insert(tmpl.path, tmpl.audio);
                        }
                    }
                    if (tmpl.audio)
                    {
                        tmpl.thumbnail =
                            renderAudioThumbnail(*tmpl.audio, QSize(320, 80));
                    }
                }
            }
        }

        m_templates.append(tmpl);
    }

    // Drop camera references that no longer resolve (deleted or hidden).
    auto visibleCamera = [this](EntityId id) {
        const Entity* e = entity(id);
        return e && e->isCamera() && !isEffectivelyHidden(id);
    };
    if (!visibleCamera(activeCamera))
    {
        activeCamera = kInvalidEntity;
        for (const Entity& e : m_entities)
        {
            if (e.isCamera() && !isEffectivelyHidden(e.id))
            {
                activeCamera = e.id;
                break;
            }
        }
    }
    if (monitorView >= 0 && !visibleCamera(monitorView))
        monitorView = activeCamera != kInvalidEntity ? activeCamera : kWorkCameraView;
    else if (monitorView < 0 && monitorView != kWorkCameraView
             && monitorView != kWholeCanvasView)
        monitorView = kWorkCameraView;

    m_scenes.clear();
    const QJsonArray sceneValues = root.value(QStringLiteral("scenes")).toArray();
    for (const QJsonValue& value : sceneValues)
    {
        const QJsonObject object = value.toObject();
        Scene scene;
        scene.id = object.value(QStringLiteral("id")).toString();
        if (scene.id.isEmpty())
            scene.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        scene.name = object.value(QStringLiteral("name")).toString(QStringLiteral("场景"));
        scene.startFrame = object.value(QStringLiteral("startFrame")).toInt();
        scene.endFrame = object.value(QStringLiteral("endFrame")).toInt(scene.startFrame);
        scene.folderId = object.value(QStringLiteral("folderId")).toInt(kInvalidEntity);
        scene.color = QColor(object.value(QStringLiteral("color")).toString());
        if (!scene.color.isValid())
            scene.color = QColor(90, 170, 255);
        scene.note = object.value(QStringLiteral("note")).toString();

        const Entity* folder = entity(scene.folderId);
        if (!folder || !folder->isFolder())
            scene.folderId = kInvalidEntity;

        // A corrupt or hand-edited file must not break the load path: skip
        // malformed ranges instead of clamping them.
        if (scene.startFrame < 0 || scene.endFrame < scene.startFrame)
            continue;
        bool duplicate = false;
        bool overlap = false;
        for (const Scene& existing : m_scenes)
        {
            if (existing.id == scene.id)
                duplicate = true;
            if (scene.startFrame <= existing.endFrame
                && existing.startFrame <= scene.endFrame)
            {
                overlap = true;
            }
        }
        if (duplicate || overlap)
            continue;
        m_scenes.append(scene);
    }
    std::stable_sort(m_scenes.begin(), m_scenes.end(),
                     [](const Scene& a, const Scene& b) {
                         return a.startFrame < b.startFrame;
                     });

    m_annotations.clear();
    const QJsonArray annotationValues =
        root.value(QStringLiteral("annotations")).toArray();
    for (const QJsonValue& value : annotationValues)
    {
        const QJsonObject object = value.toObject();
        Annotation annotation;
        annotation.id = object.value(QStringLiteral("id")).toString();
        if (annotation.id.isEmpty())
            annotation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        annotation.frame = object.value(QStringLiteral("frame")).toInt();
        annotation.title = object.value(QStringLiteral("title")).toString();
        annotation.note = object.value(QStringLiteral("note")).toString();
        for (const QJsonValue& tag : object.value(QStringLiteral("tags")).toArray())
            annotation.tags.append(tag.toString());
        annotation.color = QColor(object.value(QStringLiteral("color")).toString());
        if (!annotation.color.isValid())
            annotation.color = QColor(255, 190, 60);
        if (annotation.frame < 0)
            continue;
        bool duplicate = false;
        for (const Annotation& existing : m_annotations)
            if (existing.id == annotation.id)
                duplicate = true;
        if (duplicate)
            continue;
        m_annotations.append(annotation);
    }
    std::stable_sort(m_annotations.begin(), m_annotations.end(),
                     [](const Annotation& a, const Annotation& b) {
                         return a.frame < b.frame;
                     });

    if (version < kProjectVersion && m_scenes.isEmpty())
    {
        // v1-v3 projects had no scene concept. Wrap every root entity in one
        // default scene so later UI/MCP work has a context. The folder keeps
        // the identity transform (makeBase defaults), so reparenting does not
        // move anything. When the project has no content at all, use the
        // single frame 0..0 instead of a synthetic 2-second range: the scene
        // then matches timelineLength() and still exists for later editing.
        Entity folder = makeBase(EntityType::Folder, QStringLiteral("默认场景"));
        const EntityId folderId = folder.id;
        for (Entity& e : m_entities)
            if (e.parent == kInvalidEntity)
                e.parent = folderId;
        m_entities.append(folder);
        m_entityIndex.insert(folderId, int(m_entities.size()) - 1);
        ++m_nextId;

        Scene scene;
        scene.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        scene.name = QStringLiteral("默认场景");
        scene.startFrame = 0;
        scene.endFrame = timelineLength();
        scene.folderId = folderId;
        m_scenes.append(scene);
    }

    filePath = path;
    modified = false;
    emit changed();
    return true;
}

} // namespace pvf
