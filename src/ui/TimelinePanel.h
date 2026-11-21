#pragma once

#include "app/EditActions.h"
#include "ui/TimelineSnapshot.h"

#include <QHash>
#include <QLineEdit>
#include <QRect>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <memory>

class QKeyEvent;
class QLineEdit;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QFocusEvent;
class QHideEvent;
class QShowEvent;
class QMimeData;
class QMouseEvent;
class QPainter;
class QPalette;
class QScrollBar;
class QTimer;
class QToolButton;
class QWheelEvent;

namespace pvf {

class EditorSession;
class TimelineAdapter;

// One whole-frame keyframe diamond, identified by its entity and absolute
// project frame.
struct KeyframeSelection
{
    EntityId entity = kInvalidEntity;
    double frame = 0.0;

    bool operator==(const KeyframeSelection& other) const
    {
        return entity == other.entity
               && qFuzzyCompare(frame + 1.0, other.frame + 1.0);
    }
};

class ClipPopup;

// Clip-based timeline. Every entity is a clip inside its parent timeline;
// the main panel owns root entities and each clip can open a private
// timeline for its children. This replaces the old track-style panel; the
// public surface is kept so MainWindow keeps working unchanged.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    // Session-backed mode.  Painting, rows, scrolling and the private timeline
    // overlay are shared with the legacy mode; clip gestures commit through
    // EditorSession::editActions().
    explicit TimelinePanel(EditorSession* session, QWidget* parent = nullptr);
    ~TimelinePanel() override;

    // One threshold for painting, hit-testing and the private timeline that
    // floats above the clip: below this a "+" would be a dead target.
    static constexpr int kPlusMinClipWidth = 84;

    // Legacy query kept for the UI self-tests; session-backed panels are
    // editable through EditActions.
    bool readOnly() const { return false; }
    void refreshFromSession();
    void setReadOnlySnapshot(const TimelineSnapshot& snapshot);
    EntityId entityIdForUuid(const Uuid& uuid) const;
    Uuid uuidForEntityId(EntityId id) const;

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);
    // Window-level Delete: keyframes first, then clips.  MainWindow forwards
    // the key when no text editor has focus.
    void requestDeleteSelection();

#ifdef PVFORGE_TEST_HOOKS
    bool splitAtPlayheadForTest() { return splitAtPlayhead(); }
    bool inlineRenameActiveForTest() const { return m_renameEdit != nullptr; }
    void setSelectedKeyframesForTest(
        const QVector<KeyframeSelection>& selection)
    {
        adoptKeyframeSelection(selection);
    }
    void commitInlineRenameForTest() { finishInlineRename(true); }
    void cancelInlineRenameForTest() { finishInlineRename(false); }
#endif

    double pixelsPerFrame() const { return m_pixelsPerFrame; }

    // Whole-second grid shared by the painter and the self-tests.  The step
    // widens through 1s -> 2s -> 5s -> 10s -> 30s -> 60s (then doubles) as the
    // timeline zooms out, so the lines never merge into a grey wall.  Frames
    // are always whole-second multiples of the project frame rate.
    static int secondGridStep(int fps, double pixelsPerFrame);
    static int secondGridFrames(int fps, double pixelsPerFrame);
    // Whole-second frames in [firstFrame, lastFrame], counted from originFrame
    // (0 for absolute time, the parent clip start for relative private time).
    static QVector<int> secondGridFrameList(int fps, double pixelsPerFrame,
                                            int originFrame, int firstFrame,
                                            int lastFrame);

#ifdef PVFORGE_TEST_HOOKS
    int horizontalScrollForTest() const { return m_scrollX; }
    void setHorizontalScrollForTest(int value)
    {
        m_scrollX = std::max(0, value);
        clampScroll();
        update();
        syncPopups();
    }
    int verticalScrollForTest() const { return m_scrollY; }
    bool verticalScrollVisibleForTest() const;
#endif
    int selectedKeyframeCount() const { return int(m_selectedKeyframes.size()); }
    QVector<KeyframeSelection> selectedKeyframes() const { return m_selectedKeyframes; }
    QRect rowButtonRect(int row, int index) const;
    QRect clipButtonRect(const Entity& entity, int index) const;
