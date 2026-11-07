#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "ui/EasingChooser.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

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
    case EntityType::Camera:
        return QStringLiteral("摄像机");
    case EntityType::Audio:
        return QStringLiteral("音频");
    case EntityType::Folder:
        return QStringLiteral("文件夹");
    }
    return QString();
}

} // namespace

PropertiesPanel::PropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    m_title->setContentsMargins(10, 8, 10, 8);
    QFont titleFont = m_title->font();
    titleFont.setItalic(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    auto* transformContent = new QWidget(this);
    auto* transformForm = new QFormLayout(transformContent);
    transformForm->setContentsMargins(10, 8, 10, 8);
    transformForm->setVerticalSpacing(6);
    m_parentRow = new QWidget(transformContent);
    auto* parentLayout = new QHBoxLayout(m_parentRow);
    parentLayout->setContentsMargins(0, 0, 0, 0);
    m_parent = new QComboBox(m_parentRow);
    m_parent->setObjectName(QStringLiteral("parentCombo"));
    m_parent->setToolTip(
        QStringLiteral("选择实体的坐标系：工作台或另一个实体；子级的位置/旋转/缩放相对父级"));
    parentLayout->addWidget(m_parent);
    transformForm->addRow(QStringLiteral("父级"), m_parentRow);
    m_parentLabel = qobject_cast<QLabel*>(transformForm->labelForField(m_parentRow));
    connect(m_parent, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_entity == kInvalidEntity || m_parent->currentIndex() < 0)
                    return;
                emit parentEdited(
                    m_entity, EntityId(m_parent->currentData().toInt()));
            });

    addRow(transformForm, QStringLiteral("X"), QStringLiteral("x"), -10000, 10000, 1, 1);
    addRow(transformForm, QStringLiteral("Y"), QStringLiteral("y"), -10000, 10000, 1, 1);
    addRow(transformForm, QStringLiteral("旋转"), QStringLiteral("rotation"), -3600, 3600, 1, 1);
    addRow(transformForm, QStringLiteral("缩放 X"), QStringLiteral("scaleX"), -100, 100, 3, 0.01);
    addRow(transformForm, QStringLiteral("缩放 Y"), QStringLiteral("scaleY"), -100, 100, 3, 0.01);
    addRow(transformForm, QStringLiteral("透明度"), QStringLiteral("opacity"), 0, 100, 0, 1,
           0.01);
    m_transformSection =
        new CollapsibleSection(QStringLiteral("变换"), transformContent, this);
    m_transformSection->setExpanded(true);
    layout->addWidget(m_transformSection);

    auto* keyContent = new QWidget(this);
    auto* keyLayout = new QVBoxLayout(keyContent);
    keyLayout->setContentsMargins(10, 8, 10, 8);
    m_keyButton = new QPushButton(QStringLiteral("在当前帧打关键帧"), keyContent);
    m_keyButton->setEnabled(false);
    keyLayout->addWidget(m_keyButton);

    m_easeButton = new QPushButton(QStringLiteral("缓动：线性"), keyContent);
    m_easeButton->setObjectName(QStringLiteral("keyframeEasingButton"));
    m_easeButton->setEnabled(false);
    m_easeButton->setToolTip(
        QStringLiteral("修改当前帧关键帧的曲线；右键时间轴关键帧也可以设置"));
    keyLayout->addWidget(m_easeButton);

    m_keySection = new CollapsibleSection(QStringLiteral("关键帧"), keyContent, this);
    m_keySection->setExpanded(true);
    layout->addWidget(m_keySection);

    buildTextSection(layout);
    buildVectorSection(layout);
    buildMediaSection(layout);
    buildAudioSection(layout);
    buildPivotSection(layout);
    layout->addStretch(1);

    connect(m_keyButton, &QPushButton::clicked, this, [this] {
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        const bool camera = entity && entity->isCamera();
        for (const Row& r : m_rows)
        {
            if (camera && r.prop == QStringLiteral("opacity"))
                continue;
            m_project->setValue(m_entity, r.prop, m_frame,
                                m_project->value(m_entity, r.prop, m_frame), true);
        }
        refresh();
    });

    connect(m_easeButton, &QPushButton::clicked, this, [this] {
        if (m_entity == kInvalidEntity)
            return;
        const Entity* entity = m_project->entity(m_entity);
        if (!entity || entity->locked)
            return;

        EasingSpec current;
        bool found = false;
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - m_frame) < 1e-6)
                {
                    current = key.ease;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            return;

        const double previous = m_project->previousKeyframeFrame(m_entity, m_frame);
        if (previous < 0.0)
            return;
        const int fps = std::max(1, m_project->fps);
        const QString title = QStringLiteral("进入本帧：%1s → %2s")
                                  .arg(previous / double(fps), 0, 'f', 2)
                                  .arg(m_frame / double(fps), 0, 'f', 2);

        EasingSpec chosen;
        const QPoint anchor =
            m_easeButton->mapToGlobal(QPoint(0, m_easeButton->height()));
        if (EasingChooser::choose(this, current, anchor, &chosen, title))
            emit keyframeEasingEdited(m_entity, m_frame, chosen);
    });

    refresh();
}

