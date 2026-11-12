#include "workbench/Qoi.h"

#include <QtGlobal>

#include <cstring>
#include <limits>

namespace pvf::workbench {
namespace {

constexpr int kHeaderSize = 14;
constexpr int kPaddingSize = 8;

constexpr quint8 kOpIndex = 0x00;
constexpr quint8 kOpDiff = 0x40;
constexpr quint8 kOpLuma = 0x80;
constexpr quint8 kOpRun = 0xc0;
constexpr quint8 kOpRgb = 0xfe;
constexpr quint8 kOpRgba = 0xff;

constexpr int kMaxRun = 62;

inline int hashIndex(quint8 r, quint8 g, quint8 b, quint8 a)
{
    return (int(r) * 3 + int(g) * 5 + int(b) * 7 + int(a) * 11) & 63;
}

} // namespace

QByteArray encodeQoi(const QImage& image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return {};

    // QOI stores straight (non-premultiplied) RGBA.  Rendering produces
    // premultiplied images, so use Qt's conversion here instead of leaking the
    // premultiplied bytes into the file.
    const QImage rgba =
        image.format() == QImage::Format_RGBA8888
            ? image
            : image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
        return {};

    const int width = rgba.width();
    const int height = rgba.height();
    const qint64 pixels = qint64(width) * qint64(height);
    if (pixels <= 0)
        return {};

    const bool alpha = rgba.hasAlphaChannel();
    const int channels = alpha ? 4 : 3;
    // Worst case is one RGB/RGBA opcode per pixel; a run never grows that.
    const qint64 worstCase = kHeaderSize + pixels * (channels + 1) + kPaddingSize;
    if (worstCase > std::numeric_limits<int>::max())
        return {};

    QByteArray out;
    out.resize(int(worstCase));
    uchar* dst = reinterpret_cast<uchar*>(out.data());
    int pos = 0;

    dst[pos++] = 'q';
    dst[pos++] = 'o';
    dst[pos++] = 'i';
    dst[pos++] = 'f';
    const auto writeU32 = [&dst, &pos](quint32 value) {
        dst[pos++] = uchar(value >> 24);
        dst[pos++] = uchar(value >> 16);
        dst[pos++] = uchar(value >> 8);
        dst[pos++] = uchar(value);
    };
    writeU32(quint32(width));
    writeU32(quint32(height));
    dst[pos++] = uchar(channels);
    dst[pos++] = 0; // sRGB with linear alpha

    quint32 index[64] = {};
    quint8 prev[4] = {0, 0, 0, 255};
    int run = 0;

    for (int y = 0; y < height; ++y)
    {
        const uchar* line = rgba.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            const quint8 r = line[0];
            const quint8 g = line[1];
            const quint8 b = line[2];
            const quint8 a = alpha ? line[3] : quint8(255);
            line += 4;

            if (r == prev[0] && g == prev[1] && b == prev[2] && a == prev[3])
            {
                ++run;
                if (run == kMaxRun)
                {
                    dst[pos++] = uchar(kOpRun | (run - 1));
                    run = 0;
                }
                continue;
            }

            if (run > 0)
            {
                dst[pos++] = uchar(kOpRun | (run - 1));
                run = 0;
            }

            const int indexPos = hashIndex(r, g, b, a);
            const quint32 pixel = quint32(r) | (quint32(g) << 8)
                                  | (quint32(b) << 16) | (quint32(a) << 24);
            if (index[indexPos] == pixel)
            {
                dst[pos++] = uchar(kOpIndex | indexPos);
            }
            else
            {
                index[indexPos] = pixel;

                const int dr = int(qint8(r - prev[0]));
                const int dg = int(qint8(g - prev[1]));
                const int db = int(qint8(b - prev[2]));
                if (a == prev[3])
                {
                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1
                        && db >= -2 && db <= 1)
                    {
                        dst[pos++] = uchar(kOpDiff | ((dr + 2) << 4)
                                           | ((dg + 2) << 2) | (db + 2));
                    }
                    else
                    {
                        const int drDg = dr - dg;
                        const int dbDg = db - dg;
                        if (dg >= -32 && dg <= 31 && drDg >= -8 && drDg <= 7
                            && dbDg >= -8 && dbDg <= 7)
                        {
                            dst[pos++] = uchar(kOpLuma | (dg + 32));
                            dst[pos++] = uchar(((drDg + 8) << 4)
                                               | (dbDg + 8));
                        }
                        else
                        {
                            dst[pos++] = kOpRgb;
                            dst[pos++] = r;
                            dst[pos++] = g;
                            dst[pos++] = b;
                        }
                    }
                }
                else
                {
                    dst[pos++] = kOpRgba;
                    dst[pos++] = r;
                    dst[pos++] = g;
                    dst[pos++] = b;
                    dst[pos++] = a;
                }
            }

            prev[0] = r;
            prev[1] = g;
            prev[2] = b;
            prev[3] = a;
        }
    }

    if (run > 0)
        dst[pos++] = uchar(kOpRun | (run - 1));

    // Spec end marker: seven zero bytes followed by one 0x01 byte.
    for (int i = 0; i < kPaddingSize - 1; ++i)
        dst[pos++] = 0;
    dst[pos++] = 1;

    out.resize(pos);
    return out;
}

