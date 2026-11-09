#include "PropertiesPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

struct AlignOption
{
    const char* label;
    int value;
};

const AlignOption kAlignOptions[] = {
    {"左上", Qt::AlignLeft | Qt::AlignTop},
    {"上中", Qt::AlignHCenter | Qt::AlignTop},
    {"右上", Qt::AlignRight | Qt::AlignTop},
    {"左中", Qt::AlignLeft | Qt::AlignVCenter},
    {"居中", Qt::AlignHCenter | Qt::AlignVCenter},
    {"右中", Qt::AlignRight | Qt::AlignVCenter},
    {"左下", Qt::AlignLeft | Qt::AlignBottom},
    {"下中", Qt::AlignHCenter | Qt::AlignBottom},
    {"右下", Qt::AlignRight | Qt::AlignBottom},
};

struct ShapeOption
{
    const char* label;
    int value;
};

// Same shape numbering as the legacy vector editor; the new schema has not
// frozen the enum yet.
const ShapeOption kShapeOptions[] = {
    {"矩形", 0}, {"圆角矩形", 3}, {"椭圆", 1}, {"直线", 2}, {"三角形", 4},
    {"心形", 5}, {"星形", 6}, {"多边形", 7}, {"箭头", 8},
};

enum class ParamKind
{
    Number,
    Uuid,
    Direction
};

struct ParamSpec
{
    QString key;
    QString label;
    QString tooltip;
    ParamKind kind = ParamKind::Number;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 1.0;
    int decimals = 2;
    QString suffix;
};

ParamSpec numberParam(const QString& key, const QString& label, const QString& tooltip,
                      double defaultValue, double minimum, double maximum,
                      double step = 1.0, int decimals = 2, const QString& suffix = {})
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.tooltip = tooltip;
    spec.kind = ParamKind::Number;
    spec.defaultValue = defaultValue;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.step = step;
    spec.decimals = decimals;
    spec.suffix = suffix;
    return spec;
}

ParamSpec uuidParam(const QString& key, const QString& label, const QString& tooltip)
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.tooltip = tooltip;
    spec.kind = ParamKind::Uuid;
    return spec;
}

ParamSpec directionParam()
{
    ParamSpec spec;
    spec.key = QStringLiteral("direction");
    spec.label = QStringLiteral("方向");
    spec.kind = ParamKind::Direction;
    spec.defaultValue = 1.0;
    return spec;
}

// Labels and tooltips mirror docs/MCP_TOOLS.md 5.2; they are shown next to each
// parameter so the panel is self-explanatory without the MCP guide.
QVector<ParamSpec> solverParams(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("目标实体"),
                      QStringLiteral("重力地面/方向实体；它的局部 +Y 是重力方向，目标旋转时方向跟着转。")),
            numberParam(QStringLiteral("strength"), QStringLiteral("重力强度 (px/s²)"),
                        QStringLiteral("重力加速度。数值越大下落越快；方向始终是目标实体的局部 +Y，实体旋转时重力方向跟着转。"),
                        980.0, 0.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("elasticity"), QStringLiteral("弹性 (0..1)"),
                        QStringLiteral("碰撞反弹保留的法向速度比例。0 = 完全不反弹，1 = 无能量损失。"),
                        0.35, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("friction"), QStringLiteral("摩擦 (0..1)"),
                        QStringLiteral("碰撞时削减的切向速度比例。0 = 无摩擦、沿地面滑行；1 = 立即停止切向运动。"),
                        0.2, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("stopThreshold"), QStringLiteral("停止阈值 (px/s)"),
                        QStringLiteral("速度低于该值就停止并保持当前值，避免无限微小抖动。"),
                        2.0, 0.0, 1000.0, 0.5, 2),
        };
    case SolverEffect::Orbit:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体"),
                      QStringLiteral("公转中心实体。每帧读取它当前的位置和旋转；不能是目标实体自己，也不能形成循环依赖。")),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率 (短/长轴)"),
                        QStringLiteral("椭圆的短轴/长轴。1 = 正圆；越小轨道越扁。"),
                        1.0, 0.01, 1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度 (°)"),
                        QStringLiteral("椭圆长轴相对中心实体局部坐标的旋转角。"),
                        0.0, -360.0, 360.0, 1.0, 2),
            directionParam(),
            numberParam(QStringLiteral("periodFrames"), QStringLiteral("周期 (帧)"),
                        QStringLiteral("转一圈所需帧数。"),
                        60.0, 1.0, 1000000.0, 1.0, 0),
        };
    case SolverEffect::Spin:
        return {
            numberParam(QStringLiteral("angularVelocity"), QStringLiteral("角速度 (°/帧)"),
                        QStringLiteral("每帧旋转角度。30fps 下 6 = 180°/秒。"),
                        6.0, 0.0, 360.0, 0.5, 2),
            directionParam(),
        };
    case SolverEffect::BlackHole:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("黑洞实体"),
                      QStringLiteral("黑洞实体。每帧读取它的位置；不能是目标实体自己。")),
            numberParam(QStringLiteral("simulationSpeed"), QStringLiteral("模拟速度 (倍率)"),
                        QStringLiteral("模拟时间倍率。> 1 加速演化，< 1 放慢；只影响解算，不改工程帧率。"),
                        1.0, 0.01, 20.0, 0.1, 2),
            numberParam(QStringLiteral("mass"), QStringLiteral("质量"),
                        QStringLiteral("黑洞质量。越大引力越强、捕获范围越大。"),
                        1000.0, 0.0001, 1000000000.0, 10.0, 2),
            numberParam(QStringLiteral("angularMomentum"), QStringLiteral("角动量"),
                        QStringLiteral("角动量。非零时产生切向拖拽，轨迹呈旋涡；正负决定旋涡方向。"),
                        0.0, -1000000.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("eventHorizonRadius"), QStringLiteral("视界半径 (px)"),
                        QStringLiteral("事件视界半径，用于可视化和捕获判定参考。它不是硬边界，物体可以穿过后继续运动。"),
                        80.0, 0.0, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("objectRadius"), QStringLiteral("物体半径 (px)"),
                        QStringLiteral("运动物体半径，用于和视界/其他物体做距离判定。"),
                        40.0, 0.0, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("softening"), QStringLiteral("软化半径 (px)"),
                        QStringLiteral("引力软化半径，避免距离趋近 0 时力发散；越大中心附近越平缓。"),
                        60.0, 0.0001, 1000000.0, 1.0, 1),
            numberParam(QStringLiteral("maxSpeed"), QStringLiteral("最大速度 (px/s)"),
                        QStringLiteral("速度上限，防止数值爆炸或物体瞬间飞出画布。"),
                        4000.0, 0.0001, 1000000.0, 100.0, 1),
        };
    }
    return {};
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