#ifdef PVFORGE_TEST_HOOKS
    QRect playheadHandleRectForTest() const { return playheadHandleRect(); }
    int frameXForTest(double frame) const
    {
        return xFromFrame(int(std::lround(frame)));
    }
    int rowCenterYForTest(int row) const { return rowTop(row) + rowHeight(row) / 2; }
    int rowTopForTest(int row) const { return rowTop(row); }
    int bodyBottomForTest() const { return bodyBottom(); }
    int rulerHeightForTest() const { return kRulerHeight; }
    // Clip-first timeline: no name column, no timeline-side entity search.
    int nameColumnWidthForTest() const { return nameWidth(); }
    bool clipNameElidedForTest(EntityId id) const;
    QString clipNameTextForTest(EntityId id) const;
    int clipCornerRadiusForTest() const { return 0; }
    // Bottom-up drop row under y, clamped to row 0..highest occupied + 1.
    int dropRowForTest(int y) const { return dropRowForY(y); }
    int keyframeHitForTest(int row, int x, int y) const
    {
        return hitKeyframeRow(row, x, y);
    }
    int entitySelectionCountForTest() const { return int(m_entitySelection.size()); }
#endif

    bool displayFrames() const { return m_displayFrames; }
    void setDisplayFrames(bool frames);
#ifdef PVFORGE_TEST_HOOKS
    void setPixelsPerFrameForTest(double pixels);
    int rulerTickStepForTest() const;
    int rulerLabelStepForTest() const;
    int secondsCellStepForTest() const;
    int secondsLabelStepForTest() const;
    int secondGridOriginForTest() const { return secondGridOrigin(); }
    QVector<int> secondGridFrameListForTest(int firstFrame,
                                            int lastFrame) const
    {
        return secondGridFrameList(currentFps(), m_pixelsPerFrame,
                                   secondGridOrigin(), firstFrame, lastFrame);
    }
    int rowCountForTest() const { return int(rowIds().size()); }
    // Rows that actually contain an entity (rowIds() pads the view).
    int entityRowCountForTest() const;
    EntityId rowEntityForTest(int row) const;
    int rowForEntityForTest(EntityId id) const;
    int rowDepthForTest(int row) const { return depthOf(rowEntityForTest(row)); }
    QString rowNameForTest(int row) const;
    int rowStartForTest(int row) const;
    int rowEndForTest(int row) const;
    bool rowIsAudioForTest(int row) const;
    QString rowAssetHashForTest(int row) const;
    double rowAssetSecondsForTest(int row) const;
    int currentFrameForTest() const { return m_frame; }
    int markerCountForTest() const { return int(m_snapshot.markers.size()); }
    int markerFrameForTest(int index) const;
    QRect markerRectForTest(int index) const { return markerRect(index); }
    bool rowCollapsedForTest(EntityId id) const
    {
        Q_UNUSED(id);
        return false;
    }
    QRect folderArrowRectForTest(int row) const
    {
        Q_UNUSED(row);
        return QRect();
    }

    // Scenes and annotations are not drawn by the clip timeline yet. The
    // accessors stay so the existing UI self-tests keep compiling.
    QString selectedSceneForTest() const { return QString(); }
    QRect sceneBandRectForTest(const QString& id) const
    {
        Q_UNUSED(id);
        return QRect();
    }
    QRect annotationRectForTest(const QString& id) const
    {
        Q_UNUSED(id);
        return QRect();
    }
    int sceneBandCenterYForTest() const { return 0; }
    int annotationLaneCenterYForTest() const { return 0; }
    bool sceneActiveForTest(const QString& id) const
    {
        Q_UNUSED(id);
        return false;
    }
    bool keyframeHasNoteForTest(EntityId id, double frame) const
    {
        return keyframeHasNote(id, frame);
    }
#endif

    EntityId owner() const { return m_owner; }
