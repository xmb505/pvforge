#include "Commands.h"

namespace pvf {

SetValueCommand::SetValueCommand(EntityId id, const QString& property, double frame,
                                 double oldValue, double newValue, bool keyframe)
    : m_id(id)
    , m_property(property)
    , m_frame(frame)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
    , m_keyframe(keyframe)
{
}

void SetValueCommand::undo(Project& project)
{
    project.setValue(m_id, m_property, m_frame, m_oldValue, m_keyframe);
}

void SetValueCommand::redo(Project& project)
{
    project.setValue(m_id, m_property, m_frame, m_newValue, m_keyframe);
}

QString SetValueCommand::text() const
{
    return QStringLiteral("修改属性");
}

bool SetValueCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* set = dynamic_cast<const SetValueCommand*>(other);
    if (!set)
        return false;
    return set->m_id == m_id && set->m_property == m_property
           && qFuzzyCompare(set->m_frame + 1.0, m_frame + 1.0)
           && set->m_keyframe == m_keyframe;
}

void SetValueCommand::mergeWith(const UndoCommand* other)
{
    const auto* set = dynamic_cast<const SetValueCommand*>(other);
    if (set)
        m_newValue = set->m_newValue;
}

AddEntityCommand::AddEntityCommand(const Entity& entity, int index)
    : m_entity(entity)
    , m_index(index)
{
}

void AddEntityCommand::undo(Project& project)
{
    if (!m_present)
        return;
    project.removeEntity(m_entity.id);
    m_present = false;
}

void AddEntityCommand::redo(Project& project)
{
    if (m_present)
        return;
    project.insertEntity(m_entity, m_index);
    m_present = true;
}

QString AddEntityCommand::text() const
{
    return QStringLiteral("创建实体");
}

AddEntitiesCommand::AddEntitiesCommand(QVector<Entity> entities, QVector<int> indices)
{
    const int count = std::min(entities.size(), indices.size());
    m_items.reserve(count);
    for (int i = 0; i < count; ++i)
        m_items.append(Item{entities[i], indices[i]});
}

void AddEntitiesCommand::undo(Project& project)
{
    if (!m_present)
        return;
    for (int i = int(m_items.size()) - 1; i >= 0; --i)
        project.removeEntity(m_items[i].entity.id);
    m_present = false;
}

void AddEntitiesCommand::redo(Project& project)
{
    if (m_present)
        return;
    for (const Item& item : m_items)
        project.insertEntity(item.entity, item.index);
    m_present = true;
}

QString AddEntitiesCommand::text() const
{
    return QStringLiteral("创建实体");
}

RemoveEntityCommand::RemoveEntityCommand(const Entity& entity, int index)
    : m_entity(entity)
    , m_index(index)
{
}

void RemoveEntityCommand::undo(Project& project)
{
    if (m_present)
        return;
    project.insertEntity(m_entity, m_index);
    m_present = true;
}

void RemoveEntityCommand::redo(Project& project)
{
    if (!m_present)
        return;
    project.removeEntity(m_entity.id);
    m_present = false;
}

QString RemoveEntityCommand::text() const
{
    return QStringLiteral("删除实体");
}

SetEntityFlagCommand::SetEntityFlagCommand(EntityId id, EntityFlag flag, bool oldValue,
                                         bool newValue)
    : m_id(id)
    , m_flag(flag)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void SetEntityFlagCommand::undo(Project& project)
{
    project.setEntityFlag(m_id, m_flag, m_oldValue);
}

void SetEntityFlagCommand::redo(Project& project)
{
    project.setEntityFlag(m_id, m_flag, m_newValue);
}

QString SetEntityFlagCommand::text() const
{
    return m_flag == EntityFlag::Hidden ? QStringLiteral("隐藏实体")
                                        : QStringLiteral("锁定实体");
}

RenameEntityCommand::RenameEntityCommand(EntityId id, const QString& oldName,
                                     const QString& newName)
    : m_id(id)
    , m_oldName(oldName)
    , m_newName(newName)
{
}

void RenameEntityCommand::undo(Project& project)
{
    project.setEntityName(m_id, m_oldName);
}

void RenameEntityCommand::redo(Project& project)
{
    project.setEntityName(m_id, m_newName);
}

QString RenameEntityCommand::text() const
{
    return QStringLiteral("重命名");
}

