#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Content.h"
#include "model/Entity.h"
#include "ui/TextLayoutDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

using namespace ui::detail;

namespace {

TextStyle textStyleFromContent(const model::TextContent& content)
{
    TextStyle style;
    style.text = content.text;
    style.fontFamily = content.fontFamily;
    style.pixelSize = content.pixelSize;
    style.color = QColor(content.color);
    if (!style.color.isValid())
        style.color = Qt::white;
    style.strokeColor = QColor(content.strokeColor);
    if (!style.strokeColor.isValid())
        style.strokeColor = Qt::black;
    style.strokeWidth = content.strokeWidth;
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

model::TextContent textContentFromStyle(const model::TextContent& original,
                                        const TextStyle& style)
{
    model::TextContent out = original;
    out.text = style.text;
    out.fontFamily = style.fontFamily;
    out.pixelSize = style.pixelSize;
    out.color = colorString(style.color);
    out.strokeColor = colorString(style.strokeColor);
    out.strokeWidth = style.strokeWidth;
    out.align = style.align;
    out.lineHeight = style.lineHeight;
    out.letterSpacing = style.letterSpacing;
    out.padding = style.padding;
    out.boxWidth = style.boxWidth;
    out.boxHeight = style.boxHeight;
    out.bold = style.bold;
    out.italic = style.italic;
    out.underline = style.underline;
    out.vertical = style.vertical;
    out.verticalRtl = style.verticalRtl;
    return out;
}

} // namespace

void PropertiesPanel::buildTextSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);
    m_textForm = form;

    m_textContent = new QLineEdit(content);
    m_textContent->setObjectName(QStringLiteral("textContent"));
    connect(m_textContent, &QLineEdit::textEdited, this,
            [this](const QString& text) { applyData(QStringLiteral("text"), text); });
    form->addRow(QStringLiteral("内容"), m_textContent);

    m_textFont = new QFontComboBox(content);
    m_textFont->setObjectName(QStringLiteral("textFont"));
    // Keep the property column inside the legacy 300px panel: the font model
    // otherwise asks for the width of the longest installed family.
    m_textFont->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_textFont->setMinimumContentsLength(10);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        applyData(QStringLiteral("font"), font.family());
    });
    form->addRow(QStringLiteral("字体"), m_textFont);

    m_textSize = new QSpinBox(content);
    m_textSize->setObjectName(QStringLiteral("textSize"));
    m_textSize->setRange(8, 512);
    m_textSize->setValue(96);
    m_textSize->setSuffix(QStringLiteral(" px"));
    connect(m_textSize, &QSpinBox::valueChanged, this,
            [this](int size) { applyData(QStringLiteral("size"), size); });
    form->addRow(QStringLiteral("字号"), m_textSize);

    m_textColorButton = new QPushButton(content);
    m_textColorButton->setObjectName(QStringLiteral("textColor"));
    setColorButton(m_textColorButton, m_textColorValue);
    connect(m_textColorButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_textColorValue, QStringLiteral("文字颜色"));
        if (!color.isValid())
            return;
        m_textColorValue = color;
        setColorButton(m_textColorButton, color);
        applyData(QStringLiteral("color"), color);
    });
    form->addRow(QStringLiteral("颜色"), m_textColorButton);

    m_textStrokeWidth = new QDoubleSpinBox(content);
    m_textStrokeWidth->setObjectName(QStringLiteral("textStrokeWidth"));
    m_textStrokeWidth->setRange(0, 24);
    m_textStrokeWidth->setDecimals(1);
    m_textStrokeWidth->setSingleStep(0.5);
    m_textStrokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_textStrokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applyData(QStringLiteral("textStrokeWidth"), value);
            });
    form->addRow(QStringLiteral("描边粗细"), m_textStrokeWidth);

    m_textStrokeColorButton = new QPushButton(content);
    m_textStrokeColorButton->setObjectName(QStringLiteral("textStrokeColor"));
    setColorButton(m_textStrokeColorButton, Qt::black);
    connect(m_textStrokeColorButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_textStrokeColorValue,
                                       QStringLiteral("文字描边颜色"));
        if (!color.isValid())
            return;
        m_textStrokeColorValue = color;
        setColorButton(m_textStrokeColorButton, color);
        applyData(QStringLiteral("textStrokeColor"), color);
    });
    form->addRow(QStringLiteral("描边颜色"), m_textStrokeColorButton);

    m_textAlign = new QComboBox(content);
    m_textAlign->setObjectName(QStringLiteral("textAlign"));
    m_textAlign->addItem(QStringLiteral("居中"), int(Qt::AlignCenter));
    m_textAlign->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_textAlign->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight | Qt::AlignVCenter));
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("align"), m_textAlign->currentData());
    });
    form->addRow(QStringLiteral("对齐"), m_textAlign);

    m_textDirection = new QComboBox(content);
    m_textDirection->setObjectName(QStringLiteral("textDirection"));
    m_textDirection->addItem(QStringLiteral("横排"), 0);
    m_textDirection->addItem(QStringLiteral("竖排 · 从右到左"), 1);
    m_textDirection->addItem(QStringLiteral("竖排 · 从左到右"), 2);
    connect(m_textDirection, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_sessionMode)
        {
            QVariantMap data;
            data.insert(QStringLiteral("vertical"), index > 0);
            data.insert(QStringLiteral("verticalRtl"), index != 2);
            if (!m_sessionUuid.isEmpty())
            {
                runSessionContentEdit(
                    QStringLiteral("pvf: edit text direction"),
                    [&data](model::Content& content) {
                        if (content.kind != ContentKind::Text)
                            return false;
                        content.text.vertical =
                            data.value(QStringLiteral("vertical")).toBool();
                        content.text.verticalRtl =
                            data.value(QStringLiteral("verticalRtl")).toBool();
                        return true;
                    });
            }
            return;
        }
        if (m_entity == kInvalidEntity)
            return;
        QVariantMap data;
        data.insert(QStringLiteral("vertical"), index > 0);
        data.insert(QStringLiteral("verticalRtl"), index != 2);
        emit dataEdited(m_entity, data);
    });
    form->addRow(QStringLiteral("排版方向"), m_textDirection);

    auto* styleRow = new QWidget(content);
    auto* styleLayout = new QHBoxLayout(styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(10);
    m_textBold = new QCheckBox(QStringLiteral("加粗"), styleRow);
    m_textBold->setObjectName(QStringLiteral("textBold"));
    m_textItalic = new QCheckBox(QStringLiteral("斜体"), styleRow);
    m_textItalic->setObjectName(QStringLiteral("textItalic"));
    m_textUnderline = new QCheckBox(QStringLiteral("下划线"), styleRow);
    m_textUnderline->setObjectName(QStringLiteral("textUnderline"));
    connect(m_textBold, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("bold"), on); });
    connect(m_textItalic, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("italic"), on); });
    connect(m_textUnderline, &QCheckBox::toggled, this,
            [this](bool on) { applyData(QStringLiteral("underline"), on); });
    styleLayout->addWidget(m_textBold);
    styleLayout->addWidget(m_textItalic);
    styleLayout->addWidget(m_textUnderline);
    styleLayout->addStretch(1);
    form->addRow(QStringLiteral("字形"), styleRow);

    auto* layoutButton = new QPushButton(QStringLiteral("排版…"), content);
    layoutButton->setObjectName(QStringLiteral("textLayoutButton"));
    layoutButton->setToolTip(
        QStringLiteral("打开文字排版编辑器：换行宽度、行距、内边距"));
    connect(layoutButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            const model::Entity* entity = sessionEntity();
            if (!entity || !sessionEditable()
                || entity->content.kind != ContentKind::Text)
                return;
            TextLayoutDialog dialog(textStyleFromContent(entity->content.text),
                                    this);
            if (dialog.exec() != QDialog::Accepted)
            {
                refresh();
                return;
            }
            const model::TextContent edited = textContentFromStyle(
                entity->content.text, dialog.style());
            runSessionContentEdit(
                QStringLiteral("pvf: edit text layout"),
                [edited](model::Content& content) {
                    if (content.kind != ContentKind::Text)
                        return false;
                    content.text = edited;
                    return true;
                });
            return;
        }
    });
    form->addRow(layoutButton);

    m_textSection = new CollapsibleSection(QStringLiteral("文字"), content, this);
    m_textSection->setExpanded(true);
    layout->addWidget(m_textSection);
}

