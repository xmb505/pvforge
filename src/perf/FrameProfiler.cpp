#include "perf/FrameProfiler.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace pvf::perf {
namespace {

constexpr int kMaxRecordedFrames = 20000;
constexpr double kDroppedFrameMs = 33.4; // below 30 fps

const char* const kCategoryKeys[] = {
    "frame", "render2", "meta", "lookup", "decode", "encode", "disk",
    "sqlite", "solver", "html", "video", "paint", "ui", "other",
};

const char* const kCategoryLabels[] = {
    "整帧", "场景渲染", "元数据构建", "缓存查找", "缓存解码", "缓存编码",
    "磁盘写入", "缓存索引", "物理求解", "HTML 抓帧", "视频解码", "视口绘制",
    "界面", "其他",
};

qint64 steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

qint64 steadyNowMs()
{
    return steadyNowNs() / 1000000;
}

struct ThreadFrameState
{
    int depth = 0;
    bool active = false;
    qint64 startNs = 0;
    qint64 startMs = 0;
    std::array<double, kCategoryCount> categoryMs{};
    int cacheHits = 0;
    int cacheMisses = 0;
    int cacheStores = 0;
    int qoiStores = 0;
    int webpStores = 0;
    int maxPendingStores = 0;
    int solverCalls = 0;
    bool html = false;
    bool video = false;
};

thread_local ThreadFrameState tlsFrame;

double percentile(const QVector<double>& sorted, double fraction)
{
    if (sorted.isEmpty())
        return 0.0;
    const int index = std::clamp(
        int(std::ceil(fraction * sorted.size())) - 1, 0,
        int(sorted.size()) - 1);
    return sorted.at(index);
}

QString msText(double ms)
{
    return QString::number(ms, 'f', 1);
}

QString percentText(double share)
{
    return QString::number(share * 100.0, 'f', 1) + QLatin1Char('%');
}

QString categoryText(Category category)
{
    const int index = static_cast<int>(category);
    if (index < 0 || index >= kCategoryCount)
        return QStringLiteral("其他");
    return QString::fromUtf8(kCategoryLabels[index]);
}

QString slowFrameStage(const FrameSample& sample)
{
    int best = -1;
    double bestMs = 0.0;
    for (int i = static_cast<int>(Category::Render2); i < kCategoryCount; ++i)
    {
        if (sample.categoryMs[size_t(i)] > bestMs)
        {
            bestMs = sample.categoryMs[size_t(i)];
            best = i;
        }
    }
    if (best < 0 || bestMs <= 0.0)
        return QStringLiteral("未分阶段");
    return QStringLiteral("%1 %2 ms")
        .arg(categoryText(Category(best)), msText(bestMs));
}

QString buildConclusion(const Summary& summary, const CacheTotals& cache,
                        int solverCalls)
{
    QStringList lines;
    if (summary.count == 0)
    {
        lines << QStringLiteral("录制期间没有捕获到帧；请保持预览窗口可见或播放后再录。");
        return lines.join(QLatin1Char('\n'));
    }

    int hotspot = -1;
    double hotspotMs = 0.0;
    for (int i = static_cast<int>(Category::Render2); i < kCategoryCount; ++i)
    {
        if (summary.categoryTotalMs[size_t(i)] > hotspotMs)
        {
            hotspotMs = summary.categoryTotalMs[size_t(i)];
            hotspot = i;
        }
    }

    if (hotspot >= 0)
    {
        const double avgMs = hotspotMs / std::max(1, summary.count);
        const double share = summary.categoryShare[size_t(hotspot)];
        const Category category = Category(hotspot);
        QString headline;
        switch (category)
        {
        case Category::Encode:
            headline = QStringLiteral("缓存编码是主要开销");
            break;
        case Category::Render2:
            headline = QStringLiteral("场景渲染是主要开销");
            break;
        case Category::Decode:
            headline = QStringLiteral("缓存解码是主要开销");
            break;
        case Category::Meta:
        case Category::Lookup:
        case Category::Disk:
        case Category::Sqlite:
            headline = QStringLiteral("缓存读写与元数据是主要开销");
            break;
        case Category::Solver:
            headline = QStringLiteral("物理求解是主要开销");
            break;
        case Category::Html:
            headline = QStringLiteral("HTML 抓帧是主要开销");
            break;
        case Category::Video:
            headline = QStringLiteral("视频解码是主要开销");
            break;
        case Category::Paint:
            headline = QStringLiteral("视口绘制是主要开销");
            break;
        case Category::Ui:
            headline = QStringLiteral("界面处理是主要开销");
            break;
        default:
            headline = QStringLiteral("其他阶段是主要开销");
            break;
        }
        lines << QStringLiteral("%1（%2，平均 %3 ms/帧）。")
                     .arg(headline, percentText(share),
                          msText(avgMs));
    }

    const quint64 lookups = cache.hits + cache.misses;
    const double hitRate =
        lookups > 0 ? double(cache.hits) / double(lookups) : 0.0;
    if (lookups >= 5 && hitRate < 0.5)
    {
        lines << QStringLiteral("首次访问帧较多（缓存命中率 %1，命中 %2 / 未命中 %3）。")
                     .arg(percentText(hitRate))
                     .arg(cache.hits)
                     .arg(cache.misses);
    }
    if (solverCalls > 0)
    {
        lines << QStringLiteral(
            "捕获到 %1 次求解调用；若录制时正在播放，说明求解在播放路径重算，"
            "建议检查动画缓存是否命中。")
                     .arg(solverCalls);
    }
    if (summary.p95Ms > kDroppedFrameMs)
    {
        lines << QStringLiteral("p95 已超过 30 fps 预算（%1 ms），存在明显卡顿帧。")
                     .arg(msText(summary.p95Ms));
    }
    if (summary.meanMs > 0.0 && summary.meanMs < 8.0 && solverCalls == 0
        && (hotspot < 0
            || summary.categoryShare[size_t(hotspot)] < 0.5))
    {
        lines << QStringLiteral("帧耗时充足，未发现单一瓶颈。");
    }
    return lines.join(QLatin1Char('\n'));
}

QString buildReportText(const RecordingReport& report, int solverCalls)
{
    QStringList lines;
    lines << QStringLiteral("PVForge 性能报告");
    lines << QStringLiteral("录制时长：%1 秒，捕获 %2 帧")
                 .arg(QString::number(report.elapsedSeconds, 'f', 1))
                 .arg(report.frameCount);

    const Summary& s = report.summary;
    lines << QStringLiteral(
                 "帧耗时：FPS %1，p50 %2 ms，p95 %3 ms，p99 %4 ms，掉帧 %5")
                 .arg(QString::number(s.fps, 'f', 1), msText(s.p50Ms),
                      msText(s.p95Ms), msText(s.p99Ms))
                 .arg(s.droppedFrames);

    if (s.count > 0)
    {
        struct Stage
        {
            int index = -1;
            double totalMs = 0.0;
            double share = 0.0;
        };
        QVector<Stage> stages;
        for (int i = 1; i < kCategoryCount; ++i)
        {
            if (s.categoryTotalMs[size_t(i)] <= 0.0)
                continue;
            stages.append(Stage{i, s.categoryTotalMs[size_t(i)],
                                s.categoryShare[size_t(i)]});
        }
        std::sort(stages.begin(), stages.end(),
                  [](const Stage& a, const Stage& b) {
                      return a.totalMs > b.totalMs;
                  });
        QStringList stageLines;
        for (const Stage& stage : stages)
        {
            stageLines << QStringLiteral("  %1 %2（平均 %3 ms/帧）")
                              .arg(categoryText(Category(stage.index)),
                                   percentText(stage.share),
                                   msText(stage.totalMs / std::max(1, s.count)));
        }
        if (!stageLines.isEmpty())
            lines << QString() << QStringLiteral("阶段占比（含子阶段）：")
                  << stageLines.join(QLatin1Char('\n'));
    }

    const quint64 lookups = report.cache.hits + report.cache.misses;
    const double hitRate =
        lookups > 0 ? double(report.cache.hits) / double(lookups) : 0.0;
    lines << QStringLiteral(
                 "缓存：命中 %1 / 未命中 %2（命中率 %3），QOI 写入 %4，WebP 写入 %5")
                 .arg(report.cache.hits)
                 .arg(report.cache.misses)
                 .arg(percentText(hitRate))
                 .arg(report.cache.qoiStores)
                 .arg(report.cache.webpStores);
    lines << QStringLiteral("求解调用：%1 次").arg(solverCalls);

    if (!report.slowFrames.isEmpty())
    {
        QStringList slow;
        for (const FrameSample& sample : report.slowFrames)
        {
            slow << QStringLiteral("第 %1 帧 %2 ms（%3）")
                        .arg(sample.index)
                        .arg(msText(sample.totalMs), slowFrameStage(sample));
        }
        lines << QString() << QStringLiteral("慢帧：")
              << slow.join(QLatin1Char('\n'));
    }

    lines << QString() << QStringLiteral("结论：") << report.conclusion;
    return lines.join(QLatin1Char('\n'));
}

void copyGlobalTotals(CacheTotals* target, quint64 hits, quint64 misses,
                      quint64 stores, quint64 qoi, quint64 webp, int pending)
{
    target->hits = hits;
    target->misses = misses;
    target->stores = stores;
    target->qoiStores = qoi;
    target->webpStores = webp;
    target->pendingStores = pending;
}

} // namespace