QImage decodeQoi(const QByteArray& data)
{
    if (data.size() < kHeaderSize + kPaddingSize)
        return {};

    const uchar* src = reinterpret_cast<const uchar*>(data.constData());
    if (std::memcmp(src, "qoif", 4) != 0)
        return {};

    const auto readU32 = [src](int offset) {
        return (quint32(src[offset]) << 24) | (quint32(src[offset + 1]) << 16)
               | (quint32(src[offset + 2]) << 8) | quint32(src[offset + 3]);
    };
    const quint32 width = readU32(4);
    const quint32 height = readU32(8);
    const quint8 channels = src[12];
    const quint8 colorspace = src[13];

    if (width == 0 || height == 0 || width > quint32(std::numeric_limits<int>::max())
        || height > quint32(std::numeric_limits<int>::max()))
        return {};
    if (channels != 3 && channels != 4)
        return {};
    if (colorspace > 1)
        return {};

    const qint64 pixels = qint64(width) * qint64(height);
    if (pixels > qint64(std::numeric_limits<int>::max()))
        return {};

    QImage image(int(width), int(height),
                 channels == 4 ? QImage::Format_RGBA8888
                               : QImage::Format_RGB888);
    if (image.isNull())
        return {};

    // The last eight bytes are the spec end marker; ops must stay before them.
    const int end = data.size() - kPaddingSize;
    int pos = kHeaderSize;

    quint8 index[64][4] = {};
    quint8 prev[4] = {0, 0, 0, 255};
    int run = 0;

    int x = 0;
    int y = 0;
    uchar* line = image.scanLine(0);
    int lineOffset = 0;

    for (qint64 i = 0; i < pixels; ++i)
    {
        if (run > 0)
        {
            --run;
        }
        else
        {
            if (pos >= end)
                return {};
            const quint8 b1 = src[pos++];
            if (b1 == kOpRgb)
            {
                if (pos + 3 > end)
                    return {};
                prev[0] = src[pos++];
                prev[1] = src[pos++];
                prev[2] = src[pos++];
            }
            else if (b1 == kOpRgba)
            {
                if (pos + 4 > end)
                    return {};
                prev[0] = src[pos++];
                prev[1] = src[pos++];
                prev[2] = src[pos++];
                prev[3] = src[pos++];
            }
            else
            {
                switch (b1 & 0xc0)
                {
                case kOpIndex:
                    std::memcpy(prev, index[b1 & 0x3f], 4);
                    break;
                case kOpDiff:
                    prev[0] = uchar(int(prev[0]) + ((b1 >> 4) & 0x03) - 2);
                    prev[1] = uchar(int(prev[1]) + ((b1 >> 2) & 0x03) - 2);
                    prev[2] = uchar(int(prev[2]) + (b1 & 0x03) - 2);
                    break;
                case kOpLuma: {
                    if (pos >= end)
                        return {};
                    const quint8 b2 = src[pos++];
                    const int dg = int(b1 & 0x3f) - 32;
                    prev[0] = uchar(int(prev[0]) + dg
                                    + int((b2 >> 4) & 0x0f) - 8);
                    prev[1] = uchar(int(prev[1]) + dg);
                    prev[2] = uchar(int(prev[2]) + dg + int(b2 & 0x0f) - 8);
                    break;
                }
                case kOpRun:
                    run = int(b1 & 0x3f);
                    break;
                default:
                    return {};
                }
            }
        }

        line[lineOffset] = prev[0];
        line[lineOffset + 1] = prev[1];
        line[lineOffset + 2] = prev[2];
        if (channels == 4)
            line[lineOffset + 3] = prev[3];

        std::memcpy(index[hashIndex(prev[0], prev[1], prev[2], prev[3])],
                    prev, 4);

        lineOffset += channels;
        if (++x == int(width))
        {
            x = 0;
            ++y;
            if (y < int(height))
            {
                line = image.scanLine(y);
                lineOffset = 0;
            }
        }
    }

    return image;
}

} // namespace pvf::workbench
