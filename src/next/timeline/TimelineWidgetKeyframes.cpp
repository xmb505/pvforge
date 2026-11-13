#include "next/timeline/TimelineWidget.h"

#include "TimelineWidgetInternal.h"
#include "next/core/Document.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <climits>

namespace pvf::next {

namespace {

QStringList drivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool solverStartKey(const model::Entity& entity, const QString& track, int frame)
{
    for (const auto& solver : entity.solvers)
    {
        if (solver.startFrame == frame
            && drivenTracks(solver.effect).contains(track))
            return true;
    }
    return false;
}

QStringList genericTransformTracks()
{
    return {QStringLiteral("x"),        QStringLiteral("y"),
            QStringLiteral("rotation"), QStringLiteral("scaleX"),
            QStringLiteral("scaleY"),   QStringLiteral("opacity")};
}

bool isStepTrack(const QString& track)
{
    return track == QStringLiteral("visible")
           || track == QStringLiteral("physicsInteract");
}

double staticTrackValue(const model::Entity& entity, const QString& track)
{
    const model::Transform& transform = entity.transform;
    if (track == QStringLiteral("x"))
        return transform.x;
    if (track == QStringLiteral("y"))
        return transform.y;
    if (track == QStringLiteral("rotation"))
        return transform.rotation;
    if (track == QStringLiteral("scaleX"))
        return transform.scaleX;
    if (track == QStringLiteral("scaleY"))
        return transform.scaleY;
    if (track == QStringLiteral("opacity"))
        return transform.opacity;
    if (track == QStringLiteral("pivotX"))
        return transform.pivotX;
    if (track == QStringLiteral("pivotY"))
        return transform.pivotY;
    if (track == QStringLiteral("visible"))
        return entity.visible ? 1.0 : 0.0;
    if (track == QStringLiteral("physicsInteract"))
        return entity.physicsInteract ? 1.0 : 0.0;
    return 0.0;
}

void setKeyframeValue(model::Track& track, int frame, double value)
{
    for (model::Keyframe& key : track.keys)
    {
        if (key.frame == frame)
        {
            key.value = value;
            return;
        }
    }

    model::Keyframe key;
    key.frame = frame;
    key.value = value;
    track.keys.append(key);
}

void removeKeyframeAt(model::Track& track, int frame)
{
    for (int i = track.keys.size() - 1; i >= 0; --i)
    {
        if (track.keys.at(i).frame == frame)
            track.keys.removeAt(i);
    }
}

void sortTrack(model::Track& track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const model::Keyframe& a, const model::Keyframe& b) {
                  return a.frame < b.frame;
              });
}

} // namespace

int TimelineWidget::keyframeAt(const QPoint& pos) const
{
    if (!m_document)
        return -1;

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return -1;

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
        return -1;

    const model::Entity& entity = it.value();
    const QRect bar = entityBarRect(uuid);
    if (bar.isEmpty() || !bar.contains(pos))
        return -1;

    // Diamonds sit on the vertical centre; keep the hit lane narrow enough to
    // leave the “+” button and trim edges usable.
    if (std::abs(pos.y() - bar.center().y()) > 5)
        return -1;

    int bestFrame = -1;
    int bestDistance = 6;
    for (int frame : mergedKeyFrames(entity))
    {
        const int x = xFromFrame(entity.start + frame);
        const int distance = std::abs(pos.x() - x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestFrame = frame;
        }
    }
    return bestFrame;
}

void TimelineWidget::clearKeyframeSelection()
{
    m_keyframeSelectionUuid.clear();
    m_selectedKeyframes.clear();
}

Uuid TimelineWidget::keyframeSelectionEntity() const
{
    if (!m_document)
        return Uuid();
    return m_document->selection() == m_keyframeSelectionUuid
               ? m_keyframeSelectionUuid
               : Uuid();
}

QString TimelineWidget::entityDisplayName(const model::Entity& entity) const
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

