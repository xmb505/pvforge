#include "perf/SystemStats.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace pvf::perf {
namespace {

class UnsupportedSystemStatsProvider final : public SystemStatsProvider
{
public:
    bool supported() const override { return false; }

    SystemStats sample(const QString& cacheRoot) override
    {
        Q_UNUSED(cacheRoot);
        SystemStats stats;
        stats.supported = false;
        stats.note = QStringLiteral("当前平台暂不支持系统参数");
        return stats;
    }
};

#ifdef Q_OS_LINUX

QByteArray readProcFile(const char* path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

class LinuxSystemStatsProvider final : public SystemStatsProvider
{
public:
    LinuxSystemStatsProvider()
    {
        m_cpuClock.start();
    }

    bool supported() const override { return true; }

    SystemStats sample(const QString& cacheRoot) override
    {
        SystemStats stats;
        stats.supported = true;

        // /proc/self/stat: fields after the parenthesised command.  utime and
        // stime are indices 11 and 12, num_threads is index 17.
        const QByteArray stat = readProcFile("/proc/self/stat");
        const int close = stat.lastIndexOf(')');
        if (close >= 0)
        {
            const QList<QByteArray> fields =
                stat.mid(close + 2).simplified().split(' ');
            if (fields.size() >= 18)
            {
                bool ok = false;
                const quint64 utime = fields[11].toULongLong(&ok);
                const quint64 stime = fields[12].toULongLong(&ok);
                const int threads = fields[17].toInt(&ok);
                if (ok)
                    stats.threadCount = threads;
                const quint64 ticks = utime + stime;
                const double elapsed =
                    m_cpuClock.isValid() ? m_cpuClock.nsecsElapsed() / 1e9 : 0.0;
                if (m_hasCpuSample && elapsed > 0.0)
                {
                    const double clockTicks =
                        double(sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK)
                                                       : 100);
                    stats.processCpuPercent =
                        double(ticks - m_lastCpuTicks) / clockTicks / elapsed
                        * 100.0;
                }
                m_lastCpuTicks = ticks;
                m_hasCpuSample = true;
                m_cpuClock.restart();
            }
        }

        // Resident set size in pages.
        const QByteArray statm = readProcFile("/proc/self/statm");
        const QList<QByteArray> statmFields = statm.simplified().split(' ');
        if (statmFields.size() >= 2)
        {
            bool ok = false;
            const quint64 pages = statmFields[1].toULongLong(&ok);
            if (ok)
                stats.rssBytes = qint64(pages) * qint64(sysconf(_SC_PAGESIZE));
        }

        // Process I/O counters (bytes actually reaching the block layer).
        const QByteArray io = readProcFile("/proc/self/io");
        for (const QByteArray& line : io.split('\n'))
        {
            if (line.startsWith("read_bytes:"))
                stats.ioReadBytes = line.mid(11).trimmed().toLongLong();
            else if (line.startsWith("write_bytes:"))
                stats.ioWriteBytes = line.mid(12).trimmed().toLongLong();
        }

        const QByteArray load = readProcFile("/proc/loadavg");
        const QList<QByteArray> loadFields = load.simplified().split(' ');
        if (loadFields.size() >= 3)
        {
            stats.load1 = loadFields[0].toDouble();
            stats.load5 = loadFields[1].toDouble();
            stats.load15 = loadFields[2].toDouble();
        }

        sampleCacheDisk(cacheRoot, &stats);
        return stats;
    }

private:
    // Walking a cache with thousands of files is much slower than a frame, so
    // the window timer (500 ms) only refreshes the directory size every 5 s.
    void sampleCacheDisk(const QString& cacheRoot, SystemStats* stats)
    {
        if (cacheRoot.isEmpty())
        {
            m_cacheBytes = 0;
            m_cacheFiles = 0;
            return;
        }
        if (cacheRoot != m_cacheRoot)
        {
            m_cacheRoot = cacheRoot;
            m_cacheClock.invalidate();
        }
        constexpr qint64 kDiskRefreshMs = 5000;
        if (!m_cacheClock.isValid()
            || m_cacheClock.elapsed() >= kDiskRefreshMs)
        {
            qint64 bytes = 0;
            int files = 0;
            QDirIterator it(cacheRoot, QDir::Files | QDir::Hidden,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                const QFileInfo info = it.nextFileInfo();
                bytes += info.size();
                if (info.suffix().compare(QStringLiteral("qoi"),
                                          Qt::CaseInsensitive)
                        == 0
                    || info.suffix().compare(QStringLiteral("webp"),
                                             Qt::CaseInsensitive)
                           == 0)
                    ++files;
            }
            m_cacheBytes = bytes;
            m_cacheFiles = files;
            m_cacheClock.restart();
        }
        stats->cacheDiskBytes = m_cacheBytes;
        stats->cacheFileCount = m_cacheFiles;
    }

    QElapsedTimer m_cpuClock;
    quint64 m_lastCpuTicks = 0;
    bool m_hasCpuSample = false;
    QElapsedTimer m_cacheClock;
    QString m_cacheRoot;
    qint64 m_cacheBytes = 0;
    int m_cacheFiles = 0;
};

#endif // Q_OS_LINUX

} // namespace

std::unique_ptr<SystemStatsProvider> SystemStatsProvider::create()
{
#ifdef Q_OS_LINUX
    return std::make_unique<LinuxSystemStatsProvider>();
#else
    return std::make_unique<UnsupportedSystemStatsProvider>();
#endif
}

} // namespace pvf::perf
