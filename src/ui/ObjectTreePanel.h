#pragma once

#include "core/Project.h"

#include <QWidget>

class QListWidget;

namespace pvf {

class ObjectTreePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectTreePanel(Project* project, QWidget* parent = nullptr);

    void rebuild();
    void selectEntity(EntityId id);

signals:
    void entitySelected(EntityId id);

private:
    Project* m_project = nullptr;
    QListWidget* m_list = nullptr;
};

} // namespace pvf
