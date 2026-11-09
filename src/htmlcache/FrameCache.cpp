#include "htmlcache/FrameCache.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>

namespace pvf::htmlcache {

QByteArray encodeWebp(const QImage& image, int quality)
{
    if (image.isNull())
        return {};

    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};

    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(quality);
    if (!writer.write(image))
        return {};

    buffer.close();
    return data;
}

std::optional<QImage> decodeWebp(const QByteArray& data)
{
    if (data.isEmpty())
        return std::nullopt;

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
        return std::nullopt;

    QImageReader reader(&buffer, QByteArrayLiteral("webp"));
    if (!reader.canRead())
        return std::nullopt;

    const QImage image = reader.read();
    if (image.isNull())
        return std::nullopt;
    return image;
}

} // namespace pvf::htmlcache
