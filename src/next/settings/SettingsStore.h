#pragma once

#include <QSettings>
#include <QString>

namespace pvf::next {

// Persisted application-wide preferences for the new UI.  The defaults are the
// values documented in docs/ENTITY_MODEL.md: HTML cache 256 MB memory / 2 GB
// disk, low-resolution preview at half the project resolution, a 3 s watchdog,
// 960 px video proxies and the fixed MCP port 47821.
struct AppSettings
{
    // HTML
    bool htmlCacheEnabled = true;
    qint64 htmlMemoryBytes = 256LL * 1024 * 1024;
    qint64 htmlDiskBytes = 2LL * 1024 * 1024 * 1024;
    double htmlScale = 0.5; // project-resolution factor: 0.25/0.5/0.75/1.0
    int htmlMaxWidth = 1280;
    int watchdogMs = 3000;
    QString chromeExecutable; // empty = auto-detect

    // Video
    bool videoProxyEnabled = true;
    int videoProxyWidth = 960;
    int videoMemoryFrames = 64;

    // MCP/Git
    quint16 mcpPort = 47821;
    int humanCommitDelayMs = 1000;
    // Git is an optional prerequisite and stays off until the user enables it,
    // either in the first-run onboarding or in the settings dialog.
    bool gitEnabled = false;
    bool onboardingDone = false;

    bool operator==(const AppSettings&) const = default;
};

// QSettings-backed store with stable keys.  load() clamps every numeric value
// into its supported range and snaps choice values (HTML scale, proxy width,
// memory frames) to their allowed options, so a hand-edited INI never produces
// an unusable setting.
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
