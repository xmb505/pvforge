#include "AppIcon.h"

#include <QPainter>
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

} // namespace pvf