QString categoryKey(Category category)
{
    const int index = static_cast<int>(category);
    if (index < 0 || index >= kCategoryCount)
        return QStringLiteral("other");
    return QString::fromLatin1(kCategoryKeys[index]);
}

QString categoryLabel(Category category)
{
    return categoryText(category);
}

FrameProfiler& FrameProfiler::instance()
{
    static FrameProfiler profiler;
    return profiler;
}

FrameProfiler::FrameProfiler() = default;

void FrameProfiler::setCaptureEnabled(bool enabled)
{
    if (enabled)
    {
        m_listeners.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    int value = m_listeners.load(std::memory_order_relaxed);
    while (value > 0
           && !m_listeners.compare_exchange_weak(value, value - 1,
                                                 std::memory_order_relaxed))
    {
    }
}

bool FrameProfiler::captureEnabled() const
{
    return m_listeners.load(std::memory_order_relaxed) > 0
           || m_recording.load(std::memory_order_relaxed);
}

void FrameProfiler::beginFrame()
{
    ThreadFrameState& state = tlsFrame;
    if (state.depth++ > 0)
        return;
    if (!captureEnabled())
    {
        state.active = false;
        return;
    }
    state.active = true;
    state.startNs = steadyNowNs();
    state.startMs = steadyNowMs();
    state.categoryMs.fill(0.0);
    state.cacheHits = 0;
    state.cacheMisses = 0;
    state.cacheStores = 0;
    state.qoiStores = 0;
    state.webpStores = 0;
    state.maxPendingStores = m_pendingStores.load(std::memory_order_relaxed);
    state.solverCalls = 0;
    state.html = false;
    state.video = false;
}

void FrameProfiler::endFrame()
{
    ThreadFrameState& state = tlsFrame;
    if (state.depth <= 0)
        return;
    if (--state.depth > 0)
        return;
    if (!state.active)
        return;
    state.active = false;

    FrameSample sample;
    sample.index = m_frameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    sample.startMs = state.startMs;
    sample.totalMs = (steadyNowNs() - state.startNs) / 1e6;
    sample.categoryMs = state.categoryMs;
    sample.cacheHits = state.cacheHits;
    sample.cacheMisses = state.cacheMisses;
    sample.cacheStores = state.cacheStores;
    sample.qoiStores = state.qoiStores;
    sample.webpStores = state.webpStores;
    sample.storeQueue = state.maxPendingStores;
    sample.solverCalls = state.solverCalls;
    sample.html = state.html;
    sample.video = state.video;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ring.push_back(sample);
        while (int(m_ring.size()) > kRingCapacity)
            m_ring.pop_front();
        if (m_recording.load(std::memory_order_relaxed))
        {
            m_recorded.push_back(sample);
            if (m_recorded.size() > kMaxRecordedFrames)
                m_recorded.remove(0, m_recorded.size() - kMaxRecordedFrames);
        }
    }
    maybeFinishRecording();
}

