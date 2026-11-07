#include "TextLayoutDialog.h"

#include "core/TextBlock.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace pvf {

TextPreview::TextPreview(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
}

void TextPreview::setStyle(const TextStyle& style)
{
    m_style = style;
    update();
}

void TextPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 36));
    const TextBlock block = TextBlock::measure(m_style);
    const QSizeF size = block.size();
    if (size.width() < 1 || size.height() < 1)
        return;
    const double scale =
        std::min((width() - 20.0) / size.width(), (height() - 20.0) / size.height());
    painter.save();
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(scale, scale);
    block.paint(painter, QPointF());
    painter.restore();
}

TextLayoutDialog::TextLayoutDialog(const TextStyle& style, QWidget* parent)
    : QDialog(parent)
    , m_style(style)
{
    setWindowTitle(QStringLiteral("文字排版"));
    resize(720, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* form = new QFormLayout;
    form->setVerticalSpacing(8);

    m_content = new QPlainTextEdit(this);
    m_content->setObjectName(QStringLiteral("layoutContent"));
    m_content->setMinimumHeight(120);
    m_content->setPlainText(m_style.text);
    connect(m_content, &QPlainTextEdit::textChanged, this,
            [this] { updateStyle(); });
    form->addRow(QStringLiteral("内容"), m_content);

    m_font = new QFontComboBox(this);
    m_font->setCurrentFont(QFont(m_style.fontFamily));
    connect(m_font, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) {
                m_style.fontFamily = font.family();
                updateStyle();
            });
    form->addRow(QStringLiteral("字体"), m_font);

    m_size = new QSpinBox(this);
    m_size->setRange(8, 512);
    m_size->setValue(std::max(8, m_style.pixelSize));
    m_size->setSuffix(QStringLiteral(" px"));
    connect(m_size, &QSpinBox::valueChanged, this, [this](int value) {
        m_style.pixelSize = value;
        updateStyle();
    });
    form->addRow(QStringLiteral("字号"), m_size);

    m_color = new QPushButton(this);
    m_color->setText(m_style.color.name().toUpper());
    connect(m_color, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(m_style.color, this,
                                                    QStringLiteral("文字颜色"));
        if (!color.isValid())
            return;
        m_style.color = color;
        m_color->setText(color.name().toUpper());
        updateStyle();
    });
    form->addRow(QStringLiteral("颜色"), m_color);

    m_align = new QComboBox(this);
    m_align->addItem(QStringLiteral("居中"), int(Qt::AlignHCenter));
    m_align->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft));
    m_align->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight));
    {
        const QSignalBlocker blocker(m_align);
        const int horizontal = m_style.align & Qt::AlignHorizontal_Mask;
        for (int index = 0; index < m_align->count(); ++index)
        {
            if (m_align->itemData(index).toInt() == horizontal)
            {
                m_align->setCurrentIndex(index);
                break;
            }
        }
    }
    connect(m_align, &QComboBox::currentIndexChanged, this, [this](int) {
        const int horizontal = m_align->currentData().toInt();
        m_style.align = (m_style.align & ~int(Qt::AlignHorizontal_Mask)) | horizontal;
        updateStyle();
    });
    form->addRow(QStringLiteral("对齐"), m_align);

    m_lineHeight = new QDoubleSpinBox(this);
    m_lineHeight->setRange(0.5, 4.0);
    m_lineHeight->setSingleStep(0.1);
    m_lineHeight->setValue(m_style.lineHeight);
    connect(m_lineHeight, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_style.lineHeight = value;
        updateStyle();
    });
    form->addRow(QStringLiteral("行距"), m_lineHeight);

    m_padding = new QDoubleSpinBox(this);
    m_padding->setRange(0, 200);
    m_padding->setValue(m_style.padding);
    connect(m_padding, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_style.padding = value;
        updateStyle();
    });
    form->addRow(QStringLiteral("内边距"), m_padding);

    m_boxWidth = new QDoubleSpinBox(this);
    m_boxWidth->setRange(0, 4000);
    m_boxWidth->setSpecialValueText(QStringLiteral("自动"));
    m_boxWidth->setValue(m_style.boxWidth);
    connect(m_boxWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_style.boxWidth = value;
        updateStyle();
    });
    form->addRow(QStringLiteral("排版宽度"), m_boxWidth);

    body->addLayout(form, 1);

    m_preview = new TextPreview(this);
    m_preview->setObjectName(QStringLiteral("layoutPreview"));
    body->addWidget(m_preview, 1);
    root->addLayout(body, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    applyStyle();
}

void TextLayoutDialog::applyStyle()
{
    m_preview->setStyle(m_style);
}

void TextLayoutDialog::updateStyle()
{
    m_style.text = m_content->toPlainText();
    applyStyle();
}

} // namespace pvf
