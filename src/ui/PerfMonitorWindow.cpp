#include "ui/PerfMonitorWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QShowEvent>
#include <QSysInfo>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pvf {
namespace {

QColor categoryColor(perf::Category category)
{
    switch (category)
    {
    case perf::Category::Render2:
        return QColor(61, 174, 233);
    case perf::Category::Meta:
        return QColor(150, 140, 220);
    case perf::Category::Lookup:
        return QColor(90, 180, 160);
    case perf::Category::Decode:
        return QColor(230, 160, 60);
    case perf::Category::Encode:
        return QColor(225, 100, 70);
    case perf::Category::Disk:
        return QColor(180, 140, 100);
    case perf::Category::Sqlite:
        return QColor(140, 120, 180);
    case perf::Category::Solver:
        return QColor(240, 200, 80);
    case perf::Category::Html:
        return QColor(230, 120, 180);
    case perf::Category::Video:
        return QColor(100, 200, 240);
    case perf::Category::Paint:
        return QColor(120, 200, 120);
    case perf::Category::Ui:
        return QColor(160, 160, 165);
    default:
        return QColor(110, 110, 115);
    }
}

QString formatBytes(qint64 bytes)
{
    const double value = double(std::max<qint64>(0, bytes));
    if (value >= 1024.0 * 1024.0 * 1024.0)
        return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2)
               + QStringLiteral(" GB");
    if (value >= 1024.0 * 1024.0)
        return QString::number(value / (1024.0 * 1024.0), 'f', 1)
               + QStringLiteral(" MB");
    if (value >= 1024.0)
        return QString::number(value / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(value, 'f', 0) + QStringLiteral(" B");
}

QString msText(double ms)
{
    return ms > 0.0 ? QString::number(ms, 'f', 1) + QStringLiteral(" ms")
                    : QStringLiteral("—");
}

QJsonObject systemStatsToJson(const perf::SystemStats& stats)
{
    QJsonObject object;
    object.insert(QStringLiteral("supported"), stats.supported);
    object.insert(QStringLiteral("note"), stats.note);
    object.insert(QStringLiteral("processCpuPercent"), stats.processCpuPercent);
    object.insert(QStringLiteral("rssBytes"), QJsonValue(stats.rssBytes));
    object.insert(QStringLiteral("ioReadBytes"), QJsonValue(stats.ioReadBytes));
    object.insert(QStringLiteral("ioWriteBytes"), QJsonValue(stats.ioWriteBytes));
    object.insert(QStringLiteral("threadCount"), stats.threadCount);
    object.insert(QStringLiteral("load1"), stats.load1);
    object.insert(QStringLiteral("load5"), stats.load5);
    object.insert(QStringLiteral("load15"), stats.load15);
    object.insert(QStringLiteral("cacheDiskBytes"),
                  QJsonValue(stats.cacheDiskBytes));
    object.insert(QStringLiteral("cacheFileCount"), stats.cacheFileCount);
    return object;
}

} // namespace

