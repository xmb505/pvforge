#pragma once

#include "next/settings/SettingsStore.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace pvf::next {

// Modal application settings editor.  It works on a snapshot: widgets are
// filled from the store on construction, Cancel/reject never writes, Apply
// writes immediately and OK writes then accepts.
//
// Operations that need the rest of the application (clearing the HTML cache,
// rotating the MCP token, probing for Chrome, reading git history) are emitted
// as signals instead of being performed here, so this module stays independent
// of the renderer, MCP server and git.  The Git page only probes for a git
// binary (`git --version`); repository work stays in the shell.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(SettingsStore* store, QWidget* parent = nullptr);

    AppSettings settings() const;
    void setSettings(const AppSettings& settings);

    // Git facts are supplied by the shell; this dialog never runs git.
    void setGitInfo(const QString& repositoryPath, const QString& recentCommit);
    QString gitRepositoryPath() const;
    QString gitRecentCommit() const;

    // Read-only endpoint path shown on the MCP page.  Defaults to
    // $XDG_CONFIG_HOME/PVForge/mcp-endpoint.json.
    void setEndpointPath(const QString& path);
    QString endpointPath() const;
    static QString defaultEndpointPath();

signals:
    void clearHtmlCacheRequested();
    void resetMcpTokenRequested();
    void chromeDetectRequested();

private:
    void buildHtmlPage();
    void buildVideoPage();
    void buildMcpPage();
    void buildGitPage();
    void refreshGitStatus();
    void saveToStore();

    SettingsStore* m_store = nullptr;

    QTabWidget* m_tabs = nullptr;

    QCheckBox* m_htmlCacheEnabled = nullptr;
    QSpinBox* m_htmlMemoryMb = nullptr;
    QSpinBox* m_htmlDiskGb = nullptr;
    QComboBox* m_htmlScale = nullptr;
    QSpinBox* m_htmlMaxWidth = nullptr;
    QSpinBox* m_watchdogMs = nullptr;
    QLineEdit* m_chromeExecutable = nullptr;
    QPushButton* m_chromeBrowse = nullptr;
    QPushButton* m_chromeDetect = nullptr;
    QPushButton* m_clearHtmlCache = nullptr;

    QCheckBox* m_videoProxyEnabled = nullptr;
    QComboBox* m_videoProxyWidth = nullptr;
    QComboBox* m_videoMemoryFrames = nullptr;

    QSpinBox* m_mcpPort = nullptr;
    QPushButton* m_resetMcpToken = nullptr;
    QLineEdit* m_mcpEndpointPath = nullptr;

    QCheckBox* m_gitEnabled = nullptr;
    QLabel* m_gitStatus = nullptr;
    QPushButton* m_gitDetect = nullptr;
    QLabel* m_gitRepositoryPath = nullptr;
    QLabel* m_gitRecentCommit = nullptr;
    QSpinBox* m_humanCommitDelayMs = nullptr;
    QString m_gitRepositoryValue;
    QString m_gitRecentCommitValue;
    bool m_onboardingDone = false;
};

} // namespace pvf::next
