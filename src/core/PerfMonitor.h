#pragma once

#include <QElapsedTimer>

namespace pvf {

struct PerfSnapshot
{
    bool enabled = false;
    double fps = 0.0;
    double frameMs = 0.0;      // one playback tick (setFrame + updates)
    double sceneMs = 0.0;      // entities drawn by the viewport
    double viewportMs = 0.0;   // whole viewport paint
    double timelineMs = 0.0;   // timeline paint
    double videoMs = 0.0;      // video decode/update
    int entities = 0;
};

// Lightweight rolling metrics for the editor's frame loop. All calls happen
// on the GUI thread; values are smoothed so the status label does not jump.
class PerfMonitor
{
public:
    static PerfMonitor& instance();

    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }

    void setPlaying(bool playing);
    void tick();   // called once per playback frame

    void recordFrame(double ms);
    void recordScene(double ms);
    void recordViewport(double ms);
    void recordTimeline(double ms);
    void recordVideo(double ms);
    void setEntityCount(int count) { m_entities = count; }

    PerfSnapshot snapshot() const;

private:
    void add(double& target, double sample);

    bool m_enabled = false;
    bool m_playing = false;
    QElapsedTimer m_lastTick;
    double m_fps = 0.0;
    double m_frameMs = 0.0;
    double m_sceneMs = 0.0;
    double m_viewportMs = 0.0;
    double m_timelineMs = 0.0;
    double m_videoMs = 0.0;
    int m_entities = 0;
};

} // namespace pvf
