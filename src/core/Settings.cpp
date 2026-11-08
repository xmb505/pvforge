#include "Settings.h"

#include <QCryptographicHash>
#include <QSettings>

#include <algorithm>

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

bool autosaveEnabled()
{
    return store().value(QStringLiteral("editor/autosaveEnabled"), true).toBool();
}

void setAutosaveEnabled(bool on)
{
    store().setValue(QStringLiteral("editor/autosaveEnabled"), on);
}

int autosaveMinutes()
{
    return std::clamp(
        store().value(QStringLiteral("editor/autosaveMinutes"), 5).toInt(), 1, 120);
}

void setAutosaveMinutes(int minutes)
{
    store().setValue(QStringLiteral("editor/autosaveMinutes"),
                     std::clamp(minutes, 1, 120));
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

bool timelineDisplayFrames()
{
    return store().value(QStringLiteral("timeline/displayFrames"), false).toBool();
}

void setTimelineDisplayFrames(bool on)
{
    store().setValue(QStringLiteral("timeline/displayFrames"), on);
}

bool perfMonitor()
{
    return store().value(QStringLiteral("editor/perfMonitor"), false).toBool();
}

void setPerfMonitor(bool on)
{
    store().setValue(QStringLiteral("editor/perfMonitor"), on);
}

namespace {
QString frameKey(const QString& path)
{
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(),
                                                     QCryptographicHash::Sha1);
    return QStringLiteral("playhead/") + QString::fromLatin1(hash.toHex());
}
} // namespace

int projectFrame(const QString& path)
{
    if (path.isEmpty())
        return 0;
    return std::max(0, store().value(frameKey(path), 0).toInt());
}

void setProjectFrame(const QString& path, int frame)
{
    if (path.isEmpty())
        return;
    store().setValue(frameKey(path), std::max(0, frame));
}

bool packAssetsDefault()
{
    return store().value(QStringLiteral("assets/packDefault"), false).toBool();
}

void setPackAssetsDefault(bool on)
{
    store().setValue(QStringLiteral("assets/packDefault"), on);
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

bool mcpPortAuto()
{
    return store().value(QStringLiteral("mcp/portAuto"), true).toBool();
}

void setMcpPortAuto(bool on)
{
    store().setValue(QStringLiteral("mcp/portAuto"), on);
}

int mcpPort()
{
    return std::clamp(store().value(QStringLiteral("mcp/port"), 8765).toInt(),
                      1024, 65535);
}

void setMcpPort(int port)
{
    store().setValue(QStringLiteral("mcp/port"), std::clamp(port, 1024, 65535));
}

bool mcpLogVisible()
{
    return store().value(QStringLiteral("mcp/logVisible"), true).toBool();
}

void setMcpLogVisible(bool on)
{
    store().setValue(QStringLiteral("mcp/logVisible"), on);
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

QString exportDirectory()
{
    return store().value(QStringLiteral("export/directory")).toString();
}

void setExportDirectory(const QString& path)
{
    if (!path.isEmpty())
        store().setValue(QStringLiteral("export/directory"), path);
}

int exportCrf()
{
    return std::clamp(store().value(QStringLiteral("export/crf"), 20).toInt(), 0, 51);
}

void setExportCrf(int crf)
{
    store().setValue(QStringLiteral("export/crf"), std::clamp(crf, 0, 51));
}

bool exportAudio()
{
    return store().value(QStringLiteral("export/audio"), true).toBool();
}

void setExportAudio(bool on)
{
    store().setValue(QStringLiteral("export/audio"), on);
}

int exportEncoder()
{
    return store().value(QStringLiteral("export/encoder"), 0).toInt() == 1 ? 1 : 0;
}

void setExportEncoder(int encoder)
{
    store().setValue(QStringLiteral("export/encoder"), encoder == 1 ? 1 : 0);
}

bool showCameraFrame()
{
    return store().value(QStringLiteral("editor/showCameraFrame"), true).toBool();
}

void setShowCameraFrame(bool on)
{
    store().setValue(QStringLiteral("editor/showCameraFrame"), on);
}

bool audioScrub()
{
    return store().value(QStringLiteral("editor/audioScrub"), true).toBool();
}

void setAudioScrub(bool on)
{
    store().setValue(QStringLiteral("editor/audioScrub"), on);
}

} // namespace Settings
} // namespace pvf
