#pragma once

#include <QRect>
#include <QString>

namespace pvf {

// Persisted user preferences, backed by QSettings.  Only the keys the
// delivery shell still reads live here; the session shell keeps its own
// next::SettingsStore.
namespace Settings {

QString language();                     // e.g. "zh_CN"

int defaultWidth();
int defaultHeight();
int defaultFps();

bool timelineDisplayFrames();           // ruler labels show frames instead of seconds

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

bool mcpLogVisible();
QRect mcpLogDockGeometry();
void setMcpLogDockGeometry(const QRect& geometry);
QRect mcpLogWindowGeometry();
void setMcpLogWindowGeometry(const QRect& geometry);

bool showCameraFrame();
void setShowCameraFrame(bool on);

} // namespace Settings

} // namespace pvf
