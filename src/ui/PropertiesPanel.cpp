#include "PropertiesPanel.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace pvf {

namespace {

QString typeName(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QStringLiteral("图片");
    case EntityType::Video:
        return QStringLiteral("视频");
    case EntityType::Text:
        return QStringLiteral("文字");
    case EntityType::Vector:
        return QStringLiteral("矢量");
    }
    return QString();
}

} // namespace

PropertiesPanel::PropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QLabel(QStringLiteral("属性"), this);
    QFont headerFont = header->font();
    headerFont.setBold(true);
    header->setFont(headerFont);
    layout->addWidget(header);

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    QFont titleFont = m_title->font();
    titleFont.setItalic(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_keyButton = new QPushButton(QStringLiteral("在当前帧打关键帧"), this);
    m_keyButton->setEnabled(false);
    layout->addWidget(m_keyButton);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    layout->addLayout(form);

    addRow(form, QStringLiteral("X"), QStringLiteral("x"), -10000, 10000, 1, 1);
    addRow(form, QStringLiteral("Y"), QStringLiteral("y"), -10000, 10000, 1, 1);
    addRow(form, QStringLiteral("旋转"), QStringLiteral("rotation"), -3600, 3600, 1, 1);
    addRow(form, QStringLiteral("缩放 X"), QStringLiteral("scaleX"), -100, 100, 3, 0.01);
    addRow(form, QStringLiteral("缩放 Y"), QStringLiteral("scaleY"), -100, 100, 3, 0.01);
    addRow(form, QStringLiteral("透明度"), QStringLiteral("opacity"), 0, 100, 0, 1, 0.01);

    buildTextGroup(layout);
    buildVectorGroup(layout);
    buildMediaGroup(layout);

    layout->addStretch(1);

    connect(m_keyButton, &QPushButton::clicked, this, [this] {
        if (m_entity == kInvalidEntity)
            return;
        for (const Row& r : m_rows)
            m_project->setValue(m_entity, r.prop, m_frame,
                                m_project->value(m_entity, r.prop, m_frame), true);
        refresh();
    });

    refresh();
}

void PropertiesPanel::addRow(QFormLayout* form, const QString& label, const QString& prop,
                             double lo, double hi, int decimals, double step, double factor)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    form->addRow(label, spin);

    Row row;
    row.prop = prop;
    row.spin = spin;
    row.factor = factor;
    m_rows.append(row);

    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop, factor](double v) {
        if (m_entity != kInvalidEntity)
            emit valueEdited(m_entity, prop, v * factor);
    });
}

void PropertiesPanel::buildTextGroup(QVBoxLayout* layout)
{
    m_textGroup = new QGroupBox(QStringLiteral("文字"), this);
    auto* form = new QFormLayout(m_textGroup);
    form->setVerticalSpacing(6);

    m_textContent = new QLineEdit(m_textGroup);
    connect(m_textContent, &QLineEdit::textEdited, this,
            [this](const QString& text) { applyData(QStringLiteral("text"), text); });
    form->addRow(QStringLiteral("内容"), m_textContent);

    m_textFont = new QFontComboBox(m_textGroup);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) {
                applyData(QStringLiteral("font"), font.family());
            });
    form->addRow(QStringLiteral("字体"), m_textFont);

    m_textSize = new QSpinBox(m_textGroup);
    m_textSize->setRange(8, 512);
    m_textSize->setValue(96);
    m_textSize->setSuffix(QStringLiteral(" px"));
    connect(m_textSize, &QSpinBox::valueChanged, this,
            [this](int size) { applyData(QStringLiteral("size"), size); });
    form->addRow(QStringLiteral("字号"), m_textSize);

    m_textColorButton = new QPushButton(m_textGroup);
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

    m_textAlign = new QComboBox(m_textGroup);
    m_textAlign->addItem(QStringLiteral("居中"), int(Qt::AlignCenter));
    m_textAlign->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_textAlign->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight | Qt::AlignVCenter));
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("align"), m_textAlign->currentData());
    });
    form->addRow(QStringLiteral("对齐"), m_textAlign);

    layout->addWidget(m_textGroup);
}

void PropertiesPanel::buildVectorGroup(QVBoxLayout* layout)
{
    m_vectorGroup = new QGroupBox(QStringLiteral("矢量"), this);
    auto* form = new QFormLayout(m_vectorGroup);
    form->setVerticalSpacing(6);

    m_vectorShape = new QComboBox(m_vectorGroup);
    m_vectorShape->addItem(QStringLiteral("矩形"), 0);
    m_vectorShape->addItem(QStringLiteral("圆形"), 1);
    m_vectorShape->addItem(QStringLiteral("直线"), 2);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("shape"), m_vectorShape->currentData());
    });
    form->addRow(QStringLiteral("形状"), m_vectorShape);

    m_vectorWidth = new QDoubleSpinBox(m_vectorGroup);
    m_vectorWidth->setRange(1, 20000);
    m_vectorWidth->setDecimals(1);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    connect(m_vectorWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("width"), value); });
    form->addRow(QStringLiteral("宽度"), m_vectorWidth);

    m_vectorHeight = new QDoubleSpinBox(m_vectorGroup);
    m_vectorHeight->setRange(1, 20000);
    m_vectorHeight->setDecimals(1);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    connect(m_vectorHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("height"), value); });
    form->addRow(QStringLiteral("高度"), m_vectorHeight);

    m_fillButton = new QPushButton(m_vectorGroup);
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

    m_strokeButton = new QPushButton(m_vectorGroup);
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

    m_strokeWidth = new QDoubleSpinBox(m_vectorGroup);
    m_strokeWidth->setRange(0, 200);
    m_strokeWidth->setDecimals(1);
    m_strokeWidth->setSuffix(QStringLiteral(" px"));
    connect(m_strokeWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("strokeWidth"), value); });
    form->addRow(QStringLiteral("描边宽度"), m_strokeWidth);

    layout->addWidget(m_vectorGroup);
}

