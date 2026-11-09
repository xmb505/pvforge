#pragma once

#include <QObject>

#include <memory>

class QTimer;

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

signals:
    void playbackStateChanged(bool playing);

private:
    void tick();
    void restartAudio(int frame);
    void onPlayheadChanged();

    Document* m_document = nullptr;
    std::unique_ptr<audio2::Playback> m_audio;
    QTimer* m_timer = nullptr;
    QMetaObject::Connection m_playheadConnection;
    bool m_playing = false;
    bool m_advancing = false;
    int m_endFrame = 0;
    int m_fps = 30;
};

} // namespace pvf::next
