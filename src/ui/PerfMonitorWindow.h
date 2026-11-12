#pragma once

#include "perf/FrameProfiler.h"
#include "perf/SystemStats.h"

#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QHideEvent;
class QCloseEvent;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace pvf {

class FrameSparkline;
class FrameWaterfall;
class FlameGraph;

// Independent performance monitor window.  It never owns the profiler: closing
// the window only releases the capture listener, and the singleton keeps its
// ring buffer for the next open.  The page shows the latest 300 frames, the
// depth of the current stage split, cache counters, host/process values and a
// 5/10/30 second recording report.
class PerfMonitorWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PerfMonitorWindow(QWidget* parent = nullptr);
    ~PerfMonitorWindow() override;

    void setCacheRoot(const QString& root);
    QString cacheRoot() const { return m_cacheRoot; }

    bool isRecording() const;
    QString currentReportText() const;
    // Writes frames + system info + the last recording report, including both
    // call trees.  The toolbar button uses a file dialog; tests and the CLI
    // call this directly.
    bool exportDiagnostics(const QString& path, QString* error = nullptr);
    // Writes only the tree currently shown in the advanced area.
    bool exportCallTree(const QString& path, QString* error = nullptr);
    void refreshNow();

signals:
    void closed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void updateOverview();
    void updateRecordingUi();
    void updateReportText();
    void toggleRecording();
    void copyReport();
    void exportReport();
    void buildAdvancedArea(QVBoxLayout* root);
    void setAdvancedVisible(bool visible);
    void updateAdvanced();
    void updateCallTable(const perf::CallNode& tree);
    perf::CallNode selectedTree() const;

    perf::FrameProfiler& m_profiler;
    std::unique_ptr<perf::SystemStatsProvider> m_systemProvider;
    perf::Snapshot m_snapshot;
    perf::SystemStats m_systemStats;
    QString m_cacheRoot;
    bool m_captureHeld = false;
    bool m_wasRecording = false;

    QLabel* m_summaryLabel = nullptr;
    FrameSparkline* m_sparkline = nullptr;
    QTableWidget* m_stageTable = nullptr;
    QLabel* m_cacheLabel = nullptr;
    QLabel* m_systemLabel = nullptr;
    FrameWaterfall* m_waterfall = nullptr;
    QComboBox* m_durationCombo = nullptr;
    QPushButton* m_recordButton = nullptr;
    QLabel* m_recordStatus = nullptr;
    QPlainTextEdit* m_report = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_advancedButton = nullptr;
    QWidget* m_advancedPanel = nullptr;
    QComboBox* m_treeSource = nullptr;
    QLineEdit* m_treeSearch = nullptr;
    QPushButton* m_treeBackButton = nullptr;
    QPushButton* m_treeExportButton = nullptr;
    QLabel* m_treeStatus = nullptr;
    FlameGraph* m_flame = nullptr;
    QTableWidget* m_callTable = nullptr;
    bool m_advancedOpen = false;
    QString m_lastTreeKey;
    QTimer* m_timer = nullptr;
};

} // namespace pvf
