#pragma once

#include "common/Types.h"
#include "model/Project.h"

#include <QHash>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QVector>
#include <QWidget>

class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QScrollBar;
class QWheelEvent;

namespace pvf::assets {
struct StoredAsset;
}

namespace pvf::model {
struct Entity;
}

namespace pvf::next {

class Document;

// PR-style timeline for the new entity model.
//
// Shows only the direct children of parentUuid. Row 0 is the bottom row and
// larger rows stack upwards. Frame numbers are absolute project frames; an
// empty parentUuid means the root timeline.
//
// The widget edits the Document in place while dragging and commits one
// entity operation on mouse release (“pvf: move entity ...” / “pvf: trim
// entity ...”). Assets can be dropped from the asset panel. The “+” button on
// the left of an entity bar emits privateTimelineRequested() for the shell to
// open an overlay; this widget never switches its own parent. Double-clicking
// an entity bar writes/updates keyframes at that frame. When the selected
// entity has keyframes, merged diamonds can be selected (Ctrl/Shift for
// multiple frames), dragged, deleted and copied/pasted as one operation.
// Multi-select and rubber-band entity selection are intentionally left to a
// later pass.
class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(Document* document, QWidget* parent = nullptr);
    ~TimelineWidget() override;

    // Empty = root timeline, otherwise the private timeline of a parent.
    void setParentUuid(const Uuid& parentUuid);
    Uuid parentUuid() const { return m_parentUuid; }

    void setPixelsPerFrame(double pixelsPerFrame);
    double pixelsPerFrame() const { return m_pixelsPerFrame; }

    void setPlayhead(int frame);
    int playhead() const;

    void refresh();

    // Geometry helpers, also used by the tests.
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    QRect rulerRect() const;
    QRect breadcrumbRect() const;
    // Marker strip directly below the ruler. Markers are global project
    // annotations: they never occupy rows and never limit the timeline length.
    QRect markerLaneRect() const;
    // Clickable flag/label rectangle of a marker in the lane.
    QRect markerHitRect(const model::Marker& marker) const;
    // Marker under the cursor (lane flag/label, or a body guide line), empty
    // when there is none.
    QString markerIdAt(const QPoint& pos) const;
    QRect entityBarRect(const Uuid& uuid) const;
    // Small “+” button inside the left side of an entity bar. Empty when the
    // bar is too narrow to draw it.
    QRect addButtonRect(const Uuid& uuid) const;
    QVector<Uuid> visibleEntities() const;

    // Marker editing. Every successful edit writes project.markers and calls
    // Document::commitProject() exactly once (“pvf: ...”).
    void addMarkerAtPlayhead();
    bool insertMarker(const model::Marker& marker, QString* error = nullptr);
    bool updateMarker(const QString& markerId, const model::Marker& marker,
                      QString* error = nullptr);
    bool deleteMarker(const QString& markerId, QString* error = nullptr);
    // Dialog helpers used by the context menu and marker double-click.
    void openNewMarkerDialog();
    void openMarkerEditor(const QString& markerId);

    // Waveform peak cache diagnostics; the cache is memory-only and cleared
    // when the project root changes.
    void clearWaveformCache();
    int waveformCacheSize() const { return m_peakCache.size(); }

signals:
    // The “+” button asks the shell to open an entity's private timeline in an
    // overlay. The main timeline itself never changes parent.
    void privateTimelineRequested(const Uuid& parentUuid);
    // Emitted for drop problems that should surface in the shell status bar.
    void statusMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class DragMode
    {
        None,
        Scrub,
        Move,
        TrimStart,
        TrimEnd,
        Keyframes,
        Marker
    };

    enum class HitZone
    {
        None,
        Body,
        LeftEdge,
        RightEdge
    };

    struct Hit
    {
        Uuid uuid;
        HitZone zone = HitZone::None;
    };

    int rowTop(int row) const;
    QRect bodyRect() const;
    QRect toolbarRect() const;
    int labelFrameStep() const;

    void updateScrollBar();
    void setScrollX(int value);
    void ensureFrameVisible(int frame);
    void onPlayheadChanged();
    void onScrollBarValueChanged(int value);

    Hit hitTest(const QPoint& pos) const;
    void updateHoverCursor(const QPoint& pos);
    void applyDrag(const QPoint& pos);
    void finishDrag();

