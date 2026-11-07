#pragma once

#include <QString>
#include <QtGlobal>

namespace pvf {

// Snapshot of the machine and runtime the editor is currently using.
// Only read-only diagnostics: no preference is stored here.
struct SystemInfo
{
    QString appVersion;
    QString buildType;
    QString qtVersion;       // runtime
    QString qtBuildVersion;  // compiled against
    QString ffmpegVersion;
    QString osName;
    QString kernelType;
    QString kernelVersion;
    QString architecture;
    QString desktopSession;
    QString screens;
    QString cpuModel;
    int physicalCores = 0;
    int logicalCores = 0;
    quint64 memoryBytes = 0;
    quint64 swapBytes = 0;
    QString gpus;
    QString locale;

    QString toText() const;
};

SystemInfo collectSystemInfo();

} // namespace pvf
