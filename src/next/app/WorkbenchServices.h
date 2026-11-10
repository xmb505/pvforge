#pragma once

#include "common/Types.h"
#include "next/settings/SettingsStore.h"
#include "render2/Render.h"
#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace pvf::next {

class ProjectDocument;

// Progress of the UI-thread prewarm pass.  `done` counts frames that need no
// further work (meta hit, cached, or skipped for missing media); a frame held
// for a pending HTML render is retried instead of being counted twice.
struct WorkbenchPrewarmProgress
{
    bool scanning = false;
    bool waitingForHtml = false;
    // Scan is holding its current frame because playback is running.
    bool paused = false;
    int done = 0;
    int total = 0;
    int frame = 0;
    int cached = 0;
    int hits = 0;
    int skipped = 0;
    int failed = 0;
};

// Whole-frame workbench cache for one project and one camera.
//
// Owns the workbench::Cache (at <project>/.cache/workbench) and runs the
// prewarm pass on the UI thread with a 20 ms QTimer, because the render
// providers (video sequences and the HTML frontend) belong to the UI thread.
// One frame is built and rendered per tick; frames whose meta already matches
// are skipped without rendering.
//
// A prewarm frame containing an HTML/video asset whose provider returns an
// empty image is never stored: render2 drew a placeholder and caching that
// would poison the frame.  For HTML the pass holds the frame and retries after
// MediaServices emits htmlFrameReady (notifyHtmlFrameReady), so every HTML
// frame gets a real picture in the first pass.  Missing video frames are
// skipped and picked up by the next scan.
class WorkbenchServices : public QObject
{
    Q_OBJECT

public:
    struct Providers
    {
        render2::FrameProvider videoFrame;
        render2::FrameProvider htmlFrame;
        // True while the HTML frontend is still rendering a queued miss.
        std::function<bool()> htmlPending;
    };

    WorkbenchServices(ProjectDocument* document, const AppSettings& settings,
                      const Uuid& cameraUuid, Providers providers,
                      QObject* parent = nullptr);
    ~WorkbenchServices() override;

    WorkbenchServices(const WorkbenchServices&) = delete;
    WorkbenchServices& operator=(const WorkbenchServices&) = delete;

    bool enabled() const { return m_enabled; }
    bool cacheValid() const;
    QString cacheError() const;
    QString cacheDirectory() const;
    Uuid cameraUuid() const { return m_cameraUuid; }
    int scalePercent() const { return m_options.scalePercent; }
    int expandPercent() const { return m_options.expandPercent; }
    bool transparentOutsideCanvas() const
    {
        return m_options.transparentOutsideCanvas;
    }
    QSize targetSize() const { return m_options.targetSize; }
    // Frames 0..totalFrames()-1 cover the project content.
    int totalFrames() const;

    // Viewport hooks.  lookup() may build the frame meta and read/decode the
    // WebP on the UI thread; store() writes synchronously (5-20 ms WebP
    // encode), which is acceptable until a background writer lands.
    QImage lookup(int frame, const Uuid& cameraUuid, bool* hit);
    void store(int frame, const Uuid& cameraUuid, const QImage& image);

    // Full pass from 0 after a 500 ms debounce (open project, camera switch,
    // settings change).  startScanNow() ignores the debounce and can stop at
    // an explicit end frame ("缓存到播放头").  endFrame < 0 means the last
    // frame of project content.
    void scheduleScan();
    void startScanNow(int startFrame = 0, int endFrame = -1);
    void cancelScan();

    // Playback pauses the scan; stopping resumes it from the held frame.
    void setPlaybackActive(bool active);

    // Called when MediaServices finished an asynchronous HTML frame.  Releases
    // a frame held waiting for HTML.
    void notifyHtmlFrameReady();

    // Drops every cached frame of this camera, then schedules a fresh scan.
    void clearCache();

    workbench::CacheStats stats() const;
    WorkbenchPrewarmProgress progress() const { return m_progress; }
    const workbench::Options& options() const { return m_options; }

    // Test accessors required by the shell tests.
    workbench::CacheStats workbenchStatsForTest() const { return stats(); }
    QString workbenchCacheDirForTest() const { return cacheDirectory(); }
    WorkbenchPrewarmProgress prewarmProgressForTest() const { return progress(); }
    QImage lookupForTest(int frame, const Uuid& cameraUuid = Uuid());

private:
    void tick();
    void onDocumentChanged();
    void onHtmlWaitTimeout();
    void beginScan();
    void finishScan();
    void enterHtmlWait();
    void invalidateMeta();
    workbench::FrameMeta buildMeta(int frame);
    QImage renderForCache(int frame, bool* htmlMissing, bool* videoMissing);
    int contentEnd() const;
    bool htmlPending() const;

    ProjectDocument* m_document = nullptr;
    Providers m_providers;
    Uuid m_cameraUuid;
    workbench::Options m_options;
    std::unique_ptr<workbench::Cache> m_cache;
    bool m_enabled = false;

    QTimer m_debounce;
    QTimer m_tick;
    QTimer m_htmlWaitTimeout;

    bool m_playbackActive = false;
    bool m_waitingHtml = false;
    // After one HTML wait timeout the rest of the pass skips HTML misses
    // quickly instead of stalling on a broken renderer.
    bool m_htmlWaitDisabled = false;

    int m_scanStart = 0;
    int m_scanEnd = -1;
    int m_scanFrame = 0;
    WorkbenchPrewarmProgress m_progress;

    workbench::FrameMeta m_lastMeta;
    int m_lastMetaFrame = -1;
    bool m_lastMetaValid = false;
};

} // namespace pvf::next
