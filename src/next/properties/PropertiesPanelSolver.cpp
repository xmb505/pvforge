#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "SolverPreviewWidget.h"
#include "common/Types.h"
#include "next/core/EntityLock.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

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
    // The spiral numbers its direction the other way round: +1 is
    // counter-clockwise there, while orbit/spin keep +1 = clockwise.
    bool ccwPositive = false;
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

ParamSpec directionParam(bool ccwPositive = false)
{
    ParamSpec spec;
    spec.key = QStringLiteral("direction");
    spec.label = QStringLiteral("方向");
    spec.kind = ParamKind::Direction;
    spec.defaultValue = 1.0;
    spec.ccwPositive = ccwPositive;
    return spec;
}

// Labels and tooltips mirror docs/AI/MCP_TOOLS.md 5.2; they are shown next to each
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
    case SolverEffect::Spiral:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体"),
                      QStringLiteral("螺旋中心实体。每帧读取它当前位置，圆心跟着它移动；不能是目标实体自己。")),
            numberParam(QStringLiteral("turns"), QStringLiteral("圈数"),
                        QStringLiteral("从起点绕到终点一共转多少圈。速度 > 0 时按速度覆盖。"),
                        3.0, 0.0, 1000.0, 0.5, 2),
            directionParam(true),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率 (短/长轴)"),
                        QStringLiteral("螺旋轨道的短轴/长轴。1 = 正圆；越小轨迹越扁。"),
                        1.0, 0.01, 1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度 (°)"),
                        QStringLiteral("椭圆长轴相对中心实体局部坐标的旋转角。"),
                        0.0, -360.0, 360.0, 1.0, 2),
            numberParam(QStringLiteral("speedDegPerSec"),
                        QStringLiteral("速度 (°/s，覆盖圈数)"),
                        QStringLiteral("大于 0 时按该角速度换算总角度，忽略圈数；0 = 按圈数在整段内均分。"),
                        0.0, 0.0, 1000000.0, 10.0, 1),
        };
    }
    return {};
}

QString solverRangeText(const model::SolverSegment& solver)
{
    if (solver.endFrame < 0)
        return QStringLiteral("%1 帧 – 片段末帧").arg(solver.startFrame);
    return QStringLiteral("%1 – %2 帧").arg(solver.startFrame).arg(solver.endFrame);
}

bool solverNeedsTarget(SolverEffect effect)
{
    return effect != SolverEffect::Spin;
}

int solverEffectiveEnd(const model::SolverSegment& solver, int duration)
{
    return solver.endFrame < 0 ? duration - 1 : solver.endFrame;
}

QString solverConflictText(const model::SolverSegment& solver, int duration)
{
    return QStringLiteral("%1–%2（%3，%4）")
        .arg(solver.startFrame)
        .arg(solverEffectiveEnd(solver, duration))
        .arg(solverEffectName(solver.effect))
        .arg(solver.id.left(8));
}

} // namespace

