#include "SystemInfo.h"

#include "media/DecoderInfo.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QSysInfo>
#include <QThread>

#if __has_include(<sys/sysinfo.h>)
#include <sys/sysinfo.h>
#endif

namespace pvf {

namespace {

QString readOsPrettyName()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        while (!file.atEnd())
        {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.startsWith(QStringLiteral("PRETTY_NAME=")))
                continue;
            QString name = line.mid(12).trimmed();
            if (name.size() >= 2 && name.startsWith(QLatin1Char('"'))
                && name.endsWith(QLatin1Char('"')))
            {
                name = name.mid(1, name.size() - 2);
            }
            if (!name.isEmpty())
                return name;
        }
    }
    const QString pretty = QSysInfo::prettyProductName();
    return pretty.isEmpty() ? QStringLiteral("未知系统") : pretty;
}

struct CpuDetails
{
    QString model;
    int physicalCores = 0;
    int logicalCores = 0;
};

CpuDetails readCpuDetails()
{
    CpuDetails cpu;
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        cpu.model = QStringLiteral("未知处理器");
        cpu.logicalCores = QThread::idealThreadCount();
        cpu.physicalCores = cpu.logicalCores;
        return cpu;
    }

    QSet<QString> cores;
    QString physicalId;
    QString coreId;
    // QFile::atEnd() is unreliable on procfs (st_size is 0), so read all first.
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString& raw : lines)
    {
        const QString line = raw.trimmed();
        if (line.isEmpty())
        {
            if (!physicalId.isEmpty() && !coreId.isEmpty())
                cores.insert(physicalId + QLatin1Char(':') + coreId);
            physicalId.clear();
            coreId.clear();
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QStringLiteral("processor"))
        {
            ++cpu.logicalCores;
        }
        else if (key == QStringLiteral("model name") && cpu.model.isEmpty())
        {
            cpu.model = value;
        }
        else if (key == QStringLiteral("physical id"))
        {
            physicalId = value;
        }
        else if (key == QStringLiteral("core id"))
        {
            coreId = value;
        }
    }
    if (!physicalId.isEmpty() && !coreId.isEmpty())
        cores.insert(physicalId + QLatin1Char(':') + coreId);

    if (cpu.model.isEmpty())
        cpu.model = QStringLiteral("未知处理器");
    if (cpu.logicalCores <= 0)
        cpu.logicalCores = QThread::idealThreadCount();
    cpu.physicalCores = cores.isEmpty() ? cpu.logicalCores : int(cores.size());
    return cpu;
}

QString readDesktopSession()
{
    QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (desktop.isEmpty())
        desktop = qEnvironmentVariable("DESKTOP_SESSION");
    const QString type = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (desktop.isEmpty() && type.isEmpty())
        return QStringLiteral("未知");
    if (desktop.isEmpty())
        return type;
    if (type.isEmpty())
        return desktop;
    return QStringLiteral("%1（%2）").arg(desktop, type);
}

QString readScreens()
{
    QStringList screens;
    QScreen* primary = QGuiApplication::primaryScreen();
    for (QScreen* screen : QGuiApplication::screens())
    {
        QString entry = screen->name();
        if (entry.isEmpty())
            entry = QStringLiteral("屏幕");
        const QRect geometry = screen->geometry();
        entry += QStringLiteral(" %1×%2").arg(geometry.width()).arg(geometry.height());
        if (!qFuzzyCompare(screen->devicePixelRatio(), 1.0))
        {
            entry += QStringLiteral("（DPR %1×）")
                         .arg(screen->devicePixelRatio(), 0, 'g', 3);
        }
        if (screen->refreshRate() > 1.0)
            entry += QStringLiteral(" @ %1 Hz").arg(screen->refreshRate(), 0, 'g', 3);
        if (screen == primary)
            entry += QStringLiteral("（主屏）");
        screens << entry;
    }
    return screens.isEmpty() ? QStringLiteral("未检测到") : screens.join(QStringLiteral("；"));
}

QString gpuFromLspci()
{
    QProcess process;
    process.start(QStringLiteral("lspci"), {QStringLiteral("-nn")});
    if (!process.waitForFinished(1500))
    {
        process.kill();
        process.waitForFinished(200);
        return QString();
    }
    if (process.exitStatus() != QProcess::NormalExit)
        return QString();

    const QStringList lines =
        QString::fromLocal8Bit(process.readAllStandardOutput()).split(QLatin1Char('\n'));
    static const QRegularExpression revision(
        QStringLiteral("\\s*\\(rev [0-9a-fA-F]+\\)\\s*$"));
    QStringList gpus;
    for (const QString& line : lines)
    {
        // PCI class 0300/0302/0380: VGA, 3D and other display controllers.
        if (!line.contains(QStringLiteral("[0300]"))
            && !line.contains(QStringLiteral("[0302]"))
            && !line.contains(QStringLiteral("[0380]")))
        {
            continue;
        }
        const int colon = line.indexOf(QStringLiteral("]: "));
        QString name = colon >= 0 ? line.mid(colon + 3) : line;
        name.remove(revision);
        name = name.trimmed();
        if (!name.isEmpty())
            gpus << name;
    }
    return gpus.join(QStringLiteral("；"));
}

