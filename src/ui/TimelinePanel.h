#pragma once

#include "core/Project.h"

#include <QWidget>

class QScrollBar;

namespace pvf {

class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);

    QSize sizeHint() const override { return {800, 220}; }

signals:
    void frameRequested(int frame);
    void entitySelected(EntityId id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int xFromFrame(int frame) const;
    int frameFromX(int x) const;
    int rowAt(int y) const;
    int contentEndFrame() const;
    int contentWidth() const;
    int visibleWidth() const;
    void updateScrollBar();

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    bool m_scrubbing = false;
    QScrollBar* m_hScroll = nullptr;
    int m_scrollX = 0;

    static constexpr int kNameWidth = 180;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kRowHeight = 24;
    static constexpr int kRightPad = 16;
    static constexpr int kScrollBarHeight = 14;
    static constexpr double kPixelsPerFrame = 6.0;
};

} // namespace pvf
