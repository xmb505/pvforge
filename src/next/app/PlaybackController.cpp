#include "next/app/PlaybackController.h"

#include "audio2/Audio2.h"
#include "next/core/Document.h"

#include <QTimer>

#include <algorithm>

namespace pvf::next {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PlaybackController::tick);
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

    int end = 0;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        if (it.value().parentUuid.isEmpty())
            end = std::max(end, it.value().end);
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
    if (m_audio)
        m_audio->stop();
    if (wasPlaying)
        emit playbackStateChanged(false);
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

    const int current = m_document->playhead();
    if (current >= m_endFrame) {
        m_advancing = true;
        m_document->setPlayhead(m_endFrame);
        m_advancing = false;
        pause();
        return;
    }

    const int next = std::min(current + 1, m_endFrame);
    m_advancing = true;
    m_document->setPlayhead(next);
    m_advancing = false;

    if (next >= m_endFrame)
        pause();
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
    if (!m_playing || m_advancing || !m_document)
        return;

    const int frame = m_document->playhead();
    if (frame >= m_endFrame) {
        pause();
        return;
    }
    restartAudio(frame);
}

} // namespace pvf::next
