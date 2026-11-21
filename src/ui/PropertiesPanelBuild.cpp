#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "WholeFrameEdit.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "model/Content.h"
#include "model/Entity.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "next/properties/SolverPreviewWidget.h"

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
    configurePropertyForm(transformForm);

    addRow(transformForm, QStringLiteral("X"), QStringLiteral("x"), -10000,
           10000, 1, 1);
    addRow(transformForm, QStringLiteral("Y"), QStringLiteral("y"), -10000,
           10000, 1, 1);
    addRow(transformForm, QStringLiteral("旋转"), QStringLiteral("rotation"),
           -3600, 3600, 1, 1);
    addRow(transformForm, QStringLiteral("缩放 X"), QStringLiteral("scaleX"),
           -100, 100, 3, 0.01);
    addRow(transformForm, QStringLiteral("缩放 Y"), QStringLiteral("scaleY"),
           -100, 100, 3, 0.01);

    // Keep the equivalent-scale toggle directly under the two scale rows,
    // where it belongs; the top row is only for whole-transform operations.
    auto* linkRow = new QWidget(transformContent);
    auto* linkLayout = new QHBoxLayout(linkRow);
    linkLayout->setContentsMargins(0, 0, 0, 0);
    linkLayout->setSpacing(4);
    m_transformLink = new QCheckBox(QStringLiteral("等比缩放"), linkRow);
    m_transformLink->setObjectName(QStringLiteral("transformLink"));
    m_transformLink->setToolTip(
        QStringLiteral("勾选后修改缩放 X/Y 会按当前比例联动另一轴"));
    connect(m_transformLink, &QCheckBox::toggled, this,
            &PropertiesPanel::scaleLinkChanged);
    linkLayout->addWidget(m_transformLink);
    linkLayout->addStretch(1);
    transformForm->addRow(linkRow);

    addRow(transformForm, QStringLiteral("透明度"), QStringLiteral("opacity"),
           0, 100, 0, 1, 0.01);

    auto* transformOps = new QWidget(transformContent);
    auto* opsLayout = new QHBoxLayout(transformOps);
    opsLayout->setContentsMargins(0, 0, 0, 0);
    opsLayout->setSpacing(4);
    auto* copy = new QPushButton(QStringLiteral("复制"), transformOps);
    copy->setObjectName(QStringLiteral("transformCopy"));
    auto* paste = new QPushButton(QStringLiteral("粘贴"), transformOps);
    paste->setObjectName(QStringLiteral("transformPaste"));
    auto* reset = new QPushButton(QStringLiteral("重置"), transformOps);
    reset->setObjectName(QStringLiteral("transformReset"));
    // The three operation buttons share the remaining width instead of
    // overflowing a narrow dock.
    for (QPushButton* button : {copy, paste, reset})
    {
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        opsLayout->addWidget(button, 1);
    }
    transformForm->addRow(transformOps);
    connect(copy, &QPushButton::clicked, this,
            &PropertiesPanel::copyTransformParams);
    connect(paste, &QPushButton::clicked, this,
            &PropertiesPanel::pasteTransformParams);
    connect(reset, &QPushButton::clicked, this,
            &PropertiesPanel::resetTransformParams);

    m_transformSection =
        new CollapsibleSection(QStringLiteral("变换"), transformContent, this);
    m_transformSection->setObjectName(QStringLiteral("transformSection"));
    m_transformSection->setExpanded(true);
    layout->addWidget(m_transformSection);

    auto* keyContent = new QWidget(this);
    m_keyLayout = new QVBoxLayout(keyContent);
    m_keyLayout->setContentsMargins(10, 8, 10, 8);
    m_keyLayout->setSpacing(6);
    m_keySection = new CollapsibleSection(QStringLiteral("关键帧"), keyContent, this);
    m_keySection->setObjectName(QStringLiteral("keySection"));
    m_keySection->setExpanded(true);
    layout->addWidget(m_keySection);

    buildTextSection(layout);
    buildVectorSection(layout);
    buildMediaSection(layout);
    buildAudioSection(layout);
    buildPivotSection(layout);
    layout->addStretch(1);
}