void PropertiesPanel::addDataRow(QFormLayout* form, const QString& label,
                                 const QString& key, double lo, double hi, int decimals,
                                 double step)
{
    auto* spin = new QDoubleSpinBox(form->parentWidget());
    spin->setObjectName(QStringLiteral("data_") + key);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    connect(spin, &QDoubleSpinBox::valueChanged, this,
            [this, key](double value) { emitDataEdit(key, value); });
    DataRow row;
    row.key = key;
    row.spin = spin;
    m_dataRows.append(row);
    form->addRow(label, spin);
}

void PropertiesPanel::addDataCount(QFormLayout* form, const QString& label,
                                   const QString& key, int lo, int hi)
{
    auto* spin = new QSpinBox(form->parentWidget());
    spin->setObjectName(QStringLiteral("data_") + key);
    spin->setRange(lo, hi);
    connect(spin, &QSpinBox::valueChanged, this,
            [this, key](int value) { emitDataEdit(key, value); });
    DataRow row;
    row.key = key;
    row.count = spin;
    m_dataRows.append(row);
    form->addRow(label, spin);
}

void PropertiesPanel::addDataColor(QFormLayout* form, const QString& label,
                                   const QString& key, const QColor& initial)
{
    auto* button = new QPushButton(form->parentWidget());
    button->setObjectName(QStringLiteral("data_") + key);
    m_dataColors.insert(key, initial);
    setColorButton(button, initial);
    connect(button, &QPushButton::clicked, this, [this, key, button] {
        const QColor color = pickColor(m_dataColors.value(key), QStringLiteral("颜色"));
        if (!color.isValid())
            return;
        m_dataColors.insert(key, color);
        setColorButton(button, color);
        emitDataEdit(key, color);
    });
    DataRow row;
    row.key = key;
    row.color = button;
    m_dataRows.append(row);
    form->addRow(label, button);
}

