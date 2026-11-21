#include "EditActions.h"

#include "EditActionsSupport.h"
#include "EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"

#include <QSet>
#include <QStringList>

#include <optional>

namespace pvf {

using namespace edit_detail;

namespace {

// The instantaneous solver pose at an entity-relative frame.  Creating a key
// inside an automatic segment ends the segment, so the key must hold the pose
// the solver shows instead of the pre-segment interpolation.
std::optional<solver::Sample> solverSampleAt(EditorSession* session,
                                             const model::Entity& entity,
                                             int frame)
{
    if (!session)
        return std::nullopt;
    const model::SolverSegment* segment = anim::solverAt(entity, frame);
    if (!segment)
        return std::nullopt;
    const solver::Result* result =
        session->solverResultFor(entity.uuid, segment->id);
    if (!result || result->samples.isEmpty())
        return std::nullopt;

    for (const solver::Sample& sample : result->samples)
    {
        if (sample.frame == frame)
            return sample;
    }
    // Past a physical stop the evaluator holds the last solved pose.
    if (frame > result->samples.last().frame)
        return result->samples.last();
    return std::nullopt;
}

bool validateKeyframeFrame(const model::Entity& entity, int frame,
                           QString* error)
{
    if (frame < 0 || frame >= entity.duration())
    {
        setError(error, QStringLiteral("关键帧 %1 超出实体范围 0..%2")
                            .arg(frame)
                            .arg(entity.duration() - 1));
        return false;
    }
    if (keyframeBlockedAt(entity, frame))
    {
        setError(error,
                 QStringLiteral("第 %1 帧由显式求解段驱动，不能写入普通关键帧")
                     .arg(frame));
        return false;
    }
    return true;
}

bool validateKeyframePayload(const model::Entity& entity,
                             const model::Keyframe& keyframe, QString* error)
{
    QString contentError;
    if (!model::validateKeyframeContent(entity.content, keyframe.content,
                                        &contentError))
    {
        setError(error, QStringLiteral("关键帧内容参数非法：%1").arg(contentError));
        return false;
    }
    if (!model::validateKeyframeHtmlVars(keyframe.htmlVars, &contentError))
    {
        setError(error, QStringLiteral("关键帧 HTML 变量非法：%1").arg(contentError));
        return false;
    }
    return true;
}

// One validated selection entry.  Duplicate refs in a multi-selection are
// collapsed so one gesture never writes the same frame twice.
struct ResolvedKey
{
    Uuid entity;
    int frame = 0;
};

bool resolveKey(const model::Project& project, const KeyRef& ref,
                ResolvedKey* out, QString* error)
{
    const auto entityIt = project.entities.constFind(ref.entity);
    if (entityIt == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(ref.entity));
        return false;
    }
    if (next::isEntityLocked(project, ref.entity))
    {
        setError(error,
                 QStringLiteral("实体 %1 或其祖先已锁定，不能编辑关键帧")
                     .arg(displayName(entityIt.value())));
        return false;
    }
    if (!model::hasKeyframeAt(entityIt.value().keyframes, ref.frame))
    {
        setError(error, QStringLiteral("第 %1 帧没有关键帧（%2）")
                            .arg(ref.frame)
                            .arg(displayName(entityIt.value())));
        return false;
    }
    out->entity = ref.entity;
    out->frame = ref.frame;
    return true;
}

bool sameResolvedKey(const ResolvedKey& a, const ResolvedKey& b)
{
    return a.entity == b.entity && a.frame == b.frame;
}

bool containsResolvedKey(const QVector<ResolvedKey>& keys,
                         const ResolvedKey& candidate)
{
    for (const ResolvedKey& key : keys)
    {
        if (sameResolvedKey(key, candidate))
            return true;
    }
    return false;
}

} // namespace

