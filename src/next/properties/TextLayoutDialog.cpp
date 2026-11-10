#include "TextLayoutDialog.h"

#include "core/TextBlock.h"
#include "core/TextStyle.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

// Alignment flags this dialog owns. AlignAbsolute and AlignBaseline stay
// untouched so a round trip never loses information.
constexpr int kHorizontalMask = int(Qt::AlignLeft) | int(Qt::AlignRight)
    | int(Qt::AlignHCenter) | int(Qt::AlignJustify);
constexpr int kVerticalMask = int(Qt::AlignTop) | int(Qt::AlignVCenter)
    | int(Qt::AlignBottom);

constexpr int kPreviewWidth = 380;
constexpr int kPreviewHeight = 500;

// Qt treats "no flag" as left/top; normalise so the combos always have a
// matching item instead of a synthetic "custom" entry.
int normalisedHorizontal(int align)
{
    const int horizontal = align & kHorizontalMask;
    return horizontal == 0 ? int(Qt::AlignLeft) : horizontal;
}

int normalisedVertical(int align)
{
    const int vertical = align & kVerticalMask;
    return vertical == 0 ? int(Qt::AlignTop) : vertical;
}

void selectComboValue(QComboBox* combo, int value)
{
    int index = combo->findData(value);
    if (index < 0)
    {
        combo->addItem(QStringLiteral("自定义 (%1)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

QString colorText(const QColor& color)
{
    if (!color.isValid())
        return {};
    return color.alpha() < 255 ? color.name(QColor::HexArgb)
                               : color.name(QColor::HexRgb);
}

void styleColorButton(QPushButton* button, const QColor& color)
{
    if (!button || !color.isValid())
        return;
    const QColor text =
        color.lightnessF() > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    button->setText(colorText(color));
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2;"
                       " border: 1px solid #888; border-radius: 0;"
                       " padding: 2px 6px; }")
            .arg(color.name(QColor::HexRgb), text.name(QColor::HexRgb)));
}

QColor safeColor(const QString& name, const QColor& fallback)
{
    const QColor color(name);
    return color.isValid() ? color : fallback;
}

pvf::TextStyle textStyleFromContent(const model::TextContent& content)
{
    pvf::TextStyle style;
    style.text = content.text;
    style.fontFamily = content.fontFamily;
    style.pixelSize = std::max(1, content.pixelSize);
    style.color = safeColor(content.color, Qt::white);
    style.strokeColor = safeColor(content.strokeColor, Qt::black);
    style.strokeWidth = std::max(0.0, content.strokeWidth);
    style.align = content.align;
    style.lineHeight = content.lineHeight;
    style.letterSpacing = content.letterSpacing;
    style.padding = content.padding;
    style.boxWidth = content.boxWidth;
    style.boxHeight = content.boxHeight;
    style.bold = content.bold;
    style.italic = content.italic;
    style.underline = content.underline;
    style.vertical = content.vertical;
    style.verticalRtl = content.verticalRtl;
    return style;
}

} // namespace

// Dark checkerboard plus the TextBlock the scene draws. Empty text or an
// unusable measurement falls back to placeholder text on the same background.
QPixmap renderTextLayoutPreview(const model::TextContent& content,
                                const QSize& size)
{
    QPixmap pixmap(size);
    pixmap.fill(QColor(24, 24, 29));

    QPainter painter(&pixmap);
    constexpr int cell = 12;
    for (int y = 0; y < size.height(); y += cell)
    {
        for (int x = 0; x < size.width(); x += cell)
        {
            if (((x / cell) + (y / cell)) % 2 == 0)
                painter.fillRect(QRect(x, y, cell, cell), QColor(33, 33, 39));
        }
    }

    const pvf::TextStyle style = textStyleFromContent(content);
    const pvf::TextBlock block = pvf::TextBlock::measure(style);
    const QSizeF blockSize = block.size();
    const double stroke = style.strokeWidth > 0.0 ? style.strokeWidth : 0.0;
    const QSizeF total(blockSize.width() + stroke * 2.0,
                       blockSize.height() + stroke * 2.0);

    const auto drawPlaceholder = [&painter, &pixmap](const QString& text) {
        painter.setPen(QColor(150, 150, 162));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    };

    if (style.text.trimmed().isEmpty() || total.width() < 1.0
        || total.height() < 1.0)
    {
        drawPlaceholder(QStringLiteral("预览不可用：没有可排版的文本"));
        return pixmap;
    }
    // TextBlock rasterises the whole block before painting; refuse absurd
    // sizes instead of allocating a multi-gigabyte image.
    constexpr double kMaxPreviewSourceEdge = 4096.0;
    if (total.width() > kMaxPreviewSourceEdge
        || total.height() > kMaxPreviewSourceEdge)
    {
        drawPlaceholder(QStringLiteral("预览不可用：排版尺寸过大"));
        return pixmap;
    }

    constexpr double margin = 16.0;
    const double scale = std::min(
        {4.0, (size.width() - margin * 2.0) / total.width(),
         (size.height() - margin * 2.0) / total.height()});
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    block.paint(painter, QPointF());
    return pixmap;
}

namespace {

QGroupBox* addGroup(QVBoxLayout* layout, QWidget* parent, const QString& title)
{
    auto* group = new QGroupBox(title, parent);
    layout->addWidget(group);
    return group;
}

QFormLayout* groupForm(QGroupBox* group)
{
    auto* form = new QFormLayout(group);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

QDoubleSpinBox* makeSpin(QWidget* parent, double minimum, double maximum,
                         double step, int decimals)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    return spin;
}

} // namespace

TextLayoutDialog::TextLayoutDialog(const model::TextContent& content,
                                   QWidget* parent)
    : QDialog(parent)
    , m_initial(content)
    , m_fontFamily(content.fontFamily)
    , m_color(safeColor(content.color, Qt::white))
    , m_strokeColor(safeColor(content.strokeColor, Qt::black))
{
    setWindowTitle(QStringLiteral("文字排版"));
    resize(900, 620);
    buildUi();
    refreshPreview();
}

void TextLayoutDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("layoutScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(scroll);
    auto* hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 6, 0);
    hostLayout->setSpacing(10);
    scroll->setWidget(host);

    auto onChange = [this] { refreshPreview(); };

    // Text
    QGroupBox* textGroup = addGroup(hostLayout, host, QStringLiteral("文字"));
    QFormLayout* textForm = groupForm(textGroup);

    m_text = new QPlainTextEdit(textGroup);
    m_text->setObjectName(QStringLiteral("layoutContent"));
    m_text->setMinimumHeight(96);
    m_text->setPlainText(m_initial.text);
    m_text->setToolTip(QStringLiteral("支持多行；换行即分段，竖排时每段重新分列。"));
    textForm->addRow(QStringLiteral("文本"), m_text);
    connect(m_text, &QPlainTextEdit::textChanged, this, onChange);

    m_font = new QFontComboBox(textGroup);
    m_font->setObjectName(QStringLiteral("layoutFont"));
    m_font->setEditable(false);
    if (!m_initial.fontFamily.isEmpty())
        m_font->setCurrentFont(QFont(m_initial.fontFamily));
    connect(m_font, &QFontComboBox::currentFontChanged, this,
            [this, onChange](const QFont& font) {
                m_fontFamily = font.family();
                onChange();
            });
    textForm->addRow(QStringLiteral("字体"), m_font);

    m_size = new QSpinBox(textGroup);
    m_size->setObjectName(QStringLiteral("layoutSize"));
    m_size->setRange(1, 100000);
    m_size->setSuffix(QStringLiteral(" px"));
    m_size->setKeyboardTracking(false);
    m_size->setValue(std::max(1, m_initial.pixelSize));
    connect(m_size, &QSpinBox::valueChanged, this, onChange);
    textForm->addRow(QStringLiteral("字号"), m_size);

    m_colorButton = new QPushButton(textGroup);
    m_colorButton->setObjectName(QStringLiteral("layoutColor"));
    styleColorButton(m_colorButton, m_color);
    connect(m_colorButton, &QPushButton::clicked, this, [this, onChange] {
        const QColor chosen =
            QColorDialog::getColor(m_color, this, QStringLiteral("文字颜色"));
        if (!chosen.isValid())
            return;
        m_color = chosen;
        styleColorButton(m_colorButton, m_color);
        onChange();
    });
    textForm->addRow(QStringLiteral("颜色"), m_colorButton);

    m_strokeColorButton = new QPushButton(textGroup);
    m_strokeColorButton->setObjectName(QStringLiteral("layoutStrokeColor"));
    styleColorButton(m_strokeColorButton, m_strokeColor);
    connect(m_strokeColorButton, &QPushButton::clicked, this,
            [this, onChange] {
                const QColor chosen = QColorDialog::getColor(
                    m_strokeColor, this, QStringLiteral("文字描边颜色"),
                    QColorDialog::ShowAlphaChannel);
                if (!chosen.isValid())
                    return;
                m_strokeColor = chosen;
                styleColorButton(m_strokeColorButton, m_strokeColor);
                onChange();
            });
    textForm->addRow(QStringLiteral("描边色"), m_strokeColorButton);

    m_strokeWidth =
        makeSpin(textGroup, 0.0, 10000.0, 0.5, 2);
    m_strokeWidth->setObjectName(QStringLiteral("layoutStrokeWidth"));
    m_strokeWidth->setSuffix(QStringLiteral(" px"));
    m_strokeWidth->setValue(m_initial.strokeWidth);
    connect(m_strokeWidth, &QDoubleSpinBox::valueChanged, this, onChange);
    textForm->addRow(QStringLiteral("描边宽度"), m_strokeWidth);

    // Style
    QGroupBox* styleGroup = addGroup(hostLayout, host, QStringLiteral("样式"));
    QFormLayout* styleForm = groupForm(styleGroup);

    m_bold = new QCheckBox(QStringLiteral("粗体"), styleGroup);
    m_bold->setObjectName(QStringLiteral("layoutBold"));
    m_bold->setChecked(m_initial.bold);
    connect(m_bold, &QCheckBox::toggled, this, onChange);
    styleForm->addRow(m_bold);

    m_italic = new QCheckBox(QStringLiteral("斜体"), styleGroup);
    m_italic->setObjectName(QStringLiteral("layoutItalic"));
    m_italic->setChecked(m_initial.italic);
    connect(m_italic, &QCheckBox::toggled, this, onChange);
    styleForm->addRow(m_italic);

    m_underline = new QCheckBox(QStringLiteral("下划线"), styleGroup);
    m_underline->setObjectName(QStringLiteral("layoutUnderline"));
    m_underline->setChecked(m_initial.underline);
    connect(m_underline, &QCheckBox::toggled, this, onChange);
    styleForm->addRow(m_underline);

    m_direction = new QComboBox(styleGroup);
    m_direction->setObjectName(QStringLiteral("layoutDirection"));
    m_direction->addItem(QStringLiteral("横排"), 0);
    m_direction->addItem(QStringLiteral("竖排 · 从右到左"), 1);
    m_direction->addItem(QStringLiteral("竖排 · 从左到右"), 2);
    m_direction->setCurrentIndex(m_initial.vertical
                                     ? (m_initial.verticalRtl ? 1 : 2)
                                     : 0);
    connect(m_direction, &QComboBox::currentIndexChanged, this, onChange);
    styleForm->addRow(QStringLiteral("排版方向"), m_direction);

    // Alignment and metrics
    QGroupBox* layoutGroup =
        addGroup(hostLayout, host, QStringLiteral("对齐与排版"));
    QFormLayout* layoutForm = groupForm(layoutGroup);

    m_alignH = new QComboBox(layoutGroup);
    m_alignH->setObjectName(QStringLiteral("layoutAlignH"));
    m_alignH->addItem(QStringLiteral("左"), int(Qt::AlignLeft));
    m_alignH->addItem(QStringLiteral("中"), int(Qt::AlignHCenter));
    m_alignH->addItem(QStringLiteral("右"), int(Qt::AlignRight));
    m_alignH->addItem(QStringLiteral("两端"), int(Qt::AlignJustify));
    selectComboValue(m_alignH, normalisedHorizontal(m_initial.align));
    connect(m_alignH, &QComboBox::currentIndexChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("水平对齐"), m_alignH);

    m_alignV = new QComboBox(layoutGroup);
    m_alignV->setObjectName(QStringLiteral("layoutAlignV"));
    m_alignV->addItem(QStringLiteral("上"), int(Qt::AlignTop));
    m_alignV->addItem(QStringLiteral("中"), int(Qt::AlignVCenter));
    m_alignV->addItem(QStringLiteral("下"), int(Qt::AlignBottom));
    selectComboValue(m_alignV, normalisedVertical(m_initial.align));
    connect(m_alignV, &QComboBox::currentIndexChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("垂直对齐"), m_alignV);

    m_lineHeight = makeSpin(layoutGroup, 0.01, 100.0, 0.1, 3);
    m_lineHeight->setObjectName(QStringLiteral("layoutLineHeight"));
    m_lineHeight->setValue(m_initial.lineHeight);
    m_lineHeight->setToolTip(
        QStringLiteral("字体行高的倍数；竖排时控制列间距。"));
    connect(m_lineHeight, &QDoubleSpinBox::valueChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("行高"), m_lineHeight);

    m_letterSpacing =
        makeSpin(layoutGroup, -10000.0, 10000.0, 1.0, 2);
    m_letterSpacing->setObjectName(QStringLiteral("layoutLetterSpacing"));
    m_letterSpacing->setSuffix(QStringLiteral(" px"));
    m_letterSpacing->setValue(m_initial.letterSpacing);
    m_letterSpacing->setToolTip(QStringLiteral("字与字之间的额外间距。"));
    connect(m_letterSpacing, &QDoubleSpinBox::valueChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("字距"), m_letterSpacing);

    m_padding = makeSpin(layoutGroup, 0.0, 100000.0, 1.0, 2);
    m_padding->setObjectName(QStringLiteral("layoutPadding"));
    m_padding->setSuffix(QStringLiteral(" px"));
    m_padding->setValue(m_initial.padding);
    connect(m_padding, &QDoubleSpinBox::valueChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("内边距"), m_padding);

    m_boxWidth = makeSpin(layoutGroup, 0.0, 1000000.0, 1.0, 2);
    m_boxWidth->setObjectName(QStringLiteral("layoutBoxWidth"));
    m_boxWidth->setSuffix(QStringLiteral(" px"));
    m_boxWidth->setSpecialValueText(QStringLiteral("自动"));
    m_boxWidth->setValue(m_initial.boxWidth);
    m_boxWidth->setToolTip(
        QStringLiteral("0 = 自动；大于 0 时按该宽度换行（横排）。"));
    connect(m_boxWidth, &QDoubleSpinBox::valueChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("框宽"), m_boxWidth);

    m_boxHeight = makeSpin(layoutGroup, 0.0, 1000000.0, 1.0, 2);
    m_boxHeight->setObjectName(QStringLiteral("layoutBoxHeight"));
    m_boxHeight->setSuffix(QStringLiteral(" px"));
    m_boxHeight->setSpecialValueText(QStringLiteral("自动"));
    m_boxHeight->setValue(m_initial.boxHeight);
    m_boxHeight->setToolTip(
        QStringLiteral("0 = 自动；竖排时是一列的最大高度。"));
    connect(m_boxHeight, &QDoubleSpinBox::valueChanged, this, onChange);
    layoutForm->addRow(QStringLiteral("框高"), m_boxHeight);

    hostLayout->addStretch(1);

    body->addWidget(scroll, 1);

    auto* previewHost = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewHost);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(6);

    m_preview = new QLabel(previewHost);
    m_preview->setObjectName(QStringLiteral("layoutPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedSize(kPreviewWidth, kPreviewHeight);
    previewLayout->addWidget(m_preview);

    m_previewInfo = new QLabel(previewHost);
    m_previewInfo->setObjectName(QStringLiteral("layoutPreviewInfo"));
    m_previewInfo->setAlignment(Qt::AlignCenter);
    m_previewInfo->setWordWrap(true);
    previewLayout->addWidget(m_previewInfo);
    previewLayout->addStretch(1);

    body->addWidget(previewHost);
    root->addLayout(body, 1);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

model::TextContent TextLayoutDialog::content() const
{
    model::TextContent out = m_initial;
    out.text = m_text->toPlainText();
    out.fontFamily = m_fontFamily;
    out.pixelSize = m_size->value();
    out.color = colorText(m_color);
    out.strokeColor = colorText(m_strokeColor);
    out.strokeWidth = m_strokeWidth->value();
    out.align = (m_initial.align & ~(kHorizontalMask | kVerticalMask))
        | m_alignH->currentData().toInt() | m_alignV->currentData().toInt();
    out.lineHeight = m_lineHeight->value();
    out.letterSpacing = m_letterSpacing->value();
    out.padding = m_padding->value();
    out.boxWidth = m_boxWidth->value();
    out.boxHeight = m_boxHeight->value();
    out.bold = m_bold->isChecked();
    out.italic = m_italic->isChecked();
    out.underline = m_underline->isChecked();
    const int direction = m_direction->currentIndex();
    out.vertical = direction > 0;
    out.verticalRtl = direction != 2;
    return out;
}

void TextLayoutDialog::refreshPreview()
{
    const model::TextContent value = content();
    m_preview->setPixmap(renderTextLayoutPreview(value, m_preview->size()));

    const pvf::TextStyle style = textStyleFromContent(value);
    const pvf::TextBlock block = pvf::TextBlock::measure(style);
    const QSizeF size = block.size();
    if (value.text.trimmed().isEmpty() || size.width() < 1.0
        || size.height() < 1.0)
    {
        m_previewInfo->setText(QStringLiteral("排版尺寸：—"));
        return;
    }
    m_previewInfo->setText(
        QStringLiteral("排版尺寸：%1 × %2 px")
            .arg(size.width(), 0, 'f', 0)
            .arg(size.height(), 0, 'f', 0));
}

} // namespace pvf::next
