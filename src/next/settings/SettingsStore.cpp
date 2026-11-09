#include "next/settings/SettingsStore.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>

namespace pvf::next {

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

constexpr qint64 kMinMemoryBytes = 32 * kMiB;
constexpr qint64 kMaxMemoryBytes = 4096 * kMiB;
constexpr qint64 kMinDiskBytes = 1 * kGiB;
constexpr qint64 kMaxDiskBytes = 64 * kGiB;

constexpr int kMinMaxWidth = 320;
constexpr int kMaxMaxWidth = 4096;
constexpr int kMinWatchdogMs = 500;
constexpr int kMaxWatchdogMs = 30000;
constexpr int kMinMcpPort = 1024;
constexpr int kMaxMcpPort = 65535;
constexpr int kMinCommitDelayMs = 200;
constexpr int kMaxCommitDelayMs = 5000;

template <typename T, std::size_t N>
T nearestOf(T value, const std::array<T, N>& choices)
{
    T best = choices.front();
    for (const T choice : choices) {
        if (std::abs(value - choice) < std::abs(value - best))
            best = choice;
    }
    return best;
}

double snapHtmlScale(double value)
{
    if (!std::isfinite(value))
        return 0.5;
    return nearestOf(value, std::array<double, 4>{0.25, 0.5, 0.75, 1.0});
}

} // namespace

SettingsStore::SettingsStore(QString iniPath)
    : m_settings(iniPath.isEmpty()
                     ? QSettings(QStringLiteral("PVForge"), QStringLiteral("PVForge"))
                     : QSettings(iniPath, QSettings::IniFormat))
{
    if (!iniPath.isEmpty()) {
        const QString directory = QFileInfo(iniPath).absolutePath();
        if (!directory.isEmpty())
            QDir().mkpath(directory);
    }
}

AppSettings SettingsStore::sanitized(AppSettings settings)
{
    settings.htmlMemoryBytes =
        std::clamp(settings.htmlMemoryBytes, kMinMemoryBytes, kMaxMemoryBytes);
    settings.htmlDiskBytes =
        std::clamp(settings.htmlDiskBytes, kMinDiskBytes, kMaxDiskBytes);
    settings.htmlScale = snapHtmlScale(settings.htmlScale);
    settings.htmlMaxWidth =
        std::clamp(settings.htmlMaxWidth, kMinMaxWidth, kMaxMaxWidth);
    settings.watchdogMs = std::clamp(settings.watchdogMs, kMinWatchdogMs, kMaxWatchdogMs);

    settings.videoProxyWidth =
        nearestOf(settings.videoProxyWidth, std::array<int, 2>{960, 1280});
    settings.videoMemoryFrames =
        nearestOf(settings.videoMemoryFrames, std::array<int, 3>{32, 64, 128});

    settings.mcpPort = static_cast<quint16>(
        std::clamp<int>(settings.mcpPort, kMinMcpPort, kMaxMcpPort));
    settings.humanCommitDelayMs =
        std::clamp(settings.humanCommitDelayMs, kMinCommitDelayMs, kMaxCommitDelayMs);
    return settings;
}

AppSettings SettingsStore::load() const
{
    AppSettings settings;

    settings.htmlCacheEnabled =
        m_settings.value(QStringLiteral("html/cacheEnabled"), settings.htmlCacheEnabled)
            .toBool();
    settings.htmlMemoryBytes =
        m_settings.value(QStringLiteral("html/memoryBytes"), settings.htmlMemoryBytes)
            .toLongLong();
    settings.htmlDiskBytes =
        m_settings.value(QStringLiteral("html/diskBytes"), settings.htmlDiskBytes)
            .toLongLong();
    settings.htmlScale =
        m_settings.value(QStringLiteral("html/scale"), settings.htmlScale).toDouble();
    settings.htmlMaxWidth =
        m_settings.value(QStringLiteral("html/maxWidth"), settings.htmlMaxWidth).toInt();
    settings.watchdogMs =
        m_settings.value(QStringLiteral("html/watchdogMs"), settings.watchdogMs).toInt();
    settings.chromeExecutable =
        m_settings.value(QStringLiteral("html/chromeExecutable"), settings.chromeExecutable)
            .toString();

    settings.videoProxyEnabled =
        m_settings.value(QStringLiteral("video/proxyEnabled"), settings.videoProxyEnabled)
            .toBool();
    settings.videoProxyWidth =
        m_settings.value(QStringLiteral("video/proxyWidth"), settings.videoProxyWidth).toInt();
    settings.videoMemoryFrames =
        m_settings.value(QStringLiteral("video/memoryFrames"), settings.videoMemoryFrames)
            .toInt();

    settings.mcpPort = static_cast<quint16>(
        m_settings.value(QStringLiteral("mcp/port"), settings.mcpPort).toUInt());
    settings.humanCommitDelayMs =
        m_settings
            .value(QStringLiteral("git/humanCommitDelayMs"), settings.humanCommitDelayMs)
            .toInt();

    return sanitized(settings);
}

void SettingsStore::save(const AppSettings& settings)
{
    const AppSettings value = sanitized(settings);

    m_settings.setValue(QStringLiteral("html/cacheEnabled"), value.htmlCacheEnabled);
    m_settings.setValue(QStringLiteral("html/memoryBytes"), value.htmlMemoryBytes);
    m_settings.setValue(QStringLiteral("html/diskBytes"), value.htmlDiskBytes);
    m_settings.setValue(QStringLiteral("html/scale"), value.htmlScale);
    m_settings.setValue(QStringLiteral("html/maxWidth"), value.htmlMaxWidth);
    m_settings.setValue(QStringLiteral("html/watchdogMs"), value.watchdogMs);
    m_settings.setValue(QStringLiteral("html/chromeExecutable"), value.chromeExecutable);

    m_settings.setValue(QStringLiteral("video/proxyEnabled"), value.videoProxyEnabled);
    m_settings.setValue(QStringLiteral("video/proxyWidth"), value.videoProxyWidth);
    m_settings.setValue(QStringLiteral("video/memoryFrames"), value.videoMemoryFrames);

    m_settings.setValue(QStringLiteral("mcp/port"), value.mcpPort);
    m_settings.setValue(QStringLiteral("git/humanCommitDelayMs"), value.humanCommitDelayMs);

    m_settings.sync();
}

QString SettingsStore::path() const
{
    return m_settings.fileName();
}

} // namespace pvf::next