SetParentCommand::SetParentCommand(EntityId id, EntityId oldParent, EntityId newParent)
    : m_id(id)
    , m_oldParent(oldParent)
    , m_newParent(newParent)
{
}

void SetParentCommand::undo(Project& project)
{
    project.setEntityParent(m_id, m_oldParent);
}

void SetParentCommand::redo(Project& project)
{
    project.setEntityParent(m_id, m_newParent);
}

QString SetParentCommand::text() const
{
    return QStringLiteral("修改父级");
}

MoveEntityCommand::MoveEntityCommand(EntityId id, EntityId oldParent,
                                     EntityId newParent, int oldIndex, int newIndex)
    : m_id(id)
    , m_oldParent(oldParent)
    , m_newParent(newParent)
    , m_oldIndex(oldIndex)
    , m_newIndex(newIndex)
{
}

void MoveEntityCommand::undo(Project& project)
{
    project.moveEntity(m_id, m_oldParent, m_oldIndex);
}

void MoveEntityCommand::redo(Project& project)
{
    project.moveEntity(m_id, m_newParent, m_newIndex);
}

QString MoveEntityCommand::text() const
{
    return QStringLiteral("调整图层顺序");
}

SetEntityDataCommand::SetEntityDataCommand(EntityId id, const QString& key,
                                           const QVariant& oldValue,
                                           const QVariant& newValue, const QString& text)
    : m_id(id)
    , m_key(key)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
    , m_text(text)
{
}

void SetEntityDataCommand::undo(Project& project)
{
    project.setEntityData(m_id, m_key, m_oldValue);
}

void SetEntityDataCommand::redo(Project& project)
{
    project.setEntityData(m_id, m_key, m_newValue);
}

QString SetEntityDataCommand::text() const
{
    return m_text;
}

EntitiesSnapshotCommand::EntitiesSnapshotCommand(QVector<Entity> before,
                                                 QVector<Entity> after,
                                                 QString text)
    : m_before(std::move(before))
    , m_after(std::move(after))
    , m_text(std::move(text))
{
}

void EntitiesSnapshotCommand::undo(Project& project)
{
    project.setEntities(m_before);
}

void EntitiesSnapshotCommand::redo(Project& project)
{
    project.setEntities(m_after);
}

QString EntitiesSnapshotCommand::text() const
{
    return m_text;
}

ReplaceEntitySourcesCommand::ReplaceEntitySourcesCommand(QVector<Entity> before,
                                                         QVector<Entity> after)
    : m_before(std::move(before))
    , m_after(std::move(after))
{
}

void ReplaceEntitySourcesCommand::undo(Project& project)
{
    for (const Entity& entity : m_before)
        project.replaceEntity(entity);
}

void ReplaceEntitySourcesCommand::redo(Project& project)
{
    for (const Entity& entity : m_after)
        project.replaceEntity(entity);
}

QString ReplaceEntitySourcesCommand::text() const
{
    return QStringLiteral("更换素材");
}

SetKeyframeCommand::SetKeyframeCommand(EntityId id, const QString& property, double frame,
                                       double value, bool add)
    : m_id(id)
    , m_property(property)
    , m_frame(frame)
    , m_value(value)
    , m_add(add)
{
}

void SetKeyframeCommand::apply(Project& project, bool add)
{
    if (add)
        project.setValue(m_id, m_property, m_frame, m_value, true);
    else
        project.removeKeyframe(m_id, m_property, m_frame);
}

void SetKeyframeCommand::undo(Project& project)
{
    apply(project, !m_add);
}

void SetKeyframeCommand::redo(Project& project)
{
    apply(project, m_add);
}

QString SetKeyframeCommand::text() const
{
    return m_add ? QStringLiteral("打关键帧") : QStringLiteral("删除关键帧");
}

SetEntityDataMapCommand::SetEntityDataMapCommand(EntityId id, const QVariantMap& before,
                                                 const QVariantMap& after,
                                                 const QString& text)
    : m_id(id)
    , m_before(before)
    , m_after(after)
    , m_text(text)
{
}

void SetEntityDataMapCommand::undo(Project& project)
{
    project.setEntityDataMap(m_id, m_before);
}

void SetEntityDataMapCommand::redo(Project& project)
{
    project.setEntityDataMap(m_id, m_after);
}

QString SetEntityDataMapCommand::text() const
{
    return m_text;
}

bool SetEntityDataMapCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* command = dynamic_cast<const SetEntityDataMapCommand*>(other);
    // Only fold consecutive edits of the same fields into one undo step.
    // Changing volume and then trim should stay two separate undos.
    return command && command->m_id == m_id && command->m_after.keys() == m_after.keys();
}

