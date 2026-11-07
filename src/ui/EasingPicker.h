#pragma once

#include "core/Easing.h"

#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

namespace pvf {

// Mine-imator style visual easing picker: groups of curve thumbnails instead
// of mathematical names. The custom bezier editor lives behind one tile.
class EasingPicker : public QWidget
{
    Q_OBJECT

public:
    explicit EasingPicker(const EasingSpec& current, QWidget* parent = nullptr);

    QSize sizeHint() const override { return m_size; }

signals:
    void picked(const EasingSpec& spec);
    void customRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Item
    {
        QRect rect;
        EasingSpec spec;
        QString tooltip;
        bool custom = false;
    };

    struct Section
    {
        QString title;
        QRect titleRect;
        int firstItem = 0;
        int itemCount = 0;
    };

    void buildLayout();
    void paintCurve(QPainter& painter, const QRect& rect, const EasingSpec& spec);
    int itemAt(const QPoint& pos) const;

    QVector<Item> m_items;
    QVector<Section> m_sections;
    QSize m_size;
    EasingSpec m_current;
    int m_hover = -1;
};

} // namespace pvf
