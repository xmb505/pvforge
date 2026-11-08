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
    const QColor strokeColor =
        data.value(QStringLiteral("textStrokeColor")).value<QColor>();
    if (strokeColor.isValid())
        style.strokeColor = strokeColor;
    style.strokeWidth =
        data.value(QStringLiteral("textStrokeWidth"), 0.0).toDouble();
    style.align = data.value(QStringLiteral("align"), int(Qt::AlignCenter)).toInt();
    style.lineHeight = data.value(QStringLiteral("lineHeight"), 1.0).toDouble();
    style.letterSpacing = data.value(QStringLiteral("letterSpacing"), 0.0).toDouble();
    style.padding = data.value(QStringLiteral("padding"), 6.0).toDouble();
    style.boxWidth = data.value(QStringLiteral("boxWidth"), 0.0).toDouble();
    style.boxHeight = data.value(QStringLiteral("boxHeight"), 0.0).toDouble();
    style.bold = data.value(QStringLiteral("bold"), false).toBool();
    style.italic = data.value(QStringLiteral("italic"), false).toBool();
    style.underline = data.value(QStringLiteral("underline"), false).toBool();
    style.vertical = data.value(QStringLiteral("vertical"), false).toBool();
    style.verticalRtl = data.value(QStringLiteral("verticalRtl"), true).toBool();
    return style;
}

void TextStyle::writeTo(QVariantMap& data) const
{
    data[QStringLiteral("text")] = text;
    data[QStringLiteral("font")] = fontFamily;
    data[QStringLiteral("size")] = pixelSize;
    data[QStringLiteral("color")] = color;
    data[QStringLiteral("textStrokeColor")] = strokeColor;
    data[QStringLiteral("textStrokeWidth")] = strokeWidth;
    data[QStringLiteral("align")] = align;
    data[QStringLiteral("lineHeight")] = lineHeight;
    data[QStringLiteral("letterSpacing")] = letterSpacing;
    data[QStringLiteral("padding")] = padding;
    data[QStringLiteral("boxWidth")] = boxWidth;
    data[QStringLiteral("boxHeight")] = boxHeight;
    data[QStringLiteral("bold")] = bold;
    data[QStringLiteral("italic")] = italic;
    data[QStringLiteral("underline")] = underline;
    data[QStringLiteral("vertical")] = vertical;
    data[QStringLiteral("verticalRtl")] = verticalRtl;
}

QString TextStyle::cacheKey() const
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17")
        .arg(text)
        .arg(fontFamily)
        .arg(pixelSize)
        .arg(color.name(QColor::HexArgb))
        .arg(strokeColor.name(QColor::HexArgb))
        .arg(strokeWidth)
        .arg(align)
        .arg(lineHeight)
        .arg(letterSpacing)
        .arg(padding)
        .arg(boxWidth)
        .arg(boxHeight)
        .arg(bold ? 1 : 0)
        .arg(italic ? 1 : 0)
        .arg(underline ? 1 : 0)
        .arg(vertical ? 1 : 0)
        .arg(verticalRtl ? 1 : 0);
}

} // namespace pvf