void styleColorButton(QPushButton* button, const QColor& color)
{
    if (!color.isValid())
        return;
    const QColor text = color.lightnessF() > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2;"
                       " border: 1px solid #888; border-radius: 0; padding: 2px 6px; }")
            .arg(color.name(QColor::HexRgb), text.name(QColor::HexRgb)));
}

QString contentKindLabel(ContentKind kind)
{
    switch (kind)
    {
    case ContentKind::Empty:
        return QStringLiteral("空");
    case ContentKind::Text:
        return QStringLiteral("文字");
    case ContentKind::Vector:
        return QStringLiteral("矢量");
    case ContentKind::Camera:
        return QStringLiteral("摄像机");
    case ContentKind::Asset:
        return QStringLiteral("素材");
    }
    return QStringLiteral("空");
}

QString solverRangeText(const model::SolverSegment& solver)
{
    if (solver.endFrame < 0)
        return QStringLiteral("%1 帧 – 片段末帧").arg(solver.startFrame);
    return QStringLiteral("%1 – %2 帧").arg(solver.startFrame).arg(solver.endFrame);
}

QFormLayout* addSectionForm(QVBoxLayout* layout, QWidget* parent, const QString& title,
                            QGroupBox** group = nullptr)
{
    auto* box = new QGroupBox(title, parent);
    if (group)
        *group = box;
    auto* form = new QFormLayout(box);
    form->setContentsMargins(10, 8, 10, 8);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(box);
    return form;
}

} // namespace

PropertiesPanel::PropertiesPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setMinimumWidth(300);
    buildUi();

    if (m_document)
    {
        connect(m_document, &Document::selectionChanged, this, &PropertiesPanel::refresh);
        connect(m_document, &Document::changed, this, [this] {
            if (!m_committing)
                refresh();
        });
    }
    refresh();
}

void PropertiesPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    m_title->setObjectName(QStringLiteral("panelTitle"));
    m_title->setContentsMargins(10, 8, 10, 8);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    root->addWidget(m_title);

    m_placeholder = new QLabel(QStringLiteral("在时间轴中选择一个实体来编辑属性"), this);
    m_placeholder->setObjectName(QStringLiteral("noSelectionLabel"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setContentsMargins(16, 16, 16, 16);
    root->addWidget(m_placeholder);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("propertiesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scroll);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll, 1);

    buildBasicSection();
    buildTransformSection();
    buildContentSection();
    buildSolverSection();
    m_contentLayout->addStretch(1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setContentsMargins(10, 4, 10, 6);
    m_status->setWordWrap(true);
    root->addWidget(m_status);
}

void PropertiesPanel::buildBasicSection()
{
    QFormLayout* form = addSectionForm(m_contentLayout, m_content, QStringLiteral("基础"));

    m_nameEdit = new QLineEdit(m_content);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    form->addRow(QStringLiteral("名称"), m_nameEdit);
    addEditable(m_nameEdit);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_loading)
            return;
        const QString text = m_nameEdit->text();
        if (text.isEmpty())
        {
            setStatus(QStringLiteral("名称不能为空"));
            refresh();
            return;
        }
        commitEdit(QStringLiteral("name"), [text](model::Entity& entity) {
            entity.name = text;
            return true;
        });
    });

    m_startSpin = new QSpinBox(m_content);
    m_startSpin->setObjectName(QStringLiteral("startSpin"));
    m_startSpin->setRange(0, 1000000);
    m_startSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("开始帧"), m_startSpin);
    addEditable(m_startSpin);
    connect(m_startSpin, &QSpinBox::editingFinished, this, [this] {
        const int value = m_startSpin->value();
        commitEdit(QStringLiteral("start"), [value](model::Entity& entity) {
            entity.start = value;
            return true;
        });
    });

    m_endSpin = new QSpinBox(m_content);
    m_endSpin->setObjectName(QStringLiteral("endSpin"));
    m_endSpin->setRange(1, 1000000);
    m_endSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("结束帧"), m_endSpin);
    addEditable(m_endSpin);
    connect(m_endSpin, &QSpinBox::editingFinished, this, [this] {
        const int value = m_endSpin->value();
        commitEdit(QStringLiteral("end"), [value](model::Entity& entity) {
            entity.end = value;
            return true;
        });
    });

    m_rowSpin = new QSpinBox(m_content);
    m_rowSpin->setObjectName(QStringLiteral("rowSpin"));
    m_rowSpin->setRange(0, 100000);
    m_rowSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("行号"), m_rowSpin);
    addEditable(m_rowSpin);
    connect(m_rowSpin, &QSpinBox::editingFinished, this, [this] {
        const int value = m_rowSpin->value();
        commitEdit(QStringLiteral("row"), [value](model::Entity& entity) {
            entity.row = value;
            return true;
        });
    });

    m_hiddenCheck = new QCheckBox(QStringLiteral("编辑器隐藏"), m_content);
    m_hiddenCheck->setObjectName(QStringLiteral("hiddenCheck"));
    form->addRow(QString(), m_hiddenCheck);
    bindCheckBox(m_hiddenCheck, &model::Entity::hidden, QStringLiteral("hidden"));

    m_lockedCheck = new QCheckBox(QStringLiteral("锁定（不可编辑）"), m_content);
    m_lockedCheck->setObjectName(QStringLiteral("lockedCheck"));
    form->addRow(QString(), m_lockedCheck);
    connect(m_lockedCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_loading)
            return;
        commitEdit(QStringLiteral("locked"), [checked](model::Entity& entity) {
            entity.locked = checked;
            return true;
        }, true);
    });

    m_visibleCheck = new QCheckBox(QStringLiteral("渲染可见"), m_content);
    m_visibleCheck->setObjectName(QStringLiteral("visibleCheck"));
    form->addRow(QString(), m_visibleCheck);
    bindCheckBox(m_visibleCheck, &model::Entity::visible, QStringLiteral("visible"));

    m_inheritCheck = new QCheckBox(QStringLiteral("继承父级可见性"), m_content);
    m_inheritCheck->setObjectName(QStringLiteral("inheritVisibilityCheck"));
    form->addRow(QString(), m_inheritCheck);
    bindCheckBox(m_inheritCheck, &model::Entity::inheritVisibility,
                 QStringLiteral("inheritVisibility"));

    m_physicsCheck = new QCheckBox(QStringLiteral("参与物理碰撞"), m_content);
    m_physicsCheck->setObjectName(QStringLiteral("physicsInteractCheck"));
    form->addRow(QString(), m_physicsCheck);
    bindCheckBox(m_physicsCheck, &model::Entity::physicsInteract,
                 QStringLiteral("physicsInteract"));
}

