#pragma once

#include <QSettings>
#include <QString>
#include <QVector>

namespace pvf::next {

// Persisted application-wide preferences for the new UI.  The defaults are the
// values documented in docs/AI/ENTITY_MODEL.md: HTML cache 256 MB memory / 2 GB
// disk, low-resolution preview at half the project resolution, a 3 s watchdog,
// 960 px video proxies and the fixed MCP port 47821.
struct AppSettings
{
    // HTML
    bool htmlCacheEnabled = true;
    qint64 htmlMemoryBytes = 256LL * 1024 * 1024;
    qint64 htmlDiskBytes = 1LL * 1024 * 1024 * 1024;
    double htmlScale = 0.5; // project-resolution factor: 0.25/0.5/0.75/1.0
    int htmlMaxWidth = 1280;
    int watchdogMs = 3000;
    QString chromeExecutable; // empty = auto-detect

    // Video
    bool videoProxyEnabled = true;
    int videoProxyWidth = 960;
    int videoMemoryFrames = 64;

    // Workbench cache: whole-frame prerendered scene, hit during playback.
    // Scale is snapped to 25/50/75/100 percent.  The canvas view caches a
    // world rectangle expanded by this percentage on every side; camera views
    // and exports never expand.
    bool workbenchCacheEnabled = true;
    int workbenchCacheScalePercent = 50;
    int workbenchCacheExpandPercent = 50;
    qint64 workbenchCacheDiskBytes = 8LL * 1024 * 1024 * 1024;
    qint64 workbenchCacheMemoryBytes = 256LL * 1024 * 1024;
    // Background threads that encode playback raster misses (1..3).  Two
    // roughly cover a 30 fps 1280x720 miss stream; one falls behind.
    int workbenchCacheWriterThreads = 2;

    // Viewport snapping.  snapPixels is the screen-pixel threshold for canvas
    // centre lines and entity edges (1..32); the pixel grid step stays at one
    // project pixel.  Angles are absolute degrees; Ctrl disables snapping for
    // the current drag.
    int snapPixels = 6;
    bool snapCanvasCenter = true;
    bool snapEntityEdges = true;
    bool snapEdgeMidpoints = false;
    QVector<double> snapAngles{0.0, 90.0, 180.0, 270.0};

    // MCP/Git
    quint16 mcpPort = 47821;
    int humanCommitDelayMs = 1000;
    // Wall-clock interval for the shell's autosave timer.  The timer only
    // flushes a debounced human edit that is still pending, so an idle project
    // never produces empty commits.
    int autoSaveMinutes = 5;
    // Git is an optional prerequisite and stays off until the user enables it,
    // either in the first-run onboarding or in the settings dialog.
    bool gitEnabled = false;
    bool onboardingDone = false;

    bool operator==(const AppSettings&) const = default;
};

// QSettings-backed store with stable keys.  load() clamps every numeric value
// into its supported range and snaps choice values (HTML scale, workbench
// cache scale, proxy width, memory frames) to their allowed options, so a
// hand-edited INI never produces an unusable setting.
class SettingsStore
{
public:
    // An explicit INI path is used by tests; production uses the default
    // QSettings location for the PVForge organisation/application.
    explicit SettingsStore(QString iniPath = {});

    AppSettings load() const;
    void save(const AppSettings& settings);
    QString path() const;

    // Clamps/snaps `settings` to the ranges the settings UI exposes.
    static AppSettings sanitized(AppSettings settings);

private:
    QSettings m_settings;
};

} // namespace pvf::next