void PropertiesPanel::buildSolverSection()
{
    auto* group = new QGroupBox(QStringLiteral("求解段"), m_content);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto* createBox = new QGroupBox(QStringLiteral("新建求解段"), group);
    auto* createForm = new QFormLayout(createBox);
    createForm->setContentsMargins(8, 6, 8, 6);
    createForm->setVerticalSpacing(4);
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_solverEffectCombo = new QComboBox(createBox);
    m_solverEffectCombo->setObjectName(QStringLiteral("solverEffectCombo"));
    m_solverEffectCombo->addItem(QStringLiteral("重力"), int(SolverEffect::Gravity));
    m_solverEffectCombo->addItem(QStringLiteral("公转"), int(SolverEffect::Orbit));
    m_solverEffectCombo->addItem(QStringLiteral("自转"), int(SolverEffect::Spin));
    m_solverEffectCombo->addItem(QStringLiteral("黑洞"), int(SolverEffect::BlackHole));
    m_solverEffectCombo->addItem(QStringLiteral("螺旋"), int(SolverEffect::Spiral));
    createForm->addRow(QStringLiteral("效果"), m_solverEffectCombo);

    m_solverTargetCombo = new QComboBox(createBox);
    m_solverTargetCombo->setObjectName(QStringLiteral("solverTargetCombo"));
    m_solverTargetCombo->setToolTip(
        QStringLiteral("重力地面、公转中心、黑洞或螺旋中心实体；不能是当前实体自己。自转不需要目标。"));
    createForm->addRow(QStringLiteral("目标实体"), m_solverTargetCombo);

    m_solverEndSpin = new QSpinBox(createBox);
    m_solverEndSpin->setObjectName(QStringLiteral("solverCreateEnd"));
    m_solverEndSpin->setRange(-1, 1000000);
    m_solverEndSpin->setSpecialValueText(QStringLiteral("片段末帧"));
    m_solverEndSpin->setValue(-1);
    m_solverEndSpin->setKeyboardTracking(false);
    m_solverEndSpin->setToolTip(
        QStringLiteral("求解结束帧是停止标记，不存值；-1 表示一直解到片段最后一帧。"));
    createForm->addRow(QStringLiteral("结束帧"), m_solverEndSpin);

    m_solverCreateButton = new QPushButton(
        QStringLiteral("将当前关键帧转换为求解开始帧"), createBox);
    m_solverCreateButton->setObjectName(QStringLiteral("solverCreateButton"));
    m_solverCreateButton->setToolTip(
        QStringLiteral("开始帧取播放头相对实体 start 的帧号；该帧必须已有普通关键帧。"));
    createForm->addRow(m_solverCreateButton);

    layout->addWidget(createBox);

    m_solverEmpty = new QLabel(QStringLiteral("（没有求解段）"), group);
    m_solverEmpty->setObjectName(QStringLiteral("solverEmptyLabel"));
    layout->addWidget(m_solverEmpty);

    m_solverHost = new QWidget(group);
    m_solverCardsLayout = new QVBoxLayout(m_solverHost);
    m_solverCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_solverCardsLayout->setSpacing(6);
    layout->addWidget(m_solverHost);

    m_contentLayout->addWidget(group);

    connect(m_solverEffectCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applySolverEnabledState();
    });
    connect(m_solverCreateButton, &QPushButton::clicked, this,
            &PropertiesPanel::createSolverFromPlayhead);
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
        rebuildSolverCards(entity.uuid, solvers);
    }
    else
    {
        for (int i = 0; i < solvers.size(); ++i)
            fillSolverCard(m_solverCards[i], entity.uuid, solvers.at(i));
    }

    reloadSolverCreate(entity);
    applySolverCardSelection();
    m_solverEmpty->setVisible(solvers.isEmpty());
}

