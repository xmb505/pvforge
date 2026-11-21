#include "TimelinePanel.h"

#include "WholeFrameEdit.h"
#include "app/EditorSession.h"
#include "model/Project.h"
#include "model/Uuid.h"

#include <algorithm>
#include <cmath>

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

void TimelinePanel::copyKeyframesToClipboard()
{
    if (!sessionMode() || !m_session)
        return;

    struct Picked
    {
        int absoluteFrame = 0;
        Uuid entity;
        const model::Keyframe* keyframe = nullptr;
    };
    const model::Project& project = m_session->project();
    QVector<Picked> picked;
    QSet<QString> seen;

    auto pick = [&](EntityId id, int absoluteFrame) {
        const Uuid uuid = uuidForEntityId(id);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            return;
        const int relative = absoluteFrame - it.value().start;
        const model::Keyframe* keyframe =
            model::keyframeAt(it.value().keyframes, relative);
        if (!keyframe)
            return;
        const QString identity = uuid + QLatin1Char('/')
                                 + QString::number(relative);
        if (seen.contains(identity))
            return;
        seen.insert(identity);
        picked.append(Picked{absoluteFrame, uuid, keyframe});
    };

    for (const KeyframeSelection& selection : m_selectedKeyframes)
        pick(selection.entity,
             int(std::lround(selection.frame)));
    // No diamond selected: the current entity's playhead frame is the source.
    if (picked.isEmpty() && m_selected != kInvalidEntity)
        pick(m_selected, m_session->playhead());
    if (picked.isEmpty())
    {
        emit editCommitted(QStringLiteral("没有可复制的关键帧"));
        return;
    }

    // One clipboard entry per frame offset, ascending so the pasted selection
    // and the anchor are deterministic regardless of click order.
    std::sort(picked.begin(), picked.end(),
              [](const Picked& a, const Picked& b) {
                  return a.absoluteFrame < b.absoluteFrame;
              });
    const int anchor = picked.first().absoluteFrame;

    KeyframeClipboard clipboard;
    clipboard.sourceContent = project.entities.value(picked.first().entity)
                                  .content;
    clipboard.entries.reserve(picked.size());
    for (const Picked& item : picked)
        clipboard.entries.append(KeyframeClipEntry{
            item.absoluteFrame - anchor, *item.keyframe});
    m_keyframeClipboard = clipboard;
    emit editCommitted(QStringLiteral("已复制 %1 个关键帧")
                           .arg(clipboard.entries.size()));
}

void TimelinePanel::pasteKeyframesFromClipboard()
{
    if (!sessionMode() || !m_session || !m_session->editActions())
        return;
    if (m_keyframeClipboard.isEmpty())
    {
        emit editCommitted(QStringLiteral("剪贴板没有关键帧"));
        return;
    }
    if (m_selected == kInvalidEntity)
    {
        emit editCommitted(QStringLiteral("请先选中要粘贴关键帧的实体"));
        return;
    }

    const Uuid uuid = uuidForEntityId(m_selected);
    const model::Entity* entity = sessionEntity(uuid);
    if (!entity)
        return;
    const EntityId targetId = m_selected;
    const int clipStart = entity->start;
    const int playheadBeforePaste = m_session->playhead();
    const int anchor = playheadBeforePaste - clipStart;

    EditPlan plan;
    QVector<int> pasted;
    QString error;
    m_editingKeyframeEdit = true;
    const bool planned = m_session->editActions()->planPasteKeyframes(
        m_keyframeClipboard, uuid, anchor, &plan, &pasted, &error);
    if (!planned)
    {
        m_editingKeyframeEdit = false;
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    if (plan.isEmpty())
    {
        m_editingKeyframeEdit = false;
        emit editCommitted(plan.message);
        return;
    }
    if (!m_session->editActions()->apply(plan, &error))
    {
        m_editingKeyframeEdit = false;
        if (!error.isEmpty())
            emit editFailed(error);
        return;
    }
    m_editingKeyframeEdit = false;

    // The playhead stays where the user pasted; the new diamonds become the
    // selection so the result can be inspected immediately.  Refresh first:
    // the document-driven selection sync inside it must not drop the new
    // keys afterwards.
    emit editCommitted(plan.message);
    refreshFromSession();
    m_selected = targetId;
    m_entitySelection.clear();
    m_entitySelection.insert(targetId);
    m_selectedKeyframes.clear();
    for (int frame : pasted)
        m_selectedKeyframes.append(
            KeyframeSelection{targetId, double(clipStart + frame)});
    if (!m_selectedKeyframes.isEmpty())
    {
        m_lastClickedKeyEntity = targetId;
        m_lastClickedKeyFrame =
            int(std::lround(m_selectedKeyframes.last().frame));
    }
    emit keyframeSelectionChanged(m_selectedKeyframes);
    // The properties panel syncs a single-key selection to its own frame;
    // the paste contract is that the playhead does not move.
    if (m_session->playhead() != playheadBeforePaste)
        m_session->setPlayhead(playheadBeforePaste);
    update();
}

} // namespace pvf
