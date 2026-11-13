#pragma once

// Internal helpers shared by the EditActions translation units.  They used to
// live in an anonymous namespace inside EditActions.cpp; splitting the file
// moved them here as inline functions so every part sees the same rules and
// the public EditActions API stays untouched.

#include "EditActions.h"

#include "model/Query.h"
#include "next/core/EntityLock.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace pvf {
namespace edit_detail {

inline void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

inline QString displayName(const model::Entity& entity)
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

inline void collectSubtree(const model::Project& project, const Uuid& uuid,
                           QVector<Uuid>* out, QSet<Uuid>* visited)
{
    if (visited->contains(uuid))
        return;
    visited->insert(uuid);
    if (!project.entities.contains(uuid))
        return;
    out->append(uuid);
    for (const Uuid& child : model::childrenOf(project, uuid))
        collectSubtree(project, child, out, visited);
}

inline QVector<Uuid> subtree(const model::Project& project, const Uuid& uuid)
{
    QVector<Uuid> result;
    QSet<Uuid> visited;
    collectSubtree(project, uuid, &result, &visited);
    return result;
}

inline bool hasAncestorIn(const model::Project& project, const Uuid& uuid,
                          const QVector<Uuid>& ancestors)
{
    Uuid parent = project.entities.value(uuid).parentUuid;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (ancestors.contains(parent))
            return true;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return false;
}

inline bool isDescendant(const model::Project& project, const Uuid& candidate,
                         const Uuid& ancestor)
{
    if (candidate == ancestor)
        return true;
    Uuid parent = project.entities.value(candidate).parentUuid;
    QSet<Uuid> seen;
    while (!parent.isEmpty() && !seen.contains(parent))
    {
        if (parent == ancestor)
            return true;
        seen.insert(parent);
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
            break;
        parent = it.value().parentUuid;
    }
    return false;
}

// Roots may contain descendants of another root; only the topmost one moves.
inline QVector<Uuid> uniqueRoots(const model::Project& project,
                                 const QVector<Uuid>& roots, QString* error)
{
    QVector<Uuid> result;
    for (const Uuid& uuid : roots)
    {
        if (!project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
            return {};
        }
        if (result.contains(uuid) || hasAncestorIn(project, uuid, result))
            continue;
        result.append(uuid);
    }
    if (result.isEmpty() && !roots.isEmpty())
        setError(error, QStringLiteral("没有可编辑的实体"));
    return result;
}

inline QString lockedSubtreeError(const model::Project& project,
                                  const QVector<Uuid>& nodes)
{
    for (const Uuid& uuid : nodes)
    {
        if (next::isEntityLocked(project, uuid))
        {
            return QStringLiteral("实体 %1 或其祖先已锁定，不能编辑")
                .arg(displayName(project.entities.value(uuid)));
        }
    }
    return QString();
}

inline void addUpsert(QVector<Uuid>* upserts, QSet<Uuid>* seen, const Uuid& uuid)
{
    if (seen->contains(uuid))
        return;
    seen->insert(uuid);
    upserts->append(uuid);
}

inline int parentStart(const model::Project& project, const model::Entity& entity)
{
    if (entity.parentUuid.isEmpty())
        return 0;
    const auto it = project.entities.constFind(entity.parentUuid);
    return it == project.entities.constEnd() ? 0 : it.value().start;
}

inline QStringList solverDrivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

inline bool solverStartKey(const model::Entity& entity, const QString& track,
                           int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.startFrame == frame
            && solverDrivenTracks(solver.effect).contains(track))
            return true;
    }
    return false;
}

// A normal key inside an explicitly ended solver segment is invalid; the
// start key is the segment's initial state and stays editable.  Automatic
// segments end one frame before the first key created after the start, so a
// new key is the end marker rather than an illegal key inside the segment.
inline bool trackBlockedAt(const model::Entity& entity, const QString& track,
                           int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (!solverDrivenTracks(solver.effect).contains(track))
            continue;
        if (solver.endFrame < 0)
            continue;
        if (frame > solver.startFrame && frame <= solver.endFrame)
            return true;
    }
    return false;
}

inline bool stepTrack(const QString& track)
{
    return track == QLatin1String("visible")
           || track == QLatin1String("physicsInteract");
}

inline std::optional<double> staticTrackValue(const model::Entity& entity,
                                              const QString& track)
{
    const model::Transform& transform = entity.transform;
    if (track == QLatin1String("x"))
        return transform.x;
    if (track == QLatin1String("y"))
        return transform.y;
    if (track == QLatin1String("rotation"))
        return transform.rotation;
    if (track == QLatin1String("scaleX"))
        return transform.scaleX;
    if (track == QLatin1String("scaleY"))
        return transform.scaleY;
    if (track == QLatin1String("pivotX"))
        return transform.pivotX;
    if (track == QLatin1String("pivotY"))
        return transform.pivotY;
    if (track == QLatin1String("opacity"))
        return transform.opacity;
    if (track == QLatin1String("visible"))
        return entity.visible ? 1.0 : 0.0;
    if (track == QLatin1String("physicsInteract"))
        return entity.physicsInteract ? 1.0 : 0.0;

    const model::Content& content = entity.content;
    if (content.kind == ContentKind::Text)
    {
        const model::TextContent& text = content.text;
        if (track == QLatin1String("fontSize"))
            return text.pixelSize;
        if (track == QLatin1String("lineHeight"))
            return text.lineHeight;
        if (track == QLatin1String("letterSpacing"))
            return text.letterSpacing;
        if (track == QLatin1String("strokeWidth"))
            return text.strokeWidth;
        if (track == QLatin1String("padding"))
            return text.padding;
        if (track == QLatin1String("boxWidth"))
            return text.boxWidth;
        if (track == QLatin1String("boxHeight"))
            return text.boxHeight;
    }
    else if (content.kind == ContentKind::Vector)
    {
        const model::VectorContent& vector = content.vector;
        if (track == QLatin1String("width"))
            return vector.width;
        if (track == QLatin1String("height"))
            return vector.height;
        if (track == QLatin1String("cornerRadius"))
            return vector.cornerRadius;
        if (track == QLatin1String("strokeWidth"))
            return vector.strokeWidth;
    }
    else if (content.kind == ContentKind::Asset
             && content.asset.type == AssetType::Audio
             && track == QLatin1String("volume"))
    {
        return content.asset.volume;
    }
    return std::nullopt;
}

inline double trackValueAt(const model::Entity& entity, const QString& track,
                           int frame)
{
    const auto it = entity.tracks.constFind(track);
    if (it != entity.tracks.constEnd())
    {
        if (const auto value =
                model::evaluateTrack(it.value(), frame, stepTrack(track)))
            return *value;
    }
    return staticTrackValue(entity, track).value_or(0.0);
}

inline void sortTrack(model::Track& track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const model::Keyframe& a, const model::Keyframe& b) {
                  return a.frame < b.frame;
              });
}

inline QString keyLabel(const KeyRef& key)
{
    return key.track.isEmpty() ? QStringLiteral("全部轨道") : key.track;
}

} // namespace edit_detail
} // namespace pvf
