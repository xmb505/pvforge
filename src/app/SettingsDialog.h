#pragma once

#include "framecache/CacheUsage.h"
#include "next/settings/SettingsStore.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace pvf {

// Category-list settings dialog backed by next::SettingsStore.  The shell
// supplies the live endpoint/Git/cache facts; the dialog never starts or stops
// the MCP server or touches the cache itself.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(next::SettingsStore* store, QWidget* parent = nullptr);

    next::AppSettings appSettings() const;
    void setAppSettings(const next::AppSettings& settings);

    // Used by Help -> About so both entry points show the same page.
    void showAboutPage();

    void setMcpStatus(bool running, quint16 port, const QString& token,
                      const QString& error, bool writesEnabled);
    void setGitInfo(const QString& repositoryPath, const QString& recentCommit);
    // Cache occupancy is measured by the shell with framecache::scanDirectory;
    // the dialog only formats it.  `directory` may be empty before the cache
    // has written its first frame.
    void setWorkbenchCacheStats(bool enabled,
                                const framecache::CacheUsage& usage,
                                const QString& directory);
    void setHtmlCacheStats(const framecache::CacheUsage& usage,
                           const QString& directory);
    void setEndpointPath(const QString& path);
    static QString defaultEndpointPath();

signals:
    void settingsApplied();
    void resetMcpTokenRequested();
    void clearHtmlCacheRequested();
    void clearWorkbenchCacheRequested();
    void openCacheFolderRequested();
    void openHtmlCacheFolderRequested();
    void rebuildSolveCacheRequested();
    void chromeDetectRequested();

protected:
    void accept() override;

private:
    QWidget* createAboutPage();
    QWidget* createAppGeneralPage();
    QWidget* createAppEditorPage();
    QWidget* createAppCachePage();
    QWidget* createAppHtmlPage();
    QWidget* createAppMcpPage();
    QWidget* createAppGitPage();
    void buildAppShell();
    void saveAppSettings();
    void refreshAppGitStatus();

    int addPage(const QString& title, QWidget* page, const QStringList& iconNames);

    QListWidget* m_categories = nullptr;
    QStackedWidget* m_pages = nullptr;
    int m_aboutIndex = 0;

    next::SettingsStore* m_appStore = nullptr;
    QSpinBox* m_appAutoSaveMinutes = nullptr;
    QSpinBox* m_appCommitDelayMs = nullptr;
    QSpinBox* m_appSnapPixels = nullptr;
    QCheckBox* m_appSnapCanvasCenter = nullptr;
    QCheckBox* m_appSnapEntityEdges = nullptr;
    QCheckBox* m_appSnapEdgeMidpoints = nullptr;
    QCheckBox* m_appSnapAngle0 = nullptr;
    QCheckBox* m_appSnapAngle30 = nullptr;
    QCheckBox* m_appSnapAngle45 = nullptr;
    QCheckBox* m_appSnapAngle90 = nullptr;
    QSpinBox* m_appHtmlMemoryMb = nullptr;
    QSpinBox* m_appHtmlDiskGb = nullptr;
    QComboBox* m_appHtmlScale = nullptr;
    QSpinBox* m_appHtmlMaxWidth = nullptr;
    QSpinBox* m_appWatchdogMs = nullptr;
    QLineEdit* m_appChromeExecutable = nullptr;
    QPushButton* m_appChromeBrowse = nullptr;
    QPushButton* m_appChromeDetect = nullptr;
    QPushButton* m_appClearHtmlCache = nullptr;
    QLabel* m_appHtmlCacheStats = nullptr;
    QPushButton* m_appOpenHtmlCacheFolder = nullptr;
    QComboBox* m_appVideoProxyWidth = nullptr;
    QComboBox* m_appVideoMemoryFrames = nullptr;
    QCheckBox* m_appWorkbenchEnabled = nullptr;
    QComboBox* m_appWorkbenchScale = nullptr;
    QSpinBox* m_appWorkbenchExpand = nullptr;
    QSpinBox* m_appWorkbenchDiskGb = nullptr;
    QSpinBox* m_appWorkbenchMemoryMb = nullptr;
    QComboBox* m_appWorkbenchWriterThreads = nullptr;
    QLabel* m_appWorkbenchCacheStats = nullptr;
    QPushButton* m_appClearWorkbenchCache = nullptr;
    QPushButton* m_appOpenCacheFolder = nullptr;
    QPushButton* m_appRebuildSolveCache = nullptr;
    QSpinBox* m_appMcpPort = nullptr;
    QLineEdit* m_appMcpToken = nullptr;
    QLineEdit* m_appMcpEndpoint = nullptr;
    QPushButton* m_appResetMcpToken = nullptr;
    QLabel* m_appMcpStatus = nullptr;
    QCheckBox* m_appGitEnabled = nullptr;
    QLabel* m_appGitStatus = nullptr;
    QPushButton* m_appGitDetect = nullptr;
    QLabel* m_appGitRepository = nullptr;
    QLabel* m_appGitRecent = nullptr;
    bool m_appOnboardingDone = false;

    QPlainTextEdit* m_aboutText = nullptr;
};

} // namespace pvf