class FrameSparkline : public QWidget
{
public:
    explicit FrameSparkline(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("perfSparkline"));
        setMinimumHeight(128);
    }

    void setFrames(const QVector<perf::FrameSample>& frames)
    {
        m_frames = frames;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(16, 18, 20));

        const QRectF plot = QRectF(rect()).adjusted(10.0, 22.0, -10.0, -16.0);
        if (plot.width() <= 0.0 || plot.height() <= 0.0)
            return;

        double maxMs = 33.4;
        for (const perf::FrameSample& sample : m_frames)
            maxMs = std::max(maxMs, sample.totalMs);
        maxMs = std::max(1.0, maxMs * 1.1);

        painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        painter.setPen(QColor(120, 124, 128));
        painter.drawText(QRectF(10.0, 2.0, plot.width(), 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("帧耗时 · 最近 %1 帧")
                             .arg(m_frames.size()));

        const std::array<double, 3> grid = {16.7, 33.4, maxMs};
        for (double value : grid)
        {
            if (value > maxMs)
                continue;
            const double y = plot.bottom() - value / maxMs * plot.height();
            painter.setPen(QPen(QColor(52, 56, 60), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(QColor(110, 114, 118));
            painter.drawText(QRectF(plot.right() - 54.0, y - 14.0, 54.0, 12.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(value, 'f', 0)
                                 + QStringLiteral(" ms"));
        }

        const int count = m_frames.size();
        if (count == 0)
        {
            painter.setPen(QColor(150, 154, 158));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待预览帧…"));
            return;
        }

        const double step =
            std::max(1.0, plot.width() / double(perf::kRingCapacity));
        const double barWidth = std::max(1.0, step - 1.0);
        for (int i = 0; i < count; ++i)
        {
            const perf::FrameSample& sample =
                m_frames.at(m_frames.size() - count + i);
            const double height =
                std::clamp(sample.totalMs / maxMs, 0.0, 1.0) * plot.height();
            const double x = plot.right() - (count - i) * step;
            QColor color(90, 220, 140);
            if (sample.totalMs > 33.4)
                color = QColor(235, 90, 80);
            else if (sample.totalMs > 16.7)
                color = QColor(230, 200, 80);
            painter.fillRect(QRectF(x, plot.bottom() - height, barWidth, height),
                             color);
        }

        painter.setPen(QPen(QColor(70, 74, 78), 1.0));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    }

private:
    QVector<perf::FrameSample> m_frames;
};

class FrameWaterfall : public QWidget
{
public:
    explicit FrameWaterfall(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("perfWaterfall"));
        setMinimumHeight(58);
    }

    void setSample(const perf::FrameSample& sample, bool valid)
    {
        m_sample = sample;
        m_valid = valid;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(16, 18, 20));
        painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        if (!m_valid)
        {
            painter.setPen(QColor(150, 154, 158));
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("最近一帧：暂无样本"));
            return;
        }

        double total = 0.0;
        for (int i = 1; i < perf::kCategoryCount; ++i)
            total += m_sample.categoryMs[size_t(i)];
        if (total <= 0.0)
            total = std::max(0.001, m_sample.totalMs);

        const QRectF bar = QRectF(rect()).adjusted(10.0, 8.0, -10.0, -22.0);
        double x = bar.left();
        for (int i = 1; i < perf::kCategoryCount; ++i)
        {
            const double value = m_sample.categoryMs[size_t(i)];
            if (value <= 0.0)
                continue;
            const double width = std::max(1.0, value / total * bar.width());
            const QRectF part(x, bar.top(), width, bar.height());
            const QColor color = categoryColor(perf::Category(i));
            painter.fillRect(part, color);
            painter.setPen(QPen(QColor(16, 18, 20), 1.0));
            painter.drawRect(part);
            if (width >= 46.0)
            {
                painter.setPen(QColor(20, 20, 22));
                painter.drawText(part.adjusted(3.0, 0.0, -3.0, 0.0),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 perf::categoryLabel(perf::Category(i)));
            }
            x += width;
        }

        painter.setPen(QColor(150, 154, 158));
        painter.drawText(QRectF(bar.left(), bar.bottom() + 2.0, bar.width(), 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("第 %1 帧 · %2")
                             .arg(m_sample.index)
                             .arg(msText(m_sample.totalMs)));
    }

private:
    perf::FrameSample m_sample;
    bool m_valid = false;
};

