#include "EditActions.h"

#include "EditActionsSupport.h"
#include "EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"
#include "next/timeline/MarkerDialog.h"

#include <QSet>

#include <algorithm>
#include <climits>

namespace pvf {

using namespace edit_detail;

bool EditActions::updateEntity(const Uuid& uuid, const QString& message,
                               const std::function<bool(model::Entity&)>& mutate,
                               QString* error, bool ignoreLock)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (!mutate)
    {
        setError(error, QStringLiteral("没有可应用的修改"));
        return false;
    }

    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }
    if (!ignoreLock && next::isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能编辑")
                            .arg(displayName(it.value())));
        return false;
    }

    const bool wasFollowing = it.value().followParentRange;
    EditPlan plan;
    plan.project = project;
    model::Entity& entity = plan.project.entities[uuid];
    if (!mutate(entity))
    {
        setError(error, QStringLiteral("没有可应用的修改"));
        return false;
    }

    if (entity.followParentRange)
    {
        if (entity.parentUuid.isEmpty())
        {
            setError(error,
                     QStringLiteral("根实体 %1 不能跟随父区间")
                         .arg(displayName(entity)));
            return false;
        }
        const auto parent = plan.project.entities.constFind(entity.parentUuid);
        if (parent == plan.project.entities.constEnd())
        {
            setError(error, QStringLiteral("实体 %1 的父实体不存在")
                                .arg(displayName(entity)));
            return false;
        }
        // Enabling follow, or any update that leaves a following entity with
        // a range different from its parent, goes through the same rebase as
        // a trim: keys/solvers keep their absolute frames and out-of-range
        // items are dropped.  allowDisjointRoot keeps the entity even when
        // the range jumps far away.
        if (!wasFollowing || entity.start != parent.value().start
            || entity.end != parent.value().end)
        {
            model::Project aligned;
            QVector<Uuid> upserts;
            QVector<Uuid> deletes;
            if (!next::applyEntityRange(plan.project, m_session->assets(), uuid,
                                        parent.value().start,
                                        parent.value().end, &aligned, &upserts,
                                        &deletes, error, true))
                return false;
            if (!upserts.contains(uuid))
                upserts.prepend(uuid);
            plan.project = std::move(aligned);
            plan.upserts = std::move(upserts);
            plan.deletes = std::move(deletes);
        }
        else
        {
            plan.upserts = {uuid};
        }
    }
    else
    {
        plan.upserts = {uuid};
    }

    // Re-read after the optional rebase moved the project.
    const auto alignedIt = plan.project.entities.constFind(uuid);
    QString validationError;
    if (alignedIt == plan.project.entities.constEnd()
        || !model::validateEntity(alignedIt.value(), &validationError))
    {
        setError(error, QStringLiteral("实体校验失败：%1").arg(validationError));
        return false;
    }

    plan.message = message;
    return apply(plan, error);
}

bool EditActions::updateContent(
    const Uuid& uuid, const QString& message,
    const std::function<bool(model::Content&)>& mutate, QString* error)
{
    if (!mutate)
    {
        if (error)
            *error = QStringLiteral("没有可应用的修改");
        return false;
    }
    return updateEntity(
        uuid, message,
        [&mutate](model::Entity& entity) { return mutate(entity.content); },
        error);
}

namespace {

bool isTimedAssetType(AssetType type)
{
    return type == AssetType::Video || type == AssetType::Audio;
}

bool isVisualAssetType(AssetType type)
{
    return type == AssetType::Image || type == AssetType::Video
           || type == AssetType::Html;
}

// The compatibility table from docs/AI/ENTITY_MODEL.md 6.2.  Empty entities
// accept any asset group; cameras and generated content never accept assets.
bool assetGroupMatches(const model::Content& content, AssetType incoming)
{
    switch (content.kind)
    {
    case ContentKind::Empty:
        return true;
    case ContentKind::Asset:
        if (isVisualAssetType(content.asset.type))
            return isVisualAssetType(incoming);
        if (content.asset.type == AssetType::Audio)
            return incoming == AssetType::Audio;
        return false;
    case ContentKind::Camera:
    case ContentKind::Text:
    case ContentKind::Vector:
        return false;
    }
    return false;
}

int assetDurationFrames(const assets::StoredAsset& asset, int fps)
{
    if (!isTimedAssetType(asset.type))
        return std::max(1, fps);
    const int frames =
        asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    return frames > 0 ? frames : std::max(1, fps);
}

bool rowIsOccupied(const model::Project& project, const Uuid& parent,
                   int row, int start, int end)
{
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.parentUuid != parent || entity.row != row)
            continue;
        if (entity.start < end && start < entity.end)
            return true;
    }
    return false;
}

} // namespace

