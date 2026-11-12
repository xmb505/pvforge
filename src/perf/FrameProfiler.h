#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

namespace pvf::perf {

// Fixed stage vocabulary.  Frame is the whole-frame total and is reported by
// FrameSample::totalMs; the other values are inclusive stage timings.
enum class Category : int
{
    Frame = 0,
    Render2,
    Meta,
    Lookup,
    Decode,
    Encode,
    Disk,
    Sqlite,
    Solver,
    Html,
    Video,
    Paint,
    Ui,
    Other,
    Count,
};

inline constexpr int kCategoryCount = static_cast<int>(Category::Count);
inline constexpr int kRingCapacity = 300;

QString categoryKey(Category category);
QString categoryLabel(Category category);

struct FrameSample
{
    quint64 index = 0;
    qint64 startMs = 0;
    double totalMs = 0.0;
    std::array<double, kCategoryCount> categoryMs{};
    int cacheHits = 0;
    int cacheMisses = 0;
    int cacheStores = 0;
    int qoiStores = 0;
    int webpStores = 0;
    int storeQueue = 0;
    int solverCalls = 0;
    bool html = false;
    bool video = false;
};

struct CacheTotals
{
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 stores = 0;
    quint64 qoiStores = 0;
    quint64 webpStores = 0;
    int pendingStores = 0;
};

struct Summary
{
    int count = 0;
    double fps = 0.0;
    double meanMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    int droppedFrames = 0;
    std::array<double, kCategoryCount> categoryTotalMs{};
    std::array<double, kCategoryCount> categoryShare{};
};

struct Snapshot
{
    QVector<FrameSample> frames;
    Summary summary;
    CacheTotals cache;
    quint64 totalFrames = 0;
    quint64 solverCalls = 0;
};

struct RecordingReport
{
    bool valid = false;
    int requestedSeconds = 0;
    double elapsedSeconds = 0.0;
    int frameCount = 0;
    Summary summary;
    CacheTotals cache;
    QVector<FrameSample> slowFrames;
    QString conclusion;
    QString text;
};

// Session-level frame profiler.  It keeps a fixed 300-frame ring, so the
// memory cost is bounded and the viewport can keep recording frames while the
// window is closed at the cost of one frame timer plus atomic switches.
//
// Scope timings are inclusive: a nested scope is also part of its parent, so
// the per-category totals can overlap.  That matches what a normal profiler
// shows; the UI labels the column "含子阶段".
class FrameProfiler
{
public:
    static FrameProfiler& instance();

    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    // A visible monitor or an active recording turns capture on.  With no
    // listener only the frame boundary probe runs (two atomics and a clock).
    void setCaptureEnabled(bool enabled);
    bool captureEnabled() const;

    void beginFrame();
    void endFrame();

    bool beginScope(const char* name, Category category);
    bool beginScope(const QString& name, Category category);
    void endScope(Category category, qint64 elapsedNs);

    void noteCacheLookup(bool hit);
    void noteCacheStore(bool webp);
    void beginStore();
    void endStore();
    void noteSolverCall();

    Snapshot snapshot();
    void clear();

    // Recording keeps every frame that lands inside the duration.  The report
    // is finalised by stopRecording(), the monitor timer, or the frame that
    // crosses the deadline.
    bool startRecording(int seconds);
    void stopRecording();
    bool isRecording() const;
    double recordingElapsedSeconds() const;
    double recordingRemainingSeconds() const;
    RecordingReport recordedReport();
    QString recordedReportText();

    // Frames, summary, cache totals and the last finished report.  The window
    // adds system information to this object for the diagnostic package.
    QJsonObject diagnosticsJson();

    static QJsonObject sampleToJson(const FrameSample& sample);
    static QJsonObject summaryToJson(const Summary& summary);
    static QJsonObject cacheTotalsToJson(const CacheTotals& cache);

private:
    FrameProfiler();

    Summary summarize(const QVector<FrameSample>& frames) const;
    void maybeFinishRecording();
    void finishRecordingLocked();
    void resetRecordingState();

    mutable std::mutex m_mutex;
    std::deque<FrameSample> m_ring;
    QVector<FrameSample> m_recorded;
    RecordingReport m_lastReport;

    std::atomic<int> m_listeners{0};
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_finishing{false};
    std::atomic<quint64> m_frameCounter{0};
    std::atomic<quint64> m_cacheHits{0};
    std::atomic<quint64> m_cacheMisses{0};
    std::atomic<quint64> m_cacheStores{0};
    std::atomic<quint64> m_qoiStores{0};
    std::atomic<quint64> m_webpStores{0};
    std::atomic<int> m_pendingStores{0};
    std::atomic<quint64> m_solverCalls{0};
    std::atomic<qint64> m_recordStartNs{0};
    std::atomic<int> m_recordSeconds{0};
};

// RAII frame boundary.  Put one at the top of the viewport paint method.
class FrameScope
{
public:
    FrameScope();
    ~FrameScope();
    FrameScope(const FrameScope&) = delete;
    FrameScope& operator=(const FrameScope&) = delete;
};

// RAII stage timer.  Disabled capture makes construction/destruction no-ops.
// The scope name is kept in the API for the function-level call tree batch
// (#65); this batch only aggregates the category timings.
class Scope
{
public:
    Scope(const char* name, Category category);
    Scope(const QString& name, Category category);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    bool m_active = false;
    Category m_category = Category::Other;
    qint64 m_startNs = 0;
};

} // namespace pvf::perf
