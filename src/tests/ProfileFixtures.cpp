#include "TestSupport.h"
#include "TestApp.h"
#include "TestRegistry.h"

namespace {

// ---- Cache-miss playback profile (issue #58) -----------------------------
//
// Measurement-only mode.  It builds synthetic 1920x1080 projects plus a copy
// of the user's project and separates the cost of one uncached playback frame
// into render / workbench store / lookup / CameraMonitor.  Nothing here is on
// a production path.

struct ProfileSamples
{
    QVector<double> ms;

    void addNs(qint64 ns)
    {
        ms.append(static_cast<double>(ns) / 1.0e6);
    }

    double mean() const
    {
        if (ms.isEmpty())
            return 0.0;
        double sum = 0.0;
        for (const double value : ms)
            sum += value;
        return sum / double(ms.size());
    }

    double percentile(double p) const
    {
        if (ms.isEmpty())
            return 0.0;
        QVector<double> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        const int index =
            std::clamp(int(std::ceil(p * double(sorted.size()))) - 1, 0,
                       int(sorted.size()) - 1);
        return sorted.at(index);
    }
};

QString profileStatsDelta(const pvf::workbench::CacheStats& before,
                          const pvf::workbench::CacheStats& after)
{
    return QStringLiteral("stores +%1 hits +%2 misses +%3 entries %4 "
                          "memFrames %5")
        .arg(after.stores - before.stores)
        .arg(after.hits - before.hits)
        .arg(after.misses - before.misses)
        .arg(after.entries)
        .arg(after.memoryFrames);
}

void reportProfile(const QString& scenario, const QString& label,
                   const ProfileSamples& samples, const QString& extra = {})
{
    qInfo().noquote()
        << QStringLiteral("cache-miss-profile | %1 | %2 | n=%3 mean=%4 ms "
                          "p50=%5 ms p95=%6 ms fps=%7 | %8")
               .arg(scenario, label)
               .arg(samples.ms.size())
               .arg(samples.mean(), 0, 'f', 2)
               .arg(samples.percentile(0.5), 0, 'f', 2)
               .arg(samples.percentile(0.95), 0, 'f', 2)
               .arg(samples.mean() > 0.0 ? 1000.0 / samples.mean() : 0.0, 0,
                    'f', 1)
               .arg(extra);
}

// ---------------------------------------------------------------------------
// Cache writer scheduling models (issue #67).
//
// The production writer is one shared queue drained by N pool workers: whoever
// is free takes the oldest frame.  The proposed alternative gives every worker
// its own FIFO and assigns playback misses round-robin.  Both are simulated
// here as discrete events so they can be compared without touching the
// production scheduler: one event per arrival and per finished encode,
// deterministic service times (57 ms +- 6 ms, every 7th frame an 80-100 ms
// complex-frame tail) and the real 64-frame queue bound.
// ---------------------------------------------------------------------------
double writerServiceMs(int frame)
{
    if (frame % 7 == 3)
        return 80.0 + double((frame * 13) % 21);
    return 57.0 + double((frame * 37) % 13) - 6.0;
}

struct WriterScheduleMetrics
{
    int written = 0;
    int dropped = 0;
    int peakBacklog = 0;
    double maxLatencyMs = 0.0;
    double p95LatencyMs = 0.0;
    double drainMs = 0.0;
    // Idle worker time while another worker's FIFO held work (round-robin
    // head-of-line blocking; always 0 for the shared queue).
    double holIdleMs = 0.0;
};

WriterScheduleMetrics simulateWriterSchedule(bool roundRobin, int threads,
                                             double arrivalFps, int frames,
                                             int queueCap)
{
    const double interval = 1000.0 / arrivalFps;
    const int laneCap = roundRobin
                            ? std::max(1, (queueCap + threads - 1) / threads)
                            : queueCap;

    QVector<QQueue<QPair<int, double>>> lanes(roundRobin ? threads : 1);
    QVector<double> busyUntil(threads, -1.0);
    QVector<int> activeFrame(threads, -1);
    QVector<double> activeArrival(threads, 0.0);

    WriterScheduleMetrics metrics;
    QVector<double> latencies;
    double previous = 0.0;
    double now = 0.0;
    double lastArrival = 0.0;
    double lastCompletion = 0.0;
    int arrived = 0;

    const auto backlog = [&lanes] {
        int size = 0;
        for (const auto& lane : lanes)
            size += lane.size();
        return size;
    };
    const auto busyWorkers = [&activeFrame] {
        int count = 0;
        for (const int frame : activeFrame)
            count += frame >= 0 ? 1 : 0;
        return count;
    };
    // A worker is blocked by another worker's queue while its own is empty.
    const auto headOfLineBlocked = [&](int worker) {
        if (activeFrame.at(worker) >= 0)
            return false;
        for (int i = 0; i < lanes.size(); ++i) {
            if (i != worker && !lanes.at(i).isEmpty())
                return true;
        }
        return false;
    };

    for (;;) {
        for (int worker = 0; worker < threads; ++worker) {
            if (activeFrame.at(worker) >= 0)
                continue;
            const int lane = roundRobin ? worker : 0;
            if (lanes.at(lane).isEmpty())
                continue;
            const QPair<int, double> job = lanes[lane].dequeue();
            activeFrame[worker] = job.first;
            activeArrival[worker] = job.second;
            busyUntil[worker] = now + writerServiceMs(job.first);
        }
        metrics.peakBacklog =
            std::max(metrics.peakBacklog, backlog() + busyWorkers());

        const double nextArrival = arrived < frames ? arrived * interval : -1.0;
        double nextFinish = -1.0;
        for (const double until : busyUntil) {
            if (until >= 0.0 && (nextFinish < 0.0 || until < nextFinish))
                nextFinish = until;
        }
        if (nextArrival < 0.0 && nextFinish < 0.0 && backlog() == 0)
            break;

        double next = nextFinish;
        const bool arrivalNext =
            nextArrival >= 0.0 && (nextFinish < 0.0 || nextArrival <= nextFinish);
        if (arrivalNext)
            next = nextArrival;

        // Charge the elapsed idle-with-work-waiting interval before moving on.
        // The shared queue cannot head-of-line block (any idle worker takes the
        // oldest frame), so holIdleMs stays 0 for it by construction.
        const double elapsed = next - previous;
        if (elapsed > 0.0 && roundRobin) {
            for (int worker = 0; worker < threads; ++worker) {
                if (headOfLineBlocked(worker))
                    metrics.holIdleMs += elapsed;
            }
        }
        previous = next;
        now = next;

        if (arrivalNext) {
            const int lane = roundRobin ? arrived % threads : 0;
            QQueue<QPair<int, double>>& queue = lanes[lane];
            while (queue.size() >= laneCap) {
                queue.dequeue();
                ++metrics.dropped;
            }
            queue.enqueue(qMakePair(arrived, now));
            lastArrival = now;
            ++arrived;
        } else {
            int worker = -1;
            for (int i = 0; i < threads; ++i) {
                if (busyUntil.at(i) < 0.0)
                    continue;
                if (worker < 0 || busyUntil.at(i) < busyUntil.at(worker))
                    worker = i;
            }
            if (worker < 0)
                break;
            latencies.append(now - activeArrival.at(worker));
            activeFrame[worker] = -1;
            busyUntil[worker] = -1.0;
            lastCompletion = now;
            ++metrics.written;
        }
    }

    for (const double latency : latencies) {
        metrics.maxLatencyMs = std::max(metrics.maxLatencyMs, latency);
    }
    if (!latencies.isEmpty()) {
        std::sort(latencies.begin(), latencies.end());
        const int index = std::clamp(
            int(std::ceil(0.95 * double(latencies.size()))) - 1, 0,
            int(latencies.size()) - 1);
        metrics.p95LatencyMs = latencies.at(index);
    }
    if (metrics.written > 0)
        metrics.drainMs = lastCompletion - lastArrival;
    return metrics;
}

// Runs both scheduling models for 1..4 writers at 30 and 60 fps and prints one
// line per case, plus the head-to-head conclusion.
void runWriterSchedulingModels()
{
    constexpr int kFrames = 300;
    constexpr int kQueueCap = 64;
    constexpr int kMaxThreads = 4;
    const auto print = [](const QString& model, int threads, double fps,
                          const WriterScheduleMetrics& metrics) {
        qInfo().noquote()
            << QStringLiteral(
                   "cache-miss-profile | writer scheduling | model=%1 "
                   "threads=%2 fps=%3 | frames=%4 written=%5 dropped=%6 "
                   "peakBacklog=%7 | maxLatency=%8 ms p95Latency=%9 ms | "
                   "drain=%10 ms holIdle=%11 ms")
                   .arg(model)
                   .arg(threads)
                   .arg(fps, 0, 'f', 0)
                   .arg(kFrames)
                   .arg(metrics.written)
                   .arg(metrics.dropped)
                   .arg(metrics.peakBacklog)
                   .arg(metrics.maxLatencyMs, 0, 'f', 0)
                   .arg(metrics.p95LatencyMs, 0, 'f', 0)
                   .arg(metrics.drainMs, 0, 'f', 0)
                   .arg(metrics.holIdleMs, 0, 'f', 0);
    };

    for (const double fps : {30.0, 60.0}) {
        for (int threads = 1; threads <= kMaxThreads; ++threads) {
            print(QStringLiteral("shared-queue"), threads, fps,
                  simulateWriterSchedule(false, threads, fps, kFrames,
                                         kQueueCap));
            print(QStringLiteral("round-robin"), threads, fps,
                  simulateWriterSchedule(true, threads, fps, kFrames,
                                         kQueueCap));
        }
    }
}

bool copyProfileProject(const QString& source, const QString& target,
                        QString* error)
{
    const QDir sourceDir(source);
    if (!sourceDir.exists())
    {
        *error = QStringLiteral("missing project: %1").arg(source);
        return false;
    }
    if (!QDir().mkpath(target))
    {
        *error = QStringLiteral("cannot create %1").arg(target);
        return false;
    }

    QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString from = it.next();
        const QString relative = sourceDir.relativeFilePath(from);
        // The copy must start with a cold cache, and history is not needed for
        // a render profile.
        if (relative.startsWith(QStringLiteral(".git/"))
            || relative.startsWith(QStringLiteral(".cache/")))
            continue;
        const QString to = QDir(target).filePath(relative);
        if (!QDir().mkpath(QFileInfo(to).absolutePath()))
        {
            *error = QStringLiteral("cannot create %1")
                         .arg(QFileInfo(to).absolutePath());
            return false;
        }
        if (!QFile::copy(from, to))
        {
            *error = QStringLiteral("cannot copy %1").arg(relative);
            return false;
        }
    }
    return true;
}