void PropertiesPanel::emitDataEdit(const QString& key, const QVariant& value)
{
    if (m_sessionMode)
    {
        applySessionData(key, value);
        return;
    }
    if (m_entity == kInvalidEntity)
        return;
    QVariantMap data;
    data.insert(key, value);
    emit dataEdited(m_entity, data);
}

void PropertiesPanel::buildComicSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_comicShape = new QComboBox(content);
    m_comicShape->addItem(QStringLiteral("椭圆"), 0);
    m_comicShape->addItem(QStringLiteral("矩形"), 1);
    m_comicShape->addItem(QStringLiteral("云朵"), 2);
    m_comicShape->addItem(QStringLiteral("爆炸"), 3);
    connect(m_comicShape, &QComboBox::currentIndexChanged, this, [this](int) {
        emitDataEdit(QStringLiteral("bubbleShape"), m_comicShape->currentData());
    });
    form->addRow(QStringLiteral("形状"), m_comicShape);

    m_comicTail = new QCheckBox(QStringLiteral("尖角"), content);
    m_comicTail->setToolTip(QStringLiteral("拖动画面上的小圆点可以移动尖角"));
    connect(m_comicTail, &QCheckBox::toggled, this, [this](bool on) {
        emitDataEdit(QStringLiteral("bubbleTail"), on);
    });
    form->addRow(m_comicTail);

    addDataRow(form, QStringLiteral("圆角"), QStringLiteral("bubbleRadius"), 0, 400, 0, 1);
    addDataRow(form, QStringLiteral("尖角 X"), QStringLiteral("bubbleTipX"), -3, 3, 2, 0.05);
    addDataRow(form, QStringLiteral("尖角 Y"), QStringLiteral("bubbleTipY"), -3, 3, 2, 0.05);
    addDataRow(form, QStringLiteral("描边宽度"), QStringLiteral("strokeWidth"), 0, 60, 1, 1);
    addDataColor(form, QStringLiteral("底色"), QStringLiteral("fill"), QColor(Qt::white));
    addDataColor(form, QStringLiteral("描边色"), QStringLiteral("stroke"),
                 QColor(24, 24, 28));

    m_comicSection = new CollapsibleSection(QStringLiteral("漫画框"), content, this);
    m_comicSection->setExpanded(true);
    layout->addWidget(m_comicSection);
}

