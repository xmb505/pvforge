#pragma once

#include <QString>

namespace pvf::assets {

// Extracts the first audio stream of any FFmpeg-readable file and writes a
// standard PCM RIFF/WAVE file: signed 16-bit little endian, 48 kHz, stereo.
// Samples are written frame-aligned (the data chunk is always a whole number
// of interleaved stereo frames). Works for video containers and audio files
// alike. Existing files are replaced on success and left untouched on failure.
bool extractWav(const QString& videoPath, const QString& outPath,
                QString* error = nullptr);

} // namespace pvf::assets
