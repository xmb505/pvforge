#pragma once

#include <QVector>
#include <QtGlobal>

namespace pvf {

// Decoded audio used both for playback and waveform drawing.
// Samples are interleaved 48 kHz stereo int16.
struct AudioClip
{
    double duration = 0.0;
    int sampleRate = 48000;
    int channels = 2;
    QVector<qint16> samples;
    QVector<float> peaks;   // one peak per millisecond, 0..1

    bool valid() const { return duration > 0.0 && !peaks.isEmpty(); }
};

} // namespace pvf