void PropertiesPanel::buildTransformSection()
{
    QFormLayout* form = addSectionForm(m_contentLayout, m_content, QStringLiteral("变换"));

    auto makeSpin = [this, form](const QString& label, const QString& objectName,
                                 double minimum, double maximum, double step,
                                 int decimals) {
        auto* spin = new QDoubleSpinBox(m_content);
        spin->setObjectName(objectName);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(false);
        form->addRow(label, spin);
        addEditable(spin);
        return spin;
    };

    m_xSpin = makeSpin(QStringLiteral("X (px)"), QStringLiteral("transformXSpin"),
                       -1000000000.0, 1000000000.0, 1.0, 2);
    m_ySpin = makeSpin(QStringLiteral("Y (px)"), QStringLiteral("transformYSpin"),
                       -1000000000.0, 1000000000.0, 1.0, 2);
    m_rotationSpin = makeSpin(QStringLiteral("旋转 (°)"), QStringLiteral("transformRotationSpin"),
                              -36000.0, 36000.0, 1.0, 2);
    m_scaleXSpin = makeSpin(QStringLiteral("缩放 X"), QStringLiteral("transformScaleXSpin"),
                            -1000.0, 1000.0, 0.01, 4);
    m_scaleYSpin = makeSpin(QStringLiteral("缩放 Y"), QStringLiteral("transformScaleYSpin"),
                            -1000.0, 1000.0, 0.01, 4);
    m_pivotXSpin = makeSpin(QStringLiteral("轴心 X (px)"), QStringLiteral("transformPivotXSpin"),
                            -1000000000.0, 1000000000.0, 1.0, 2);
    m_pivotYSpin = makeSpin(QStringLiteral("轴心 Y (px)"), QStringLiteral("transformPivotYSpin"),
                            -1000000000.0, 1000000000.0, 1.0, 2);
    m_opacitySpin = makeSpin(QStringLiteral("不透明度"), QStringLiteral("transformOpacitySpin"),
                             0.0, 1.0, 0.01, 3);

    bindTransform(m_xSpin, QStringLiteral("transform.x"), &model::Transform::x);
    bindTransform(m_ySpin, QStringLiteral("transform.y"), &model::Transform::y);
    bindTransform(m_rotationSpin, QStringLiteral("transform.rotation"),
                  &model::Transform::rotation);
    bindTransform(m_scaleXSpin, QStringLiteral("transform.scaleX"), &model::Transform::scaleX);
    bindTransform(m_scaleYSpin, QStringLiteral("transform.scaleY"), &model::Transform::scaleY);
    bindTransform(m_pivotXSpin, QStringLiteral("transform.pivotX"), &model::Transform::pivotX);
    bindTransform(m_pivotYSpin, QStringLiteral("transform.pivotY"), &model::Transform::pivotY);
    bindTransform(m_opacitySpin, QStringLiteral("transform.opacity"), &model::Transform::opacity);
}

