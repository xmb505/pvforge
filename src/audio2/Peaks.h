#pragma once

#include <QString>
#include <QVector>

namespace pvf::audio2 {

// Decodes any audio file FFmpeg understands and returns `buckets` absolute peak
// values (0..1), one per uniform slice of the requested source window.
//
// The second overload reads only [startSeconds, startSeconds + durationSeconds).
// A duration <= 0 means "until the end of the file". Decoding never touches an
// audio output device. On failure the returned vector is empty and *error is
// set; a successful decode of silence still returns `buckets` zero values.
QVector<float> audioPeaks(const QString& path, int buckets,
                          QString* error = nullptr);
QVector<float> audioPeaks(const QString& path, int buckets, double startSeconds,
                          double durationSeconds, QString* error = nullptr);

} // namespace pvf::audio2