PerfMonitorWindow::PerfMonitorWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_profiler(perf::FrameProfiler::instance())
    , m_systemProvider(perf::SystemStatsProvider::create())
{
    setObjectName(QStringLiteral("perfMonitorWindow"));
    setWindowTitle(QStringLiteral("性能监视"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(960, 820);
    setMinimumSize(760, 600);

    buildUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &PerfMonitorWindow::refreshNow);

    refreshNow();
}

PerfMonitorWindow::~PerfMonitorWindow()
{
    if (m_captureHeld)
        m_profiler.setCaptureEnabled(false);
    if (m_profiler.isRecording())
        m_profiler.stopRecording();
}

void PerfMonitorWindow::buildUi()
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("perfSummaryLabel"));
    m_summaryLabel->setFont(mono);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setToolTip(
        QStringLiteral("掉帧 = 帧耗时超过 33.4 ms（低于 30 fps）；"
                       "样本为最近 300 帧。"));
    root->addWidget(m_summaryLabel);

    m_sparkline = new FrameSparkline(this);
    root->addWidget(m_sparkline);

    auto* middle = new QHBoxLayout();
    middle->setSpacing(6);

    auto* stageBox = new QGroupBox(QStringLiteral("阶段占比（含子阶段）"), this);
    stageBox->setToolTip(
        QStringLiteral("父阶段包含子阶段，百分比之和可能超过 100%。"));
    auto* stageLayout = new QVBoxLayout(stageBox);
    stageLayout->setContentsMargins(6, 6, 6, 6);
    m_stageTable = new QTableWidget(0, 3, stageBox);
    m_stageTable->setObjectName(QStringLiteral("perfStageTable"));
    m_stageTable->setHorizontalHeaderLabels(
        {QStringLiteral("阶段"), QStringLiteral("平均"), QStringLiteral("占比")});
    m_stageTable->verticalHeader()->hide();
    m_stageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stageTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_stageTable->setShowGrid(false);
    m_stageTable->setFont(mono);
    m_stageTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_stageTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_stageTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_stageTable->setMinimumHeight(150);
    stageLayout->addWidget(m_stageTable);
    middle->addWidget(stageBox, 3);

    auto* side = new QVBoxLayout();
    side->setSpacing(6);

    auto* cacheBox = new QGroupBox(QStringLiteral("缓存"), this);
    auto* cacheLayout = new QVBoxLayout(cacheBox);
    cacheLayout->setContentsMargins(6, 6, 6, 6);
    m_cacheLabel = new QLabel(cacheBox);
    m_cacheLabel->setObjectName(QStringLiteral("perfCacheLabel"));
    m_cacheLabel->setFont(mono);
    m_cacheLabel->setWordWrap(true);
    m_cacheLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cacheLayout->addWidget(m_cacheLabel);
    side->addWidget(cacheBox);

    auto* systemBox = new QGroupBox(QStringLiteral("系统"), this);
    auto* systemLayout = new QVBoxLayout(systemBox);
    systemLayout->setContentsMargins(6, 6, 6, 6);
    m_systemLabel = new QLabel(systemBox);
    m_systemLabel->setObjectName(QStringLiteral("perfSystemLabel"));
    m_systemLabel->setFont(mono);
    m_systemLabel->setWordWrap(true);
    m_systemLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    systemLayout->addWidget(m_systemLabel);
    side->addWidget(systemBox);
    side->addStretch(1);
    middle->addLayout(side, 2);

    root->addLayout(middle);

    auto* waterfallBox = new QGroupBox(QStringLiteral("最近一帧阶段瀑布"), this);
    auto* waterfallLayout = new QVBoxLayout(waterfallBox);
    waterfallLayout->setContentsMargins(6, 6, 6, 6);
    m_waterfall = new FrameWaterfall(waterfallBox);
    waterfallLayout->addWidget(m_waterfall);
    root->addWidget(waterfallBox);

    auto* recordBox = new QGroupBox(QStringLiteral("录制"), this);
    auto* recordLayout = new QVBoxLayout(recordBox);
    recordLayout->setContentsMargins(6, 6, 6, 6);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    auto* durationLabel = new QLabel(QStringLiteral("时长"), recordBox);
    controls->addWidget(durationLabel);
    m_durationCombo = new QComboBox(recordBox);
    m_durationCombo->setObjectName(QStringLiteral("perfRecordDuration"));
    m_durationCombo->addItem(QStringLiteral("5 秒"), 5);
    m_durationCombo->addItem(QStringLiteral("10 秒"), 10);
    m_durationCombo->addItem(QStringLiteral("30 秒"), 30);
    m_durationCombo->setCurrentIndex(1);
    controls->addWidget(m_durationCombo);

    m_recordButton = new QPushButton(QStringLiteral("开始录制"), recordBox);
    m_recordButton->setObjectName(QStringLiteral("perfRecordButton"));
    connect(m_recordButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::toggleRecording);
    controls->addWidget(m_recordButton);

    m_recordStatus = new QLabel(QStringLiteral("选择时长后开始录制"), recordBox);
    m_recordStatus->setObjectName(QStringLiteral("perfRecordStatus"));
    m_recordStatus->setFont(mono);
    controls->addWidget(m_recordStatus, 1);
    recordLayout->addLayout(controls);

    m_report = new QPlainTextEdit(recordBox);
    m_report->setObjectName(QStringLiteral("perfReport"));
    m_report->setReadOnly(true);
    m_report->setFont(mono);
    m_report->setPlaceholderText(
        QStringLiteral("录制结束后这里会显示人话报告；也可以导出 JSON 诊断包。"));
    m_report->setMinimumHeight(180);
    recordLayout->addWidget(m_report);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    m_copyButton = new QPushButton(QStringLiteral("复制报告"), recordBox);
    m_copyButton->setObjectName(QStringLiteral("perfCopyButton"));
    connect(m_copyButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::copyReport);
    actions->addWidget(m_copyButton);
    m_exportButton = new QPushButton(QStringLiteral("导出诊断包"), recordBox);
    m_exportButton->setObjectName(QStringLiteral("perfExportButton"));
    connect(m_exportButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::exportReport);
    actions->addWidget(m_exportButton);
    recordLayout->addLayout(actions);

    root->addWidget(recordBox, 1);

    setStyleSheet(QStringLiteral(
        "QWidget { border-radius: 0px; }"
        "QGroupBox { border: 1px solid #6b6b73; margin-top: 9px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px;"
        " padding: 0px 3px; }"
        "QTableWidget { border: 1px solid #6b6b73;"
        " gridline-color: #505056; }"
        "QHeaderView::section { border: 0px;"
        " border-right: 1px solid #505056;"
        " border-bottom: 1px solid #505056; padding: 3px; }"
        "QPushButton, QComboBox { border: 1px solid #6b6b73;"
        " border-radius: 0px; padding: 3px 10px; }"
        "QPlainTextEdit { border: 1px solid #6b6b73;"
        " border-radius: 0px; }"));
}