bool FrameProfiler::beginScope(const char* name, Category category)
{
    Q_UNUSED(name);
    if (!tlsFrame.active)
        return false;
    if (category == Category::Html)
        tlsFrame.html = true;
    else if (category == Category::Video)
        tlsFrame.video = true;
    return true;
}

bool FrameProfiler::beginScope(const QString& name, Category category)
{
    Q_UNUSED(name);
    return beginScope(static_cast<const char*>(nullptr), category);
}

void FrameProfiler::endScope(Category category, qint64 elapsedNs)
{
    if (!tlsFrame.active)
        return;
    const int index = static_cast<int>(category);
    if (index >= 0 && index < kCategoryCount)
        tlsFrame.categoryMs[size_t(index)] += double(elapsedNs) / 1e6;
}

void FrameProfiler::noteCacheLookup(bool hit)
{
    if (hit)
        m_cacheHits.fetch_add(1, std::memory_order_relaxed);
    else
        m_cacheMisses.fetch_add(1, std::memory_order_relaxed);
    if (!tlsFrame.active)
        return;
    if (hit)
        ++tlsFrame.cacheHits;
    else
        ++tlsFrame.cacheMisses;
}

void FrameProfiler::noteCacheStore(bool webp)
{
    m_cacheStores.fetch_add(1, std::memory_order_relaxed);
    if (webp)
        m_webpStores.fetch_add(1, std::memory_order_relaxed);
    else
        m_qoiStores.fetch_add(1, std::memory_order_relaxed);
    if (!tlsFrame.active)
        return;
    ++tlsFrame.cacheStores;
    if (webp)
        ++tlsFrame.webpStores;
    else
        ++tlsFrame.qoiStores;
}

