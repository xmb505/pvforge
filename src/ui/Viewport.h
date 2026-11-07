#pragma once

#include "core/Project.h"

#include <QPointF>
#include <QWidget>

namespace pvf {

class Viewport : public QWidget
{
    Q_OBJECT

public:
    explicit Viewport(Project* project, QWidget* parent = nullptr);

    void setCurrentFrame(int frame);
    void setSelectedEntity(EntityId id);
    void resetView();

    QSize minimumSizeHint() const override { return {480, 270}; }

signals:
    void entitySelected(EntityId id);
    void valueEdited(EntityId id, const QString& prop, double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRectF canvasRect() const;
    double viewScale() const;
    QPointF screenToCanvas(const QPointF& pos) const;
    const Entity* hitTest(const QPointF& pos) const;

    Project* m_project = nullptr;
    int m_frame = 0;
    EntityId m_selected = kInvalidEntity;
    double m_zoom = 1.0;
    QPointF m_pan;

    bool m_dragging = false;
    QPointF m_dragLast;
    EntityId m_dragEntity = kInvalidEntity;

    bool m_panning = false;
    QPointF m_panLast;
};

} // namespace pvf