void PropertiesPanel::buildSessionUi()
{
    // The whole-frame status bar sits above every section.  There is no
    // per-parameter diamond and no Auto-Key checkbox: every animation edit
    // writes the whole frame.
    m_keyStatusRow = new QWidget(this);
    m_keyStatusRow->setObjectName(QStringLiteral("keyframeStatusRow"));
    auto* statusLayout = new QHBoxLayout(m_keyStatusRow);
    statusLayout->setContentsMargins(10, 6, 10, 0);
    statusLayout->setSpacing(6);
    m_keyStatus = new QLabel(m_keyStatusRow);
    m_keyStatus->setObjectName(QStringLiteral("keyframeStatus"));
    m_keyStatus->setWordWrap(true);
    statusLayout->addWidget(m_keyStatus, 1);
    m_keyDelete = new QPushButton(QStringLiteral("删帧"), m_keyStatusRow);
    m_keyDelete->setObjectName(QStringLiteral("keyframeDeleteButton"));
    m_keyDelete->setToolTip(QStringLiteral("整帧删除当前关键帧"));
    m_keyDelete->setFixedWidth(52);
    statusLayout->addWidget(m_keyDelete);
    m_rootLayout->insertWidget(1, m_keyStatusRow);
    connect(m_keyDelete, &QPushButton::clicked, this,
            &PropertiesPanel::deleteActiveKeyframe);

    buildBasicSection(m_rootLayout);

    // Text layout fields that only the new model exposes.
    m_textLineHeight = new QDoubleSpinBox(this);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeight"));
    m_textLineHeight->setRange(0.1, 10.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.05);
    connect(m_textLineHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                writeAnimationValue(QStringLiteral("lineHeight"), value);
            });
    m_textForm->addRow(QStringLiteral("行高"), m_textLineHeight);

    m_textLetterSpacing = new QDoubleSpinBox(this);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacing"));
    m_textLetterSpacing->setRange(-1000.0, 1000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    connect(m_textLetterSpacing, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                writeAnimationValue(QStringLiteral("letterSpacing"), value);
            });
    m_textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);

    m_textPadding = new QDoubleSpinBox(this);
    m_textPadding->setObjectName(QStringLiteral("textPadding"));
    m_textPadding->setRange(0.0, 100000.0);
    m_textPadding->setDecimals(2);
    m_textPadding->setSingleStep(1.0);
    m_textPadding->setSuffix(QStringLiteral(" px"));
    connect(m_textPadding, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                writeAnimationValue(QStringLiteral("padding"), value);
            });
    m_textForm->addRow(QStringLiteral("内边距"), m_textPadding);

    m_textBoxWidth = new QDoubleSpinBox(this);
    m_textBoxWidth->setObjectName(QStringLiteral("textBoxWidth"));
    m_textBoxWidth->setRange(0.0, 100000.0);
    m_textBoxWidth->setDecimals(2);
    m_textBoxWidth->setSingleStep(1.0);
    m_textBoxWidth->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxWidth, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                writeAnimationValue(QStringLiteral("boxWidth"), value);
            });
    m_textForm->addRow(QStringLiteral("框宽"), m_textBoxWidth);

    m_textBoxHeight = new QDoubleSpinBox(this);
    m_textBoxHeight->setObjectName(QStringLiteral("textBoxHeight"));
    m_textBoxHeight->setRange(0.0, 100000.0);
    m_textBoxHeight->setDecimals(2);
    m_textBoxHeight->setSingleStep(1.0);
    m_textBoxHeight->setSuffix(QStringLiteral(" px"));
    connect(m_textBoxHeight, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                writeAnimationValue(QStringLiteral("boxHeight"), value);
            });
    m_textForm->addRow(QStringLiteral("框高"), m_textBoxHeight);

    // Asset fields.
    auto* assetForm = new QFormLayout();
    configurePropertyForm(assetForm, QMargins(0, 0, 0, 0));
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
    m_assetSourceIn->setToolTip(
        QStringLiteral("从素材的第几帧开始播放；范围受素材长度限制"));
    connect(m_assetSourceIn, &QSpinBox::valueChanged, this, [this](int value) {
        applySessionAsset(QStringLiteral("sourceIn"), value);
    });
    assetForm->addRow(QStringLiteral("素材起点"), m_assetSourceIn);
    m_mediaLayout->addLayout(assetForm);

    buildSessionHtmlSection(m_rootLayout);
    buildCameraSection(m_rootLayout);
    buildSolverSection(m_rootLayout);

    // Keyframe metadata: one whole-frame easing, a note, solver conversion and
    // the trajectory preview all live in the key section.
    auto* easingRow = new QWidget(this);
    auto* easingLayout = new QHBoxLayout(easingRow);
    easingLayout->setContentsMargins(0, 0, 0, 0);
    easingLayout->setSpacing(6);
    easingLayout->addWidget(new QLabel(QStringLiteral("整帧缓动"), easingRow));
    m_keyEasingButton = new QToolButton(easingRow);
    m_keyEasingButton->setObjectName(QStringLiteral("keyframeEasingGroup"));
    m_keyEasingButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_keyEasingButton->setIconSize(QSize(40, 22));
    m_keyEasingButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_keyEasingButton->setToolTip(
        QStringLiteral("整帧共用一条缓动：从本关键帧到下一关键帧。"
                       "点开选择曲线缩略图，或自定义贝塞尔"));
    easingLayout->addWidget(m_keyEasingButton, 1);
    m_keyLayout->addWidget(easingRow);
    connect(m_keyEasingButton, &QToolButton::clicked, this,
            &PropertiesPanel::openKeyframeEasingMenu);

    auto* noteRow = new QWidget(this);
    auto* noteLayout = new QHBoxLayout(noteRow);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(6);
    noteLayout->addWidget(new QLabel(QStringLiteral("注释给 AI"), noteRow));
    m_keyNote = new QLineEdit(noteRow);
    m_keyNote->setObjectName(QStringLiteral("keyframeNote"));
    m_keyNote->setPlaceholderText(QStringLiteral("注释关键帧（给 AI 定位节奏）"));
    noteLayout->addWidget(m_keyNote, 1);
    m_keyLayout->addWidget(noteRow);
    connect(m_keyNote, &QLineEdit::editingFinished, this, [this] {
        const model::Entity* entity = sessionEntity();
        if (!entity || !sessionEditable())
            return;
        const QString note = m_keyNote->text();
        if (activeTargetKeyFrames(*entity).size() > 1)
        {
            bool allSame = true;
            for (int frame : activeTargetKeyFrames(*entity))
            {
                const model::Keyframe* key =
                    model::keyframeAt(entity->keyframes, frame);
                if (!key || key->note != note)
                {
                    allSame = false;
                    break;
                }
            }
            if (allSame)
                return;
            if (!applyBatchKeyframeEdit(
                    QStringLiteral("pvf: batch set keyframe note"),
                    [note](model::Keyframe& key) { key.note = note; }))
                refresh();
            return;
        }
        const int frame = activeKeyFrame(*entity);
        const model::Keyframe* key = model::keyframeAt(entity->keyframes, frame);
        if (!key || key->note == note)
            return;
        m_editing = true;
        QString error;
        const bool ok = m_session->editActions()->setKeyframeNote(
            {KeyRef{entity->uuid, frame}}, note, &error);
        m_editing = false;
        if (!ok)
            reportSessionError(error);
        refresh();
    });

    m_convertSolverButton =
        new QPushButton(QStringLiteral("转换为求解关键帧…"), this);
    m_convertSolverButton->setObjectName(QStringLiteral("solverConvertButton"));
    m_convertSolverButton->setEnabled(false);
    m_keyLayout->addWidget(m_convertSolverButton);
    connect(m_convertSolverButton, &QPushButton::clicked, this,
            &PropertiesPanel::showSolverConvertMenu);

    m_cancelSolverButton = new QPushButton(QStringLiteral("取消求解"), this);
    m_cancelSolverButton->setObjectName(QStringLiteral("solverCancelButton"));
    m_cancelSolverButton->hide();
    m_keyLayout->addWidget(m_cancelSolverButton);
    connect(m_cancelSolverButton, &QPushButton::clicked, this,
            &PropertiesPanel::cancelCurrentSolver);

    m_convertSolverHint = new QLabel(this);
    m_convertSolverHint->setObjectName(QStringLiteral("solverConvertHint"));
    m_convertSolverHint->setWordWrap(true);
    m_convertSolverHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    styleHintLabel(m_convertSolverHint);
    m_keyLayout->addWidget(m_convertSolverHint);

    if (m_session)
        m_solverPreview = new next::SolverPreviewWidget(
            m_session->document(), this);
    if (m_solverPreview)
    {
        if (m_session)
            m_solverPreview->setAnimCache(&m_session->animCache());
        m_solverPreview->setObjectName(QStringLiteral("solverPreviewMini"));
        m_solverPreview->setMinimumWidth(260);
        m_solverPreview->setFixedHeight(172);
        m_solverPreview->setToolTip(
            QStringLiteral("拖动红色箭头手柄调整初速度（松手提交，Esc 取消）"));
        m_solverPreview->hide();
        m_keyLayout->addWidget(m_solverPreview);
    }

    m_basicSection->setExpanded(false);
    m_keySection->setExpanded(true);
    // One growing field column for every editor: numbers, enums and colour
    // buttons share the same right edge.
    for (QWidget* widget : findChildren<QWidget*>())
        if (qobject_cast<QDoubleSpinBox*>(widget)
            || qobject_cast<QSpinBox*>(widget) || qobject_cast<QComboBox*>(widget)
            || qobject_cast<QLineEdit*>(widget))
            stylePropertyField(widget);
    for (QFormLayout* form : findChildren<QFormLayout*>())
        equalizePropertyFormLabels(form);
}

