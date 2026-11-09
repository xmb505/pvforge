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
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMimeData;
class QPushButton;
class QVBoxLayout;

namespace pvf::video2 {
class VideoExtractionQueue;
struct ExtractionJob;
}

namespace pvf::next {

// Asset bin for the new UI. Lists assets from the content-addressed store,
// shows missing assets referenced by entities, and renames/tags/deletes/imports
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
    QStringList assetTagsAt(int index) const;
    bool assetMissingAt(int index) const;

    // Tag filter above the list. tagFilters() excludes the "all tags" entry.
    QStringList tagFilters() const;
    void setTagFilter(const QString& tag);
    QString tagFilter() const;

    // Background video extraction rows at the bottom of the panel. Rows are
    // ordered by enqueue time; completed rows disappear after two seconds.
    int progressRowCount() const;
    QString progressTextAt(int index) const;
    QPushButton* progressCancelButtonAt(int index) const;

    // Background video extraction options. The values are captured into each
    // job when it is enqueued: imports that happen after the call use the new
    // values, running jobs are not interrupted. They only affect the WebP frame
    // cache under .cache/video, never the video preview proxy. Defaults are
    // 960 px and 2 GiB.
    void setVideoExtractionOptions(int maxWidth, qint64 diskBudgetBytes);
    int videoExtractionWidth() const;
    qint64 videoExtractionDiskBudget() const;

    // Queues every video asset in the project whose frame cache index at the
    // current project fps and extraction width is missing or does not cover
    // all frames. The queue skips complete indexes and duplicate hashes, so
    // calling this repeatedly is cheap for finished videos. Hosts call it once
    // after opening a project; refresh() deliberately never rescans the cache.
    void enqueueMissingVideoExtractions();

    // Imports files or .pvhtml directories/zips. Deduplicates through the
    // store, refreshes the list and commits once. Returns false if any path
    // failed; `error` lists the failures.
    bool importPaths(const QStringList& paths, QString* error = nullptr);

    // Business operations without dialogs. Both refresh and commit.
    bool renameAsset(const AssetHash& hash, const QString& newName,
                     QString* error = nullptr);
    bool setAssetTags(const AssetHash& hash, const QStringList& tags,
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
        QStringList tags;
        bool missing = false;
        bool hasAudio = false;
    };

    class ProgressRow;

    void rebuildEntries();
    void rebuildTagFilter();
    void applyFilter();
    void updateSelectionState();
    void chooseImportFiles();
    void chooseImportPvhtml();
    void renameSelected();
    void editSelectedTags();
    void deleteSelected();
    void showContextMenu(const QPoint& pos);
    void setStatus(const QString& text);
    QPixmap iconFor(const Entry& entry);
    QListWidgetItem* makeItem(const Entry& entry);
    QString selectedHash() const;
    int referenceCount(const AssetHash& hash) const;

    void enqueueVideoExtraction(const assets::StoredAsset& asset);
    bool buildExtractionJob(const assets::StoredAsset& asset,
                            video2::ExtractionJob* job) const;
    void ensureProgressRow(const AssetHash& hash);
    void updateProgressRow(const AssetHash& hash, int done, int total);
    void finishProgressRow(const AssetHash& hash);
    void failProgressRow(const AssetHash& hash, const QString& error);
    void removeProgressRow(const AssetHash& hash);
    int progressRowIndex(const AssetHash& hash) const;
    QString displayNameFor(const AssetHash& hash) const;

    Document* m_document = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_tagFilter = nullptr;
    QPushButton* m_importButton = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_tagButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_progressArea = nullptr;
    QVBoxLayout* m_progressLayout = nullptr;
    QVector<ProgressRow*> m_progressRows;
    video2::VideoExtractionQueue* m_extractionQueue = nullptr;
    int m_videoExtractionWidth = 960;
    qint64 m_videoExtractionDiskBudget = 2LL * 1024 * 1024 * 1024;
    QVector<Entry> m_entries;
    QHash<AssetHash, QPixmap> m_iconCache;
    QString m_lastImportDir;
};

} // namespace pvf::next