void PerfMonitorWindow::setCacheRoot(const QString& root)
{
    if (m_cacheRoot == root)
        return;
    m_cacheRoot = root;
    refreshNow();
}

void PerfMonitorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_captureHeld)
    {
        m_profiler.setCaptureEnabled(true);
        m_captureHeld = true;
    }
    m_timer->start();
    refreshNow();
    updateReportText();
    raise();
    activateWindow();
}

void PerfMonitorWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    if (m_captureHeld)
    {
        m_profiler.setCaptureEnabled(false);
        m_captureHeld = false;
    }
    if (m_profiler.isRecording())
        m_profiler.stopRecording();
    QWidget::hideEvent(event);
}

void PerfMonitorWindow::closeEvent(QCloseEvent* event)
{
    emit closed();
    QWidget::closeEvent(event);
}

bool PerfMonitorWindow::isRecording() const
{
    return m_profiler.isRecording();
}

QString PerfMonitorWindow::currentReportText() const
{
    return m_report ? m_report->toPlainText() : QString();
}

void PerfMonitorWindow::refreshNow()
{
    m_snapshot = m_profiler.snapshot();
    if (m_systemProvider)
        m_systemStats = m_systemProvider->sample(m_cacheRoot);

    updateOverview();

    const bool recording = m_profiler.isRecording();
    if (m_wasRecording && !recording)
        updateReportText();
    m_wasRecording = recording;
    updateRecordingUi();
}

void PerfMonitorWindow::updateOverview()
{
    const perf::Summary& summary = m_snapshot.summary;
    const int sampleCount = summary.count;

    m_summaryLabel->setText(
        QStringLiteral(
            "FPS %1    p50 %2    p95 %3    p99 %4    掉帧 %5    样本 %6/%7")
            .arg(QString::number(summary.fps, 'f', 1), msText(summary.p50Ms),
                 msText(summary.p95Ms), msText(summary.p99Ms))
            .arg(summary.droppedFrames)
            .arg(sampleCount)
            .arg(perf::kRingCapacity));

    m_sparkline->setFrames(m_snapshot.frames);

    // Stage table: skip the whole-frame pseudo category, keep only measured
    // stages and sort by total time.
    struct Row
    {
        perf::Category category;
        double totalMs;
        double share;
    };
    QVector<Row> rows;
    for (int i = 1; i < perf::kCategoryCount; ++i)
    {
        const double total = summary.categoryTotalMs[size_t(i)];
        if (total <= 0.0)
            continue;
        rows.append(Row{perf::Category(i), total, summary.categoryShare[size_t(i)]});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.totalMs > b.totalMs;
    });

    m_stageTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
    {
        const Row& stage = rows.at(row);
        auto* name = new QTableWidgetItem(perf::categoryLabel(stage.category));
        auto* average = new QTableWidgetItem(
            sampleCount > 0 ? msText(stage.totalMs / sampleCount)
                            : QStringLiteral("—"));
        auto* share = new QTableWidgetItem(QString::number(stage.share * 100.0,
                                                           'f', 1)
                                           + QStringLiteral("%"));
        average->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        share->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_stageTable->setItem(row, 0, name);
        m_stageTable->setItem(row, 1, average);
        m_stageTable->setItem(row, 2, share);
    }

    const perf::CacheTotals& cache = m_snapshot.cache;
    const quint64 lookups = cache.hits + cache.misses;
    const double hitRate =
        lookups > 0 ? double(cache.hits) / double(lookups) : 0.0;
    m_cacheLabel->setText(
        QStringLiteral("命中 %1 / 未命中 %2\n命中率 %3\n"
                       "QOI 编码 %4 帧 · WebP 编码 %5 帧\n"
                       "存储队列 %6 · 累计写入 %7")
            .arg(cache.hits)
            .arg(cache.misses)
            .arg(lookups > 0 ? QString::number(hitRate * 100.0, 'f', 1)
                                  + QStringLiteral("%")
                             : QStringLiteral("—"))
            .arg(cache.qoiStores)
            .arg(cache.webpStores)
            .arg(cache.pendingStores)
            .arg(cache.stores));

    if (m_systemStats.supported)
    {
        m_systemLabel->setText(
            QStringLiteral("进程 CPU %1% · 线程 %2\n"
                           "RSS %3\n"
                           "磁盘读 %4 / 写 %5\n"
                           "系统负载 %6 / %7 / %8\n"
                           "缓存占用 %9（%10 帧文件）")
                .arg(QString::number(m_systemStats.processCpuPercent, 'f', 1))
                .arg(m_systemStats.threadCount)
                .arg(formatBytes(m_systemStats.rssBytes),
                     formatBytes(m_systemStats.ioReadBytes),
                     formatBytes(m_systemStats.ioWriteBytes))
                .arg(QString::number(m_systemStats.load1, 'f', 2),
                     QString::number(m_systemStats.load5, 'f', 2),
                     QString::number(m_systemStats.load15, 'f', 2),
                     formatBytes(m_systemStats.cacheDiskBytes))
                .arg(m_systemStats.cacheFileCount));
    }
    else
    {
        m_systemLabel->setText(
            m_systemStats.note.isEmpty()
                ? QStringLiteral("当前平台暂不支持系统参数")
                : m_systemStats.note);
    }

    if (!m_snapshot.frames.isEmpty())
        m_waterfall->setSample(m_snapshot.frames.last(), true);
    else
        m_waterfall->setSample(perf::FrameSample{}, false);
}

