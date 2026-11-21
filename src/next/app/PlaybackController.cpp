#include "next/app/PlaybackController.h"

#include "audio2/Audio2.h"
#include "next/core/Document.h"

#include <QTimer>

#include <algorithm>
#include <utility>

namespace pvf::next {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PlaybackController::tick);

    m_watchdog.setTimerType(Qt::PreciseTimer);
    m_watchdog.setInterval(100);
    connect(&m_watchdog, &QTimer::timeout, this,
            &PlaybackController::checkWatchdog);
}

PlaybackController::~PlaybackController()
{
    setDocument(nullptr);
}

void PlaybackController::setDocument(Document* document)
{
    if (m_document == document)
        return;

    pause();
    if (m_playheadConnection)
        QObject::disconnect(m_playheadConnection);
    m_playheadConnection = {};

    m_document = document;
    m_audio.reset();

    if (m_document) {
        m_audio = std::make_unique<audio2::Playback>(
            m_document->project(), m_document->assets(), this);
        m_playheadConnection = connect(
            m_document, &Document::playheadChanged, this,
            &PlaybackController::onPlayheadChanged);
    }
}

int PlaybackController::contentEnd() const
{
    if (!m_document)
        return 0;

    // Exclusive boundary: the last renderable frame is the furthest root
    // entity's closed `end`, so the boundary sits at end + 1.
    int end = 0;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        if (it.value().parentUuid.isEmpty())
            end = std::max(end, it.value().end + 1);
    }
    return std::max(0, end);
}

void PlaybackController::play()
{
    if (!m_document || m_playing)
        return;

    const int end = contentEnd();
    const int start = std::max(0, m_document->playhead());
    if (end <= start)
        return;

    m_fps = std::max(1, m_document->project().settings.fps);
    m_endFrame = end;
    m_playing = true;
    m_timer->setInterval(std::max(1, 1000 / m_fps));
    m_timer->start();
    anchorClock(start, clockNs());
    m_lastAudioResyncNs = 0;
    m_lastFrame.start();
    m_watchdog.start();

    if (m_audio) {
        QString error;
        // Best effort: no audio output device is not an error for the
        // picture timeline.
        m_audio->play(start, end, m_fps, &error);
    }

    emit playbackStateChanged(true);
}

void PlaybackController::pause()
{
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_timer->stop();
    m_watchdog.stop();
    if (m_audio)
        m_audio->stop();
    if (wasPlaying)
        emit playbackStateChanged(false);
}

void PlaybackController::scrub(int fromFrame, int toFrame)
{
    if (!m_document || !m_audio || m_playing || !m_audioScrubEnabled)
        return;
    if (fromFrame == toFrame)
        return;
    const int fps = std::max(1, m_document->project().settings.fps);
    m_audio->scrub(fromFrame, toFrame, fps);
}

void PlaybackController::noteFramePresented()
{
    m_lastFrame.start();
}

void PlaybackController::setWatchdogMs(int ms)
{
    m_watchdogMs = std::max(1, ms);
}

void PlaybackController::checkWatchdog()
{
    if (!m_playing)
    {
        m_watchdog.stop();
        return;
    }

    // The watchdog only feeds on real frames via noteFramePresented().  The
    // clock timer advancing or a media provider being busy is not evidence
    // that anything reached the screen.
    if (m_lastFrame.isValid() && m_lastFrame.elapsed() > m_watchdogMs)
    {
        pause();
        emit stalled(tr("帧渲染超时，已暂停"));
    }
}

void PlaybackController::toggle()
{
    if (m_playing)
        pause();
    else
        play();
}

void PlaybackController::tick()
{
    if (!m_document || !m_playing) {
        pause();
        return;
    }

    const qint64 elapsedNs = std::max<qint64>(0, clockNs() - m_anchorNs);
    // Integer frame from elapsed monotonic time: 60fps lands on the correct
    // frame at any tick granularity, and a stalled UI thread skips frames
    // instead of slowing the picture down (issue #130).
    const qint64 frameOffset = (elapsedNs * qint64(m_fps)) / 1000000000LL;
    int target = m_anchorFrame + int(frameOffset);
    // m_endFrame is exclusive; clamp the picture to the last real frame so the
    // empty boundary frame is never presented.
    const bool finished = target >= m_endFrame;
    target = std::min(target, std::max(0, m_endFrame - 1));
    // The clock is monotonic; a weird clock reading must never rewind the
    // playhead or retrigger the same frame.
    target = std::max(target, m_lastTarget);
    m_lastTarget = target;

    const int current = m_document->playhead();
    if (target > current) {
        m_advancing = true;
        m_document->setPlayhead(target);
        m_advancing = false;
    }

    if (m_audio && m_audio->isPlaying()) {
        const int audioFrame = m_audio->positionFrame();
        const int tolerance = std::max(2, m_fps / 10);
        if (audioFrame >= 0 && std::abs(audioFrame - target) > tolerance
            && elapsedNs - m_lastAudioResyncNs >= 1000000000LL)
        {
            // The soundtrack drifted more than ~100 ms (device latency, a
            // manual seek on the audio side); restart it at the picture clock
            // instead of letting the two diverge for the rest of the clip.
            m_lastAudioResyncNs = elapsedNs;
            restartAudio(target);
        }
    }

    if (finished)
        pause();
}

qint64 PlaybackController::clockNs() const
{
#ifdef PVFORGE_TEST_HOOKS
    if (m_clockForTest)
        return m_clockForTest();
#endif
    return m_clock.nsecsElapsed();
}

void PlaybackController::anchorClock(int frame, qint64 nowNs)
{
    m_anchorFrame = frame;
    m_anchorNs = nowNs;
    m_lastTarget = frame;
}

void PlaybackController::restartAudio(int frame)
{
    if (!m_audio)
        return;
    m_audio->stop();
    QString error;
    m_audio->play(frame, m_endFrame, m_fps, &error);
}

void PlaybackController::onPlayheadChanged()
{
    if (!m_document || m_advancing)
        return;

    // A manual seek resets the stall clock.  Playback ticks are excluded above,
    // so an advancing clock can never feed the watchdog.
    if (!m_playing)
        return;
    m_lastFrame.start();

    const int frame = m_document->playhead();
    if (frame >= m_endFrame) {
        pause();
        return;
    }

    // A seek re-anchors the monotonic clock at the requested frame; without
    // this the old elapsed time would immediately jump the playhead back to
    // where the playback was before the seek.
    anchorClock(frame, clockNs());
    m_lastAudioResyncNs = 0;
    restartAudio(frame);
}

#ifdef PVFORGE_TEST_HOOKS
void PlaybackController::setClockForTest(std::function<qint64()> clock)
{
    m_clockForTest = std::move(clock);
}
#endif

} // namespace pvf::next
