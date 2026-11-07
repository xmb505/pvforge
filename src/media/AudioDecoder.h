#pragma once

#include "media/AudioClip.h"

#include <QImage>
#include <QString>

#include <memory>

namespace pvf {

struct AudioDecodeResult
{
    std::shared_ptr<AudioClip> clip;
    QString error;
};

// Decodes any FFmpeg-supported audio file and resamples it to 48 kHz stereo
// int16. Also builds the 1 ms waveform peak envelope. Safe for worker threads.
AudioDecodeResult decodeAudioFile(const QString& path);

// Small waveform bitmap for the library/template preview.
QImage renderAudioThumbnail(const AudioClip& clip, const QSize& size,
                            const QColor& accent = QColor(90, 190, 255),
                            const QColor& background = QColor(30, 30, 36));

} // namespace pvf
