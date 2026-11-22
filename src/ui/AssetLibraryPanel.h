#pragma once

#include "common/Types.h"
#include "core/EntityId.h"


#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QComboBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMimeData;
class QPaintEvent;
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
    // Session mode: lists assets from EditorSession::assets(), imports through
    // the content-addressed store and drags application/x-pvforge-asset.
    explicit AssetLibraryPanel(EditorSession* session, QWidget* parent = nullptr);
    ~AssetLibraryPanel() override;

    bool sessionMode() const { return m_sessionMode; }
    void refresh();

    // Compact mode for the timeline-left asset bin: keeps the search field,
    // import buttons, tag filter, list and create/remove actions, but hides
    // the folder header and the long info line so the panel fits ~220 px.
    void setCompactMode(bool compact);
    bool compactMode() const { return m_compact; }

    // Library entries that can replace an entity of this media type.

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
#ifdef PVFORGE_TEST_HOOKS
    // HTML thumbnails come from an injected renderer instead of html2 when one
    // is set: the callback receives the asset hash and the requested size and
    // returns a null image to mean "still rendering".  An empty function
    // restores the real MediaServices path.
    using HtmlThumbnailRenderer =
        std::function<QImage(const AssetHash&, const QSize&)>;
    void setHtmlThumbnailRendererForTest(HtmlThumbnailRenderer renderer);
    // Bumped only when a finished HTML thumbnail actually changed a row's
    // icon; a frameReady with nothing new leaves it alone.
    int htmlThumbnailRefreshCountForTest() const
    {
        return m_htmlThumbRefreshCount;
    }
    // §1 regression: the row pointer must survive a frameReady, which is what
    // proves the list was not rebuilt.
    QListWidgetItem* assetItemAtForTest(int index) const;
    // Number of memoised icons, so a test can tell "only this row was
    // invalidated" from "the whole cache was dropped".
    int iconCacheSizeForTest() const;
    // The hover card: its frame style and its palette roles, so a test can
    // assert it follows the system theme instead of a hardcoded dark QSS.
    void showHoverPreviewAtForTest(int index);
    QFrame* hoverPreviewForTest() const { return m_hoverPreview; }
    // Drives the htmlFrameReady handler without a running browser.
    void handleHtmlFrameReadyForTest() { refreshHtmlThumbnails(); }
#endif
    void setSearchText(const QString& text);
    QString searchText() const;
    QStringList tagFilters() const;
    void setTagFilter(const QString& tag);
    QString tagFilter() const;

    // Session-mode imports and asset metadata edits. Each refreshes the list;
    // imports and metadata edits schedule one project commit. When
    // `importedByPath` is given, successfully imported input paths are mapped
    // to their content hash (the drop path uses it to skip repeated drops).
    bool importPaths(const QStringList& paths, QString* error = nullptr,
                     QHash<QString, AssetHash>* importedByPath = nullptr);
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
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

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
    void createSelectedSession();
    void removeSelectedSession();


    void buildSessionUi();
    void rebuildSessionEntries();
    void rebuildSessionTagFilter();
    void applySessionFilter();
    void updateSessionSelection();
    void chooseSessionImportFiles();
    void chooseSessionImportHtml();

    // External file-manager drops reuse the import buttons' pipeline. The
    // panel and the list viewport share these handlers so a drop anywhere in
    // the (possibly empty) bin lands in the same place.
    bool externalDropAvailable(const QMimeData* mime, const QObject* source) const;
    bool handleExternalDragMove(QDragMoveEvent* event);
    bool handleExternalDrop(QDropEvent* event);
    void importDroppedPaths(const QStringList& paths);
    QString externalImportBlockedReason() const;
    void setExternalDragActive(bool active);
    void renameSessionSelected();
    void editSessionTags();
    void deleteSessionSelected();
    void reimportSessionSelected();
    void replaceSelectedEntityFromMenu();
    void showSessionContextMenu(const QPoint& pos);
    void setStatus(const QString& text);
    QPixmap iconFor(const SessionEntry& entry);
    QPixmap previewFor(const SessionEntry& entry, const QSize& size);
    // The memo key of iconFor(): the hash plus the missing reason, so a
    // re-imported or newly-offline asset gets a fresh icon.
    QString iconCacheKey(const SessionEntry& entry) const;
    // HTML asset thumbnail: the html2 cache first, then one asynchronous
    // request per entry.  A null result means "not rendered yet, keep the
    // placeholder".  Always renders at the hover card's 240x150 and caches
    // that one sharp frame; both views downscale it on demand.
    QImage htmlThumbnail(const SessionEntry& entry, const QSize& target);
    // Picks up the frames that finished since the last call: re-icons only the
    // rows that gained one, and never rebuilds the list.
    void refreshHtmlThumbnails();
    void ensureHoverPreview();
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

    EditorSession* m_session = nullptr;
    bool m_sessionMode = false;
    bool m_compact = false;
    QVBoxLayout* m_layout = nullptr;
    QWidget* m_header = nullptr;
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
    // HTML 0-frame thumbnails, keyed by asset hash.  Only finished images are
    // stored: a miss is re-asked on the next paint and MediaServices never
    // queues the same asset twice while its render is in flight.
    QHash<AssetHash, QImage> m_htmlThumbs;
    QMetaObject::Connection m_htmlFrameConnection;
    int m_lastHoverIndex = -1;
#ifdef PVFORGE_TEST_HOOKS
    HtmlThumbnailRenderer m_htmlThumbnailForTest;
    int m_htmlThumbRefreshCount = 0;
#endif
    QString m_lastImportDir;
    QStringList m_sessionSignature;

    // External drops: canonical path -> content hash. A repeated drop of the
    // same path is skipped while its asset is still in the store; dropping it
    // again after a delete imports it back.
    QHash<QString, AssetHash> m_droppedPathHashes;
    bool m_externalDragActive = false;
};

} // namespace pvf