    // Merged keyframe diamonds: one frame can hold keys on several tracks.
    QSet<int> mergedKeyFrames(const model::Entity& entity) const;
    int keyframeAt(const QPoint& pos) const;
    void clearKeyframeSelection();
    void insertKeyframesAt(const Uuid& uuid, int relativeFrame);
    void applyKeyframeDrag(const QPoint& pos);
    void deleteSelectedKeyframes();
    void copySelectedKeyframes();
    void pasteKeyframes();
    Uuid keyframeSelectionEntity() const;
    QString entityDisplayName(const model::Entity& entity) const;

    int markerIndex(const QString& markerId) const;
    void deleteMarkerWithConfirm(const QString& markerId);
    bool commitMarkers(const QVector<model::Marker>& before,
                       const QString& message, QString* error);

    void paintWaveform(QPainter& painter, const model::Entity& entity,
                       const QRect& bar);
    QVector<float> peaksForAsset(const assets::StoredAsset& asset,
                                 QString* error);

    int assetDurationFrames(const assets::StoredAsset& asset) const;
    int dropRowFor(int y, int startFrame, int endFrame) const;
    QVector<Uuid> createEntitiesForAsset(const assets::StoredAsset& asset,
                                         int startFrame, int row);

    void trimEntityTree(const Uuid& uuid, int newStart, int newEnd,
                        QVector<Uuid>& upserts, QVector<Uuid>& deletes);

    void paintToolbar(QPainter& painter);
    void paintRuler(QPainter& painter);
    void paintRows(QPainter& painter);
    void paintGrid(QPainter& painter);
    void paintMarkers(QPainter& painter);
    void paintEntities(QPainter& painter);
    void paintEntity(QPainter& painter, const Uuid& uuid);
    void paintSolverBands(QPainter& painter, const model::Entity& entity,
                          const QRect& bar);
    void paintKeyframes(QPainter& painter, const model::Entity& entity,
                        const QRect& bar);
    void paintPlayhead(QPainter& painter);
    void paintEmptyHint(QPainter& painter);

    Document* m_document = nullptr;
    Uuid m_parentUuid;
    double m_pixelsPerFrame = 4.0;
    int m_scrollX = 0;
    int m_playhead = 0;
    QScrollBar* m_hScroll = nullptr;

    DragMode m_dragMode = DragMode::None;
    Uuid m_dragEntity;
    QPoint m_dragPressPos;
    int m_dragOriginalStart = 0;
    int m_dragOriginalEnd = 0;
    bool m_dragChanged = false;

    // Move drags shift the whole subtree. Ranges are snapshotted on press so
    // every mouse move can be applied as original + delta.
    QVector<Uuid> m_dragMoveUuids;
    QHash<Uuid, QPair<int, int>> m_dragOriginalRanges;

    // Trim previews only shift start/end in memory. The destructive keyframe,
    // sourceIn and solver edits run once on release from this snapshot.
    model::Project m_dragProjectSnapshot;
    bool m_dragHasSnapshot = false;

    // Merged keyframe frame selection. Frames are entity-relative and belong to
    // m_keyframeSelectionUuid; a selection elsewhere is dropped.
    Uuid m_keyframeSelectionUuid;
    QSet<int> m_selectedKeyframes;

    // Keyframe drag preview: tracks are snapshotted on press so every mouse
    // move is “original + delta”, never cumulative.
    QHash<QString, QVector<model::Keyframe>> m_dragKeyTracks;
    QSet<int> m_dragKeyFrames;
    int m_dragKeyPressFrame = 0;
    int m_dragKeyDelta = 0;
    bool m_dragKeyConflict = false;

    // Marker drag preview. The whole marker vector is snapshotted on press so
    // a failed commit can restore it.
    QString m_dragMarkerId;
    QVector<model::Marker> m_dragMarkerSnapshot;

    // Decoded peak envelopes keyed by "<assetHash>:<buckets>". Memory only.
    QHash<QString, QVector<float>> m_peakCache;
    QString m_peakCacheRoot;

    struct CopiedKey
    {
        QString track;
        int frame = 0;
        double value = 0.0;
        model::EasingSpec easing;
        QString note;
    };
    QVector<CopiedKey> m_keyClipboard;
    Uuid m_keyClipboardUuid;

    static constexpr int kToolbarHeight = 24;
    static constexpr int kRulerHeight = 32;
    static constexpr int kMarkerLaneHeight = 16;
    static constexpr int kRowHeight = 30;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kEdgeGrabPixels = 5;
    // Peak resolution cached per asset. Entity bars sample this envelope and
    // fold it down to one value per painted pixel.
    static constexpr int kPeakBuckets = 2048;
};

} // namespace pvf::next