void PropertiesPanel::buildLinesSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);
    m_linesStack = new QStackedWidget(content);

    auto* radial = new QWidget(m_linesStack);
    auto* radialForm = new QFormLayout(radial);
    radialForm->setContentsMargins(0, 0, 0, 0);
    addDataCount(radialForm, QStringLiteral("线条数"), QStringLiteral("count"), 1, 400);
    addDataRow(radialForm, QStringLiteral("半径"), QStringLiteral("radius"), 10, 4000, 0, 10);
    addDataRow(radialForm, QStringLiteral("线宽"), QStringLiteral("width"), 1, 120, 1, 1);
    addDataRow(radialForm, QStringLiteral("旋转 °/s"), QStringLiteral("spin"), -180, 180, 1,
               1);
    addDataRow(radialForm, QStringLiteral("随机速度 Hz"), QStringLiteral("flicker"), 0, 60, 1,
               1);
    addDataRow(radialForm, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(radialForm, QStringLiteral("颜色"), QStringLiteral("color"), QColor(Qt::white));
    m_linesStack->addWidget(radial);

    auto* flow = new QWidget(m_linesStack);
    auto* flowForm = new QFormLayout(flow);
    flowForm->setContentsMargins(0, 0, 0, 0);
    addDataCount(flowForm, QStringLiteral("线条数"), QStringLiteral("count"), 1, 400);
    addDataRow(flowForm, QStringLiteral("长度"), QStringLiteral("length"), 20, 4000, 0, 10);
    addDataRow(flowForm, QStringLiteral("间距"), QStringLiteral("spacing"), 5, 600, 0, 5);
    addDataRow(flowForm, QStringLiteral("粗细"), QStringLiteral("thickness"), 1, 80, 1, 1);
    addDataRow(flowForm, QStringLiteral("速度 px/s"), QStringLiteral("speed"), 0, 6000, 0, 50);
    addDataRow(flowForm, QStringLiteral("角度"), QStringLiteral("angle"), -180, 180, 0, 5);
    addDataRow(flowForm, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(flowForm, QStringLiteral("颜色"), QStringLiteral("color"), QColor(Qt::white));
    m_linesStack->addWidget(flow);

    box->addWidget(m_linesStack);
    m_linesSection = new CollapsibleSection(QStringLiteral("装饰线条"), content, this);
    m_linesSection->setExpanded(true);
    layout->addWidget(m_linesSection);
}

void PropertiesPanel::buildBrandWallSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_wallInfo = new QLabel(content);
    m_wallInfo->setWordWrap(true);
    m_wallInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_wallInfo->setMinimumWidth(0);
    form->addRow(m_wallInfo);

    auto* edit = new QPushButton(QStringLiteral("编辑瓷砖墙…"), content);
    edit->setObjectName(QStringLiteral("wallEditButton"));
    connect(edit, &QPushButton::clicked, this, [this] {
    });
    form->addRow(edit);

    m_wallSection = new CollapsibleSection(QStringLiteral("品牌瓷砖墙"), content, this);
    m_wallSection->setExpanded(true);
    layout->addWidget(m_wallSection);
}

void PropertiesPanel::buildHalftoneSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    addDataRow(form, QStringLiteral("范围"), QStringLiteral("area"), 100, 4000, 0, 50);
    addDataRow(form, QStringLiteral("点间距"), QStringLiteral("spacing"), 4, 200, 1, 1);
    addDataRow(form, QStringLiteral("最大点径"), QStringLiteral("dotSize"), 0.5, 60, 1,
               0.5);
    addDataRow(form, QStringLiteral("网格角度"), QStringLiteral("angle"), -180, 180, 1,
               5);
    addDataRow(form, QStringLiteral("渐隐角度"), QStringLiteral("gradientAngle"), -180,
               180, 1, 5);
    addDataRow(form, QStringLiteral("最小点比例"), QStringLiteral("minRatio"), 0, 1, 2,
               0.05);
    addDataRow(form, QStringLiteral("手绘抖动"), QStringLiteral("jitter"), 0, 1, 2, 0.05);
    addDataRow(form, QStringLiteral("随机种子"), QStringLiteral("seed"), 1, 9999, 0, 1);
    addDataColor(form, QStringLiteral("颜色"), QStringLiteral("color"), Qt::white);

    m_halftoneSection =
        new CollapsibleSection(QStringLiteral("漫画点阵"), content, this);
    m_halftoneSection->setExpanded(true);
    layout->addWidget(m_halftoneSection);
}

