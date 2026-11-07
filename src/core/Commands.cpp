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

RemoveKeyframesCommand::RemoveKeyframesCommand(const QVector<KeyframeRecord>& records)
    : m_records(records)
{
}

void RemoveKeyframesCommand::undo(Project& project)
{
    for (const KeyframeRecord& record : m_records)
        project.addKeyframe(record.entity, record.property, record.frame, record.value);
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
