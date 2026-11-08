#pragma once

#include "core/TextStyle.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace pvf {

// Live preview of a text block. It draws with the same TextBlock the scene
// uses, so the dialog shows what the entity will look like.
class TextPreview : public QWidget
{
    Q_OBJECT

public:
    explicit TextPreview(QWidget* parent = nullptr);

    void setStyle(const TextStyle& style);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TextStyle m_style;
};

// Editor for the text appearance: content, font, size, colour, alignment,
// line height, padding and the wrap width. The dialog only edits a copy of
// the style; the caller decides what to do with the result.
class TextLayoutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextLayoutDialog(const TextStyle& style, QWidget* parent = nullptr);

    TextStyle style() const { return m_style; }

private:
    void applyStyle();
    void updateStyle();

    TextStyle m_style;
    QPlainTextEdit* m_content = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_size = nullptr;
    QPushButton* m_color = nullptr;
    QComboBox* m_align = nullptr;
    QDoubleSpinBox* m_lineHeight = nullptr;
    QDoubleSpinBox* m_letterSpacing = nullptr;
    QDoubleSpinBox* m_padding = nullptr;
    QDoubleSpinBox* m_boxWidth = nullptr;
    QDoubleSpinBox* m_boxHeight = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_italic = nullptr;
    QCheckBox* m_underline = nullptr;
    QComboBox* m_direction = nullptr;
    TextPreview* m_preview = nullptr;
};

} // namespace pvf
