#pragma once

#include "core/Project.h"

#include <QObject>

#include <memory>
#include <vector>

namespace pvf {

// A reversible edit. Undo and redo must leave the project in the exact
// state the command was created from.
class UndoCommand
{
public:
    virtual ~UndoCommand() = default;

    virtual void undo(Project& project) = 0;
    virtual void redo(Project& project) = 0;
    virtual QString text() const = 0;

    // Continuous edits (dragging a value) merge into one undo step.
    virtual bool canMergeWith(const UndoCommand* other) const { return false; }
    virtual void mergeWith(const UndoCommand* other) { Q_UNUSED(other); }
};

class History : public QObject
{
    Q_OBJECT

public:
    explicit History(QObject* parent = nullptr);

    // Applies the command and records it.
    void push(std::unique_ptr<UndoCommand> command, Project& project);

    bool canUndo() const { return m_index >= 0; }
    bool canRedo() const { return m_index + 1 < int(m_commands.size()); }
    QString undoText() const;
    QString redoText() const;

    void undo(Project& project);
    void redo(Project& project);
    void clear();

signals:
    void changed();

private:
    std::vector<std::unique_ptr<UndoCommand>> m_commands;
    int m_index = -1;
};

} // namespace pvf