void PerfMonitorWindow::updateRecordingUi()
{
    const bool recording = m_profiler.isRecording();
    m_durationCombo->setEnabled(!recording);
    m_recordButton->setText(recording ? QStringLiteral("停止录制")
                                      : QStringLiteral("开始录制"));

    if (recording)
    {
        m_recordStatus->setText(
            QStringLiteral("录制中 %1 / %2 秒")
                .arg(QString::number(m_profiler.recordingElapsedSeconds(), 'f',
                                     1))
                .arg(m_profiler.recordingRemainingSeconds()
                             + m_profiler.recordingElapsedSeconds(),
                     'f', 1));
        return;
    }

    const perf::RecordingReport report = m_profiler.recordedReport();
    if (report.valid)
    {
        m_recordStatus->setText(
            QStringLiteral("录制完成：%1 帧 · %2 秒")
                .arg(report.frameCount)
                .arg(QString::number(report.elapsedSeconds, 'f', 1)));
    }
    else
    {
        m_recordStatus->setText(QStringLiteral("选择时长后开始录制"));
    }
}

void PerfMonitorWindow::updateReportText()
{
    const perf::RecordingReport report = m_profiler.recordedReport();
    if (!report.valid)
        return;
    m_report->setPlainText(report.text);
}

void PerfMonitorWindow::toggleRecording()
{
    if (m_profiler.isRecording())
    {
        m_profiler.stopRecording();
        updateReportText();
        m_wasRecording = false;
        updateRecordingUi();
        refreshNow();
        return;
    }

    const int seconds = m_durationCombo->currentData().toInt();
    if (!m_profiler.startRecording(seconds))
        return;
    m_report->clear();
    m_wasRecording = true;
    updateRecordingUi();
    refreshNow();
}

void PerfMonitorWindow::copyReport()
{
    QString text = m_report->toPlainText();
    if (text.isEmpty())
        text = m_profiler.recordedReportText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
}

bool PerfMonitorWindow::exportDiagnostics(const QString& path, QString* error)
{
    QJsonObject root = m_profiler.diagnosticsJson();
    root.insert(QStringLiteral("system"), systemStatsToJson(m_systemStats));
    root.insert(QStringLiteral("platform"), QSysInfo::productType());
    root.insert(QStringLiteral("generatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

void PerfMonitorWindow::exportReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出诊断包"),
        QStringLiteral("pvforge-perf.json"),
        QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!exportDiagnostics(path, &error) && !error.isEmpty())
        m_recordStatus->setText(QStringLiteral("导出失败：%1").arg(error));
    else
        m_recordStatus->setText(QStringLiteral("诊断包已导出：%1").arg(path));
}

} // namespace pvf
