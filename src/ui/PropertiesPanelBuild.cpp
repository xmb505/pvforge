#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "model/Content.h"
#include "model/Easing.h"
#include "model/Entity.h"

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
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace pvf {

using namespace ui::detail;

void PropertiesPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_rootLayout = layout;

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
                if (m_sessionMode)
                {
                    if (m_sessionUuid.isEmpty() || m_parent->currentIndex() < 0)
                        return;
                    applySessionParent(m_parent->currentData().toString());
                    return;
                }
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
    m_keyLayout = keyLayout;
    m_keyButton = new QPushButton(QStringLiteral("在当前帧打关键帧"), keyContent);
    m_keyButton->setObjectName(QStringLiteral("keyframeButton"));
    m_keyButton->setEnabled(false);
    keyLayout->addWidget(m_keyButton);

    m_easeButton = new QPushButton(QStringLiteral("缓动：线性"), keyContent);
    m_easeButton->setObjectName(QStringLiteral("keyframeEasingButton"));
    m_easeButton->setEnabled(false);
    m_easeButton->setToolTip(
        QStringLiteral("修改当前帧关键帧的进入段曲线"));
    keyLayout->addWidget(m_easeButton);

    m_keySection = new CollapsibleSection(QStringLiteral("关键帧"), keyContent, this);
    m_keySection->setObjectName(QStringLiteral("keySection"));
    m_keySection->setExpanded(true);
    layout->addWidget(m_keySection);

    buildTextSection(layout);
    buildComicSection(layout);
    buildLinesSection(layout);
    buildBrandWallSection(layout);
    buildHalftoneSection(layout);
    buildVectorSection(layout);
    buildMediaSection(layout);
    buildAudioSection(layout);
    buildPivotSection(layout);
    layout->addStretch(1);

    connect(m_keyButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
        {
            sessionAddTransformKeys();
            return;
        }
    });

    connect(m_easeButton, &QPushButton::clicked, this, [this] {
        if (m_sessionMode)
            return;
    });
}

