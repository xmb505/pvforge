#include "ObjectTreePanel.h"

#include <QListWidget>
#include <QVBoxLayout>

namespace pvf {

ObjectTreePanel::ObjectTreePanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const auto id = m_list->item(row)->data(Qt::UserRole).toInt();
        emit entitySelected(id);
    });
}

void ObjectTreePanel::rebuild()
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const Entity& e : m_project->entities())
    {
        auto* item = new QListWidgetItem(e.name, m_list);
        item->setData(Qt::UserRole, e.id);
        item->setToolTip(e.sourcePath);
    }
}

void ObjectTreePanel::selectEntity(EntityId id)
{
    const QSignalBlocker blocker(m_list);
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toInt() == id)
        {
            m_list->setCurrentRow(i);
            return;
        }
    }
    m_list->setCurrentRow(-1);
}

} // namespace pvf