QVector<Uuid> EditActions::addAssetEntities(const AssetHash& hash, int startFrame,
                                            int row, const Uuid& parent,
                                            QString* error)
{
    QVector<Uuid> created;
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return created;
    }

    const auto asset = m_session->assets().get(hash);
    if (!asset)
    {
        setError(error, QStringLiteral("素材不存在或内容缺失：%1").arg(hash));
        return created;
    }

    const model::Project& project = m_session->project();
    const int fps = std::max(1, project.settings.fps);
    const int start = std::max(0, startFrame);
    const int duration = assetDurationFrames(*asset, fps);
    const int end = start + duration;

    if (!parent.isEmpty())
    {
        const auto it = project.entities.constFind(parent);
        if (it == project.entities.constEnd())
        {
            setError(error, QStringLiteral("父实体不存在：%1").arg(parent));
            return created;
        }
        if (next::isEntityLocked(project, parent))
        {
            setError(error, QStringLiteral("父实体已锁定，不能添加素材"));
            return created;
        }
        if (start < it->start || end > it->end)
        {
            setError(error,
                     QStringLiteral("素材范围 [%1, %2) 超出父实体 [%3, %4)")
                         .arg(start)
                         .arg(end)
                         .arg(it->start)
                         .arg(it->end));
            return created;
        }
    }

    int targetRow = row;
    // row < 0 is the "pick a row" sentinel for root insertions.  Inside a
    // private timeline it is a real negative track and must be honoured; only
    // a time conflict on that layer falls back to the next free row.
    const bool autoRow = row < 0 && parent.isEmpty();
    if (autoRow || rowIsOccupied(project, parent, targetRow, start, end))
        targetRow = model::nextAutoRow(project, parent, start, end);
    if (parent.isEmpty())
        targetRow = std::max(0, targetRow);

    const QString name = asset->name.isEmpty() ? hash : asset->name;

    EditPlan plan;
    plan.project = project;
    plan.message = QStringLiteral("pvf: add asset \"%1\"").arg(name);

    model::Entity main;
    main.uuid = model::newUuid();
    main.name = name;
    main.parentUuid = parent;
    main.start = start;
    main.end = end;
    main.row = targetRow;
    main.content.kind = ContentKind::Asset;
    main.content.asset.hash = asset->hash;
    main.content.asset.type = asset->type;
    main.content.asset.sourceIn = 0;
    main.content.asset.volume = 1.0;
    plan.project.entities.insert(main.uuid, main);
    plan.upserts.append(main.uuid);
    created.append(main.uuid);

    const QString audioHash =
        asset->metadata.value(QStringLiteral("audioHash")).toString();
    if (asset->type == AssetType::Video && !audioHash.isEmpty()
        && m_session->assets().get(audioHash))
    {
        // The split-out track is a child of the video with followParentRange
        // enabled: moving/trimming/deleting the picture moves the sound with it
        // (issue #128).  Users can still turn the follow flag off or reparent
        // the audio when they want independent timing.
        model::Entity audio;
        audio.uuid = model::newUuid();
        audio.name = name + QStringLiteral(" 音频");
        audio.parentUuid = main.uuid;
        audio.followParentRange = true;
        audio.start = start;
        audio.end = end;
        audio.row = 0;
        audio.content.kind = ContentKind::Asset;
        audio.content.asset.hash = audioHash;
        audio.content.asset.type = AssetType::Audio;
        audio.content.asset.sourceIn = 0;
        audio.content.asset.volume = 1.0;
        plan.project.entities.insert(audio.uuid, audio);
        plan.upserts.append(audio.uuid);
        created.append(audio.uuid);
    }

    if (!apply(plan, error))
    {
        created.clear();
        return created;
    }
    return created;
}

