#include "EditActions.h"

#include "EditActionsSupport.h"
#include "EditorSession.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"

#include <QSet>

#include <algorithm>
#include <climits>

namespace pvf {

using namespace edit_detail;

bool EditActions::planKeyframe(const Uuid& uuid, const QString& trackName,
                               int frame, EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (next::isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能编辑关键帧")
                            .arg(displayName(it.value())));
        return false;
    }

    const QString track = trackName.trimmed();
    if (track.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的轨道"));
        return false;
    }
    if (!it.value().tracks.contains(track) && !trackNames(uuid).contains(track))
    {
        setError(error, QStringLiteral("实体 %1 不支持轨道 %2")
                            .arg(displayName(it.value()), track));
        return false;
    }
    if (frame < 0 || frame >= it.value().duration())
    {
        setError(error, QStringLiteral("关键帧 %1 超出实体范围 0..%2")
                            .arg(frame)
                            .arg(it.value().duration() - 1));
        return false;
    }
    if (trackBlockedAt(it.value(), track, frame))
    {
        setError(error, QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动，不能写入普通关键帧")
                            .arg(track)
                            .arg(frame));
        return false;
    }

    model::Project working = project;
    model::Entity& entity = working.entities[uuid];
    const double value = trackValueAt(entity, track, frame);
    model::Track& target = entity.tracks[track];
    bool updated = false;
    for (model::Keyframe& key : target.keys)
    {
        if (key.frame == frame)
        {
            key.value = value;
            updated = true;
            break;
        }
    }
    if (!updated)
    {
        model::Keyframe key;
        key.frame = frame;
        key.value = value;
        target.keys.append(key);
    }
    sortTrack(target);