void PropertiesPanel::buildContentSection()
{
    auto* group = new QGroupBox(QStringLiteral("内容"), m_content);
    m_contentGroup = group;
    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);
    m_contentLayout->addWidget(group);

    auto addPage = [this, outer](const QString& objectName) {
        auto* page = new QWidget(m_contentGroup);
        page->setObjectName(objectName);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        outer->addWidget(page);
        page->setVisible(false);
        return form;
    };

    // empty
    QFormLayout* emptyForm = addPage(QStringLiteral("emptyPage"));
    m_emptyPage = emptyForm->parentWidget();
    auto* emptyLabel = new QLabel(
        QStringLiteral("空实体没有内容；可以当父级，也可以之后挂素材或内联内容。"),
        m_emptyPage);
    emptyLabel->setWordWrap(true);
    emptyForm->addRow(emptyLabel);

    // text
    QFormLayout* textForm = addPage(QStringLiteral("textPage"));
    m_textPage = textForm->parentWidget();

    m_textEdit = new QLineEdit(m_textPage);
    m_textEdit->setObjectName(QStringLiteral("textContentEdit"));
    textForm->addRow(QStringLiteral("文本"), m_textEdit);
    addEditable(m_textEdit);
    connect(m_textEdit, &QLineEdit::editingFinished, this, [this] {
        const QString text = m_textEdit->text();
        commitEdit(QStringLiteral("content.text.text"), [text](model::Entity& entity) {
            entity.content.text.text = text;
            return true;
        });
    });

    m_textFont = new QFontComboBox(m_textPage);
    m_textFont->setObjectName(QStringLiteral("textFontCombo"));
    // Only a dropdown pick changes the font, so one pick is one commit.
    m_textFont->setEditable(false);
    textForm->addRow(QStringLiteral("字体"), m_textFont);
    addEditable(m_textFont);
    connect(m_textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        const QString family = font.family();
        commitEdit(QStringLiteral("content.text.fontFamily"), [family](model::Entity& entity) {
            entity.content.text.fontFamily = family;
            return true;
        });
    });

    m_textPixelSize = new QSpinBox(m_textPage);
    m_textPixelSize->setObjectName(QStringLiteral("textPixelSizeSpin"));
    m_textPixelSize->setRange(1, 100000);
    m_textPixelSize->setSuffix(QStringLiteral(" px"));
    m_textPixelSize->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字号"), m_textPixelSize);
    addEditable(m_textPixelSize);
    connect(m_textPixelSize, &QSpinBox::editingFinished, this, [this] {
        const int value = m_textPixelSize->value();
        commitEdit(QStringLiteral("content.text.pixelSize"), [value](model::Entity& entity) {
            entity.content.text.pixelSize = value;
            return true;
        });
    });

    m_textColorButton = new QPushButton(m_textPage);
    m_textColorButton->setObjectName(QStringLiteral("textColorButton"));
    textForm->addRow(QStringLiteral("颜色"), m_textColorButton);
    addEditable(m_textColorButton);
    connect(m_textColorButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_textColorValue, this, QStringLiteral("文字颜色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.text.color"), [color](model::Entity& entity) {
            entity.content.text.color = color;
            return true;
        });
    });

    m_textAlign = new QComboBox(m_textPage);
    m_textAlign->setObjectName(QStringLiteral("textAlignCombo"));
    for (const AlignOption& option : kAlignOptions)
        m_textAlign->addItem(QString::fromUtf8(option.label), option.value);
    textForm->addRow(QStringLiteral("对齐"), m_textAlign);
    addEditable(m_textAlign);
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_textAlign->currentData().toInt();
        commitEdit(QStringLiteral("content.text.align"), [value](model::Entity& entity) {
            entity.content.text.align = value;
            return true;
        });
    });

    m_textLineHeight = new QDoubleSpinBox(m_textPage);
    m_textLineHeight->setObjectName(QStringLiteral("textLineHeightSpin"));
    m_textLineHeight->setRange(0.01, 100.0);
    m_textLineHeight->setDecimals(3);
    m_textLineHeight->setSingleStep(0.1);
    m_textLineHeight->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("行高"), m_textLineHeight);
    addEditable(m_textLineHeight);
    connect(m_textLineHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLineHeight->value();
        commitEdit(QStringLiteral("content.text.lineHeight"), [value](model::Entity& entity) {
            entity.content.text.lineHeight = value;
            return true;
        });
    });

    m_textLetterSpacing = new QDoubleSpinBox(m_textPage);
    m_textLetterSpacing->setObjectName(QStringLiteral("textLetterSpacingSpin"));
    m_textLetterSpacing->setRange(-10000.0, 10000.0);
    m_textLetterSpacing->setDecimals(2);
    m_textLetterSpacing->setSingleStep(1.0);
    m_textLetterSpacing->setSuffix(QStringLiteral(" px"));
    m_textLetterSpacing->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("字距"), m_textLetterSpacing);
    addEditable(m_textLetterSpacing);
    connect(m_textLetterSpacing, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textLetterSpacing->value();
        commitEdit(QStringLiteral("content.text.letterSpacing"), [value](model::Entity& entity) {
            entity.content.text.letterSpacing = value;
            return true;
        });
    });

    m_textStrokeWidth = new QDoubleSpinBox(m_textPage);
    m_textStrokeWidth->setObjectName(QStringLiteral("textStrokeWidthSpin"));
    m_textStrokeWidth->setRange(0.0, 10000.0);
    m_textStrokeWidth->setDecimals(2);
    m_textStrokeWidth->setSingleStep(0.5);
    m_textStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_textStrokeWidth->setKeyboardTracking(false);
    textForm->addRow(QStringLiteral("描边宽度"), m_textStrokeWidth);
    addEditable(m_textStrokeWidth);
    connect(m_textStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_textStrokeWidth->value();
        commitEdit(QStringLiteral("content.text.strokeWidth"), [value](model::Entity& entity) {
            entity.content.text.strokeWidth = value;
            return true;
        });
    });

    // vector
    QFormLayout* vectorForm = addPage(QStringLiteral("vectorPage"));
    m_vectorPage = vectorForm->parentWidget();

    m_vectorShape = new QComboBox(m_vectorPage);
    m_vectorShape->setObjectName(QStringLiteral("vectorShapeCombo"));
    for (const ShapeOption& option : kShapeOptions)
        m_vectorShape->addItem(QString::fromUtf8(option.label), option.value);
    vectorForm->addRow(QStringLiteral("形状"), m_vectorShape);
    addEditable(m_vectorShape);
    connect(m_vectorShape, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_vectorShape->currentData().toInt();
        commitEdit(QStringLiteral("content.vector.shape"), [value](model::Entity& entity) {
            entity.content.vector.shape = value;
            return true;
        });
    });

    m_vectorWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorWidth->setObjectName(QStringLiteral("vectorWidthSpin"));
    m_vectorWidth->setRange(0.0, 1000000000.0);
    m_vectorWidth->setDecimals(2);
    m_vectorWidth->setSingleStep(1.0);
    m_vectorWidth->setSuffix(QStringLiteral(" px"));
    m_vectorWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("宽度"), m_vectorWidth);
    addEditable(m_vectorWidth);
    connect(m_vectorWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorWidth->value();
        commitEdit(QStringLiteral("content.vector.width"), [value](model::Entity& entity) {
            entity.content.vector.width = value;
            return true;
        });
    });

    m_vectorHeight = new QDoubleSpinBox(m_vectorPage);
    m_vectorHeight->setObjectName(QStringLiteral("vectorHeightSpin"));
    m_vectorHeight->setRange(0.0, 1000000000.0);
    m_vectorHeight->setDecimals(2);
    m_vectorHeight->setSingleStep(1.0);
    m_vectorHeight->setSuffix(QStringLiteral(" px"));
    m_vectorHeight->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("高度"), m_vectorHeight);
    addEditable(m_vectorHeight);
    connect(m_vectorHeight, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorHeight->value();
        commitEdit(QStringLiteral("content.vector.height"), [value](model::Entity& entity) {
            entity.content.vector.height = value;
            return true;
        });
    });

    m_vectorCorner = new QDoubleSpinBox(m_vectorPage);
    m_vectorCorner->setObjectName(QStringLiteral("vectorCornerRadiusSpin"));
    m_vectorCorner->setRange(0.0, 1000000000.0);
    m_vectorCorner->setDecimals(2);
    m_vectorCorner->setSingleStep(1.0);
    m_vectorCorner->setSuffix(QStringLiteral(" px"));
    m_vectorCorner->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("圆角"), m_vectorCorner);
    addEditable(m_vectorCorner);
    connect(m_vectorCorner, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorCorner->value();
        commitEdit(QStringLiteral("content.vector.cornerRadius"), [value](model::Entity& entity) {
            entity.content.vector.cornerRadius = value;
            return true;
        });
    });

    m_vectorFillButton = new QPushButton(m_vectorPage);
    m_vectorFillButton->setObjectName(QStringLiteral("vectorFillButton"));
    vectorForm->addRow(QStringLiteral("填充色"), m_vectorFillButton);
    addEditable(m_vectorFillButton);
    connect(m_vectorFillButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorFillValue, this, QStringLiteral("矢量填充色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.fillColor"), [color](model::Entity& entity) {
            entity.content.vector.fillColor = color;
            return true;
        });
    });

    m_vectorStrokeButton = new QPushButton(m_vectorPage);
    m_vectorStrokeButton->setObjectName(QStringLiteral("vectorStrokeColorButton"));
    vectorForm->addRow(QStringLiteral("描边色"), m_vectorStrokeButton);
    addEditable(m_vectorStrokeButton);
    connect(m_vectorStrokeButton, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(m_vectorStrokeValue, this, QStringLiteral("矢量描边色"));
        if (!chosen.isValid())
            return;
        const QString color = chosen.name(QColor::HexRgb);
        commitEdit(QStringLiteral("content.vector.strokeColor"), [color](model::Entity& entity) {
            entity.content.vector.strokeColor = color;
            return true;
        });
    });

    m_vectorStrokeWidth = new QDoubleSpinBox(m_vectorPage);
    m_vectorStrokeWidth->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
    m_vectorStrokeWidth->setRange(0.0, 1000000000.0);
    m_vectorStrokeWidth->setDecimals(2);
    m_vectorStrokeWidth->setSingleStep(0.5);
    m_vectorStrokeWidth->setSuffix(QStringLiteral(" px"));
    m_vectorStrokeWidth->setKeyboardTracking(false);
    vectorForm->addRow(QStringLiteral("描边宽度"), m_vectorStrokeWidth);
    addEditable(m_vectorStrokeWidth);
    connect(m_vectorStrokeWidth, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_vectorStrokeWidth->value();
        commitEdit(QStringLiteral("content.vector.strokeWidth"), [value](model::Entity& entity) {
            entity.content.vector.strokeWidth = value;
            return true;
        });
    });

    // asset
    QFormLayout* assetForm = addPage(QStringLiteral("assetPage"));
    m_assetPage = assetForm->parentWidget();

    m_assetHash = new QLineEdit(m_assetPage);
    m_assetHash->setObjectName(QStringLiteral("assetHashEdit"));
    m_assetHash->setReadOnly(true);
    assetForm->addRow(QStringLiteral("素材 hash"), m_assetHash);

    m_assetType = new QComboBox(m_assetPage);
    m_assetType->setObjectName(QStringLiteral("assetTypeCombo"));
    m_assetType->addItem(QStringLiteral("图片"), int(AssetType::Image));
    m_assetType->addItem(QStringLiteral("视频"), int(AssetType::Video));
    m_assetType->addItem(QStringLiteral("音频"), int(AssetType::Audio));
    m_assetType->addItem(QStringLiteral("HTML"), int(AssetType::Html));
    assetForm->addRow(QStringLiteral("素材类型"), m_assetType);
    addEditable(m_assetType);
    connect(m_assetType, &QComboBox::currentIndexChanged, this, [this](int) {
        const int value = m_assetType->currentData().toInt();
        commitEdit(QStringLiteral("content.asset.assetType"), [value](model::Entity& entity) {
            entity.content.asset.type = AssetType(value);
            return true;
        });
    });

    m_assetSourceIn = new QSpinBox(m_assetPage);
    m_assetSourceIn->setObjectName(QStringLiteral("assetSourceInSpin"));
    m_assetSourceIn->setRange(0, 1000000);
    m_assetSourceIn->setSuffix(QStringLiteral(" 帧"));
    m_assetSourceIn->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("源起点"), m_assetSourceIn);
    addEditable(m_assetSourceIn);
    connect(m_assetSourceIn, &QSpinBox::editingFinished, this, [this] {
        const int value = m_assetSourceIn->value();
        commitEdit(QStringLiteral("content.asset.sourceIn"), [value](model::Entity& entity) {
            entity.content.asset.sourceIn = value;
            return true;
        });
    });

    m_assetVolume = new QDoubleSpinBox(m_assetPage);
    m_assetVolume->setObjectName(QStringLiteral("assetVolumeSpin"));
    m_assetVolume->setRange(0.0, 1000.0);
    m_assetVolume->setDecimals(3);
    m_assetVolume->setSingleStep(0.05);
    m_assetVolume->setKeyboardTracking(false);
    assetForm->addRow(QStringLiteral("音量"), m_assetVolume);
    addEditable(m_assetVolume);
    connect(m_assetVolume, &QDoubleSpinBox::editingFinished, this, [this] {
        const double value = m_assetVolume->value();
        commitEdit(QStringLiteral("content.asset.volume"), [value](model::Entity& entity) {
            entity.content.asset.volume = value;
            return true;
        });
    });

    // camera
    QFormLayout* cameraForm = addPage(QStringLiteral("cameraPage"));
    m_cameraPage = cameraForm->parentWidget();
    auto* cameraHint = new QLabel(
        QStringLiteral("摄像机使用 transform 取景：x/y 是取景框中心，scaleX/scaleY 是画布占比"
                       "（0.05..1），rotation 是旋转。"),
        m_cameraPage);
    cameraHint->setWordWrap(true);
    cameraForm->addRow(cameraHint);
}

