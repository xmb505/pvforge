#pragma once

#include "core/Project.h"

#include <QWidget>

class QKeyEvent;
class QLineEdit;
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
    int selectedKeyframeCount() const { return int(m_selectedKeyframes.size()); }
    QVector<KeyframeSelection> selectedKeyframes() const { return m_selectedKeyframes; }
    QRect rowButtonRect(int row, int index) const;

    bool displayFrames() const { return m_displayFrames; }
    void setDisplayFrames(bool frames);
    void setPixelsPerFrameForTest(double pixels);
    int rulerTickStepForTest() const;
    int rulerLabelStepForTest() const;
    int secondsCellStepForTest() const;
    int secondsLabelStepForTest() const;
signals:
    void frameRequested(int frame);
    void scrubStarted();   // the user takes manual control of the playhead
    void displayFramesChanged(bool frames);
    void entitySelected(EntityId id);
    void entityFlagChanged(EntityId id, EntityFlag flag, bool value);
    void deleteKeyframesRequested();
    void entityRenamed(EntityId id, const QString& name);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    int rowAt(int y) const;
    EntityId entityAtRow(int row) const;
    bool hitKeyframe(int row, int x, EntityId* outId, double* outFrame) const;
    QVector<double> keyframeFrames(EntityId id) const;
    void handleRowButton(int row, int index);
    int contentWidth() const;
    int visibleWidth() const;
    QVector<int> visibleEntities() const;
    void updateScrollBar();
    void zoomAt(double factor, int anchorX);
    void commitRename();
    void stepFrame(int delta);

    enum class DragMode { None, Scrub, PendingSelect, BoxSelect };

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    DragMode m_dragMode = DragMode::None;
    QPointF m_pressPos;
    QPointF m_boxStart;
    QPointF m_boxEnd;
    int m_pressFrame = 0;
    EntityId m_pressEntity = kInvalidEntity;
    int m_hoverRow = -1;
    QVector<KeyframeSelection> m_selectedKeyframes;
    QLineEdit* m_search = nullptr;
    QToolButton* m_unitButton = nullptr;
    QLineEdit* m_renameEdit = nullptr;
    EntityId m_renameEntity = kInvalidEntity;
    QScrollBar* m_hScroll = nullptr;
    int m_scrollX = 0;
    double m_pixelsPerFrame = 6.0;
    bool m_displayFrames = false;
    QTimer* m_holdTimer = nullptr;
    int m_holdDirection = 0;

    static constexpr int kNameWidth = 180;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kRowHeight = 24;
    static constexpr int kRightPad = 16;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kButtonSize = 16;
};

} // namespace pvf
