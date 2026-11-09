#pragma once

#include "next/core/Document.h"

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMimeData;
class QPushButton;

namespace pvf::next {

// Asset bin for the new UI. Lists assets from the content-addressed store,
// shows missing assets referenced by entities, and renames/deletes/imports
// through the shared Document so every operation commits to git.
class AssetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetPanel(Document* document, QWidget* parent = nullptr);
    ~AssetPanel() override;

    void refresh();
    void setSearchText(const QString& text);
    QString searchText() const;

    // Visible (filtered) row count and row accessors, used by the shell and
    // by tests.
    int assetCount() const;
    QString assetNameAt(int index) const;
    QString assetHashAt(int index) const;
    bool assetMissingAt(int index) const;

    // Imports files or .pvhtml directories/zips. Deduplicates through the
    // store, refreshes the list and commits once. Returns false if any path
    // failed; `error` lists the failures.
    bool importPaths(const QStringList& paths, QString* error = nullptr);

    // Business operations without dialogs. Both refresh and commit.
    bool renameAsset(const AssetHash& hash, const QString& newName,
                     QString* error = nullptr);
    bool deleteAsset(const AssetHash& hash, QString* error = nullptr);

    // MIME data for dragging an asset out to the timeline. Caller owns it.
    QMimeData* assetMimeData(const AssetHash& hash) const;

signals:
    void assetsChanged();
    void statusMessage(const QString& message);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Entry
    {
        AssetHash hash;
        QString name;
        QString directory;
        QString content;
        AssetType type = AssetType::Image;
        bool missing = false;
        bool hasAudio = false;
    };

    void rebuildEntries();
    void applyFilter();
    void updateSelectionState();
    void chooseImportFiles();
    void chooseImportPvhtml();
    void renameSelected();
    void deleteSelected();
    void showContextMenu(const QPoint& pos);
    void setStatus(const QString& text);
    QPixmap iconFor(const Entry& entry);
    QListWidgetItem* makeItem(const Entry& entry);
    QString selectedHash() const;
    int referenceCount(const AssetHash& hash) const;

    Document* m_document = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_importButton = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_status = nullptr;
    QVector<Entry> m_entries;
    QHash<AssetHash, QPixmap> m_iconCache;
    QString m_lastImportDir;
};

} // namespace pvf::next
