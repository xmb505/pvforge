#pragma once

#include <QByteArray>
#include <QImage>

namespace pvf::workbench {

// Built-in QOI (Quite OK Image) codec.  The cache uses it for frames without
// image/video/HTML content: lossless, alpha-preserving and fast enough to stay
// on the store path instead of the Qt WebP plugin.
//
// Both functions are pure and need no Qt image plugin.  A null/empty input or
// malformed QOI stream returns an empty value.
QByteArray encodeQoi(const QImage& image);
QImage decodeQoi(const QByteArray& data);

} // namespace pvf::workbench
