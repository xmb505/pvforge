#pragma once

#include "core/Project.h"

#include <QWidget>

class QCloseEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pvf {

// In-memory MCP invocation log window. It follows the camera monitor's
// lightweight docking model: a plain child of the viewport that can be
// dragged out into its own top-level window, resized and closed. Rows are
// capped so a long agent session cannot grow the log without bound.
class McpLogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit McpLogPanel(QWidget* viewport, QWidget* parent = nullptr);

    void setServerState(bool running, quint16 port, const QString& error = QString());
    void setLoggingEnabled(bool on);
    bool loggingEnabled() const { return m_loggingEnabled; }

    // Permission state shown in the header: read-only, armed for one scene,
    // or stopped by the human. It is independent from the HTTP server state:
    // the server keeps accepting read tools after an emergency stop.
    void setPermissionState(bool armed, const QString& sceneName,
                            bool stopped = false);
    // Adds a non-tool row (arm/disarm/stop/notification) to the log.
    void addSystemEvent(const QString& title, const QString& detail = QString());

    void addToolInvocation(const QString& name, const QString& argsJson,
                           const QString& resultJson, qint64 durationMs, bool ok);
    void showFromMenu();

    bool isDetached() const { return m_detached; }
    int entryCountForTest() const;
    QString entryTargetForTest(int row) const;
    QString entryDetailForTest(int row) const;
    void activateEntryForTest(int row);

signals:
    void entityActivated(EntityId id);
    void frameRequested(int frame);
    void stopAiRequested();
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QRect titleRect() const;
    QRect headerRect() const;
    QRect gripRect() const;
    void layoutChildren();
    void updateHeader();
    void updateButtons();
    void activateItem(QTreeWidgetItem* item);
    void detachWindow();
    void dockWindow();
    void clampDockGeometry();
    QRect clampedDockGeometry(const QRect& geometry) const;
    void saveDockGeometry();

    QWidget* m_viewport = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QToolButton* m_detachButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QToolButton* m_stopButton = nullptr;
    QToolButton* m_clearButton = nullptr;

    bool m_detached = false;
    bool m_initialized = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_loggingEnabled = true;
    bool m_autoShowPending = false;
    bool m_serverRunning = false;
    bool m_armed = false;
    bool m_stopped = false;
    quint16 m_serverPort = 0;
    QString m_serverError;
    QString m_sceneName;
    int m_totalCalls = 0;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;

    static constexpr int kTitleHeight = 28;
    static constexpr int kHeaderHeight = 30;
    static constexpr int kGripSize = 14;
    static constexpr int kMaxRows = 500;
    static constexpr int kMaxDetailChars = 1500;
};

} // namespace pvf
