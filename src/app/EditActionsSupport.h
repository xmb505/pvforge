#pragma once

// Internal helpers shared by the EditActions translation units.  They used to
// live in an anonymous namespace inside EditActions.cpp; splitting the file
// moved them here as inline functions so every part sees the same rules and
// the public EditActions API stays untouched.

#include "EditActions.h"

#include "model/Query.h"
#include "next/core/EntityLock.h"

#include <QSet>
#include <QString>

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

inline bool solverStartsAt(const model::Entity& entity, int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.startFrame == frame)
            return true;
    }
    return false;
}

// An ordinary whole-frame key inside an explicitly ended solver segment is
// invalid; the start key is the segment's initial state and stays editable.
// A key at frame == endFrame is part of the solved band, so it is rejected
// too.  Automatic segments derive their end from the first key after the
// start, so a new key is the end marker rather than an illegal key inside the
// segment.
inline bool keyframeBlockedAt(const model::Entity& entity, int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.endFrame < 0)
            continue;
        if (frame > solver.startFrame && frame <= solver.endFrame)
            return true;
    }
    return false;
}

inline model::Keyframe keyframeFromSample(const model::KeyframeSample& sample,
                                          int frame)
{
    model::Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.x = sample.x;
    keyframe.y = sample.y;
    keyframe.rotation = sample.rotation;
    keyframe.scaleX = sample.scaleX;
    keyframe.scaleY = sample.scaleY;
    keyframe.opacity = sample.opacity;
    keyframe.visible = sample.visible;
    keyframe.physicsInteract = sample.physicsInteract;
    keyframe.content = sample.content;
    keyframe.htmlVars = sample.htmlVars;
    return keyframe;
}

// Copies every animation parameter, leaving frame/easing/note/extras alone.
inline void copyAnimationParams(const model::Keyframe& source,
                                model::Keyframe* target)
{
    target->x = source.x;
    target->y = source.y;
    target->rotation = source.rotation;
    target->scaleX = source.scaleX;
    target->scaleY = source.scaleY;
    target->opacity = source.opacity;
    target->visible = source.visible;
    target->physicsInteract = source.physicsInteract;
    target->content = source.content;
    target->htmlVars = source.htmlVars;
}

// Content animation parameters only make sense between entities with the same
// content schema.  HTML variables additionally belong to one asset manifest.
inline bool contentSchemasCompatible(const model::Content& source,
                                     const model::Content& target)
{
    if (source.kind != target.kind)
        return false;
    if (source.kind != ContentKind::Asset)
        return true;
    if (source.asset.type != target.asset.type)
        return false;
    if (source.asset.type == AssetType::Html)
        return source.asset.hash == target.asset.hash;
    return true;
}

inline bool keyframeParamsCompatible(const model::Entity& source,
                                     const model::Entity& target)
{
    return contentSchemasCompatible(source.content, target.content);
}

inline model::Keyframe* mutableKeyframeAt(QVector<model::Keyframe>& keyframes,
                                          int frame)
{
    for (model::Keyframe& keyframe : keyframes)
    {
        if (keyframe.frame == frame)
            return &keyframe;
        if (keyframe.frame > frame)
            break;
    }
    return nullptr;
}

} // namespace edit_detail
} // namespace pvf