QString gpuFromSysfs()
{
    static const QHash<QString, QString> vendors{
        {QStringLiteral("0x8086"), QStringLiteral("Intel")},
        {QStringLiteral("0x10de"), QStringLiteral("NVIDIA")},
        {QStringLiteral("0x1002"), QStringLiteral("AMD")},
        {QStringLiteral("0x13b5"), QStringLiteral("ARM")},
        {QStringLiteral("0x5143"), QStringLiteral("Qualcomm")},
        {QStringLiteral("0x1af4"), QStringLiteral("Red Hat")},
    };
    static const QRegularExpression cardName(QStringLiteral("^card\\d+$"));

    QDir drm(QStringLiteral("/sys/class/drm"));
    QStringList gpus;
    for (const QString& entry : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (!cardName.match(entry).hasMatch())
            continue;
        QFile vendorFile(drm.filePath(entry + QStringLiteral("/device/vendor")));
        QFile deviceFile(drm.filePath(entry + QStringLiteral("/device/device")));
        if (!vendorFile.open(QIODevice::ReadOnly | QIODevice::Text)
            || !deviceFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            continue;
        }
        const QString vendor =
            QString::fromLatin1(vendorFile.readAll()).trimmed().toLower();
        const QString device =
            QString::fromLatin1(deviceFile.readAll()).trimmed().toLower();
        const QString vendorName =
            vendors.value(vendor, QStringLiteral("PCI"));
        const QString ids = vendor.mid(2) + QLatin1Char(':') + device.mid(2);
        gpus << QStringLiteral("%1 显卡（%2）").arg(vendorName, ids);
    }
    return gpus.join(QStringLiteral("；"));
}

QString readGpus()
{
    const QString gpus = gpuFromLspci();
    if (!gpus.isEmpty())
        return gpus;
    const QString sysfs = gpuFromSysfs();
    return sysfs.isEmpty() ? QStringLiteral("未检测到") : sysfs;
}

QString formatBytes(quint64 bytes)
{
    if (bytes == 0)
        return QStringLiteral("未知");
    return QLocale::system().formattedDataSize(qint64(bytes), 1,
                                               QLocale::DataSizeIecFormat);
}

} // namespace

SystemInfo collectSystemInfo()
{
    SystemInfo info;
#ifdef PVFORGE_VERSION
    info.appVersion = QString::fromLatin1(PVFORGE_VERSION);
#else
    info.appVersion = QStringLiteral("0.0.0");
#endif
#ifdef PVFORGE_BUILD_TYPE
    info.buildType = QString::fromLatin1(PVFORGE_BUILD_TYPE);
#endif
    if (info.buildType.isEmpty())
        info.buildType = QStringLiteral("默认");

    info.qtVersion = QString::fromLatin1(qVersion());
    info.qtBuildVersion = QString::fromLatin1(QT_VERSION_STR);
    info.ffmpegVersion = decoderInfo().ffmpegVersion;

    info.osName = readOsPrettyName();
    info.kernelType = QSysInfo::kernelType();
    info.kernelVersion = QSysInfo::kernelVersion();
    info.architecture = QSysInfo::currentCpuArchitecture();
    info.desktopSession = readDesktopSession();
    info.screens = readScreens();
    info.locale = QLocale::system().name();

    const CpuDetails cpu = readCpuDetails();
    info.cpuModel = cpu.model;
    info.physicalCores = cpu.physicalCores;
    info.logicalCores = cpu.logicalCores;

#if __has_include(<sys/sysinfo.h>)
    struct sysinfo systemInfo
    {
    };
    if (sysinfo(&systemInfo) == 0)
    {
        info.memoryBytes = quint64(systemInfo.totalram) * systemInfo.mem_unit;
        info.swapBytes = quint64(systemInfo.totalswap) * systemInfo.mem_unit;
    }
#endif

    info.gpus = readGpus();
    return info;
}

QString SystemInfo::toText() const
{
    QString kernel = kernelType;
    if (!kernel.isEmpty())
        kernel[0] = kernel.at(0).toUpper();
    if (!kernelVersion.isEmpty())
        kernel += QLatin1Char(' ') + kernelVersion;

    QStringList lines;
    lines << QStringLiteral("PVForge %1（%2 构建）").arg(appVersion, buildType);
    lines << QStringLiteral("Qt %1（运行时）/ %2（编译）").arg(qtVersion, qtBuildVersion);
    lines << QStringLiteral("FFmpeg %1").arg(ffmpegVersion);
    lines << QString();
    lines << QStringLiteral("【系统】");
    lines << QStringLiteral("系统：%1").arg(osName);
    lines << QStringLiteral("内核：%1").arg(kernel.trimmed());
    lines << QStringLiteral("架构：%1").arg(architecture);
    lines << QStringLiteral("桌面：%1").arg(desktopSession);
    lines << QStringLiteral("语言：%1").arg(locale);
    lines << QStringLiteral("显示器：%1").arg(screens);
    lines << QString();
    lines << QStringLiteral("【硬件】");
    lines << QStringLiteral("处理器：%1").arg(cpuModel);
    lines << QStringLiteral("核心：%1 核 / %2 线程").arg(physicalCores).arg(logicalCores);
    lines << QStringLiteral("内存：%1（交换 %2）")
                 .arg(formatBytes(memoryBytes), formatBytes(swapBytes));
    lines << QStringLiteral("显卡：%1").arg(gpus);
    return lines.join(QLatin1Char('\n'));
}

} // namespace pvf