void FrameProfiler::beginStore()
{
    const int pending =
        m_pendingStores.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tlsFrame.active)
        tlsFrame.maxPendingStores = std::max(tlsFrame.maxPendingStores, pending);
}

void FrameProfiler::endStore()
{
    int value = m_pendingStores.load(std::memory_order_relaxed);
    while (value > 0
           && !m_pendingStores.compare_exchange_weak(value, value - 1,
                                                     std::memory_order_relaxed))
    {
    }
}

void FrameProfiler::noteSolverCall()
{
    m_solverCalls.fetch_add(1, std::memory_order_relaxed);
    if (tlsFrame.active)
        ++tlsFrame.solverCalls;
}

Summary FrameProfiler::summarize(
    const QVector<FrameSample>& frames) const
{
    Summary summary;
    summary.count = frames.size();
    if (frames.isEmpty())
        return summary;

    QVector<double> totals;
    totals.reserve(frames.size());
    double sum = 0.0;
    double categorySum = 0.0;
    for (const FrameSample& sample : frames)
    {
        totals.append(sample.totalMs);
        sum += sample.totalMs;
        for (int i = 0; i < kCategoryCount; ++i)
        {
            summary.categoryTotalMs[size_t(i)] += sample.categoryMs[size_t(i)];
            categorySum += sample.categoryMs[size_t(i)];
        }
        if (sample.totalMs > kDroppedFrameMs)
            ++summary.droppedFrames;
    }
    summary.meanMs = sum / frames.size();

    std::sort(totals.begin(), totals.end());
    summary.p50Ms = percentile(totals, 0.50);
    summary.p95Ms = percentile(totals, 0.95);
    summary.p99Ms = percentile(totals, 0.99);

    const qint64 spanMs = frames.last().startMs - frames.first().startMs;
    if (frames.size() > 1 && spanMs > 0)
        summary.fps = (frames.size() - 1) * 1000.0 / double(spanMs);

    if (categorySum > 0.0)
    {
        for (int i = 0; i < kCategoryCount; ++i)
            summary.categoryShare[size_t(i)] =
                summary.categoryTotalMs[size_t(i)] / categorySum;
    }
    return summary;
}