void PropertiesPanel::addRow(QFormLayout* form, const QString& label, const QString& prop,
                             double lo, double hi, int decimals, double step, double factor)
{
    auto* spin = new QDoubleSpinBox(form->parentWidget());
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    auto* labelWidget = new QLabel(label, form->parentWidget());
    form->addRow(labelWidget, spin);

    Row row;
    row.prop = prop;
    row.baseLabel = label;
    row.label = labelWidget;
    row.spin = spin;
    row.factor = factor;
    m_rows.append(row);

    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop](double v) {
        if (m_entity == kInvalidEntity)
            return;
        double rowFactor = 1.0;
        for (const Row& row : m_rows)
        {
            if (row.prop == prop)
            {
                rowFactor = row.factor;
                break;
            }
        }
        emit valueEdited(m_entity, prop, v * rowFactor);
    });
}

void PropertiesPanel::buildTextSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_textContent = new QLineEdit(content);
    connect(m_textContent, &QLineEdit::textEdited, this,
            [this](const QString& text) { applyData(QStringLiteral("text"), text); });
    form->addRow(QStringLiteral("内容"), m_textContent);

    m_textFont = new QFontComboBox(content);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        applyData(QStringLiteral("font"), font.family());
    });
    form->addRow(QStringLiteral("字体"), m_textFont);

    m_textSize = new QSpinBox(content);
    m_textSize->setRange(8, 512);
    m_textSize->setValue(96);
    m_textSize->setSuffix(QStringLiteral(" px"));
    connect(m_textSize, &QSpinBox::valueChanged, this,
            [this](int size) { applyData(QStringLiteral("size"), size); });
    form->addRow(QStringLiteral("字号"), m_textSize);

    m_textColorButton = new QPushButton(content);
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

    m_textAlign = new QComboBox(content);
    m_textAlign->addItem(QStringLiteral("居中"), int(Qt::AlignCenter));
    m_textAlign->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft | Qt::AlignVCenter));
    m_textAlign->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight | Qt::AlignVCenter));
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        applyData(QStringLiteral("align"), m_textAlign->currentData());
    });
    form->addRow(QStringLiteral("对齐"), m_textAlign);

    m_textSection = new CollapsibleSection(QStringLiteral("文字"), content, this);
    m_textSection->setExpanded(true);
    layout->addWidget(m_textSection);
}

void PropertiesPanel::buildVectorSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_vectorShape = new QComboBox(content);
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
    m_vectorCorner->setRange(0, 2000);
    m_vectorCorner->setDecimals(1);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    connect(m_vectorCorner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("cornerRadius"), value); });
    form->addRow(QStringLiteral("圆角"), m_vectorCorner);

    m_vectorSides = new QSpinBox(content);
    m_vectorSides->setRange(3, 64);
    connect(m_vectorSides, &QSpinBox::valueChanged, this,
            [this](int value) { applyData(QStringLiteral("sides"), value); });
    form->addRow(QStringLiteral("边数"), m_vectorSides);

    m_vectorInner = new QDoubleSpinBox(content);
    m_vectorInner->setRange(0.05, 1.0);
    m_vectorInner->setSingleStep(0.05);
    m_vectorInner->setDecimals(2);
    connect(m_vectorInner, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("innerRatio"), value); });
    form->addRow(QStringLiteral("内径比"), m_vectorInner);

    m_vectorWidth = new QDoubleSpinBox(content);
    m_vectorWidth->setRange(1, 20000);
    m_vectorWidth->setDecimals(1);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    connect(m_vectorWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("width"), value); });
    form->addRow(QStringLiteral("宽度"), m_vectorWidth);

    m_vectorHeight = new QDoubleSpinBox(content);
    m_vectorHeight->setRange(1, 20000);
    m_vectorHeight->setDecimals(1);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    connect(m_vectorHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("height"), value); });
    form->addRow(QStringLiteral("高度"), m_vectorHeight);

    m_fillButton = new QPushButton(content);
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
    m_mediaInfo = new QLabel(content);
    m_mediaInfo->setWordWrap(true);
    m_mediaInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_mediaInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_mediaInfo->setMinimumWidth(0);
    mediaLayout->addWidget(m_mediaInfo);
    m_mediaSection = new CollapsibleSection(QStringLiteral("素材"), content, this);
    m_mediaSection->setExpanded(true);
    layout->addWidget(m_mediaSection);
}

