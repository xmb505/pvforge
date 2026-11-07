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

} // namespace pvf