#ifdef PVFORGE_TEST_HOOKS
    bool playheadVisibleForTest() const
    {
        return m_owner < 0
               || (m_frame >= contentStart() && m_frame < contentEnd());
    }
    int displayFrameForTest(int frame) const { return displayFrame(frame); }
    bool relativeTimeForTest() const { return m_relativeTime; }
    void setRelativeTimeForTest(bool relative)
    {
        m_relativeTime = relative;
        update();
    }
    double pixelsPerFrameForTest() const { return m_pixelsPerFrame; }
#endif
    QRect clipRectFor(EntityId id) const;
#ifdef PVFORGE_TEST_HOOKS
    // Solver colour band inside the clip: start -> Result::stopFrame.
    QRect solverBandRectForTest(const Uuid& entityUuid,
                                const Uuid& solverId) const;
    int solverBandStopFrameForTest(const Uuid& entityUuid,
                                   const Uuid& solverId) const;
    // Number of cold solver evaluations done to fill the band cache (a hit in
    // EditorSession's shared solver cache does not count).  Paint and playhead
    // ticks must not grow it.
    int solverBandSolveCountForTest() const { return m_solverBandSolveCount; }
#endif
    // Opens (or raises) the private timeline for a clip and returns its panel.
    TimelinePanel* openPopup(EntityId ownerId);
    void closeAllPopups();
    void syncPopups();
    void removePopup(EntityId ownerId);
#ifdef PVFORGE_TEST_HOOKS
    EntityId dropAssetForTest(const QMimeData& mime, int frame, int row = -1)
    {
        return dropAsset(mime, frame, row);
    }
    int audioPeakCountForTest(EntityId id) const
    {
        const Entity* entity = entityById(id);
        return entity && entity->audio ? int(entity->audio->peaks.size()) : 0;
    }
    double clipTrimStartForTest(EntityId id) const
    {
        const Entity* entity = entityById(id);
        return entity ? entity->data.value(QStringLiteral("trimStart"), 0.0)
                            .toDouble()
                      : 0.0;
    }
#endif
    // Popup keyframe selections are adopted by the main panel so the existing
    // Delete / easing actions see them.
    void adoptKeyframeSelection(const QVector<KeyframeSelection>& selection);

