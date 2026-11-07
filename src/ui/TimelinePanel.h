#pragma once

#include "core/Project.h"

#include <QWidget>

namespace pvf {

class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);

    QSize sizeHint() const override { return {800, 210}; }

signals:
    void frameRequested(int frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int xFromFrame(double frame) const;
    int frameFromX(double x) const;
    int rowAt(int y) const;

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    bool m_scrubbing = false;

    static constexpr int kNameWidth = 180;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kRowHeight = 24;
    static constexpr int kRightPad = 16;
};

} // namespace pvf