void TimelineWidget::insertKeyframesAt(const Uuid& uuid, int relativeFrame)
{
    if (!m_document)
        return;

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (detail::entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    const int duration = entity.duration();
    if (duration <= 0)
        return;
    const int frame = std::clamp(relativeFrame, 0, duration - 1);

    // Prefer the tracks that already animate this entity; only fall back to
    // the generic transform tracks when no track has keys yet.
    QStringList targets;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
         ++it)
    {
        if (!it.value().keys.isEmpty())
            targets.append(it.key());
    }
    const bool animated = !targets.isEmpty();
    if (!animated)
        targets = genericTransformTracks();

    const QHash<QString, model::Track> before = entity.tracks;

    int written = 0;
    int blocked = 0;
    for (const QString& trackName : targets)
    {
        if (detail::trackBlockedAt(entity, trackName, frame))
        {
            ++blocked;
            continue;
        }

        double value = staticTrackValue(entity, trackName);
        const auto trackIt = entity.tracks.constFind(trackName);
        if (animated && trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated =
                    trackIt.value().valueAt(frame, isStepTrack(trackName)))
                value = *evaluated;
        }

        setKeyframeValue(entity.tracks[trackName], frame, value);
        sortTrack(entity.tracks[trackName]);
        ++written;
    }

    if (written == 0)
    {
        entity.tracks = before;
        emit statusMessage(tr("该帧由求解段驱动，不能写入普通关键帧"));
        update();
        return;
    }

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: set keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧写入失败")
                               : tr("关键帧写入失败：%1").arg(error));
    }

    if (blocked > 0 && written > 0)
    {
        emit statusMessage(
            tr("求解段内的轨道已跳过，写入 %1 条轨道").arg(written));
    }

    update();
}

void TimelineWidget::deleteSelectedKeyframes()
{
    const Uuid uuid = keyframeSelectionEntity();
    if (uuid.isEmpty() || m_selectedKeyframes.isEmpty())
        return;

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (detail::entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
         ++it)
    {
        for (const model::Keyframe& key : it.value().keys)
        {
            if (m_selectedKeyframes.contains(key.frame)
                && solverStartKey(entity, it.key(), key.frame))
            {
                emit statusMessage(
                    tr("求解段开始帧的关键帧不能删除，请先删除求解段"));
                return;
            }
        }
    }

    const QHash<QString, model::Track> before = entity.tracks;
    bool changed = false;
    for (auto it = entity.tracks.begin(); it != entity.tracks.end();)
    {
        QVector<model::Keyframe> kept;
        kept.reserve(it.value().keys.size());
        for (const model::Keyframe& key : it.value().keys)
        {
            if (m_selectedKeyframes.contains(key.frame))
                changed = true;
            else
                kept.append(key);
        }

        if (kept.isEmpty())
            it = entity.tracks.erase(it);
        else
        {
            it.value().keys = kept;
            ++it;
        }
    }

    if (!changed)
        return;

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: delete keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧删除失败")
                               : tr("关键帧删除失败：%1").arg(error));
    }

    clearKeyframeSelection();
    update();
}

void TimelineWidget::copySelectedKeyframes()
{
    const Uuid uuid = keyframeSelectionEntity();
    if (uuid.isEmpty() || m_selectedKeyframes.isEmpty())
        return;

    const auto entityIt = m_document->project().entities.constFind(uuid);
    if (entityIt == m_document->project().entities.constEnd())
        return;

    m_keyClipboard.clear();
    m_keyClipboardUuid = uuid;
    for (auto it = entityIt.value().tracks.constBegin();
         it != entityIt.value().tracks.constEnd(); ++it)
    {
        for (const model::Keyframe& key : it.value().keys)
        {
            if (!m_selectedKeyframes.contains(key.frame))
                continue;
            CopiedKey copied;
            copied.track = it.key();
            copied.frame = key.frame;
            copied.value = key.value;
            copied.easing = key.easing;
            copied.note = key.note;
            m_keyClipboard.append(copied);
        }
    }

    if (m_keyClipboard.isEmpty())
        return;
    emit statusMessage(
        tr("已复制 %1 个关键帧").arg(m_keyClipboard.size()));
}