void PropertiesPanel::buildSolverSection()
{
    auto* group = new QGroupBox(QStringLiteral("求解段"), m_content);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    m_solverEmpty = new QLabel(QStringLiteral("（没有求解段）"), group);
    m_solverEmpty->setObjectName(QStringLiteral("solverEmptyLabel"));
    layout->addWidget(m_solverEmpty);

    m_solverHost = new QWidget(group);
    m_solverCardsLayout = new QVBoxLayout(m_solverHost);
    m_solverCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_solverCardsLayout->setSpacing(6);
    layout->addWidget(m_solverHost);

    m_contentLayout->addWidget(group);
}

void PropertiesPanel::reloadBasic(const model::Entity& entity)
{
    m_nameEdit->setText(entity.name);
    m_startSpin->setValue(entity.start);
    m_endSpin->setValue(entity.end);
    m_rowSpin->setValue(entity.row);
    m_hiddenCheck->setChecked(entity.hidden);
    m_lockedCheck->setChecked(entity.locked);
    m_visibleCheck->setChecked(entity.visible);
    m_inheritCheck->setChecked(entity.inheritVisibility);
    m_physicsCheck->setChecked(entity.physicsInteract);
}

void PropertiesPanel::reloadTransform(const model::Entity& entity)
{
    const model::Transform& transform = entity.transform;
    m_xSpin->setValue(transform.x);
    m_ySpin->setValue(transform.y);
    m_rotationSpin->setValue(transform.rotation);
    m_scaleXSpin->setValue(transform.scaleX);
    m_scaleYSpin->setValue(transform.scaleY);
    m_pivotXSpin->setValue(transform.pivotX);
    m_pivotYSpin->setValue(transform.pivotY);
    m_opacitySpin->setValue(transform.opacity);
}

