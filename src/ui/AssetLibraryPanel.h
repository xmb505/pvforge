#pragma once

#include "core/Project.h"

#include <QWidget>

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;

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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void updatePreview();
    void setIconView(bool iconView);
    void showHoverPreview(QListWidgetItem* item);
    void hideHoverPreview();
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
    QToolButton* m_viewButton = nullptr;
    QFrame* m_hoverPreview = nullptr;
    QLabel* m_hoverImage = nullptr;
    QLabel* m_hoverCaption = nullptr;
    bool m_iconView = false;
    QStringList m_signature;
};

} // namespace pvf