void PropertiesPanel::buildAudioSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    auto addCaptureRow = [this, content, form](const QString& label, QWidget* field,
                                               const QString& objectName,
                                               const QString& tooltip,
                                               std::function<void()> capture) {
        auto* row = new QWidget(content);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        field->setParent(row);
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        rowLayout->addWidget(field, 1);
        auto* button = new QPushButton(row);
        button->setObjectName(objectName);
        button->setText(QStringLiteral("取"));
        button->setToolTip(tooltip);
        button->setFixedWidth(30);
        connect(button, &QPushButton::clicked, this, capture);
        rowLayout->addWidget(button);
        form->addRow(label, row);
    };

    m_audioStart = new QSpinBox(content);
    m_audioStart->setObjectName(QStringLiteral("audioStartFrame"));
    m_audioStart->setRange(0, 1000000);
    m_audioStart->setSuffix(QStringLiteral(" 帧"));
    m_audioStart->setKeyboardTracking(false);
    m_audioStart->setToolTip(QStringLiteral("音频在时间轴上的起始帧；也可以拖动时间轴上的音频块"));
    addCaptureRow(QStringLiteral("时间轴开始"), m_audioStart,
                  QStringLiteral("audioStartFromPlayhead"),
                  QStringLiteral("把音频在时间轴上的开始位置设为播放头位置"),
                  [this] { applyData(QStringLiteral("startFrame"), double(m_frame)); });

    m_audioTrimStart = new QDoubleSpinBox(content);
    m_audioTrimStart->setObjectName(QStringLiteral("audioTrimStart"));
    m_audioTrimStart->setRange(0.0, 36000.0);
    m_audioTrimStart->setDecimals(3);
    m_audioTrimStart->setSuffix(QStringLiteral(" s"));
    m_audioTrimStart->setKeyboardTracking(false);
    m_audioTrimStart->setToolTip(
        QStringLiteral("裁掉音频开头的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(
        QStringLiteral("裁掉开头"), m_audioTrimStart,
        QStringLiteral("audioTrimStartFromPlayhead"),
        QStringLiteral("从播放头对应的音频位置开始，裁掉前面部分；后面的波形保持原位"),
        [this] { applyAudioTrimStart(m_frame); });

    m_audioTrimEnd = new QDoubleSpinBox(content);
    m_audioTrimEnd->setObjectName(QStringLiteral("audioTrimEnd"));
    m_audioTrimEnd->setRange(0.0, 36000.0);
    m_audioTrimEnd->setDecimals(3);
    m_audioTrimEnd->setSuffix(QStringLiteral(" s"));
    m_audioTrimEnd->setKeyboardTracking(false);
    m_audioTrimEnd->setToolTip(
        QStringLiteral("裁掉音频结尾的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(
        QStringLiteral("裁掉结尾"), m_audioTrimEnd,
        QStringLiteral("audioTrimEndFromPlayhead"),
        QStringLiteral("在播放头对应的音频位置结束，裁掉后面部分；前面的波形保持原位"),
        [this] { applyAudioTrimEnd(m_frame); });

    m_audioVolume = new QSpinBox(content);
    m_audioVolume->setObjectName(QStringLiteral("audioVolume"));
    m_audioVolume->setRange(0, 200);
    m_audioVolume->setSuffix(QStringLiteral(" %"));
    m_audioVolume->setKeyboardTracking(false);
    form->addRow(QStringLiteral("音量"), m_audioVolume);

    auto* hint =
        new QLabel(QStringLiteral("直接拖动时间轴上的音频块可以整体移动；「时间轴开始」决定位置，"
                                  "「裁掉开头/结尾」会缩短音频块，但保留的波形保持原位。"
                                  "「取」把播放头位置填入字段。"),
                   content);
    hint->setWordWrap(true);
    form->addRow(hint);

    m_audioSection = new CollapsibleSection(QStringLiteral("音频"), content, this);
    m_audioSection->setExpanded(true);
    layout->addWidget(m_audioSection);

    connect(m_audioStart, &QSpinBox::valueChanged, this,
            [this](int value) { applyData(QStringLiteral("startFrame"), double(value)); });
    connect(m_audioTrimStart, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("trimStart"), value); });
    connect(m_audioTrimEnd, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("trimEnd"), value); });
    connect(m_audioVolume, &QSpinBox::valueChanged, this,
            [this](int value) { applyData(QStringLiteral("volume"), value / 100.0); });
}

