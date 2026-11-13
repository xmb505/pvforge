#include "LegacyTestSupport.h"
#include "TestRegistry.h"

namespace {

int perfMonitorTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (condition)
            return;
        qCritical() << "perf-monitor-test:" << message;
        ++failures;
    };
    const auto busyWait = [](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds)
        {
        }
    };

    pvf::perf::FrameProfiler& profiler =
        pvf::perf::FrameProfiler::instance();
    profiler.clear();
    profiler.setCaptureEnabled(true);

    // Ring buffer + stage aggregation + percentiles.  More than 300 frames
    // forces the oldest samples out.
    for (int i = 0; i < 420; ++i)
    {
        pvf::perf::FrameScope frame;
        {
            pvf::perf::Scope render(QStringLiteral("test-render"),
                                   pvf::perf::Category::Render2);
            busyWait(1);
        }
        if (i % 4 == 0)
        {
            pvf::perf::Scope encode(QStringLiteral("test-encode"),
                                    pvf::perf::Category::Encode);
            busyWait(1);
        }
        profiler.noteCacheLookup(i % 3 != 0);
        if (i % 10 == 0)
            profiler.noteSolverCall();
    }

    pvf::perf::Snapshot snapshot = profiler.snapshot();
    check(snapshot.frames.size() == pvf::perf::kRingCapacity,
          QStringLiteral("ring buffer did not cap at 300 (%1)")
              .arg(snapshot.frames.size()));
    check(snapshot.summary.count == pvf::perf::kRingCapacity,
          QStringLiteral("summary count is not the ring size"));
    check(!snapshot.frames.isEmpty()
              && snapshot.frames.first().index + pvf::perf::kRingCapacity - 1
                     == snapshot.frames.last().index,
          QStringLiteral("ring buffer dropped the wrong frames"));
    check(snapshot.summary.p50Ms <= snapshot.summary.p95Ms
              && snapshot.summary.p95Ms <= snapshot.summary.p99Ms,
          QStringLiteral("percentiles are not ordered"));
    check(snapshot.summary.p95Ms > 0.0 && snapshot.summary.meanMs > 0.0,
          QStringLiteral("frame timings are empty"));
    check(snapshot.summary.categoryTotalMs[size_t(
              int(pvf::perf::Category::Render2))] > 0.0,
          QStringLiteral("render2 stage was not recorded"));
    check(snapshot.summary.categoryTotalMs[size_t(
              int(pvf::perf::Category::Encode))] > 0.0,
          QStringLiteral("encode stage was not recorded"));
    check(snapshot.cache.hits > 0 && snapshot.cache.misses > 0,
          QStringLiteral("cache hit/miss counters are empty"));
    check(snapshot.solverCalls > 0,
          QStringLiteral("solver counter is empty"));

    // 5/10/30 second recording report.
    check(profiler.startRecording(60), QStringLiteral("recording did not start"));
    for (int i = 0; i < 90; ++i)
    {
        pvf::perf::FrameScope frame;
        {
            pvf::perf::Scope render(QStringLiteral("test-render"),
                                    pvf::perf::Category::Render2);
            busyWait(1);
        }
        profiler.noteCacheLookup(i % 2 == 0);
    }
    profiler.stopRecording();
    const pvf::perf::RecordingReport report = profiler.recordedReport();
    check(report.valid, QStringLiteral("recording report is invalid"));
    check(report.frameCount >= 90,
          QStringLiteral("recording lost frames (%1)").arg(report.frameCount));
    check(report.text.contains(QStringLiteral("PVForge 性能报告")),
          QStringLiteral("recording text has no header"));
    check(!report.conclusion.isEmpty(),
          QStringLiteral("recording conclusion is empty"));
    check(report.summary.p95Ms > 0.0,
          QStringLiteral("recording percentiles are empty"));

    // A duration-only recording ends even when no new frame arrives; the
    // monitor refresh calls snapshot() and that finalises it.
    check(profiler.startRecording(1),
          QStringLiteral("timed recording did not start"));
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 1150)
    {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    profiler.snapshot();
    check(!profiler.isRecording(),
          QStringLiteral("timed recording did not stop on its own"));
    check(profiler.recordedReport().valid,
          QStringLiteral("timed recording produced no report"));

    // Encode-only frames must produce the cache-encoding conclusion.
    check(profiler.startRecording(60),
          QStringLiteral("second recording did not start"));
    for (int i = 0; i < 30; ++i)
    {
        pvf::perf::FrameScope frame;
        pvf::perf::Scope encode(QStringLiteral("test-encode"),
                                pvf::perf::Category::Encode);
        busyWait(1);
        profiler.noteCacheLookup(false);
    }
    profiler.stopRecording();
    const pvf::perf::RecordingReport encodeReport = profiler.recordedReport();
    check(encodeReport.conclusion.contains(QStringLiteral("缓存编码")),
          QStringLiteral("encode hotspot conclusion missing: %1")
              .arg(encodeReport.conclusion));

    const QJsonObject diagnostics = profiler.diagnosticsJson();
    check(diagnostics.contains(QStringLiteral("frames"))
              && diagnostics.value(QStringLiteral("frames")).toArray().size()
                     == pvf::perf::kRingCapacity,
          QStringLiteral("diagnostics frames are missing"));
    check(diagnostics.contains(QStringLiteral("summary")),
          QStringLiteral("diagnostics summary is missing"));
    check(diagnostics.contains(QStringLiteral("cache")),
          QStringLiteral("diagnostics cache is missing"));
    check(diagnostics.contains(QStringLiteral("report")),
          QStringLiteral("diagnostics report is missing"));

    // The window opens offscreen, has every section, and survives a recording
    // and a close without touching the profiler ring.
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("no temp dir for the window test"));
    pvf::PerfMonitorWindow window;
    window.setCacheRoot(temp.path());
    window.show();
    QApplication::processEvents();

    check(window.windowTitle() == QStringLiteral("性能监视"),
          QStringLiteral("window title changed"));
    check(window.findChild<QLabel*>(QStringLiteral("perfSummaryLabel")),
          QStringLiteral("summary label missing"));
    check(window.findChild<QWidget*>(QStringLiteral("perfSparkline")),
          QStringLiteral("sparkline missing"));
    check(window.findChild<QTableWidget*>(QStringLiteral("perfStageTable")),
          QStringLiteral("stage table missing"));
    check(window.findChild<QLabel*>(QStringLiteral("perfCacheLabel")),
          QStringLiteral("cache label missing"));
    check(window.findChild<QLabel*>(QStringLiteral("perfSystemLabel")),
          QStringLiteral("system label missing"));
    check(window.findChild<QWidget*>(QStringLiteral("perfWaterfall")),
          QStringLiteral("waterfall missing"));

    auto* scrollArea = window.findChild<QScrollArea*>(
        QStringLiteral("perfScrollArea"));
    check(scrollArea != nullptr,
          QStringLiteral("perfScrollArea is missing"));
    if (scrollArea)
    {
        check(scrollArea->widgetResizable(),
              QStringLiteral("perfScrollArea is not widget-resizable"));
        check(scrollArea->frameShape() == QFrame::NoFrame,
              QStringLiteral("perfScrollArea still draws a frame"));
        check(scrollArea->horizontalScrollBarPolicy()
                  == Qt::ScrollBarAlwaysOff,
              QStringLiteral("perfScrollArea allows horizontal scrolling"));
    }

    auto* duration = window.findChild<QComboBox*>(
        QStringLiteral("perfRecordDuration"));
    auto* recordButton = window.findChild<QPushButton*>(
        QStringLiteral("perfRecordButton"));
    auto* reportEdit = window.findChild<QPlainTextEdit*>(
        QStringLiteral("perfReport"));
    check(duration && recordButton && reportEdit,
          QStringLiteral("recording controls missing"));
    if (duration && recordButton && reportEdit)
    {
        check(duration->count() == 3
                  && duration->itemData(0).toInt() == 5
                  && duration->itemData(1).toInt() == 10
                  && duration->itemData(2).toInt() == 30,
              QStringLiteral("recording durations are not 5/10/30"));
        duration->setCurrentIndex(0);
        recordButton->click();
        QApplication::processEvents();
        check(recordButton->text().contains(QStringLiteral("停止")),
              QStringLiteral("start did not flip the record button"));
        check(!duration->isEnabled(),
              QStringLiteral("duration combo stayed enabled while recording"));

        for (int i = 0; i < 6; ++i)
        {
            pvf::perf::FrameScope frame;
            pvf::perf::Scope render(QStringLiteral("test-render"),
                                    pvf::perf::Category::Render2);
            busyWait(1);
        }
        recordButton->click();
        QApplication::processEvents();
        check(!window.isRecording(),
              QStringLiteral("stop did not end the recording"));
        check(recordButton->text().contains(QStringLiteral("开始")),
              QStringLiteral("stop did not restore the record button"));
        check(duration->isEnabled(),
              QStringLiteral("duration combo stayed disabled after stop"));
        check(window.currentReportText().contains(
                  QStringLiteral("PVForge 性能报告")),
              QStringLiteral("window report is empty"));
    }

    const QString exportPath =
        QDir(temp.path()).filePath(QStringLiteral("perf-diagnostics.json"));
    QString exportError;
    check(window.exportDiagnostics(exportPath, &exportError),
          QStringLiteral("diagnostics export failed: %1").arg(exportError));
    QFile exported(exportPath);
    if (exported.open(QIODevice::ReadOnly))
    {
        const QJsonObject root =
            QJsonDocument::fromJson(exported.readAll()).object();
        check(root.contains(QStringLiteral("frames")),
              QStringLiteral("export has no frames"));
        check(root.contains(QStringLiteral("system")),
              QStringLiteral("export has no system section"));
        check(root.value(QStringLiteral("system")).toObject().contains(
                  QStringLiteral("supported")),
              QStringLiteral("system section has no support flag"));
        check(root.contains(QStringLiteral("report")),
              QStringLiteral("export has no report"));
    }
    else
    {
        check(false, QStringLiteral("exported JSON cannot be read"));
    }

    // The content column scrolls instead of being clipped, and opening the
    // advanced area must not resize the window.
    QDir().mkpath(QStringLiteral("/tmp/opencode"));
    const auto saveShot = [&check](QWidget& widget, const QString& path) {
        const QImage image = widget.grab().toImage();
        check(!image.isNull() && image.save(path),
              QStringLiteral("screenshot failed: %1").arg(path));
    };
    auto* advancedButton = window.findChild<QPushButton*>(
        QStringLiteral("perfAdvancedButton"));
    check(advancedButton != nullptr,
          QStringLiteral("advanced button missing"));

    window.resize(760, 600);
    QApplication::processEvents();
    if (scrollArea)
    {
        QScrollBar* bar = scrollArea->verticalScrollBar();
        check(bar->maximum() > 0,
              QStringLiteral("no vertical scrolling at 760x600"));
        bar->setValue(bar->maximum());
        QApplication::processEvents();
        check(bar->value() == bar->maximum(),
              QStringLiteral("could not scroll to the bottom"));
    }
    saveShot(window, QStringLiteral("/tmp/opencode/pvforge-perf-monitor.png"));

    const QSize sizeBeforeAdvanced = window.size();
    if (advancedButton)
        advancedButton->click();
    QApplication::processEvents();
    check(window.size() == sizeBeforeAdvanced,
          QStringLiteral("opening the advanced area resized the window"));
    if (scrollArea)
    {
        QScrollBar* bar = scrollArea->verticalScrollBar();
        check(bar->maximum() > 0,
              QStringLiteral("advanced area does not scroll"));
        bar->setValue(bar->maximum());
        QApplication::processEvents();
    }
    saveShot(window,
             QStringLiteral("/tmp/opencode/pvforge-perf-monitor-advanced.png"));

    const int framesBeforeClose = window.findChild<QWidget*>(
                                      QStringLiteral("perfSparkline"))
                                      ? profiler.snapshot().frames.size()
                                      : 0;
    window.grab().toImage().save(
        QStringLiteral("/tmp/pvforge_perf_monitor.png"));
    window.close();
    QApplication::processEvents();
    check(profiler.snapshot().frames.size() == framesBeforeClose,
          QStringLiteral("closing the window cleared the profiler ring"));

    // The View menu action opens and closes the same window without touching
    // the profiler.
    auto* project = new pvf::Project;
    pvf::MainWindow mainWindow(project);
    mainWindow.show();
    QApplication::processEvents();
    auto* perfAction = mainWindow.findChild<QAction*>(
        QStringLiteral("perfMonitorWindowAction"));
    check(perfAction != nullptr,
          QStringLiteral("View menu performance monitor action missing"));
    if (perfAction)
    {
        perfAction->setChecked(true);
        QApplication::processEvents();
        auto* opened = mainWindow.perfMonitorWindowForTest();
        check(opened != nullptr && opened->isVisible(),
              QStringLiteral("menu action did not open the performance window"));
        if (opened)
        {
            check(opened->findChild<QLabel*>(
                      QStringLiteral("perfSummaryLabel"))
                      != nullptr,
                  QStringLiteral("menu-opened window has no summary"));
            perfAction->setChecked(false);
            QApplication::processEvents();
            check(!opened->isVisible(),
                  QStringLiteral("menu action did not hide the window"));
        }
    }
    mainWindow.close();
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    profiler.setCaptureEnabled(false);
    qInfo() << "perf-monitor-test OK: ring" << snapshot.frames.size()
            << "frames, p50" << snapshot.summary.p50Ms << "ms, p95"
            << snapshot.summary.p95Ms << "ms, failures" << failures;
    return failures == 0 ? 0 : 1;
}