void PropertiesPanel::buildVectorSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_vectorShape = new QComboBox(content);
    m_vectorShape->setObjectName(QStringLiteral("vectorShape"));
    m_vectorShape->addItem(QStringLiteral("矩形"), 0);
    m_vectorShape->addItem(QStringLiteral("圆角矩形"), 3);
    m_vectorShape->addItem(QStringLiteral("椭圆"), 1);
    m_vectorShape->addItem(QStringLiteral("直线"), 2);
    m_vectorShape->addItem(QStringLiteral("三角形"), 4);
    m_vectorShape->addItem(QStringLiteral("心形"), 5);
    m_vectorShape->addItem(QStringLiteral("星形"), 6);
    m_vectorShape->addItem(QStringLiteral("多边形"), 7);
    m_vectorShape->addItem(QStringLiteral("箭头"), 8);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("shape"), m_vectorShape->currentData());
    });
    form->addRow(QStringLiteral("形状"), m_vectorShape);

    m_vectorCorner = new QDoubleSpinBox(content);
    m_vectorCorner->setObjectName(QStringLiteral("vectorCorner"));
    m_vectorCorner->setRange(0, 2000);
    m_vectorCorner->setDecimals(1);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    connect(m_vectorCorner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("cornerRadius"), value); });
    form->addRow(QStringLiteral("圆角"), m_vectorCorner);

    m_vectorSides = new QSpinBox(content);
    m_vectorSides->setObjectName(QStringLiteral("vectorSides"));
    m_vectorSides->setRange(3, 64);
    connect(m_vectorSides, &QSpinBox::valueChanged, this,
            [this](int value) { applyData(QStringLiteral("sides"), value); });
    form->addRow(QStringLiteral("边数"), m_vectorSides);

    m_vectorInner = new QDoubleSpinBox(content);
    m_vectorInner->setObjectName(QStringLiteral("vectorInner"));
    m_vectorInner->setRange(0.05, 1.0);
    m_vectorInner->setSingleStep(0.05);
    m_vectorInner->setDecimals(2);
    connect(m_vectorInner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("innerRatio"), value); });
    form->addRow(QStringLiteral("内径比"), m_vectorInner);

    m_vectorWidth = new QDoubleSpinBox(content);
    m_vectorWidth->setObjectName(QStringLiteral("vectorWidth"));
    m_vectorWidth->setRange(1, 20000);
    m_vectorWidth->setDecimals(1);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    connect(m_vectorWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("width"), value); });
    form->addRow(QStringLiteral("宽度"), m_vectorWidth);

    m_vectorHeight = new QDoubleSpinBox(content);
    m_vectorHeight->setObjectName(QStringLiteral("vectorHeight"));
    m_vectorHeight->setRange(1, 20000);
    m_vectorHeight->setDecimals(1);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    connect(m_vectorHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("height"), value); });
    form->addRow(QStringLiteral("高度"), m_vectorHeight);

    m_fillButton = new QPushButton(content);
    m_fillButton->setObjectName(QStringLiteral("vectorFill"));
    setColorButton(m_fillButton, m_fillValue);
    connect(m_fillButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_fillValue, QStringLiteral("填充颜色"));
        if (!color.isValid())
            return;
        m_fillValue = color;
        setColorButton(m_fillButton, color);
        applyData(QStringLiteral("fill"), color);
    });
    form->addRow(QStringLiteral("填充"), m_fillButton);

    m_strokeButton = new QPushButton(content);
    m_strokeButton->setObjectName(QStringLiteral("vectorStroke"));
    setColorButton(m_strokeButton, m_strokeValue);
    connect(m_strokeButton, &QPushButton::clicked, this, [this] {
        const QColor color = pickColor(m_strokeValue, QStringLiteral("描边颜色"));
        if (!color.isValid())
            return;
        m_strokeValue = color;
        setColorButton(m_strokeButton, color);
        applyData(QStringLiteral("stroke"), color);
    });
    form->addRow(QStringLiteral("描边"), m_strokeButton);

    m_strokeWidth = new QDoubleSpinBox(content);
    m_strokeWidth->setObjectName(QStringLiteral("vectorStrokeWidth"));
    m_strokeWidth->setRange(0, 200);
    m_strokeWidth->setDecimals(1);
    m_strokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_strokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("strokeWidth"), value); });
    form->addRow(QStringLiteral("描边宽度"), m_strokeWidth);

    m_vectorSection = new CollapsibleSection(QStringLiteral("矢量"), content, this);
    m_vectorSection->setExpanded(true);
    layout->addWidget(m_vectorSection);
}

