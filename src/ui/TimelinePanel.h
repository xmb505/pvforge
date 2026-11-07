#pragma once

#include "core/Project.h"

#include <QWidget>

class QScrollBar;
class QToolButton;
class QWheelEvent;

namespace pvf {

class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);

    double pixelsPerFrame() const { return m_pixelsPerFrame; }

    QSize sizeHint() const override { return {800, 220}; }

signals:
    void frameRequested(int frame);
    void entitySelected(EntityId id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    int rowAt(int y) const;
    int contentWidth() const;
    int visibleWidth() const;
    void updateScrollBar();
    void zoomAt(double factor, int anchorX);

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    bool m_scrubbing = false;
    QScrollBar* m_hScroll = nullptr;
    QToolButton* m_zoomOut = nullptr;
    QToolButton* m_zoomIn = nullptr;
    int m_scrollX = 0;
    double m_pixelsPerFrame = 6.0;

    static constexpr int kNameWidth = 180;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kRowHeight = 24;
    static constexpr int kRightPad = 16;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kZoomButtonWidth = 16;
};

} // namespace pvf