int perfFlameTest(QApplication& app)
{
    Q_UNUSED(app);
    int failures = 0;
    const auto check = [&failures](bool condition, const QString& message) {
        if (condition)
            return;
        qCritical() << "perf-flame-test:" << message;
        ++failures;
    };
    const auto busyWait = [](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds)
        {
        }
    };
    std::function<const pvf::perf::CallNode*(const pvf::perf::CallNode&,
                                             const QString&)>
        findNode;
    findNode = [&findNode](const pvf::perf::CallNode& node,
                           const QString& name) -> const pvf::perf::CallNode* {
        if (node.name == name)
            return &node;
        for (const pvf::perf::CallNode& child : node.children)
            if (const auto* found = findNode(child, name))
                return found;
        return nullptr;
    };

    pvf::perf::FrameProfiler& profiler =
        pvf::perf::FrameProfiler::instance();
    profiler.clear();
    profiler.setCaptureEnabled(true);

    // frame -> UI thread -> A -> A1/A2, B.  The tree must keep the nested
    // parent/child relation and split inclusive from exclusive time.
    {
        pvf::perf::FrameScope frame;
        {
            pvf::perf::Scope a(QStringLiteral("A"),
                               pvf::perf::Category::Render2);
            busyWait(3);
            {
                pvf::perf::Scope a1(QStringLiteral("A1"),
                                    pvf::perf::Category::Lookup);
                busyWait(1);
            }
            {
                pvf::perf::Scope a2(QStringLiteral("A2"),
                                    pvf::perf::Category::Decode);
                busyWait(1);
            }
        }
        pvf::perf::Scope b(QStringLiteral("B"), pvf::perf::Category::Paint);
        busyWait(1);
    }

    const pvf::perf::Snapshot snapshot = profiler.snapshot();
    check(!snapshot.frames.isEmpty(),
          QStringLiteral("synthetic frame was not captured"));
    const pvf::perf::CallNode& root = snapshot.lastFrameTree;
    check(root.name == QStringLiteral("帧"),
          QStringLiteral("last frame tree root is not 帧 (%1)").arg(root.name));
    const auto* uiRoot = findNode(root, QStringLiteral("界面线程"));
    check(uiRoot != nullptr,
          QStringLiteral("UI thread node is missing from the tree"));
    const auto* nodeA = findNode(root, QStringLiteral("A"));
    const auto* nodeB = findNode(root, QStringLiteral("B"));
    const auto* nodeA1 = findNode(root, QStringLiteral("A1"));
    const auto* nodeA2 = findNode(root, QStringLiteral("A2"));
    check(nodeA && nodeB && nodeA1 && nodeA2,
          QStringLiteral("synthetic scope names are missing"));
    if (nodeA && nodeB && nodeA1 && nodeA2)
    {
        check(nodeA->children.size() == 2,
              QStringLiteral("A does not have two children (%1)")
                  .arg(nodeA->children.size()));
        check(nodeA1->calls == 1 && nodeA2->calls == 1,
              QStringLiteral("nested call counts are not merged"));
        check(nodeB->category == pvf::perf::Category::Paint,
              QStringLiteral("B lost its category"));
        check(nodeA->inclusiveMs + 0.001
                  >= nodeA1->inclusiveMs + nodeA2->inclusiveMs,
              QStringLiteral("A inclusive is smaller than its children"));
        const double expectedExclusive =
            nodeA->inclusiveMs - nodeA1->inclusiveMs - nodeA2->inclusiveMs;
        check(std::abs(nodeA->exclusiveMs - expectedExclusive) < 0.01,
              QStringLiteral("A exclusive does not split from children (%1 vs %2)")
                  .arg(nodeA->exclusiveMs)
                  .arg(expectedExclusive));
        check(nodeA->exclusiveMs > 0.0,
              QStringLiteral("A exclusive time is empty"));
    }
    check(uiRoot != nullptr
              && std::abs(uiRoot->inclusiveMs - root.inclusiveMs) < 0.1,
          QStringLiteral("UI inclusive does not cover the frame"));

    // The classic misleading pair: 帧缓存查找 wraps 缓存解码/QOI.  The stage
    // table must show lookup inclusive slightly above decode and lookup self
    // far below decode, instead of counting both as independent 100% stages.
    profiler.clear();
    {
        pvf::perf::FrameScope frame;
        {
            pvf::perf::Scope lookup(QStringLiteral("帧缓存查找"),
                                    pvf::perf::Category::Lookup);
            busyWait(1);
            {
                pvf::perf::Scope decode(QStringLiteral("缓存解码/QOI"),
                                        pvf::perf::Category::Decode);
                busyWait(3);
            }
        }
        pvf::perf::Scope paint(QStringLiteral("B"),
                               pvf::perf::Category::Paint);
        busyWait(4);
    }
    const pvf::perf::Snapshot lookupSnapshot = profiler.snapshot();
    const auto* lookupNode = findNode(lookupSnapshot.lastFrameTree,
                                      QStringLiteral("帧缓存查找"));
    const auto* decodeNode = findNode(lookupSnapshot.lastFrameTree,
                                      QStringLiteral("缓存解码/QOI"));
    check(lookupNode && decodeNode,
          QStringLiteral("lookup/decode parent-child tree is missing"));
    if (lookupNode && decodeNode)
    {
        check(lookupNode->exclusiveMs < decodeNode->inclusiveMs,
              QStringLiteral("lookup self time is not smaller than decode"));
        check(std::abs(decodeNode->exclusiveMs - decodeNode->inclusiveMs)
                  < 0.01,
              QStringLiteral("decode self time was reduced by a child"));
    }
    const auto& lookupSummary = lookupSnapshot.summary;
    const size_t lookupIndex = size_t(int(pvf::perf::Category::Lookup));
    const size_t decodeIndex = size_t(int(pvf::perf::Category::Decode));
    const size_t paintIndex = size_t(int(pvf::perf::Category::Paint));
    check(lookupSummary.categorySelfMs[lookupIndex] > 0.0,
          QStringLiteral("lookup self time was not accumulated"));
    check(std::abs(lookupSummary.categorySelfMs[lookupIndex]
                   - (lookupSummary.categoryTotalMs[lookupIndex]
                      - lookupSummary.categoryTotalMs[decodeIndex]))
              < 0.05,
          QStringLiteral("lookup self != lookup inclusive - decode inclusive"));
    check(lookupSummary.categoryShare[lookupIndex] < 0.75,
          QStringLiteral("lookup inclusive share uses the overlapping sum (%1)")
              .arg(lookupSummary.categoryShare[lookupIndex]));
    check(lookupSummary.categoryShare[lookupIndex]
                  + lookupSummary.categoryShare[decodeIndex]
                  + lookupSummary.categoryShare[paintIndex]
              > 1.1,
          QStringLiteral("inclusive shares do not expose parent/child overlap"));
    check(lookupSummary.categorySelfShare[lookupIndex]
                  + lookupSummary.categorySelfShare[decodeIndex]
                  + lookupSummary.categorySelfShare[paintIndex]
              <= 1.05,
          QStringLiteral("self shares do not use the frame total denominator"));
    check(lookupSummary.categorySelfShare[lookupIndex]
              < lookupSummary.categoryShare[lookupIndex],
          QStringLiteral("lookup self share is not smaller than inclusive share"));

    // A worker task without FrameScope (cache encode path) joins the next
    // frame under its thread-name node.
    std::thread worker([&busyWait]() {
        pvf::perf::Scope task(QStringLiteral("worker-task"),
                              pvf::perf::Category::Encode);
        busyWait(2);
    });
    worker.join();
    {
        pvf::perf::FrameScope frame;
        pvf::perf::Scope render(QStringLiteral("after-worker"),
                                pvf::perf::Category::Render2);
        busyWait(1);
    }
    const pvf::perf::Snapshot workerSnapshot = profiler.snapshot();
    const pvf::perf::CallNode* workerThread = nullptr;
    for (const pvf::perf::CallNode& child : workerSnapshot.lastFrameTree.children)
        if (child.name.startsWith(QStringLiteral("线程 ")))
            workerThread = &child;
    check(workerThread != nullptr,
          QStringLiteral("worker thread root is missing from the frame tree"));
    check(workerThread
              && findNode(*workerThread, QStringLiteral("worker-task")) != nullptr,
          QStringLiteral("worker scope was not attached to its thread node"));

    // Ten recorded frames collapse into one aggregate tree keyed by path.
    check(profiler.startRecording(60),
          QStringLiteral("recording did not start"));
    for (int i = 0; i < 10; ++i)
    {
        pvf::perf::FrameScope frame;
        pvf::perf::Scope a(QStringLiteral("A"),
                           pvf::perf::Category::Render2);
        busyWait(1);
        pvf::perf::Scope a1(QStringLiteral("A1"),
                            pvf::perf::Category::Other);
        busyWait(1);
    }
    profiler.stopRecording();
    const pvf::perf::RecordingReport report = profiler.recordedReport();
    check(report.valid, QStringLiteral("recording report is invalid"));
    check(report.callTree.name == QStringLiteral("录制聚合"),
          QStringLiteral("recording tree root is not 录制聚合"));
    check(report.text.contains(QStringLiteral("自身"))
              && report.text.contains(QStringLiteral("整帧总时长")),
          QStringLiteral("report does not separate inclusive and self time"));
    const auto* recUi = findNode(report.callTree, QStringLiteral("界面线程"));
    const auto* recA = findNode(report.callTree, QStringLiteral("A"));
    const auto* recA1 = findNode(report.callTree, QStringLiteral("A1"));
    check(recUi && recUi->calls == 10,
          QStringLiteral("recorded UI root did not merge calls"));
    check(recA && recA->calls == 10,
          QStringLiteral("recorded A calls are %1, expected 10")
              .arg(recA ? recA->calls : -1));
    check(recA1 && recA1->calls == 10,
          QStringLiteral("recorded A1 calls did not merge"));

    const QJsonObject diagnostics = profiler.diagnosticsJson();
    check(diagnostics.contains(QStringLiteral("lastFrameTree")),
          QStringLiteral("diagnostics has no lastFrameTree"));
    check(diagnostics.value(QStringLiteral("lastFrameTree"))
              .toObject()
              .value(QStringLiteral("name"))
              .toString()
              == QStringLiteral("帧"),
          QStringLiteral("diagnostics lastFrameTree has no name"));
    const QJsonObject reportJson =
        diagnostics.value(QStringLiteral("report")).toObject();
    check(reportJson.contains(QStringLiteral("callTree")),
          QStringLiteral("diagnostics report has no callTree"));
    check(reportJson.value(QStringLiteral("callTree"))
              .toObject()
              .value(QStringLiteral("name"))
              .toString()
              == QStringLiteral("录制聚合"),
          QStringLiteral("diagnostics callTree has no aggregate root"));
    check(!reportJson.value(QStringLiteral("callTree"))
               .toObject()
               .value(QStringLiteral("children"))
               .toArray()
               .isEmpty(),
          QStringLiteral("diagnostics callTree has no children"));
    const QJsonObject summaryJson =
        diagnostics.value(QStringLiteral("summary")).toObject();
    const QJsonObject lookupJson =
        summaryJson.value(QStringLiteral("categories"))
            .toObject()
            .value(QStringLiteral("lookup"))
            .toObject();
    check(lookupJson.contains(QStringLiteral("selfMs"))
              && lookupJson.contains(QStringLiteral("selfShare")),
          QStringLiteral("summary JSON has no self time for lookup"));
    const QJsonArray diagnosticFrames =
        diagnostics.value(QStringLiteral("frames")).toArray();
    check(!diagnosticFrames.isEmpty()
              && diagnosticFrames.first()
                     .toObject()
                     .contains(QStringLiteral("selfCategories")),
          QStringLiteral("frame JSON has no selfCategories"));

    // Leave lookup/decode frames as the newest tree so the screenshot and the
    // stage table cover the parent/child example.
    for (int i = 0; i < 3; ++i)
    {
        pvf::perf::FrameScope frame;
        pvf::perf::Scope lookup(QStringLiteral("帧缓存查找"),
                                pvf::perf::Category::Lookup);
        busyWait(1);
        pvf::perf::Scope decode(QStringLiteral("缓存解码/QOI"),
                                pvf::perf::Category::Decode);
        busyWait(3);
    }

    // Advanced area: hidden by default, flame graph and sortable call table
    // appear only after the user presses 高级.
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("no temp dir for the window test"));
    pvf::PerfMonitorWindow window;
    window.setCacheRoot(temp.path());
    window.resize(1040, 1320);
    window.show();
    QApplication::processEvents();
    window.refreshNow();

    auto* stageTable =
        window.findChild<QTableWidget*>(QStringLiteral("perfStageTable"));
    check(stageTable && stageTable->columnCount() == 5,
          QStringLiteral("stage table does not show inclusive/self columns"));
    if (stageTable)
    {
        int lookupRow = -1;
        int decodeRow = -1;
        for (int row = 0; row < stageTable->rowCount(); ++row)
        {
            const QTableWidgetItem* item = stageTable->item(row, 0);
            if (item && item->text() == QStringLiteral("缓存查找"))
                lookupRow = row;
            if (item && item->text() == QStringLiteral("缓存解码"))
                decodeRow = row;
        }
        check(lookupRow >= 0,
              QStringLiteral("stage table has no lookup row"));
        check(decodeRow >= 0 && lookupRow >= 0 && decodeRow < lookupRow,
              QStringLiteral("stage table is not sorted by self time"));
        if (lookupRow >= 0)
        {
            const double inclusive =
                stageTable->item(lookupRow, 1)->data(Qt::UserRole).toDouble();
            const double self =
                stageTable->item(lookupRow, 2)->data(Qt::UserRole).toDouble();
            check(self > 0.0 && self < inclusive,
                  QStringLiteral(
                      "stage table lookup self %1 ms is not below inclusive %2 ms")
                      .arg(self)
                      .arg(inclusive));
        }
    }

    auto* advancedPanel =
        window.findChild<QWidget*>(QStringLiteral("perfAdvancedPanel"));
    auto* advancedButton =
        window.findChild<QPushButton*>(QStringLiteral("perfAdvancedButton"));
    check(advancedPanel != nullptr && advancedButton != nullptr,
          QStringLiteral("advanced panel/button missing"));
    check(advancedPanel && !advancedPanel->isVisible(),
          QStringLiteral("advanced panel is visible before the user opens it"));
    if (advancedButton)
        advancedButton->click();
    QApplication::processEvents();
    check(advancedPanel && advancedPanel->isVisible(),
          QStringLiteral("advanced panel did not open"));

    auto* flame = window.findChild<QWidget*>(QStringLiteral("perfFlameGraph"));
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("perfCallTable"));
    check(flame != nullptr && flame->isVisible(),
          QStringLiteral("flame graph is missing after opening advanced"));
    check(table != nullptr && table->isVisible(),
          QStringLiteral("call table is missing after opening advanced"));
    check(table && table->rowCount() > 0,
          QStringLiteral("call table has no rows"));
    check(table && table->columnCount() == 5,
          QStringLiteral("call table does not have 5 columns"));

    auto* source = window.findChild<QComboBox*>(
        QStringLiteral("perfTreeSource"));
    if (source && table)
    {
        source->setCurrentIndex(1);
        QApplication::processEvents();
        window.refreshNow();
        check(table->rowCount() > 0,
              QStringLiteral("recording aggregate has no call table rows"));
    }

    auto* search =
        window.findChild<QLineEdit*>(QStringLiteral("perfCallSearch"));
    if (search && table)
    {
        search->setText(QStringLiteral("A1"));
        QApplication::processEvents();
        int visibleRows = 0;
        for (int i = 0; i < table->rowCount(); ++i)
            if (!table->isRowHidden(i))
                ++visibleRows;
        check(visibleRows >= 1 && visibleRows < table->rowCount(),
              QStringLiteral("call tree search did not filter rows (%1 of %2)")
                  .arg(visibleRows)
                  .arg(table->rowCount()));
        search->clear();
        QApplication::processEvents();
    }

    const QString treeJson =
        QDir(temp.path()).filePath(QStringLiteral("calltree.json"));
    QString treeError;
    check(window.exportCallTree(treeJson, &treeError),
          QStringLiteral("call tree export failed: %1").arg(treeError));
    QFile treeFile(treeJson);
    if (treeFile.open(QIODevice::ReadOnly))
    {
        const QJsonObject treeObject =
            QJsonDocument::fromJson(treeFile.readAll()).object();
        check(treeObject.value(QStringLiteral("name")).toString()
                  == QStringLiteral("录制聚合"),
              QStringLiteral("exported call tree is not the selected aggregate"));
        check(treeObject.contains(QStringLiteral("children")),
              QStringLiteral("exported call tree has no children"));
    }
    else
    {
        check(false, QStringLiteral("exported call tree cannot be read"));
    }

    // Screenshot the newest lookup/decode tree, not the aggregate recording.
    if (source)
    {
        source->setCurrentIndex(0);
        QApplication::processEvents();
        window.refreshNow();
    }
    window.grab().toImage().save(
        QStringLiteral("/tmp/pvforge_perf_flame.png"));
    window.close();
    QApplication::processEvents();

    profiler.setCaptureEnabled(false);
    qInfo() << "perf-flame-test OK: tree root" << root.name
            << "children" << root.children.size() << "recorded frames"
            << report.frameCount << "failures" << failures;
    return failures == 0 ? 0 : 1;
}


} // namespace

namespace pvf::tests {

void registerPerfTests(Registry& registry)
{
    registry.add(QStringLiteral("--perf-monitor-test"),
                 [](QApplication& app) { return perfMonitorTest(app); });
    registry.add(QStringLiteral("--perf-flame-test"),
                 [](QApplication& app) { return perfFlameTest(app); });
}

} // namespace pvf::tests