    QString validationError;
    if (!model::validateEntity(entity, &validationError))
    {
        setError(error, QStringLiteral("关键帧校验失败：%1").arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = {uuid};
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("设置关键帧 %1").arg(track);
    return true;
}

bool EditActions::planMoveKeyframes(const QVector<KeyRef>& keys, int delta,
                                    EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (keys.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    struct Move
    {
        Uuid entity;
        QString track;
        int from = 0;
        model::Keyframe key;
    };
    QVector<Move> moves;
    QSet<Uuid> entities;

    for (const KeyRef& ref : keys)
    {
        const auto entityIt = project.entities.constFind(ref.entity);
        if (entityIt == project.entities.constEnd())
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(ref.entity));
            return false;
        }
        const model::Entity& entity = entityIt.value();
        if (next::isEntityLocked(project, ref.entity))
        {
            setError(error,
                     QStringLiteral("实体 %1 或其祖先已锁定，不能移动关键帧")
                         .arg(displayName(entity)));
            return false;
        }
        if (ref.frame < 0 || ref.frame >= entity.duration())
        {
            setError(error, QStringLiteral("关键帧 %1 超出实体范围").arg(ref.frame));
            return false;
        }

        bool found = false;
        auto collect = [&](const QString& trackName, const model::Track& track) {
            for (const model::Keyframe& key : track.keys)
            {
                if (key.frame != ref.frame)
                    continue;
                found = true;
                moves.append(Move{ref.entity, trackName, key.frame, key});
            }
        };
        if (ref.track.isEmpty())
        {
            for (auto track = entity.tracks.constBegin();
                 track != entity.tracks.constEnd(); ++track)
                collect(track.key(), track.value());
        }
        else
        {
            const auto track = entity.tracks.constFind(ref.track);
            if (track != entity.tracks.constEnd())
                collect(ref.track, track.value());
        }
        if (!found)
        {
            setError(error, QStringLiteral("第 %1 帧没有关键帧（%2）")
                                .arg(ref.frame)
                                .arg(keyLabel(ref)));
            return false;
        }
        entities.insert(ref.entity);
    }

    int minFrame = INT_MAX;
    int maxFrame = INT_MIN;
    int duration = INT_MAX;
    for (const Move& move : moves)
    {
        minFrame = std::min(minFrame, move.from);
        maxFrame = std::max(maxFrame, move.from);
        duration = std::min(duration,
                            project.entities.value(move.entity).duration());
    }
    delta = std::clamp(delta, -minFrame, duration - 1 - maxFrame);

    if (delta == 0)
    {
        plan->project = project;
        plan->upserts.clear();
        plan->deletes.clear();
        plan->projectChanged = false;
        plan->message = QStringLiteral("移动关键帧");
        return true;
    }

    for (const Move& move : moves)
    {
        const model::Entity& entity = project.entities.value(move.entity);
        const int target = move.from + delta;
        if (solverStartKey(entity, move.track, move.from)
            || solverStartKey(entity, move.track, target))
        {
            setError(error,
                     QStringLiteral("求解段开始帧的关键帧不能移动，请先删除求解段"));
            return false;
        }
        if (trackBlockedAt(entity, move.track, target))
        {
            setError(error, QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动")
                                .arg(move.track)
                                .arg(target));
            return false;
        }
    }

    model::Project working = project;
    for (const Uuid& uuid : entities)
    {
        model::Entity& entity = working.entities[uuid];
        QHash<QString, QSet<int>> sources;
        QHash<QString, QSet<int>> targets;
        for (const Move& move : moves)
        {
            if (move.entity != uuid)
                continue;
            sources[move.track].insert(move.from);
            targets[move.track].insert(move.from + delta);
        }
        for (auto track = entity.tracks.begin(); track != entity.tracks.end();
             ++track)
        {
            const QSet<int> trackSources = sources.value(track.key());
            const QSet<int> trackTargets = targets.value(track.key());
            if (trackSources.isEmpty() && trackTargets.isEmpty())
                continue;
            QVector<model::Keyframe>& trackKeys = track->keys;
            trackKeys.erase(std::remove_if(trackKeys.begin(), trackKeys.end(),
                                           [&trackSources](const model::Keyframe& key) {
                                               return trackSources.contains(key.frame);
                                           }),
                            trackKeys.end());
            trackKeys.erase(std::remove_if(trackKeys.begin(), trackKeys.end(),
                                           [&trackTargets](const model::Keyframe& key) {
                                               return trackTargets.contains(key.frame);
                                           }),
                            trackKeys.end());
        }
        for (const Move& move : moves)
        {
            if (move.entity != uuid)
                continue;
            model::Keyframe key = move.key;
            key.frame += delta;
            entity.tracks[move.track].keys.append(key);
        }
        for (auto track = entity.tracks.begin(); track != entity.tracks.end();
             ++track)
            sortTrack(track.value());

        QString validationError;
        if (!model::validateEntity(entity, &validationError))
        {
            setError(error,
                     QStringLiteral("移动后关键帧校验失败：%1").arg(validationError));
            return false;
        }
    }

    plan->project = std::move(working);
    plan->upserts = QVector<Uuid>(entities.begin(), entities.end());
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("移动关键帧");
    return true;
}

bool EditActions::planDeleteKeyframes(const QVector<KeyRef>& keys,
                                      EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (keys.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    model::Project working = project;
    QSet<Uuid> entities;
    bool changed = false;

    for (const KeyRef& ref : keys)
    {
        const auto entityIt = working.entities.constFind(ref.entity);
        if (entityIt == working.entities.constEnd())
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(ref.entity));
            return false;
        }
        if (next::isEntityLocked(project, ref.entity))
        {
            setError(error,
                     QStringLiteral("实体 %1 或其祖先已锁定，不能删除关键帧")
                         .arg(displayName(entityIt.value())));
            return false;
        }
        model::Entity& entity = working.entities[ref.entity];

        auto eraseAt = [&](const QString& trackName, model::Track& track) {
            QVector<model::Keyframe> kept;
            kept.reserve(track.keys.size());
            for (const model::Keyframe& key : track.keys)
            {
                if (key.frame != ref.frame)
                {
                    kept.append(key);
                    continue;
                }
                if (solverStartKey(entity, trackName, key.frame))
                {
                    setError(error,
                             QStringLiteral("求解段开始帧的关键帧不能删除，请先删除求解段"));
                    return false;
                }
                changed = true;
            }
            track.keys = kept;
            return true;
        };

        if (ref.track.isEmpty())
        {
            for (auto track = entity.tracks.begin(); track != entity.tracks.end();)
            {
                if (!eraseAt(track.key(), track.value()))
                    return false;
                if (track->keys.isEmpty())
                    track = entity.tracks.erase(track);
                else
                    ++track;
            }
        }
        else
        {
            auto track = entity.tracks.find(ref.track);
            if (track == entity.tracks.end())
                continue;
            if (!eraseAt(ref.track, track.value()))
                return false;
            if (track->keys.isEmpty())
                entity.tracks.erase(track);
        }
        entities.insert(ref.entity);
    }

    if (!changed)
    {
        setError(error, QStringLiteral("没有可删除的关键帧"));
        return false;
    }

