#pragma once

#include <QImage>
#include <QString>

namespace pvf {

// Decodes the first video frame to RGBA. Returns a null image on failure.
// duration (seconds) is filled from the container when available.
QImage decodeVideoFirstFrame(const QString& path, QString* error = nullptr,
                             double* duration = nullptr);

} // namespace pvf