void PropertiesPanel::buildMediaSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* mediaLayout = new QVBoxLayout(content);
    mediaLayout->setContentsMargins(10, 8, 10, 8);
    m_mediaLayout = mediaLayout;
    m_mediaInfo = new QLabel(content);
    m_mediaInfo->setWordWrap(true);
    m_mediaInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mediaInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_mediaInfo->setMinimumWidth(0);
    mediaLayout->addWidget(m_mediaInfo);
    m_replaceSource = new QPushButton(QStringLiteral("更换素材…"), content);
    m_replaceSource->setObjectName(QStringLiteral("replaceSourceButton"));
    connect(m_replaceSource, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            if (!m_sessionUuid.isEmpty())
                emit sessionReplaceSourceRequested(m_sessionUuid);
            return;
        }
        if (m_entity != kInvalidEntity)
            emit replaceSourceRequested(m_entity);
    });
    mediaLayout->addWidget(m_replaceSource);
    m_mediaSection = new CollapsibleSection(QStringLiteral("素材"), content, this);
    m_mediaSection->setExpanded(true);
    layout->addWidget(m_mediaSection);
}

void PropertiesPanel::buildPivotSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_usePivot = new QCheckBox(QStringLiteral("启用自定义轴心"), content);
    m_usePivot->setObjectName(QStringLiteral("usePivot"));
    form->addRow(m_usePivot);

    m_pivotX = new QDoubleSpinBox(content);
    m_pivotX->setObjectName(QStringLiteral("pivotX"));
    m_pivotX->setRange(-10000, 10000);
    m_pivotX->setDecimals(1);
    m_pivotX->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 X"), m_pivotX);

    m_pivotY = new QDoubleSpinBox(content);
    m_pivotY->setObjectName(QStringLiteral("pivotY"));
    m_pivotY->setRange(-10000, 10000);
    m_pivotY->setDecimals(1);
    m_pivotY->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 Y"), m_pivotY);

    auto* hint = new QLabel(QStringLiteral("轴心相对实体中心，旋转围绕轴心。"), content);
    hint->setWordWrap(true);
    form->addRow(hint);

    connect(m_usePivot, &QCheckBox::toggled, this, [this](bool on) {
        Q_UNUSED(on);
    });
    connect(m_pivotX, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_sessionMode)
        {
            sessionSetTrackValue(QStringLiteral("pivotX"), value);
            return;
        }
    });
    connect(m_pivotY, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_sessionMode)
        {
            sessionSetTrackValue(QStringLiteral("pivotY"), value);
            return;
        }
    });

    m_pivotSection = new CollapsibleSection(QStringLiteral("轴心"), content, this);
    m_pivotSection->setExpanded(false);
    layout->addWidget(m_pivotSection);
}

void PropertiesPanel::applyData(const QString& key, const QVariant& value)
{
    if (m_sessionMode)
    {
        applySessionData(key, value);
        return;
    }
    Q_UNUSED(key);
    Q_UNUSED(value);
}

