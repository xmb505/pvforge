#include "TimelinePanel.h"

#include "app/EditorSession.h"
#include "model/Project.h"
#include "model/Uuid.h"

#include <QMenu>

#include <algorithm>

namespace pvf {

bool TimelinePanel::addKeyframeAt(EntityId id, int frame, const QString& track)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return false;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return false;
    const int relative = frame - entity->start;
    QString error;
    if (!m_session->editActions()->addKeyframe(uuid, track, relative, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return false;
    }
    m_selectedTrack = track;
    m_selectedKeyframes.clear();
    m_selectedKeyframes.append(KeyframeSelection{id, double(frame), track});
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("设置关键帧 %1").arg(track));
    refreshFromSession();
    return true;
}

QStringList TimelinePanel::tracksAtFrame(EntityId id, int frame) const
{
    QStringList result;
    const Entity* entity = entityById(id);
    if (!entity)
        return result;
    for (auto track = entity->tracks.constBegin(); track != entity->tracks.constEnd();
         ++track)
    {
        for (const Keyframe& key : track->keys)
        {
            if (int(std::lround(key.frame)) == frame)
            {
                result.append(track.key());
                break;
            }
        }
    }
    return result;
}

void TimelinePanel::promptTrackAndAddKey(EntityId id, int frame,
                                         const QPoint& globalPos)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const Uuid uuid = uuidForEntityId(id);
    const QStringList tracks = m_session->editActions()->trackNames(uuid);
    if (tracks.isEmpty())
    {
        emit editFailed(QStringLiteral("这个片段没有可打关键帧的轨道"));
        return;
    }

    QMenu menu(this);
    menu.setTitle(QStringLiteral("选择轨道"));
    QHash<QAction*, QString> actions;
    for (const QString& track : tracks)
    {
        QAction* action = menu.addAction(track);
        actions.insert(action, track);
    }
    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    const QString track = actions.value(chosen);
    if (track.isEmpty())
        return;
    m_selectedTrack = track;
    addKeyframeAt(id, frame, track);
}

bool TimelinePanel::keyframeSelectedAt(EntityId id, int frame) const
{
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        if (selection.entity == id
            && int(std::lround(selection.frame)) == frame)
            return true;
    }
    return false;
}

QVector<KeyRef> TimelinePanel::selectedKeyRefs() const
{
    QVector<KeyRef> result;
    if (!sessionMode() || !m_session)
        return result;
    const model::Project& project = m_session->project();
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        const Uuid uuid = uuidForEntityId(selection.entity);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;
        KeyRef ref;
        ref.entity = uuid;
        ref.track = selection.track;
        ref.frame = int(std::lround(selection.frame)) - it.value().start;
        result.append(ref);
    }
    return result;
}

void TimelinePanel::deleteSelectedKeyframes()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const QVector<KeyRef> refs = selectedKeyRefs();
    if (refs.isEmpty())
        return;
    QString error;
    if (!m_session->editActions()->deleteKeyframes(refs, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_selectedKeyframes.clear();
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("删除关键帧"));
    refreshFromSession();
}

void TimelinePanel::copySelectedKeyframes()
{
    if (!sessionMode() || !m_session)
        return;
    const QVector<KeyRef> refs = selectedKeyRefs();
    if (refs.isEmpty())
        return;

    const model::Project& project = m_session->project();
    QVector<TrackKey> copied;
    QString entity;
    for (const KeyRef& ref : refs)
    {
        if (entity.isEmpty())
            entity = ref.entity;
        else if (entity != ref.entity)
        {
            emit editFailed(QStringLiteral("一次只能复制同一个实体的关键帧"));
            return;
        }
        const auto it = project.entities.constFind(ref.entity);
        if (it == project.entities.constEnd())
            continue;
        const model::Entity& source = it.value();
        auto collect = [&](const QString& trackName, const model::Track& track) {
            for (const model::Keyframe& key : track.keys)
            {
                if (key.frame != ref.frame)
                    continue;
                TrackKey item;
                item.track = trackName;
                item.key = key;
                copied.append(item);
            }
        };
        if (ref.track.isEmpty())
        {
            for (auto track = source.tracks.constBegin();
                 track != source.tracks.constEnd(); ++track)
                collect(track.key(), track.value());
        }
        else
        {
            const auto track = source.tracks.constFind(ref.track);
            if (track != source.tracks.constEnd())
                collect(ref.track, track.value());
        }
    }

    if (copied.isEmpty())
        return;
    m_keyClipboard = copied;
    m_keyClipboardEntity = entity;
    emit editCommitted(QStringLiteral("复制 %1 个关键帧").arg(copied.size()));
}

void TimelinePanel::pasteKeyframes()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    if (m_keyClipboard.isEmpty() || m_keyClipboardEntity.isEmpty())
        return;

    const Uuid target =
        m_selected != kInvalidEntity ? uuidForEntityId(m_selected)
                                     : m_keyClipboardEntity;
    if (target != m_keyClipboardEntity)
    {
        emit editFailed(QStringLiteral("关键帧只能粘贴回原实体"));
        return;
    }
    const auto entityIt = m_session->project().entities.constFind(target);
    if (entityIt == m_session->project().entities.constEnd())
        return;
    const model::Entity& entity = entityIt.value();
    const int duration = entity.duration();
    if (duration <= 0)
        return;

    int first = m_keyClipboard.first().key.frame;
    int last = first;
    for (const TrackKey& item : m_keyClipboard)
    {
        first = std::min(first, item.key.frame);
        last = std::max(last, item.key.frame);
    }
    const int span = last - first;
    if (span >= duration)
    {
        emit editFailed(QStringLiteral("关键帧片段超出实体时长，无法粘贴"));
        return;
    }

    const int relativePlayhead =
        std::clamp(m_session->playhead() - entity.start, 0, duration - 1);
    const int delta =
        std::clamp(relativePlayhead - first, -first, duration - 1 - last);

    QVector<TrackKey> placed;
    placed.reserve(m_keyClipboard.size());
    for (const TrackKey& item : m_keyClipboard)
    {
        TrackKey targetKey = item;
        targetKey.key.frame += delta;
        placed.append(targetKey);
    }

    QString error;
    if (!m_session->editActions()->pasteKeyframes(target, placed, &error))
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }

    m_selectedKeyframes.clear();
    QSet<QString> seen;
    const EntityId targetId =
        m_selected != kInvalidEntity ? m_selected : entityIdForUuid(target);
    for (const TrackKey& item : placed)
    {
        const QString key = QString::number(item.key.frame);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        m_selectedKeyframes.append(
            KeyframeSelection{targetId, double(entity.start + item.key.frame),
                              QString()});
    }
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("粘贴关键帧"));
    refreshFromSession();
}

} // namespace pvf
