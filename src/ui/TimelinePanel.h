#pragma once

#include "core/Project.h"

#include <QHash>
#include <QRect>
#include <QSet>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QScrollBar;
class QTimer;
class QToolButton;
class QWheelEvent;

namespace pvf {

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
    ~TimelinePanel() override;

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);

    double pixelsPerFrame() const { return m_pixelsPerFrame; }
    int horizontalScrollForTest() const { return m_scrollX; }
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
    EntityId rowEntityForTest(int row) const;
    int rowDepthForTest(int row) const { return depthOf(rowEntityForTest(row)); }
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
    QRect clipRectFor(EntityId id) const;
    // Opens (or raises) the private timeline for a clip and returns its panel.
    TimelinePanel* openPopup(EntityId ownerId);
    void closeAllPopups();
    void syncPopups();
    void removePopup(EntityId ownerId);
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
    void entityRenamed(EntityId id, const QString& name);
    void audioMoved(EntityId id, double startFrame);
    void entityMoveRequested(EntityId id, EntityId parent, int index);
    void entitiesMoveRequested(const QVector<EntityId>& ids, EntityId parent,
                               int index);
    void keyframesMoveRequested(const QVector<KeyframeRecord>& records,
                                double delta);
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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class ClipPopup;

    enum class DragMode
    {
        None,
        Scrub,
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

    Project* m_project = nullptr;
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
    QVector<Entity> m_before;
    QVector<Entity> m_preview;
    QVector<Entity> m_externalPreview;
    bool m_hasExternalPreview = false;
    bool m_dragging = false;
    bool m_moved = false;
    int m_hoverRow = -1;
    bool m_hoverLeft = false;
    bool m_hoverRight = false;

    QScrollBar* m_hScroll = nullptr;
    QScrollBar* m_vScroll = nullptr;
    QTimer* m_autoScroll = nullptr;
    int m_scrollX = 0;
    int m_scrollY = 0;
    double m_pixelsPerFrame = 6.0;
    bool m_displayFrames = false;
    bool m_relativeTime = false;   // popup only

    QHash<EntityId, ClipPopup*> m_popups;

    static constexpr int kRowHeight = 30;
    static constexpr int kRulerHeight = 26;
    static constexpr int kScrollBarSize = 14;
    static constexpr int kPlusWidth = 30;
    static constexpr int kTrimGrab = 6;
};

} // namespace pvf
