#pragma once

#include "common/Types.h"
#include "core/Project.h"

#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMimeData;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace pvf {

namespace assets {
struct StoredAsset;
}

namespace video2 {
class VideoExtractionQueue;
}

class EditorSession;

class AssetLibraryPanel : public QWidget
{
    Q_OBJECT

public:
    // Legacy Project mode, kept for the old shell and its self-tests.
    explicit AssetLibraryPanel(Project* project, QWidget* parent = nullptr);
    // Session mode: lists assets from EditorSession::assets(), imports through
    // the content-addressed store and drags application/x-pvforge-asset.
    explicit AssetLibraryPanel(EditorSession* session, QWidget* parent = nullptr);
    ~AssetLibraryPanel() override;

    bool sessionMode() const { return m_sessionMode; }
    void refresh();

    // Library entries that can replace an entity of this media type.
    static QVector<int> mediaTemplateIndices(const Project& project, EntityType type);

    // Opens a picker showing the library entries of one media type. The
    // import button falls back to a file dialog with the given filter.
    // Returns the chosen source path, or an empty string when cancelled.
    static QString pickMedia(QWidget* parent, Project* project, EntityType type,
                             const QString& filter);

    // Session-mode picker: compatible assets from the store, plus an
    // "import new file" button. Returns the chosen hash or an empty string.
    // `contentKind` follows the compatibility table: empty accepts everything,
    // asset(visual) lists image/video/html, asset(audio) lists audio.
    static QString pickAsset(QWidget* parent, EditorSession* session,
                             ContentKind contentKind, AssetType currentType,
                             const AssetHash& currentHash);

    // Session-mode test/business accessors. Rows are the filtered visible
    // entries in display order.
    int assetCount() const;
    QString assetNameAt(int index) const;
    QString assetHashAt(int index) const;
    QStringList assetTagsAt(int index) const;
    bool assetMissingAt(int index) const;
    QString assetMissingReasonAt(int index) const;
    QPixmap assetThumbnailAt(int index) const;
    void setSearchText(const QString& text);
    QString searchText() const;
    QStringList tagFilters() const;
    void setTagFilter(const QString& tag);
    QString tagFilter() const;

    // Session-mode imports and asset metadata edits. Each refreshes the list;
    // imports and metadata edits schedule one project commit.
    bool importPaths(const QStringList& paths, QString* error = nullptr);
    bool renameAsset(const AssetHash& hash, const QString& name,
                     QString* error = nullptr);
    bool setAssetTags(const AssetHash& hash, const QStringList& tags,
                      QString* error = nullptr);
    // Removes the store entry only; entities keep referencing the hash and
    // show the offline state until the same content is imported again.
    bool deleteAsset(const AssetHash& hash, QString* error = nullptr);
    // Re-imports a file for a missing hash and verifies the content hash.
    bool reimportMissing(const AssetHash& hash, const QString& path,
                         QString* error = nullptr);
    // Replaces the currently selected entity (Document::selection()) with the
    // given asset through EditActions. Used by the asset-bin context menu.
    bool replaceSelectedEntityWith(const AssetHash& hash, QString* error = nullptr);

    // Queues background frame extraction for every video in the store that
    // has no complete cache index. Repeated calls are cheap.
    void enqueueMissingVideoExtractions();

    // MIME data for dragging one stored asset to the timeline. Caller owns it.
    QMimeData* assetMimeData(const AssetHash& hash) const;

    // Bottom video-extraction rows (queued/running/completed) and their state.
    int progressRowCount() const;
    QString progressTextAt(int index) const;
    QPushButton* progressCancelButtonAt(int index) const;

signals:
    void entityCreated(EntityId id);
    void assetsChanged();
    void statusMessage(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct SessionEntry
    {
        AssetHash hash;
        QString name;
        QString directory;
        QString content;
        AssetType type = AssetType::Image;
        QStringList tags;
        bool missing = false;
        QString missingReason;
        bool hasAudio = false;
        int width = 0;
        int height = 0;
        double seconds = 0.0;
        int frames = 0;
    };

    class ProgressRow;

    void buildUi();
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

    void buildSessionUi();
    void rebuildSessionEntries();
    void rebuildSessionTagFilter();
    void applySessionFilter();
    void updateSessionSelection();
    void chooseSessionImportFiles();
    void chooseSessionImportHtml();
    void renameSessionSelected();
    void editSessionTags();
    void deleteSessionSelected();
    void reimportSessionSelected();
    void replaceSelectedEntityFromMenu();
    void showSessionContextMenu(const QPoint& pos);
    void setStatus(const QString& text);
    QPixmap iconFor(const SessionEntry& entry);
    QPixmap previewFor(const SessionEntry& entry, const QSize& size);
    QListWidgetItem* makeSessionItem(const SessionEntry& entry);
    QString selectedHash() const;
    AssetType entryTypeForHash(const AssetHash& hash) const;
    int referenceCount(const AssetHash& hash) const;

    void enqueueVideoExtraction(const assets::StoredAsset& asset);
    void ensureProgressRow(const AssetHash& hash);
    void updateProgressRow(const AssetHash& hash, int done, int total);
    void finishProgressRow(const AssetHash& hash);
    void failProgressRow(const AssetHash& hash, const QString& error);
    void removeProgressRow(const AssetHash& hash);
    int progressRowIndex(const AssetHash& hash) const;
    QString displayNameFor(const AssetHash& hash) const;

    Project* m_project = nullptr;
    EditorSession* m_session = nullptr;
    bool m_sessionMode = false;
    QLineEdit* m_search = nullptr;
    QComboBox* m_tagFilter = nullptr;
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

    // Session mode.
    QWidget* m_progressArea = nullptr;
    QVBoxLayout* m_progressLayout = nullptr;
    QVector<ProgressRow*> m_progressRows;
    video2::VideoExtractionQueue* m_extractionQueue = nullptr;
    QVector<SessionEntry> m_entries;
    QHash<AssetHash, QPixmap> m_iconCache;
    QString m_lastImportDir;
    QStringList m_sessionSignature;
};

} // namespace pvf