void PropertiesPanel::buildPivotSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_usePivot = new QCheckBox(QStringLiteral("启用自定义轴心"), content);
    form->addRow(m_usePivot);

    m_pivotX = new QDoubleSpinBox(content);
    m_pivotX->setRange(-10000, 10000);
    m_pivotX->setDecimals(1);
    m_pivotX->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 X"), m_pivotX);

    m_pivotY = new QDoubleSpinBox(content);
    m_pivotY->setRange(-10000, 10000);
    m_pivotY->setDecimals(1);
    m_pivotY->setKeyboardTracking(false);
    form->addRow(QStringLiteral("轴心 Y"), m_pivotY);

    auto* hint = new QLabel(QStringLiteral("轴心相对实体中心，旋转围绕轴心。"), content);
    hint->setWordWrap(true);
    form->addRow(hint);

    connect(m_usePivot, &QCheckBox::toggled, this, [this](bool on) {
        Entity* e = m_project->entity(m_entity);
        if (!e)
            return;
        e->data[QStringLiteral("rotPointCustom")] = on;
        m_project->notifyChanged();
        refresh();
    });
    connect(m_pivotX, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_entity != kInvalidEntity)
            emit pivotEdited(m_entity, value,
                             m_project->value(m_entity, QStringLiteral("rotPointY"), m_frame));
    });
    connect(m_pivotY, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_entity != kInvalidEntity)
            emit pivotEdited(m_entity,
                             m_project->value(m_entity, QStringLiteral("rotPointX"), m_frame),
                             value);
    });

    m_pivotSection = new CollapsibleSection(QStringLiteral("轴心"), content, this);
    m_pivotSection->setExpanded(false);
    layout->addWidget(m_pivotSection);
}

void PropertiesPanel::applyData(const QString& key, const QVariant& value)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    e->data[key] = value;
    m_project->modified = true;
    m_project->notifyChanged();
}

void PropertiesPanel::applyAudioTrimStart(int playheadFrame)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    const double fps = std::max(1, m_project->fps);
    const double startFrame =
        e->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    const double duration =
        e->data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double trimStart =
        e->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd =
        e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double audibleFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (audibleFrames <= 1e-9)
        return;

    // Trimming the left edge moves the clip start to the playhead while the
    // rest of the waveform stays exactly where it was. Leave one frame.
    const double deltaFrames =
        std::clamp(playheadFrame - startFrame, 0.0,
                   std::max(0.0, audibleFrames - 1.0));
    if (deltaFrames <= 1e-9)
        return;

    e->data[QStringLiteral("startFrame")] = startFrame + deltaFrames;
    e->data[QStringLiteral("trimStart")] = trimStart + deltaFrames / fps;
    m_project->modified = true;
    m_project->notifyChanged();
}

