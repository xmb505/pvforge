#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace pvf::audio2 {
class Playback;
}

namespace pvf::next {

class Document;

// Monotonic-clock playback clock for the new app.  The audio soundtrack is a
// best-effort follower: the playhead is derived from elapsed monotonic time,
// so a slow timer or a rendering stall skips frames instead of making the
// picture drift away from the audio.  Audio failures never stop playback.
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

#ifdef PVFORGE_TEST_HOOKS
    // Deterministic monotonic clock for tests: returns nanoseconds since an
    // arbitrary epoch.  Production uses QElapsedTimer.
    void setClockForTest(std::function<qint64()> clock);
    void tickForTest() { tick(); }
    // Playhead computed by the last tick (before clamping/advancing).
    int clockTargetForTest() const { return m_lastTarget; }
#endif

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
    qint64 clockNs() const;
    void anchorClock(int frame, qint64 nowNs);

    Document* m_document = nullptr;
    std::unique_ptr<audio2::Playback> m_audio;
    QTimer* m_timer = nullptr;
    QTimer m_watchdog;
    QElapsedTimer m_lastFrame;
    // Monotonic playback clock: frame = anchorFrame + (now - anchorNs) * fps.
    QElapsedTimer m_clock;
    QMetaObject::Connection m_playheadConnection;
    bool m_playing = false;
    bool m_advancing = false;
    int m_endFrame = 0;
    int m_fps = 30;
    int m_watchdogMs = 3000;
    int m_anchorFrame = 0;
    qint64 m_anchorNs = 0;
    int m_lastTarget = 0;
    qint64 m_lastAudioResyncNs = 0;
#ifdef PVFORGE_TEST_HOOKS
    std::function<qint64()> m_clockForTest;
#endif
};

} // namespace pvf::next
