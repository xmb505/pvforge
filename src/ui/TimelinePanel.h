#pragma once

#include "core/Project.h"

#include <QSet>
#include <QWidget>
#include <cmath>

class QKeyEvent;
class QLineEdit;
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

class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(Project* project, QWidget* parent = nullptr);

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
    int rowCountForTest() const { return int(visibleEntities().size()); }
    EntityId rowEntityForTest(int row) const { return entityAtRow(row); }
    int rowDepthForTest(int row) const { return depthOf(entityAtRow(row)); }
    bool rowCollapsedForTest(EntityId id) const
    {
        return m_collapsedFolders.contains(id);
    }
    QRect folderArrowRectForTest(int row) const { return folderArrowRect(row); }

    // Scene band and annotation lane used by the UI tests.
    QString selectedSceneForTest() const { return m_selectedScene; }
    QRect sceneBandRectForTest(const QString& id) const;
    QRect annotationRectForTest(const QString& id) const;
    int sceneBandCenterYForTest() const { return kHeaderHeight + kSceneBandHeight / 2; }
    int annotationLaneCenterYForTest() const
    {
        return kHeaderHeight + kSceneBandHeight + kAnnotationLaneHeight / 2;
    }
    bool sceneActiveForTest(const QString& id) const
    {
        const Scene* scene = m_project->sceneAt(m_frame);
        return scene && scene->id == id;
    }
    bool keyframeHasNoteForTest(EntityId id, double frame) const
    {
        return keyframeHasNote(id, frame);
    }
signals:
    void frameRequested(int frame);
    void scrubStarted();   // the user takes manual control of the playhead
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

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    int rowAt(int y) const;
    int rowTop(int row) const;
    int rowHeight(int row) const;
    EntityId entityAtRow(int row) const;
    bool hitKeyframe(int row, int x, EntityId* outId, double* outFrame) const;
    bool audioClipRange(const Entity& entity, double* left, double* right) const;
    EntityId audioClipAt(int x, int y) const;
    void updateHoverCursor(const QPointF& pos);
    int depthOf(EntityId id) const;
    QRect folderArrowRect(int row) const;
    void updateRowDropTarget(const QPointF& pos);
    QVector<EntityId> rowDragRoots(EntityId clicked) const;
    double dragFrame(EntityId id, double frame) const;
    QVector<double> keyframeFrames(EntityId id) const;
    void handleRowButton(int row, int index);
    int contentWidth() const;
    int visibleWidth() const;
    QVector<int> visibleEntities() const;
    void updateScrollBar();
    void zoomAt(double factor, int anchorX);
    void commitRename();
    void stepFrame(int delta);
    void stopHold();
    EasingSpec keyframeEasingAt(EntityId id, double frame) const;
    void showKeyframeMenu(const QPoint& globalPos, EntityId id, double frame);
    void drawAudioWaveform(QPainter& painter, const Entity& entity, const QRect& row,
                           int right);
    int annotationTop() const { return kHeaderHeight + kSceneBandHeight; }
    QRect sceneBandRect(const Scene& scene) const;
    const Scene* sceneAtX(int x) const;
    const Annotation* annotationAtX(int x) const;
    QString keyframeNoteAt(EntityId id, double frame) const;
    bool keyframeHasNote(EntityId id, double frame) const;
    void drawSceneBand(QPainter& painter);
    void drawAnnotations(QPainter& painter);
    QString sceneTooltip(const Scene& scene) const;
    QString annotationTooltip(const Annotation& annotation) const;
    void showSceneMenu(const QPoint& globalPos, const Scene* scene);
    void showAnnotationMenu(const QPoint& globalPos, const Annotation* annotation,
                            int frame);

    enum class DragMode
    {
        None,
        Scrub,
        PendingSelect,
        BoxSelect,
        MoveAudio,
        PendingRowDrag,
        MoveRow,
        MoveKeyframe
    };
    enum class DropMode { None, Above, Below, Into };

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    DragMode m_dragMode = DragMode::None;
    QPointF m_pressPos;
    QPointF m_boxStart;
    QPointF m_boxEnd;
    int m_pressFrame = 0;
    EntityId m_pressEntity = kInvalidEntity;
    EntityId m_audioDragId = kInvalidEntity;
    double m_audioDragStart = 0.0;
    double m_audioDragOffset = 0.0;
    EntityId m_rowDragId = kInvalidEntity;
    QVector<EntityId> m_rowDragIds;
    QVector<KeyframeRecord> m_kfDragRecords;
    double m_kfFocusFrame = 0.0;
    int m_kfDragStartFrame = 0;
    int m_kfDragDelta = 0;
    int m_dropRow = -1;
    DropMode m_dropMode = DropMode::None;
    QSet<EntityId> m_collapsedFolders;
    QSet<EntityId> m_entitySelection;
    EntityId m_selectionAnchor = kInvalidEntity;
    int m_hoverRow = -1;
    QVector<KeyframeSelection> m_selectedKeyframes;
    QLineEdit* m_search = nullptr;
    QToolButton* m_unitButton = nullptr;
    QLineEdit* m_renameEdit = nullptr;
    EntityId m_renameEntity = kInvalidEntity;
    QScrollBar* m_hScroll = nullptr;
    QScrollBar* m_vScroll = nullptr;
    int m_scrollX = 0;
    int m_scrollY = 0;
    double m_pixelsPerFrame = 6.0;
    bool m_displayFrames = false;
    QString m_selectedScene;
    QTimer* m_holdTimer = nullptr;
    QTimer* m_holdDelay = nullptr;
    int m_holdDirection = 0;

    static constexpr int kNameWidth = 180;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kSceneBandHeight = 18;
    static constexpr int kAnnotationLaneHeight = 16;
    static constexpr int kRowsTop =
        kHeaderHeight + kSceneBandHeight + kAnnotationLaneHeight;
    static constexpr int kRowHeight = 24;
    static constexpr int kAudioRowHeight = 56;
    static constexpr int kRightPad = 16;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kButtonSize = 16;
};

} // namespace pvf