struct ProfileFixture
{
    QString name;
    int vectors = 0;
    int texts = 0;
    bool strokes = false;
    bool raster = false;
};

pvf::Uuid buildProfileFixture(pvf::EditorSession& session,
                              const ProfileFixture& fixture, QString* error)
{
    pvf::model::Project& project = session.mutableProject();
    QVector<pvf::Uuid> uuids;

    const int perRow =
        fixture.vectors <= 10 ? 3 : (fixture.vectors <= 60 ? 10 : 20);
    const int rows = std::max(1, (fixture.vectors + perRow - 1) / perRow);
    for (int i = 0; i < fixture.vectors; ++i)
    {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = QStringLiteral("图元 %1").arg(i + 1);
        entity.start = 0;
        entity.end = 3000;
        entity.row = i;
        entity.content.kind = pvf::ContentKind::Vector;
        pvf::model::VectorContent& vector = entity.content.vector;
        vector.shape = i % 2;
        vector.width = 90.0 + 14.0 * (i % 5);
        vector.height = 64.0 + 12.0 * (i % 4);
        vector.fillColor =
            QColor::fromHsv((i * 37) % 360, 180, 235).name(QColor::HexArgb);
        vector.strokeWidth = fixture.strokes && i % 3 == 0 ? 6.0 : 0.0;
        vector.strokeColor = QStringLiteral("#101014");

        const int column = i % perRow;
        const int row = i / perRow;
        entity.transform.x =
            -880.0 + 1760.0 * double(column + 1) / double(perRow + 1);
        entity.transform.y =
            400.0 - 800.0 * double(row + 1) / double(rows + 1);

        project.entities.insert(entity.uuid, entity);
        uuids.append(entity.uuid);
    }

    for (int i = 0; i < fixture.texts; ++i)
    {
        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = QStringLiteral("标题 %1").arg(i + 1);
        entity.start = 0;
        entity.end = 3000;
        entity.row = fixture.vectors + i;
        entity.content.kind = pvf::ContentKind::Text;
        entity.content.text.text =
            QStringLiteral("PVForge 性能标题 %1 · 未缓存播放").arg(i + 1);
        entity.content.text.pixelSize = 96;
        entity.content.text.color = QStringLiteral("#ffffff");
        entity.content.text.strokeColor = QStringLiteral("#000000");
        entity.content.text.strokeWidth = fixture.strokes ? 5.0 : 0.0;
        entity.transform.x = -420.0 + 380.0 * i;
        entity.transform.y = 360.0 - 90.0 * i;

        project.entities.insert(entity.uuid, entity);
        uuids.append(entity.uuid);
    }

    if (fixture.raster)
    {
        // A real image asset makes the frame meta hasRaster=true, which is the
        // WebP (asynchronous during playback) store path.
        QImage photo(640, 360, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&photo);
            QLinearGradient gradient(0, 0, 640, 360);
            gradient.setColorAt(0.0, QColor(210, 70, 40));
            gradient.setColorAt(0.5, QColor(40, 170, 120));
            gradient.setColorAt(1.0, QColor(40, 70, 210));
            painter.fillRect(photo.rect(), gradient);
            painter.setPen(QColor(255, 255, 255, 220));
            painter.drawRect(40, 40, 560, 280);
        }
        const QString photoPath =
            QDir(session.root()).filePath(QStringLiteral("profile-photo.png"));
        if (!photo.save(photoPath))
        {
            *error = QStringLiteral("cannot write %1").arg(photoPath);
            return {};
        }
        const auto stored = session.assets().importFile(photoPath, error);
        if (!stored.has_value())
            return {};

        pvf::model::Entity entity;
        entity.uuid = pvf::model::newUuid();
        entity.name = QStringLiteral("照片");
        entity.start = 0;
        entity.end = 3000;
        entity.row = fixture.vectors + fixture.texts;
        entity.transform.x = 260.0;
        entity.transform.y = -190.0;
        entity.content.kind = pvf::ContentKind::Asset;
        entity.content.asset.type = pvf::AssetType::Image;
        entity.content.asset.hash = stored->hash;
        project.entities.insert(entity.uuid, entity);
        uuids.append(entity.uuid);
    }

    if (uuids.isEmpty())
    {
        *error = QStringLiteral("fixture has no entities");
        return {};
    }
    if (!session.document()->commitEntities(
            uuids, QStringLiteral("cache miss profile fixture"), error))
        return {};
    session.flush();
    return uuids.first();
}

