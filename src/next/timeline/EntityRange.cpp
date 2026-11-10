#include "next/timeline/EntityRange.h"

#include "anim/Anim.h"
#include "render2/Render.h"

#include <QSet>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace pvf::next {

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString displayName(const model::Entity& entity)
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

QStringList drivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool isTimedAsset(const model::Entity& entity)
{
    return entity.content.kind == ContentKind::Asset
           && (entity.content.asset.type == AssetType::Video
               || entity.content.asset.type == AssetType::Audio);
}

int oldEffectiveEnd(const model::Entity& entity,
                    const model::SolverSegment& segment)
{
    const int last = std::max(0, entity.duration() - 1);
    return segment.endFrame < 0 ? last : std::min(last, segment.endFrame);
}

// Solver samples drive local x/y (parent space) or rotation.  Evaluation adds
// pivot/scale and every parent transform, so probe with a neutral scale/pivot
// to recover exactly the values the solver wrote into the driven tracks.
struct SolvedPose
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
};

bool solvedPoseAt(const model::Project& project,
                  const assets::AssetStore& assets, const Uuid& uuid,
                  int projectFrame, SolvedPose* pose, QString* error)
{
    model::Project probe = project;
    auto it = probe.entities.find(uuid);
    if (it == probe.entities.end())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }

    model::Entity& entity = it.value();
    entity.transform.scaleX = 1.0;
    entity.transform.scaleY = 1.0;
    entity.transform.pivotX = 0.0;
    entity.transform.pivotY = 0.0;
    entity.tracks.remove(QStringLiteral("scaleX"));
    entity.tracks.remove(QStringLiteral("scaleY"));
    entity.tracks.remove(QStringLiteral("pivotX"));
    entity.tracks.remove(QStringLiteral("pivotY"));

    render2::SceneWorld world(probe, assets);
    QTransform local = anim::evaluateEntity(probe, uuid, projectFrame, world).world;
    if (!entity.parentUuid.isEmpty()
        && probe.entities.contains(entity.parentUuid))
    {
        const QTransform parent =
            anim::evaluateEntity(probe, entity.parentUuid, projectFrame, world)
                .world;
        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (invertible)
            local = local * inverse;
    }

    pose->x = local.dx();
    pose->y = local.dy();
    pose->rotation = qRadiansToDegrees(std::atan2(local.m12(), local.m11()));
    return true;
}

bool collectSubtreeDeletes(const model::Project& project, const Uuid& uuid,
                           QVector<Uuid>& deletes, QSet<Uuid>& visited,
                           QString* error)
{
    if (visited.contains(uuid))
        return true;
    visited.insert(uuid);

    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 已锁定，不能裁剪或删除")
                            .arg(displayName(it.value())));
        return false;
    }

    deletes.append(uuid);
    const QVector<Uuid> children = model::childrenOf(project, uuid);
    for (const Uuid& child : children)
    {
        if (!collectSubtreeDeletes(project, child, deletes, visited, error))
            return false;
    }
    return true;
}

bool collectSubtreeDeletes(const model::Project& project, const Uuid& uuid,
                           QVector<Uuid>& deletes, QString* error)
{
    QSet<Uuid> visited;
    return collectSubtreeDeletes(project, uuid, deletes, visited, error);
}

