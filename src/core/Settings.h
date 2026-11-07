#pragma once

#include <QList>
#include <QString>

namespace pvf {

// Persisted user preferences, backed by QSettings.
namespace Settings {

int snapPixels();                       // 0 disables snapping
void setSnapPixels(int pixels);

bool edgeCenterSnap();                  // entity edges snap to canvas centre lines
void setEdgeCenterSnap(bool on);

QList<int> snapAngles();                // rotation snapping targets in degrees
void setSnapAngles(const QList<int>& angles);

QString language();                     // e.g. "zh_CN"
void setLanguage(const QString& code);

bool autoKeyDefault();
void setAutoKeyDefault(bool on);

int defaultWidth();
int defaultHeight();
int defaultFps();
void setDefaultSize(int width, int height, int fps);

double timelinePixelsPerFrame();
void setTimelinePixelsPerFrame(double pixels);

bool packAssetsDefault();
void setPackAssetsDefault(bool on);

} // namespace Settings

} // namespace pvf