void PropertiesPanel::applyAudioTrimEnd(int playheadFrame)
{
    Entity* e = m_project->entity(m_entity);
    if (!e)
        return;
    const double fps = std::max(1, m_project->fps);
    const double startFrame =
        e->data.value(QStringLiteral("startFrame"), 0.0).toDouble();
    const double duration =
        e->data.value(QStringLiteral("duration"), 0.0).toDouble();
    const double trimStart =
        e->data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double trimEnd =
        e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble();
    const double audibleFrames =
        std::max(0.0, (duration - trimStart - trimEnd) * fps);
    if (audibleFrames <= 1.0)
        return;

    // The playhead is relative to the clip start; trimEnd counts from the end
    // of the source, so trimStart has to be subtracted as well.
    const double deltaFrames =
        std::clamp(playheadFrame - startFrame, 1.0, audibleFrames);
    const double value =
        std::max(0.0, duration - trimStart - deltaFrames / fps);
    if (std::abs(value - trimEnd) < 1e-9)
        return;

    e->data[QStringLiteral("trimEnd")] = value;
    m_project->modified = true;
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
    const bool audio = valid && e->isAudio();

    m_transformSection->setVisible(valid && !audio);
    m_keySection->setVisible(valid && !audio);
    m_textSection->setVisible(valid && e->type == EntityType::Text);
    m_vectorSection->setVisible(valid && e->type == EntityType::Vector);
    m_mediaSection->setVisible(valid
                               && (e->type == EntityType::Image
                                   || e->type == EntityType::Video
                                   || e->type == EntityType::Audio));
    m_audioSection->setVisible(audio);
    m_pivotSection->setVisible(valid && !e->isCamera() && !audio);

    const bool editable = valid && !m_project->isEffectivelyLocked(e->id);
    const bool camera = valid && e->isCamera();
    m_pivotSection->setEnabled(editable);
    m_transformSection->setEnabled(editable);
    m_keySection->setEnabled(editable);
    m_textSection->setEnabled(editable);
    m_vectorSection->setEnabled(editable);
    m_audioSection->setEnabled(editable);
    m_keyButton->setEnabled(editable);

    {
        const QSignalBlocker blocker(m_parent);
        m_parent->clear();
        m_parent->addItem(QStringLiteral("工作台（世界坐标）"),
                          int(kInvalidEntity));
        if (valid && !audio)
        {
            for (int index : m_project->entityOrder())
            {
                const Entity& candidate = m_project->entities()[index];
                if (candidate.id == e->id || candidate.isAudio())
                    continue;
                if (e->isCamera() && candidate.isCamera())
                    continue;
                if (m_project->isDescendantOf(candidate.id, e->id))
                    continue;

                int depth = 0;
                QSet<EntityId> seen;
                EntityId parentId = candidate.parent;
                while (parentId != kInvalidEntity && !seen.contains(parentId)
                       && depth < 32)
                {
                    seen.insert(parentId);
                    const Entity* parent = m_project->entity(parentId);
                    if (!parent)
                        break;
                    ++depth;
                    parentId = parent->parent;
                }
                const QString prefix = QStringLiteral("  ").repeated(depth);
                const QString typePrefix =
                    candidate.isFolder() ? QStringLiteral("[文件夹] ")
                    : candidate.isCamera() ? QStringLiteral("[摄像机] ")
                                           : QString();
                m_parent->addItem(prefix + typePrefix + candidate.name,
                                  candidate.id);
            }
            const int parentIndex = m_parent->findData(e->parent);
            m_parent->setCurrentIndex(parentIndex >= 0 ? parentIndex : 0);
        }
        m_parentRow->setVisible(valid && !audio);
        if (m_parentLabel)
            m_parentLabel->setVisible(valid && !audio);
        m_parentRow->setEnabled(editable && !audio);
    }

    for (Row& r : m_rows)
    {
        const bool hiddenRow = camera
                               && (r.prop == QStringLiteral("scaleY")
                                   || r.prop == QStringLiteral("opacity"));
        r.spin->setVisible(!hiddenRow);
        r.label->setVisible(!hiddenRow);

        const QSignalBlocker blocker(r.spin);
        if (camera && r.prop == QStringLiteral("scaleX"))
        {
            r.label->setText(QStringLiteral("取景比例"));
            r.spin->setRange(5.0, 100.0);
            r.spin->setDecimals(1);
            r.spin->setSingleStep(1.0);
            r.spin->setSuffix(QStringLiteral("%"));
            r.factor = 0.01;
        }
        else if (r.prop == QStringLiteral("scaleX"))
        {
            r.label->setText(r.baseLabel);
            r.spin->setRange(-100.0, 100.0);
            r.spin->setDecimals(3);
            r.spin->setSingleStep(0.01);
            r.spin->setSuffix(QString());
            r.factor = 1.0;
        }
        r.spin->setEnabled(editable);
    }

    EasingSpec currentEase;
    bool hasKeyframe = false;
    if (valid)
    {
        for (auto it = e->tracks.constBegin(); it != e->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - m_frame) < 1e-6)
                {
                    currentEase = key.ease;
                    hasKeyframe = true;
                    break;
                }
            }
            if (hasKeyframe)
                break;
        }
    }
    m_easeButton->setEnabled(editable && hasKeyframe);
    m_easeButton->setText(hasKeyframe
                              ? QStringLiteral("缓动：")
                                    + easingDisplayName(currentEase.easing)
                              : QStringLiteral("缓动（当前帧无关键帧）"));
    if (hasKeyframe)
    {
        const double previous = m_project->previousKeyframeFrame(m_entity, m_frame);
        if (previous >= 0.0)
        {
            const int fps = std::max(1, m_project->fps);
            m_easeButton->setEnabled(editable);
            m_easeButton->setText(
                QStringLiteral("缓动 %1s → %2s：%3")
                    .arg(previous / double(fps), 0, 'f', 2)
                    .arg(m_frame / double(fps), 0, 'f', 2)
                    .arg(easingDisplayName(currentEase.easing)));
        }
        else
        {
            m_easeButton->setEnabled(false);
            m_easeButton->setText(QStringLiteral("缓动（首帧无进入段）"));
        }
    }

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

    {
        const QSignalBlocker blocker(m_usePivot);
        m_usePivot->setChecked(e->rotPointCustom());
    }
    {
        const QSignalBlocker blocker(m_pivotX);
        m_pivotX->setValue(m_project->value(m_entity, QStringLiteral("rotPointX"), m_frame));
    }
    {
        const QSignalBlocker blocker(m_pivotY);
        m_pivotY->setValue(m_project->value(m_entity, QStringLiteral("rotPointY"), m_frame));
    }
    const bool pivotEnabled = e->rotPointCustom();
    m_pivotX->setEnabled(editable && pivotEnabled);
    m_pivotY->setEnabled(editable && pivotEnabled);

    if (e->type == EntityType::Audio)
    {
        const double duration = e->data.value(QStringLiteral("duration"), 0.0).toDouble();
        {
            const QSignalBlocker blocker(m_audioStart);
            m_audioStart->setValue(
                int(std::lround(e->data.value(QStringLiteral("startFrame"), 0.0).toDouble())));
        }
        {
            const QSignalBlocker blocker(m_audioTrimStart);
            const QSignalBlocker blockerEnd(m_audioTrimEnd);
            m_audioTrimStart->setRange(0.0, std::max(0.0, duration));
            m_audioTrimEnd->setRange(0.0, std::max(0.0, duration));
            m_audioTrimStart->setValue(
                e->data.value(QStringLiteral("trimStart"), 0.0).toDouble());
            m_audioTrimEnd->setValue(
                e->data.value(QStringLiteral("trimEnd"), 0.0).toDouble());
        }
        {
            const QSignalBlocker blocker(m_audioVolume);
            m_audioVolume->setValue(int(
                std::lround(e->data.value(QStringLiteral("volume"), 1.0).toDouble()
                            * 100.0)));
        }
    }

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
            const QSignalBlocker blocker(m_vectorCorner);
            m_vectorCorner->setValue(e->cornerRadius());
        }
        {
            const QSignalBlocker blocker(m_vectorSides);
            m_vectorSides->setValue(e->vectorSides());
        }
        {
            const QSignalBlocker blocker(m_vectorInner);
            m_vectorInner->setValue(e->vectorInnerRatio());
        }
        const int shape = e->vectorShape();
        m_vectorCorner->setEnabled(editable && shape == 3);
        m_vectorSides->setEnabled(editable && (shape == 6 || shape == 7));
        m_vectorInner->setEnabled(editable && shape == 6);
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
        int mediaWidth = e->image.width();
        int mediaHeight = e->image.height();
        if (e->type == EntityType::Video)
        {
            mediaWidth =
                e->data.value(QStringLiteral("sourceWidth"), mediaWidth).toInt();
            mediaHeight =
                e->data.value(QStringLiteral("sourceHeight"), mediaHeight).toInt();
        }
        if (mediaWidth > 0 && mediaHeight > 0)
            info += QStringLiteral(" · %1×%2").arg(mediaWidth).arg(mediaHeight);
        if (e->type == EntityType::Audio)
        {
            const double duration =
                e->data.value(QStringLiteral("duration"), 0.0).toDouble();
            info += QStringLiteral(" · %1 秒").arg(duration, 0, 'f', 2);
            if (e->audio)
                info += QStringLiteral(" · 48 kHz stereo");
            else
                info += QStringLiteral(" · 解码失败或未解码");
        }
        if (!e->sourcePath.isEmpty())
        {
            QString shown = e->sourcePath;
            constexpr int maxChars = 54;
            if (shown.size() > maxChars)
                shown = shown.left(8) + QStringLiteral("…") + shown.right(maxChars - 9);
            info += QStringLiteral("\n") + shown;
            m_mediaInfo->setToolTip(e->sourcePath);
        }
        else
        {
            m_mediaInfo->setToolTip(QString());
        }
        m_mediaInfo->setText(info);
    }
}

} // namespace pvf