signals:
    void frameRequested(int frame);
    void scrubStarted();
    void displayFramesChanged(bool frames);
    void entitySelected(EntityId id);
    void entityFlagChanged(EntityId id, EntityFlag flag, bool value);
    void deleteKeyframesRequested();
    void deleteEntityRequested(EntityId id);
    void entityRenamed(EntityId id, const QString& name);
    void audioMoved(EntityId id, double startFrame);
    void entityMoveRequested(EntityId id, EntityId parent, int index);
    void entitiesMoveRequested(const QVector<EntityId>& ids, EntityId parent,
                               int index);
    void folderCreateRequested(EntityId parent);
    void replaceSourceRequested(EntityId id);
    void copyEntityRequested(EntityId id);
    void duplicateEntityRequested(EntityId id);
    void pasteEntityRequested();
    void groupIntoFolderRequested(const QVector<EntityId>& ids);
    void sceneCreateRequested();
    void sceneEditRequested(const QString& id);
    void sceneDeleteRequested(const QString& id);
    void sceneRetireRequested(const QString& id, bool retire);
    void annotationCreateRequested(int frame);
    void annotationEditRequested(const QString& id);
    void annotationDeleteRequested(const QString& id);
    void keyframeSelectionChanged(const QVector<KeyframeSelection>& selection);

    // One undoable whole-project edit produced by a clip/keyframe drag.
    void snapshotEditRequested(const QVector<Entity>& before,
                               const QVector<Entity>& after,
                               const QString& text);

    // Session-backed edits report their result so the shell can show it in the
    // status bar; a failed commit has already been rolled back.
    void editCommitted(const QString& text);
    void editFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class ClipPopup;

    // Child panels of a session-backed panel share the adapter so int ids stay
    // identical between the main timeline and its private timelines.
    TimelinePanel(TimelineAdapter* adapter, QWidget* parent);

    bool sessionMode() const { return m_adapter != nullptr; }
    const model::Entity* sessionEntity(const Uuid& uuid) const;
    void beginSessionDrag();
    void updateSessionDragPreview(const QPointF& pos);
    // Preview helpers: patch the live snapshot in place instead of rebuilding
    // it from a whole-project copy on every mouse move (issue #132).
    void applySessionPreview(const EditPlan& plan);
    void resetSessionPreview();
    void finishSessionDrag();
    void cancelSessionDrag();
    bool isReparentCandidate(const Uuid& entity, const Uuid& target) const;
    EntityId sessionBaseEntityAtRow(int row) const;
    QRect sessionBaseClipRect(const Entity& entity) const;
    int promptReparentMode(const Uuid& target);
    void deleteSelectedEntities();
    void copySelectedClips();
    void pasteClips();
    void duplicateSelectedClips();
    void groupSelectedClips();
    QVector<Uuid> selectedUuids() const;

    // Session-backed keyframe editing.  Frames passed in are absolute project
    // frames; the model stores entity-relative ones.
    bool addKeyframeAt(EntityId id, int frame);
    bool keyframeSelectedAt(EntityId id, int frame) const;
    QVector<KeyRef> selectedKeyRefs() const;
    void deleteSelectedKeyframes();
    void copyParamsToSelectedKeyframes();
    // Linux-terminal style timeline clipboard: Ctrl+C / Ctrl+V move whole
    // keyframes only, Ctrl+Shift+C / Ctrl+Shift+V stay on the clip layer.
    void copyKeyframesToClipboard();
    void pasteKeyframesFromClipboard();

    // Alt + fast double-click inline rename.  Enter/focus-out commits, Esc
    // cancels.
    void beginInlineRename(EntityId id);
    void finishInlineRename(bool commit);

    // Split and marker edits, session mode only.
    bool splitAtPlayhead();
    bool splitEntityAtPlayhead(EntityId id);
    void addMarkerAtPlayhead();
    void editMarker(const QString& id);
    void deleteMarkerWithConfirm(const QString& id);
    QString markerAt(const QPoint& pos) const;
    void beginMarkerDrag(const QString& id, int x);
    void updateMarkerDrag(int x);
    void finishMarkerDrag();

    void initPanel();
    const Entity* entityById(EntityId id) const;
    int currentFps() const;
    bool isEntityLocked(EntityId id) const;
    QRect markerRect(int index) const;

    // Solver band stop frames are expensive to evaluate, so they are computed
    // once per model change and only read by paintEvent()/the test hooks.
    void rebuildSolverStopCache();
    QRect solverBandRect(const Uuid& entityUuid, const Uuid& solverId) const;

    enum class DragMode
    {
        None,
        Scrub,
        PendingSelect,
        BoxSelect,
        MoveClip,
        TrimStart,
        TrimEnd,
        MoveKeyframe,
        MoveMarker
    };

    // Row and range helpers.
    // One paint pass resolves rows, clip rectangles and the popup track
    // geometry once; the per-row loop then never re-scans the entity table
    // (issue #132).
    struct PaintCache
    {
        struct Row
        {
            int row = 0;
            int top = 0;
            QVector<const Entity*> entities;
        };

        int contentStart = 0;
        int contentEnd = 180;
        int minNeg = -1;
        int maxPos = 0;
        int dividerTop = 0;
        QVector<Row> rows;
        QHash<EntityId, QRect> clipRects;
        QHash<EntityId, QVector<int>> keyframes;
        bool hasAnyEntity = false;
    };
    PaintCache buildPaintCache() const;

    QVector<EntityId> rowIds() const;
    QVector<const Entity*> entitiesInRow(int row) const;
    EntityId entityAtRow(int row) const;
    bool isValidRow(int row) const;
    void popupTrackBounds(int* minNeg, int* maxPos) const;
    int popupDividerTop() const;
    int rowAt(int y) const;
    int rowTop(int row) const;
    QString searchFilter() const;
    bool rowMatchesFilter(const Entity& entity) const;
    int nameWidth() const;
    void handleRowButton(int row, int index);
    void toggleSessionFlag(EntityId id, bool hidden);
    void setAltHeld(bool held);
    void clearSelection();
    bool clipShowsInlineButtons(const Entity& entity) const;
    int clipIconCount(const Entity& entity) const;
    // Drop row under y for a clip drag: bottom row 0, gaps allowed, rows
    // above the highest clip collapse to highest + 1.
    int dropRowForY(int y) const;
    int highestRowForDrop() const;
    int rowHeight(int) const { return kRowHeight; }
    int depthOf(EntityId id) const;
    int rowOfEntity(const Entity& entity) const;
    void assignMissingRows();
    void moveEntityToRow(QVector<Entity>& entities, EntityId id, int targetRow);
    int contentStart() const;
    int contentEnd() const;
    // Converts an absolute project frame to the number the ruler shows.
    // Private timelines in relative mode label their own 0..duration window;
    // every other panel keeps global project frames.
    int displayFrame(int frame) const;
    const Entity* rangeOwner() const;
    int clipStartOf(const Entity& entity) const;
    int clipEndOf(const Entity& entity) const;
    QRect clipRect(const Entity& entity) const;
    QVector<int> keyframeFrames(const Entity& entity) const;
    bool keyframeHasNote(EntityId id, double frame) const;

    // Scroll and coordinates.
    int visibleWidth() const;
    int bodyBottom() const;
    int timelineLength() const;
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    void clampScroll();
    void updateScrollBars();
    void ensureFrameVisible(int frame);
    void setScrubFrame(double rawX);
    void stepFrame(int delta);
    void stopHold();

    // Live preview during a drag.
    const QVector<Entity>& activeEntities() const;
    Entity* activeEntity(EntityId id);
    void beginDrag();
    void endDrag(const QString& text);
    void resetPreview();
    void updateDragPreview(const QPointF& pos);
    void applyShift(QVector<Entity>& entities, EntityId id, int delta);
    void setClipRange(Entity& entity, int start, int end);
    void cleanKeys(Entity& entity);
    void trimStart(QVector<Entity>& entities, EntityId id, int newStart);
    void trimEnd(QVector<Entity>& entities, EntityId id, int newEnd);
    void trimStartRec(QVector<Entity>& entities, EntityId parentId, int limit,
                      QSet<EntityId>& doomed);
    void trimEndRec(QVector<Entity>& entities, EntityId parentId, int limit,
                    QSet<EntityId>& doomed);
    void removeEntities(QVector<Entity>& entities, const QSet<EntityId>& doomed);
    void reorderChild(QVector<Entity>& entities, EntityId owner, EntityId id,
                      int targetRow);
    void moveKeys(QVector<Entity>& entities, const QVector<KeyframeSelection>& keys,
                  int delta);
    QRect keyRect(const Entity& entity, int frame) const;
    QRect playheadHandleRect() const;
    void drawAudioClip(QPainter& painter, const Entity& entity,
                       const QRect& clip) const;

    // Whole-second grid: absolute origin (or the parent clip start in a
    // relative private timeline), a palette-adaptive soft grey, and the body
    // lines drawn after the track backgrounds but before the clips.
    int secondGridOrigin() const;
    static QColor secondGridColor(const QPalette& palette);
    void paintSecondGrid(QPainter& painter, const QVector<int>& frames,
                         const QHash<EntityId, QRect>& clipRects) const;

    // Interaction.
    EntityId hitClipRow(int row, int x) const;
    int hitKeyframeRow(int row, int x, int y) const;
    void selectEntity(EntityId id, bool emitSignal);
    void autoScrollTick();
    void syncPopupGeometry();
    EntityId dropAsset(const QMimeData& mime, int frame, int row);
    EntityId dropSessionAsset(const QMimeData& mime, int frame, int row);

    EditorSession* m_session = nullptr;   // session-backed main/popup panels
    TimelineAdapter* m_adapter = nullptr;
    std::unique_ptr<TimelineAdapter> m_ownedAdapter;
    TimelineSnapshot m_snapshot;
    EntityId m_owner = kInvalidEntity;   // kInvalidEntity = main timeline
    bool m_popup = false;
    TimelinePanel* m_sourcePanel = nullptr;  // popup only

    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    QSet<EntityId> m_entitySelection;
    QVector<KeyframeSelection> m_selectedKeyframes;
    EntityId m_lastClickedKeyEntity = kInvalidEntity;
    int m_lastClickedKeyFrame = -1;
    bool m_editingKeyframeEdit = false;
    QLineEdit* m_renameEdit = nullptr;
    EntityId m_renameEntity = kInvalidEntity;
    QVector<model::Entity> m_clipClipboard;
    KeyframeClipboard m_keyframeClipboard;
    QString m_dragMarkerId;
    int m_markerDragStartFrame = 0;
    int m_markerDragFrame = 0;

    DragMode m_dragMode = DragMode::None;
    EntityId m_dragEntity = kInvalidEntity;
    QVector<Uuid> m_dragRoots;
    int m_dragRow = -1;
    int m_dragStartFrame = 0;
    int m_dragStartRow = -1;
    int m_dragClipStart = 0;
    int m_dragClipEnd = 0;
    int m_keyDragStart = 0;
    QPointF m_lastDragPos;
    QPointF m_pressPos;
    QPointF m_boxStart;
    QPointF m_boxEnd;
    EntityId m_pressEntity = kInvalidEntity;
    int m_pressFrame = 0;
    QVector<Entity> m_before;
    QVector<Entity> m_preview;
    QVector<Entity> m_externalPreview;
    bool m_hasExternalPreview = false;
    bool m_dragging = false;
    bool m_moved = false;
    bool m_dropActive = false;
    int m_dropFrame = 0;
    int m_hoverRow = -1;
    bool m_hoverLeft = false;
    bool m_hoverRight = false;

    // Session-backed drag preview.  One plan is produced repeatedly without
    // writing, then committed exactly once on release.
    EditPlan m_sessionPlan;
    bool m_sessionPlanValid = false;
    TimelineSnapshot m_sessionBaseSnapshot;

    // entityUuid + '/' + solverId -> clipped physical stop frame.
    QHash<QString, int> m_solverStopFrames;
    int m_solverBandSolveCount = 0;

    QScrollBar* m_hScroll = nullptr;
    QScrollBar* m_vScroll = nullptr;
    QToolButton* m_unitButton = nullptr;
    QTimer* m_autoScroll = nullptr;
    QTimer* m_holdTimer = nullptr;
    QTimer* m_holdDelay = nullptr;
    int m_holdDirection = 0;
    int m_scrollX = 0;
    int m_scrollY = 0;
    double m_pixelsPerFrame = 6.0;
    bool m_displayFrames = false;
    bool m_relativeTime = false;   // popup only
    // Alt = clip management layer: keyframes are hit-blocked and dimmed,
    // while the eye/lock buttons become clickable.
    bool m_altHeld = false;
    bool m_boxSelectClips = false;

    QHash<EntityId, ClipPopup*> m_popups;

    // No left name column: clips carry their entity names. The constant stays
    // at zero so the existing coordinate helpers keep one shared origin.
    static constexpr int kNameWidth = 0;
    static constexpr int kRowHeight = 30;
    static constexpr int kPopupDividerHeight = 14;
    static constexpr int kInvalidRow = -1000000;
    static constexpr int kRulerHeight = 28;
    static constexpr int kScrollBarSize = 14;
    static constexpr int kPlusWidth = 30;
    static constexpr int kTrimGrab = 6;
    static constexpr int kButtonSize = 14;
    static constexpr int kButtonGap = 2;
    static constexpr int kButtonStrip = 2 * (kButtonSize + kButtonGap);
    // Clips narrower than this hide the inline eye/lock buttons; the
    // right-click menu keeps those actions reachable for short clips.
    static constexpr int kInlineButtonsMinWidth = kButtonStrip + 44;
    static constexpr int kPlayheadHandleWidth = 14;
    static constexpr int kPlayheadHandleHeight = 11;
};

} // namespace pvf
