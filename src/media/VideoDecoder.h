#pragma once

#include <QImage>
#include <QString>

namespace pvf {

// Decodes the first video frame to RGBA. Returns a null image on failure.
QImage decodeVideoFirstFrame(const QString& path, QString* error = nullptr);

} // namespace pvf