    for (const Uuid& uuid : entities)
    {
        QString validationError;
        if (!model::validateEntity(working.entities[uuid], &validationError))
        {
            setError(error,
                     QStringLiteral("删除后关键帧校验失败：%1").arg(validationError));
            return false;
        }
    }

    plan->project = std::move(working);
    plan->upserts = QVector<Uuid>(entities.begin(), entities.end());
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("删除关键帧");
    return true;
}

bool EditActions::planSetKeyframeEasing(const QVector<KeyRef>& keys,
                                        const model::EasingSpec& easing,
                                        EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (keys.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    model::Project working = project;
    QSet<Uuid> entities;
    bool changed = false;

    for (const KeyRef& ref : keys)
    {
        const auto entityIt = working.entities.constFind(ref.entity);
        if (entityIt == working.entities.constEnd())
        {
            setError(error, QStringLiteral("实体不存在：%1").arg(ref.entity));
            return false;
        }
        if (next::isEntityLocked(project, ref.entity))
        {
            setError(error,
                     QStringLiteral("实体 %1 或其祖先已锁定，不能修改关键帧缓动")
                         .arg(displayName(entityIt.value())));
            return false;
        }
        model::Entity& entity = working.entities[ref.entity];

        auto applyTrack = [&](const QString& trackName, model::Track& track) {
            Q_UNUSED(trackName);
            for (model::Keyframe& key : track.keys)
            {
                if (key.frame != ref.frame)
                    continue;
                key.easing = easing;
                changed = true;
            }
        };
        if (ref.track.isEmpty())
        {
            for (auto track = entity.tracks.begin(); track != entity.tracks.end();
                 ++track)
                applyTrack(track.key(), track.value());
        }
        else
        {
            auto track = entity.tracks.find(ref.track);
            if (track != entity.tracks.end())
                applyTrack(ref.track, track.value());
        }
        entities.insert(ref.entity);
    }

    if (!changed)
    {
        setError(error, QStringLiteral("没有可修改缓动的关键帧"));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = QVector<Uuid>(entities.begin(), entities.end());
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("修改关键帧缓动");
    return true;
}

bool EditActions::planPasteKeyframes(const Uuid& uuid,
                                     const QVector<TrackKey>& keys,
                                     EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (keys.isEmpty())
    {
        setError(error, QStringLiteral("剪贴板没有关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto entityIt = project.entities.constFind(uuid);
    if (entityIt == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (next::isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能粘贴关键帧")
                            .arg(displayName(entityIt.value())));
        return false;
    }

    const model::Entity& source = entityIt.value();
    model::Project working = project;
    model::Entity& entity = working.entities[uuid];
    for (const TrackKey& item : keys)
    {
        if (item.track.isEmpty())
        {
            setError(error, QStringLiteral("粘贴的关键帧缺少轨道"));
            return false;
        }
        if (item.key.frame < 0 || item.key.frame >= source.duration())
        {
            setError(error, QStringLiteral("关键帧 %1 超出实体范围 0..%2")
                                .arg(item.key.frame)
                                .arg(source.duration() - 1));
            return false;
        }
        if (trackBlockedAt(source, item.track, item.key.frame))
        {
            setError(error, QStringLiteral("轨道 %1 的第 %2 帧由求解段驱动")
                                .arg(item.track)
                                .arg(item.key.frame));
            return false;
        }
        model::Track& track = entity.tracks[item.track];
        track.keys.erase(std::remove_if(track.keys.begin(), track.keys.end(),
                                        [&item](const model::Keyframe& key) {
                                            return key.frame == item.key.frame;
                                        }),
                         track.keys.end());
        track.keys.append(item.key);
        sortTrack(track);
    }

    QString validationError;
    if (!model::validateEntity(entity, &validationError))
    {
        setError(error, QStringLiteral("粘贴后关键帧校验失败：%1").arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = {uuid};
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("粘贴关键帧");
    return true;
}

bool EditActions::planSplit(const Uuid& uuid, int frame, EditPlan* plan,
                            QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }

    model::Project working;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    Uuid rightUuid;
    if (!next::splitEntityAt(m_session->project(), m_session->assets(), uuid,
                             frame, &working, &upserts, &deletes, &rightUuid,
                             error))
        return false;

    plan->project = std::move(working);
    plan->upserts = std::move(upserts);
    plan->deletes = std::move(deletes);
    plan->projectChanged = false;
    plan->message = QStringLiteral("切分片段");
    return true;
}

} // namespace pvf