void PropertiesPanel::buildBasicSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    configurePropertyForm(form);

    // Parent selection is identity/range information, not a visual transform:
    // it lives in the basic section so audio clips keep it while the transform
    // section is hidden.
    m_parentRow = new QWidget(content);
    auto* parentLayout = new QHBoxLayout(m_parentRow);
    parentLayout->setContentsMargins(0, 0, 0, 0);
    m_parent = new QComboBox(m_parentRow);
    m_parent->setObjectName(QStringLiteral("parentCombo"));
    m_parent->setToolTip(
        QStringLiteral("选择实体的父级；子级的位置/旋转/缩放相对父级"));
    parentLayout->addWidget(m_parent);
    form->addRow(QStringLiteral("父级"), m_parentRow);
    m_parentLabel = qobject_cast<QLabel*>(form->labelForField(m_parentRow));
    connect(m_parent, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (m_sessionUuid.isEmpty() || m_parent->currentIndex() < 0)
                    return;
                applySessionParent(m_parent->currentData().toString());
            });

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
    m_basicRow->setRange(-1000000, 1000000);
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

    auto addDefinitionFlag = [this, form, content](
                                 const QString& label, const QString& name,
                                 bool model::Entity::*member,
                                 bool ignoreLock = false) {
        auto* box = new QCheckBox(label, content);
        box->setObjectName(name);
        connect(box, &QCheckBox::toggled, this,
                [this, member, ignoreLock](bool on) {
                    if (m_editing)
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
    m_basicHidden = addDefinitionFlag(QStringLiteral("隐藏"),
                                      QStringLiteral("entityHidden"),
                                      &model::Entity::hidden);
    m_basicLocked = addDefinitionFlag(QStringLiteral("锁定"),
                                      QStringLiteral("entityLocked"),
                                      &model::Entity::locked, true);
    m_basicInherit = addDefinitionFlag(
        QStringLiteral("继承父级可见性"),
        QStringLiteral("entityInheritVisibility"),
        &model::Entity::inheritVisibility);

    // Visible and physicsInteract are animation parameters: they use the same
    // whole-frame auto-key rule as any numeric field.
    m_basicVisible = new QCheckBox(QStringLiteral("可见"), content);
    m_basicVisible->setObjectName(QStringLiteral("entityVisible"));
    connect(m_basicVisible, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_editing)
            writeAnimationValue(QStringLiteral("visible"), on ? 1.0 : 0.0,
                                true);
    });
    form->addRow(m_basicVisible);
    m_basicPhysics = new QCheckBox(QStringLiteral("参与物理干涉"), content);
    m_basicPhysics->setObjectName(QStringLiteral("entityPhysicsInteract"));
    connect(m_basicPhysics, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_editing)
            writeAnimationValue(QStringLiteral("physicsInteract"),
                                on ? 1.0 : 0.0, true);
    });
    form->addRow(m_basicPhysics);

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
    m_basicSection->setObjectName(QStringLiteral("basicSection"));
    m_basicSection->setExpanded(true);
    layout->insertWidget(2, m_basicSection);
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
    styleHintLabel(m_cameraInfo);
    box->addWidget(m_cameraInfo);
    m_cameraSection = new CollapsibleSection(QStringLiteral("摄像机"), content, this);
    m_cameraSection->setExpanded(true);
    layout->insertWidget(3, m_cameraSection);
}

