#include "SpecialContentEditor.h"

#include "TextLayoutDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pvf::next {

namespace {

constexpr int kSeedMinimum = -2147483647;
constexpr int kSeedMaximum = 2147483647;

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

void selectComboData(QComboBox* combo, int value)
{
    int index = combo->findData(value);
    if (index < 0)
    {
        combo->addItem(QStringLiteral("自定义 (%1)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

void selectComboString(QComboBox* combo, const QString& value)
{
    int index = combo->findData(value);
    if (index < 0)
    {
        combo->addItem(QStringLiteral("自定义 (%1)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

// Brand wall labels/colours accept line breaks and commas; blank entries are
// dropped so a trailing comma is not an error.  Empty results are rejected by
// PropertiesPanel (logos) or model::validateContent (colours).
QStringList splitList(const QString& raw)
{
    QStringList out;
    const QStringList parts =
        raw.split(QRegularExpression(QStringLiteral("[,\\n]")),
                  Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QString value = part.trimmed();
        if (!value.isEmpty())
            out.append(value);
    }
    return out;
}

} // namespace

SpecialContentEditor::SpecialContentEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    buildPages();
    wireWidgets();
    m_stack->setCurrentIndex(-1);
}

bool SpecialContentEditor::handles(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::ComicText:
    case ContentKind::RadialLines:
    case ContentKind::FlowLines:
    case ContentKind::HalftoneDots:
    case ContentKind::BrandWall:
        return true;
    case ContentKind::Empty:
    case ContentKind::Text:
    case ContentKind::Vector:
    case ContentKind::Asset:
    case ContentKind::Camera:
        return false;
    }
    return false;
}

int SpecialContentEditor::pageIndexFor(ContentKind kind) const
{
    switch (kind)
    {
    case ContentKind::ComicText:
        return 0;
    case ContentKind::RadialLines:
        return 1;
    case ContentKind::FlowLines:
        return 2;
    case ContentKind::HalftoneDots:
        return 3;
    case ContentKind::BrandWall:
        return 4;
    default:
        return -1;
    }
}

void SpecialContentEditor::setContent(const model::Content& content)
{
    m_loading = true;
    m_kind = content.kind;
    m_stack->setCurrentIndex(pageIndexFor(content.kind));

    switch (content.kind)
    {
    case ContentKind::ComicText:
        loadComic(content.comic);
        break;
    case ContentKind::RadialLines:
        loadRadial(content.radial);
        break;
    case ContentKind::FlowLines:
        loadFlow(content.flow);
        break;
    case ContentKind::HalftoneDots:
        loadHalftone(content.halftone);
        break;
    case ContentKind::BrandWall:
        loadBrandWall(content.brandWall);
        break;
    default:
        break;
    }

    updateComicPreview();
    m_loading = false;
}

void SpecialContentEditor::applyTo(model::Content& content) const
{
    switch (m_kind)
    {
    case ContentKind::ComicText:
        saveComic(content);
        break;
    case ContentKind::RadialLines:
        saveRadial(content);
        break;
    case ContentKind::FlowLines:
        saveFlow(content);
        break;
    case ContentKind::HalftoneDots:
        saveHalftone(content);
        break;
    case ContentKind::BrandWall:
        saveBrandWall(content);
        break;
    default:
        break;
    }
}

void SpecialContentEditor::buildPages()
{
    buildComicPage();
    buildRadialPage();
    buildFlowPage();
    buildHalftonePage();
    buildBrandWallPage();
}

QFormLayout* SpecialContentEditor::addPage(const QString& objectName)
{
    auto* page = new QWidget(m_stack);
    page->setObjectName(objectName);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_stack->addWidget(page);
    return form;
}

QSpinBox* SpecialContentEditor::addInt(QFormLayout* form, const QString& label,
                                       const QString& objectName, int minimum,
                                       int maximum, const QString& suffix)
{
    auto* spin = new QSpinBox(form->parentWidget());
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    form->addRow(label, spin);
    m_intSpins.append(spin);
    return spin;
}

QDoubleSpinBox* SpecialContentEditor::addDouble(
    QFormLayout* form, const QString& label, const QString& objectName,
    double minimum, double maximum, double step, int decimals,
    const QString& suffix)
{
    auto* spin = new QDoubleSpinBox(form->parentWidget());
    spin->setObjectName(objectName);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    form->addRow(label, spin);
    m_doubleSpins.append(spin);
    return spin;
}

QCheckBox* SpecialContentEditor::addCheck(QFormLayout* form, const QString& label,
                                          const QString& objectName)
{
    auto* box = new QCheckBox(label, form->parentWidget());
    box->setObjectName(objectName);
    form->addRow(box);
    m_checks.append(box);
    return box;
}

QComboBox* SpecialContentEditor::addCombo(QFormLayout* form, const QString& label,
                                          const QString& objectName)
{
    auto* combo = new QComboBox(form->parentWidget());
    combo->setObjectName(objectName);
    form->addRow(label, combo);
    m_combos.append(combo);
    return combo;
}

QLineEdit* SpecialContentEditor::addLine(QFormLayout* form, const QString& label,
                                         const QString& objectName)
{
    auto* line = new QLineEdit(form->parentWidget());
    line->setObjectName(objectName);
    form->addRow(label, line);
    m_lines.append(line);
    return line;
}

QPlainTextEdit* SpecialContentEditor::addPlain(QFormLayout* form,
                                               const QString& label,
                                               const QString& objectName)
{
    auto* editor = new QPlainTextEdit(form->parentWidget());
    editor->setObjectName(objectName);
    editor->setMinimumHeight(84);
    form->addRow(label, editor);
    m_plains.append(editor);
    editor->installEventFilter(this);
    return editor;
}

QPushButton* SpecialContentEditor::addColor(QFormLayout* form,
                                            const QString& label,
                                            const QString& objectName,
                                            const QColor& initial)
{
    auto* button = new QPushButton(form->parentWidget());
    button->setObjectName(objectName);
    button->setToolTip(QStringLiteral("点击选择颜色（支持透明度）。"));
    form->addRow(label, button);
    m_colorButtons.append(button);
    m_colorValues.insert(button, initial);
    m_colorTexts.insert(button, colorText(initial));
    styleColorButton(button, initial);
    return button;
}

void SpecialContentEditor::wireWidgets()
{
    for (QSpinBox* spin : m_intSpins)
    {
        connect(spin, &QSpinBox::editingFinished, this,
                &SpecialContentEditor::notifyEdited);
    }
    for (QDoubleSpinBox* spin : m_doubleSpins)
    {
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &SpecialContentEditor::notifyEdited);
    }
    for (QComboBox* combo : m_combos)
    {
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this](int) { notifyEdited(); });
    }
    if (m_comicFont)
    {
        connect(m_comicFont, &QFontComboBox::currentFontChanged, this,
                [this](const QFont& font) {
                    if (m_loading)
                        return;
                    m_comicFontFamily = font.family();
                    notifyEdited();
                });
    }
    for (QCheckBox* box : m_checks)
    {
        connect(box, &QCheckBox::toggled, this,
                [this](bool) { notifyEdited(); });
    }
    for (QLineEdit* line : m_lines)
    {
        connect(line, &QLineEdit::editingFinished, this,
                &SpecialContentEditor::notifyEdited);
    }
    for (QPlainTextEdit* plain : m_plains)
    {
        // The balloon text and logo list are multi-line.  Repaint the preview
        // while typing, but only report a finished edit on focus out.
        connect(plain, &QPlainTextEdit::textChanged, this,
                &SpecialContentEditor::updateComicPreview);
    }
    for (QPushButton* button : m_colorButtons)
    {
        connect(button, &QPushButton::clicked, this,
                [this, button] { pickColor(button); });
    }
}

bool SpecialContentEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusOut
        && m_plains.contains(qobject_cast<QPlainTextEdit*>(watched)))
    {
        notifyEdited();
    }
    return QWidget::eventFilter(watched, event);
}

void SpecialContentEditor::notifyEdited()
{
    if (m_loading)
        return;
    updateComicPreview();
    emit edited();
}

void SpecialContentEditor::pickColor(QPushButton* button)
{
    const QColor current = m_colorValues.value(button, Qt::white);
    const QColor chosen =
        QColorDialog::getColor(current, this, QStringLiteral("选择颜色"),
                               QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    setColor(button, chosen);
    notifyEdited();
}

void SpecialContentEditor::setColor(QPushButton* button, const QColor& color)
{
    if (!button || !color.isValid())
        return;
    m_colorValues.insert(button, color);
    m_colorTexts.insert(button, colorText(color));
    styleColorButton(button, color);
}

void SpecialContentEditor::loadColor(QPushButton* button, const QString& value,
                                     const QColor& fallback)
{
    const QColor color(value);
    const QColor shown = color.isValid() ? color : fallback;
    m_colorValues.insert(button, shown);
    m_colorTexts.insert(button, value.isEmpty() ? colorText(fallback) : value);
    styleColorButton(button, shown);
}

QString SpecialContentEditor::colorTextValue(QPushButton* button,
                                             const QString& fallback) const
{
    const auto it = m_colorTexts.constFind(button);
    return it == m_colorTexts.constEnd() || it.value().isEmpty()
        ? fallback
        : it.value();
}

void SpecialContentEditor::updateComicPreview()
{
    if (!m_comicPreview || m_kind != ContentKind::ComicText)
        return;

    // Reuse exactly the dialog's text layout preview for the text fields.  The
    // balloon body is not drawn here; it is an entity-level decoration.
    model::TextContent text;
    text.text = m_comicText->toPlainText();
    text.fontFamily = m_comicFontFamily;
    text.pixelSize = m_comicPixelSize->value();
    text.color = colorTextValue(m_comicColor, QStringLiteral("#111111"));
    text.strokeColor =
        colorTextValue(m_comicStrokeColor, QStringLiteral("#000000"));
    text.strokeWidth = m_comicStrokeWidth->value();
    text.align = m_comicAlign->currentData().toInt();
    text.lineHeight = m_comicLineHeight->value();
    text.bold = m_comicBold->isChecked();
    text.italic = m_comicItalic->isChecked();
    text.boxWidth = m_comicBoxWidth->value();
    text.boxHeight = m_comicBoxHeight->value();
    text.padding = m_comicPadding->value();

    m_comicPreview->setPixmap(
        renderTextLayoutPreview(text, m_comicPreview->size()));
}

void SpecialContentEditor::buildComicPage()
{
    QFormLayout* form = addPage(QStringLiteral("comicTextPage"));

    m_comicText = addPlain(form, QStringLiteral("文本"),
                           QStringLiteral("comicTextEdit"));
    m_comicText->setToolTip(
        QStringLiteral("漫画文字支持多行；换行即分段。失焦后一次性写回。"));

    m_comicFont = new QFontComboBox(form->parentWidget());
    m_comicFont->setObjectName(QStringLiteral("comicFontCombo"));
    m_comicFont->setEditable(false);
    form->addRow(QStringLiteral("字体"), m_comicFont);

    m_comicPixelSize = addInt(form, QStringLiteral("字号"),
                              QStringLiteral("comicPixelSizeSpin"), 1, 8192,
                              QStringLiteral(" px"));

    m_comicColor = addColor(form, QStringLiteral("文字颜色"),
                            QStringLiteral("comicColorButton"),
                            QColor(QStringLiteral("#111111")));
    m_comicStrokeColor = addColor(form, QStringLiteral("文字描边色"),
                                  QStringLiteral("comicStrokeColorButton"),
                                  QColor(QStringLiteral("#000000")));
    m_comicStrokeWidth =
        addDouble(form, QStringLiteral("文字描边宽度"),
                  QStringLiteral("comicStrokeWidthSpin"), 0.0, 1000.0, 0.5, 2,
                  QStringLiteral(" px"));

    m_comicAlign = addCombo(form, QStringLiteral("对齐"),
                            QStringLiteral("comicAlignCombo"));
    const QVector<QPair<QString, int>> alignItems = {
        {QStringLiteral("左上"), int(Qt::AlignLeft) | int(Qt::AlignTop)},
        {QStringLiteral("中上"), int(Qt::AlignHCenter) | int(Qt::AlignTop)},
        {QStringLiteral("右上"), int(Qt::AlignRight) | int(Qt::AlignTop)},
        {QStringLiteral("左中"), int(Qt::AlignLeft) | int(Qt::AlignVCenter)},
        {QStringLiteral("居中"), int(Qt::AlignHCenter) | int(Qt::AlignVCenter)},
        {QStringLiteral("右中"), int(Qt::AlignRight) | int(Qt::AlignVCenter)},
        {QStringLiteral("左下"), int(Qt::AlignLeft) | int(Qt::AlignBottom)},
        {QStringLiteral("中下"), int(Qt::AlignHCenter) | int(Qt::AlignBottom)},
        {QStringLiteral("右下"), int(Qt::AlignRight) | int(Qt::AlignBottom)},
    };
    for (const auto& item : alignItems)
        m_comicAlign->addItem(item.first, item.second);

    m_comicLineHeight =
        addDouble(form, QStringLiteral("行高"),
                  QStringLiteral("comicLineHeightSpin"), 0.1, 10.0, 0.1, 3);
    m_comicBold = addCheck(form, QStringLiteral("粗体"),
                           QStringLiteral("comicBoldCheck"));
    m_comicItalic = addCheck(form, QStringLiteral("斜体"),
                             QStringLiteral("comicItalicCheck"));

    m_comicShape = addCombo(form, QStringLiteral("气泡形状"),
                            QStringLiteral("comicShapeCombo"));
    m_comicShape->addItem(QStringLiteral("椭圆"), QStringLiteral("ellipse"));
    m_comicShape->addItem(QStringLiteral("矩形"), QStringLiteral("rect"));
    m_comicShape->addItem(QStringLiteral("云朵"), QStringLiteral("cloud"));
    m_comicShape->addItem(QStringLiteral("爆炸"), QStringLiteral("burst"));

    m_comicBoxWidth = addDouble(form, QStringLiteral("气泡宽"),
                                QStringLiteral("comicBoxWidthSpin"), 1.0,
                                1000000.0, 1.0, 2, QStringLiteral(" px"));
    m_comicBoxHeight = addDouble(form, QStringLiteral("气泡高"),
                                 QStringLiteral("comicBoxHeightSpin"), 1.0,
                                 1000000.0, 1.0, 2, QStringLiteral(" px"));
    m_comicFillColor = addColor(form, QStringLiteral("气泡填充色"),
                                QStringLiteral("comicFillColorButton"),
                                QColor(QStringLiteral("#ffffff")));
    m_comicBubbleStrokeColor =
        addColor(form, QStringLiteral("气泡描边色"),
                 QStringLiteral("comicBubbleStrokeColorButton"),
                 QColor(QStringLiteral("#18181c")));
    m_comicBubbleStrokeWidth =
        addDouble(form, QStringLiteral("气泡描边宽度"),
                  QStringLiteral("comicBubbleStrokeWidthSpin"), 0.0, 1000.0,
                  0.5, 2, QStringLiteral(" px"));
    m_comicCornerRadius =
        addDouble(form, QStringLiteral("圆角"),
                  QStringLiteral("comicCornerRadiusSpin"), 0.0, 1000000.0, 1.0,
                  2, QStringLiteral(" px"));

    m_comicTail = addCheck(form, QStringLiteral("显示尾巴"),
                           QStringLiteral("comicTailCheck"));
    m_comicTailX = addDouble(form, QStringLiteral("尾巴 X"),
                             QStringLiteral("comicTailXSpin"), -16.0, 16.0, 0.05,
                             3);
    m_comicTailY = addDouble(form, QStringLiteral("尾巴 Y"),
                             QStringLiteral("comicTailYSpin"), -16.0, 16.0, 0.05,
                             3);
    m_comicTailX->setToolTip(
        QStringLiteral("尾巴尖相对半宽的偏移；跟随实体缩放。"));
    m_comicTailY->setToolTip(
        QStringLiteral("尾巴尖相对半高的偏移；跟随实体缩放。"));

    m_comicRotation = addDouble(form, QStringLiteral("气泡旋转 (°)"),
                                QStringLiteral("comicRotationSpin"), -36000.0,
                                36000.0, 1.0, 2);
    m_comicPadding = addDouble(form, QStringLiteral("内边距"),
                               QStringLiteral("comicPaddingSpin"), 0.0,
                               100000.0, 1.0, 2, QStringLiteral(" px"));
    m_comicSeed = addInt(form, QStringLiteral("随机种子"),
                         QStringLiteral("comicSeedSpin"), kSeedMinimum,
                         kSeedMaximum);

    m_comicPreview = new QLabel(form->parentWidget());
    m_comicPreview->setObjectName(QStringLiteral("comicPreviewLabel"));
    m_comicPreview->setAlignment(Qt::AlignCenter);
    m_comicPreview->setFixedSize(240, 150);
    m_comicPreview->setToolTip(
        QStringLiteral("与「排版…」对话框相同的文字预览；不绘制气泡轮廓。"));
    form->addRow(QStringLiteral("预览"), m_comicPreview);
}

void SpecialContentEditor::buildRadialPage()
{
    QFormLayout* form = addPage(QStringLiteral("radialLinesPage"));
    // 0 is kept representable so a typed 0 reaches validation and is rejected
    // with a status message instead of being silently clamped to 1.
    m_radialCount = addInt(form, QStringLiteral("线条数"),
                           QStringLiteral("radialCountSpin"), 0, 512);
    m_radialCount->setToolTip(QStringLiteral("有效范围 1..512；0 不提交。"));
    m_radialRadius = addDouble(form, QStringLiteral("半径"),
                               QStringLiteral("radialRadiusSpin"), 1.0,
                               1000000.0, 10.0, 2, QStringLiteral(" px"));
    m_radialInnerRatio =
        addDouble(form, QStringLiteral("内圈比例"),
                  QStringLiteral("radialInnerRatioSpin"), 0.0, 0.95, 0.01, 3);
    m_radialWidth = addDouble(form, QStringLiteral("线宽"),
                              QStringLiteral("radialWidthSpin"), 0.1, 100000.0,
                              1.0, 2, QStringLiteral(" px"));
    m_radialAngle = addDouble(form, QStringLiteral("角度 (°)"),
                              QStringLiteral("radialAngleSpin"), -36000.0,
                              36000.0, 1.0, 2);
    m_radialColor = addColor(form, QStringLiteral("颜色"),
                             QStringLiteral("radialColorButton"),
                             QColor(QStringLiteral("#ffffff")));
    m_radialSeed = addInt(form, QStringLiteral("随机种子"),
                          QStringLiteral("radialSeedSpin"), kSeedMinimum,
                          kSeedMaximum);
}

void SpecialContentEditor::buildFlowPage()
{
    QFormLayout* form = addPage(QStringLiteral("flowLinesPage"));
    m_flowCount = addInt(form, QStringLiteral("线条数"),
                         QStringLiteral("flowCountSpin"), 0, 512);
    m_flowCount->setToolTip(QStringLiteral("有效范围 1..512；0 不提交。"));
    m_flowLength = addDouble(form, QStringLiteral("长度"),
                             QStringLiteral("flowLengthSpin"), 1.0, 1000000.0,
                             10.0, 2, QStringLiteral(" px"));
    m_flowSpacing = addDouble(form, QStringLiteral("间距"),
                              QStringLiteral("flowSpacingSpin"), 0.1,
                              1000000.0, 1.0, 2, QStringLiteral(" px"));
    m_flowThickness = addDouble(form, QStringLiteral("粗细"),
                                QStringLiteral("flowThicknessSpin"), 0.1,
                                100000.0, 1.0, 2, QStringLiteral(" px"));
    m_flowArea = addDouble(form, QStringLiteral("分布范围"),
                           QStringLiteral("flowAreaSpin"), 1.0, 10000000.0,
                           10.0, 2, QStringLiteral(" px"));
    m_flowBend = addDouble(form, QStringLiteral("弯曲"),
                           QStringLiteral("flowBendSpin"), -1000000.0,
                           1000000.0, 10.0, 2, QStringLiteral(" px"));
    m_flowAngle = addDouble(form, QStringLiteral("角度 (°)"),
                            QStringLiteral("flowAngleSpin"), -36000.0, 36000.0,
                            1.0, 2);
    m_flowPhase = addDouble(form, QStringLiteral("相位"),
                            QStringLiteral("flowPhaseSpin"), 0.0, 1.0, 0.05,
                            3);
    m_flowColor = addColor(form, QStringLiteral("颜色"),
                           QStringLiteral("flowColorButton"),
                           QColor(QStringLiteral("#ffffff")));
    m_flowSeed = addInt(form, QStringLiteral("随机种子"),
                        QStringLiteral("flowSeedSpin"), kSeedMinimum,
                        kSeedMaximum);
}

void SpecialContentEditor::buildHalftonePage()
{
    QFormLayout* form = addPage(QStringLiteral("halftoneDotsPage"));
    m_halftoneLayout = addCombo(form, QStringLiteral("排列"),
                                QStringLiteral("halftoneLayoutCombo"));
    m_halftoneLayout->addItem(QStringLiteral("网格"), QStringLiteral("grid"));
    m_halftoneLayout->addItem(QStringLiteral("径向"), QStringLiteral("radial"));

    m_halftoneArea = addDouble(form, QStringLiteral("网格范围"),
                               QStringLiteral("halftoneAreaSpin"), 1.0,
                               10000000.0, 10.0, 2, QStringLiteral(" px"));
    m_halftoneRadius = addDouble(form, QStringLiteral("径向半径"),
                                 QStringLiteral("halftoneRadiusSpin"), 1.0,
                                 1000000.0, 10.0, 2, QStringLiteral(" px"));
    m_halftoneSpacing = addDouble(form, QStringLiteral("点间距"),
                                  QStringLiteral("halftoneSpacingSpin"), 0.1,
                                  100000.0, 1.0, 2, QStringLiteral(" px"));
    m_halftoneDotSize = addDouble(form, QStringLiteral("点大小"),
                                  QStringLiteral("halftoneDotSizeSpin"), 0.1,
                                  100000.0, 1.0, 2, QStringLiteral(" px"));
    m_halftoneAngle = addDouble(form, QStringLiteral("网格角度 (°)"),
                                QStringLiteral("halftoneAngleSpin"), -36000.0,
                                36000.0, 1.0, 2);
    m_halftoneGradientAngle =
        addDouble(form, QStringLiteral("渐变角度 (°)"),
                  QStringLiteral("halftoneGradientAngleSpin"), -36000.0,
                  36000.0, 1.0, 2);
    m_halftoneMinRatio =
        addDouble(form, QStringLiteral("最小比例"),
                  QStringLiteral("halftoneMinRatioSpin"), 0.0, 1.0, 0.01, 3);
    m_halftoneJitter =
        addDouble(form, QStringLiteral("抖动"),
                  QStringLiteral("halftoneJitterSpin"), 0.0, 1.0, 0.01, 3);
    m_halftoneColor = addColor(form, QStringLiteral("颜色"),
                               QStringLiteral("halftoneColorButton"),
                               QColor(QStringLiteral("#ffffff")));
    m_halftoneSeed = addInt(form, QStringLiteral("随机种子"),
                            QStringLiteral("halftoneSeedSpin"), kSeedMinimum,
                            kSeedMaximum);

    m_halftoneArea->setToolTip(
        QStringLiteral("网格模式使用；area/spacing 超过 512 会被模型拒绝。"));
    m_halftoneRadius->setToolTip(
        QStringLiteral("径向模式使用；radius/spacing 超过 256 会被模型拒绝。"));
}

void SpecialContentEditor::buildBrandWallPage()
{
    QFormLayout* form = addPage(QStringLiteral("brandWallPage"));

    m_brandLogos = addPlain(form, QStringLiteral("Logo 标签"),
                            QStringLiteral("brandLogosEdit"));
    m_brandLogos->setMinimumHeight(96);
    m_brandLogos->setToolTip(
        QStringLiteral("每行一个，也可用逗号分隔；最多 64 个，每项 1..128 字。"));

    m_brandColumns = addInt(form, QStringLiteral("列数"),
                            QStringLiteral("brandColumnsSpin"), 0, 64);
    m_brandColumns->setToolTip(QStringLiteral("有效范围 1..64；0 不提交。"));
    m_brandRows = addInt(form, QStringLiteral("行数"),
                         QStringLiteral("brandRowsSpin"), 0, 64);
    m_brandRows->setToolTip(QStringLiteral("有效范围 1..64；0 不提交。"));
    m_brandSpacing = addDouble(form, QStringLiteral("间距"),
                               QStringLiteral("brandSpacingSpin"), 0.0, 512.0,
                               1.0, 2, QStringLiteral(" px"));
    m_brandLogoSize = addDouble(form, QStringLiteral("Logo 尺寸"),
                                QStringLiteral("brandLogoSizeSpin"), 1.0,
                                1024.0, 1.0, 2, QStringLiteral(" px"));
    m_brandColors = addLine(form, QStringLiteral("颜色"),
                            QStringLiteral("brandColorsEdit"));
    m_brandColors->setToolTip(
        QStringLiteral("逗号分隔，#rgb / #rrggbb / #aarrggbb；至少一个。"));
    m_brandAlternate = addCheck(form, QStringLiteral("棋盘交替"),
                                QStringLiteral("brandAlternateCheck"));
    m_brandAngle = addDouble(form, QStringLiteral("旋转 (°)"),
                             QStringLiteral("brandAngleSpin"), -360.0, 360.0,
                             1.0, 2);
    m_brandBackground = addLine(form, QStringLiteral("背景色"),
                                QStringLiteral("brandBackgroundEdit"));
    m_brandBackground->setPlaceholderText(QStringLiteral("空 = 透明"));
    m_brandBackground->setToolTip(
        QStringLiteral("留空透明；填写 #rgb / #rrggbb / #aarrggbb。"));
    m_brandSeed = addInt(form, QStringLiteral("随机种子"),
                         QStringLiteral("brandSeedSpin"), kSeedMinimum,
                         kSeedMaximum);
}

void SpecialContentEditor::loadComic(const model::ComicTextContent& content)
{
    m_comicText->setPlainText(content.text);
    m_comicFontFamily = content.fontFamily;
    m_comicFont->setCurrentFont(content.fontFamily.isEmpty()
                                    ? QFont()
                                    : QFont(content.fontFamily));
    m_comicPixelSize->setValue(content.pixelSize);
    loadColor(m_comicColor, content.color, Qt::white);
    loadColor(m_comicStrokeColor, content.strokeColor, Qt::black);
    m_comicStrokeWidth->setValue(content.strokeWidth);
    selectComboData(m_comicAlign, content.align);
    m_comicLineHeight->setValue(content.lineHeight);
    m_comicBold->setChecked(content.bold);
    m_comicItalic->setChecked(content.italic);
    selectComboString(m_comicShape, content.shape);
    m_comicBoxWidth->setValue(content.boxWidth);
    m_comicBoxHeight->setValue(content.boxHeight);
    loadColor(m_comicFillColor, content.fillColor, Qt::white);
    loadColor(m_comicBubbleStrokeColor, content.bubbleStrokeColor, Qt::black);
    m_comicBubbleStrokeWidth->setValue(content.bubbleStrokeWidth);
    m_comicCornerRadius->setValue(content.cornerRadius);
    m_comicTail->setChecked(content.tail);
    m_comicTailX->setValue(content.tailX);
    m_comicTailY->setValue(content.tailY);
    m_comicRotation->setValue(content.rotation);
    m_comicPadding->setValue(content.padding);
    m_comicSeed->setValue(content.seed);
}

void SpecialContentEditor::loadRadial(const model::RadialLinesContent& content)
{
    m_radialCount->setValue(content.count);
    m_radialRadius->setValue(content.radius);
    m_radialInnerRatio->setValue(content.innerRatio);
    m_radialWidth->setValue(content.width);
    m_radialAngle->setValue(content.angle);
    loadColor(m_radialColor, content.color, Qt::white);
    m_radialSeed->setValue(content.seed);
}

void SpecialContentEditor::loadFlow(const model::FlowLinesContent& content)
{
    m_flowCount->setValue(content.count);
    m_flowLength->setValue(content.length);
    m_flowSpacing->setValue(content.spacing);
    m_flowThickness->setValue(content.thickness);
    m_flowArea->setValue(content.area);
    m_flowBend->setValue(content.bend);
    m_flowAngle->setValue(content.angle);
    m_flowPhase->setValue(content.phase);
    loadColor(m_flowColor, content.color, Qt::white);
    m_flowSeed->setValue(content.seed);
}

void SpecialContentEditor::loadHalftone(
    const model::HalftoneDotsContent& content)
{
    selectComboString(m_halftoneLayout, content.layout);
    m_halftoneArea->setValue(content.area);
    m_halftoneRadius->setValue(content.radius);
    m_halftoneSpacing->setValue(content.spacing);
    m_halftoneDotSize->setValue(content.dotSize);
    m_halftoneAngle->setValue(content.angle);
    m_halftoneGradientAngle->setValue(content.gradientAngle);
    m_halftoneMinRatio->setValue(content.minRatio);
    m_halftoneJitter->setValue(content.jitter);
    loadColor(m_halftoneColor, content.color, Qt::white);
    m_halftoneSeed->setValue(content.seed);
}

void SpecialContentEditor::loadBrandWall(
    const model::BrandWallContent& content)
{
    m_brandLogos->setPlainText(content.logos.join(QLatin1Char('\n')));
    m_brandColumns->setValue(content.columns);
    m_brandRows->setValue(content.rows);
    m_brandSpacing->setValue(content.spacing);
    m_brandLogoSize->setValue(content.logoSize);
    m_brandColors->setText(content.colors.join(QStringLiteral(", ")));
    m_brandAlternate->setChecked(content.alternate);
    m_brandAngle->setValue(content.angle);
    m_brandBackground->setText(content.background);
    m_brandSeed->setValue(content.seed);
}

void SpecialContentEditor::saveComic(model::Content& content) const
{
    model::ComicTextContent& comic = content.comic;
    comic.text = m_comicText->toPlainText();
    comic.fontFamily = m_comicFontFamily;
    comic.pixelSize = m_comicPixelSize->value();
    comic.color = colorTextValue(m_comicColor, QStringLiteral("#111111"));
    comic.strokeColor =
        colorTextValue(m_comicStrokeColor, QStringLiteral("#000000"));
    comic.strokeWidth = m_comicStrokeWidth->value();
    comic.align = m_comicAlign->currentData().toInt();
    comic.lineHeight = m_comicLineHeight->value();
    comic.bold = m_comicBold->isChecked();
    comic.italic = m_comicItalic->isChecked();
    comic.shape = m_comicShape->currentData().toString();
    comic.boxWidth = m_comicBoxWidth->value();
    comic.boxHeight = m_comicBoxHeight->value();
    comic.fillColor =
        colorTextValue(m_comicFillColor, QStringLiteral("#ffffff"));
    comic.bubbleStrokeColor = colorTextValue(m_comicBubbleStrokeColor,
                                             QStringLiteral("#18181c"));
    comic.bubbleStrokeWidth = m_comicBubbleStrokeWidth->value();
    comic.cornerRadius = m_comicCornerRadius->value();
    comic.tail = m_comicTail->isChecked();
    comic.tailX = m_comicTailX->value();
    comic.tailY = m_comicTailY->value();
    comic.rotation = m_comicRotation->value();
    comic.padding = m_comicPadding->value();
    comic.seed = m_comicSeed->value();
}

void SpecialContentEditor::saveRadial(model::Content& content) const
{
    model::RadialLinesContent& radial = content.radial;
    radial.count = m_radialCount->value();
    radial.radius = m_radialRadius->value();
    radial.innerRatio = m_radialInnerRatio->value();
    radial.width = m_radialWidth->value();
    radial.angle = m_radialAngle->value();
    radial.color = colorTextValue(m_radialColor, QStringLiteral("#ffffff"));
    radial.seed = m_radialSeed->value();
}

void SpecialContentEditor::saveFlow(model::Content& content) const
{
    model::FlowLinesContent& flow = content.flow;
    flow.count = m_flowCount->value();
    flow.length = m_flowLength->value();
    flow.spacing = m_flowSpacing->value();
    flow.thickness = m_flowThickness->value();
    flow.area = m_flowArea->value();
    flow.bend = m_flowBend->value();
    flow.angle = m_flowAngle->value();
    flow.phase = m_flowPhase->value();
    flow.color = colorTextValue(m_flowColor, QStringLiteral("#ffffff"));
    flow.seed = m_flowSeed->value();
}

void SpecialContentEditor::saveHalftone(model::Content& content) const
{
    model::HalftoneDotsContent& halftone = content.halftone;
    halftone.layout = m_halftoneLayout->currentData().toString();
    halftone.area = m_halftoneArea->value();
    halftone.radius = m_halftoneRadius->value();
    halftone.spacing = m_halftoneSpacing->value();
    halftone.dotSize = m_halftoneDotSize->value();
    halftone.angle = m_halftoneAngle->value();
    halftone.gradientAngle = m_halftoneGradientAngle->value();
    halftone.minRatio = m_halftoneMinRatio->value();
    halftone.jitter = m_halftoneJitter->value();
    halftone.color =
        colorTextValue(m_halftoneColor, QStringLiteral("#ffffff"));
    halftone.seed = m_halftoneSeed->value();
}

void SpecialContentEditor::saveBrandWall(model::Content& content) const
{
    model::BrandWallContent& wall = content.brandWall;
    wall.logos = splitList(m_brandLogos->toPlainText());
    wall.columns = m_brandColumns->value();
    wall.rows = m_brandRows->value();
    wall.spacing = m_brandSpacing->value();
    wall.logoSize = m_brandLogoSize->value();
    wall.colors = splitList(m_brandColors->text());
    wall.alternate = m_brandAlternate->isChecked();
    wall.angle = m_brandAngle->value();
    wall.background = m_brandBackground->text().trimmed();
    wall.seed = m_brandSeed->value();
}

} // namespace pvf::next