bool trimNode(model::Project& project, const assets::AssetStore& assets,
              const Uuid& uuid, int newStart, int newEnd,
              QVector<Uuid>& upserts, QVector<Uuid>& deletes, QString* error)
{
    auto it = project.entities.find(uuid);
    if (it == project.entities.end())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }

    model::Entity& entity = it.value();
    const int oldStart = entity.start;
    const int oldEnd = entity.end;
    if (newStart >= newEnd)
    {
        setError(error, QStringLiteral("实体 %1 的新范围 [%2, %3) 无效")
                            .arg(displayName(entity))
                            .arg(newStart)
                            .arg(newEnd));
        return false;
    }

    if (isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能修改时间范围")
                            .arg(displayName(entity)));
        return false;
    }

    // The whole subtree is outside the new range: delete it explicitly.
    if (oldStart >= newEnd || oldEnd <= newStart)
        return collectSubtreeDeletes(project, uuid, deletes, error);

    if (newStart == oldStart && newEnd == oldEnd)
        return true;

    const int oldDuration = std::max(1, oldEnd - oldStart);
    const int deltaStart = newStart - oldStart;
    const int newDuration = newEnd - newStart;

    int newSourceIn = 0;
    if (isTimedAsset(entity))
    {
        const int frames = timedAssetDurationFrames(assets, entity);
        newSourceIn = entity.content.asset.sourceIn + deltaStart;
        if (newSourceIn < 0)
        {
            setError(error,
                     QStringLiteral("实体 %1 的源起点会变成 %2：素材开头没有可扩展的帧")
                         .arg(displayName(entity))
                         .arg(newSourceIn));
            return false;
        }
        if (frames > 0 && newSourceIn + newDuration > frames)
        {
            setError(error,
                     QStringLiteral("实体 %1 需要源素材 %2 帧，但源起点 %3 后只剩 %4 帧")
                         .arg(displayName(entity))
                         .arg(newDuration)
                         .arg(newSourceIn)
                         .arg(std::max(0, frames - newSourceIn)));
            return false;
        }
    }

    // A solver segment cut by the new start is removed.  Capture the pose it
    // held at the cut frame before any track is shifted.
    QSet<QString> cutTracks;
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (deltaStart <= 0 || solver.startFrame >= deltaStart
            || deltaStart > oldEffectiveEnd(entity, solver))
            continue;
        for (const QString& track : drivenTracks(solver.effect))
            cutTracks.insert(track);
    }

    SolvedPose cutPose;
    if (!cutTracks.isEmpty()
        && !solvedPoseAt(project, assets, uuid, newStart, &cutPose, error))
        return false;

    const bool trimmedEnd = newEnd < oldEnd;
    QVector<model::SolverSegment> shiftedSolvers;
    for (const model::SolverSegment& oldSolver : entity.solvers)
    {
        const int effectiveEnd = oldEffectiveEnd(entity, oldSolver);
        const bool cut = deltaStart > 0 && oldSolver.startFrame < deltaStart
                         && deltaStart <= effectiveEnd;
        if (cut)
            continue;

        model::SolverSegment shifted = oldSolver;
        shifted.startFrame -= deltaStart;
        if (shifted.endFrame != -1)
            shifted.endFrame -= deltaStart;
        else if (trimmedEnd)
            shifted.endFrame = newDuration - 1;

        if (shifted.startFrame >= newDuration)
            continue;
        if (shifted.endFrame != -1 && shifted.endFrame >= newDuration)
            shifted.endFrame = newDuration - 1;
        if (shifted.endFrame != -1 && shifted.endFrame < shifted.startFrame)
            continue;
        shiftedSolvers.append(shifted);
    }

    // Shift / crop every ordinary track.  Frame numbers are entity-relative.
    for (auto trackIt = entity.tracks.begin(); trackIt != entity.tracks.end();
         ++trackIt)
    {
        QVector<model::Keyframe> kept;
        kept.reserve(trackIt.value().keys.size());
        for (model::Keyframe key : trackIt.value().keys)
        {
            key.frame -= deltaStart;
            if (key.frame < 0 || key.frame >= newDuration)
                continue;
            kept.append(key);
        }
        trackIt.value().keys = kept;
    }

    // A segment cut by the new start freezes at frame 0 with the evaluated
    // pose, replacing whatever ordinary key landed there after the shift.
    for (const QString& track : cutTracks)
    {
        model::Track& target = entity.tracks[track];
        for (int i = target.keys.size() - 1; i >= 0; --i)
        {
            if (target.keys.at(i).frame == 0)
                target.keys.removeAt(i);
        }

        model::Keyframe key;
        key.frame = 0;
        if (track == QStringLiteral("x"))
            key.value = cutPose.x;
        else if (track == QStringLiteral("y"))
            key.value = cutPose.y;
        else
            key.value = cutPose.rotation;
        target.keys.append(key);
    }

    for (auto trackIt = entity.tracks.begin(); trackIt != entity.tracks.end();
         ++trackIt)
    {
        std::sort(trackIt.value().keys.begin(), trackIt.value().keys.end(),
                  [](const model::Keyframe& a, const model::Keyframe& b) {
                      return a.frame < b.frame;
                  });
    }

    if (isTimedAsset(entity))
        entity.content.asset.sourceIn = newSourceIn;
    entity.solvers = shiftedSolvers;
    entity.start = newStart;
    entity.end = newEnd;
    upserts.append(uuid);

    const QVector<Uuid> children = model::childrenOf(project, uuid);
    for (const Uuid& child : children)
    {
        const auto childIt = project.entities.constFind(child);
        if (childIt == project.entities.constEnd())
            continue;

        const int childStart = std::max(childIt.value().start, newStart);
        const int childEnd = std::min(childIt.value().end, newEnd);
        if (childStart >= childEnd)
        {
            if (!collectSubtreeDeletes(project, child, deletes, error))
                return false;
            continue;
        }
        if (!trimNode(project, assets, child, childStart, childEnd, upserts,
                      deletes, error))
            return false;
    }
    return true;
}

} // namespace

