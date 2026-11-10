#include "EditActions.h"

#include "EditorSession.h"
#include "model/Query.h"
#include "model/Uuid.h"
#include "next/app/ProjectDocument.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"
#include "next/timeline/MarkerDialog.h"

#include <QMessageBox>
#include <QSet>

#include <algorithm>
#include <climits>

namespace pvf {

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

void collectSubtree(const model::Project& project, const Uuid& uuid,
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

QVector<Uuid> subtree(const model::Project& project, const Uuid& uuid)
{
    QVector<Uuid> result;
    QSet<Uuid> visited;
    collectSubtree(project, uuid, &result, &visited);
    return result;
}

bool hasAncestorIn(const model::Project& project, const Uuid& uuid,
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

bool isDescendant(const model::Project& project, const Uuid& candidate,
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
QVector<Uuid> uniqueRoots(const model::Project& project,
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

QString lockedSubtreeError(const model::Project& project,
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

void addUpsert(QVector<Uuid>* upserts, QSet<Uuid>* seen, const Uuid& uuid)
{
    if (seen->contains(uuid))
        return;
    seen->insert(uuid);
    upserts->append(uuid);
}

int parentStart(const model::Project& project, const model::Entity& entity)
{
    if (entity.parentUuid.isEmpty())
        return 0;
    const auto it = project.entities.constFind(entity.parentUuid);
    return it == project.entities.constEnd() ? 0 : it.value().start;
}

QStringList solverDrivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool solverStartKey(const model::Entity& entity, const QString& track,
                    int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (solver.startFrame == frame
            && solverDrivenTracks(solver.effect).contains(track))
            return true;
    }
    return false;
}

// A normal key inside a solver segment is invalid; the start key is the
// segment's initial state and stays editable.
bool trackBlockedAt(const model::Entity& entity, const QString& track,
                    int frame)
{
    for (const model::SolverSegment& solver : entity.solvers)
    {
        if (!solverDrivenTracks(solver.effect).contains(track))
            continue;
        const int end = solver.endFrame < 0 ? entity.duration() - 1
                                            : solver.endFrame;
        if (frame > solver.startFrame && frame <= end)
            return true;
    }
    return false;
}

bool stepTrack(const QString& track)
{
    return track == QLatin1String("visible")
           || track == QLatin1String("physicsInteract");
}

std::optional<double> staticTrackValue(const model::Entity& entity,
                                       const QString& track)
{
    const model::Transform& transform = entity.transform;
    if (track == QLatin1String("x"))
        return transform.x;
    if (track == QLatin1String("y"))
        return transform.y;
    if (track == QLatin1String("rotation"))
        return transform.rotation;
    if (track == QLatin1String("scaleX"))
        return transform.scaleX;
    if (track == QLatin1String("scaleY"))
        return transform.scaleY;
    if (track == QLatin1String("pivotX"))
        return transform.pivotX;
    if (track == QLatin1String("pivotY"))
        return transform.pivotY;
    if (track == QLatin1String("opacity"))
        return transform.opacity;
    if (track == QLatin1String("visible"))
        return entity.visible ? 1.0 : 0.0;
    if (track == QLatin1String("physicsInteract"))
        return entity.physicsInteract ? 1.0 : 0.0;

    const model::Content& content = entity.content;
    if (content.kind == ContentKind::Text)
    {
        const model::TextContent& text = content.text;
        if (track == QLatin1String("fontSize"))
            return text.pixelSize;
        if (track == QLatin1String("lineHeight"))
            return text.lineHeight;
        if (track == QLatin1String("letterSpacing"))
            return text.letterSpacing;
        if (track == QLatin1String("strokeWidth"))
            return text.strokeWidth;
        if (track == QLatin1String("padding"))
            return text.padding;
        if (track == QLatin1String("boxWidth"))
            return text.boxWidth;
        if (track == QLatin1String("boxHeight"))
            return text.boxHeight;
    }
    else if (content.kind == ContentKind::Vector)
    {
        const model::VectorContent& vector = content.vector;
        if (track == QLatin1String("width"))
            return vector.width;
        if (track == QLatin1String("height"))
            return vector.height;
        if (track == QLatin1String("cornerRadius"))
            return vector.cornerRadius;
        if (track == QLatin1String("strokeWidth"))
            return vector.strokeWidth;
    }
    else if (content.kind == ContentKind::Asset
             && content.asset.type == AssetType::Audio
             && track == QLatin1String("volume"))
    {
        return content.asset.volume;
    }
    return std::nullopt;
}

double trackValueAt(const model::Entity& entity, const QString& track,
                    int frame)
{
    const auto it = entity.tracks.constFind(track);
    if (it != entity.tracks.constEnd())
    {
        if (const auto value =
                model::evaluateTrack(it.value(), frame, stepTrack(track)))
            return *value;
    }
    return staticTrackValue(entity, track).value_or(0.0);
}

void sortTrack(model::Track& track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const model::Keyframe& a, const model::Keyframe& b) {
                  return a.frame < b.frame;
              });
}

QString keyLabel(const KeyRef& key)
{
    return key.track.isEmpty() ? QStringLiteral("全部轨道") : key.track;
}


} // namespace

EditActions::EditActions(EditorSession* session)
    : m_session(session)
{
}

bool EditActions::isValid() const
{
    return m_session && m_session->document();
}

bool EditActions::planMove(const QVector<Uuid>& roots, int deltaFrames,
                           const QVector<Uuid>& siblingOrder, EditPlan* plan,
                           QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (roots.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的片段"));
        return false;
    }

    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return false;

    QVector<Uuid> allNodes;
    QSet<Uuid> nodeSet;
    for (const Uuid& root : selected)
    {
        for (const Uuid& uuid : subtree(project, root))
            addUpsert(&allNodes, &nodeSet, uuid);
    }
    const QString locked = lockedSubtreeError(project, allNodes);
    if (!locked.isEmpty())
    {
        setError(error, locked);
        return false;
    }

    for (const Uuid& root : selected)
    {
        const model::Entity& entity = project.entities[root];
        const int lower = parentStart(project, entity);
        if (entity.start + deltaFrames < lower)
        {
            const QString parent =
                entity.parentUuid.isEmpty()
                    ? QStringLiteral("时间轴起点")
                    : displayName(project.entities.value(entity.parentUuid));
            setError(error,
                     QStringLiteral("片段 %1 不能移动到 %2 之前")
                         .arg(displayName(entity), parent));
            return false;
        }
        if (entity.end + deltaFrames <= 0)
        {
            setError(error, QStringLiteral("片段 %1 的结束帧必须大于 0")
                                .arg(displayName(entity)));
            return false;
        }
    }

    model::Project working = project;
    for (const Uuid& uuid : allNodes)
    {
        model::Entity& entity = working.entities[uuid];
        entity.start += deltaFrames;
        entity.end += deltaFrames;
    }

    QVector<Uuid> upserts;
    QSet<Uuid> upsertSet;
    for (const Uuid& uuid : allNodes)
        addUpsert(&upserts, &upsertSet, uuid);

    if (!siblingOrder.isEmpty())
    {
        if (selected.size() != 1)
        {
            setError(error, QStringLiteral("一次只能调整一个片段的行序"));
            return false;
        }
        const Uuid root = selected.first();
        const model::Entity& entity = project.entities[root];
        const QVector<Uuid> siblings =
            model::childrenOf(project, entity.parentUuid);
        QSet<Uuid> expected(siblings.begin(), siblings.end());
        QSet<Uuid> actual(siblingOrder.begin(), siblingOrder.end());
        if (siblings.size() != siblingOrder.size() || expected != actual)
        {
            setError(error, QStringLiteral("行序列表与父级下的片段不一致"));
            return false;
        }
        for (int row = 0; row < siblingOrder.size(); ++row)
        {
            const Uuid uuid = siblingOrder.at(row);
            model::Entity& sibling = working.entities[uuid];
            if (sibling.row == row)
                continue;
            sibling.row = row;
            addUpsert(&upserts, &upsertSet, uuid);
        }
    }

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("移动后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = upserts;
    plan->deletes.clear();
    plan->message = siblingOrder.isEmpty() ? QStringLiteral("移动片段")
                                           : QStringLiteral("调整片段顺序");
    return true;
}

bool EditActions::planTrim(const Uuid& uuid, int newStart, int newEnd,
                           EditPlan* plan, QString* error) const
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
    if (!next::applyEntityRange(m_session->project(), m_session->assets(), uuid,
                                newStart, newEnd, &working, &upserts, &deletes,
                                error))
        return false;

    plan->project = std::move(working);
    plan->upserts = std::move(upserts);
    plan->deletes = std::move(deletes);
    plan->message = QStringLiteral("裁剪片段");
    return true;
}

bool EditActions::planReparent(const Uuid& uuid, const Uuid& newParent,
                               bool keepWorld, EditPlan* plan,
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
    const model::Entity& entity = it.value();
    if (!newParent.isEmpty() && !project.entities.contains(newParent))
    {
        setError(error, QStringLiteral("父级实体不存在：%1").arg(newParent));
        return false;
    }
    if (newParent == entity.parentUuid)
    {
        plan->project = project;
        plan->upserts.clear();
        plan->deletes.clear();
        plan->message = QStringLiteral("更改父级");
        return true;
    }
    if (!newParent.isEmpty() && isDescendant(project, newParent, uuid))
    {
        setError(error, QStringLiteral("不能把片段 %1 挂到它自己或它的子级下")
                            .arg(displayName(entity)));
        return false;
    }

    const QVector<Uuid> nodes = subtree(project, uuid);
    const QString locked = lockedSubtreeError(project, nodes);
    if (!locked.isEmpty())
    {
        setError(error, locked);
        return false;
    }
    if (!newParent.isEmpty() && next::isEntityLocked(project, newParent))
    {
        setError(error, QStringLiteral("新父级 %1 或其祖先已锁定")
                            .arg(displayName(project.entities.value(newParent))));
        return false;
    }

    const int oldParentStart = parentStart(project, entity);
    int newParentStart = 0;
    if (!newParent.isEmpty())
        newParentStart = project.entities.value(newParent).start;
    const int delta = keepWorld ? 0 : newParentStart - oldParentStart;
    const int newStart = entity.start + delta;
    const int newEnd = entity.end + delta;
    if (newStart < 0)
    {
        setError(error, QStringLiteral("片段 %1 的新开始帧为 %2")
                            .arg(displayName(entity))
                            .arg(newStart));
        return false;
    }
    if (!newParent.isEmpty())
    {
        const model::Entity& parent = project.entities.value(newParent);
        if (newStart < parent.start || newEnd > parent.end)
        {
            setError(error,
                     QStringLiteral("%1后，片段 %2 的范围 [%3, %4) 超出新父级 %5 的 [%6, %7)")
                         .arg(keepWorld ? QStringLiteral("保持世界位置")
                                        : QStringLiteral("保持局部坐标"))
                         .arg(displayName(entity))
                         .arg(newStart)
                         .arg(newEnd)
                         .arg(displayName(parent))
                         .arg(parent.start)
                         .arg(parent.end));
            return false;
        }
    }

    model::Project working = project;
    QVector<Uuid> upserts;
    QSet<Uuid> upsertSet;
    for (const Uuid& node : nodes)
    {
        model::Entity& child = working.entities[node];
        child.start += delta;
        child.end += delta;
        addUpsert(&upserts, &upsertSet, node);
    }

    model::Entity& moved = working.entities[uuid];
    moved.parentUuid = newParent;
    int maxRow = -1;
    for (const Uuid& sibling : model::childrenOf(project, newParent))
    {
        if (sibling == uuid)
            continue;
        maxRow = std::max(maxRow, project.entities.value(sibling).row);
    }
    moved.row = maxRow + 1;

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("更改父级后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts = upserts;
    plan->deletes.clear();
    plan->message = QStringLiteral("更改父级");
    return true;
}

bool EditActions::planDelete(const QVector<Uuid>& roots, EditPlan* plan,
                             QString* error) const
{
    if (error)
        error->clear();
    if (!isValid() || !plan)
    {
        setError(error, QStringLiteral("内部错误：没有可编辑的工程"));
        return false;
    }
    if (roots.isEmpty())
    {
        setError(error, QStringLiteral("没有选中的片段"));
        return false;
    }

    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, error);
    if (selected.isEmpty())
        return false;

    QVector<Uuid> deletes;
    QSet<Uuid> seen;
    for (const Uuid& root : selected)
    {
        for (const Uuid& uuid : subtree(project, root))
        {
            const QString locked = lockedSubtreeError(project, {uuid});
            if (!locked.isEmpty())
            {
                setError(error, locked);
                return false;
            }
            addUpsert(&deletes, &seen, uuid);
        }
    }

    model::Project working = project;
    for (const Uuid& uuid : deletes)
        working.entities.remove(uuid);

    QString validationError;
    if (!model::validateProject(working, &validationError))
    {
        setError(error, QStringLiteral("删除后工程校验失败：%1")
                            .arg(validationError));
        return false;
    }

    plan->project = std::move(working);
    plan->upserts.clear();
    plan->deletes = deletes;
    plan->message = QStringLiteral("删除片段");
    return true;
}

QStringList EditActions::trackNames(const Uuid& uuid) const
{
    QStringList result;
    if (!isValid())
        return result;
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return result;

    const model::Entity& entity = it.value();
    for (auto track = entity.tracks.constBegin(); track != entity.tracks.constEnd();
         ++track)
        if (!result.contains(track.key()))
            result.append(track.key());

    auto add = [&result](const QString& name) {
        if (!result.contains(name))
            result.append(name);
    };
    add(QStringLiteral("x"));
    add(QStringLiteral("y"));
    add(QStringLiteral("rotation"));
    add(QStringLiteral("scaleX"));
    add(QStringLiteral("scaleY"));
    if (entity.content.kind == ContentKind::Camera)
        return result;
    add(QStringLiteral("opacity"));
    add(QStringLiteral("pivotX"));
    add(QStringLiteral("pivotY"));
    add(QStringLiteral("visible"));
    add(QStringLiteral("physicsInteract"));

    switch (entity.content.kind)
    {
    case ContentKind::Text:
        add(QStringLiteral("fontSize"));
        add(QStringLiteral("lineHeight"));
        add(QStringLiteral("letterSpacing"));
        add(QStringLiteral("strokeWidth"));
        add(QStringLiteral("padding"));
        add(QStringLiteral("boxWidth"));
        add(QStringLiteral("boxHeight"));
        break;
    case ContentKind::Vector:
        add(QStringLiteral("width"));
        add(QStringLiteral("height"));
        add(QStringLiteral("cornerRadius"));
        add(QStringLiteral("strokeWidth"));
        break;
    case ContentKind::Asset:
        if (entity.content.asset.type == AssetType::Audio)
            add(QStringLiteral("volume"));
        break;
    default:
        break;
    }
    return result;
}

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

int EditActions::minMoveDelta(const QVector<Uuid>& roots) const
{
    if (!isValid())
        return 0;
    const model::Project& project = m_session->project();
    const QVector<Uuid> selected = uniqueRoots(project, roots, nullptr);
    int minimum = INT_MIN;
    for (const Uuid& root : selected)
    {
        const model::Entity& entity = project.entities[root];
        minimum = std::max(minimum, parentStart(project, entity) - entity.start);
    }
    return minimum == INT_MIN ? 0 : std::min(0, minimum);
}

int EditActions::minTrimStart(const Uuid& uuid) const
{
    if (!isValid())
        return 0;
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return 0;
    const model::Entity& entity = it.value();
    int minimum = parentStart(project, entity);
    if (entity.content.kind == ContentKind::Asset
        && (entity.content.asset.type == AssetType::Video
            || entity.content.asset.type == AssetType::Audio))
    {
        minimum = std::max(minimum,
                           entity.start - entity.content.asset.sourceIn);
    }
    return minimum;
}

int EditActions::maxTrimEnd(const Uuid& uuid) const
{
    if (!isValid())
        return std::numeric_limits<int>::max();
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return std::numeric_limits<int>::max();
    const model::Entity& entity = it.value();
    int maximum = std::numeric_limits<int>::max();
    if (!entity.parentUuid.isEmpty())
    {
        const auto parent = project.entities.constFind(entity.parentUuid);
        if (parent != project.entities.constEnd())
            maximum = std::min(maximum, parent.value().end);
    }
    const int frames = next::timedAssetDurationFrames(m_session->assets(), entity);
    if (frames > 0)
    {
        maximum = std::min(
            maximum, entity.start + std::max(0, frames - entity.content.asset.sourceIn));
    }
    return maximum;
}

bool EditActions::moveClips(const QVector<Uuid>& roots, int deltaFrames,
                            QString* error)
{
    return moveClips(roots, deltaFrames, {}, error);
}

bool EditActions::moveClips(const QVector<Uuid>& roots, int deltaFrames,
                            const QVector<Uuid>& siblingOrder, QString* error)
{
    EditPlan plan;
    if (!planMove(roots, deltaFrames, siblingOrder, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::trimClip(const Uuid& uuid, int newStart, int newEnd,
                           QString* error)
{
    EditPlan plan;
    if (!planTrim(uuid, newStart, newEnd, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::reparent(const Uuid& uuid, const Uuid& newParent,
                           bool keepWorld, QString* error)
{
    EditPlan plan;
    if (!planReparent(uuid, newParent, keepWorld, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::deleteEntities(const QVector<Uuid>& roots, bool confirm,
                                 QWidget* parent, QString* error)
{
    EditPlan plan;
    if (!planDelete(roots, &plan, error))
        return false;
    if (confirm)
    {
        const auto answer = QMessageBox::question(
            parent, QStringLiteral("删除片段"),
            QStringLiteral("删除 %1 个片段及其子片段？\n\n此操作可用撤销恢复。")
                .arg(plan.deletes.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            if (error)
                error->clear();
            return false;
        }
    }
    return apply(plan, error);
}

bool EditActions::addKeyframe(const Uuid& uuid, const QString& track, int frame,
                              QString* error)
{
    EditPlan plan;
    if (!planKeyframe(uuid, track, frame, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::deleteKeyframes(const QVector<KeyRef>& keys, QString* error)
{
    EditPlan plan;
    if (!planDeleteKeyframes(keys, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::moveKeyframes(const QVector<KeyRef>& keys, int delta,
                                QString* error)
{
    EditPlan plan;
    if (!planMoveKeyframes(keys, delta, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::setKeyframeEasing(const QVector<KeyRef>& keys,
                                    const model::EasingSpec& easing,
                                    QString* error)
{
    EditPlan plan;
    if (!planSetKeyframeEasing(keys, easing, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::pasteKeyframes(const Uuid& uuid,
                                 const QVector<TrackKey>& keys, QString* error)
{
    EditPlan plan;
    if (!planPasteKeyframes(uuid, keys, &plan, error))
        return false;
    return apply(plan, error);
}

bool EditActions::splitClip(const Uuid& uuid, int frame, QString* error)
{
    EditPlan plan;
    if (!planSplit(uuid, frame, &plan, error))
        return false;
    return apply(plan, error);
}

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
