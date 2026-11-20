#include "TimelinePanel.h"

#include "WholeFrameEdit.h"
#include "app/EditorSession.h"
#include "model/Project.h"
#include "model/Uuid.h"

#include <algorithm>

namespace pvf {

bool TimelinePanel::addKeyframeAt(EntityId id, int frame)
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return false;
    const Uuid uuid = uuidForEntityId(id);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return false;
    const int relative = frame - entity->start;
    QString reason;
    if (!ui::wholeframe::canWriteKeyAt(*entity, relative, &reason))
    {
        if (!reason.isEmpty())
            emit editCommitted(reason);
        return false;
    }

    QString error;
    m_editingKeyframeEdit = true;
    const bool ok = m_session->editActions()->addKeyframe(uuid, relative, &error);
    m_editingKeyframeEdit = false;
    if (!ok)
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return false;
    }
    m_selectedKeyframes.clear();
    m_selectedKeyframes.append(KeyframeSelection{id, double(frame)});
    m_lastClickedKeyEntity = id;
    m_lastClickedKeyFrame = frame;
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("设置第 %1 帧整帧关键帧").arg(frame));
    refreshFromSession();
    return true;
}

bool TimelinePanel::keyframeSelectedAt(EntityId id, int frame) const
{
    for (const KeyframeSelection& selection : m_selectedKeyframes)
        if (selection.entity == id
            && int(std::lround(selection.frame)) == frame)
            return true;
    return false;
}

QVector<KeyRef> TimelinePanel::selectedKeyRefs() const
{
    QVector<KeyRef> result;
    if (!sessionMode() || !m_session)
        return result;
    const model::Project& project = m_session->project();
    QSet<QString> seen;
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        const Uuid uuid = uuidForEntityId(selection.entity);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;
        KeyRef ref;
        ref.entity = uuid;
        ref.frame = int(std::lround(selection.frame)) - it.value().start;
        const QString key = ref.entity + QLatin1Char('\n')
                            + QString::number(ref.frame);
        if (seen.contains(key))
            continue;
        seen.insert(key);
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
    m_editingKeyframeEdit = true;
    const bool ok = m_session->editActions()->deleteKeyframes(refs, &error);
    m_editingKeyframeEdit = false;
    if (!ok)
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_selectedKeyframes.clear();
    emit keyframeSelectionChanged(m_selectedKeyframes);
    emit editCommitted(QStringLiteral("删除整帧关键帧"));
    refreshFromSession();
}

void TimelinePanel::copyParamsToSelectedKeyframes()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    const QVector<KeyRef> targets = selectedKeyRefs();
    if (targets.isEmpty())
        return;

    // Source: the playhead key when it has one, otherwise the last clicked
    // diamond.  Both are whole-frame keys.
    KeyRef source;
    const model::Project& project = m_session->project();
    for (const KeyframeSelection& selection : m_selectedKeyframes)
    {
        const Uuid uuid = uuidForEntityId(selection.entity);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;
        const int playhead = m_session->playhead();
        const int relative = playhead - it.value().start;
        if (model::hasKeyframeAt(it.value().keyframes, relative))
        {
            source = KeyRef{uuid, relative};
            break;
        }
    }
    if (source.entity.isEmpty() && m_lastClickedKeyEntity != kInvalidEntity)
    {
        const Uuid uuid = uuidForEntityId(m_lastClickedKeyEntity);
        const auto it = project.entities.constFind(uuid);
        const int relative = m_lastClickedKeyFrame - it.value().start;
        if (it != project.entities.constEnd()
            && model::hasKeyframeAt(it.value().keyframes, relative))
            source = KeyRef{uuid, relative};
    }
    if (source.entity.isEmpty())
    {
        emit editCommitted(QStringLiteral(
            "复制参数：播放针不在关键帧上，且没有最后点击的关键帧"));
        return;
    }

    QString error;
    m_editingKeyframeEdit = true;
    const bool ok = m_session->editActions()->copyKeyframeParams(
        source, targets, &error);
    m_editingKeyframeEdit = false;
    if (!ok)
    {
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    emit editCommitted(QStringLiteral("复制整帧参数到 %1 个关键帧")
                           .arg(targets.size()));
    refreshFromSession();
}

} // namespace pvf
