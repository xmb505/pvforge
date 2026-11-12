#pragma once

#include <QString>
#include <QtGlobal>

#include <memory>

namespace pvf::perf {

// Snapshot of the process and host values shown in the monitor window.  The
// provider only reads /proc on Linux; other platforms report unsupported so
// the UI can keep the rest of the page functional.
struct SystemStats
{
    bool supported = false;
    QString note;
    double processCpuPercent = 0.0;
    qint64 rssBytes = 0;
    qint64 ioReadBytes = 0;
    qint64 ioWriteBytes = 0;
    int threadCount = 0;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
    qint64 cacheDiskBytes = 0;
    int cacheFileCount = 0;
};

class SystemStatsProvider
{
public:
    virtual ~SystemStatsProvider() = default;

    static std::unique_ptr<SystemStatsProvider> create();

    virtual bool supported() const = 0;
    // `cacheRoot` is the workbench cache directory; empty disables the disk
    // usage scan.  Sampling is expected on the UI timer, never in a frame.
    virtual SystemStats sample(const QString& cacheRoot) = 0;
};

} // namespace pvf::perf
