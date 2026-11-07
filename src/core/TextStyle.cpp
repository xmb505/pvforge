#include "TextStyle.h"

namespace pvf {

TextStyle TextStyle::fromData(const QVariantMap& data)
{
    TextStyle style;
    style.text = data.value(QStringLiteral("text")).toString();
    style.fontFamily = data.value(QStringLiteral("font")).toString();
    style.pixelSize = data.value(QStringLiteral("size"), 96).toInt();
    const QColor color = data.value(QStringLiteral("color")).value<QColor>();
    style.color = color.isValid() ? color : QColor(Qt::white);
    style.align = data.value(QStringLiteral("align"), int(Qt::AlignCenter)).toInt();
    style.lineHeight = data.value(QStringLiteral("lineHeight"), 1.0).toDouble();
    style.padding = data.value(QStringLiteral("padding"), 6.0).toDouble();
    style.boxWidth = data.value(QStringLiteral("boxWidth"), 0.0).toDouble();
    return style;
}

void TextStyle::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("text")] = text;
    data[QStringLiteral("font")] = fontFamily;
    data[QStringLiteral("size")] = pixelSize;
    data[QStringLiteral("color")] = color;
    data[QStringLiteral("align")] = align;
    data[QStringLiteral("lineHeight")] = lineHeight;
    data[QStringLiteral("padding")] = padding;
    data[QStringLiteral("boxWidth")] = boxWidth;
}

QString TextStyle::cacheKey() const
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(text)
        .arg(fontFamily)
        .arg(pixelSize)
        .arg(color.name(QColor::HexArgb))
        .arg(align)
        .arg(lineHeight)
        .arg(padding)
        .arg(boxWidth);
}

} // namespace pvf
