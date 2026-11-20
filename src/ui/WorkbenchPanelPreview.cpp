#include "WorkbenchPanel.h"

#include "core/TextStyle.h"

#include <QComboBox>
#include <QFontComboBox>
#include <QLineEdit>
#include <QSpinBox>

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

} // namespace pvf