int runCacheProfileScenario(const QString& scenario,
                            std::unique_ptr<pvf::EditorSession> session,
                            const pvf::Uuid& selected, QString* error)
{
    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    auto* live = window->editorSessionForTest();
    auto* viewport = window->viewportForTest();
    auto* monitor = window->findChild<pvf::CameraMonitor*>();
    auto* workbench = live ? live->workbench() : nullptr;
    auto* document = live ? live->document() : nullptr;
    if (!live || !viewport || !monitor || !workbench || !document)
    {
        *error = QStringLiteral("window parts missing");
        window->close();
        QApplication::processEvents();
        return 1;
    }

    // The user's screenshot has an entity selected and the floating monitor
    // open; keep both states for every measured frame.
    document->setSelection(selected);
    monitor->setVisible(true);
    live->setPlayhead(0);
    QApplication::processEvents();
    QApplication::processEvents();

    constexpr int kFrames = 50;
    qInfo().nospace()
        << "cache-miss-profile | scenario=" << scenario
        << " entities=" << live->project().entities.size()
        << " viewport=" << viewport->width() << 'x' << viewport->height()
        << " monitor=" << monitor->width() << 'x' << monitor->height()
        << " cacheTarget=" << workbench->targetSize().width() << 'x'
        << workbench->targetSize().height()
        << " scale=" << workbench->scalePercent()
        << "% expand=" << workbench->expandPercent()
        << "% cacheDir=" << workbench->cacheDirectory();

    auto stats = [workbench] { return workbench->stats(); };

    // Hidden: no monitor paint at all.  Auto: the monitor is visible but only
    // the playhead's update() request paints it inside processEvents().
    // Explicit: monitor->repaint() runs in the timed paint slot.
    enum class MonitorPaint { Hidden, Auto, Explicit };
    auto measureWholeFrame = [&](const QString& label, int firstFrame,
                                 MonitorPaint monitorPaint,
                                 QVector<QImage>* captured) {
        const bool monitorVisible = monitorPaint != MonitorPaint::Hidden;
        monitor->setVisible(monitorVisible);
        QApplication::processEvents();
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        ProfileSamples paintSamples;
        ProfileSamples eventSamples;
        for (int i = 0; i < kFrames; ++i)
        {
            QElapsedTimer timer;
            timer.start();
            live->setPlayhead(firstFrame + i);
            viewport->repaint();
            if (monitorPaint == MonitorPaint::Explicit)
                monitor->repaint();
            const qint64 paintNs = timer.nsecsElapsed();
            paintSamples.addNs(paintNs);
            QApplication::processEvents();
            const qint64 totalNs = timer.nsecsElapsed();
            eventSamples.addNs(totalNs - paintNs);
            samples.addNs(totalNs);
            if (captured)
                captured->append(viewport->sessionImageForTest());
        }
        const pvf::workbench::CacheStats after = stats();
        reportProfile(scenario, label, samples,
                      QStringLiteral("paint=%1 ms events=%2 ms | %3")
                          .arg(paintSamples.mean(), 0, 'f', 2)
                          .arg(eventSamples.mean(), 0, 'f', 2)
                          .arg(profileStatsDelta(before, after)));
    };

    // Interleaved so a drifting CPU frequency cannot look like a monitor
    // effect; each condition is sampled twice.
    QVector<QImage> viewportImages;
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=hidden A"), 0,
                      MonitorPaint::Hidden, nullptr);
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=visible A"),
                      kFrames, MonitorPaint::Explicit, &viewportImages);
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=hidden B"),
                      2 * kFrames, MonitorPaint::Hidden, nullptr);
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=visible B"),
                      3 * kFrames, MonitorPaint::Explicit, nullptr);
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=auto (visible)"),
                      4 * kFrames, MonitorPaint::Auto, nullptr);
    measureWholeFrame(QStringLiteral("whole-frame miss monitor=hidden C"),
                      5 * kFrames, MonitorPaint::Hidden, nullptr);

    // CameraMonitor alone: a fresh playhead invalidates its preview and grab()
    // renders the draft-size camera image plus the widget chrome.
    {
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        for (int i = 0; i < kFrames; ++i)
        {
            QElapsedTimer timer;
            timer.start();
            live->setPlayhead(1000 + i);
            monitor->grab();
            samples.addNs(timer.nsecsElapsed());
        }
        reportProfile(scenario, QStringLiteral("camera-monitor render (grab)"),
                      samples, profileStatsDelta(before, stats()));
        QApplication::processEvents();
    }

    // Store microbenchmark with the real viewport-sized images.  Fresh frame
    // numbers keep every call a real miss (new file + new index row).
    double storeMeanMs = 0.0;
    {
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        for (int i = 0; i < kFrames; ++i)
        {
            const QImage image =
                viewportImages.isEmpty()
                    ? QImage()
                    : viewportImages.at(i % viewportImages.size());
            QElapsedTimer timer;
            timer.start();
            workbench->store(400 + i, workbench->cameraUuid(), image);
            samples.addNs(timer.nsecsElapsed());
        }
        const pvf::workbench::CacheStats after = stats();
        storeMeanMs = samples.mean();
        reportProfile(scenario,
                      QStringLiteral("store (viewport frame -> cache)"),
                      samples, profileStatsDelta(before, after));
    }

    const QImage lookupImage = viewportImages.isEmpty()
                                   ? QImage()
                                   : viewportImages.first();

    // Playback raster misses use the asynchronous writer.  This is the
    // issue #58/#66 scenario: the paint frame enqueues, 1..3 workers encode,
    // and the UI thread writes the file and index row after the frame is
    // already up.  Every run repeats the same three measurements:
    //   * whole-frame paint (UI cost with the writer running),
    //   * an unpaced 120-frame burst (queue bound + drain throughput),
    //   * paced 30 fps and 60 fps passes (the drop counts that matter in
    //     playback).
    if (workbench->buildMetaForTest(1000).hasRaster)
    {
        struct WriterRun
        {
            int threads = 1;
            double enqueueMs = 0.0;
            double drainMs = 0.0;
            double encodesPerSecond = 0.0;
            int burstDropped = 0;
            int burstWritten = 0;
            double tickMs = 0.0;
            int pacedDropped = 0;
            int pacedBacklog = 0;
            double pacedDrainMs = 0.0;
            double fastTickMs = 0.0;
            int fastWritten = 0;
            int fastDropped = 0;
            int fastBacklog = 0;
            double fastDrainMs = 0.0;
            bool drained = false;
        };

        const quint64 droppedBefore = workbench->droppedStores();
        const quint64 writtenBefore = workbench->asyncStoresWritten();
        const quint64 encodesBefore = workbench->workerEncodes();
        const double encodeMeanBefore = workbench->workerEncodeMeanMs();
        QVector<WriterRun> writerRuns;
        workbench->setPlaybackActive(true);

        for (const int threads : {1, 2, 3, 4})
        {
            workbench->setWriterThreads(threads);
            WriterRun run;
            run.threads = threads;

            // A normal playback pass: render + cache lookup + meta + render2,
            // no synchronous WebP encode on the paint thread.  The frame
            // ranges stay inside the fixture's 3000-frame content so the meta
            // really flags the raster asset.
            measureWholeFrame(
                QStringLiteral("whole-frame raster miss (async store, %1 thr)")
                    .arg(threads),
                1000 + threads * 50, MonitorPaint::Hidden, nullptr);
            // Drain whatever the pass queued so the burst starts empty.
            if (!workbench->waitForPendingStores(120000))
            {
                *error = QStringLiteral("writer pass did not drain");
                window->close();
                QApplication::processEvents();
                return 1;
            }

            const quint64 runDropped = workbench->droppedStores();
            const quint64 runWritten = workbench->asyncStoresWritten();
            const quint64 runEncodes = workbench->workerEncodes();

            // Unpaced burst: 120 misses with no event loop.  The bounded queue
            // must keep the last 64 and drop the rest oldest-first.
            ProfileSamples enqueueSamples;
            int maxQueue = 0;
            for (int i = 0; i < 120; ++i)
            {
                const QImage image =
                    viewportImages.isEmpty()
                        ? QImage(workbench->targetSize(),
                                 QImage::Format_ARGB32_Premultiplied)
                        : viewportImages.at(i % viewportImages.size());
                QElapsedTimer timer;
                timer.start();
                workbench->store(1200 + threads * 130 + i,
                                 workbench->cameraUuid(), image);
                enqueueSamples.addNs(timer.nsecsElapsed());
                maxQueue = std::max(maxQueue, workbench->pendingStores());
            }
            const int queuedAtBurstEnd = workbench->pendingStores();

            QElapsedTimer drain;
            drain.start();
            run.drained = workbench->waitForPendingStores(120000);
            run.drainMs = double(drain.nsecsElapsed()) / 1.0e6;
            const quint64 runEncodesAfter = workbench->workerEncodes();
            run.encodesPerSecond =
                run.drainMs > 0.0
                    ? double(runEncodesAfter - runEncodes) * 1000.0 / run.drainMs
                    : 0.0;
            run.burstDropped =
                int(workbench->droppedStores() - runDropped);
            run.burstWritten =
                int(workbench->asyncStoresWritten() - runWritten);
            run.enqueueMs = enqueueSamples.mean();

            qInfo().noquote()
                << QStringLiteral(
                       "cache-miss-profile | %1 | playback burst | threads=%2 | "
                       "ui-enqueue mean=%3 ms p50=%4 ms p95=%5 ms | maxQueue=%6 "
                       "queuedBurstEnd=%7 drained=%8 drain=%9 ms | encodes/s=%10 "
                       "| written +%11 dropped +%12")
                       .arg(scenario)
                       .arg(threads)
                       .arg(enqueueSamples.mean(), 0, 'f', 3)
                       .arg(enqueueSamples.percentile(0.5), 0, 'f', 3)
                       .arg(enqueueSamples.percentile(0.95), 0, 'f', 3)
                       .arg(maxQueue)
                       .arg(queuedAtBurstEnd)
                       .arg(run.drained ? QStringLiteral("yes")
                                        : QStringLiteral("no"))
                       .arg(run.drainMs, 0, 'f', 1)
                       .arg(run.encodesPerSecond, 0, 'f', 1)
                       .arg(run.burstWritten)
                       .arg(run.burstDropped);

            // Paced 30 fps pass: one store per playback tick, then pump the
            // event loop for the rest of the 33.3 ms budget so the writers get
            // exactly the time a real 30 fps playhead would give them.  10 s
            // is long enough for a slow writer to hit the 64-frame bound.
            const quint64 pacedDropped = workbench->droppedStores();
            const quint64 pacedWritten = workbench->asyncStoresWritten();
            constexpr int kPacedFrames = 300;
            constexpr double kTickBudgetMs = 1000.0 / 30.0;
            // Non-overlapping 300-frame windows inside the fixture's 3000
            // frames: 1800..2999 for 1..4 writers.
            const int pacedFirstFrame = 1800 + (threads - 1) * kPacedFrames;
            ProfileSamples tickSamples;
            for (int i = 0; i < kPacedFrames; ++i)
            {
                const QImage image =
                    viewportImages.isEmpty()
                        ? QImage(workbench->targetSize(),
                                 QImage::Format_ARGB32_Premultiplied)
                        : viewportImages.at(i % viewportImages.size());
                QElapsedTimer tick;
                tick.start();
                workbench->store(pacedFirstFrame + i,
                                 workbench->cameraUuid(), image);
                while (tick.elapsed() < kTickBudgetMs)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                    QThread::msleep(1);
                }
                tickSamples.addNs(tick.nsecsElapsed());
            }
            run.tickMs = tickSamples.mean();
            run.pacedBacklog = workbench->pendingStores();
            run.pacedDropped =
                int(workbench->droppedStores() - pacedDropped);
            const int pacedWrittenDuring =
                int(workbench->asyncStoresWritten() - pacedWritten);

            QElapsedTimer pacedDrain;
            pacedDrain.start();
            const bool pacedDrained = workbench->waitForPendingStores(120000);
            run.pacedDrainMs =
                double(pacedDrain.nsecsElapsed()) / 1.0e6;
            const int pacedWrittenCount =
                int(workbench->asyncStoresWritten() - pacedWritten);

            qInfo().noquote()
                << QStringLiteral(
                       "cache-miss-profile | %1 | playback 30fps | threads=%2 | "
                       "tick mean=%3 ms | written during=%4/%5 after drain=%6 "
                       "dropped=%7 backlog=%8 drain=%9 ms drained=%10")
                       .arg(scenario)
                       .arg(threads)
                       .arg(run.tickMs, 0, 'f', 2)
                       .arg(pacedWrittenDuring)
                       .arg(kPacedFrames)
                       .arg(pacedWrittenCount)
                       .arg(run.pacedDropped)
                       .arg(run.pacedBacklog)
                       .arg(run.pacedDrainMs, 0, 'f', 1)
                       .arg(pacedDrained ? QStringLiteral("yes")
                                         : QStringLiteral("no"));

            // 60 fps pass: the same 300 frames with half the tick budget.  One
            // writer cannot keep up here; the number shows how much the extra
            // slots claw back.
            const quint64 fastDropped = workbench->droppedStores();
            const quint64 fastWrittenBase = workbench->asyncStoresWritten();
            constexpr double kFastBudgetMs = 1000.0 / 60.0;
            ProfileSamples fastTicks;
            for (int i = 0; i < kPacedFrames; ++i)
            {
                const QImage image =
                    viewportImages.isEmpty()
                        ? QImage(workbench->targetSize(),
                                 QImage::Format_ARGB32_Premultiplied)
                        : viewportImages.at(i % viewportImages.size());
                QElapsedTimer tick;
                tick.start();
                workbench->store(i, workbench->cameraUuid(), image);
                while (tick.elapsed() < kFastBudgetMs)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
                    QThread::msleep(1);
                }
                fastTicks.addNs(tick.nsecsElapsed());
            }
            run.fastTickMs = fastTicks.mean();
            run.fastBacklog = workbench->pendingStores();
            run.fastDropped = int(workbench->droppedStores() - fastDropped);
            run.fastWritten =
                int(workbench->asyncStoresWritten() - fastWrittenBase);
            QElapsedTimer fastDrain;
            fastDrain.start();
            const bool fastDrained = workbench->waitForPendingStores(120000);
            run.fastDrainMs = double(fastDrain.nsecsElapsed()) / 1.0e6;
            qInfo().noquote()
                << QStringLiteral(
                       "cache-miss-profile | %1 | playback 60fps | threads=%2 | "
                       "tick mean=%3 ms | written during=%4/%5 dropped=%6 "
                       "backlog=%7 drain=%8 ms drained=%9")
                       .arg(scenario)
                       .arg(threads)
                       .arg(run.fastTickMs, 0, 'f', 2)
                       .arg(run.fastWritten)
                       .arg(kPacedFrames)
                       .arg(run.fastDropped)
                       .arg(run.fastBacklog)
                       .arg(run.fastDrainMs, 0, 'f', 1)
                       .arg(fastDrained ? QStringLiteral("yes")
                                        : QStringLiteral("no"));
            writerRuns.append(run);
        }

        // Conclusion: which writer count actually keeps up at 30 fps.
        QString conclusion;
        for (const WriterRun& run : writerRuns)
        {
            if (!conclusion.isEmpty())
                conclusion += QStringLiteral(" | ");
            conclusion +=
                QStringLiteral("%1 thr: 30fps %2/300 dropped, 60fps %3/300, "
                               "%4 encodes/s, post-run drain %5 ms")
                    .arg(run.threads)
                    .arg(run.pacedDropped)
                    .arg(run.fastDropped)
                    .arg(run.encodesPerSecond, 0, 'f', 1)
                    .arg(run.pacedDrainMs, 0, 'f', 1);
        }
        qInfo().noquote()
            << QStringLiteral("cache-miss-profile | %1 | writer scale "
                              "conclusion | %2")
                   .arg(scenario, conclusion);

        const quint64 encodesAfter = workbench->workerEncodes();
        const double encodeMeanAfter = workbench->workerEncodeMeanMs();
        const quint64 encodeCount = encodesAfter - encodesBefore;
        const double runMean =
            encodeCount == 0
                ? 0.0
                : (encodeMeanAfter * double(encodesAfter)
                   - encodeMeanBefore * double(encodesBefore))
                      / double(encodeCount);
        qInfo().noquote()
            << QStringLiteral(
                   "cache-miss-profile | %1 | writer totals | encodes=%2 "
                   "mean=%3 ms | written +%4 dropped +%5")
                   .arg(scenario)
                   .arg(encodeCount)
                   .arg(runMean, 0, 'f', 2)
                   .arg(workbench->asyncStoresWritten() - writtenBefore)
                   .arg(workbench->droppedStores() - droppedBefore);
        workbench->setPlaybackActive(false);
    }

    // Warm hit with a known-resident frame: 20 stores fit in the 256 MB LRU
    // budget (about 3.7 MB per 1280x720 frame), so the image comes back from
    // memory.  The meta is still rebuilt and the SQLite row touched.
    {
        for (int i = 0; i < 20; ++i)
            workbench->store(500 + i, workbench->cameraUuid(), lookupImage);
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        int hits = 0;
        for (int i = 0; i < 20; ++i)
        {
            bool hit = false;
            QElapsedTimer timer;
            timer.start();
            const QImage image =
                workbench->lookup(500 + i, workbench->cameraUuid(), &hit);
            samples.addNs(timer.nsecsElapsed());
            if (hit && !image.isNull())
                ++hits;
        }
        reportProfile(scenario, QStringLiteral("lookup hit (memory LRU)"),
                      samples, QStringLiteral("hits %1/20 | %2")
                                   .arg(hits)
                                   .arg(profileStatsDelta(before, stats())));
    }

    // Push the 500..519 images out of the LRU with 80 fresh stores; the same
    // lookups now decode the WebP from disk.
    {
        for (int i = 0; i < 80; ++i)
            workbench->store(600 + i, workbench->cameraUuid(), lookupImage);
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        int hits = 0;
        for (int i = 0; i < 20; ++i)
        {
            bool hit = false;
            QElapsedTimer timer;
            timer.start();
            const QImage image =
                workbench->lookup(500 + i, workbench->cameraUuid(), &hit);
            samples.addNs(timer.nsecsElapsed());
            if (hit && !image.isNull())
                ++hits;
        }
        reportProfile(scenario,
                      QStringLiteral("lookup hit (evicted, disk decode)"),
                      samples, QStringLiteral("hits %1/20 | %2")
                                   .arg(hits)
                                   .arg(profileStatsDelta(before, stats())));
    }

    // Cached playback, whole frame: a resident hit and a decode hit.  This is
    // the framerate the uncached path is compared against.
    auto measureCachedWholeFrame = [&](const QString& label, int firstFrame,
                                       int count) {
        monitor->setVisible(true);
        QApplication::processEvents();
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        for (int i = 0; i < count; ++i)
        {
            QElapsedTimer timer;
            timer.start();
            live->setPlayhead(firstFrame + i);
            viewport->repaint();
            monitor->repaint();
            QApplication::processEvents();
            samples.addNs(timer.nsecsElapsed());
        }
        const pvf::workbench::CacheStats after = stats();
        reportProfile(scenario, label, samples,
                      QStringLiteral("hits +%1 | %2")
                          .arg(after.hits - before.hits)
                          .arg(profileStatsDelta(before, after)));
    };
    measureCachedWholeFrame(QStringLiteral("whole-frame cached (memory hit)"),
                            670, 10);
    // The lookup-hit benchmark above re-inserted 500..519 into the LRU when it
    // decoded them; evict them again so the next pass really decodes.
    for (int i = 0; i < 80; ++i)
        workbench->store(700 + i, workbench->cameraUuid(), lookupImage);
    measureCachedWholeFrame(QStringLiteral("whole-frame cached (decode)"),
                            500, 10);

    // Raw render2 cost at project resolution, exactly what export and
    // grabProjectFrame use.
    {
        ProfileSamples samples;
        for (int i = 0; i < kFrames; ++i)
        {
            live->setPlayhead(100 + i);
            QElapsedTimer timer;
            timer.start();
            const QImage image = viewport->grabProjectFrame();
            samples.addNs(timer.nsecsElapsed());
            if (image.isNull())
            {
                *error = QStringLiteral("grabProjectFrame returned null");
                window->close();
                QApplication::processEvents();
                return 1;
            }
        }
        reportProfile(scenario,
                      QStringLiteral("render 1920x1080 (grabProjectFrame)"),
                      samples, QStringLiteral("output=1920x1080"));
        QApplication::processEvents();
    }

    // Raw render2 cost at the viewport's paint size with the same expanded
    // world rect the workbench lookup/render path uses.
    {
        const int expand = workbench->expandPercent();
        ProfileSamples samples;
        for (int i = 0; i < kFrames; ++i)
        {
            live->setPlayhead(100 + i);
            QElapsedTimer timer;
            timer.start();
            pvf::render2::Options options;
            options.projectFrame = live->playhead();
            options.outputSize = viewport->size();
            if (expand > 0)
            {
                options.worldRect = pvf::workbench::renderRect(
                    live->project(), expand);
                options.transparentOutsideCanvas = true;
            }
            pvf::anim::Cache& cache = live->animCache();
            pvf::render2::SceneWorld world(live->project(), live->assets(),
                                           &cache);
            const QImage image = pvf::render2::renderFrame(
                live->project(), live->assets(), options, world, cache);
            samples.addNs(timer.nsecsElapsed());
            if (image.isNull())
            {
                *error = QStringLiteral("viewport render returned null");
                window->close();
                QApplication::processEvents();
                return 1;
            }
        }
        reportProfile(scenario,
                      QStringLiteral("render @ viewport size (render2)"),
                      samples,
                      QStringLiteral("output=%1x%2")
                          .arg(viewport->width())
                          .arg(viewport->height()));
    }

    // Lookup on fresh frames: meta build + SHA-1 + SQLite miss.
    {
        const pvf::workbench::CacheStats before = stats();
        ProfileSamples samples;
        int hits = 0;
        for (int i = 0; i < kFrames; ++i)
        {
            bool hit = false;
            QElapsedTimer timer;
            timer.start();
            const QImage image =
                workbench->lookup(300 + i, workbench->cameraUuid(), &hit);
            samples.addNs(timer.nsecsElapsed());
            if (hit)
                ++hits;
        }
        reportProfile(scenario, QStringLiteral("lookup miss (meta + sqlite)"),
                      samples, QStringLiteral("hits %1/%2 | %3")
                                   .arg(hits)
                                   .arg(kFrames)
                                   .arg(profileStatsDelta(before, stats())));
    }

    // Split the store cost with the same primitives the cache uses: smooth
    // scale, QOI (vector frames) or WebP q80 (raster frames) and QSaveFile
    // write.  The residual is SQLite upsert, LRU bookkeeping and stat work.
    if (!viewportImages.isEmpty())
    {
        const QSize target = workbench->targetSize();
        const QImage source = viewportImages.first();
        const bool rasterStore = workbench->buildMetaForTest(400).hasRaster;
        const QString encoderName =
            rasterStore ? QStringLiteral("webp-encode")
                        : QStringLiteral("qoi-encode");
        const QString encodePath =
            QDir(QDir::tempPath()).filePath(
                QStringLiteral("pvf-cache-profile-encode.%1")
                    .arg(rasterStore ? QStringLiteral("webp")
                                     : QStringLiteral("qoi")));
        ProfileSamples scaleSamples;
        ProfileSamples encodeSamples;
        ProfileSamples writeSamples;
        qint64 encodedBytes = 0;
        bool encodeOk = true;
        for (int i = 0; i < 20; ++i)
        {
            QElapsedTimer timer;
            timer.start();
            const QImage scaled = source.scaled(
                target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            scaleSamples.addNs(timer.nsecsElapsed());

            QByteArray data;
            timer.restart();
            if (rasterStore)
            {
                QBuffer buffer(&data);
                buffer.open(QIODevice::WriteOnly);
                QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
                writer.setQuality(80);
                encodeOk = writer.write(scaled);
                buffer.close();
            }
            else
            {
                data = pvf::workbench::encodeQoi(scaled);
                encodeOk = !data.isEmpty();
            }
            encodeSamples.addNs(timer.nsecsElapsed());
            if (!encodeOk)
                break;
            encodedBytes = data.size();

            QSaveFile file(encodePath);
            timer.restart();
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
                || !file.commit())
            {
                encodeOk = false;
                break;
            }
            writeSamples.addNs(timer.nsecsElapsed());
        }

        const double measured =
            scaleSamples.mean() + encodeSamples.mean() + writeSamples.mean();
        qInfo().noquote()
            << QStringLiteral(
                   "cache-miss-profile | %1 | store split | scale=%2 ms "
                   "%3=%4 ms qsave-write=%5 ms "
                   "residual(sqlite/lru)=%6 ms total=%7 ms | %8 KB")
                   .arg(scenario)
                   .arg(scaleSamples.mean(), 0, 'f', 2)
                   .arg(encoderName)
                   .arg(encodeSamples.mean(), 0, 'f', 2)
                   .arg(writeSamples.mean(), 0, 'f', 2)
                   .arg(std::max(0.0, storeMeanMs - measured), 0, 'f', 2)
                   .arg(storeMeanMs, 0, 'f', 2)
                   .arg(encodedBytes / 1024);
        if (!encodeOk)
        {
            *error = QStringLiteral("%1 failed during store split")
                         .arg(encoderName);
            window->close();
            QApplication::processEvents();
            return 1;
        }

        // How much of the encode cost is the cached size itself?  960x540 is
        // the workbench target when expandPercent is 0 (plain 50% project
        // scale); 1280x720 is what the default 50% expand produces.
        auto encodeMs = [&source](const QSize& size, int quality) {
            ProfileSamples samples;
            for (int i = 0; i < 10; ++i)
            {
                const QImage scaled = source.scaled(
                    size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QByteArray data;
                QBuffer buffer(&data);
                buffer.open(QIODevice::WriteOnly);
                QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
                writer.setQuality(quality);
                QElapsedTimer timer;
                timer.start();
                if (!writer.write(scaled))
                    return -1.0;
                samples.addNs(timer.nsecsElapsed());
            }
            return samples.mean();
        };
        qInfo().noquote()
            << QStringLiteral(
                   "cache-miss-profile | %1 | encode-size sweep | "
                   "1280x720-q80=%2 ms 960x540-q80=%3 ms 640x360-q80=%4 ms "
                   "1280x720-q60=%5 ms")
                   .arg(scenario)
                   .arg(encodeMs(QSize(1280, 720), 80), 0, 'f', 2)
                   .arg(encodeMs(QSize(960, 540), 80), 0, 'f', 2)
                   .arg(encodeMs(QSize(640, 360), 80), 0, 'f', 2)
                   .arg(encodeMs(QSize(1280, 720), 60), 0, 'f', 2);
    }

    window->close();
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return 0;
}

int cacheMissProfileTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "cache-miss-profile: no temp dir";
        return 1;
    }

    const QVector<ProfileFixture> fixtures = {
        {QStringLiteral("simple"), 3, 0, false, false},
        {QStringLiteral("medium"), 50, 2, false, false},
        {QStringLiteral("complex"), 200, 4, true, false},
        {QStringLiteral("raster-image"), 12, 1, false, true},
    };

    // Scheduling model comparison is pure simulation: it does not touch the
    // production writer and runs before any project exists.
    runWriterSchedulingModels();

    for (const ProfileFixture& fixture : fixtures)
    {
        QString error;
        pvf::model::ProjectSettings settings;
        settings.name = QStringLiteral("cache-miss-%1").arg(fixture.name);
        settings.width = 1920;
        settings.height = 1080;
        settings.fps = 30;
        const QString directory =
            QDir(temp.path()).filePath(fixture.name
                                       + QStringLiteral(".pvproj"));
        auto session = pvf::EditorSession::createProject(
            directory, settings, &error, false);
        if (!session)
        {
            qCritical() << "cache-miss-profile: create" << fixture.name
                        << "failed:" << error;
            return 1;
        }
        const pvf::Uuid selected = buildProfileFixture(*session, fixture, &error);
        if (selected.isEmpty())
        {
            qCritical() << "cache-miss-profile: fixture" << fixture.name
                        << "failed:" << error;
            return 1;
        }
        if (runCacheProfileScenario(fixture.name, std::move(session), selected,
                                    &error)
            != 0)
        {
            qCritical() << "cache-miss-profile: scenario" << fixture.name
                        << "failed:" << error;
            return 1;
        }
    }

    // The user's reported project: copied so the profile never writes to it.
    const QString userSource =
        QStringLiteral("/home/xmb505/视频/PVForge/未命名项目1.pvproj");
    if (!QFileInfo::exists(userSource))
    {
        qWarning() << "cache-miss-profile: user project not found, skipping"
                   << userSource;
        return 0;
    }

    QString error;
    const QString copy =
        QDir(temp.path()).filePath(QStringLiteral("user-project.pvproj"));
    if (!copyProfileProject(userSource, copy, &error))
    {
        qWarning() << "cache-miss-profile: copy failed:" << error;
        return 0;
    }
    auto session = pvf::EditorSession::openProject(copy, &error, false);
    if (!session)
    {
        qWarning() << "cache-miss-profile: open user copy failed:" << error;
        return 0;
    }
    const pvf::Uuid selected = session->project().entities.isEmpty()
                                   ? pvf::Uuid()
                                   : session->project().entities.constBegin()->uuid;
    if (runCacheProfileScenario(QStringLiteral("user-project"),
                                std::move(session), selected, &error)
        != 0)
    {
        qCritical() << "cache-miss-profile: user-project failed:" << error;
        return 1;
    }
    return 0;
}

