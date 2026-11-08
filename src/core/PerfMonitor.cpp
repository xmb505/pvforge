#include "PerfMonitor.h"

#include <algorithm>

namespace pvf {

PerfMonitor& PerfMonitor::instance()
{
    static PerfMonitor monitor;
    return monitor;
}

void PerfMonitor::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (!enabled)
    {
        m_fps = 0.0;
        m_frameMs = 0.0;
        m_sceneMs = 0.0;
        m_viewportMs = 0.0;
        m_timelineMs = 0.0;
        m_videoMs = 0.0;
        m_entities = 0;
        m_lastTick.invalidate();
    }
    else if (m_playing)
    {
        m_lastTick.start();
    }
}

void PerfMonitor::setPlaying(bool playing)
{
    m_playing = playing;
    if (playing)
        m_lastTick.start();
    else
    {
        m_lastTick.invalidate();
        m_fps = 0.0;
    }
}

void PerfMonitor::tick()
{
    if (!m_enabled || !m_playing)
        return;
    if (m_lastTick.isValid())
    {
        const double seconds = m_lastTick.nsecsElapsed() / 1e9;
        if (seconds > 1e-4 && seconds < 2.0)
            add(m_fps, 1.0 / seconds);
    }
    m_lastTick.restart();
}

void PerfMonitor::recordFrame(double ms)
{
    if (m_enabled)
        add(m_frameMs, ms);
}

void PerfMonitor::recordScene(double ms)
{
    if (m_enabled)
        add(m_sceneMs, ms);
}

void PerfMonitor::recordViewport(double ms)
{
    if (m_enabled)
        add(m_viewportMs, ms);
}

void PerfMonitor::recordTimeline(double ms)
{
    if (m_enabled)
        add(m_timelineMs, ms);
}

void PerfMonitor::recordVideo(double ms)
{
    if (m_enabled)
        add(m_videoMs, ms);
}

void PerfMonitor::add(double& target, double sample)
{
    sample = std::max(0.0, sample);
    target = target <= 0.0 ? sample : target * 0.85 + sample * 0.15;
}

PerfSnapshot PerfMonitor::snapshot() const
{
    PerfSnapshot result;
    result.enabled = m_enabled;
    result.fps = m_fps;
    result.frameMs = m_frameMs;
    result.sceneMs = m_sceneMs;
    result.viewportMs = m_viewportMs;
    result.timelineMs = m_timelineMs;
    result.videoMs = m_videoMs;
    result.entities = m_entities;
    return result;
}

} // namespace pvf