QVector<Uuid> EditActions::duplicateEntities(const QVector<Uuid>& roots,
                                            int deltaFrames,
                                            const Uuid& newParent,
                                            QString* error)
{
    QVector<Uuid> created;
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return created;
    }
    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return created;

    QVector<model::Entity> clipboard;
    QSet<Uuid> seen;
    for (const Uuid& root : selected)
    {
        for (const Uuid& uuid : subtree(project, root))
        {
            if (seen.contains(uuid))
                continue;
            seen.insert(uuid);
            clipboard.append(project.entities.value(uuid));
        }
    }
    return pasteEntities(clipboard, newParent, deltaFrames, error);
}

QVector<Uuid> EditActions::pasteEntities(
    const QVector<model::Entity>& clipboard, const Uuid& parent,
    int deltaFrames, QString* error)
{
    QVector<Uuid> created;
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return created;
    }
    if (clipboard.isEmpty())
    {
        setError(error, QStringLiteral("没有可粘贴的片段"));
        return created;
    }

    const model::Project& project = m_session->project();
    if (!parent.isEmpty())
    {
        const auto parentIt = project.entities.constFind(parent);
        if (parentIt == project.entities.constEnd())
        {
            setError(error, QStringLiteral("父实体不存在：%1").arg(parent));
            return created;
        }
        if (next::isEntityLocked(project, parent))
        {
            setError(error, QStringLiteral("父实体已锁定，不能粘贴"));
            return created;
        }
    }

    QSet<Uuid> sourceIds;
    for (const model::Entity& entity : clipboard)
        sourceIds.insert(entity.uuid);

    model::Project working = project;
    QHash<Uuid, Uuid> remap;
    for (const model::Entity& entity : clipboard)
        remap.insert(entity.uuid, model::newUuid());

    QVector<Uuid> upserts;
    for (const model::Entity& source : clipboard)
    {
        model::Entity clone = source;
        clone.uuid = remap.value(source.uuid);
        const bool isRoot =
            source.parentUuid.isEmpty() || !sourceIds.contains(source.parentUuid);
        clone.parentUuid = isRoot ? parent : remap.value(source.parentUuid);
        clone.start += deltaFrames;
        clone.end += deltaFrames;
        if (clone.start < 0)
        {
            setError(error, QStringLiteral("粘贴后的开始帧为 %1").arg(clone.start));
            return created;
        }
        if (isRoot)
        {
            clone.row = model::nextAutoRow(working, parent, clone.start,
                                           clone.end);
            created.append(clone.uuid);
        }
        for (model::SolverSegment& solver : clone.solvers)
        {
            const QString target =
                solver.params.value(QStringLiteral("targetUuid")).toString();
            const auto mapped = remap.constFind(target);
            if (mapped != remap.constEnd())
                solver.params.insert(QStringLiteral("targetUuid"), mapped.value());
        }
        working.entities.insert(clone.uuid, clone);
        upserts.append(clone.uuid);
    }

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("粘贴后工程校验失败：%1")
                            .arg(validationError));
        return {};
    }

    EditPlan plan;
    plan.project = std::move(working);
    plan.upserts = upserts;
    plan.message = QStringLiteral("粘贴片段");
    if (!apply(plan, error))
        return {};
    return created;
}