void PropertiesPanel::buildSessionUi()
{
    // Conversion lives in the keyframe section: a normal key that is the last
    // key in the clip can become the solver start.  The effect is chosen from
    // the button's menu; the target stays in the solver section.
    m_convertSolverButton =
        new QPushButton(QStringLiteral("转换为求解关键帧…"), this);
    m_convertSolverButton->setObjectName(QStringLiteral("solverConvertButton"));
    m_convertSolverButton->setEnabled(false);
    m_keyLayout->addWidget(m_convertSolverButton);
    m_convertSolverHint = new QLabel(this);
    m_convertSolverHint->setObjectName(QStringLiteral("solverConvertHint"));
    m_convertSolverHint->setWordWrap(true);
    m_convertSolverHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_keyLayout->addWidget(m_convertSolverHint);
    connect(m_convertSolverButton, &QPushButton::clicked, this,
            &PropertiesPanel::showSolverConvertMenu);

    buildBasicSection(m_rootLayout);

    // Text layout fields that only the new model exposes.
    m_textLineHeight = new QDoubleSpinBox(this);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeight"));
    m_textLineHeight->setRange(0.1, 10.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.05);
    connect(m_textLineHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("lineHeight"), value); });
    m_textForm->addRow(QStringLiteral("行高"), m_textLineHeight);

    m_textLetterSpacing = new QDoubleSpinBox(this);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacing"));
    m_textLetterSpacing->setRange(-1000.0, 1000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    connect(m_textLetterSpacing, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applyData(QStringLiteral("letterSpacing"), value);
            });
    m_textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);

    m_textPadding = new QDoubleSpinBox(this);
    m_textPadding->setObjectName(QStringLiteral("textPadding"));
    m_textPadding->setRange(0.0, 100000.0);
    m_textPadding->setDecimals(2);
    m_textPadding->setSingleStep(1.0);
    m_textPadding->setSuffix(QStringLiteral(" px"));
    connect(m_textPadding, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("padding"), value); });
    m_textForm->addRow(QStringLiteral("内边距"), m_textPadding);

    m_textBoxWidth = new QDoubleSpinBox(this);
    m_textBoxWidth->setObjectName(QStringLiteral("textBoxWidth"));
    m_textBoxWidth->setRange(0.0, 100000.0);
    m_textBoxWidth->setDecimals(2);
    m_textBoxWidth->setSingleStep(1.0);
    m_textBoxWidth->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("boxWidth"), value); });
    m_textForm->addRow(QStringLiteral("框宽"), m_textBoxWidth);

    m_textBoxHeight = new QDoubleSpinBox(this);
    m_textBoxHeight->setObjectName(QStringLiteral("textBoxHeight"));
    m_textBoxHeight->setRange(0.0, 100000.0);
    m_textBoxHeight->setDecimals(2);
    m_textBoxHeight->setSingleStep(1.0);
    m_textBoxHeight->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { applyData(QStringLiteral("boxHeight"), value); });
    m_textForm->addRow(QStringLiteral("框高"), m_textBoxHeight);

    // Asset fields. The hash and type stay read-only in P4a; sourceIn and
    // volume are the editable asset values.
    auto* assetForm = new QFormLayout();
    assetForm->setContentsMargins(0, 0, 0, 0);
    assetForm->setVerticalSpacing(6);
    m_assetHash = new QLineEdit(this);
    m_assetHash->setObjectName(QStringLiteral("assetHash"));
    m_assetHash->setReadOnly(true);
    assetForm->addRow(QStringLiteral("素材 Hash"), m_assetHash);
    m_assetType = new QLabel(this);
    m_assetType->setObjectName(QStringLiteral("assetType"));
    assetForm->addRow(QStringLiteral("类型"), m_assetType);
    m_assetSourceIn = new QSpinBox(this);
    m_assetSourceIn->setObjectName(QStringLiteral("assetSourceIn"));
    m_assetSourceIn->setRange(0, 1000000);
    m_assetSourceIn->setSuffix(QStringLiteral(" 帧"));
    m_assetSourceIn->setKeyboardTracking(false);
    m_assetSourceIn->setToolTip(QStringLiteral("从素材的第几帧开始播放；范围受素材长度限制"));
    connect(m_assetSourceIn, &QSpinBox::valueChanged, this, [this](int value) {
        applySessionAsset(QStringLiteral("sourceIn"), value);
    });
    assetForm->addRow(QStringLiteral("素材起点"), m_assetSourceIn);
    m_assetVolume = new QDoubleSpinBox(this);
    m_assetVolume->setObjectName(QStringLiteral("assetVolume"));
    m_assetVolume->setRange(0.0, 2.0);
    m_assetVolume->setDecimals(3);
    m_assetVolume->setSingleStep(0.05);
    m_assetVolume->setSuffix(QStringLiteral(" ×"));
    connect(m_assetVolume, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applySessionAsset(QStringLiteral("volume"), value);
    });
    assetForm->addRow(QStringLiteral("音量"), m_assetVolume);
    m_mediaLayout->addLayout(assetForm);

    buildSessionHtmlSection(m_rootLayout);
    buildSpecialSection(m_rootLayout);
    buildCameraSection(m_rootLayout);
    buildSolverSection(m_rootLayout);

    // One easing control for the merged keyframe group at the current frame.
    // The legacy UI applied the choice to every track, not one track at a time.
    auto* easingRow = new QWidget(this);
    auto* easingLayout = new QHBoxLayout(easingRow);
    easingLayout->setContentsMargins(0, 0, 0, 0);
    easingLayout->setSpacing(6);
    auto* easingLabel = new QLabel(QStringLiteral("缓动"), easingRow);
    easingLayout->addWidget(easingLabel);
    m_groupEasing = new QComboBox(easingRow);
    m_groupEasing->setObjectName(QStringLiteral("keyframeEasingGroup"));
    m_groupEasing->setToolTip(
        QStringLiteral("修改当前帧所有轨道关键帧的进入段曲线；"
                       "自定义贝塞尔仍可选"));
    m_groupEasing->addItem(QStringLiteral("linear"), QStringLiteral("linear"));
    m_groupEasing->addItem(QStringLiteral("instant"), QStringLiteral("instant"));
    for (const QString& preset : model::easingPresetNames())
        m_groupEasing->addItem(preset, preset);
    m_groupEasing->addItem(QStringLiteral("bezier"), QStringLiteral("bezier"));
    easingLayout->addWidget(m_groupEasing, 1);
    m_keyLayout->addWidget(easingRow);
    connect(m_groupEasing, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_editing || !m_groupEasing)
            return;
        const QString type = m_groupEasing->currentData().toString();
        if (type.isEmpty())
            return;
        if (type == QLatin1String("bezier"))
        {
            sessionPromptGroupBezier();
            return;
        }
        model::EasingSpec spec;
        spec.type = model::canonicalEasingName(type);
        sessionSetGroupEasing(spec);
    });

    m_autoKeyCheck = new QCheckBox(QStringLiteral("自动关键帧"), this);
    m_autoKeyCheck->setObjectName(QStringLiteral("autoKeyCheck"));
    m_autoKeyCheck->setChecked(m_autoKey);
    m_autoKeyCheck->setToolTip(
        QStringLiteral("修改数值时自动在播放头处插入或更新关键帧"));
    connect(m_autoKeyCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_autoKey = on;
        emit autoKeyToggled(on);
    });
    m_keyLayout->addWidget(m_autoKeyCheck);

    m_trackHost = new QWidget(this);
    m_trackHost->setObjectName(QStringLiteral("trackHost"));
    m_trackForm = new QFormLayout(m_trackHost);
    m_trackForm->setContentsMargins(0, 2, 0, 0);
    m_trackForm->setVerticalSpacing(2);
    m_trackForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_trackForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_keyLayout->addWidget(m_trackHost);

    // The legacy button is kept for the legacy Project shell; session mode
    // uses the group combo above.
    m_easeButton->hide();

    // #50: keep the content pages on the first screen of an 800px window.
    // The entity and keyframe sections are the two tall blocks in front of
    // them; both stay one click away.  Compact controls match the denser
    // legacy property rows and keep their height stable across pages.
    m_basicSection->setExpanded(false);
    m_keySection->setExpanded(false);
    for (QWidget* widget : findChildren<QWidget*>())
        if (qobject_cast<QDoubleSpinBox*>(widget) || qobject_cast<QSpinBox*>(widget)
            || qobject_cast<QComboBox*>(widget)
            || qobject_cast<QLineEdit*>(widget))
            widget->setFixedHeight(24);
    for (CollapsibleSection* section :
         {m_transformSection, m_textSection, m_vectorSection})
        if (auto* form = section ? section->findChild<QFormLayout*>() : nullptr)
            form->setVerticalSpacing(2);
}

