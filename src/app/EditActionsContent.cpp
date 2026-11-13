#include "EditActions.h"

#include "EditActionsSupport.h"
#include "EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Query.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/MarkerDialog.h"

#include <QSet>

#include <algorithm>

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

    EditPlan plan;
    plan.project = project;
    model::Entity& entity = plan.project.entities[uuid];
    if (!mutate(entity))
    {
        setError(error, QStringLiteral("没有可应用的修改"));
        return false;
    }

    QString validationError;
    if (!model::validateEntity(entity, &validationError))
    {
        setError(error, QStringLiteral("实体校验失败：%1").arg(validationError));
        return false;
    }

    plan.upserts = {uuid};
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
    case ContentKind::ComicText:
    case ContentKind::RadialLines:
    case ContentKind::FlowLines:
    case ContentKind::HalftoneDots:
    case ContentKind::BrandWall:
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
    if (targetRow < 0
        || rowIsOccupied(project, parent, targetRow, start, end))
        targetRow = model::nextAutoRow(project, parent, start, end);
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
        model::Entity audio;
        audio.uuid = model::newUuid();
        audio.name = name + QStringLiteral(" 音频");
        audio.parentUuid = parent;
        audio.start = start;
        audio.end = end;
        audio.row = model::nextAutoRow(plan.project, parent, start, end);
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

bool EditActions::replaceAsset(const Uuid& uuid, const AssetHash& hash,
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

    if (needsTrim)
    {
        for (auto child = project.entities.constBegin();
             child != project.entities.constEnd(); ++child)
        {
            if (child.value().parentUuid != uuid)
                continue;
            if (child.value().end > newEnd)
            {
                setError(error,
                         QStringLiteral("换素材会裁剪子实体 %1，请先裁剪子实体")
                             .arg(displayName(child.value())));
                return false;
            }
        }
    }

    const assets::StoredAsset asset = *stored;
    return updateEntity(
        uuid,
        QStringLiteral("pvf: assign asset \"%1\" to \"%2\"")
            .arg(asset.name, displayName(entity)),
        [asset, sourceIn, needsTrim, duration](model::Entity& target) {
            if (needsTrim)
            {
                target.end = target.start + duration;
                for (auto track = target.tracks.begin();
                     track != target.tracks.end();)
                {
                    QVector<model::Keyframe>& keys = track.value().keys;
                    keys.erase(std::remove_if(
                                   keys.begin(), keys.end(),
                                   [duration](const model::Keyframe& key) {
                                       return key.frame >= duration;
                                   }),
                               keys.end());
                    if (keys.isEmpty())
                        track = target.tracks.erase(track);
                    else
                        ++track;
                }
                QVector<model::SolverSegment> solvers;
                for (model::SolverSegment& solver : target.solvers)
                {
                    if (solver.startFrame >= duration)
                        continue;
                    if (solver.endFrame != -1 && solver.endFrame >= duration)
                        solver.endFrame = duration - 1;
                    solvers.append(solver);
                }
                target.solvers = solvers;
            }
            target.content.kind = ContentKind::Asset;
            target.content.asset.hash = asset.hash;
            target.content.asset.type = asset.type;
            target.content.asset.sourceIn = sourceIn;
            return true;
        },
        error);
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
