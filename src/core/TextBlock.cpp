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
#include <cmath>
#include <limits>

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
    font.setBold(style.bold);
    font.setItalic(style.italic);
    font.setUnderline(style.underline);
    if (std::abs(style.letterSpacing) > 1e-6)
        font.setLetterSpacing(QFont::AbsoluteSpacing, style.letterSpacing);
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

    if (style.vertical)
    {
        // Tategaki: characters run downwards, columns run right to left.
        const QFontMetricsF metrics(font);
        const double baseStep =
            metrics.lineSpacing() * std::max(0.5, style.lineHeight);
        block->m_columnStep = baseStep;
        block->m_charStep = std::max(1.0, baseStep + style.letterSpacing);
        const int perColumn = style.boxHeight > 0
                                  ? std::max(1, int(style.boxHeight / block->m_charStep))
                                  : std::numeric_limits<int>::max();
        const QStringList paragraphs = style.text.split(QLatin1Char('\n'));
        for (const QString& paragraph : paragraphs)
        {
            int start = 0;
            while (start < paragraph.size())
            {
                const int count = std::min(perColumn, int(paragraph.size()) - start);
                block->m_columns.append(paragraph.mid(start, count));
                start += count;
            }
        }
        int longest = 0;
        for (const QString& column : block->m_columns)
            longest = std::max(longest, int(column.size()));
        block->m_content = QSizeF(block->m_columns.size() * block->m_columnStep,
                                  longest * block->m_charStep);
        block->m_size = block->m_content +
                        QSizeF(style.padding * 2.0, style.padding * 2.0);
        if (blocks.size() > 256)
            blocks.clear();
        blocks.insert(key, block);
        return *block;
    }

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
    const QFont font = fontOf(m_style);
    painter.save();
    painter.setFont(font);
    painter.setPen(m_style.color);

    if (m_style.vertical)
    {
        const QPointF topLeft = centre -
                                QPointF(m_size.width() / 2.0, m_size.height() / 2.0) +
                                QPointF(m_style.padding, m_style.padding);
        const int count = m_columns.size();
        for (int columnIndex = 0; columnIndex < count; ++columnIndex)
        {
            const QString& column = m_columns[columnIndex];
            // Japanese columns run right to left; Chinese left to right.
            const int visual = m_style.verticalRtl ? count - 1 - columnIndex
                                                    : columnIndex;
            const double x = topLeft.x() + visual * m_columnStep +
                             m_columnStep / 2.0;
            for (int index = 0; index < column.size(); ++index)
            {
                const QRectF cell(x - m_columnStep / 2.0,
                                  topLeft.y() + index * m_charStep,
                                  m_columnStep, m_charStep);
                painter.drawText(cell, Qt::AlignCenter, QString(column.at(index)));
            }
        }
        painter.restore();
        return;
    }

    if (!m_document || m_document->isEmpty())
    {
        painter.restore();
        return;
    }

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
