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
} // namespace Settings

} // namespace pvf