bool EditActions::groupIntoFolder(const QVector<Uuid>& roots, Uuid* folder,
                                  QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return false;
    if (selected.size() < 2)
    {
        setError(error, QStringLiteral("至少选择两个片段才能成组"));
        return false;
    }

    const Uuid parent = project.entities.value(selected.first()).parentUuid;
    int start = INT_MAX;
    int end = INT_MIN;
    QVector<Uuid> allNodes;
    QSet<Uuid> nodeSet;
    for (const Uuid& root : selected)
    {
        const model::Entity& entity = project.entities.value(root);
        if (entity.parentUuid != parent)
        {
            setError(error, QStringLiteral("只能把同一父级下的片段成组"));
            return false;
        }
        start = std::min(start, entity.start);
        end = std::max(end, entity.end);
        for (const Uuid& node : subtree(project, root))
            addUpsert(&allNodes, &nodeSet, node);
    }
    const QString locked = lockedSubtreeError(project, allNodes);
    if (!locked.isEmpty())
    {
        setError(error, locked);
        return false;
    }

    model::Project working = project;
    model::Entity group;
    group.uuid = model::newUuid();
    group.name = QStringLiteral("文件夹");
    group.parentUuid = parent;
    group.start = start;
    group.end = end;
    group.row = model::nextAutoRow(project, parent, start, end);
    group.content.kind = ContentKind::Empty;
    working.entities.insert(group.uuid, group);

    QVector<Uuid> upserts{group.uuid};
    for (int i = 0; i < selected.size(); ++i)
    {
        model::Entity& child = working.entities[selected.at(i)];
        child.parentUuid = group.uuid;
        child.row = i;
        upserts.append(selected.at(i));
    }

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("成组后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    EditPlan plan;
    plan.project = std::move(working);
    plan.upserts = upserts;
    plan.message = QStringLiteral("成组为文件夹");
    if (!apply(plan, error))
        return false;
    if (folder)
        *folder = group.uuid;
    return true;
}

bool EditActions::replaceAsset(const Uuid& uuid, const AssetHash& hash,
                               QString* error, bool syncChildAudio)
{
    if (error)
        error->clear();
    if (!isValid())
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
    const model::Entity& entity = it.value();
    if (next::isEntityLocked(project, uuid))
    {
        setError(error, QStringLiteral("实体 %1 或其祖先已锁定，不能更换素材")
                            .arg(displayName(entity)));
        return false;
    }

    const auto stored = m_session->assets().get(hash);
    if (!stored)
    {
        setError(error, QStringLiteral("素材不存在或内容缺失：%1").arg(hash));
        return false;
    }
    if (!assetGroupMatches(entity.content, stored->type))
    {
        setError(error, QStringLiteral("素材类型不兼容：%1 不能换成 %2")
                            .arg(contentKindName(entity.content.kind),
                                 assetTypeName(stored->type)));
        return false;
    }
    if (entity.content.kind == ContentKind::Asset
        && entity.content.asset.hash == hash)
    {
        setError(error, QStringLiteral("实体已经使用该素材"));
        return false;
    }

    const int fps = std::max(1, project.settings.fps);
    const bool timed = isTimedAssetType(stored->type);
    const int frames = stored->metadata.value(QStringLiteral("durationFrames"))
                           .toInt(0);

    int sourceIn = 0;
    if (timed && entity.content.kind == ContentKind::Asset
        && isTimedAssetType(entity.content.asset.type))
        sourceIn = entity.content.asset.sourceIn;
    if (frames > 0)
        sourceIn = std::clamp(sourceIn, 0, std::max(0, frames - 1));
    else
        sourceIn = 0;

    int duration = entity.duration();
    bool needsTrim = false;
    if (timed && frames > 0)
    {
        const int available = frames - sourceIn;
        if (available <= 0)
        {
            setError(error, QStringLiteral("素材太短，没有可用帧"));
            return false;
        }
        if (duration > available)
        {
            duration = available;
            needsTrim = true;
        }
    }
    if (duration <= 0)
        duration = std::max(1, fps);
    const int newEnd = entity.start + duration;

    // The split-out audio track of a video is a following child (issue #128).
    // It is updated together with its picture, or removed when the new video
    // has no audio stream.
    const model::Entity* audioChild = nullptr;
    for (auto child = project.entities.constBegin();
         child != project.entities.constEnd(); ++child)
    {
        if (child.value().parentUuid != uuid)
            continue;
        if (child.value().content.kind == ContentKind::Asset
            && child.value().content.asset.type == AssetType::Audio)
        {
            audioChild = &child.value();
            break;
        }
    }

    if (needsTrim)
    {
        for (auto child = project.entities.constBegin();
             child != project.entities.constEnd(); ++child)
        {
            if (child.value().parentUuid != uuid)
                continue;
            if (audioChild && child.value().uuid == audioChild->uuid)
                continue;
            if (!child.value().followParentRange && child.value().end > newEnd)
            {
                setError(error,
                         QStringLiteral("换素材会裁剪子实体 %1，请先裁剪子实体")
                             .arg(displayName(child.value())));
                return false;
            }
        }
    }

    EditPlan plan;
    // Install the new asset (and clamped sourceIn) before the range edit: the
    // material-duration check inside applyEntityRange must use the new asset,
    // not the one being replaced.
    plan.project = project;
    {
        model::Entity& swapped = plan.project.entities[uuid];
        swapped.content.kind = ContentKind::Asset;
        swapped.content.asset.hash = stored->hash;
        swapped.content.asset.type = stored->type;
        swapped.content.asset.sourceIn = sourceIn;
    }
    if (needsTrim)
    {
        // Trimming the picture must also rewrite a following audio child so the
        // whole subtree keeps valid ranges and keyframes.
        model::Project trimmed;
        if (!next::applyEntityRange(plan.project, m_session->assets(), uuid,
                                    entity.start, newEnd, &trimmed,
                                    &plan.upserts, &plan.deletes, error, true))
            return false;
        plan.project = std::move(trimmed);
    }
    else
    {
        plan.upserts = {uuid};
    }
    if (!plan.project.entities.contains(uuid))
    {
        setError(error, QStringLiteral("实体不存在：%1").arg(uuid));
        return false;
    }

    const assets::StoredAsset asset = *stored;
    model::Entity& target = plan.project.entities[uuid];
    target.content.kind = ContentKind::Asset;
    target.content.asset.hash = asset.hash;
    target.content.asset.type = asset.type;
    target.content.asset.sourceIn = sourceIn;
    target.end = target.start + duration;

    if (audioChild)
    {
        const Uuid audioUuid = audioChild->uuid;
        const QString newAudioHash =
            asset.metadata.value(QStringLiteral("audioHash")).toString();
        const bool audioAvailable = !newAudioHash.isEmpty()
                                    && m_session->assets().get(newAudioHash);
        if (syncChildAudio && !audioAvailable)
        {
            if (!plan.deletes.contains(audioUuid))
                plan.deletes.append(audioUuid);
            plan.upserts.removeAll(audioUuid);
        }
        else if (syncChildAudio && plan.project.entities.contains(audioUuid))
        {
            model::Entity& audio = plan.project.entities[audioUuid];
            audio.content.asset.hash = newAudioHash;
            audio.content.asset.type = AssetType::Audio;
            if (!plan.upserts.contains(audioUuid))
                plan.upserts.append(audioUuid);
        }
    }

    QString validationError;
    if (!model::validateEntity(target, &validationError))
    {
        setError(error, QStringLiteral("实体校验失败：%1").arg(validationError));
        return false;
    }

    plan.message =
        QStringLiteral("pvf: assign asset \"%1\" to \"%2\"")
            .arg(asset.name, displayName(entity));
    return apply(plan, error);
}

namespace {

bool applyMarkerPlan(EditorSession* session, const model::Project& project,
                     const QString& message, QString* error)
{
    if (!session || !session->document())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    next::ProjectDocument* document = session->document();
    const model::Project before = document->project();
    model::Project& live = document->mutableProject();
    live = before;
    live.markers = project.markers;

    QString validationError;
    if (!model::validateProject(live, &validationError))
    {
        live = before;
        setError(error, QStringLiteral("标记校验失败：%1").arg(validationError));
        return false;
    }
    if (!document->commitProject(message, error))
    {
        live = before;
        return false;
    }
    return true;
}

} // namespace

bool EditActions::addMarker(const model::Marker& marker, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    model::Marker candidate = marker;
    if (candidate.id.isEmpty())
        candidate.id = model::newUuid();
    candidate.frame = std::max(0, candidate.frame);
    if (!next::normalizeMarker(candidate, error))
        return false;

    const model::Project& project = m_session->project();
    for (const model::Marker& existing : project.markers)
    {
        if (existing.id == candidate.id)
        {
            setError(error, QStringLiteral("标记 id 已存在：%1").arg(candidate.id));
            return false;
        }
    }

    model::Project working = project;
    working.markers.append(candidate);
    return applyMarkerPlan(m_session, working,
                           QStringLiteral("添加标记 %1").arg(candidate.title),
                           error);
}

bool EditActions::updateMarker(const model::Marker& marker, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    model::Marker candidate = marker;
    if (!next::normalizeMarker(candidate, error))
        return false;

    const model::Project& project = m_session->project();
    model::Project working = project;
    for (model::Marker& existing : working.markers)
    {
        if (existing.id != candidate.id)
            continue;
        existing = candidate;
        return applyMarkerPlan(m_session, working,
                               QStringLiteral("编辑标记 %1").arg(candidate.title),
                               error);
    }
    setError(error, QStringLiteral("标记不存在：%1").arg(candidate.id));
    return false;
}

bool EditActions::deleteMarker(const QString& id, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    const model::Project& project = m_session->project();
    model::Project working = project;
    for (int i = 0; i < working.markers.size(); ++i)
    {
        if (working.markers.at(i).id != id)
            continue;
        const QString title = working.markers.at(i).title;
        working.markers.removeAt(i);
        return applyMarkerPlan(m_session, working,
                               QStringLiteral("删除标记 %1").arg(title), error);
    }
    setError(error, QStringLiteral("标记不存在：%1").arg(id));
    return false;
}

bool EditActions::moveMarker(const QString& id, int frame, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    const model::Project& project = m_session->project();
    model::Project working = project;
    for (model::Marker& marker : working.markers)
    {
        if (marker.id != id)
            continue;
        marker.frame = std::max(0, frame);
        return applyMarkerPlan(m_session, working,
                               QStringLiteral("移动标记 %1").arg(marker.title),
                               error);
    }
    setError(error, QStringLiteral("标记不存在：%1").arg(id));
    return false;
}

namespace {

// ProjectDocument validates the whole tree and then saves every upsert before
// deleting the removed entities.  A trim can therefore leave a to-be-deleted
// sibling outside its parent's new range while the parent tree is written.
// Normalise the doomed entities to their parent's range (and drop their
// keyframes/solvers) so the intermediate tree is valid; they are removed
// immediately afterwards.  The final model/file state is exactly the plan's.
void prepareDeletedForWrite(model::Project& live, const QVector<Uuid>& deletes)
{
    QSet<Uuid> deleting(deletes.begin(), deletes.end());
    QSet<Uuid> done;
    bool progress = true;
    while (progress)
    {
        progress = false;
        for (const Uuid& uuid : deletes)
        {
            if (done.contains(uuid))
                continue;
            auto it = live.entities.find(uuid);
            if (it == live.entities.end())
            {
                done.insert(uuid);
                continue;
            }
            const Uuid parent = it->parentUuid;
            if (!parent.isEmpty() && deleting.contains(parent)
                && !done.contains(parent))
                continue; // parent first so the range chain stays valid

            int start = 0;
            int end = 1;
            if (!parent.isEmpty())
            {
                const auto parentIt = live.entities.constFind(parent);
                if (parentIt != live.entities.constEnd())
                {
                    start = parentIt->start;
                    end = parentIt->end;
                }
            }
            else
            {
                it->followParentRange = false;
            }
            it->start = start;
            it->end = end;
            it->keyframes.clear();
            it->solvers.clear();
            done.insert(uuid);
            progress = true;
        }
    }
}

} // namespace

bool EditActions::apply(const EditPlan& plan, QString* error)
{
    if (error)
        error->clear();
    if (!isValid())
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (!m_session->humanEditingEnabled())
    {
        setError(error, QStringLiteral("AI 正在修改工程，请稍后再编辑，或先按“急停”"));
        return false;
    }
    if (plan.isEmpty())
        return true;

    next::ProjectDocument* document = m_session->document();
    const model::Project before = document->project();

    if (plan.projectChanged)
    {
        model::Project& live = document->mutableProject();
        live = before;
        live.markers = plan.project.markers;
        QString validationError;
        if (!model::validateProject(live, &validationError))
        {
            live = before;
            setError(error, QStringLiteral("标记校验失败：%1").arg(validationError));
            return false;
        }
        if (!document->commitProject(plan.message, error))
        {
            live = before;
            return false;
        }
        return true;
    }

    for (const Uuid& uuid : plan.upserts)
    {
        if (!plan.project.entities.contains(uuid))
        {
            setError(error, QStringLiteral("编辑结果缺少实体：%1").arg(uuid));
            return false;
        }
    }

    model::Project& live = document->mutableProject();
    live = before;
    for (const Uuid& uuid : plan.upserts)
        live.entities.insert(uuid, plan.project.entities.value(uuid));
    prepareDeletedForWrite(live, plan.deletes);

    const bool ok =
        plan.upserts.isEmpty()
            ? document->deleteEntities(plan.deletes, plan.message, error)
            : document->commitChanges(plan.upserts, plan.deletes, plan.message,
                                      error);
    if (!ok)
    {
        live = before;
        return false;
    }
    return true;
}

} // namespace pvf
