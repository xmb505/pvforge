#pragma once

#include "model/Content.h"

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace pvf::next {

// Modal editor for model::TextContent. It edits a copy only; the caller reads
// content() after exec() and writes the result back in one commit. The preview
// paints with pvf::TextBlock/TextStyle, the same code render2 uses.
class TextLayoutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextLayoutDialog(const model::TextContent& content,
                              QWidget* parent = nullptr);

    // Current widget values. Widgets are the source of truth, so this is valid
    // even when the dialog is driven directly by tests.
    model::TextContent content() const;

private:
    void buildUi();
    void refreshPreview();

    model::TextContent m_initial;
    // Kept separately because QFontComboBox cannot represent a missing family;
    // opening the dialog must not silently replace it with the system default.
    QString m_fontFamily;
    QColor m_color = Qt::white;
    QColor m_strokeColor = Qt::black;

    QPlainTextEdit* m_text = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_size = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPushButton* m_strokeColorButton = nullptr;
    QDoubleSpinBox* m_strokeWidth = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_italic = nullptr;
    QCheckBox* m_underline = nullptr;
    QComboBox* m_alignH = nullptr;
    QComboBox* m_alignV = nullptr;
    QDoubleSpinBox* m_lineHeight = nullptr;
    QDoubleSpinBox* m_letterSpacing = nullptr;
    QDoubleSpinBox* m_padding = nullptr;
    QDoubleSpinBox* m_boxWidth = nullptr;
    QDoubleSpinBox* m_boxHeight = nullptr;
    QComboBox* m_direction = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_previewInfo = nullptr;
};

} // namespace pvf::next
