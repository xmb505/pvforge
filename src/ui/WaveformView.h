#pragma once

#include "core/Project.h"

#include <QVector>
#include <QWidget>

class QScrollBar;

namespace pvf {

// Full-width audio waveform page. Every audio entity gets its own lane, the
// time axis zooms with Ctrl+wheel and scrolls with the wheel/shift+wheel.
// Beat markers are intentionally left for a later pass.
class WaveformView : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformView(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void refresh();

    int laneCountForTest() const;
    QRect laneRectForTest(int lane) const;
    int frameXForTest(double frame) const;
    int frameFromXForTest(int x) const;
    void setPixelsPerFrameForTest(double pixels);
    int horizontalScrollForTest() const { return m_scrollX; }
    int verticalScrollForTest() const { return m_scrollY; }
    double pixelsPerFrameForTest() const { return m_pixelsPerFrame; }

signals:
    void frameRequested(int frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Lane
    {
        EntityId id = kInvalidEntity;
        int index = 0;
    };

    QVector<Lane> lanes() const;
    int plotWidth() const;
    int contentWidth() const;
    void updateScrollBars();
    double frameFromX(double x) const;
    double xForFrame(double frame) const;

    static constexpr int kNameWidth = 150;
    static constexpr int kRulerHeight = 24;
    static constexpr int kLaneHeight = 84;
    static constexpr int kScrollBarSize = 12;

    Project* m_project = nullptr;
    int m_frame = 0;
    double m_pixelsPerFrame = 6.0;
    int m_scrollX = 0;
    int m_scrollY = 0;
    bool m_scrubbing = false;
    QScrollBar* m_hScroll = nullptr;
    QScrollBar* m_vScroll = nullptr;
};

} // namespace pvf
