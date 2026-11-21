#pragma once

#include "model/Easing.h"

#include <QPalette>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

class QPainter;

namespace pvf {

// Mine-imator style visual easing picker: groups of curve thumbnails instead
// of mathematical names, ending with one custom bezier row.  The widget only
// chooses a model::EasingSpec; applying it (and the custom dialog) is the
// caller's job.
class EasingPicker : public QWidget
{
    Q_OBJECT

public:
    explicit EasingPicker(const model::EasingSpec& current,
                          QWidget* parent = nullptr);

    QSize sizeHint() const override { return m_size; }

    // Rectangle of one tile in widget coordinates.  "bezier" is the custom
    // row; an invalid rectangle means the type is not offered.
    QRect tileRect(const QString& type) const;

    // Programmatic activation (keyboard/tests): emits picked(), or
    // customRequested() for the bezier row.
    bool activate(const QString& type);

    // Curve thumbnail used by the properties panel button and the grid.
    static QPixmap curvePreview(const model::EasingSpec& spec,
                                const QPalette& palette, const QSize& size,
                                int margin = 3);

signals:
    void picked(const model::EasingSpec& spec);
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
        QString type;
        model::EasingSpec spec;
        QString tooltip;
        bool custom = false;
    };

    struct Section
    {
        QString title;
        QRect titleRect;
    };

    void buildLayout();
    void paintCurve(QPainter& painter, const QRect& rect,
                    const model::EasingSpec& spec) const;
    int itemAt(const QPoint& pos) const;
    int itemForType(const QString& type) const;
    bool isCurrent(const Item& item) const;

    QVector<Item> m_items;
    QVector<Section> m_sections;
    QSize m_size;
    model::EasingSpec m_current;
    int m_hover = -1;
};

} // namespace pvf