void PropertiesPanel::addRow(QFormLayout* form, const QString& label,
                             const QString& prop, double lo, double hi,
                             int decimals, double step, double factor)
{
    auto* spin = new QDoubleSpinBox(form->parentWidget());
    spin->setObjectName(QStringLiteral("row_") + prop);
    spin->setRange(lo, hi);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(false);
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
        double rowFactor = 1.0;
        for (const Row& row : m_rows)
            if (row.prop == prop)
            {
                rowFactor = row.factor;
                break;
            }
        const bool linkedScale =
            m_transformLink && m_transformLink->isChecked()
            && (prop == QLatin1String("scaleX")
                || prop == QLatin1String("scaleY"));
        writeAnimationValue(prop, v * rowFactor, false, linkedScale);
    });
}

void PropertiesPanel::buildAudioSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* form = new QFormLayout(content);
    configurePropertyForm(form);

    auto addCaptureRow = [this, content, form](const QString& label,
                                               QWidget* field,
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
    m_audioStart->setToolTip(
        QStringLiteral("音频在时间轴上的起始帧；也可以拖动时间轴上的音频块"));
    addCaptureRow(QStringLiteral("时间轴开始"), m_audioStart,
                  QStringLiteral("audioStartFromPlayhead"),
                  QStringLiteral("把音频在时间轴上的开始位置设为播放头位置"),
                  [this] { applySessionData(QStringLiteral("startFrame"), double(m_frame)); });

    m_audioTrimStart = new QDoubleSpinBox(content);
    m_audioTrimStart->setObjectName(QStringLiteral("audioTrimStart"));
    m_audioTrimStart->setRange(0.0, 36000.0);
    m_audioTrimStart->setDecimals(3);
    m_audioTrimStart->setSuffix(QStringLiteral(" s"));
    m_audioTrimStart->setKeyboardTracking(false);
    m_audioTrimStart->setToolTip(
        QStringLiteral("裁掉音频开头的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(QStringLiteral("裁掉开头"), m_audioTrimStart,
                  QStringLiteral("audioTrimStartFromPlayhead"),
                  QStringLiteral("从播放头对应的音频位置开始，裁掉前面部分；后面的波形保持原位"),
                  [this] {});

    m_audioTrimEnd = new QDoubleSpinBox(content);
    m_audioTrimEnd->setObjectName(QStringLiteral("audioTrimEnd"));
    m_audioTrimEnd->setRange(0.0, 36000.0);
    m_audioTrimEnd->setDecimals(3);
    m_audioTrimEnd->setSuffix(QStringLiteral(" s"));
    m_audioTrimEnd->setKeyboardTracking(false);
    m_audioTrimEnd->setToolTip(
        QStringLiteral("裁掉音频结尾的秒数；不改变音频在时间轴上的位置"));
    addCaptureRow(QStringLiteral("裁掉结尾"), m_audioTrimEnd,
                  QStringLiteral("audioTrimEndFromPlayhead"),
                  QStringLiteral("在播放头对应的音频位置结束，裁掉后面部分；前面的波形保持原位"),
                  [this] {});

    m_audioVolume = new QDoubleSpinBox(content);
    m_audioVolume->setObjectName(QStringLiteral("audioVolume"));
    m_audioVolume->setRange(-60.0, 24.0);
    m_audioVolume->setDecimals(1);
    m_audioVolume->setSingleStep(0.5);
    m_audioVolume->setSuffix(QStringLiteral(" dB"));
    m_audioVolume->setKeyboardTracking(false);
    m_audioVolume->setToolTip(
        QStringLiteral("音频增益；-60 dB 为静音。底层仍存线性倍率，"
                       "修改会自动写整帧关键帧"));
    form->addRow(QStringLiteral("增益"), m_audioVolume);

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

    auto* hint = new QLabel(
        QStringLiteral("直接拖动时间轴上的音频块可以整体移动；「时间轴开始」决定位置，"
                       "「裁掉开头/结尾」会缩短音频块，但保留的波形保持原位。"
                       "「取」把播放头位置填入字段。"),
        content);
    hint->setWordWrap(true);
    styleHintLabel(hint);
    form->addRow(hint);

    m_audioSection = new CollapsibleSection(QStringLiteral("音频"), content, this);
    m_audioSection->setObjectName(QStringLiteral("audioSection"));
    m_audioSection->setExpanded(true);
    layout->addWidget(m_audioSection);

    connect(m_audioStart, &QSpinBox::valueChanged, this,
            [this](int value) {
                applySessionData(QStringLiteral("startFrame"), double(value));
            });
    connect(m_audioTrimStart, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applySessionData(QStringLiteral("trimStart"), value);
            });
    connect(m_audioTrimEnd, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applySessionData(QStringLiteral("trimEnd"), value);
            });
    connect(m_audioVolume, &QDoubleSpinBox::valueChanged, this,
            [this](double db) {
                writeAnimationValue(QStringLiteral("volume"),
                                    audioLinearGainFromDb(db));
            });
    connect(m_audioFadeIn, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applySessionData(QStringLiteral("fadeIn"), value);
            });
    connect(m_audioFadeOut, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                applySessionData(QStringLiteral("fadeOut"), value);
            });
}

} // namespace pvf
