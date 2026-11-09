#pragma once

#include "common/Types.h"

#include <QHash>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
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
// entity ...”). Assets can be dropped from the asset panel; multi-select,
// rubber-band selection and keyframe dragging are intentionally left to a
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
    QRect entityBarRect(const Uuid& uuid) const;
    QVector<Uuid> visibleEntities() const;

signals:
    // Emitted for drop problems that should surface in the shell status bar.
    void statusMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
        TrimEnd
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

    int assetDurationFrames(const assets::StoredAsset& asset) const;
    int dropRowFor(int y, int startFrame, int endFrame) const;
    QVector<Uuid> createEntitiesForAsset(const assets::StoredAsset& asset,
                                         int startFrame, int row);

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

    static constexpr int kToolbarHeight = 24;
    static constexpr int kRulerHeight = 32;
    static constexpr int kRowHeight = 30;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kEdgeGrabPixels = 5;
};

} // namespace pvf::next
