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
    int align = int(Qt::AlignCenter);
    double lineHeight = 1.0;   // multiplier of the font line height
    double padding = 6.0;      // space between the text and its box
    double boxWidth = 0.0;     // 0 = the text keeps its natural width

    static TextStyle fromData(const QVariantMap& data);
    void writeTo(QVariantMap& data) const;

    // Identity of the laid out result; used to reuse measurements.
    QString cacheKey() const;
};

} // namespace pvf