void PropertiesPanel::reloadContent(const model::Entity& entity)
{
    const model::Content& content = entity.content;
    const ContentKind kind = content.kind;
    m_contentGroup->setTitle(QStringLiteral("内容 · %1").arg(contentKindLabel(kind)));

    m_emptyPage->setVisible(kind == ContentKind::Empty);
    m_textPage->setVisible(kind == ContentKind::Text);
    m_vectorPage->setVisible(kind == ContentKind::Vector);
    m_assetPage->setVisible(kind == ContentKind::Asset);
    m_cameraPage->setVisible(kind == ContentKind::Camera);

    const model::TextContent& text = content.text;
    m_textEdit->setText(text.text);
    m_textFont->setCurrentFont(text.fontFamily.isEmpty() ? QFont() : QFont(text.fontFamily));
    m_textPixelSize->setValue(text.pixelSize);
    m_textColorValue = QColor(text.color);
    styleColorButton(m_textColorButton, m_textColorValue);
    selectComboData(m_textAlign, text.align);
    m_textLineHeight->setValue(text.lineHeight);
    m_textLetterSpacing->setValue(text.letterSpacing);
    m_textStrokeWidth->setValue(text.strokeWidth);

    const model::VectorContent& vector = content.vector;
    selectComboData(m_vectorShape, vector.shape);
    m_vectorWidth->setValue(vector.width);
    m_vectorHeight->setValue(vector.height);
    m_vectorCorner->setValue(vector.cornerRadius);
    m_vectorFillValue = QColor(vector.fillColor);
    m_vectorStrokeValue = QColor(vector.strokeColor);
    styleColorButton(m_vectorFillButton, m_vectorFillValue);
    styleColorButton(m_vectorStrokeButton, m_vectorStrokeValue);
    m_vectorStrokeWidth->setValue(vector.strokeWidth);

    const model::AssetContent& asset = content.asset;
    m_assetHash->setText(asset.hash);
    selectComboData(m_assetType, int(asset.type));
    m_assetSourceIn->setValue(asset.sourceIn);
    m_assetVolume->setValue(asset.volume);
}

void PropertiesPanel::updateSolverCards(const model::Entity& entity)
{
    QVector<model::SolverSegment> solvers = entity.solvers;
    std::sort(solvers.begin(), solvers.end(),
              [](const model::SolverSegment& a, const model::SolverSegment& b) {
                  if (a.startFrame != b.startFrame)
                      return a.startFrame < b.startFrame;
                  return a.id < b.id;
              });

    bool sameStructure = solvers.size() == m_solverCards.size();
    for (int i = 0; sameStructure && i < solvers.size(); ++i)
    {
        sameStructure = m_solverCards.at(i).id == solvers.at(i).id
            && m_solverCards.at(i).effect == solvers.at(i).effect;
    }

    if (!sameStructure)
    {
        rebuildSolverCards(solvers);
    }
    else
    {
        for (int i = 0; i < solvers.size(); ++i)
            fillSolverCard(m_solverCards[i], solvers.at(i));
    }

    m_solverEmpty->setVisible(solvers.isEmpty());
}

