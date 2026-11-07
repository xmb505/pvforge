#pragma once

#include "TextStyle.h"

#include <QSizeF>

#include <memory>

class QPainter;
class QTextDocument;

namespace pvf {

// A measured block of text. Wrapping, line height, padding and alignment
// live here, so the scene, the layout dialog preview and the asset library
// draw exactly the same thing.
class TextBlock
{
public:
    static TextBlock measure(const TextStyle& style);

    QSizeF contentSize() const { return m_content; }   // text without padding
    QSizeF size() const { return m_size; }             // text plus padding

    // Draws the text centred on the given point.
    void paint(QPainter& painter, const QPointF& centre) const;

private:
    TextStyle m_style;
    std::shared_ptr<QTextDocument> m_document;
    QSizeF m_content;
    QSizeF m_size;
};

} // namespace pvf