void PropertiesPanel::buildMediaGroup(QVBoxLayout* layout)
{
    m_mediaGroup = new QGroupBox(QStringLiteral("素材"), this);
    auto* mediaLayout = new QVBoxLayout(m_mediaGroup);
    mediaLayout->setContentsMargins(8, 8, 8, 8);
    m_mediaInfo = new QLabel(m_mediaGroup);
    m_mediaInfo->setWordWrap(true);
    m_mediaInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaLayout->addWidget(m_mediaInfo);
    layout->addWidget(m_mediaGroup);
}

void PropertiesPanel::applyData(const QString& key, const QVariant& value)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    e->data[key] = value;
    m_project->notifyChanged();
}

void PropertiesPanel::setColorButton(QPushButton* button, const QColor& color)
{
    button->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    button->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

QColor PropertiesPanel::pickColor(const QColor& initial, const QString& title)
{
    return QColorDialog::getColor(initial, this, title, QColorDialog::ShowAlphaChannel);
}

void PropertiesPanel::setEntity(EntityId id)
{
    m_entity = id;
    refresh();
}

void PropertiesPanel::setFrame(int frame)
{
    m_frame = frame;
    refresh();
}

void PropertiesPanel::refresh()
{
    const Entity* e = m_project->entity(m_entity);
    const bool valid = e != nullptr;

    m_keyButton->setEnabled(valid);
    for (Row& r : m_rows)
        r.spin->setEnabled(valid);

    m_textGroup->setVisible(valid && e->type == EntityType::Text);
    m_vectorGroup->setVisible(valid && e->type == EntityType::Vector);
    m_mediaGroup->setVisible(valid
                             && (e->type == EntityType::Image
                                 || e->type == EntityType::Video));

    if (!valid)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        for (Row& r : m_rows)
        {
            const QSignalBlocker blocker(r.spin);
            r.spin->setValue(0);
        }
        return;
    }

    m_title->setText(e->name + QStringLiteral(" · ") + typeName(e->type));

    for (Row& r : m_rows)
    {
        const QSignalBlocker blocker(r.spin);
        r.spin->setValue(m_project->value(m_entity, r.prop, m_frame) / r.factor);
    }

    if (e->type == EntityType::Text)
    {
        if (!m_textContent->hasFocus())
        {
            const QSignalBlocker blocker(m_textContent);
            m_textContent->setText(e->textContent());
        }
        if (!m_textFont->hasFocus())
        {
            const QSignalBlocker blocker(m_textFont);
            m_textFont->setCurrentFont(QFont(e->fontFamily()));
        }
        {
            const QSignalBlocker blocker(m_textSize);
            m_textSize->setValue(e->fontSize());
        }
        m_textColorValue = e->textColor();
        setColorButton(m_textColorButton, m_textColorValue);
        {
            const QSignalBlocker blocker(m_textAlign);
            int index = 0;
            for (int i = 0; i < m_textAlign->count(); ++i)
            {
                if (m_textAlign->itemData(i).toInt() == e->textAlign())
                {
                    index = i;
                    break;
                }
            }
            m_textAlign->setCurrentIndex(index);
        }
    }
    else if (e->type == EntityType::Vector)
    {
        {
            const QSignalBlocker blocker(m_vectorShape);
            for (int i = 0; i < m_vectorShape->count(); ++i)
            {
                if (m_vectorShape->itemData(i).toInt() == e->vectorShape())
                {
                    m_vectorShape->setCurrentIndex(i);
                    break;
                }
            }
        }
        {
            const QSignalBlocker blocker(m_vectorWidth);
            m_vectorWidth->setValue(e->vectorWidth());
        }
        {
            const QSignalBlocker blocker(m_vectorHeight);
            m_vectorHeight->setValue(e->vectorHeight());
        }
        m_fillValue = e->fillColor();
        setColorButton(m_fillButton, m_fillValue);
        m_strokeValue = e->strokeColor();
        setColorButton(m_strokeButton, m_strokeValue);
        {
            const QSignalBlocker blocker(m_strokeWidth);
            m_strokeWidth->setValue(e->strokeWidth());
        }
    }
    else
    {
        QString info = typeName(e->type);
        if (!e->image.isNull())
            info += QStringLiteral(" · %1×%2").arg(e->image.width()).arg(e->image.height());
        if (!e->sourcePath.isEmpty())
            info += QStringLiteral("\n") + e->sourcePath;
        m_mediaInfo->setText(info);
    }
}

} // namespace pvf