Snapshot FrameProfiler::snapshot()
{
    maybeFinishRecording();

    Snapshot snapshot;
    std::lock_guard<std::mutex> lock(m_mutex);
    snapshot.frames.reserve(int(m_ring.size()));
    for (const FrameSample& sample : m_ring)
        snapshot.frames.append(sample);
    snapshot.summary = summarize(snapshot.frames);
    copyGlobalTotals(&snapshot.cache, m_cacheHits.load(std::memory_order_relaxed),
                     m_cacheMisses.load(std::memory_order_relaxed),
                     m_cacheStores.load(std::memory_order_relaxed),
                     m_qoiStores.load(std::memory_order_relaxed),
                     m_webpStores.load(std::memory_order_relaxed),
                     m_pendingStores.load(std::memory_order_relaxed));
    snapshot.totalFrames = m_frameCounter.load(std::memory_order_relaxed);
    snapshot.solverCalls = m_solverCalls.load(std::memory_order_relaxed);
    return snapshot;
}

void FrameProfiler::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ring.clear();
        m_recorded.clear();
        m_lastReport = RecordingReport{};
    }
    m_frameCounter.store(0, std::memory_order_relaxed);
    m_cacheHits.store(0, std::memory_order_relaxed);
    m_cacheMisses.store(0, std::memory_order_relaxed);
    m_cacheStores.store(0, std::memory_order_relaxed);
    m_qoiStores.store(0, std::memory_order_relaxed);
    m_webpStores.store(0, std::memory_order_relaxed);
    m_pendingStores.store(0, std::memory_order_relaxed);
    m_solverCalls.store(0, std::memory_order_relaxed);
    m_recording.store(false, std::memory_order_relaxed);
    m_recordStartNs.store(0, std::memory_order_relaxed);
    m_recordSeconds.store(0, std::memory_order_relaxed);
}

bool FrameProfiler::startRecording(int seconds)
{
    if (m_recording.load(std::memory_order_relaxed))
        return false;
    const int bounded = std::clamp(seconds, 1, 600);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recorded.clear();
        m_lastReport = RecordingReport{};
    }
    m_recordStartNs.store(steadyNowNs(), std::memory_order_relaxed);
    m_recordSeconds.store(bounded, std::memory_order_relaxed);
    m_recording.store(true, std::memory_order_relaxed);
    return true;
}