void SetEntityDataMapCommand::mergeWith(const UndoCommand* other)
{
    const auto* command = dynamic_cast<const SetEntityDataMapCommand*>(other);
    if (!command)
        return;
    // Keep the oldest "before" values and take the newest "after" values.
    for (auto it = command->m_after.constBegin(); it != command->m_after.constEnd();
         ++it)
        m_after.insert(it.key(), it.value());
}

SetPositionCommand::SetPositionCommand(EntityId id, int frame, double oldX, double oldY,
                                       double newX, double newY, bool keyframe)
    : m_id(id)
    , m_frame(frame)
    , m_oldX(oldX)
    , m_oldY(oldY)
    , m_newX(newX)
    , m_newY(newY)
    , m_keyframe(keyframe)
{
}

void SetPositionCommand::undo(Project& project)
{
    project.setValue(m_id, QStringLiteral("x"), m_frame, m_oldX, m_keyframe);
    project.setValue(m_id, QStringLiteral("y"), m_frame, m_oldY, m_keyframe);
}

void SetPositionCommand::redo(Project& project)
{
    project.setValue(m_id, QStringLiteral("x"), m_frame, m_newX, m_keyframe);
    project.setValue(m_id, QStringLiteral("y"), m_frame, m_newY, m_keyframe);
}

QString SetPositionCommand::text() const
{
    return QStringLiteral("移动实体");
}

bool SetPositionCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* move = dynamic_cast<const SetPositionCommand*>(other);
    return move && move->m_id == m_id && move->m_frame == m_frame;
}

void SetPositionCommand::mergeWith(const UndoCommand* other)
{
    const auto* move = dynamic_cast<const SetPositionCommand*>(other);
    if (move)
    {
        m_newX = move->m_newX;
        m_newY = move->m_newY;
    }
}

SetScaleCommand::SetScaleCommand(EntityId id, int frame, double oldSx, double oldSy,
                                 double oldX, double oldY, double newSx, double newSy,
                                 double newX, double newY, bool keyframe)
    : m_id(id)
    , m_frame(frame)
    , m_oldSx(oldSx)
    , m_oldSy(oldSy)
    , m_newSx(newSx)
    , m_newSy(newSy)
    , m_oldX(oldX)
    , m_oldY(oldY)
    , m_newX(newX)
    , m_newY(newY)
    , m_keyframe(keyframe)
{
}

void SetScaleCommand::undo(Project& project)
{
    project.setValue(m_id, QStringLiteral("scaleX"), m_frame, m_oldSx, m_keyframe);
    project.setValue(m_id, QStringLiteral("scaleY"), m_frame, m_oldSy, m_keyframe);
    project.setValue(m_id, QStringLiteral("x"), m_frame, m_oldX, m_keyframe);
    project.setValue(m_id, QStringLiteral("y"), m_frame, m_oldY, m_keyframe);
}

void SetScaleCommand::redo(Project& project)
{
    project.setValue(m_id, QStringLiteral("scaleX"), m_frame, m_newSx, m_keyframe);
    project.setValue(m_id, QStringLiteral("scaleY"), m_frame, m_newSy, m_keyframe);
    project.setValue(m_id, QStringLiteral("x"), m_frame, m_newX, m_keyframe);
    project.setValue(m_id, QStringLiteral("y"), m_frame, m_newY, m_keyframe);
}

QString SetScaleCommand::text() const
{
    return QStringLiteral("缩放实体");
}

bool SetScaleCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* scale = dynamic_cast<const SetScaleCommand*>(other);
    return scale && scale->m_id == m_id && scale->m_frame == m_frame;
}

void SetScaleCommand::mergeWith(const UndoCommand* other)
{
    const auto* scale = dynamic_cast<const SetScaleCommand*>(other);
    if (scale)
    {
        m_newSx = scale->m_newSx;
        m_newSy = scale->m_newSy;
        m_newX = scale->m_newX;
        m_newY = scale->m_newY;
    }
}

SetCameraCommand::SetCameraCommand(EntityId id, int frame, double oldX, double oldY,
                                   double oldScale, double oldRotation, double newX,
                                   double newY, double newScale, double newRotation,
                                   bool keyframe)
    : m_id(id)
    , m_frame(frame)
    , m_oldX(oldX)
    , m_oldY(oldY)
    , m_oldScale(oldScale)
    , m_oldRotation(oldRotation)
    , m_newX(newX)
    , m_newY(newY)
    , m_newScale(newScale)
    , m_newRotation(newRotation)
    , m_keyframe(keyframe)
{
}

