#pragma once

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

    // Used by Help -> About so both entry points show the same page.
    void showAboutPage();

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
