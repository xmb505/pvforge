#include "AppIcon.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace pvf {

QIcon makeCubeIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const QPolygonF top{{32, 8}, {56, 21}, {32, 34}, {8, 21}};
    const QPolygonF left{{8, 21}, {32, 34}, {32, 58}, {8, 45}};
    const QPolygonF right{{56, 21}, {32, 34}, {32, 58}, {56, 45}};

    p.setBrush(QColor(255, 205, 100));
    p.drawPolygon(top);
    p.setBrush(QColor(225, 165, 65));
    p.drawPolygon(left);
    p.setBrush(QColor(185, 125, 45));
    p.drawPolygon(right);
    return QIcon(pixmap);
}

QIcon makeEyeIcon(bool hidden)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(200, 200, 210), 1.2));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    path.moveTo(1.5, 8);
    path.quadTo(8, 2.5, 14.5, 8);
    path.quadTo(8, 13.5, 1.5, 8);
    p.drawPath(path);

    p.setBrush(QColor(200, 200, 210));
    p.drawEllipse(QPointF(8, 8), 2.2, 2.2);

    if (hidden)
    {
        p.setPen(QPen(QColor(230, 90, 90), 1.6));
        p.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 2.5));
    }
    return QIcon(pixmap);
}

QIcon makeLockIcon(bool locked)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(200, 200, 210), 1.2));
    p.setBrush(Qt::NoBrush);

    // Shackle.
    if (locked)
    {
        p.drawArc(QRectF(4.5, 2.0, 7.0, 7.0), 0, 180 * 16);
    }
    else
    {
        p.drawArc(QRectF(6.5, 2.5, 7.0, 7.0), 0, 180 * 16);
    }

    p.setBrush(QColor(200, 200, 210));
    p.drawRoundedRect(QRectF(3.0, 7.0, 10.0, 7.0), 1.5, 1.5);
    return QIcon(pixmap);
}

QIcon makeKeyframeIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 190, 110));
    QPolygonF diamond;
    diamond << QPointF(8, 1.5) << QPointF(14.5, 8) << QPointF(8, 14.5) << QPointF(1.5, 8);
    p.drawPolygon(diamond);
    return QIcon(pixmap);
}

} // namespace pvf