void SetCameraCommand::apply(Project& project, double x, double y, double scale,
                             double rotation)
{
    project.setValue(m_id, QStringLiteral("x"), m_frame, x, m_keyframe);
    project.setValue(m_id, QStringLiteral("y"), m_frame, y, m_keyframe);
    project.setValue(m_id, QStringLiteral("scaleX"), m_frame, scale, m_keyframe);
    project.setValue(m_id, QStringLiteral("scaleY"), m_frame, scale, m_keyframe);
    project.setValue(m_id, QStringLiteral("rotation"), m_frame, rotation, m_keyframe);
}

void SetCameraCommand::undo(Project& project)
{
    apply(project, m_oldX, m_oldY, m_oldScale, m_oldRotation);
}

void SetCameraCommand::redo(Project& project)
{
    apply(project, m_newX, m_newY, m_newScale, m_newRotation);
}

QString SetCameraCommand::text() const
{
    return QStringLiteral("调整摄像机");
}

bool SetCameraCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* camera = dynamic_cast<const SetCameraCommand*>(other);
    return camera && camera->m_id == m_id && camera->m_frame == m_frame;
}

void SetCameraCommand::mergeWith(const UndoCommand* other)
{
    const auto* camera = dynamic_cast<const SetCameraCommand*>(other);
    if (camera)
    {
        m_newX = camera->m_newX;
        m_newY = camera->m_newY;
        m_newScale = camera->m_newScale;
        m_newRotation = camera->m_newRotation;
    }
}

SetPivotCommand::SetPivotCommand(EntityId id, double oldAx, double oldAy, double oldX,
                                 double oldY, double newAx, double newAy, double newX,
                                 double newY, int frame)
    : m_id(id)
    , m_oldAx(oldAx)
    , m_oldAy(oldAy)
    , m_oldX(oldX)
    , m_oldY(oldY)
    , m_newAx(newAx)
    , m_newAy(newAy)
    , m_newX(newX)
    , m_newY(newY)
    , m_frame(frame)
{
}

void SetPivotCommand::apply(Project& project, double ax, double ay, double x, double y)
{
    project.setValue(m_id, QStringLiteral("rotPointX"), m_frame, ax, true);
    project.setValue(m_id, QStringLiteral("rotPointY"), m_frame, ay, true);
    project.setValue(m_id, QStringLiteral("x"), m_frame, x, true);
    project.setValue(m_id, QStringLiteral("y"), m_frame, y, true);
}

void SetPivotCommand::undo(Project& project)
{
    apply(project, m_oldAx, m_oldAy, m_oldX, m_oldY);
}

void SetPivotCommand::redo(Project& project)
{
    apply(project, m_newAx, m_newAy, m_newX, m_newY);
}

QString SetPivotCommand::text() const
{
    return QStringLiteral("调整轴心");
}

bool SetPivotCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* pivot = dynamic_cast<const SetPivotCommand*>(other);
    return pivot && pivot->m_id == m_id && pivot->m_frame == m_frame;
}

void SetPivotCommand::mergeWith(const UndoCommand* other)
{
    const auto* pivot = dynamic_cast<const SetPivotCommand*>(other);
    if (pivot)
    {
        m_newAx = pivot->m_newAx;
        m_newAy = pivot->m_newAy;
        m_newX = pivot->m_newX;
        m_newY = pivot->m_newY;
    }
}

SetKeyframeEasingCommand::SetKeyframeEasingCommand(
    const QVector<KeyframeEasingRecord>& records)
    : m_records(records)
{
}

void SetKeyframeEasingCommand::undo(Project& project)
{
    for (const KeyframeEasingRecord& record : m_records)
        project.setKeyframeEasing(record.entity, record.property, record.frame,
                                  record.oldEase);
}

void SetKeyframeEasingCommand::redo(Project& project)
{
    for (const KeyframeEasingRecord& record : m_records)
        project.setKeyframeEasing(record.entity, record.property, record.frame,
                                  record.newEase);
}

QString SetKeyframeEasingCommand::text() const
{
    return QStringLiteral("调整关键帧缓动");
}

