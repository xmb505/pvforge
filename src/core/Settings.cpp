#include "Settings.h"

#include <QSettings>

namespace pvf {
namespace Settings {

namespace {
QSettings store()
{
    return QSettings(QStringLiteral("PVForge"), QStringLiteral("PVForge"));
}
} // namespace

int snapPixels()
{
    return store().value(QStringLiteral("editor/snapPixels"), 6).toInt();
}

void setSnapPixels(int pixels)
{
    store().setValue(QStringLiteral("editor/snapPixels"), pixels);
}

bool edgeCenterSnap()
{
    return store().value(QStringLiteral("editor/edgeCenterSnap"), true).toBool();
}

void setEdgeCenterSnap(bool on)
{
    store().setValue(QStringLiteral("editor/edgeCenterSnap"), on);
}

bool edgeMidpointSnap()
{
    return store().value(QStringLiteral("editor/edgeMidpointSnap"), false).toBool();
}

void setEdgeMidpointSnap(bool on)
{
    store().setValue(QStringLiteral("editor/edgeMidpointSnap"), on);
}

QList<int> snapAngles()
{
    const QString raw =
        store().value(QStringLiteral("editor/snapAngles"), QStringLiteral("0,90"))
            .toString();
    QList<int> angles;
    const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts)
        angles.append(part.trimmed().toInt());
    if (angles.isEmpty())
        angles.append(0);
    return angles;
}

void setSnapAngles(const QList<int>& angles)
{
    QStringList parts;
    for (int angle : angles)
        parts.append(QString::number(angle));
    store().setValue(QStringLiteral("editor/snapAngles"), parts.join(QLatin1Char(',')));
}

QString language()
{
    return store().value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
}

void setLanguage(const QString& code)
{
    store().setValue(QStringLiteral("ui/language"), code);
}

bool autoKeyDefault()
{
    return store().value(QStringLiteral("editor/autoKeyDefault"), true).toBool();
}

void setAutoKeyDefault(bool on)
{
    store().setValue(QStringLiteral("editor/autoKeyDefault"), on);
}

int defaultWidth()
{
    return store().value(QStringLiteral("project/width"), 1920).toInt();
}

int defaultHeight()
{
    return store().value(QStringLiteral("project/height"), 1080).toInt();
}

int defaultFps()
{
    return store().value(QStringLiteral("project/fps"), 30).toInt();
}

void setDefaultSize(int width, int height, int fps)
{
    store().setValue(QStringLiteral("project/width"), width);
    store().setValue(QStringLiteral("project/height"), height);
    store().setValue(QStringLiteral("project/fps"), fps);
}

double timelinePixelsPerFrame()
{
    return store().value(QStringLiteral("timeline/pixelsPerFrame"), 6.0).toDouble();
}

void setTimelinePixelsPerFrame(double pixels)
{
    store().setValue(QStringLiteral("timeline/pixelsPerFrame"), pixels);
}

bool packAssetsDefault()
{
    return store().value(QStringLiteral("assets/packDefault"), false).toBool();
}

void setPackAssetsDefault(bool on)
{
    store().setValue(QStringLiteral("assets/packDefault"), on);
}

} // namespace Settings
} // namespace pvf
