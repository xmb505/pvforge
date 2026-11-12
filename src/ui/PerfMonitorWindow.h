#pragma once

#include "perf/FrameProfiler.h"
#include "perf/SystemStats.h"

#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QHideEvent;
class QCloseEvent;
class QTableWidget;
class QTimer;

namespace pvf {

class FrameSparkline;
class FrameWaterfall;

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
    // Writes frames + system info + the last recording report.  The toolbar
    // button uses a file dialog; tests and the CLI call this directly.
    bool exportDiagnostics(const QString& path, QString* error = nullptr);
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
    QTimer* m_timer = nullptr;
};

} // namespace pvf
