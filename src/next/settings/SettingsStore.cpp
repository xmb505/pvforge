#include "next/settings/SettingsStore.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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
constexpr int kMinAutoSaveMinutes = 1;
constexpr int kMaxAutoSaveMinutes = 60;

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

int snapWorkbenchScalePercent(int value)
{
    return nearestOf(value, std::array<int, 4>{25, 50, 75, 100});
}

QString snapAnglesToString(const QVector<double>& angles)
{
    QStringList parts;
    parts.reserve(angles.size());
    for (double angle : angles)
        parts.append(QString::number(angle, 'g', 12));
    return parts.join(QLatin1Char(','));
}

QVector<double> snapAnglesFromString(const QString& text,
                                     const QVector<double>& fallback)
{
    if (text.trimmed().isEmpty())
        return {};

    QVector<double> angles;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double angle = part.trimmed().toDouble(&ok);
        if (ok && std::isfinite(angle))
            angles.append(angle);
    }
    return angles.isEmpty() ? fallback : angles;
}

QVector<double> sanitizedSnapAngles(QVector<double> angles)
{
    QVector<double> normalized;
    normalized.reserve(angles.size());
    for (double angle : angles) {
        if (!std::isfinite(angle))
            continue;
        double value = std::fmod(angle, 360.0);
        if (value < 0.0)
            value += 360.0;
        normalized.append(value);
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()),
                     normalized.end());
    return normalized;
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

    settings.workbenchCacheScalePercent =
        snapWorkbenchScalePercent(settings.workbenchCacheScalePercent);
    settings.workbenchCacheExpandPercent =
        std::clamp(settings.workbenchCacheExpandPercent, 0, 200);
    settings.workbenchCacheDiskBytes =
        std::clamp(settings.workbenchCacheDiskBytes, kMinDiskBytes, kMaxDiskBytes);
    settings.workbenchCacheMemoryBytes = std::clamp(
        settings.workbenchCacheMemoryBytes, kMinMemoryBytes, kMaxMemoryBytes);

    settings.snapPixels = std::clamp(settings.snapPixels, 1, 32);
    settings.snapAngles = sanitizedSnapAngles(std::move(settings.snapAngles));

    settings.mcpPort = static_cast<quint16>(
        std::clamp<int>(settings.mcpPort, kMinMcpPort, kMaxMcpPort));
    settings.humanCommitDelayMs =
        std::clamp(settings.humanCommitDelayMs, kMinCommitDelayMs, kMaxCommitDelayMs);
    settings.autoSaveMinutes = std::clamp(
        settings.autoSaveMinutes, kMinAutoSaveMinutes, kMaxAutoSaveMinutes);
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

    settings.workbenchCacheEnabled =
        m_settings
            .value(QStringLiteral("workbench/enabled"), settings.workbenchCacheEnabled)
            .toBool();
    settings.workbenchCacheScalePercent =
        m_settings
            .value(QStringLiteral("workbench/scalePercent"),
                   settings.workbenchCacheScalePercent)
            .toInt();
    settings.workbenchCacheExpandPercent =
        m_settings
            .value(QStringLiteral("workbench/expandPercent"),
                   settings.workbenchCacheExpandPercent)
            .toInt();
    settings.workbenchCacheDiskBytes =
        m_settings
            .value(QStringLiteral("workbench/diskBytes"), settings.workbenchCacheDiskBytes)
            .toLongLong();
    settings.workbenchCacheMemoryBytes =
        m_settings
            .value(QStringLiteral("workbench/memoryBytes"),
                   settings.workbenchCacheMemoryBytes)
            .toLongLong();

    settings.snapPixels =
        m_settings.value(QStringLiteral("snap/pixels"), settings.snapPixels).toInt();
    settings.snapCanvasCenter =
        m_settings.value(QStringLiteral("snap/canvasCenter"),
                         settings.snapCanvasCenter)
            .toBool();
    settings.snapEntityEdges =
        m_settings.value(QStringLiteral("snap/entityEdges"),
                         settings.snapEntityEdges)
            .toBool();
    settings.snapEdgeMidpoints =
        m_settings.value(QStringLiteral("snap/edgeMidpoints"),
                         settings.snapEdgeMidpoints)
            .toBool();
    settings.snapAngles = snapAnglesFromString(
        m_settings.value(QStringLiteral("snap/angles"),
                         snapAnglesToString(settings.snapAngles))
            .toString(),
        settings.snapAngles);

    settings.mcpPort = static_cast<quint16>(
        m_settings.value(QStringLiteral("mcp/port"), settings.mcpPort).toUInt());
    settings.humanCommitDelayMs =
        m_settings
            .value(QStringLiteral("git/humanCommitDelayMs"), settings.humanCommitDelayMs)
            .toInt();
    settings.autoSaveMinutes =
        m_settings
            .value(QStringLiteral("app/autoSaveMinutes"), settings.autoSaveMinutes)
            .toInt();
    settings.gitEnabled =
        m_settings.value(QStringLiteral("git/enabled"), settings.gitEnabled).toBool();
    settings.onboardingDone =
        m_settings.value(QStringLiteral("app/onboardingDone"), settings.onboardingDone)
            .toBool();

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

    m_settings.setValue(QStringLiteral("workbench/enabled"), value.workbenchCacheEnabled);
    m_settings.setValue(QStringLiteral("workbench/scalePercent"),
                        value.workbenchCacheScalePercent);
    m_settings.setValue(QStringLiteral("workbench/expandPercent"),
                        value.workbenchCacheExpandPercent);
    m_settings.setValue(QStringLiteral("workbench/diskBytes"),
                        value.workbenchCacheDiskBytes);
    m_settings.setValue(QStringLiteral("workbench/memoryBytes"),
                        value.workbenchCacheMemoryBytes);

    m_settings.setValue(QStringLiteral("snap/pixels"), value.snapPixels);
    m_settings.setValue(QStringLiteral("snap/canvasCenter"),
                        value.snapCanvasCenter);
    m_settings.setValue(QStringLiteral("snap/entityEdges"),
                        value.snapEntityEdges);
    m_settings.setValue(QStringLiteral("snap/edgeMidpoints"),
                        value.snapEdgeMidpoints);
    m_settings.setValue(QStringLiteral("snap/angles"),
                        snapAnglesToString(value.snapAngles));

    m_settings.setValue(QStringLiteral("mcp/port"), value.mcpPort);
    m_settings.setValue(QStringLiteral("git/humanCommitDelayMs"), value.humanCommitDelayMs);
    m_settings.setValue(QStringLiteral("app/autoSaveMinutes"), value.autoSaveMinutes);
    m_settings.setValue(QStringLiteral("git/enabled"), value.gitEnabled);
    m_settings.setValue(QStringLiteral("app/onboardingDone"), value.onboardingDone);

    m_settings.sync();
}

QString SettingsStore::path() const
{
    return m_settings.fileName();
}

} // namespace pvf::next
