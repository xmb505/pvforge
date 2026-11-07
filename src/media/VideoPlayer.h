#pragma once

#include <QHash>
#include <QImage>
#include <QString>

namespace pvf {

class Project;

// Decodes the current frame of every video entity while the playhead moves.
// Frames are decoded on the UI thread; sequential playback reuses the open
// decoder, jumps seek to the previous keyframe and decode forward.
class VideoPlayer
{
public:
    struct Stream;

    VideoPlayer();
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Updates video entity images for the project frame. Safe to call every
    // play tick and on every playhead move.
    void update(Project& project, int frame);
    void clear();

private:
    bool openStream(Stream& stream, const QString& path);
    static void closeStream(Stream& stream);
    QImage frameAt(Stream& stream, double seconds, bool* changed);

    QHash<int, Stream*> m_streams;
};

} // namespace pvf
