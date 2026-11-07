#pragma once

#include <QString>

namespace pvf {

struct DecoderInfo
{
    QString ffmpegVersion;
    QString mode;
    QString video;
    QString audioDecode;
    QString audioOutput;
    QString hardware;

    QString toText() const;
};

// Reports the actual decoder paths used by the current build. This is a
// read-only diagnostic; do not advertise hardware decoding that isn't wired.
DecoderInfo decoderInfo();

} // namespace pvf