bool SetKeyframeEasingCommand::canMergeWith(const UndoCommand* other) const
{
    const auto* command = dynamic_cast<const SetKeyframeEasingCommand*>(other);
    if (!command || command->m_records.size() != m_records.size())
        return false;
    for (int i = 0; i < m_records.size(); ++i)
    {
        if (command->m_records[i].entity != m_records[i].entity
            || command->m_records[i].property != m_records[i].property
            || !qFuzzyCompare(command->m_records[i].frame + 1.0,
                              m_records[i].frame + 1.0))
            return false;
    }
    return true;
}

void SetKeyframeEasingCommand::mergeWith(const UndoCommand* other)
{
    const auto* command = dynamic_cast<const SetKeyframeEasingCommand*>(other);
    if (!command)
        return;
    for (int i = 0; i < m_records.size(); ++i)
        m_records[i].newEase = command->m_records[i].newEase;
}

RemoveKeyframesCommand::RemoveKeyframesCommand(const QVector<KeyframeRecord>& records)
    : m_records(records)
{
}

void RemoveKeyframesCommand::undo(Project& project)
{
    for (const KeyframeRecord& record : m_records)
    {
        Keyframe key;
        key.frame = record.frame;
        key.value = record.value;
        key.ease = record.ease;
        key.note = record.note;
        project.addKeyframe(record.entity, record.property, key);
    }
}

void RemoveKeyframesCommand::redo(Project& project)
{
    for (const KeyframeRecord& record : m_records)
        project.removeKeyframesAt(record.entity, record.frame);
}

QString RemoveKeyframesCommand::text() const
{
    return QStringLiteral("删除关键帧");
}

namespace {

// Recreates the scene record on an existing folder. Project::addScene always
// creates a throwaway folder; it is removed right after the record takes the
// folder id that belongs to the scene.
void attachSceneRecord(Project& project, const Scene& scene)
{
    const bool folderExists = project.entity(scene.folderId) != nullptr;
    QString error;
    Scene* created = project.addScene(scene.name, scene.startFrame, scene.endFrame,
                                      scene.color, scene.note, &error);
    if (!created)
        return;
    const EntityId temporary = created->folderId;
    created->id = scene.id;
    if (folderExists)
    {
        created->folderId = scene.folderId;
        if (temporary != scene.folderId)
            project.removeEntity(temporary);
    }
    // If the folder is gone, keep the fresh folder the API just created.
}

Annotation* attachAnnotationRecord(Project& project, const Annotation& annotation)
{
    Annotation* created = project.addAnnotation(annotation.frame, annotation.title,
                                                annotation.note, annotation.tags,
                                                annotation.color);
    if (created)
        created->id = annotation.id;
    return created;
}

} // namespace

bool sceneIsRetired(const Project& project, const Scene& scene)
{
    const Entity* folder = project.entity(scene.folderId);
    return folder && folder->data.value(QStringLiteral("sceneRetired"), false).toBool();
}

AddSceneCommand::AddSceneCommand(const Scene& scene, const Entity& folder,
                                 int folderIndex)
    : m_scene(scene)
    , m_folder(folder)
    , m_folderIndex(folderIndex)
{
}

void AddSceneCommand::undo(Project& project)
{
    if (!m_present)
        return;
    project.removeScene(m_scene.id);
    // Children are normally removed by their own undo steps first. Keep the
    // folder if something is still inside it instead of orphaning entities.
    if (project.entity(m_scene.folderId) && !project.hasChildren(m_scene.folderId))
        project.removeEntity(m_scene.folderId);
    m_present = false;
}

void AddSceneCommand::redo(Project& project)
{
    if (m_present)
        return;

    bool inserted = false;
    if (!project.entity(m_scene.folderId))
    {
        project.insertEntity(m_folder, m_folderIndex);
        inserted = true;
    }
    QString error;
    Scene* created = project.addScene(m_scene.name, m_scene.startFrame,
                                      m_scene.endFrame, m_scene.color, m_scene.note,
                                      &error);
    if (!created)
    {
        if (inserted && m_folder.id == m_scene.folderId)
            project.removeEntity(m_scene.folderId);
        return;
    }
    const EntityId temporary = created->folderId;
    created->id = m_scene.id;
    created->folderId = m_scene.folderId;
    if (temporary != m_scene.folderId)
        project.removeEntity(temporary);
    m_present = true;
}

QString AddSceneCommand::text() const
{
    return QStringLiteral("新建场景");
}

