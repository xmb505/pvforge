#pragma once

#include "core/Project.h"
#include "app/EditActions.h"
#include "ui/TimelineData.h"

#include <QHash>
#include <QRect>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <memory>

class QKeyEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMimeData;
class QMouseEvent;
class QPainter;
class QScrollBar;
class QTimer;
class QToolButton;
class QWheelEvent;

namespace pvf {

class EditorSession;
class TimelineAdapter;

// A keyframe position, identified by its entity and frame.
struct KeyframeSelection
{
    EntityId entity = kInvalidEntity;
    double frame = 0.0;

    bool operator==(const KeyframeSelection& other) const
    {
        return entity == other.entity && qFuzzyCompare(frame + 1.0, other.frame + 1.0);
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
    explicit TimelinePanel(Project* project, QWidget* parent = nullptr);
    // Session-backed mode.  Painting, rows, scrolling and the private timeline
    // overlay are shared with the legacy mode; clip gestures commit through
    // EditorSession::editActions().
    explicit TimelinePanel(EditorSession* session, QWidget* parent = nullptr);
    ~TimelinePanel() override;

    // Legacy query kept for the UI self-tests; session-backed panels are
    // editable through EditActions.
    bool readOnly() const { return false; }
    void refreshFromSession();
    void setReadOnlySnapshot(const TimelineSnapshot& snapshot);
    EntityId entityIdForUuid(const Uuid& uuid) const;
    Uuid uuidForEntityId(EntityId id) const;

    enum class EditMode { Clips, Keyframes };

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);
    void setEditMode(EditMode mode);
    EditMode editMode() const { return m_editMode; }

    double pixelsPerFrame() const { return m_pixelsPerFrame; }
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
    int selectedKeyframeCount() const { return int(m_selectedKeyframes.size()); }
    QVector<KeyframeSelection> selectedKeyframes() const { return m_selectedKeyframes; }
    QRect rowButtonRect(int row, int index) const;
    int frameXForTest(double frame) const
    {
        return xFromFrame(int(std::lround(frame)));
    }
    int rowCenterYForTest(int row) const { return rowTop(row) + rowHeight(row) / 2; }
    int entitySelectionCountForTest() const { return int(m_entitySelection.size()); }

    bool displayFrames() const { return m_displayFrames; }
    void setDisplayFrames(bool frames);
    void setPixelsPerFrameForTest(double pixels);
    int rulerTickStepForTest() const;
    int rulerLabelStepForTest() const;
    int secondsCellStepForTest() const;
    int secondsLabelStepForTest() const;
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

    EntityId owner() const { return m_owner; }
    bool playheadVisibleForTest() const
    {
        return m_owner < 0
               || (m_frame >= contentStart() && m_frame < contentEnd());
    }
    QRect clipRectFor(EntityId id) const;
    // Opens (or raises) the private timeline for a clip and returns its panel.
    TimelinePanel* openPopup(EntityId ownerId);
    void closeAllPopups();
    void syncPopups();
    void removePopup(EntityId ownerId);
    EntityId dropAssetForTest(const QMimeData& mime, int frame, int row = -1)
    {
        return dropAsset(mime, frame, row);
    }
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
    void keyframesMoveRequested(const QVector<KeyframeRecord>& records,
                                double delta);
    void keyframeAddRequested(EntityId id, int frame);
    void folderCreateRequested(EntityId parent);
    void keyframeEasingRequested(const EasingSpec& spec);
    void keyframeNoteRequested(EntityId id, double frame);
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
    void finishSessionDrag();
    void cancelSessionDrag();
    bool isReparentCandidate(const Uuid& entity, const Uuid& target) const;
    QVector<Uuid> siblingOrderForDrop(const Uuid& entity, const Uuid& target,
                                      bool after) const;
    EntityId sessionBaseEntityAtRow(int row) const;
    QRect sessionBaseClipRect(const Entity& entity) const;
    int promptReparentMode(const Uuid& target);
    void deleteSelectedEntities();

    void initPanel();
    const Entity* entityById(EntityId id) const;
    int currentFps() const;
    bool isEntityLocked(EntityId id) const;
    QRect markerRect(int index) const;

    enum class DragMode
    {
        None,
        Scrub,
        PendingSelect,
        BoxSelect,
        MoveClip,
        TrimStart,
        TrimEnd,
        MoveKeyframe
    };

    // Row and range helpers.
    QVector<EntityId> rowIds() const;
    EntityId entityAtRow(int row) const;
    int rowAt(int y) const;
    int rowTop(int row) const;
    int rowHeight(int) const { return kRowHeight; }
    int depthOf(EntityId id) const;
    int rowOfEntity(const Entity& entity) const;
    void assignMissingRows();
    void moveEntityToRow(QVector<Entity>& entities, EntityId id, int targetRow);
    int contentStart() const;
    int contentEnd() const;
    const Entity* rangeOwner() const;
    int clipStartOf(const Entity& entity) const;
    int clipEndOf(const Entity& entity) const;
    QRect clipRect(const Entity& entity) const;
    QVector<int> keyframeFrames(const Entity& entity) const;
    QVector<KeyframeRecord> keyframeRecords(EntityId id, int frame) const;
    bool keyframeHasNote(EntityId id, double frame) const;

    // Scroll and coordinates.
    int visibleWidth() const;
    int bodyBottom() const;
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    void clampScroll();
    void updateScrollBars();
    void ensureFrameVisible(int frame);
    void setScrubFrame(double rawX);

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
    void drawAudioClip(QPainter& painter, const Entity& entity,
                       const QRect& clip) const;

    // Interaction.
    EntityId hitClipRow(int row, int x) const;
    int hitKeyframeRow(int row, int x) const;
    void selectEntity(EntityId id, bool emitSignal);
    void autoScrollTick();
    void syncPopupGeometry();
    EntityId dropAsset(const QMimeData& mime, int frame, int row);

    Project* m_project = nullptr;
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

    DragMode m_dragMode = DragMode::None;
    EntityId m_dragEntity = kInvalidEntity;
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

    QScrollBar* m_hScroll = nullptr;
    QScrollBar* m_vScroll = nullptr;
    QToolButton* m_unitButton = nullptr;
    QToolButton* m_modeButton = nullptr;
    QTimer* m_autoScroll = nullptr;
    int m_scrollX = 0;
    int m_scrollY = 0;
    double m_pixelsPerFrame = 6.0;
    bool m_displayFrames = false;
    EditMode m_editMode = EditMode::Clips;
    bool m_relativeTime = false;   // popup only

    QHash<EntityId, ClipPopup*> m_popups;

    static constexpr int kRowHeight = 30;
    static constexpr int kRulerHeight = 26;
    static constexpr int kScrollBarSize = 14;
    static constexpr int kPlusWidth = 30;
    static constexpr int kTrimGrab = 6;
};

} // namespace pvf
