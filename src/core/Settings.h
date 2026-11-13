#pragma once

#include <QList>
#include <QRect>
#include <QString>

namespace pvf {

// Persisted user preferences, backed by QSettings.
namespace Settings {

int snapPixels();                       // 0 disables snapping
void setSnapPixels(int pixels);

bool edgeCenterSnap();                  // entity edges snap to canvas centre lines
void setEdgeCenterSnap(bool on);

bool edgeMidpointSnap();                // snap to edge midpoints of other entities
void setEdgeMidpointSnap(bool on);

QList<int> snapAngles();                // rotation snapping targets in degrees
void setSnapAngles(const QList<int>& angles);

QString language();                     // e.g. "zh_CN"
void setLanguage(const QString& code);

bool autoKeyDefault();
void setAutoKeyDefault(bool on);

bool autosaveEnabled();                 // periodically save named projects
void setAutosaveEnabled(bool on);

int autosaveMinutes();                  // default 5
void setAutosaveMinutes(int minutes);

int defaultWidth();
int defaultHeight();
int defaultFps();
void setDefaultSize(int width, int height, int fps);

double timelinePixelsPerFrame();
void setTimelinePixelsPerFrame(double pixels);

bool timelineDisplayFrames();           // ruler labels show frames instead of seconds
void setTimelineDisplayFrames(bool on);

bool perfMonitor();                     // show the performance overlay in the status bar
void setPerfMonitor(bool on);

bool monitorHighQuality();
void setMonitorHighQuality(bool on);

QRect monitorDockGeometry();
void setMonitorDockGeometry(const QRect& geometry);

QRect monitorWindowGeometry();
void setMonitorWindowGeometry(const QRect& geometry);

bool monitorAlwaysOnTop();
void setMonitorAlwaysOnTop(bool on);

bool monitorVisible();
void setMonitorVisible(bool on);

// MCP settings. "Enabled" is deliberately not persisted: every launch starts
// with the server off and the human has to enable it again.
bool mcpPortAuto();
void setMcpPortAuto(bool on);
int mcpPort();
void setMcpPort(int port);
bool mcpLogVisible();
void setMcpLogVisible(bool on);
QRect mcpLogDockGeometry();
void setMcpLogDockGeometry(const QRect& geometry);
QRect mcpLogWindowGeometry();
void setMcpLogWindowGeometry(const QRect& geometry);

// Workbench panel host state.  Keys live under `panel/workbench/*` so the
// remaining panels can adopt the same layout scheme later.
QString workbenchMode();                // "docked" (default) or "floating"
void setWorkbenchMode(const QString& mode);
bool workbenchVisible();
void setWorkbenchVisible(bool on);
QRect workbenchDockGeometry();          // (-1,-1,...) means "not set"
void setWorkbenchDockGeometry(const QRect& geometry);
QRect workbenchWindowGeometry();        // (-1,-1,...) means "not set"
void setWorkbenchWindowGeometry(const QRect& geometry);

bool showCameraFrame();
void setShowCameraFrame(bool on);

bool audioScrub();
void setAudioScrub(bool on);

bool packAssetsDefault();
void setPackAssetsDefault(bool on);

// Playhead position remembered per project file, so reopening a project
// returns to where the user left it.
int projectFrame(const QString& path);
void setProjectFrame(const QString& path, int frame);

// Last used MP4 export parameters.
QString exportDirectory();
void setExportDirectory(const QString& path);
int exportCrf();
void setExportCrf(int crf);
bool exportAudio();
void setExportAudio(bool on);
int exportEncoder();                    // 0 = libx264, 1 = VAAPI
void setExportEncoder(int encoder);
} // namespace Settings

} // namespace pvf