void PropertiesPanel::buildBasicSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);

    m_basicName = new QLineEdit(content);
    m_basicName->setObjectName(QStringLiteral("entityName"));
    connect(m_basicName, &QLineEdit::editingFinished, this, [this] {
        if (!m_session || m_editing || m_sessionUuid.isEmpty())
            return;
        const model::Entity* entity = sessionEntity();
        if (!entity || m_basicName->text() == entity->name)
            return;
        const QString name = m_basicName->text().trimmed();
        runSessionEdit(QStringLiteral("pvf: rename entity"),
                       [name](model::Entity& e) {
                           e.name = name;
                           return true;
                       });
    });
    form->addRow(QStringLiteral("名称"), m_basicName);

    m_basicStart = new QSpinBox(content);
    m_basicStart->setObjectName(QStringLiteral("entityStart"));
    m_basicStart->setRange(0, 1000000);
    m_basicStart->setSuffix(QStringLiteral(" 帧"));
    m_basicStart->setKeyboardTracking(false);
    form->addRow(QStringLiteral("开始"), m_basicStart);

    m_basicEnd = new QSpinBox(content);
    m_basicEnd->setObjectName(QStringLiteral("entityEnd"));
    m_basicEnd->setRange(1, 1000000);
    m_basicEnd->setSuffix(QStringLiteral(" 帧"));
    m_basicEnd->setKeyboardTracking(false);
    form->addRow(QStringLiteral("结束"), m_basicEnd);

    auto applyRange = [this] {
        if (!m_session || m_editing || m_sessionUuid.isEmpty())
            return;
        const model::Entity* entity = sessionEntity();
        if (!entity)
            return;
        const int start = m_basicStart->value();
        const int end = m_basicEnd->value();
        if (start == entity->start && end == entity->end)
            return;
        m_editing = true;
        QString error;
        const bool ok = m_session->editActions()->trimClip(
            m_sessionUuid, start, end, &error);
        m_editing = false;
        if (!ok)
            reportSessionError(error);
        refresh();
    };
    connect(m_basicStart, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [applyRange](int) { applyRange(); });
    connect(m_basicEnd, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [applyRange](int) { applyRange(); });

    m_basicRow = new QSpinBox(content);
    m_basicRow->setObjectName(QStringLiteral("entityRow"));
    m_basicRow->setRange(0, 1000000);
    m_basicRow->setKeyboardTracking(false);
    connect(m_basicRow, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (!m_session || m_editing)
                    return;
                const model::Entity* entity = sessionEntity();
                if (!entity || entity->row == value)
                    return;
                runSessionEdit(QStringLiteral("pvf: edit entity row"),
                               [value](model::Entity& e) {
                                   e.row = value;
                                   return true;
                               });
            });
    form->addRow(QStringLiteral("行"), m_basicRow);

    auto addFlag = [this, form, content](const QString& label, const QString& name,
                                         bool model::Entity::*member,
                                         bool ignoreLock = false) {
        auto* box = new QCheckBox(label, content);
        box->setObjectName(name);
        connect(box, &QCheckBox::toggled, this, [this, member, ignoreLock](bool on) {
            if (!m_sessionMode || m_editing)
                return;
            runSessionEdit(QStringLiteral("pvf: edit entity flags"),
                           [member, on](model::Entity& e) {
                               e.*member = on;
                               return true;
                           },
                           ignoreLock);
        });
        form->addRow(box);
        return box;
    };
    m_basicHidden = addFlag(QStringLiteral("隐藏"), QStringLiteral("entityHidden"),
                            &model::Entity::hidden);
    m_basicLocked = addFlag(QStringLiteral("锁定"), QStringLiteral("entityLocked"),
                            &model::Entity::locked, true);
    m_basicVisible = addFlag(QStringLiteral("可见"), QStringLiteral("entityVisible"),
                             &model::Entity::visible);
    m_basicInherit =
        addFlag(QStringLiteral("继承父级可见性"),
                QStringLiteral("entityInheritVisibility"),
                &model::Entity::inheritVisibility);
    m_basicPhysics =
        addFlag(QStringLiteral("参与物理干涉"),
                QStringLiteral("entityPhysicsInteract"),
                &model::Entity::physicsInteract);

    m_basicFollow = new QCheckBox(QStringLiteral("跟随父区间"), content);
    m_basicFollow->setObjectName(QStringLiteral("entityFollowParentRange"));
    m_basicFollow->setToolTip(
        QStringLiteral("开启后，子实体的时间范围始终等于直接父实体；"
                       "父实体移动或拉伸时子实体一起变化"));
    connect(m_basicFollow, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_session || m_editing || m_sessionUuid.isEmpty())
            return;
        const model::Entity* entity = sessionEntity();
        if (!entity || entity->followParentRange == on)
            return;
        m_editing = true;
        QString error;
        const bool ok =
            m_session->editActions()->setFollowParentRange(m_sessionUuid, on,
                                                           &error);
        m_editing = false;
        if (!ok)
            reportSessionError(error);
        refresh();
    });
    form->addRow(m_basicFollow);

    m_basicSection = new CollapsibleSection(QStringLiteral("实体"), content, this);
    m_basicSection->setExpanded(true);
    layout->insertWidget(1, m_basicSection);
}

