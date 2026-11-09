#pragma once

#include "common/Types.h"

#include <QSize>
#include <QString>

namespace pvf::assets {

struct Probe
{
    QSize size;
    double durationSeconds = 0;
    int sampleRate = 0;
    int channels = 0;
};

// Reads media properties. Images use QImageReader; video and audio use
// FFmpeg. HTML has no media properties and returns an empty Probe without
// setting `error`. On failure an empty Probe is returned and `error` is set.
Probe probe(const QString& path, AssetType type, QString* error = nullptr);

} // namespace pvf::assets
