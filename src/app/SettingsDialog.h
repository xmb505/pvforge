#pragma once

#include "next/settings/SettingsStore.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace pvf {

class McpServer;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr,
                            McpServer* mcpServer = nullptr);
    // AppSettings-backed mode used by the delivery shell.  It keeps the old
    // category list / page stack layout but exposes the new backend settings
    // (HTML, video proxy, workbench cache, snapping, MCP, Git, autosave).
    explicit SettingsDialog(next::SettingsStore* store, QWidget* parent = nullptr);

    bool appSettingsMode() const { return m_appStore != nullptr; }
    next::AppSettings appSettings() const;
    void setAppSettings(const next::AppSettings& settings);

    // Used by Help -> About so both entry points show the same page.
    void showAboutPage();

    // Live endpoint facts supplied by the shell; the dialog never starts or
    // stops the server itself.
    void setMcpStatus(bool running, quint16 port, const QString& token,
                      const QString& error, bool writesEnabled);
    void setGitInfo(const QString& repositoryPath, const QString& recentCommit);
    // Cache occupancy is measured by the shell; the dialog only formats it.
    // `directory` may be empty before the cache has written its first frame.
    void setWorkbenchCacheStats(bool enabled, int entries, qint64 bytes,
                                const QString& directory);
    void setHtmlCacheStats(int files, qint64 bytes, const QString& directory);
    QString gitRepositoryPath() const { return m_appGitRepositoryValue; }
    QString gitRecentCommit() const { return m_appGitRecentValue; }
    void setEndpointPath(const QString& path);
    QString endpointPath() const;
    static QString defaultEndpointPath();

    // Reflects MainWindow's authoritative permission state. The dialog never
    // arms writes itself: it asks MainWindow, which prepares the checkpoint,
    // scene scope and service instance.
    void setMcpPermissionState(bool armed, const QString& sceneName,
                               const QString& error = QString());

signals:
    // Emitted whenever the enable/start/stop controls change the live server
    // state. MainWindow uses it to update the status bar and log header.
    void mcpStateChanged(bool running, quint16 port, const QString& error);
    void mcpPermissionArmRequested();
    void mcpPermissionDisarmRequested();

    // AppSettings mode.
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
    QWidget* createGeneralPage();
    QWidget* createEditorPage();
    QWidget* createProjectPage();
    QWidget* createTimelinePage();
    QWidget* createAssetsPage();
    QWidget* createDecodePage();
    QWidget* createMcpPage();
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

    void applyMcpEnabled(bool enabled);
    void refreshMcpState();
    void copyMcpToken();
    void resetMcpToken();
    void copyMcpConfig();
    void copyMcpStdioConfig();
    QString mcpConfigJson() const;
    QString mcpStdioConfigJson() const;

    QListWidget* m_categories = nullptr;
    QStackedWidget* m_pages = nullptr;
    int m_aboutIndex = 0;
    McpServer* m_mcpServer = nullptr;

    // AppSettings mode.
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
    QCheckBox* m_appHtmlCacheEnabled = nullptr;
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
    QCheckBox* m_appVideoProxyEnabled = nullptr;
    QComboBox* m_appVideoProxyWidth = nullptr;
    QComboBox* m_appVideoMemoryFrames = nullptr;
    QCheckBox* m_appWorkbenchEnabled = nullptr;
    QComboBox* m_appWorkbenchScale = nullptr;
    QSpinBox* m_appWorkbenchExpand = nullptr;
    QSpinBox* m_appWorkbenchDiskGb = nullptr;
    QSpinBox* m_appWorkbenchMemoryMb = nullptr;
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
    QString m_appGitRepositoryValue;
    QString m_appGitRecentValue;
    bool m_appOnboardingDone = false;

    QComboBox* m_language = nullptr;
    QSpinBox* m_snapPixels = nullptr;
    QCheckBox* m_edgeCenterSnap = nullptr;
    QCheckBox* m_edgeMidpointSnap = nullptr;
    QCheckBox* m_angle0 = nullptr;
    QCheckBox* m_angle90 = nullptr;
    QCheckBox* m_angle180 = nullptr;
    QCheckBox* m_angle270 = nullptr;
    QCheckBox* m_autoKey = nullptr;
    QCheckBox* m_audioScrub = nullptr;
    QCheckBox* m_autosave = nullptr;
    QSpinBox* m_autosaveMinutes = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
    QDoubleSpinBox* m_timelineZoom = nullptr;
    QCheckBox* m_packAssets = nullptr;
    QCheckBox* m_mcpEnabled = nullptr;
    QCheckBox* m_mcpAutoPort = nullptr;
    QSpinBox* m_mcpPort = nullptr;
    QLabel* m_mcpStatus = nullptr;
    QLabel* m_mcpError = nullptr;
    QLineEdit* m_mcpToken = nullptr;
    QPushButton* m_mcpStart = nullptr;
    QPushButton* m_mcpStop = nullptr;
    QComboBox* m_mcpPermission = nullptr;
    QLabel* m_mcpPermissionStatus = nullptr;
    QCheckBox* m_mcpLogEnabled = nullptr;
    QPushButton* m_mcpCopyToken = nullptr;
    QPushButton* m_mcpResetToken = nullptr;
    QPushButton* m_mcpCopyConfig = nullptr;
    QPushButton* m_mcpCopyStdioConfig = nullptr;
    QPlainTextEdit* m_aboutText = nullptr;
};

} // namespace pvf
