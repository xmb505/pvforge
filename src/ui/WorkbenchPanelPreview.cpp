#include "WorkbenchPanel.h"

#include "core/BrandWall.h"
#include "core/ComicBox.h"
#include "core/LineDeco.h"
#include "core/TextStyle.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>

#include <algorithm>

namespace pvf {

TextStyle WorkbenchPanel::currentTextStyle() const
{
    TextStyle style = m_textStyle;
    style.text = m_textContent->text().isEmpty() ? QStringLiteral("Text")
                                                  : m_textContent->text();
    style.fontFamily = m_textFont->currentFont().family();
    style.pixelSize = m_textSize->value();
    style.color = m_textColorValue;
    style.align = m_textAlign->currentData().toInt();
    return style;
}

void WorkbenchPanel::updateSpecialPreview()
{
    if (!m_specialPreview)
        return;

    const int row = m_specialTypes ? m_specialTypes->currentRow() : 0;
    QImage image(240, 150, QImage::Format_ARGB32);
    image.fill(QColor(30, 30, 36));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF centre(image.width() / 2.0, image.height() / 2.0);
    const auto fit = [&](const QRectF& bounds) {
        return std::min((image.width() - 16.0) / std::max(1.0, bounds.width()),
                         (image.height() - 16.0) / std::max(1.0, bounds.height()));
    };

    if (row == 0)
    {
        RadialParams params;
        params.count = m_radialCount->value();
        params.width = m_radialThickness->value();
        params.flicker = m_radialFlicker->value();
        params.color = m_radialColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::radialBounds(params)),
                      fit(LineDeco::radialBounds(params)));
        LineDeco::paintRadial(painter, params, 0.0);
        painter.restore();
    }
    else if (row == 1)
    {
        FlowParams params;
        params.count = m_flowCount->value();
        params.speed = m_flowSpeed->value();
        params.angle = m_flowAngle->value();
        params.color = m_flowColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::flowBounds(params)),
                      fit(LineDeco::flowBounds(params)));
        LineDeco::paintFlow(painter, params, 0.0);
        painter.restore();
    }
    else if (row == 2)
    {
        HalftoneParams params;
        params.area = m_halftoneArea->value();
        params.spacing = m_halftoneSpacing->value();
        params.dotSize = m_halftoneSize->value();
        params.angle = m_halftoneAngle->value();
        params.gradientAngle = m_halftoneGradient->value();
        params.minRatio = m_halftoneMin->value();
        params.jitter = m_halftoneJitter->value();
        params.color = m_halftoneColorValue;
        painter.save();
        painter.translate(centre);
        painter.scale(fit(LineDeco::halftoneBounds(params)),
                      fit(LineDeco::halftoneBounds(params)));
        LineDeco::paintHalftone(painter, params);
        painter.restore();
    }
    else
    {
        painter.save();
        painter.translate(centre);
        painter.scale(fit(BrandWall::bounds(m_wallParams)),
                      fit(BrandWall::bounds(m_wallParams)));
        BrandWall::paint(painter, m_wallParams, m_wallImages);
        painter.restore();
    }
    m_specialPreview->setPixmap(QPixmap::fromImage(image));
}

void WorkbenchPanel::updateTextBoxPreview()
{
    if (!m_textBoxPreview)
        return;

    const int row = m_textBoxTypes ? m_textBoxTypes->currentRow() : 0;
    int shape = m_textBoxShape ? m_textBoxShape->currentData().toInt() : 0;
    if (row == 1)
        shape = 1;   // Japanese hard rectangle
    else if (row == 2)
        shape = 3;   // explosion

    TextStyle style = m_comicStyle;
    style.text = m_comicContent->text().isEmpty() ? QStringLiteral("台词")
                                                   : m_comicContent->text();
    style.fontFamily = QFont().family();
    style.pixelSize = m_comicSize->value();
    style.color = m_comicColorValue;
    style.align = int(Qt::AlignCenter);
    BubbleStyle bubble;
    bubble.shape = shape;
    bubble.tail = row == 0;
    bubble.fill = m_comicFillValue;
    bubble.stroke = m_comicStrokeValue;
    bubble.strokeWidth = m_comicStrokeWidth ? m_comicStrokeWidth->value() : 6.0;
    const ComicBox box = ComicBox::build(style, bubble);
    const QRectF bounds = box.bounds();

    QImage image(280, 150, QImage::Format_ARGB32);
    image.fill(QColor(30, 30, 36));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double scale =
        std::min((image.width() - 16.0) / std::max(1.0, bounds.width()),
                 (image.height() - 16.0) / std::max(1.0, bounds.height()));
    painter.translate(image.width() / 2.0, image.height() / 2.0);
    painter.scale(scale, scale);
    box.paint(painter);
    m_textBoxPreview->setPixmap(QPixmap::fromImage(image));
}

} // namespace pvf
