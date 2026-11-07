#pragma once

#include "core/Project.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace pvf {

class AssetLibraryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetLibraryPanel(Project* project, QWidget* parent = nullptr);

    void refresh();

signals:
    void entityCreated(EntityId id);

private:
    void rebuild();
    void updatePreview();
    int selectedIndex() const;
    void createSelected();
    void removeSelected();

    Project* m_project = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_create = nullptr;
    QPushButton* m_remove = nullptr;
    QStringList m_signature;
};

} // namespace pvf