void FrameProfiler::stopRecording()
{
    bool expected = true;
    if (!m_recording.compare_exchange_strong(expected, false,
                                             std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    finishRecordingLocked();
}

bool FrameProfiler::isRecording() const
{
    return m_recording.load(std::memory_order_relaxed);
}

double FrameProfiler::recordingElapsedSeconds() const
{
    if (!m_recording.load(std::memory_order_relaxed))
        return 0.0;
    const qint64 start = m_recordStartNs.load(std::memory_order_relaxed);
    return double(steadyNowNs() - start) / 1e9;
}

double FrameProfiler::recordingRemainingSeconds() const
{
    if (!m_recording.load(std::memory_order_relaxed))
        return 0.0;
    const int seconds = m_recordSeconds.load(std::memory_order_relaxed);
    return std::max(0.0, double(seconds) - recordingElapsedSeconds());
}

void FrameProfiler::maybeFinishRecording()
{
    if (!m_recording.load(std::memory_order_relaxed))
        return;
    const int seconds = m_recordSeconds.load(std::memory_order_relaxed);
    if (recordingElapsedSeconds() < double(seconds))
        return;
    bool expected = true;
    if (!m_recording.compare_exchange_strong(expected, false,
                                             std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    finishRecordingLocked();
}

void FrameProfiler::finishRecordingLocked()
{
    if (m_finishing.exchange(true, std::memory_order_relaxed))
        return;

    RecordingReport report;
    report.valid = true;
    report.requestedSeconds = m_recordSeconds.load(std::memory_order_relaxed);
    report.elapsedSeconds = double(steadyNowNs()
                                   - m_recordStartNs.load(std::memory_order_relaxed))
                            / 1e9;
    report.frameCount = m_recorded.size();
    report.summary = summarize(m_recorded);

    QVector<FrameSample> slow = m_recorded;
    std::sort(slow.begin(), slow.end(),
              [](const FrameSample& a, const FrameSample& b) {
                  return a.totalMs > b.totalMs;
              });
    const int slowCount = int(std::min<qsizetype>(5, slow.size()));
    for (int i = 0; i < slowCount; ++i)
        report.slowFrames.append(slow.at(i));

    qint64 hits = 0;
    qint64 misses = 0;
    qint64 stores = 0;
    qint64 qoi = 0;
    qint64 webp = 0;
    int pending = 0;
    int solverCalls = 0;
    for (const FrameSample& sample : m_recorded)
    {
        hits += sample.cacheHits;
        misses += sample.cacheMisses;
        stores += sample.cacheStores;
        qoi += sample.qoiStores;
        webp += sample.webpStores;
        pending = std::max(pending, sample.storeQueue);
        solverCalls += sample.solverCalls;
    }
    report.cache.hits = quint64(std::max<qint64>(0, hits));
    report.cache.misses = quint64(std::max<qint64>(0, misses));
    report.cache.stores = quint64(std::max<qint64>(0, stores));
    report.cache.qoiStores = quint64(std::max<qint64>(0, qoi));
    report.cache.webpStores = quint64(std::max<qint64>(0, webp));
    report.cache.pendingStores = pending;

    report.conclusion = buildConclusion(report.summary, report.cache, solverCalls);
    report.text = buildReportText(report, solverCalls);
    m_lastReport = report;
    m_recorded.clear();
    m_finishing.store(false, std::memory_order_relaxed);
}

RecordingReport FrameProfiler::recordedReport()
{
    maybeFinishRecording();
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastReport;
}

QString FrameProfiler::recordedReportText()
{
    return recordedReport().text;
}

QJsonObject FrameProfiler::sampleToJson(const FrameSample& sample)
{
    QJsonObject object;
    object.insert(QStringLiteral("index"), QJsonValue(qint64(sample.index)));
    object.insert(QStringLiteral("totalMs"),
                  QJsonValue(std::round(sample.totalMs * 1000.0) / 1000.0));
    QJsonObject categories;
    for (int i = 0; i < kCategoryCount; ++i)
    {
        if (sample.categoryMs[size_t(i)] <= 0.0)
            continue;
        categories.insert(categoryKey(Category(i)),
                          QJsonValue(std::round(
                                         sample.categoryMs[size_t(i)] * 1000.0)
                                     / 1000.0));
    }
    object.insert(QStringLiteral("categories"), categories);
    object.insert(QStringLiteral("cacheHits"), sample.cacheHits);
    object.insert(QStringLiteral("cacheMisses"), sample.cacheMisses);
    object.insert(QStringLiteral("cacheStores"), sample.cacheStores);
    object.insert(QStringLiteral("qoiStores"), sample.qoiStores);
    object.insert(QStringLiteral("webpStores"), sample.webpStores);
    object.insert(QStringLiteral("storeQueue"), sample.storeQueue);
    object.insert(QStringLiteral("solverCalls"), sample.solverCalls);
    object.insert(QStringLiteral("html"), sample.html);
    object.insert(QStringLiteral("video"), sample.video);
    return object;
}

QJsonObject FrameProfiler::summaryToJson(const Summary& summary)
{
    QJsonObject object;
    object.insert(QStringLiteral("count"), summary.count);
    object.insert(QStringLiteral("fps"),
                  QJsonValue(std::round(summary.fps * 100.0) / 100.0));
    object.insert(QStringLiteral("meanMs"), summary.meanMs);
    object.insert(QStringLiteral("p50Ms"), summary.p50Ms);
    object.insert(QStringLiteral("p95Ms"), summary.p95Ms);
    object.insert(QStringLiteral("p99Ms"), summary.p99Ms);
    object.insert(QStringLiteral("droppedFrames"), summary.droppedFrames);
    QJsonObject categories;
    for (int i = 0; i < kCategoryCount; ++i)
    {
        QJsonObject stage;
        stage.insert(QStringLiteral("totalMs"),
                     summary.categoryTotalMs[size_t(i)]);
        stage.insert(QStringLiteral("share"),
                     summary.categoryShare[size_t(i)]);
        categories.insert(categoryKey(Category(i)), stage);
    }
    object.insert(QStringLiteral("categories"), categories);
    return object;
}

QJsonObject FrameProfiler::cacheTotalsToJson(const CacheTotals& cache)
{
    QJsonObject object;
    object.insert(QStringLiteral("hits"), QJsonValue(qint64(cache.hits)));
    object.insert(QStringLiteral("misses"), QJsonValue(qint64(cache.misses)));
    object.insert(QStringLiteral("stores"), QJsonValue(qint64(cache.stores)));
    object.insert(QStringLiteral("qoiStores"),
                  QJsonValue(qint64(cache.qoiStores)));
    object.insert(QStringLiteral("webpStores"),
                  QJsonValue(qint64(cache.webpStores)));
    object.insert(QStringLiteral("pendingStores"), cache.pendingStores);
    return object;
}

QJsonObject FrameProfiler::diagnosticsJson()
{
    const Snapshot current = snapshot();
    QJsonObject root;
    root.insert(QStringLiteral("tool"), QStringLiteral("PVForge"));
    root.insert(QStringLiteral("generatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("ringCapacity"), kRingCapacity);
    root.insert(QStringLiteral("totalFrames"),
                QJsonValue(qint64(current.totalFrames)));
    root.insert(QStringLiteral("solverCalls"),
                QJsonValue(qint64(current.solverCalls)));

    QJsonArray frames;
    for (const FrameSample& sample : current.frames)
        frames.append(sampleToJson(sample));
    root.insert(QStringLiteral("frames"), frames);
    root.insert(QStringLiteral("summary"), summaryToJson(current.summary));
    root.insert(QStringLiteral("cache"), cacheTotalsToJson(current.cache));

    const RecordingReport report = recordedReport();
    QJsonObject reportObject;
    reportObject.insert(QStringLiteral("valid"), report.valid);
    if (report.valid)
    {
        reportObject.insert(QStringLiteral("requestedSeconds"),
                            report.requestedSeconds);
        reportObject.insert(QStringLiteral("elapsedSeconds"),
                            report.elapsedSeconds);
        reportObject.insert(QStringLiteral("frameCount"), report.frameCount);
        reportObject.insert(QStringLiteral("summary"),
                            summaryToJson(report.summary));
        reportObject.insert(QStringLiteral("cache"),
                            cacheTotalsToJson(report.cache));
        reportObject.insert(QStringLiteral("conclusion"), report.conclusion);
        reportObject.insert(QStringLiteral("text"), report.text);
        QJsonArray slow;
        for (const FrameSample& sample : report.slowFrames)
            slow.append(sampleToJson(sample));
        reportObject.insert(QStringLiteral("slowFrames"), slow);
    }
    root.insert(QStringLiteral("report"), reportObject);
    return root;
}

FrameScope::FrameScope()
{
    FrameProfiler::instance().beginFrame();
}

FrameScope::~FrameScope()
{
    FrameProfiler::instance().endFrame();
}

Scope::Scope(const char* name, Category category)
    : m_category(category)
{
    FrameProfiler& profiler = FrameProfiler::instance();
    if (!profiler.captureEnabled())
        return;
    m_active = profiler.beginScope(name, category);
    if (m_active)
        m_startNs = steadyNowNs();
}

Scope::Scope(const QString& name, Category category)
    : m_category(category)
{
    FrameProfiler& profiler = FrameProfiler::instance();
    if (!profiler.captureEnabled())
        return;
    m_active = profiler.beginScope(name, category);
    if (m_active)
        m_startNs = steadyNowNs();
}

Scope::~Scope()
{
    if (!m_active)
        return;
    FrameProfiler::instance().endScope(m_category, steadyNowNs() - m_startNs);
}

} // namespace pvf::perf
