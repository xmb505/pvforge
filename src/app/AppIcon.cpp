#include "AppIcon.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <QPalette>
#include <QPixmap>

#include <functional>

namespace pvf {

namespace {

// Icons are drawn at 4x and scaled down, so they stay sharp at any size.
constexpr int kIconSize = 64;

QColor themeTextColor()
{
    return QApplication::palette().color(QPalette::Text);
}

QColor themeAccentColor()
{
    return QApplication::palette().color(QPalette::Highlight);
}

// Looks up a themed icon by name, trying several conventions.
QIcon themedIcon(const QStringList& names)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

QIcon vectorIcon(const std::function<void(QPainter&)>& draw)
{
    QIcon icon;
    for (int state = 0; state < 3; ++state)
    {
        QPixmap pixmap(kIconSize, kIconSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing, true);
        draw(p);
        p.end();

        const QIcon::Mode mode = state == 0 ? QIcon::Normal
                                            : (state == 1 ? QIcon::Active : QIcon::Selected);
        icon.addPixmap(pixmap, mode, QIcon::On);
    }
    return icon;
}

QIcon drawEye(bool hidden)
{
    return vectorIcon([hidden](QPainter& p) {
        const QColor color = themeTextColor();
        p.setPen(QPen(color, 6));
        p.setBrush(Qt::NoBrush);

        QPainterPath path;
        path.moveTo(8, 32);
        path.quadTo(32, 6, 56, 32);
        path.quadTo(32, 58, 8, 32);
        p.drawPath(path);

        p.setBrush(color);
        p.drawEllipse(QPointF(32, 32), 7, 7);

        if (hidden)
        {
            p.setPen(QPen(color, 6));
            p.drawLine(QPointF(12, 52), QPointF(52, 12));
        }
    });
}

QIcon drawLock(bool locked)
{
    return vectorIcon([locked](QPainter& p) {
        const QColor color = themeTextColor();
        p.setPen(QPen(color, 6));
        p.setBrush(Qt::NoBrush);

        if (locked)
        {
            p.drawArc(QRectF(18, 8, 28, 30), 0, 180 * 16);
        }
        else
        {
            p.drawArc(QRectF(26, 14, 28, 26), 0, 180 * 16);
        }

        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(12, 28, 40, 28), 6, 6);
    });
}

QIcon drawKeyframe()
{
    return vectorIcon([](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(themeAccentColor());
        QPolygonF diamond;
        diamond << QPointF(32, 6) << QPointF(58, 32) << QPointF(32, 58)
                << QPointF(6, 32);
        p.drawPolygon(diamond);
    });
}

} // namespace

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
    const QIcon themed = themedIcon(hidden ? QStringList{"visibility-off", "view-hidden"}
                                            : QStringList{"visibility", "view-visible", "eye"});
    if (!themed.isNull())
        return themed;
    return drawEye(hidden);
}

QIcon makeLockIcon(bool locked)
{
    const QIcon themed = themedIcon(locked ? QStringList{"changes-prevent", "lock", "locked"}
                                           : QStringList{"changes-allow", "unlock"});
    if (!themed.isNull())
        return themed;
    return drawLock(locked);
}

QIcon makeKeyframeIcon()
{
    return drawKeyframe();
}

namespace {

QPainterPath heartPath(const QRectF& r)
{
    const double w = r.width();
    const double h = r.height();
    const double cx = r.center().x();
    const double cy = r.center().y();
    QPainterPath path;
    path.moveTo(cx, cy + h * 0.35);
    path.cubicTo(cx - w * 0.5, cy + h * 0.05, cx - w * 0.45, cy - h * 0.42, cx,
                 cy - h * 0.18);
    path.cubicTo(cx + w * 0.45, cy - h * 0.42, cx + w * 0.5, cy + h * 0.05, cx,
                 cy + h * 0.35);
    path.closeSubpath();
    return path;
}

QPainterPath starPath(const QRectF& r, int points, double innerRatio)
{
    QPainterPath path;
    const double rx = r.width() / 2.0;
    const double ry = r.height() / 2.0;
    const QPointF center = r.center();
    for (int i = 0; i < points * 2; ++i)
    {
        const double angle = -M_PI / 2.0 + i * M_PI / points;
        const double radius = (i % 2 == 0) ? 1.0 : innerRatio;
        const QPointF p(center.x() + std::cos(angle) * rx * radius,
                        center.y() + std::sin(angle) * ry * radius);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

QPainterPath polygonPath(const QRectF& r, int sides)
{
    QPainterPath path;
    const double rx = r.width() / 2.0;
    const double ry = r.height() / 2.0;
    const QPointF center = r.center();
    for (int i = 0; i < sides; ++i)
    {
        const double angle = -M_PI / 2.0 + i * 2.0 * M_PI / sides;
        const QPointF p(center.x() + std::cos(angle) * rx,
                        center.y() + std::sin(angle) * ry);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

QPainterPath arrowPath(const QRectF& r)
{
    const double w = r.width();
    const double h = r.height();
    const double cx = r.center().x();
    const double cy = r.center().y();
    const double shaft = h * 0.36;
    QPainterPath path;
    path.moveTo(cx - w * 0.5, cy - shaft / 2);
    path.lineTo(cx + w * 0.1, cy - shaft / 2);
    path.lineTo(cx + w * 0.1, cy - h * 0.5);
    path.lineTo(cx + w * 0.5, cy);
    path.lineTo(cx + w * 0.1, cy + h * 0.5);
    path.lineTo(cx + w * 0.1, cy + shaft / 2);
    path.lineTo(cx - w * 0.5, cy + shaft / 2);
    path.closeSubpath();
    return path;
}

} // namespace

QIcon makeShapeIcon(int shape)
{
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = themeTextColor();
    p.setPen(QPen(color, 2.2));
    p.setBrush(QColor(color.red(), color.green(), color.blue(), 40));

    const QRectF rect(6, 6, 36, 36);
    switch (shape)
    {
    case 0:
        p.drawRect(rect);
        break;
    case 1:
        p.drawEllipse(rect);
        break;
    case 2:
        p.drawLine(QPointF(6, 24), QPointF(42, 24));
        break;
    case 3:
        p.drawRoundedRect(rect, 9, 9);
        break;
    case 4:
    {
        QPolygonF triangle;
        triangle << QPointF(24, 6) << QPointF(42, 42) << QPointF(6, 42);
        p.drawPolygon(triangle);
        break;
    }
    case 5:
        p.drawPath(heartPath(rect));
        break;
    case 6:
        p.drawPath(starPath(rect, 5, 0.45));
        break;
    case 7:
        p.drawPath(polygonPath(rect, 6));
        break;
    case 8:
        p.drawPath(arrowPath(rect));
        break;
    default:
        p.drawRect(rect);
        break;
    }
    return QIcon(pixmap);
}

} // namespace pvf