void PropertiesPanel::rebuildSolverCards(const QVector<model::SolverSegment>& solvers)
{
    for (SolverCard& card : m_solverCards)
        delete card.box;
    m_solverCards.clear();

    for (int index = 0; index < solvers.size(); ++index)
    {
        const model::SolverSegment& solver = solvers.at(index);
        SolverCard card;
        card.id = solver.id;
        card.effect = solver.effect;
        card.box = new QGroupBox(m_solverHost);
        card.box->setObjectName(QStringLiteral("solverCard%1").arg(index));
        auto* form = new QFormLayout(card.box);
        form->setContentsMargins(8, 6, 8, 6);
        form->setVerticalSpacing(4);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        auto* effectLabel = new QLabel(solverEffectName(solver.effect), card.box);
        effectLabel->setObjectName(QStringLiteral("solverEffect%1").arg(index));
        form->addRow(QStringLiteral("效果"), effectLabel);

        card.start = new QSpinBox(card.box);
        card.start->setObjectName(QStringLiteral("solverStart%1").arg(index));
        card.start->setRange(0, 1000000);
        card.start->setKeyboardTracking(false);
        card.start->setToolTip(QStringLiteral("求解开始帧，相对实体 start；开始帧必须已有被驱动属性的关键帧。"));
        form->addRow(QStringLiteral("开始帧"), card.start);

        card.end = new QSpinBox(card.box);
        card.end->setObjectName(QStringLiteral("solverEnd%1").arg(index));
        card.end->setRange(-1, 1000000);
        card.end->setSpecialValueText(QStringLiteral("片段末帧"));
        card.end->setKeyboardTracking(false);
        card.end->setToolTip(QStringLiteral("求解结束帧是停止标记，不存值；-1 表示一直解到片段最后一帧。"));
        form->addRow(QStringLiteral("结束帧"), card.end);

        card.velocityX = new QDoubleSpinBox(card.box);
        card.velocityX->setObjectName(QStringLiteral("solverVelocityX%1").arg(index));
        card.velocityX->setRange(-1000000.0, 1000000.0);
        card.velocityX->setDecimals(1);
        card.velocityX->setSingleStep(10.0);
        card.velocityX->setSuffix(QStringLiteral(" px/s"));
        card.velocityX->setKeyboardTracking(false);
        form->addRow(QStringLiteral("初速度 X"), card.velocityX);

        card.velocityY = new QDoubleSpinBox(card.box);
        card.velocityY->setObjectName(QStringLiteral("solverVelocityY%1").arg(index));
        card.velocityY->setRange(-1000000.0, 1000000.0);
        card.velocityY->setDecimals(1);
        card.velocityY->setSingleStep(10.0);
        card.velocityY->setSuffix(QStringLiteral(" px/s"));
        card.velocityY->setKeyboardTracking(false);
        form->addRow(QStringLiteral("初速度 Y"), card.velocityY);

        const Uuid solverId = solver.id;
        connect(card.start, &QSpinBox::editingFinished, this, [this, solverId, card] {
            const int value = card.start->value();
            commitEdit(QStringLiteral("solver startFrame"), [solverId, value](model::Entity& entity) {
                for (model::SolverSegment& segment : entity.solvers)
                {
                    if (segment.id == solverId)
                    {
                        segment.startFrame = value;
                        return true;
                    }
                }
                return false;
            });
        });
        connect(card.end, &QSpinBox::editingFinished, this, [this, solverId, card] {
            const int value = card.end->value();
            commitEdit(QStringLiteral("solver endFrame"), [solverId, value](model::Entity& entity) {
                for (model::SolverSegment& segment : entity.solvers)
                {
                    if (segment.id == solverId)
                    {
                        segment.endFrame = value;
                        return true;
                    }
                }
                return false;
            });
        });
        connect(card.velocityX, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityX->value();
                    commitEdit(QStringLiteral("solver initialVelocity.x"),
                               [solverId, value](model::Entity& entity) {
                                   for (model::SolverSegment& segment : entity.solvers)
                                   {
                                       if (segment.id == solverId)
                                       {
                                           segment.initialVelocity.setX(value);
                                           return true;
                                       }
                                   }
                                   return false;
                               });
                });
        connect(card.velocityY, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityY->value();
                    commitEdit(QStringLiteral("solver initialVelocity.y"),
                               [solverId, value](model::Entity& entity) {
                                   for (model::SolverSegment& segment : entity.solvers)
                                   {
                                       if (segment.id == solverId)
                                       {
                                           segment.initialVelocity.setY(value);
                                           return true;
                                       }
                                   }
                                   return false;
                               });
                });

        const QVector<ParamSpec> specs = solverParams(solver.effect);
        for (const ParamSpec& spec : specs)
        {
            QWidget* editor = nullptr;
            if (spec.kind == ParamKind::Uuid)
            {
                auto* edit = new QLineEdit(card.box);
                edit->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                editor = edit;
                connect(edit, &QLineEdit::editingFinished, this, [this, solverId, spec, edit] {
                    const QString value = edit->text().trimmed();
                    if (value.isEmpty())
                    {
                        setStatus(QStringLiteral("%1不能为空").arg(spec.label));
                        refresh();
                        return;
                    }
                    if (m_document && !m_document->project().entities.contains(value))
                    {
                        setStatus(QStringLiteral("目标实体不存在：%1").arg(value));
                        refresh();
                        return;
                    }
                    commitEdit(QStringLiteral("solver %1").arg(spec.key),
                               [solverId, spec, value](model::Entity& entity) {
                                   for (model::SolverSegment& segment : entity.solvers)
                                   {
                                       if (segment.id == solverId)
                                       {
                                           segment.params.insert(spec.key, value);
                                           return true;
                                       }
                                   }
                                   return false;
                               });
                });
            }
            else if (spec.kind == ParamKind::Direction)
            {
                auto* combo = new QComboBox(card.box);
                combo->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                combo->addItem(QStringLiteral("顺时针 (+1)"), 1);
                combo->addItem(QStringLiteral("逆时针 (-1)"), -1);
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            const int value = combo->currentData().toInt();
                            commitEdit(QStringLiteral("solver %1").arg(spec.key),
                                       [solverId, spec, value](model::Entity& entity) {
                                           for (model::SolverSegment& segment : entity.solvers)
                                           {
                                               if (segment.id == solverId)
                                               {
                                                   segment.params.insert(spec.key, value);
                                                   return true;
                                               }
                                           }
                                           return false;
                                       });
                        });
            }
            else
            {
                auto* spin = new QDoubleSpinBox(card.box);
                spin->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                spin->setRange(spec.minimum, spec.maximum);
                spin->setDecimals(spec.decimals);
                spin->setSingleStep(spec.step);
                if (!spec.suffix.isEmpty())
                    spin->setSuffix(QStringLiteral(" ") + spec.suffix);
                spin->setKeyboardTracking(false);
                editor = spin;
                connect(spin, &QDoubleSpinBox::editingFinished, this,
                        [this, solverId, spec, spin] {
                            const double value = spin->value();
                            commitEdit(QStringLiteral("solver %1").arg(spec.key),
                                       [solverId, spec, value](model::Entity& entity) {
                                           for (model::SolverSegment& segment : entity.solvers)
                                           {
                                               if (segment.id == solverId)
                                               {
                                                   segment.params.insert(spec.key, value);
                                                   return true;
                                               }
                                           }
                                           return false;
                                       });
                        });
            }

            QString tooltip = spec.tooltip;
            if (spec.kind == ParamKind::Number)
            {
                tooltip += QStringLiteral("\n范围 %1..%2，默认 %3")
                               .arg(spec.minimum)
                               .arg(spec.maximum)
                               .arg(spec.defaultValue);
            }
            editor->setToolTip(tooltip);
            form->addRow(spec.label, editor);
            card.params.insert(spec.key, editor);
        }

        m_solverCardsLayout->addWidget(card.box);
        fillSolverCard(card, solver);
        m_solverCards.append(card);
    }

    applySolverEnabledState();
}