UpdateSceneCommand::UpdateSceneCommand(const Scene& before, const Scene& after,
                                       EntityId folderId,
                                       const QString& oldFolderName,
                                       const QString& newFolderName)
    : m_before(before)
    , m_after(after)
    , m_folderId(folderId)
    , m_oldFolderName(oldFolderName)
    , m_newFolderName(newFolderName)
{
}

void UpdateSceneCommand::undo(Project& project)
{
    project.updateScene(m_before.id, m_before.name, m_before.startFrame,
                        m_before.endFrame, m_before.color, m_before.note);
    if (m_folderId != kInvalidEntity && m_oldFolderName != m_newFolderName)
        project.setEntityName(m_folderId, m_oldFolderName);
}

void UpdateSceneCommand::redo(Project& project)
{
    project.updateScene(m_after.id, m_after.name, m_after.startFrame,
                        m_after.endFrame, m_after.color, m_after.note);
    if (m_folderId != kInvalidEntity && m_oldFolderName != m_newFolderName)
        project.setEntityName(m_folderId, m_newFolderName);
}

QString UpdateSceneCommand::text() const
{
    return QStringLiteral("编辑场景");
}

RemoveSceneCommand::RemoveSceneCommand(const Scene& scene, const Entity& folder,
                                       int folderIndex)
    : m_scene(scene)
    , m_folder(folder)
    , m_folderIndex(folderIndex)
{
}

void RemoveSceneCommand::undo(Project& project)
{
    if (m_present)
        return;
    if (m_folder.id == m_scene.folderId && !project.entity(m_scene.folderId))
        project.insertEntity(m_folder, m_folderIndex);
    attachSceneRecord(project, m_scene);
    if (const Scene* restored = project.sceneById(m_scene.id))
        m_scene.folderId = restored->folderId;
    m_present = true;
}

void RemoveSceneCommand::redo(Project& project)
{
    if (!m_present)
        return;
    project.removeScene(m_scene.id);
    m_present = false;
}

QString RemoveSceneCommand::text() const
{
    return QStringLiteral("删除场景");
}

AddAnnotationCommand::AddAnnotationCommand(const Annotation& annotation)
    : m_annotation(annotation)
{
}

void AddAnnotationCommand::undo(Project& project)
{
    if (!m_present)
        return;
    project.removeAnnotation(m_annotation.id);
    m_present = false;
}

void AddAnnotationCommand::redo(Project& project)
{
    if (m_present)
        return;
    if (attachAnnotationRecord(project, m_annotation))
        m_present = true;
}

QString AddAnnotationCommand::text() const
{
    return QStringLiteral("新建注释");
}

UpdateAnnotationCommand::UpdateAnnotationCommand(const Annotation& before,
                                                 const Annotation& after)
    : m_before(before)
    , m_after(after)
{
}

void UpdateAnnotationCommand::undo(Project& project)
{
    project.updateAnnotation(m_before);
}

void UpdateAnnotationCommand::redo(Project& project)
{
    project.updateAnnotation(m_after);
}

QString UpdateAnnotationCommand::text() const
{
    return QStringLiteral("编辑注释");
}

RemoveAnnotationCommand::RemoveAnnotationCommand(const Annotation& annotation)
    : m_annotation(annotation)
{
}

void RemoveAnnotationCommand::undo(Project& project)
{
    if (m_present)
        return;
    if (attachAnnotationRecord(project, m_annotation))
        m_present = true;
}

void RemoveAnnotationCommand::redo(Project& project)
{
    if (!m_present)
        return;
    project.removeAnnotation(m_annotation.id);
    m_present = false;
}

QString RemoveAnnotationCommand::text() const
{
    return QStringLiteral("删除注释");
}

SetKeyframeNoteCommand::SetKeyframeNoteCommand(
    const QVector<KeyframeNoteRecord>& records)
    : m_records(records)
{
}

void SetKeyframeNoteCommand::apply(Project& project, bool useNew)
{
    for (const KeyframeNoteRecord& record : m_records)
        project.setKeyframeNote(record.entity, record.property, record.frame,
                                useNew ? record.newNote : record.oldNote);
}

void SetKeyframeNoteCommand::undo(Project& project)
{
    apply(project, false);
}

void SetKeyframeNoteCommand::redo(Project& project)
{
    apply(project, true);
}

QString SetKeyframeNoteCommand::text() const
{
    return QStringLiteral("编辑关键帧备注");
}

} // namespace pvf