void PropertiesPanel::rebuildSolverCards(
    const Uuid& entityUuid, const QVector<model::SolverSegment>& solvers)
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

        card.preview = new SolverPreviewWidget(m_document, card.box);
        card.preview->setObjectName(QStringLiteral("solverPreview%1").arg(index));
        card.preview->setMinimumHeight(180);
        form->addRow(card.preview);

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
        card.remove = new QPushButton(QStringLiteral("删除求解段"), card.box);
        card.remove->setObjectName(QStringLiteral("solverDelete%1").arg(index));
        card.remove->setToolTip(
            QStringLiteral("删除这个求解段；开始帧的普通关键帧会保留。"));
        form->addRow(card.remove);
        connect(card.remove, &QPushButton::clicked, this, [this, solverId] {
            deleteSolverSegment(solverId);
        });

        connect(card.start, &QSpinBox::editingFinished, this, [this, solverId, card] {
            const int value = card.start->value();
            commitSolverEdit(
                solverId, QStringLiteral("solver startFrame"),
                [solverId, value](model::Entity& entity) {
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
            commitSolverEdit(
                solverId, QStringLiteral("solver endFrame"),
                [solverId, value](model::Entity& entity) {
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
                    commitSolverEdit(
                        solverId, QStringLiteral("solver initialVelocity.x"),
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
                    commitSolverEdit(
                        solverId, QStringLiteral("solver initialVelocity.y"),
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
                connect(edit, &QLineEdit::editingFinished, this,
                        [this, solverId, spec, edit] {
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
                    commitSolverEdit(
                        solverId, QStringLiteral("solver %1").arg(spec.key),
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
                combo->addItem(spec.ccwPositive ? QStringLiteral("逆时针 (+1)")
                                                : QStringLiteral("顺时针 (+1)"),
                               1);
                combo->addItem(spec.ccwPositive ? QStringLiteral("顺时针 (-1)")
                                                : QStringLiteral("逆时针 (-1)"),
                               -1);
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            const int value = combo->currentData().toInt();
                            commitSolverEdit(
                                solverId,
                                QStringLiteral("solver %1").arg(spec.key),
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
                            commitSolverEdit(
                                solverId,
                                QStringLiteral("solver %1").arg(spec.key),
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
        fillSolverCard(card, entityUuid, solver);
        m_solverCards.append(card);
    }

    applySolverEnabledState();
}

void PropertiesPanel::fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                                     const model::SolverSegment& solver)
{
    card.box->setTitle(
        QStringLiteral("%1 · %2").arg(solverEffectName(solver.effect), solverRangeText(solver)));
    if (card.preview)
        card.preview->setSolver(entityUuid, solver.id);
    card.start->setValue(solver.startFrame);
    card.end->setValue(solver.endFrame);
    card.velocityX->setValue(solver.initialVelocity.x());
    card.velocityY->setValue(solver.initialVelocity.y());

    const bool usesVelocity =
        solver.effect == SolverEffect::Gravity || solver.effect == SolverEffect::BlackHole;
    const QString velocityTip =
        usesVelocity
            ? QStringLiteral("开始帧的求解空间初速度，单位 px/s；负值向上。")
            : QStringLiteral("orbit / spin / spiral 不使用初速度。");
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

void PropertiesPanel::reloadSolverCreate(const model::Entity& entity)
{
    const Uuid previous = m_solverTargetCombo->currentData().toString();
    QSignalBlocker blocker(m_solverTargetCombo);
    m_solverTargetCombo->clear();
    m_solverTargetCombo->addItem(QStringLiteral("（选择目标实体）"), QString());

    QVector<const model::Entity*> others;
    for (auto it = m_document->project().entities.constBegin();
         it != m_document->project().entities.constEnd(); ++it)
    {
        if (it.key() != entity.uuid)
            others.append(&it.value());
    }
    std::sort(others.begin(), others.end(),
              [](const model::Entity* a, const model::Entity* b) {
                  if (a->name != b->name)
                      return a->name < b->name;
                  return a->uuid < b->uuid;
              });
    for (const model::Entity* other : others)
    {
        const QString label = other->name.isEmpty()
            ? other->uuid.left(8)
            : QStringLiteral("%1 (%2)").arg(other->name, other->uuid.left(8));
        m_solverTargetCombo->addItem(label, other->uuid);
    }

    const int previousIndex = m_solverTargetCombo->findData(previous);
    m_solverTargetCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);

    const int duration = entity.duration();
    m_solverEndSpin->setRange(-1, qMax(0, duration - 1));
    if (m_solverEndSpin->value() > duration - 1)
        m_solverEndSpin->setValue(duration - 1);
}

void PropertiesPanel::applySolverCardSelection()
{
    bool found = false;
    for (SolverCard& card : m_solverCards)
    {
        const bool selected = !m_selectedSolverId.isEmpty()
            && card.id == m_selectedSolverId;
        card.box->setStyleSheet(selected
                                    ? QStringLiteral("QGroupBox { border: 2px solid #ffbe3c; }")
                                    : QString());
        if (selected)
        {
            found = true;
            m_scroll->ensureWidgetVisible(card.box);
        }
    }
    if (!found)
        m_selectedSolverId.clear();
}

SolverPreviewWidget* PropertiesPanel::solverPreview(const Uuid& solverId) const
{
    for (const SolverCard& card : m_solverCards)
    {
        if (card.id == solverId)
            return card.preview;
    }
    return nullptr;
}

void PropertiesPanel::commitSolverEdit(
    const Uuid& solverId, const QString& what,
    const std::function<bool(model::Entity&)>& mutate)
{
    if (!commitEdit(what, mutate))
        return;
    if (SolverPreviewWidget* preview = solverPreview(solverId))
        preview->refreshPreview();
}

SolverEffect PropertiesPanel::selectedSolverEffect() const
{
    return SolverEffect(m_solverEffectCombo->currentData().toInt());
}

void PropertiesPanel::createSolverFromPlayhead()
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能创建求解段"));
        return;
    }

    const int relativeFrame = m_document->playhead() - entity->start;
    const int duration = entity->duration();
    if (relativeFrame < 0 || relativeFrame >= duration)
    {
        setStatus(QStringLiteral("播放头不在实体范围内：相对帧 %1，允许 0..%2")
                      .arg(relativeFrame)
                      .arg(duration - 1));
        return;
    }

    const SolverEffect effect = selectedSolverEffect();
    const QStringList drivenTracks = detail::solverDrivenTracks(effect);
    bool hasStartKey = true;
    for (const QString& trackName : drivenTracks)
        hasStartKey = hasStartKey && detail::hasKeyAt(*entity, trackName, relativeFrame);
    if (!hasStartKey)
    {
        setStatus(QStringLiteral("请先在播放头处给 X/Y（或旋转）打普通关键帧"));
        return;
    }

    const Uuid self = entity->uuid;
    Uuid target;
    if (solverNeedsTarget(effect))
    {
        target = m_solverTargetCombo->currentData().toString();
        if (target.isEmpty())
        {
            setStatus(QStringLiteral("请先选择目标实体"));
            return;
        }
        if (target == self)
        {
            setStatus(QStringLiteral("目标实体不能是自己"));
            return;
        }
        if (!m_document->project().entities.contains(target))
        {
            setStatus(QStringLiteral("目标实体不存在：%1").arg(target));
            return;
        }
    }

    const int endFrame = m_solverEndSpin->value();
    if (endFrame != -1 && (endFrame < relativeFrame || endFrame >= duration))
    {
        setStatus(QStringLiteral("结束帧必须为 -1 或位于 %1..%2")
                      .arg(relativeFrame)
                      .arg(duration - 1));
        return;
    }
    const int effectiveEnd = endFrame < 0 ? duration - 1 : endFrame;

    QStringList conflicts;
    for (const model::SolverSegment& existing : entity->solvers)
    {
        const int existingEnd = solverEffectiveEnd(existing, duration);
        if (relativeFrame <= existingEnd && existing.startFrame <= effectiveEnd)
            conflicts.append(solverConflictText(existing, duration));
    }
    if (!conflicts.isEmpty())
    {
        setStatus(QStringLiteral("新求解段与已有求解段重叠：%1")
                      .arg(conflicts.join(QStringLiteral("、"))));
        return;
    }

    QStringList innerKeys;
    for (const QString& trackName : drivenTracks)
    {
        const auto track = entity->tracks.constFind(trackName);
        if (track == entity->tracks.constEnd())
            continue;
        for (const model::Keyframe& keyframe : track.value().keys)
        {
            if (keyframe.frame > relativeFrame && keyframe.frame <= effectiveEnd)
            {
                innerKeys.append(
                    QStringLiteral("%1@%2").arg(trackName).arg(keyframe.frame));
            }
        }
    }
    if (!innerKeys.isEmpty())
    {
        setStatus(QStringLiteral("求解段内不能有普通关键帧：%1")
                      .arg(innerKeys.join(QStringLiteral("、"))));
        return;
    }

    model::SolverSegment segment;
    segment.id = model::newUuid();
    segment.effect = effect;
    segment.startFrame = relativeFrame;
    segment.endFrame = endFrame;
    segment.version = 1;
    segment.initialVelocity = QPointF(0.0, 0.0);

    switch (effect)
    {
    case SolverEffect::Gravity:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        break;
    case SolverEffect::Orbit:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("aspect"), 1.0);
        segment.params.insert(QStringLiteral("angle"), 0.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        segment.params.insert(QStringLiteral("periodFrames"), 60);
        break;
    case SolverEffect::Spin:
        segment.params.insert(QStringLiteral("angularVelocity"), 6.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        break;
    case SolverEffect::BlackHole:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("simulationSpeed"), 1.0);
        segment.params.insert(QStringLiteral("mass"), 1000.0);
        segment.params.insert(QStringLiteral("angularMomentum"), 0.0);
        segment.params.insert(QStringLiteral("eventHorizonRadius"), 80.0);
        segment.params.insert(QStringLiteral("objectRadius"), 40.0);
        segment.params.insert(QStringLiteral("softening"), 60.0);
        segment.params.insert(QStringLiteral("maxSpeed"), 4000.0);
        break;
    case SolverEffect::Spiral:
        segment.params.insert(QStringLiteral("targetUuid"), target);
        segment.params.insert(QStringLiteral("turns"), 3.0);
        segment.params.insert(QStringLiteral("direction"), 1);
        segment.params.insert(QStringLiteral("aspect"), 1.0);
        segment.params.insert(QStringLiteral("angle"), 0.0);
        segment.params.insert(QStringLiteral("speedDegPerSec"), 0.0);
        break;
    }

    const Uuid solverId = segment.id;
    entity->solvers.append(segment);

    QString error;
    m_committing = true;
    const bool ok = m_document->commitEntity(
        self, QStringLiteral("pvf: convert keyframe to solver start"), &error);
    m_committing = false;

    if (!ok)
    {
        auto it = m_document->mutableProject().entities.find(self);
        if (it != m_document->mutableProject().entities.end())
        {
            QVector<model::SolverSegment>& solvers = it.value().solvers;
            for (int i = 0; i < solvers.size(); ++i)
            {
                if (solvers.at(i).id == solverId)
                {
                    solvers.removeAt(i);
                    break;
                }
            }
        }
        setStatus(QStringLiteral("创建求解段失败：%1")
                      .arg(error.isEmpty() ? QStringLiteral("保存失败") : error));
        refresh();
        return;
    }

    m_selectedSolverId = solverId;
    setStatus(QStringLiteral("已创建求解段：%1（%2–%3）")
                  .arg(solverEffectName(effect))
                  .arg(relativeFrame)
                  .arg(effectiveEnd));
    refresh();
}

void PropertiesPanel::deleteSolverSegment(const Uuid& solverId)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能删除求解段"));
        return;
    }

    int index = -1;
    for (int i = 0; i < entity->solvers.size(); ++i)
    {
        if (entity->solvers.at(i).id == solverId)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const Uuid self = entity->uuid;
    const model::SolverSegment removed = entity->solvers.at(index);
    entity->solvers.removeAt(index);

    QString error;
    m_committing = true;
    const bool ok = m_document->commitEntity(
        self,
        QStringLiteral("pvf: delete solver \"%1\" %2")
            .arg(entity->name, solverEffectName(removed.effect)),
        &error);
    m_committing = false;

    if (!ok)
    {
        auto it = m_document->mutableProject().entities.find(self);
        if (it != m_document->mutableProject().entities.end())
        {
            QVector<model::SolverSegment>& solvers = it.value().solvers;
            solvers.insert(qMin(index, solvers.size()), removed);
        }
        setStatus(QStringLiteral("删除求解段失败：%1")
                      .arg(error.isEmpty() ? QStringLiteral("保存失败") : error));
        refresh();
        return;
    }

    if (m_selectedSolverId == solverId)
        m_selectedSolverId.clear();
    setStatus(QStringLiteral("已删除求解段"));
    refresh();
}

void PropertiesPanel::applySolverEnabledState()
{
    const bool canEdit = editable();
    const bool needsTarget = solverNeedsTarget(selectedSolverEffect());
    m_solverEffectCombo->setEnabled(canEdit);
    m_solverTargetCombo->setEnabled(canEdit && needsTarget);
    m_solverEndSpin->setEnabled(canEdit);
    m_solverCreateButton->setEnabled(canEdit);

    for (SolverCard& card : m_solverCards)
    {
        card.start->setEnabled(canEdit);
        card.end->setEnabled(canEdit);
        const bool usesVelocity =
            card.effect == SolverEffect::Gravity || card.effect == SolverEffect::BlackHole;
        card.velocityX->setEnabled(canEdit && usesVelocity);
        card.velocityY->setEnabled(canEdit && usesVelocity);
        card.remove->setEnabled(canEdit);
        for (QWidget* editor : card.params)
            editor->setEnabled(canEdit);
    }
}

} // namespace pvf::next
