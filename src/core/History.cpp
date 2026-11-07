#include "History.h"

namespace pvf {

History::History(QObject* parent)
    : QObject(parent)
{
}

void History::push(std::unique_ptr<UndoCommand> command, Project& project)
{
    if (!command)
        return;

    // A new edit drops the redo branch.
    if (m_index + 1 < int(m_commands.size()))
        m_commands.resize(size_t(m_index + 1));

    if (canUndo() && m_commands[m_index]->canMergeWith(command.get()))
    {
        m_commands[m_index]->mergeWith(command.get());
        // The merged value is already the current project state.
    }
    else
    {
        command->redo(project);
        m_commands.push_back(std::move(command));
        m_index = int(m_commands.size()) - 1;
    }

    emit changed();
}

QString History::undoText() const
{
    return canUndo() ? m_commands[m_index]->text() : QString();
}

QString History::redoText() const
{
    return canRedo() ? m_commands[m_index + 1]->text() : QString();
}

void History::undo(Project& project)
{
    if (!canUndo())
        return;
    m_commands[m_index]->undo(project);
    --m_index;
    emit changed();
}

void History::redo(Project& project)
{
    if (!canRedo())
        return;
    ++m_index;
    m_commands[m_index]->redo(project);
    emit changed();
}

void History::clear()
{
    m_commands.clear();
    m_index = -1;
    emit changed();
}

} // namespace pvf
