#pragma once

#include "core/Project.h"
#include "ui/TimelinePanel.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>

class QScrollBar;
class QToolButton;

namespace pvf {

class History;

// Time/value graph for the properties of the selected entity. Curves are
// sampled through the same Track::evaluate math as the renderer, so what is
// drawn here is what plays back. Keyframes can be dragged in time, beziers
// expose draggable handles, and all edits become one undo step on release.
class CurveEditor : public QWidget
{
    Q_OBJECT

public:
    explicit CurveEditor(Project* project, History* history, QWidget* parent = nullptr);

    void setSelectedEntity(EntityId id);
    EntityId selectedEntity() const { return m_entity; }
    void setCurrentFrame(int frame);
    void refreshTracks();

    int curveCount() const;
    QString curveProperty(int index) const;
    QString curveLabel(int index) const;
    bool curveVisible(int index) const;
    void setCurveVisible(int index, bool visible);
    QColor curveColor(int index) const;

    QRect chartRectForTest() const;
    QRect plotRectForTest() const;
    QPointF keyframePointForTest(const QString& property, double frame) const;
    QPointF bezierHandlePointForTest(const QString& property, double frame, bool in) const;
    int frameXForTest(double frame) const;
    int frameFromXForTest(int x) const;
    void setPixelsPerFrameForTest(double pixels);
    int horizontalScrollForTest() const { return m_scrollX; }
    int selectedKeyframeCount() const { return int(m_selection.size()); }
    QVector<KeyframeSelection> selectedKeyframes() const { return m_selection; }

signals:
    void frameRequested(int frame);
    void statusMessage(const QString& text);
    void keyframeNoteRequested(EntityId id, double frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct ValueRange
    {
        double minimum = 0.0;
        double maximum = 1.0;
    };

    enum class DragMode
    {
        None,
        Scrub,
        PendingSelect,
        BoxSelect,
        MoveKeys,
        MoveHandle
    };

    int plotWidth() const;
    int contentWidth() const;
    void updateScrollBar();
    double frameFromX(double x) const;
    double xForFrame(double frame) const;
    QRectF plotRect() const;
    ValueRange rangeFor(const QString& property) const;
    double yForValue(const ValueRange& range, double value) const;
    double valueForY(const ValueRange& range, double y) const;
    double valueAt(const Entity& entity, const QString& property, double frame) const;
    double displayFrame(double frame) const;
    bool isSelectedFrame(double frame) const;
    bool trackAt(const QString& property, const Track** track) const;
    bool keyIndexAt(const Track& track, double frame, int* index) const;
    bool hitKeyframe(const QPointF& pos, KeyframeSelection* result) const;
    bool hitHandle(const QPointF& pos, QString* property, double* frame,
                   bool* inHandle) const;
    bool handlePoint(const QString& property, double frame, bool inHandle,
                     QPointF* result) const;
    EasingSpec easingAt(const QString& property, double frame) const;
    QVector<KeyframeRecord> selectionRecords() const;
    int clampDelta(const QVector<KeyframeRecord>& records, int desired) const;
    void updateButtons();
    void showEasingMenu(const QPoint& globalPos);
    void copySelection();
    void pasteClipboard();
    void deleteSelection();
    void selectAllKeyframes();
    void applyEasingToSelection(const EasingSpec& spec);
    void convertToBezier();

    static constexpr int kHeaderHeight = 26;
    static constexpr int kRulerHeight = 18;
    static constexpr int kScrollBarHeight = 12;
    static constexpr int kPlotMargin = 8;

    Project* m_project = nullptr;
    History* m_history = nullptr;
    EntityId m_entity = kInvalidEntity;
    int m_frame = 0;
    QVector<KeyframeSelection> m_selection;
    QVector<KeyframeRecord> m_clipboard;
    double m_clipboardBase = 0.0;

    QVector<QToolButton*> m_buttons;
    QVector<bool> m_visible;
    QScrollBar* m_hScroll = nullptr;
    double m_pixelsPerFrame = 6.0;
    int m_scrollX = 0;

    DragMode m_dragMode = DragMode::None;
    QPointF m_pressPos;
    QPointF m_boxStart;
    QPointF m_boxEnd;
    int m_pressFrame = 0;
    int m_dragDelta = 0;
    QVector<KeyframeRecord> m_dragRecords;
    QVector<double> m_dragFrames;   // selected frames, used to ignore moving keys

    QString m_handleProperty;
    double m_handleFrame = 0.0;
    bool m_handleIn = true;
    EasingSpec m_handleOldEase;
    EasingSpec m_handlePreview;
};

} // namespace pvf
