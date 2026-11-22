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

QString language()
{
    return store().value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
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

bool timelineDisplayFrames()
{
    return store().value(QStringLiteral("timeline/displayFrames"), false).toBool();
}

bool monitorHighQuality()
{
    return store().value(QStringLiteral("monitor/highQuality"), false).toBool();
}

void setMonitorHighQuality(bool on)
{
    store().setValue(QStringLiteral("monitor/highQuality"), on);
}

QRect monitorDockGeometry()
{
    return store()
        .value(QStringLiteral("monitor/dockGeometry"), QRect(-1, -1, 320, 208))
        .toRect();
}

void setMonitorDockGeometry(const QRect& geometry)
{
    store().setValue(QStringLiteral("monitor/dockGeometry"), geometry);
}

QRect monitorWindowGeometry()
{
    return store()
        .value(QStringLiteral("monitor/windowGeometry"), QRect(100, 100, 640, 360))
        .toRect();
}

void setMonitorWindowGeometry(const QRect& geometry)
{
    store().setValue(QStringLiteral("monitor/windowGeometry"), geometry);
}

bool monitorAlwaysOnTop()
{
    return store().value(QStringLiteral("monitor/alwaysOnTop"), false).toBool();
}

void setMonitorAlwaysOnTop(bool on)
{
    store().setValue(QStringLiteral("monitor/alwaysOnTop"), on);
}

bool monitorVisible()
{
    return store().value(QStringLiteral("monitor/visible"), true).toBool();
}

void setMonitorVisible(bool on)
{
    store().setValue(QStringLiteral("monitor/visible"), on);
}

bool mcpLogVisible()
{
    return store().value(QStringLiteral("mcp/logVisible"), true).toBool();
}

QRect mcpLogDockGeometry()
{
    return store()
        .value(QStringLiteral("mcp/logDockGeometry"), QRect(-1, -1, 420, 280))
        .toRect();
}

void setMcpLogDockGeometry(const QRect& geometry)
{
    store().setValue(QStringLiteral("mcp/logDockGeometry"), geometry);
}

QRect mcpLogWindowGeometry()
{
    return store()
        .value(QStringLiteral("mcp/logWindowGeometry"), QRect(100, 100, 560, 360))
        .toRect();
}

void setMcpLogWindowGeometry(const QRect& geometry)
{
    store().setValue(QStringLiteral("mcp/logWindowGeometry"), geometry);
}

bool showCameraFrame()
{
    return store().value(QStringLiteral("editor/showCameraFrame"), true).toBool();
}

void setShowCameraFrame(bool on)
{
    store().setValue(QStringLiteral("editor/showCameraFrame"), on);
}

} // namespace Settings
} // namespace pvf
