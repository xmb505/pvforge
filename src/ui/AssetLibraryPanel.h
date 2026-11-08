#pragma once

#include "core/Project.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace pvf {

class AssetLibraryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetLibraryPanel(Project* project, QWidget* parent = nullptr);

    void refresh();

    // Library entries that can replace an entity of this media type.
    static QVector<int> mediaTemplateIndices(const Project& project, EntityType type);

    // Opens a picker showing the library entries of one media type. The
    // import button falls back to a file dialog with the given filter.
    // Returns the chosen source path, or an empty string when cancelled.
    static QString pickMedia(QWidget* parent, Project* project, EntityType type,
                             const QString& filter);

signals:
    void entityCreated(EntityId id);

private:
    void rebuild();
    void updatePreview();
    int selectedIndex() const;
    QString selectedHtmlAsset() const;
    void createSelected();
    void removeSelected();
    void importHtmlAsset();

    Project* m_project = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_create = nullptr;
    QPushButton* m_remove = nullptr;
    QStringList m_signature;
};

} // namespace pvf