void PropertiesPanel::fillSolverCard(SolverCard& card, const model::SolverSegment& solver)
{
    card.box->setTitle(
        QStringLiteral("%1 · %2").arg(solverEffectName(solver.effect), solverRangeText(solver)));
    card.start->setValue(solver.startFrame);
    card.end->setValue(solver.endFrame);
    card.velocityX->setValue(solver.initialVelocity.x());
    card.velocityY->setValue(solver.initialVelocity.y());

    const bool usesVelocity =
        solver.effect == SolverEffect::Gravity || solver.effect == SolverEffect::BlackHole;
    const QString velocityTip =
        usesVelocity
            ? QStringLiteral("开始帧的求解空间初速度，单位 px/s；负值向上。")
            : QStringLiteral("orbit / spin 不使用初速度。");
    card.velocityX->setToolTip(velocityTip);
    card.velocityY->setToolTip(velocityTip);

    const QVector<ParamSpec> specs = solverParams(solver.effect);
    for (const ParamSpec& spec : specs)
    {
        QWidget* editor = card.params.value(spec.key);
        if (!editor)
            continue;

        if (auto* edit = qobject_cast<QLineEdit*>(editor))
        {
            edit->setText(solver.params.value(spec.key).toString());
        }
        else if (auto* combo = qobject_cast<QComboBox*>(editor))
        {
            const int value = solver.params.value(spec.key, int(spec.defaultValue)).toInt();
            const int comboIndex = combo->findData(value);
            combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
        }
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
        {
            spin->setValue(solver.params.value(spec.key, spec.defaultValue).toDouble());
        }
    }
}

void PropertiesPanel::applyEditingState()
{
    const bool canEdit = editable();
    for (QWidget* widget : m_editable)
        widget->setEnabled(canEdit);

    m_lockedCheck->setEnabled(selectedEntity() != nullptr);
    applySolverEnabledState();
}

void PropertiesPanel::applySolverEnabledState()
{
    const bool canEdit = editable();
    for (SolverCard& card : m_solverCards)
    {
        card.start->setEnabled(canEdit);
        card.end->setEnabled(canEdit);
        const bool usesVelocity =
            card.effect == SolverEffect::Gravity || card.effect == SolverEffect::BlackHole;
        card.velocityX->setEnabled(canEdit && usesVelocity);
        card.velocityY->setEnabled(canEdit && usesVelocity);
        for (QWidget* editor : card.params)
            editor->setEnabled(canEdit);
    }
}

model::Entity* PropertiesPanel::selectedEntity() const
{
    if (!m_document)
        return nullptr;
    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return nullptr;
    auto it = m_document->mutableProject().entities.find(uuid);
    if (it == m_document->mutableProject().entities.end())
        return nullptr;
    return &it.value();
}

bool PropertiesPanel::editable() const
{
    const model::Entity* entity = selectedEntity();
    return entity && !entity->locked;
}

void PropertiesPanel::commitEdit(const QString& what,
                                 const std::function<bool(model::Entity&)>& mutate,
                                 bool ignoreLock)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (entity->locked && !ignoreLock)
        return;

    const QJsonObject before = model::toJson(*entity);
    model::Entity updated = *entity;
    if (!mutate(updated))
    {
        refresh();
        return;
    }

    QString error;
    if (!model::validateEntity(updated, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        refresh();
        return;
    }
    if (model::toJson(updated) == before)
        return;

    const Uuid uuid = updated.uuid;
    const QString message =
        QStringLiteral("pvf: update entity \"%1\" %2").arg(updated.name, what);
    *entity = updated;

    m_committing = true;
    const bool ok = m_document->commitEntity(uuid, message, &error);
    m_committing = false;

    if (!ok)
    {
        showSaveError(error);
        refresh();
        return;
    }

    setStatus(QStringLiteral("已保存：%1").arg(what));
    refresh();
}

void PropertiesPanel::bindTransform(QDoubleSpinBox* spin, const QString& field,
                                    double model::Transform::*member)
{
    connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, field, member] {
        const double value = spin->value();
        commitEdit(field, [member, value](model::Entity& entity) {
            entity.transform.*member = value;
            return true;
        });
    });
}

void PropertiesPanel::bindCheckBox(QCheckBox* box, bool model::Entity::*member,
                                   const QString& field)
{
    connect(box, &QCheckBox::toggled, this, [this, member, field](bool checked) {
        if (m_loading)
            return;
        commitEdit(field, [member, checked](model::Entity& entity) {
            entity.*member = checked;
            return true;
        });
    });
}

void PropertiesPanel::addEditable(QWidget* widget)
{
    m_editable.append(widget);
}

void PropertiesPanel::setStatus(const QString& text)
{
    m_status->setText(text);
}

void PropertiesPanel::showSaveError(const QString& error)
{
    const QString text = error.isEmpty() ? QStringLiteral("保存失败") : error;
    setStatus(QStringLiteral("保存失败：%1").arg(text));
    QMessageBox::warning(this, QStringLiteral("保存失败"), text);
}

void PropertiesPanel::refresh()
{
    if (m_loading)
        return;

    m_loading = true;
    model::Entity* entity = selectedEntity();
    const Uuid uuid = entity ? entity->uuid : Uuid();

    if (!entity)
    {
        m_title->setText(QStringLiteral("未选择对象"));
        m_title->setToolTip(QString());
        m_placeholder->setVisible(true);
        m_scroll->setVisible(false);
    }
    else
    {
        QString title =
            QStringLiteral("%1 · %2")
                .arg(entity->name, contentKindLabel(entity->content.kind));
        if (entity->locked)
            title += QStringLiteral(" · 已锁定");
        m_title->setText(title);
        m_title->setToolTip(entity->uuid);
        m_placeholder->setVisible(false);
        m_scroll->setVisible(true);

        reloadBasic(*entity);
        reloadTransform(*entity);
        reloadContent(*entity);
        updateSolverCards(*entity);
    }

    if (uuid != m_lastUuid)
    {
        m_lastUuid = uuid;
        m_status->clear();
    }

    applyEditingState();
    m_loading = false;
}

} // namespace pvf::next