void PropertiesPanel::applySessionData(const QString& key, const QVariant& value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    const ContentKind kind = entity->content.kind;
    if (kind == ContentKind::Text)
    {
        if (key == QLatin1String("size"))
        {
            sessionSetTrackValue(QStringLiteral("fontSize"), value.toInt());
            return;
        }
        if (key == QLatin1String("textStrokeWidth"))
        {
            sessionSetTrackValue(QStringLiteral("strokeWidth"),
                                 value.toDouble());
            return;
        }
        if (key == QLatin1String("lineHeight")
            || key == QLatin1String("letterSpacing")
            || key == QLatin1String("padding")
            || key == QLatin1String("boxWidth")
            || key == QLatin1String("boxHeight"))
        {
            sessionSetTrackValue(key, value.toDouble());
            return;
        }

        runSessionContentEdit(QStringLiteral("pvf: edit text content"),
                              [key, value](model::Content& content) {
                                  if (content.kind != ContentKind::Text)
                                      return false;
                                  model::TextContent& text = content.text;
                                  if (key == QLatin1String("text"))
                                      text.text = value.toString();
                                  else if (key == QLatin1String("font"))
                                      text.fontFamily = value.toString();
                                  else if (key == QLatin1String("color"))
                                      text.color = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("textStrokeColor"))
                                      text.strokeColor = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("align"))
                                      text.align = value.toInt();
                                  else if (key == QLatin1String("bold"))
                                      text.bold = value.toBool();
                                  else if (key == QLatin1String("italic"))
                                      text.italic = value.toBool();
                                  else if (key == QLatin1String("underline"))
                                      text.underline = value.toBool();
                                  else if (key == QLatin1String("vertical"))
                                      text.vertical = value.toBool();
                                  else if (key == QLatin1String("verticalRtl"))
                                      text.verticalRtl = value.toBool();
                                  else
                                      return false;
                                  return true;
                              });
        return;
    }

    if (kind == ContentKind::Vector)
    {
        if (key == QLatin1String("cornerRadius")
            || key == QLatin1String("width") || key == QLatin1String("height")
            || key == QLatin1String("strokeWidth"))
        {
            sessionSetTrackValue(key, value.toDouble());
            return;
        }

        runSessionContentEdit(QStringLiteral("pvf: edit vector content"),
                              [key, value](model::Content& content) {
                                  if (content.kind != ContentKind::Vector)
                                      return false;
                                  model::VectorContent& vector = content.vector;
                                  if (key == QLatin1String("shape"))
                                      vector.shape = value.toInt();
                                  else if (key == QLatin1String("sides"))
                                      vector.sides = value.toInt();
                                  else if (key == QLatin1String("innerRatio"))
                                      vector.innerRatio = value.toDouble();
                                  else if (key == QLatin1String("fill"))
                                      vector.fillColor = colorString(
                                          value.value<QColor>());
                                  else if (key == QLatin1String("stroke"))
                                      vector.strokeColor = colorString(
                                          value.value<QColor>());
                                  else
                                      return false;
                                  return true;
                              });
        return;
    }

    if (kind == ContentKind::Asset)
    {
        if (key == QLatin1String("sourceIn"))
            applySessionAsset(QStringLiteral("sourceIn"), value);
        else if (key == QLatin1String("volume"))
            applySessionAsset(QStringLiteral("volume"), value);
    }
}

void PropertiesPanel::applySessionParent(const Uuid& parent)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable() || entity->parentUuid == parent)
        return;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->reparent(
        m_sessionUuid, parent, false, &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::applySessionAsset(const QString& field, const QVariant& value)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable()
        || entity->content.kind != ContentKind::Asset)
        return;

    int maxSourceIn = 1000000;
    const auto stored =
        m_session->assets().getIncludingMissing(entity->content.asset.hash);
    if (stored)
    {
        const int frames =
            stored->metadata.value(QStringLiteral("durationFrames")).toInt(0);
        if (frames > 0)
            maxSourceIn = std::max(0, frames - 1);
    }

    runSessionContentEdit(QStringLiteral("pvf: edit asset content"),
                          [field, value, maxSourceIn](model::Content& content) {
                              if (content.kind != ContentKind::Asset)
                                  return false;
                              if (field == QLatin1String("sourceIn"))
                                  content.asset.sourceIn = std::clamp(
                                      value.toInt(), 0, maxSourceIn);
                              else if (field == QLatin1String("volume"))
                                  content.asset.volume = std::clamp(
                                      value.toDouble(), 0.0, 2.0);
                              else
                                  return false;
                              return true;
                          });
}

} // namespace pvf