bool EditActions::planKeyframe(const Uuid& uuid, int frame, EditPlan* plan,
                               QString* error) const
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
    if (!validateKeyframeFrame(it.value(), frame, error))
        return false;

    model::KeyframeSample sample = model::entitySampleAt(it.value(), frame);
    if (const auto pose = solverSampleAt(m_session, it.value(), frame))
    {
        sample.x = pose->pose.position.x();
        sample.y = pose->pose.position.y();
        sample.rotation = pose->pose.rotation;
        sample.scaleX *= pose->pose.scale;
        sample.scaleY *= pose->pose.scale;
    }

    model::Project working = project;
    model::Entity& entity = working.entities[uuid];
    model::Keyframe keyframe = keyframeFromSample(sample, frame);
    if (const model::Keyframe* existing =
            model::keyframeAt(entity.keyframes, frame))
    {
        // Re-evaluating a frame that already has a key keeps its author
        // metadata and only refreshes the animation values.
        keyframe.easing = existing->easing;
        keyframe.note = existing->note;
        keyframe.extras = existing->extras;
    }
    model::upsertKeyframe(entity.keyframes, keyframe);

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
    plan->message = QStringLiteral("设置关键帧");
    return true;
}

bool EditActions::planUpsertKeyframe(const Uuid& uuid,
                                     const model::Keyframe& keyframe,
                                     EditPlan* plan, QString* error) const
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
    if (!validateKeyframeFrame(it.value(), keyframe.frame, error))
        return false;
    if (!validateKeyframePayload(it.value(), keyframe, error))
        return false;

    model::Project working = project;
    model::Entity& entity = working.entities[uuid];
    model::upsertKeyframe(entity.keyframes, keyframe);

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
    plan->message = QStringLiteral("设置关键帧");
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
        int from = 0;
        model::Keyframe key;
        bool keep = true;
    };
    QVector<Move> moves;
    QVector<ResolvedKey> resolved;
    QSet<Uuid> entities;

    for (const KeyRef& ref : keys)
    {
        ResolvedKey resolvedKey;
        if (!resolveKey(project, ref, &resolvedKey, error))
            return false;
        if (containsResolvedKey(resolved, resolvedKey))
            continue;
        resolved.append(resolvedKey);

        const model::Entity& entity = project.entities.value(ref.entity);
        if (solverStartsAt(entity, ref.frame))
        {
            setError(error, QStringLiteral("求解段开始帧的关键帧不能移动，"
                                           "请先删除求解段"));
            return false;
        }

        const int target = ref.frame + delta;
        const model::Keyframe* key =
            model::keyframeAt(entity.keyframes, ref.frame);
        Move move{ref.entity, ref.frame, *key, true};
        // Frames dragged past the clip boundary are dropped, matching the
        // timeline gesture contract.  The last legal relative frame is
        // duration() - 1.
        if (target < 0 || target >= entity.duration())
        {
            move.keep = false;
            moves.append(move);
            entities.insert(ref.entity);
            continue;
        }
        if (solverStartsAt(entity, target) && target != ref.frame)
        {
            setError(error, QStringLiteral("第 %1 帧是求解段开始帧，"
                                           "不能把普通关键帧移到那里")
                                .arg(target));
            return false;
        }
        if (keyframeBlockedAt(entity, target))
        {
            setError(error, QStringLiteral("第 %1 帧由显式求解段驱动")
                                .arg(target));
            return false;
        }

        moves.append(move);
        entities.insert(ref.entity);
    }

    if (moves.isEmpty() || delta == 0)
    {
        plan->project = project;
        plan->upserts.clear();
        plan->deletes.clear();
        plan->projectChanged = false;
        plan->frameDelta = 0;
        plan->message = QStringLiteral("移动关键帧");
        return true;
    }

    model::Project working = project;
    for (const Uuid& uuid : entities)
    {
        model::Entity& entity = working.entities[uuid];
        QSet<int> sources;
        QSet<int> targets;
        for (const Move& move : moves)
        {
            if (move.entity != uuid)
                continue;
            sources.insert(move.from);
            if (move.keep)
                targets.insert(move.from + delta);
        }

        QVector<model::Keyframe> kept;
        kept.reserve(entity.keyframes.size());
        for (const model::Keyframe& keyframe : entity.keyframes)
        {
            if (sources.contains(keyframe.frame)
                || targets.contains(keyframe.frame))
                continue;
            kept.append(keyframe);
        }
        for (const Move& move : moves)
        {
            if (move.entity != uuid || !move.keep)
                continue;
            model::Keyframe moved = move.key;
            moved.frame = move.from + delta;
            kept.append(moved);
        }
        model::sortKeyframes(kept);
        entity.keyframes = std::move(kept);

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
    plan->frameDelta = delta;
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
    QVector<ResolvedKey> resolved;
    QSet<Uuid> entities;
    bool changed = false;

    for (const KeyRef& ref : keys)
    {
        ResolvedKey key;
        if (!resolveKey(project, ref, &key, error))
            return false;
        if (containsResolvedKey(resolved, key))
            continue;
        resolved.append(key);

        model::Entity& entity = working.entities[ref.entity];
        if (solverStartsAt(entity, ref.frame))
        {
            setError(error,
                     QStringLiteral("求解段开始帧的关键帧不能删除，请先删除求解段"));
            return false;
        }
        if (model::removeKeyframe(entity.keyframes, ref.frame))
        {
            changed = true;
            entities.insert(ref.entity);
        }
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
    QVector<ResolvedKey> resolved;
    QSet<Uuid> entities;
    bool changed = false;

    for (const KeyRef& ref : keys)
    {
        ResolvedKey key;
        if (!resolveKey(project, ref, &key, error))
            return false;
        if (containsResolvedKey(resolved, key))
            continue;
        resolved.append(key);

        model::Entity& entity = working.entities[ref.entity];
        if (model::Keyframe* keyframe =
                mutableKeyframeAt(entity.keyframes, ref.frame))
        {
            keyframe->easing = easing;
            changed = true;
            entities.insert(ref.entity);
        }
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

bool EditActions::planSetKeyframeNote(const QVector<KeyRef>& keys,
                                      const QString& note, EditPlan* plan,
                                      QString* error) const
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
    QVector<ResolvedKey> resolved;
    QSet<Uuid> entities;
    bool changed = false;

    for (const KeyRef& ref : keys)
    {
        ResolvedKey key;
        if (!resolveKey(project, ref, &key, error))
            return false;
        if (containsResolvedKey(resolved, key))
            continue;
        resolved.append(key);

        model::Entity& entity = working.entities[ref.entity];
        if (model::Keyframe* keyframe =
                mutableKeyframeAt(entity.keyframes, ref.frame))
        {
            keyframe->note = note;
            changed = true;
            entities.insert(ref.entity);
        }
    }

    if (!changed)
    {
        setError(error, QStringLiteral("没有可修改注释的关键帧"));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = QVector<Uuid>(entities.begin(), entities.end());
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("修改关键帧注释");
    return true;
}

bool EditActions::planCopyKeyframeParams(const KeyRef& source,
                                         const QVector<KeyRef>& targets,
                                         EditPlan* plan, QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (targets.isEmpty())
    {
        setError(error, QStringLiteral("没有目标关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto sourceIt = project.entities.constFind(source.entity);
    if (sourceIt == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(source.entity));
        return false;
    }
    const model::Keyframe* sourceKey =
        model::keyframeAt(sourceIt.value().keyframes, source.frame);
    if (!sourceKey)
    {
        setError(error, QStringLiteral("源帧第 %1 帧没有关键帧")
                            .arg(source.frame));
        return false;
    }

    model::Project working = project;
    QSet<Uuid> entities;
    QVector<ResolvedKey> resolved;
    bool changed = false;

    for (const KeyRef& ref : targets)
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
                     QStringLiteral("实体 %1 或其祖先已锁定，不能复制参数")
                         .arg(displayName(entityIt.value())));
            return false;
        }
        // Out-of-range targets are dropped rather than rejected.
        if (ref.frame < 0 || ref.frame >= entityIt.value().duration())
            continue;

        ResolvedKey key;
        if (!resolveKey(project, ref, &key, error))
            return false;
        if (containsResolvedKey(resolved, key))
            continue;
        resolved.append(key);

        model::Entity& entity = working.entities[ref.entity];
        model::Keyframe* target = mutableKeyframeAt(entity.keyframes, ref.frame);
        if (!target)
            continue;
        copyAnimationParams(*sourceKey, target);
        if (!keyframeParamsCompatible(sourceIt.value(), entity))
        {
            // Keep the target's own content values when the schemas differ.
            const auto originalIt = project.entities.constFind(ref.entity);
            const model::Keyframe* original =
                originalIt == project.entities.constEnd()
                    ? nullptr
                    : model::keyframeAt(originalIt.value().keyframes, ref.frame);
            if (original)
            {
                target->content = original->content;
                target->htmlVars = original->htmlVars;
            }
        }
        changed = true;
        entities.insert(ref.entity);
    }

    plan->project = std::move(working);
    plan->upserts = QVector<Uuid>(entities.begin(), entities.end());
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = QStringLiteral("复制关键帧参数");
    if (!changed)
        plan->project = project;
    return true;
}

bool EditActions::planPasteKeyframes(const KeyframeClipboard& clipboard,
                                     const Uuid& target, int anchor,
                                     EditPlan* plan,
                                     QVector<int>* pastedFrames,
                                     QString* error) const
{
    if (error)
        error->clear();
    if (pastedFrames)
        pastedFrames->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (clipboard.entries.isEmpty())
    {
        setError(error, QStringLiteral("剪贴板没有关键帧"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(target);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(target));
        return false;
    }
    if (next::isEntityLocked(project, target))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能粘贴关键帧")
                            .arg(displayName(it.value())));
        return false;
    }

    model::Project working = project;
    model::Entity& entity = working.entities[target];
    const bool compatible = contentSchemasCompatible(clipboard.sourceContent,
                                                     entity.content);

    QVector<int> frames;
    QSet<int> seen;
    int dropped = 0;
    int blocked = 0;
    for (const KeyframeClipEntry& entry : clipboard.entries)
    {
        const int frame = anchor + entry.offset;
        // Pasted frames never stretch the clip: out-of-range entries vanish.
        if (frame < 0 || frame >= entity.duration())
        {
            ++dropped;
            continue;
        }
        if (keyframeBlockedAt(entity, frame))
        {
            ++blocked;
            continue;
        }
        if (seen.contains(frame))
            continue;
        seen.insert(frame);

        // Start from the target's own whole-frame values so an incompatible
        // source can never leak its content/htmlVars schema into the target.
        model::KeyframeSample sample = model::entitySampleAt(entity, frame);
        if (const auto pose = solverSampleAt(m_session, entity, frame))
        {
            sample.x = pose->pose.position.x();
            sample.y = pose->pose.position.y();
            sample.rotation = pose->pose.rotation;
            sample.scaleX *= pose->pose.scale;
            sample.scaleY *= pose->pose.scale;
        }
        model::Keyframe keyframe = keyframeFromSample(sample, frame);
        keyframe.x = entry.keyframe.x;
        keyframe.y = entry.keyframe.y;
        keyframe.rotation = entry.keyframe.rotation;
        keyframe.scaleX = entry.keyframe.scaleX;
        keyframe.scaleY = entry.keyframe.scaleY;
        keyframe.opacity = entry.keyframe.opacity;
        keyframe.visible = entry.keyframe.visible;
        keyframe.physicsInteract = entry.keyframe.physicsInteract;
        keyframe.easing = entry.keyframe.easing;
        keyframe.note = entry.keyframe.note;
        keyframe.extras = entry.keyframe.extras;
        if (compatible)
        {
            keyframe.content = entry.keyframe.content;
            keyframe.htmlVars = entry.keyframe.htmlVars;
        }
        model::upsertKeyframe(entity.keyframes, keyframe);
        frames.append(frame);
    }

    QString message = frames.isEmpty()
                          ? QStringLiteral("没有可粘贴的关键帧")
                          : QStringLiteral("粘贴 %1 个关键帧").arg(frames.size());
    QStringList skipped;
    if (dropped > 0)
        skipped.append(QStringLiteral("越界 %1 帧").arg(dropped));
    if (blocked > 0)
        skipped.append(QStringLiteral("求解段 %1 帧").arg(blocked));
    if (!skipped.isEmpty())
        message += QStringLiteral("；跳过 ") + skipped.join(QStringLiteral("、"));

    if (!frames.isEmpty())
    {
        QString validationError;
        if (!model::validateEntity(entity, &validationError))
        {
            setError(error, QStringLiteral("粘贴后关键帧校验失败：%1")
                                .arg(validationError));
            return false;
        }
    }

    plan->project = std::move(working);
    plan->upserts = frames.isEmpty() ? QVector<Uuid>{}
                                     : QVector<Uuid>{target};
    plan->deletes.clear();
    plan->projectChanged = false;
    plan->message = message;
    if (pastedFrames)
        *pastedFrames = frames;
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