// ---- Cache encoder benchmark (issues #58 / #60) --------------------------
//
// Measurement-only mode.  It grabs one frame from the user's project and
// builds one synthetic 200-entity frame, expands both into the workbench's
// 50%-expanded cache geometry (transparent outside the canvas) and compares
// the encoders that could replace the current Qt WebP q80 store path.  No
// production cache code is touched.

struct EncoderBenchRow
{
    QString imageName;
    QString encoder;
    QSize size;
    ProfileSamples encodeSamples;
    ProfileSamples decodeSamples;
    qint64 bytes = 0;
    bool hasAlpha = false;
    double psnr = -1.0;
    bool ok = false;
    QString error;
};

double encoderBenchPsnr(const QImage& decoded, const QImage& reference)
{
    if (decoded.isNull() || reference.isNull()
        || decoded.size() != reference.size())
        return -1.0;

    const QImage actual = decoded.convertToFormat(QImage::Format_RGBA8888);
    const QImage expected = reference.convertToFormat(QImage::Format_RGBA8888);
    // Expanded cache frames are mostly transparent.  Compare only the opaque
    // canvas pixels so the transparent margin does not dominate the score.
    const bool masked = expected.hasAlphaChannel();
    double sum = 0.0;
    qint64 count = 0;
    for (int y = 0; y < expected.height(); ++y)
    {
        const uchar* actualLine = actual.constScanLine(y);
        const uchar* expectedLine = expected.constScanLine(y);
        for (int x = 0; x < expected.width(); ++x)
        {
            const int offset = x * 4;
            if (masked && expectedLine[offset + 3] != 255)
                continue;
            for (int channel = 0; channel < 3; ++channel)
            {
                const double delta = double(actualLine[offset + channel])
                                     - double(expectedLine[offset + channel]);
                sum += delta * delta;
                ++count;
            }
        }
    }
    if (count == 0)
        return -1.0;
    const double mse = sum / double(count);
    return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

void reportEncoderBenchRow(const EncoderBenchRow& row)
{
    if (!row.ok)
    {
        qWarning().noquote()
            << QStringLiteral("cache-encoder-bench | image=%1 size=%2x%3 "
                              "encoder=%4 | FAILED %5")
                   .arg(row.imageName)
                   .arg(row.size.width())
                   .arg(row.size.height())
                   .arg(row.encoder, row.error);
        return;
    }

    const double encodeP50 = row.encodeSamples.percentile(0.5);
    const double encodeP95 = row.encodeSamples.percentile(0.95);
    const double decodeP50 = row.decodeSamples.percentile(0.5);
    const double decodeP95 = row.decodeSamples.percentile(0.95);
    const QString psnr = row.psnr < 0.0
                             ? QStringLiteral("n/a")
                             : (row.psnr >= 99.0
                                    ? QStringLiteral("inf")
                                    : QString::number(row.psnr, 'f', 2));
    qInfo().noquote()
        << QStringLiteral(
               "cache-encoder-bench | image=%1 size=%2x%3 encoder=%4 | "
               "encode mean=%5 p50=%6 p95=%7 ms | decode mean=%8 p50=%9 "
               "p95=%10 ms | bytes=%11 (%12 KB) | alpha=%13 psnr=%14 | "
               "enc-p95<=16.7ms=%15 <=33ms=%16")
               .arg(row.imageName)
               .arg(row.size.width())
               .arg(row.size.height())
               .arg(row.encoder)
               .arg(row.encodeSamples.mean(), 0, 'f', 2)
               .arg(encodeP50, 0, 'f', 2)
               .arg(encodeP95, 0, 'f', 2)
               .arg(row.decodeSamples.mean(), 0, 'f', 2)
               .arg(decodeP50, 0, 'f', 2)
               .arg(decodeP95, 0, 'f', 2)
               .arg(row.bytes)
               .arg(double(row.bytes) / 1024.0, 0, 'f', 1)
               .arg(row.hasAlpha ? QStringLiteral("yes")
                                 : QStringLiteral("no"))
               .arg(psnr)
               .arg(encodeP95 <= 16.7 ? QStringLiteral("yes")
                                      : QStringLiteral("no"))
               .arg(encodeP95 <= 33.0 ? QStringLiteral("yes")
                                      : QStringLiteral("no"));
}

EncoderBenchRow runEncoderBench(
    const QString& imageName, const QString& encoder, const QSize& size,
    const QImage& image, const QImage& reference,
    const std::function<QByteArray(const QImage&)>& encode,
    const std::function<QImage(const QByteArray&)>& decode, int iterations)
{
    EncoderBenchRow row;
    row.imageName = imageName;
    row.encoder = encoder;
    row.size = size;

    QByteArray data;
    for (int i = 0; i < 2; ++i)
    {
        data = encode(image);
        if (data.isEmpty())
        {
            row.error = QStringLiteral("warmup encode failed");
            return row;
        }
        if (decode(data).isNull())
        {
            row.error = QStringLiteral("warmup decode failed");
            return row;
        }
    }

    for (int i = 0; i < iterations; ++i)
    {
        QElapsedTimer timer;
        timer.start();
        const QByteArray encoded = encode(image);
        row.encodeSamples.addNs(timer.nsecsElapsed());
        if (encoded.isEmpty())
        {
            row.error = QStringLiteral("encode failed");
            return row;
        }
        data = encoded;
        row.bytes = data.size();
    }

    QImage decoded;
    for (int i = 0; i < 2; ++i)
        decoded = decode(data);
    for (int i = 0; i < iterations; ++i)
    {
        QElapsedTimer timer;
        timer.start();
        decoded = decode(data);
        row.decodeSamples.addNs(timer.nsecsElapsed());
    }

    if (decoded.isNull())
    {
        row.error = QStringLiteral("decode failed");
        return row;
    }
    row.hasAlpha = decoded.hasAlphaChannel();
    row.psnr = encoderBenchPsnr(decoded, reference);
    row.ok = true;
    return row;
}

QImage makeEncoderBenchSynthetic()
{
    QImage image(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#101014")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    for (int i = 0; i < 196; ++i)
    {
        const int column = i % 14;
        const int row = i / 14;
        const double x = 60.0 + column * 130.0 + (i % 3) * 7.0;
        const double y = 45.0 + row * 72.0 + (i % 5) * 4.0;
        const double width = 70.0 + (i % 7) * 11.0;
        const double height = 34.0 + (i % 5) * 9.0;
        painter.setBrush(
            QColor::fromHsv((i * 37) % 360, 170 + (i % 3) * 30, 210));
        painter.setPen(i % 4 == 0
                           ? QPen(QColor(QStringLiteral("#e8e8f0")), 3.0)
                           : QPen(Qt::NoPen));
        if (i % 3 == 0)
            painter.drawEllipse(QRectF(x, y, width, height));
        else if (i % 3 == 1)
            painter.drawRoundedRect(QRectF(x, y, width, height), 10.0, 10.0);
        else
            painter.drawRect(QRectF(x, y, width, height));
    }

    QFont font;
    font.setPixelSize(72);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#ffffff")));
    for (int i = 0; i < 4; ++i)
        painter.drawText(QPointF(100.0 + i * 430.0, 980.0 - i * 120.0),
                         QStringLiteral("PVForge 缓存编码基准 %1").arg(i + 1));
    painter.end();
    return image;
}

QImage makeEncoderBenchExpandedFrame(const QImage& content, const QSize& size)
{
    // expandPercent=50 makes the canvas half of the cached world in each
    // axis: 1280x720 stores a 640x360 canvas with transparent margins.
    QImage frame(size, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    const int canvasWidth = std::max(1, size.width() / 2);
    const int canvasHeight = std::max(1, size.height() / 2);
    const QRect canvas((size.width() - canvasWidth) / 2,
                       (size.height() - canvasHeight) / 2, canvasWidth,
                       canvasHeight);
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(canvas, content);
    painter.end();
    return frame;
}

QImage makeEncoderBenchOpaque(const QImage& frame)
{
    QImage opaque(frame.size(), QImage::Format_RGB32);
    opaque.fill(QColor(QStringLiteral("#101014")));
    QPainter painter(&opaque);
    painter.drawImage(0, 0, frame);
    painter.end();
    return opaque;
}

QImage loadEncoderBenchPhoto()
{
    // QOI is lossless, so a photographic frame is its worst case for disk
    // size.  The desktop wallpapers make that bound measurable; the benchmark
    // simply skips the row when none of them is installed.
    for (const QString& path :
         {QStringLiteral("/usr/share/backgrounds/Clouds_by_Tibor_Mokanszki.jpg"),
          QStringLiteral(
              "/usr/share/backgrounds/Monument_valley_by_orbitelambda.jpg")})
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull())
            return image.scaled(1920, 1080, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return {};
}

QImage grabEncoderBenchUserFrame(QTemporaryDir& temp, QString* error)
{
    const QString source =
        QStringLiteral("/home/xmb505/视频/PVForge/未命名项目1.pvproj");
    if (!QFileInfo::exists(source))
    {
        *error = QStringLiteral("user project not found: %1").arg(source);
        return {};
    }

    const QString copy =
        QDir(temp.path()).filePath(QStringLiteral("encoder-user.pvproj"));
    if (!copyProfileProject(source, copy, error))
        return {};

    auto session = pvf::EditorSession::openProject(copy, error, false);
    if (!session)
        return {};

    auto* window = new pvf::MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1440, 900);
    window->show();
    QApplication::processEvents();
    QApplication::processEvents();

    QImage frame;
    if (auto* viewport = window->viewportForTest())
        frame = viewport->grabProjectFrame();
    else
        *error = QStringLiteral("viewport missing");

    window->close();
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (frame.isNull() && error->isEmpty())
        *error = QStringLiteral("grabProjectFrame returned null");
    return frame;
}

int cacheEncoderBenchTest(QApplication& app)
{
    Q_UNUSED(app);
    QTemporaryDir temp;
    if (!temp.isValid())
    {
        qCritical() << "cache-encoder-bench: no temp dir";
        return 1;
    }

    const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    const bool hasWebp = formats.contains(QByteArrayLiteral("webp"));
    const bool hasJpeg = formats.contains(QByteArrayLiteral("jpeg"));
    const bool hasPng = formats.contains(QByteArrayLiteral("png"));
    const bool hasQoi = formats.contains(QByteArrayLiteral("qoi"));

    QByteArray probeData;
    QBuffer probeBuffer(&probeData);
    probeBuffer.open(QIODevice::WriteOnly);
    QImageWriter webpProbe(&probeBuffer, QByteArrayLiteral("webp"));
    const bool hasWebpCompression =
        hasWebp
        && webpProbe.supportsOption(QImageIOHandler::CompressionRatio);
    qInfo().noquote()
        << QStringLiteral("cache-encoder-bench | qt=%1 formats webp=%2 "
                          "jpeg=%3 png=%4 qoi=%5 | webp-method-option=%6 | "
                          "libwebp=%7 | qoi-builtin=yes")
               .arg(QString::fromLatin1(qVersion()))
               .arg(hasWebp ? QStringLiteral("yes") : QStringLiteral("no"))
               .arg(hasJpeg ? QStringLiteral("yes") : QStringLiteral("no"))
               .arg(hasPng ? QStringLiteral("yes") : QStringLiteral("no"))
               .arg(hasQoi ? QStringLiteral("yes") : QStringLiteral("no"))
               .arg(hasWebpCompression ? QStringLiteral("yes")
                                       : QStringLiteral("no"))
               .arg(QStringLiteral(
                   "skipped (pkg-config --exists libwebp failed)"));

    QString error;
    const QImage userContent = grabEncoderBenchUserFrame(temp, &error);
    if (userContent.isNull())
        qWarning() << "cache-encoder-bench: user frame unavailable:" << error;

    QVector<QPair<QString, QImage>> sources;
    if (!userContent.isNull())
        sources.append(qMakePair(QStringLiteral("user"), userContent));
    sources.append(
        qMakePair(QStringLiteral("synthetic"), makeEncoderBenchSynthetic()));

    const QVector<QSize> sizes = {QSize(1280, 720), QSize(1116, 590),
                                  QSize(960, 540), QSize(640, 360)};
    constexpr int kIterations = 15;
    QVector<EncoderBenchRow> rows;

    auto qtEncode = [](const QByteArray& format, int quality, int compression) {
        return [format, quality, compression](const QImage& image) {
            QByteArray data;
            QBuffer buffer(&data);
            if (!buffer.open(QIODevice::WriteOnly))
                return QByteArray();
            QImageWriter writer(&buffer, format);
            if (quality >= 0)
                writer.setQuality(quality);
            if (compression >= 0)
                writer.setCompression(compression);
            if (!writer.write(image))
                return QByteArray();
            buffer.close();
            return data;
        };
    };
    auto qtDecode = [](const QByteArray& format) {
        return [format](const QByteArray& data) {
            QBuffer buffer;
            buffer.setData(data);
            if (!buffer.open(QIODevice::ReadOnly))
                return QImage();
            QImageReader reader(&buffer, format);
            reader.setAutoTransform(false);
            return reader.read();
        };
    };
    const auto builtinQoiEncode = [](const QImage& image) {
        return pvf::workbench::encodeQoi(image);
    };
    const auto builtinQoiDecode = [](const QByteArray& data) {
        return pvf::workbench::decodeQoi(data);
    };

    auto addRow = [&](const QString& imageName, const QString& encoder,
                      const QSize& size, const QImage& image,
                      const QImage& reference,
                      const std::function<QByteArray(const QImage&)>& encode,
                      const std::function<QImage(const QByteArray&)>& decode) {
        EncoderBenchRow row = runEncoderBench(imageName, encoder, size, image,
                                              reference, encode, decode,
                                              kIterations);
        reportEncoderBenchRow(row);
        if (row.ok)
            rows.append(row);
    };

    for (const QSize& size : sizes)
    {
        for (const auto& source : sources)
        {
            const QImage frame =
                makeEncoderBenchExpandedFrame(source.second, size);
            const QImage opaque = makeEncoderBenchOpaque(frame);

            if (hasWebp)
            {
                addRow(source.first, QStringLiteral("webp-q80"), size, frame,
                       frame, qtEncode(QByteArrayLiteral("webp"), 80, -1),
                       qtDecode(QByteArrayLiteral("webp")));
                addRow(source.first, QStringLiteral("webp-q60"), size, frame,
                       frame, qtEncode(QByteArrayLiteral("webp"), 60, -1),
                       qtDecode(QByteArrayLiteral("webp")));
                addRow(source.first, QStringLiteral("webp-q50"), size, frame,
                       frame, qtEncode(QByteArrayLiteral("webp"), 50, -1),
                       qtDecode(QByteArrayLiteral("webp")));
                addRow(source.first, QStringLiteral("webp-q80-opaque"), size,
                       opaque, opaque,
                       qtEncode(QByteArrayLiteral("webp"), 80, -1),
                       qtDecode(QByteArrayLiteral("webp")));
            }
            if (hasJpeg)
            {
                addRow(source.first, QStringLiteral("jpeg-q85"), size, opaque,
                       opaque, qtEncode(QByteArrayLiteral("jpeg"), 85, -1),
                       qtDecode(QByteArrayLiteral("jpeg")));
                addRow(source.first, QStringLiteral("jpeg-q90"), size, opaque,
                       opaque, qtEncode(QByteArrayLiteral("jpeg"), 90, -1),
                       qtDecode(QByteArrayLiteral("jpeg")));
            }
            if (hasPng)
            {
                // QImageWriter::setCompression() is 0-100 for PNG (it maps
                // to zlib 0-9).  Passing 1/6 literally gives zlib level 0,
                // so use 0/11/67 for zlib 0/1/6 and -1 for Qt's default.
                const QVector<QPair<QString, int>> pngLevels = {
                    {QStringLiteral("png-zlib0"), 0},
                    {QStringLiteral("png-zlib1"), 11},
                    {QStringLiteral("png-zlib6"), 67},
                    {QStringLiteral("png-default"), -1}};
                for (const auto& level : pngLevels)
                    addRow(source.first, level.first, size, frame, frame,
                           qtEncode(QByteArrayLiteral("png"), -1,
                                    level.second),
                           qtDecode(QByteArrayLiteral("png")));
            }
            if (hasQoi)
                addRow(source.first, QStringLiteral("qoi"), size, frame, frame,
                       qtEncode(QByteArrayLiteral("qoi"), -1, -1),
                       qtDecode(QByteArrayLiteral("qoi")));
            addRow(source.first, QStringLiteral("qoi-builtin"), size, frame,
                   frame, builtinQoiEncode, builtinQoiDecode);
        }
    }

    // One photographic frame at the default cache size: the disk-size bound
    // when a cached frame contains an image or video asset.
    const QImage photoContent = loadEncoderBenchPhoto();
    if (!photoContent.isNull())
    {
        const QSize size(1280, 720);
        const QImage frame =
            makeEncoderBenchExpandedFrame(photoContent, size);
        const QImage opaque = makeEncoderBenchOpaque(frame);
        if (hasWebp)
            addRow(QStringLiteral("photo"), QStringLiteral("webp-q80"), size,
                   frame, frame, qtEncode(QByteArrayLiteral("webp"), 80, -1),
                   qtDecode(QByteArrayLiteral("webp")));
        if (hasJpeg)
            addRow(QStringLiteral("photo"), QStringLiteral("jpeg-q90"), size,
                   opaque, opaque, qtEncode(QByteArrayLiteral("jpeg"), 90, -1),
                   qtDecode(QByteArrayLiteral("jpeg")));
        if (hasPng)
            addRow(QStringLiteral("photo"), QStringLiteral("png-zlib6"), size,
                   frame, frame,
                   qtEncode(QByteArrayLiteral("png"), -1, 67),
                   qtDecode(QByteArrayLiteral("png")));
        if (hasQoi)
            addRow(QStringLiteral("photo"), QStringLiteral("qoi"), size, frame,
                   frame, qtEncode(QByteArrayLiteral("qoi"), -1, -1),
                   qtDecode(QByteArrayLiteral("qoi")));
        addRow(QStringLiteral("photo"), QStringLiteral("qoi-builtin"), size,
               frame, frame, builtinQoiEncode, builtinQoiDecode);

        // expandPercent=0 / camera cache: the raster fills the whole frame.
        // QOI is lossless and therefore near-raw on this content.
        const QImage full = photoContent
                                .scaled(size, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (hasWebp)
            addRow(QStringLiteral("photo-full"), QStringLiteral("webp-q80"),
                   size, full, full,
                   qtEncode(QByteArrayLiteral("webp"), 80, -1),
                   qtDecode(QByteArrayLiteral("webp")));
        if (hasJpeg)
            addRow(QStringLiteral("photo-full"), QStringLiteral("jpeg-q90"),
                   size, full, full,
                   qtEncode(QByteArrayLiteral("jpeg"), 90, -1),
                   qtDecode(QByteArrayLiteral("jpeg")));
        if (hasQoi)
            addRow(QStringLiteral("photo-full"), QStringLiteral("qoi"), size,
                   full, full, qtEncode(QByteArrayLiteral("qoi"), -1, -1),
                   qtDecode(QByteArrayLiteral("qoi")));
        addRow(QStringLiteral("photo-full"), QStringLiteral("qoi-builtin"), size,
               full, full, builtinQoiEncode, builtinQoiDecode);
    }

    auto findRow = [&rows](const QString& imageName, const QString& encoder,
                           const QSize& size) -> const EncoderBenchRow* {
        for (const EncoderBenchRow& row : rows)
            if (row.imageName == imageName && row.encoder == encoder
                && row.size == size)
                return &row;
        return nullptr;
    };

    for (const auto& source : sources)
    {
        const EncoderBenchRow* base = findRow(
            source.first, QStringLiteral("webp-q80"), QSize(1280, 720));
        if (!base)
            continue;
        QString capLine;
        for (const QSize& size : sizes)
        {
            const EncoderBenchRow* row = findRow(
                source.first, QStringLiteral("webp-q80"), size);
            if (!row)
                continue;
            const double p50 = row->encodeSamples.percentile(0.5);
            const double saved =
                base->encodeSamples.percentile(0.5) > 0.0
                    ? 100.0
                          * (1.0 - p50 / base->encodeSamples.percentile(0.5))
                    : 0.0;
            capLine += QStringLiteral(" %1x%2=%3ms(-%4%)")
                           .arg(size.width())
                           .arg(size.height())
                           .arg(p50, 0, 'f', 2)
                           .arg(saved, 0, 'f', 0);
        }
        qInfo().noquote()
            << QStringLiteral("cache-encoder-bench | cap | image=%1 "
                              "webp-q80 encode-p50:%2")
                   .arg(source.first, capLine);
    }

    for (const QString& encoder :
         {QStringLiteral("webp-q80"), QStringLiteral("webp-q60"),
          QStringLiteral("webp-q50"), QStringLiteral("qoi"),
          QStringLiteral("qoi-builtin"), QStringLiteral("jpeg-q90")})
    {
        const EncoderBenchRow* row =
            findRow(QStringLiteral("user"), encoder, QSize(1280, 720));
        if (!row)
            row = findRow(QStringLiteral("synthetic"), encoder,
                          QSize(1280, 720));
        if (!row)
            continue;
        const double p50 = row->encodeSamples.percentile(0.5);
        const double p95 = row->encodeSamples.percentile(0.95);
        const auto writers = [](double ms, double budget) {
            return std::max(1, int(std::ceil(ms / budget)));
        };
        qInfo().noquote()
            << QStringLiteral("cache-encoder-bench | writers | %1 "
                              "1280x720 encode p50=%2 p95=%3 ms -> "
                              "30fps p50=%4 p95=%5 threads, 60fps p50=%6 "
                              "p95=%7 threads")
                   .arg(encoder)
                   .arg(p50, 0, 'f', 2)
                   .arg(p95, 0, 'f', 2)
                   .arg(writers(p50, 33.0))
                   .arg(writers(p95, 33.0))
                   .arg(writers(p50, 16.7))
                   .arg(writers(p95, 16.7));
    }

    return 0;
}


} // namespace

namespace pvf::tests {

void registerProfileFixtures(Registry& registry)
{
    registry.add(QStringLiteral("--cache-miss-profile"),
                 [](QApplication& app) { return cacheMissProfileTest(app); });
    registry.add(QStringLiteral("--cache-encoder-bench"),
                 [](QApplication& app) { return cacheEncoderBenchTest(app); });
}

} // namespace pvf::tests
