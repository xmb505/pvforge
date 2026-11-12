#pragma once

#include "common/Types.h"
#include "next/settings/SettingsStore.h"
#include "render2/Render.h"
#include "solver/Solver.h"
#include "workbench/Cache.h"
#include "workbench/MetaBuilder.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace pvf::next {

class ProjectDocument;

// Progress of an explicitly requested cache pass.  `done` counts frames that
// need no further work (meta hit, cached, or skipped for missing media); a
// frame held for a pending HTML render is retried instead of being counted
// twice.  Nothing starts this pass automatically.
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
// Frames are cached strictly on demand: the viewport calls lookup() for every
// frame it is about to draw; a miss renders normally and store() writes the
// picture plus its meta.  Opening a project, switching cameras or editing does
// not scan anything, and the cache directory is created by the first store.
// Returning to a frame later is a plain meta hit.
//
// startScanNow() remains available for an explicit "cache all"/"cache to
// playhead" command (and for tests).  It runs on the UI thread with a 20 ms
// QTimer because the render providers (video sequences and the HTML frontend)
// belong to the UI thread.  One frame is built and rendered per tick; frames
// whose meta already matches are skipped without rendering.
//
// A scan frame containing an HTML/video asset whose provider returns an empty
// image is never stored: render2 drew a placeholder and caching that would
// poison the frame.  For HTML the pass holds the frame and retries after
// MediaServices emits htmlFrameReady (notifyHtmlFrameReady), so every HTML
// frame gets a real picture in the first pass.  Missing video frames are
// skipped and picked up by the next manual pass.
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
    // Empty until the first store (or an explicit manual scan) creates the
    // per-camera directory.
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

    // Viewport hooks.  lookup() builds the frame meta and reads/decodes QOI
    // (vector frames) or WebP (raster frames) on the UI thread; it never
    // creates the cache directory.  store() writes synchronously (~2 ms QOI,
    // 5-20 ms WebP) and creates the directory on the first miss.  No timer or
    // signal starts a scan on its own.
    QImage lookup(int frame, const Uuid& cameraUuid, bool* hit);
    void store(int frame, const Uuid& cameraUuid, const QImage& image);

    // Explicit cache pass.  Nothing calls these automatically: they back the
    // optional "cache all", "cache to playhead" and "rescan" commands.
    // startScanNow() runs from startFrame to endFrame; endFrame < 0 means the
    // last frame of project content.
    void startScanNow(int startFrame = 0, int endFrame = -1);
    void cancelScan();

    // Playback pauses an explicitly started pass; stopping resumes it from the
    // held frame.  It does not start a pass.
    void setPlaybackActive(bool active);

    // Called when MediaServices finished an asynchronous HTML frame.  Releases
    // a scan frame held waiting for HTML.
    void notifyHtmlFrameReady();

    // Drops every cached frame of this camera.  Does not start a new scan;
    // frames come back on demand.
    void clearCache();

    // Borrowed solver-result map owned by EditorSession.  Frame-meta builds
    // and cache renders consult it before solving a segment, so an image-cache
    // hit only re-serialises poses instead of re-running physics for every
    // active entity.  The owner clears the map after edits; nullptr detaches.
    // Must be cleared before the owner's map dies (EditorSession keeps the map
    // alive past this service).
    void setSolverResults(QHash<QString, solver::Result>* results);

    workbench::CacheStats stats() const;
    WorkbenchPrewarmProgress progress() const { return m_progress; }
    const workbench::Options& options() const { return m_options; }

    // Test accessors required by the shell tests.
    workbench::CacheStats workbenchStatsForTest() const { return stats(); }
    QString workbenchCacheDirForTest() const { return cacheDirectory(); }
    WorkbenchPrewarmProgress prewarmProgressForTest() const { return progress(); }
    QImage lookupForTest(int frame, const Uuid& cameraUuid = Uuid());
    // Builds the meta exactly as lookup()/store() do, without requiring an
    // existing cache directory.
    workbench::FrameMeta buildMetaForTest(int frame);

private:
    void tick();
    void onDocumentChanged();
    void onHtmlWaitTimeout();
    void ensureCache();
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
    QString m_cacheRoot;
    workbench::Options m_options;
    std::unique_ptr<workbench::Cache> m_cache;
    QHash<QString, solver::Result>* m_sharedSolverResults = nullptr;
    bool m_enabled = false;

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
