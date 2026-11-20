#include "mcp2/EndpointFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cerrno>
#include <cstdio>

#if defined(Q_OS_UNIX)
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace pvf::mcp2::detail {

namespace {

QFileDevice::Permissions privateFilePermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

QFileDevice::Permissions privateDirectoryPermissions()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
}

// Both mcp.json and the endpoint file contain the bearer token. Refuse to
// follow a symlink or touch a file owned by another user, and never write into
// a directory this user does not own.
bool checkPrivateDirectory(const QString& directory, QString* error)
{
#if defined(Q_OS_UNIX)
    struct stat info;
    if (::stat(QFile::encodeName(directory).constData(), &info) != 0
        || !S_ISDIR(info.st_mode))
    {
        if (error)
            *error = QStringLiteral("%1 不是可用的配置目录").arg(directory);
        return false;
    }
    if (info.st_uid != ::geteuid())
    {
        if (error)
            *error = QStringLiteral("%1 不属于当前用户").arg(directory);
        return false;
    }
#else
    Q_UNUSED(directory);
    Q_UNUSED(error);
#endif
    return true;
}

bool checkPrivateFile(const QString& path, QString* error)
{
#if defined(Q_OS_UNIX)
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0)
        return true; // Missing is fine; the caller creates it.
    if (S_ISLNK(info.st_mode))
    {
        if (error)
            *error = QStringLiteral("%1 是符号链接，拒绝写入").arg(path);
        return false;
    }
    if (!S_ISREG(info.st_mode))
    {
        if (error)
            *error = QStringLiteral("%1 不是普通文件，拒绝写入").arg(path);
        return false;
    }
    if (info.st_uid != ::geteuid())
    {
        if (error)
            *error = QStringLiteral("%1 不属于当前用户").arg(path);
        return false;
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(error);
#endif
    return true;
}

} // namespace

QString configDirectory()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/PVForge");
}

QString defaultConfigPath()
{
    return configDirectory() + QStringLiteral("/mcp.json");
}

QString defaultEndpointPath()
{
#if defined(Q_OS_MACOS)
    // Keep this identical to defaultMcpEndpointPath() in src/mcp/StdioBridge.cpp;
    // a different directory made the bridge unable to find the GUI endpoint.
    return QDir::homePath()
           + QStringLiteral("/Library/Application Support/PVForge/mcp-endpoint.json");
#else
    return configDirectory() + QStringLiteral("/mcp-endpoint.json");
#endif
}

bool writePrivateFile(const QString& path, const QByteArray& data, QString* error)
{
    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    if (!QDir().mkpath(directory))
    {
        if (error)
            *error = QStringLiteral("无法创建目录 %1").arg(directory);
        return false;
    }
    if (!checkPrivateDirectory(directory, error))
        return false;
    if (!QFile::setPermissions(directory, privateDirectoryPermissions()))
    {
#if defined(Q_OS_UNIX)
        if (error)
            *error = QStringLiteral("无法设置目录权限 %1").arg(directory);
        return false;
#endif
    }
    if (!checkPrivateFile(path, error))
        return false;

    const QString tempPath =
        path + QStringLiteral(".tmp-%1").arg(QCoreApplication::applicationPid());
    QFile::remove(tempPath);
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (!file.setPermissions(privateFilePermissions()))
    {
        if (error)
            *error = QStringLiteral("无法设置临时文件权限");
        file.close();
        QFile::remove(tempPath);
        return false;
    }
    if (file.write(data) != data.size() || !file.flush())
    {
        if (error)
            *error = file.errorString();
        file.close();
        QFile::remove(tempPath);
        return false;
    }
    file.close();

#if defined(Q_OS_UNIX)
    if (std::rename(QFile::encodeName(tempPath).constData(),
                    QFile::encodeName(path).constData())
        != 0)
    {
        if (error)
            *error = QStringLiteral("无法替换文件 %1").arg(path);
        QFile::remove(tempPath);
        return false;
    }
#else
    QFile::remove(path);
    if (!QFile::rename(tempPath, path))
    {
        if (error)
            *error = QStringLiteral("无法替换文件 %1").arg(path);
        QFile::remove(tempPath);
        return false;
    }
#endif
    if (!QFile::setPermissions(path, privateFilePermissions()))
    {
        if (error)
            *error = QStringLiteral("无法设置文件权限 %1").arg(path);
        return false;
    }
    return true;
}

// True when the pid in an endpoint file cannot belong to a live PVForge
// instance of this user. A reused pid is detected through /proc on Linux.
bool endpointPidIsStale(qint64 pid)
{
    if (pid <= 0)
        return true;
    if (pid == qint64(QCoreApplication::applicationPid()))
        return true; // This process has not written the endpoint yet.
#if defined(Q_OS_UNIX)
    if (::kill(pid_t(pid), 0) != 0 && errno == ESRCH)
        return true;
#if defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly))
        return true;
    while (true)
    {
        const QByteArray line = status.readLine();
        if (line.isEmpty())
            return true;
        if (!line.startsWith("Uid:"))
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        return fields.size() < 2 || fields.at(1).toUInt() != ::geteuid();
    }
#endif
#endif
    return false;
}

} // namespace pvf::mcp2::detail
