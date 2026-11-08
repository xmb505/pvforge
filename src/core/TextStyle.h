#pragma once

#include <QColor>
#include <QString>
#include <QVariantMap>

namespace pvf {

// Static appearance of a text block. Plain text entities, comic text boxes
// and every preview that draws them share this description, so a layout
// change shows up everywhere at once.
struct TextStyle
{
    QString text;
    QString fontFamily;
    int pixelSize = 96;
    QColor color = Qt::white;
    QColor strokeColor = Qt::black;
    double strokeWidth = 0.0;   // glyph outline width in pixels
    int align = int(Qt::AlignCenter);
    double lineHeight = 1.0;   // multiplier of the font line height
    double letterSpacing = 0.0; // extra space between glyphs, pixels
    double padding = 6.0;      // space between the text and its box
    double boxWidth = 0.0;     // 0 = the text keeps its natural width
    double boxHeight = 0.0;    // 0 = the text keeps its natural height
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool vertical = false;     // tategaki: characters run downwards
    bool verticalRtl = true;   // Japanese columns run right to left

    static TextStyle fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;

    // Identity of the laid out result; used to reuse measurements.
    QString cacheKey() const;
};

} // namespace pvf