void PropertiesPanel::buildCameraSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    m_cameraInfo = new QLabel(content);
    m_cameraInfo->setObjectName(QStringLiteral("cameraInfo"));
    m_cameraInfo->setWordWrap(true);
    m_cameraInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cameraInfo->setText(
        QStringLiteral("摄像机内容为只读：位置、旋转和取景比例请在变换区编辑。"
                       "取景比例按绝对值限制在 0.05–1.0（5%–100%）。"));
    box->addWidget(m_cameraInfo);
    m_cameraSection = new CollapsibleSection(QStringLiteral("摄像机"), content, this);
    m_cameraSection->setExpanded(true);
    layout->insertWidget(3, m_cameraSection);
}

void PropertiesPanel::addRow(QFormLayout* form, const QString& label, const QString& prop,
                             double lo, double hi, int decimals, double step, double factor)
{
    auto* rowWidget = new QWidget(form->parentWidget());
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);
    auto* spin = new QDoubleSpinBox(rowWidget);
    spin->setObjectName(QStringLiteral("row_") + prop);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    rowLayout->addWidget(spin, 1);
    auto* key = new QToolButton(rowWidget);
    key->setObjectName(QStringLiteral("key_") + prop);
    key->setFixedSize(18, 18);
    key->setAutoRaise(true);
    key->setText(QStringLiteral("◇"));
    key->setToolTip(QStringLiteral("在当前帧打关键帧"));
    key->setCursor(Qt::PointingHandCursor);
    connect(key, &QToolButton::clicked, this, [this, prop] {
        if (m_sessionMode)
        {
            sessionToggleKey(prop);
            return;
        }
    });
    rowLayout->addWidget(key);
    auto* labelWidget = new QLabel(label, form->parentWidget());
    form->addRow(labelWidget, rowWidget);

    Row row;
    row.prop = prop;
    row.baseLabel = label;
    row.label = labelWidget;
    row.spin = spin;
    row.key = key;
    row.factor = factor;
    m_rows.append(row);

    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop](double v) {
        if (m_sessionMode)
        {
            double rowFactor = 1.0;
            for (const Row& row : m_rows)
            {
                if (row.prop == prop)
                {
                    rowFactor = row.factor;
                    break;
                }
            }
            sessionSetTrackValue(prop, v * rowFactor);
            return;
        }
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
                  [this] { emitDataEdit(QStringLiteral("startFrame"), double(m_frame)); });

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
        [this] {
        });

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
        [this] {
        });

    m_audioVolume = new QSpinBox(content);
    m_audioVolume->setObjectName(QStringLiteral("audioVolume"));
    m_audioVolume->setRange(0, 200);
    m_audioVolume->setSuffix(QStringLiteral(" %"));
    m_audioVolume->setKeyboardTracking(false);
    form->addRow(QStringLiteral("音量"), m_audioVolume);

    m_audioFadeIn = new QDoubleSpinBox(content);
    m_audioFadeIn->setObjectName(QStringLiteral("audioFadeIn"));
    m_audioFadeIn->setRange(0.0, 36000.0);
    m_audioFadeIn->setDecimals(3);
    m_audioFadeIn->setSuffix(QStringLiteral(" s"));
    m_audioFadeIn->setKeyboardTracking(false);
    m_audioFadeIn->setToolTip(QStringLiteral("从音频开头渐入的秒数"));
    form->addRow(QStringLiteral("淡入"), m_audioFadeIn);

    m_audioFadeOut = new QDoubleSpinBox(content);
    m_audioFadeOut->setObjectName(QStringLiteral("audioFadeOut"));
    m_audioFadeOut->setRange(0.0, 36000.0);
    m_audioFadeOut->setDecimals(3);
    m_audioFadeOut->setSuffix(QStringLiteral(" s"));
    m_audioFadeOut->setKeyboardTracking(false);
    m_audioFadeOut->setToolTip(QStringLiteral("到音频结尾前渐出的秒数"));
    form->addRow(QStringLiteral("淡出"), m_audioFadeOut);

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
            [this](int value) { emitDataEdit(QStringLiteral("startFrame"), double(value)); });
    connect(m_audioTrimStart, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("trimStart"), value); });
    connect(m_audioTrimEnd, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("trimEnd"), value); });
    connect(m_audioVolume, &QSpinBox::valueChanged, this,
            [this](int value) { emitDataEdit(QStringLiteral("volume"), value / 100.0); });
    connect(m_audioFadeIn, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("fadeIn"), value); });
    connect(m_audioFadeOut, &QDoubleSpinBox::valueChanged, this,
            [this](double value) { emitDataEdit(QStringLiteral("fadeOut"), value); });
}

} // namespace pvf
