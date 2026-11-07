#pragma once

#include <QString>

namespace pvf {

// Persisted user preferences, backed by QSettings.
namespace Settings {

int snapPixels();                       // 0 disables snapping
void setSnapPixels(int pixels);

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
