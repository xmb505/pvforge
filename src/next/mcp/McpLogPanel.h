#pragma once

#include <QWidget>

class QCloseEvent;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;

namespace pvf::next {

// Bottom-dock style MCP activity log for the new entity-model UI.
//
// The panel only presents state; it never touches the MCP server.  The
// application forwards HttpServer::toolInvoked to addToolInvocation(), calls
// setServerState()/setAiState() when the transport or AI guard changes, and
// performs the actual emergency stop when stopAiRequested() arrives.
//
// At most kMaxRows top-level entries are kept.  Each entry has a collapsed
// child row with the full argument/result text, truncated to kMaxDetailChars.
class McpLogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit McpLogPanel(QWidget* parent = nullptr);

    // running=false with a non-empty error shows "MCP 未启动 · <error>".
    void setServerState(bool running, quint16 port, const QString& error = {});
    // stopped wins over busy; the stop/resume buttons follow this state.
    void setAiState(bool stopped, bool busy);
    // Controls whether the panel is allowed to stay visible.  Disabling hides
    // it (emitting visibilityChanged(false)); enabling does not auto-show it.
    void setLoggingEnabled(bool on);
    bool loggingEnabled() const { return m_loggingEnabled; }

    void addSystemEvent(const QString& title, const QString& detail = {});
    void addToolInvocation(const QString& name, const QString& argsJson,
                           const QString& resultJson, qint64 ms, bool ok);
    void showFromMenu();

    // Also invoked by the 清空 button.
    void clearLog();

#ifdef PVFORGE_TEST_HOOKS
    // Test hooks: index is the top-level row.  entryTargetForTest returns the
    // "参数" summary column, entryDetailForTest the collapsed full-detail row.
    int entryCountForTest() const;
    QString entryTargetForTest(int row) const;
    QString entryDetailForTest(int row) const;
    void activateEntryForTest(int row);
#endif

    QSize sizeHint() const override;

signals:
    void stopAiRequested();
    void resumeAiRequested();
    void visibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QTreeWidgetItem* appendRow(const QString& tool, const QString& target,
                               const QString& duration, const QString& result,
                               const QString& detail, bool ok, bool system);
    void trimRows();
    void updateHeader();
    void updateStateLabels();
    void updateButtons();

    QLabel* m_countLabel = nullptr;
    QLabel* m_serverLabel = nullptr;
    QLabel* m_aiLabel = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_resumeButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_closeButton = nullptr;

    bool m_loggingEnabled = true;
    bool m_serverRunning = false;
    bool m_aiStopped = false;
    bool m_aiBusy = false;
    quint16 m_serverPort = 0;
    QString m_serverError;
    int m_toolCalls = 0;

    static constexpr int kMaxRows = 500;
    static constexpr int kMaxDetailChars = 1500;
    static constexpr int kMaxSummaryChars = 80;
};

} // namespace pvf::next
