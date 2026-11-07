#pragma once

#include "core/History.h"

namespace pvf {

// Changing an animated property, with the previous value kept for undo.
class SetValueCommand : public UndoCommand
{
public:
    SetValueCommand(EntityId id, const QString& property, double frame, double oldValue,
                    double newValue, bool keyframe);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    EntityId m_id = kInvalidEntity;
    QString m_property;
    double m_frame = 0.0;
    double m_oldValue = 0.0;
    double m_newValue = 0.0;
    bool m_keyframe = true;
};

// Creating an entity.
class AddEntityCommand : public UndoCommand
{
public:
    AddEntityCommand(const Entity& entity, int index);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    Entity m_entity;
    int m_index = 0;
    bool m_present = true;   // the entity already exists when recorded
};

// Creating a group of entities as one undo step (video plus its extracted audio).
class AddEntitiesCommand : public UndoCommand
{
public:
    AddEntitiesCommand(QVector<Entity> entities, QVector<int> indices);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    struct Item
    {
        Entity entity;
        int index = 0;
    };
    QVector<Item> m_items;
    bool m_present = true;
};

// Removing an entity, keeping a copy so it can be restored.
class RemoveEntityCommand : public UndoCommand
{
public:
    RemoveEntityCommand(const Entity& entity, int index);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    Entity m_entity;
    int m_index = 0;
    bool m_present = false;  // the entity is already gone when recorded
};

// Toggling the hidden or locked flag of an entity.
class SetEntityFlagCommand : public UndoCommand
{
public:
    SetEntityFlagCommand(EntityId id, EntityFlag flag, bool oldValue, bool newValue);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    EntityId m_id = kInvalidEntity;
    EntityFlag m_flag = EntityFlag::Hidden;
    bool m_oldValue = false;
    bool m_newValue = false;
};

// Renaming an entity.
class RenameEntityCommand : public UndoCommand
{
public:
    RenameEntityCommand(EntityId id, const QString& oldName, const QString& newName);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    EntityId m_id = kInvalidEntity;
    QString m_oldName;
    QString m_newName;
};

// Changing the parent (coordinate system) of an entity.
class SetParentCommand : public UndoCommand
{
public:
    SetParentCommand(EntityId id, EntityId oldParent, EntityId newParent);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    EntityId m_id = kInvalidEntity;
    EntityId m_oldParent = kInvalidEntity;
    EntityId m_newParent = kInvalidEntity;
};

// Reordering an entity in the layer list, optionally into another parent.
class MoveEntityCommand : public UndoCommand
{
public:
    MoveEntityCommand(EntityId id, EntityId oldParent, EntityId newParent,
                      int oldIndex, int newIndex);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    EntityId m_id = kInvalidEntity;
    EntityId m_oldParent = kInvalidEntity;
    EntityId m_newParent = kInvalidEntity;
    int m_oldIndex = 0;
    int m_newIndex = 0;
};

// Changing one entry of an entity's static data map (audio start frame, ...).
class SetEntityDataCommand : public UndoCommand
{
public:
    SetEntityDataCommand(EntityId id, const QString& key, const QVariant& oldValue,
                         const QVariant& newValue, const QString& text);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    EntityId m_id = kInvalidEntity;
    QString m_key;
    QVariant m_oldValue;
    QVariant m_newValue;
    QString m_text;
};

// Moving an entity on both axes as one undo step.
class SetPositionCommand : public UndoCommand
{
public:
    SetPositionCommand(EntityId id, int frame, double oldX, double oldY, double newX,
                       double newY, bool keyframe);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    EntityId m_id = kInvalidEntity;
    int m_frame = 0;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
    bool m_keyframe = true;
};

// Scaling an entity on both axes as one undo step.
class SetScaleCommand : public UndoCommand
{
public:
    SetScaleCommand(EntityId id, int frame, double oldSx, double oldSy, double oldX,
                    double oldY, double newSx, double newSy, double newX, double newY,
                    bool keyframe);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    EntityId m_id = kInvalidEntity;
    int m_frame = 0;
    double m_oldSx = 1.0;
    double m_oldSy = 1.0;
    double m_newSx = 1.0;
    double m_newSy = 1.0;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
    bool m_keyframe = true;
};

// Moving, zooming and rotating a camera as one undo step.
class SetCameraCommand : public UndoCommand
{
public:
    SetCameraCommand(EntityId id, int frame, double oldX, double oldY, double oldScale,
                     double oldRotation, double newX, double newY, double newScale,
                     double newRotation, bool keyframe);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    void apply(Project& project, double x, double y, double scale, double rotation);

    EntityId m_id = kInvalidEntity;
    int m_frame = 0;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_oldScale = 1.0;
    double m_oldRotation = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
    double m_newScale = 1.0;
    double m_newRotation = 0.0;
    bool m_keyframe = true;
};

// Changing the pivot and the compensating position in one step.
class SetPivotCommand : public UndoCommand
{
public:
    SetPivotCommand(EntityId id, double oldAx, double oldAy, double oldX, double oldY,
                    double newAx, double newAy, double newX, double newY, int frame);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    void apply(Project& project, double ax, double ay, double x, double y);

    EntityId m_id = kInvalidEntity;
    double m_oldAx = 0.0;
    double m_oldAy = 0.0;
    double m_oldX = 0.0;
    double m_oldY = 0.0;
    double m_newAx = 0.0;
    double m_newAy = 0.0;
    double m_newX = 0.0;
    double m_newY = 0.0;
    int m_frame = 0;
};

// Changing the easing of one or more keyframes at once.
struct KeyframeEasingRecord
{
    EntityId entity = kInvalidEntity;
    QString property;
    double frame = 0.0;
    EasingSpec oldEase;
    EasingSpec newEase;
};

class SetKeyframeEasingCommand : public UndoCommand
{
public:
    explicit SetKeyframeEasingCommand(const QVector<KeyframeEasingRecord>& records);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

    bool canMergeWith(const UndoCommand* other) const override;
    void mergeWith(const UndoCommand* other) override;

private:
    QVector<KeyframeEasingRecord> m_records;
};

// Deleting keyframes, keeping a copy so they can be restored.
class RemoveKeyframesCommand : public UndoCommand
{
public:
    explicit RemoveKeyframesCommand(const QVector<KeyframeRecord>& records);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override;

private:
    QVector<KeyframeRecord> m_records;
};

} // namespace pvf
