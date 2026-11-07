#include "TextBlock.h"

#include <QFont>
#include <QHash>
#include <QPainter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <algorithm>

namespace pvf {

namespace {

// Measurements are reused whenever the style did not change; the scene,
// the monitor and the asset library all ask for the same blocks.
QHash<QString, std::weak_ptr<TextBlock>>& cache()
{
    static QHash<QString, std::weak_ptr<TextBlock>> blocks;
    return blocks;
}

double blockWidth(const QTextLayout* layout)
{
    double width = 0.0;
    for (int index = 0; index < layout->lineCount(); ++index)
        width = std::max(width, layout->lineAt(index).width());
    return width;
}

QFont fontOf(const TextStyle& style)
{
    QFont font(style.fontFamily.isEmpty() ? QFont().family() : style.fontFamily);
    font.setPixelSize(std::max(1, style.pixelSize));
    return font;
}

} // namespace

TextBlock TextBlock::measure(const TextStyle& style)
{
    const QString key = style.cacheKey();
    auto& blocks = cache();
    if (const auto found = blocks.constFind(key); found != blocks.constEnd())
        if (const auto existing = found->lock())
            return *existing;

    auto block = std::make_shared<TextBlock>();
    block->m_style = style;

    const QFont font = fontOf(style);

    auto document = std::make_shared<QTextDocument>();
    document->setDocumentMargin(0);
    document->setDefaultFont(font);
    QTextOption option = document->defaultTextOption();
    option.setWrapMode(style.boxWidth > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                         : QTextOption::NoWrap);
    option.setAlignment(Qt::Alignment(style.align & Qt::AlignHorizontal_Mask));
    document->setDefaultTextOption(option);
    document->setPlainText(style.text);

    QTextCursor cursor(document.get());
    cursor.select(QTextCursor::Document);
    QTextCharFormat characterFormat;
    characterFormat.setFont(font);
    characterFormat.setForeground(style.color);
    cursor.mergeCharFormat(characterFormat);
    if (std::abs(style.lineHeight - 1.0) > 1e-6)
    {
        QTextBlockFormat format;
        format.setLineHeight(style.lineHeight * 100.0,
                             QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(format);
    }
    document->setTextWidth(style.boxWidth > 0 ? style.boxWidth : -1);

    block->m_document = document;
    block->m_content = document->size();
    block->m_size = block->m_content +
                    QSizeF(style.padding * 2.0, style.padding * 2.0);

    if (blocks.size() > 256)
        blocks.clear();
    blocks.insert(key, block);
    return *block;
}

void TextBlock::paint(QPainter& painter, const QPointF& centre) const
{
    if (!m_document || m_document->isEmpty())
        return;

    const QFont font = fontOf(m_style);
    painter.save();
    painter.setFont(font);
    painter.setPen(m_style.color);

    const QPointF topLeft = centre -
                            QPointF(m_size.width() / 2.0, m_size.height() / 2.0) +
                            QPointF(m_style.padding, m_style.padding);
    const int horizontal = m_style.align & Qt::AlignHorizontal_Mask;

    for (QTextBlock block = m_document->firstBlock(); block.isValid();
         block = block.next())
    {
        const QTextLayout* layout = block.layout();
        if (!layout || layout->lineCount() == 0)
            continue;
        const double width = blockWidth(layout);
        double x = 0.0;
        if (horizontal == Qt::AlignRight)
            x = m_content.width() - width;
        else if (horizontal == Qt::AlignHCenter)
            x = (m_content.width() - width) / 2.0;
        // The document layout already stacks the paragraphs, so every block
        // is drawn at the same origin.
        layout->draw(&painter, QPointF(topLeft.x() + x, topLeft.y()));
    }
    painter.restore();
}

} // namespace pvf
