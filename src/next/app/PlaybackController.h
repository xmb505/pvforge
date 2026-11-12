#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

namespace pvf::audio2 {
class Playback;
}

namespace pvf::next {

class Document;

// UI-timer clock for the new app.  It advances Document::playhead() once per
// frame and mirrors the same range into audio2::Playback as a best-effort
// soundtrack.  Audio failures never stop playback.
class PlaybackController : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController() override;

    // Switches the document.  Stops the timer/audio and recreates the audio
    // playback object bound to the new project and asset store.
    void setDocument(Document* document);

    void play();
    void pause();
    void stop() { pause(); }
    void toggle();
    bool isPlaying() const { return m_playing; }

    // Highest end frame among root entities.  0 means no content.
    int contentEnd() const;

    // Frame watchdog (docs/AI/ENTITY_MODEL.md 6.5).  While playing, every real
    // frame that reaches the screen must call noteFramePresented(); when no
    // such frame arrives for longer than watchdogMs(), playback pauses and
    // stalled() fires.  The clock timer never feeds the watchdog itself.
    void noteFramePresented();
    void setWatchdogMs(int ms);
    int watchdogMs() const { return m_watchdogMs; }

signals:
    void playbackStateChanged(bool playing);
    // Playback paused because no frame reached the screen within watchdogMs().
    // The manual play command is required to resume; nothing restarts itself.
    void stalled(const QString& message);

private:
    void checkWatchdog();
    void tick();
    void restartAudio(int frame);
    void onPlayheadChanged();

    Document* m_document = nullptr;
    std::unique_ptr<audio2::Playback> m_audio;
    QTimer* m_timer = nullptr;
    QTimer m_watchdog;
    QElapsedTimer m_lastFrame;
    QMetaObject::Connection m_playheadConnection;
    bool m_playing = false;
    bool m_advancing = false;
    int m_endFrame = 0;
    int m_fps = 30;
    int m_watchdogMs = 3000;
};

} // namespace pvf::next