bool hasLockedAncestor(const model::Project& project, const Uuid& uuid)
{
    QSet<Uuid> visited;
    visited.insert(uuid);
    auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return false;

    Uuid parent = it.value().parentUuid;
    while (!parent.isEmpty() && !visited.contains(parent))
    {
        visited.insert(parent);
        const auto parentIt = project.entities.constFind(parent);
        if (parentIt == project.entities.constEnd())
            return false;
        if (parentIt.value().locked)
            return true;
        parent = parentIt.value().parentUuid;
    }
    return false;
}

bool isEntityLocked(const model::Project& project, const Uuid& uuid)
{
    const auto it = project.entities.constFind(uuid);
    if (it != project.entities.constEnd() && it.value().locked)
        return true;
    return hasLockedAncestor(project, uuid);
}

int timedAssetDurationFrames(const assets::AssetStore& assets,
                             const model::Entity& entity)
{
    if (!isTimedAsset(entity))
        return 0;
    const auto asset = assets.get(entity.content.asset.hash);
    if (!asset)
        return 0;
    return asset->metadata.value(QStringLiteral("durationFrames")).toInt(0);
}

bool applyEntityRange(const model::Project& project,
                      const assets::AssetStore& assets, const Uuid& uuid,
                      int newStart, int newEnd, model::Project* result,
                      QVector<Uuid>* upserts, QVector<Uuid>* deletes,
                      QString* error)
{
    if (error)
        error->clear();
    if (!result || !upserts || !deletes)
    {
        setError(error, QStringLiteral("内部错误：范围编辑输出为空"));
        return false;
    }

    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (newStart < 0)
    {
        setError(error, QStringLiteral("开始帧不能小于 0：%1").arg(newStart));
        return false;
    }
    if (newStart >= newEnd)
    {
        setError(error, QStringLiteral("开始帧 %1 必须小于结束帧 %2")
                            .arg(newStart)
                            .arg(newEnd));
        return false;
    }
    if (isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能修改时间范围")
                            .arg(displayName(it.value())));
        return false;
    }

    const Uuid parentUuid = it.value().parentUuid;
    if (!parentUuid.isEmpty())
    {
        const auto parent = project.entities.constFind(parentUuid);
        if (parent == project.entities.constEnd())
        {
            setError(error, QStringLiteral("实体 %1 的父实体不存在")
                                .arg(displayName(it.value())));
            return false;
        }
        if (newStart < parent.value().start || newEnd > parent.value().end)
        {
            setError(error,
                     QStringLiteral("实体 %1 的新范围 [%2, %3) 超出父实体 %4 的 [%5, %6)")
                         .arg(displayName(it.value()))
                         .arg(newStart)
                         .arg(newEnd)
                         .arg(displayName(parent.value()))
                         .arg(parent.value().start)
                         .arg(parent.value().end));
            return false;
        }
    }

    model::Project working = project;
    QVector<Uuid> localUpserts;
    QVector<Uuid> localDeletes;
    if (!trimNode(working, assets, uuid, newStart, newEnd, localUpserts,
                  localDeletes, error))
        return false;

    for (const Uuid& deleted : localDeletes)
        working.entities.remove(deleted);

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("范围编辑后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    *result = std::move(working);
    *upserts = localUpserts;
    *deletes = localDeletes;
    return true;
}

} // namespace pvf::next
