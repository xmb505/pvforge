#pragma once

#include "core/Project.h"

#include <QBuffer>
#include <QByteArray>
#include <QObject>

class QAudioSink;

namespace pvf {

// Offline-mixes the project audio for the current playback range and feeds
// it to QAudioSink. The mix is rebuilt on every play/seek, which keeps the
// engine simple and avoids locking the UI while audio is running.
class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(Project* project, QObject* parent = nullptr);

    // Starts audio at startFrame. Returns false when the project has no
    // audible audio or no output device; callers then use the timer clock.
    bool play(int startFrame);

    void stop();

    bool isRunning() const { return m_running; }
    double positionSeconds() const;
    double endSeconds() const { return m_endSeconds; }

    static QByteArray mixRange(const Project& project, double startSeconds,
                               double endSeconds);

private:
    Project* m_project = nullptr;
    QAudioSink* m_sink = nullptr;
    QBuffer m_buffer;
    QByteArray m_pcm;
    bool m_running = false;
    double m_startSeconds = 0.0;
    double m_endSeconds = 0.0;
};

} // namespace pvf