void TimelineWidget::pasteKeyframes()
{
    if (!m_document || m_keyClipboard.isEmpty())
        return;

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return;
    if (uuid != m_keyClipboardUuid)
    {
        emit statusMessage(tr("关键帧只能粘贴回原实体"));
        return;
    }

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (detail::entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    const int duration = entity.duration();
    if (duration <= 0)
        return;

    int firstFrame = m_keyClipboard.first().frame;
    int lastFrame = firstFrame;
    for (const CopiedKey& copied : m_keyClipboard)
    {
        firstFrame = std::min(firstFrame, copied.frame);
        lastFrame = std::max(lastFrame, copied.frame);
    }

    const int span = lastFrame - firstFrame;
    if (span >= duration)
    {
        emit statusMessage(tr("关键帧片段超出实体时长，无法粘贴"));
        return;
    }

    const int base = std::clamp(
        playhead() - entity.start, 0, std::max(0, duration - 1 - span));

    QVector<CopiedKey> placed;
    placed.reserve(m_keyClipboard.size());
    for (const CopiedKey& copied : m_keyClipboard)
    {
        CopiedKey target = copied;
        target.frame = base + (copied.frame - firstFrame);
        if (detail::trackBlockedAt(entity, target.track, target.frame))
        {
            emit statusMessage(tr("粘贴目标落在求解段驱动的区间，已取消"));
            return;
        }
        placed.append(target);
    }

    const QHash<QString, model::Track> before = entity.tracks;
    QSet<int> pastedFrames;
    for (const CopiedKey& target : placed)
    {
        model::Track& track = entity.tracks[target.track];
        removeKeyframeAt(track, target.frame);
        model::Keyframe key;
        key.frame = target.frame;
        key.value = target.value;
        key.easing = target.easing;
        key.note = target.note;
        track.keys.append(key);
        sortTrack(track);
        pastedFrames.insert(target.frame);
    }

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: paste keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧粘贴失败")
                               : tr("关键帧粘贴失败：%1").arg(error));
        update();
        return;
    }

    m_keyframeSelectionUuid = uuid;
    m_selectedKeyframes = pastedFrames;
    update();
}

void TimelineWidget::applyKeyframeDrag(const QPoint& pos)
{
    if (!m_document || m_dragEntity.isEmpty() || m_dragKeyFrames.isEmpty())
        return;

    auto it = m_document->mutableProject().entities.find(m_dragEntity);
    if (it == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = it.value();
    const int duration = entity.duration();
    if (duration <= 0)
        return;
    if (detail::entityIsLocked(m_document, entity.uuid))
        return;

    int minFrame = INT_MAX;
    int maxFrame = INT_MIN;
    for (int frame : m_dragKeyFrames)
    {
        minFrame = std::min(minFrame, frame);
        maxFrame = std::max(maxFrame, frame);
    }

    int delta = frameFromX(pos.x()) - m_dragKeyPressFrame;
    delta = std::clamp(delta, -minFrame, duration - 1 - maxFrame);
    if (delta == m_dragKeyDelta)
        return;

    // Every preview starts from the press snapshot, so dragging back and forth
    // never accumulates rounding or overwritten keys.
    entity.tracks.clear();
    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd(); ++trackIt)
    {
        model::Track track;
        track.keys = trackIt.value();
        entity.tracks.insert(trackIt.key(), track);
    }

    bool conflict = false;
    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd() && !conflict; ++trackIt)
    {
        for (const model::Keyframe& key : trackIt.value())
        {
            if (!m_dragKeyFrames.contains(key.frame))
                continue;
            const int targetFrame = key.frame + delta;
            if (solverStartKey(entity, trackIt.key(), key.frame)
                || solverStartKey(entity, trackIt.key(), targetFrame)
                || detail::trackBlockedAt(entity, trackIt.key(), targetFrame))
            {
                conflict = true;
                break;
            }
        }
    }

    m_dragKeyDelta = delta;
    m_dragKeyConflict = conflict;
    if (conflict)
    {
        m_dragChanged = false;
        update();
        return;
    }

    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd(); ++trackIt)
    {
        QSet<int> destinations;
        QVector<model::Keyframe> kept;
        QVector<model::Keyframe> moved;
        kept.reserve(trackIt.value().size());
        moved.reserve(trackIt.value().size());

        for (const model::Keyframe& key : trackIt.value())
        {
            if (!m_dragKeyFrames.contains(key.frame))
            {
                kept.append(key);
                continue;
            }
            model::Keyframe shifted = key;
            shifted.frame = key.frame + delta;
            moved.append(shifted);
            destinations.insert(shifted.frame);
        }
        if (moved.isEmpty())
            continue;

        // Moving onto a frame that already has a key on the same track
        // overwrites it, matching normal keyframe editing behaviour.
        QVector<model::Keyframe> finalKeys;
        finalKeys.reserve(kept.size() + moved.size());
        for (const model::Keyframe& key : kept)
        {
            if (!destinations.contains(key.frame))
                finalKeys.append(key);
        }
        finalKeys += moved;
        std::sort(finalKeys.begin(), finalKeys.end(),
                  [](const model::Keyframe& a, const model::Keyframe& b) {
                      return a.frame < b.frame;
                  });

        entity.tracks[trackIt.key()].keys = finalKeys;
    }

    QSet<int> shiftedSelection;
    for (int frame : m_dragKeyFrames)
        shiftedSelection.insert(frame + delta);
    m_selectedKeyframes = shiftedSelection;
    m_keyframeSelectionUuid = m_dragEntity;
    m_dragChanged = delta != 0;
    update();
}

} // namespace pvf::next
