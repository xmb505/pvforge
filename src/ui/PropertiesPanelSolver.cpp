#include "PropertiesPanel.h"

#include "CollapsibleSection.h"
#include "PropertiesPanelInternal.h"
#include "app/EditActions.h"
#include "app/EditorSession.h"
#include "model/Entity.h"
#include "next/app/ProjectDocument.h"
#include "next/properties/SolverPreviewWidget.h"
#include "ui/Viewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf {

using namespace ui::detail;

namespace {

bool solverNeedsTarget(SolverEffect effect)
{
    return effect != SolverEffect::Spin;
}

QString solverRangeText(const model::SolverSegment& solver)
{
    if (solver.endFrame < 0)
        return QStringLiteral("%1 帧起 · 自动结束").arg(solver.startFrame);
    return QStringLiteral("%1 – %2 帧").arg(solver.startFrame).arg(solver.endFrame);
}

QString solverStopReasonText(solver::StopReason reason)
{
    switch (reason)
    {
    case solver::StopReason::Collision:
        return QStringLiteral("碰撞/捕获停止");
    case solver::StopReason::StopThreshold:
        return QStringLiteral("速度低于停止阈值");
    case solver::StopReason::ClipEnd:
        return QStringLiteral("片段末尾");
    case solver::StopReason::ExplicitEnd:
        return QStringLiteral("显式覆盖");
    case solver::StopReason::None:
        break;
    }
    return QStringLiteral("无法解算");
}

QString solverStopInfoText(const model::Entity& entity,
                           const model::SolverSegment& solver,
                           const SolverPreviewEval& preview)
{
    if (!preview.error.isEmpty())
        return QStringLiteral("结束：无法解算（%1）").arg(preview.error);
    if (!preview.isValid() || preview.result.stopFrame < 0)
        return QStringLiteral("结束：无法解算");

    if (solver.endFrame >= 0)
        return QStringLiteral("结束：第 %1 帧（%2）")
            .arg(preview.result.stopFrame)
            .arg(solverStopReasonText(preview.result.reason));

    const int structural = model::solverStructuralEnd(entity, solver);
    int end = std::min(preview.result.stopFrame, structural);
    QString reason = solverStopReasonText(preview.result.reason);
    if (structural < preview.result.stopFrame)
    {
        end = structural;
        if (structural >= entity.duration() - 1)
            reason = QStringLiteral("片段末尾");
        else
        {
            bool nextSegment = false;
            for (const model::SolverSegment& other : entity.solvers)
            {
                if (other.id != solver.id
                    && other.startFrame == structural + 1)
                {
                    nextSegment = true;
                    break;
                }
            }
            reason = nextSegment ? QStringLiteral("下一段开始")
                                 : QStringLiteral("后续关键帧");
        }
    }
    return QStringLiteral("结束：自动（第 %1 帧 %2）").arg(end).arg(reason);
}

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
    ParamKind kind = ParamKind::Number;
    double defaultValue = 0.0;
    double minimum = -1000000000.0;
    double maximum = 1000000000.0;
    double step = 1.0;
    int decimals = 2;
    QString suffix;
    // Direction params differ per effect: orbit/spin use +1 = clockwise, while
    // the spiral uses +1 = counter-clockwise.
    bool ccwPositive = false;
};

ParamSpec numberParam(const QString& key, const QString& label, double defaultValue,
                      double minimum, double maximum, double step = 1.0,
                      int decimals = 2, const QString& suffix = {})
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
    spec.kind = ParamKind::Number;
    spec.defaultValue = defaultValue;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.step = step;
    spec.decimals = decimals;
    spec.suffix = suffix;
    return spec;
}

ParamSpec uuidParam(const QString& key, const QString& label)
{
    ParamSpec spec;
    spec.key = key;
    spec.label = label;
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

QVector<ParamSpec> solverParams(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("目标实体")),
            numberParam(QStringLiteral("strength"), QStringLiteral("重力强度"),
                        980.0, 0.0, 1000000.0, 10.0, 2,
                        QStringLiteral("px/s²")),
            numberParam(QStringLiteral("elasticity"), QStringLiteral("弹性"),
                        0.35, 0.0, 1.0, 0.05, 3),
            numberParam(QStringLiteral("friction"), QStringLiteral("摩擦"), 0.2, 0.0,
                        1.0, 0.05, 3),
            numberParam(QStringLiteral("stopThreshold"), QStringLiteral("停止阈值"),
                        2.0, 0.0, 1000.0, 0.5, 2, QStringLiteral("px/s")),
        };
    case SolverEffect::Orbit:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体")),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率"), 1.0, 0.01,
                        1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度"), 0.0,
                        -360.0, 360.0, 1.0, 2, QStringLiteral("°")),
            directionParam(),
            numberParam(QStringLiteral("periodFrames"), QStringLiteral("周期"), 60.0,
                        1.0, 1000000.0, 1.0, 0, QStringLiteral("帧")),
        };
    case SolverEffect::Spin:
        return {
            numberParam(QStringLiteral("angularVelocity"), QStringLiteral("角速度"),
                        6.0, 0.0, 360.0, 0.5, 2, QStringLiteral("°/帧")),
            directionParam(),
        };
    case SolverEffect::BlackHole:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("黑洞实体")),
            numberParam(QStringLiteral("simulationSpeed"), QStringLiteral("模拟速度"),
                        1.0, 0.01, 20.0, 0.1, 2),
            numberParam(QStringLiteral("mass"), QStringLiteral("质量"), 1000.0,
                        0.0001, 1000000000.0, 10.0, 2),
            numberParam(QStringLiteral("angularMomentum"), QStringLiteral("角动量"),
                        0.0, -1000000.0, 1000000.0, 10.0, 2),
            numberParam(QStringLiteral("eventHorizonRadius"),
                        QStringLiteral("视界半径"), 80.0, 0.0, 1000000.0, 1.0, 1,
                        QStringLiteral("px")),
            numberParam(QStringLiteral("objectRadius"), QStringLiteral("物体半径"),
                        40.0, 0.0, 1000000.0, 1.0, 1, QStringLiteral("px")),
            numberParam(QStringLiteral("softening"), QStringLiteral("软化半径"), 60.0,
                        0.0001, 1000000.0, 1.0, 1, QStringLiteral("px")),
            numberParam(QStringLiteral("maxSpeed"), QStringLiteral("最大速度"), 4000.0,
                        0.0001, 1000000.0, 100.0, 1, QStringLiteral("px/s")),
        };
    case SolverEffect::Spiral:
        return {
            uuidParam(QStringLiteral("targetUuid"), QStringLiteral("中心实体")),
            numberParam(QStringLiteral("turns"), QStringLiteral("圈数"), 3.0, 0.0,
                        1000.0, 0.5, 2, QStringLiteral("圈")),
            directionParam(true),
            numberParam(QStringLiteral("aspect"), QStringLiteral("扁率"), 1.0, 0.01,
                        1.0, 0.05, 3),
            numberParam(QStringLiteral("angle"), QStringLiteral("椭圆角度"), 0.0,
                        -360.0, 360.0, 1.0, 2, QStringLiteral("°")),
            numberParam(QStringLiteral("speedDegPerSec"),
                        QStringLiteral("速度（覆盖圈数）"), 0.0, 0.0, 1000000.0,
                        10.0, 1, QStringLiteral("°/s")),
        };
    }
    return {};
}

} // namespace

void PropertiesPanel::buildSolverSection(QVBoxLayout* layout)
{
    auto* content = new QWidget(this);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);

    auto* createBox = new QGroupBox(QStringLiteral("转换为求解段（快捷）"), content);
    createBox->setObjectName(QStringLiteral("solverCreateBox"));
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
    createForm->addRow(QStringLiteral("目标实体"), m_solverTargetCombo);

    m_solverEndAuto = new QLabel(
        QStringLiteral("结束：自动（后续第一个关键帧 / 物理停止 / 片段末尾）"),
        createBox);
    m_solverEndAuto->setObjectName(QStringLiteral("solverCreateEndAuto"));
    m_solverEndAuto->setWordWrap(true);
    createForm->addRow(m_solverEndAuto);

    m_solverManualEnd =
        new QCheckBox(QStringLiteral("手动指定结束帧"), createBox);
    m_solverManualEnd->setObjectName(QStringLiteral("solverCreateManualEnd"));
    m_solverManualEnd->setToolTip(
        QStringLiteral("高级覆盖：只解到指定帧，之后冻结。默认由物理停止决定。"));
    createForm->addRow(m_solverManualEnd);

    m_solverEndRow = new QWidget(createBox);
    auto* endLayout = new QHBoxLayout(m_solverEndRow);
    endLayout->setContentsMargins(0, 0, 0, 0);
    endLayout->addWidget(new QLabel(QStringLiteral("结束帧"), m_solverEndRow));
    m_solverEndSpin = new QSpinBox(m_solverEndRow);
    m_solverEndSpin->setObjectName(QStringLiteral("solverCreateEnd"));
    m_solverEndSpin->setRange(-1, 1000000);
    m_solverEndSpin->setSpecialValueText(QStringLiteral("自动"));
    m_solverEndSpin->setValue(-1);
    m_solverEndSpin->setKeyboardTracking(false);
    endLayout->addWidget(m_solverEndSpin, 1);
    m_solverEndRow->hide();
    createForm->addRow(m_solverEndRow);
    connect(m_solverManualEnd, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_solverEndRow)
            return;
        m_solverEndRow->setVisible(on);
        if (on && m_solverEndSpin->value() < 0)
        {
            const model::Entity* entity = sessionEntity();
            if (entity)
                m_solverEndSpin->setValue(std::max(0, entity->duration() - 1));
        }
    });

    m_solverCreateButton =
        new QPushButton(QStringLiteral("将当前关键帧转换为求解开始帧"), createBox);
    m_solverCreateButton->setObjectName(QStringLiteral("solverCreateButton"));
    createForm->addRow(m_solverCreateButton);
    box->addWidget(createBox);

    m_solverEmpty = new QLabel(QStringLiteral("（没有求解段）"), content);
    m_solverEmpty->setObjectName(QStringLiteral("solverEmptyLabel"));
    box->addWidget(m_solverEmpty);

    // Mini trajectory window: target point, solver point, path and the
    // initial-velocity arrow.  It never draws the entity content, so it stays
    // readable at this size.  Above the cards it is the primary handle.
    m_solverPreview = new next::SolverPreviewWidget(
        m_session ? m_session->document() : nullptr, content);
    if (m_session)
        m_solverPreview->setAnimCache(&m_session->animCache());
    m_solverPreview->setObjectName(QStringLiteral("solverPreviewMini"));
    m_solverPreview->setMinimumWidth(260);
    m_solverPreview->setFixedHeight(172);
    m_solverPreview->setToolTip(
        QStringLiteral("拖动红色箭头手柄调整初速度（松手提交，Esc 取消）"));
    m_solverPreview->hide();
    box->addWidget(m_solverPreview);

    m_solverHost = new QWidget(content);
    m_solverCardsLayout = new QVBoxLayout(m_solverHost);
    m_solverCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_solverCardsLayout->setSpacing(6);
    box->addWidget(m_solverHost);

    connect(m_solverCreateButton, &QPushButton::clicked, this,
            &PropertiesPanel::createSolverFromPlayhead);

    m_solverSection = new CollapsibleSection(QStringLiteral("求解段"), content, this);
    m_solverSection->setObjectName(QStringLiteral("solverSection"));
    m_solverSection->setExpanded(false);
    layout->insertWidget(layout->count() - 1, m_solverSection);
}

void PropertiesPanel::reloadSolverCreate(const model::Entity& entity)
{
    const Uuid previous = m_solverTargetCombo->currentData().toString();
    {
        const QSignalBlocker blocker(m_solverTargetCombo);
        m_solverTargetCombo->clear();
        m_solverTargetCombo->addItem(QStringLiteral("（选择目标实体）"),
                                     QString());
        for (auto it = m_session->project().entities.constBegin();
             it != m_session->project().entities.constEnd(); ++it)
        {
            if (it.key() == entity.uuid)
                continue;
            const QString label =
                it.value().name.isEmpty()
                    ? it.key().left(8)
                    : QStringLiteral("%1 (%2)")
                          .arg(it.value().name, it.key().left(8));
            m_solverTargetCombo->addItem(label, it.key());
        }
        const int index = m_solverTargetCombo->findData(previous);
        m_solverTargetCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    const int duration = entity.duration();
    m_solverEndSpin->setRange(-1, std::max(0, duration - 1));
    if (m_solverEndSpin->value() > duration - 1)
        m_solverEndSpin->setValue(duration - 1);
    m_solverEndRow->setVisible(m_solverManualEnd->isChecked());
    if (m_solverManualEnd->isChecked() && m_solverEndSpin->value() < 0)
        m_solverEndSpin->setValue(std::max(0, duration - 1));
    reloadSolverConvert(entity);
}

bool PropertiesPanel::canConvertCurrentKey(const model::Entity& entity,
                                           QString* reason) const
{
    const int relative = sessionRelativeFrame(entity);
    if (relative < 0 || relative >= entity.duration())
    {
        if (reason)
            *reason = QStringLiteral("播放头不在片段范围内：相对帧 %1，允许 0..%2")
                          .arg(relative)
                          .arg(entity.duration() - 1);
        return false;
    }
    if (!hasAnyKeyAt(entity, relative))
    {
        if (reason)
            *reason = QStringLiteral("当前帧没有关键帧；先在播放头处打一个普通关键帧");
        return false;
    }
    if (!model::isLastKeyframeInClip(entity, relative))
    {
        if (reason)
            *reason = QStringLiteral("后面还有关键帧，不能转换");
        return false;
    }
    for (const model::SolverSegment& solver : entity.solvers)
    {
        const int end = model::solverStructuralEnd(entity, solver);
        if (relative >= solver.startFrame && relative <= end)
        {
            if (reason)
                *reason = QStringLiteral("该关键帧已在求解段内，不能再次转换");
            return false;
        }
    }
    if (reason)
        *reason = QStringLiteral(
            "将当前关键帧作为求解开始；求解在后续第一个关键帧前一帧、"
            "物理停止或片段末尾结束");
    return true;
}

void PropertiesPanel::reloadSolverConvert(const model::Entity& entity)
{
    if (!m_convertSolverButton || !m_solverCreateButton)
        return;
    QString reason;
    const bool convert = sessionEditable()
                         && canConvertCurrentKey(entity, &reason);
    m_convertSolverButton->setEnabled(convert);
    m_solverCreateButton->setEnabled(convert);
    m_convertSolverButton->setToolTip(reason);
    m_solverCreateButton->setToolTip(reason);
    if (m_convertSolverHint)
        m_convertSolverHint->setText(reason);
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
        rebuildSolverCards(entity.uuid, solvers);
    else
        for (int i = 0; i < solvers.size(); ++i)
            fillSolverCard(m_solverCards[i], entity.uuid, solvers.at(i));

    syncSolverFocus(solvers);
    reloadSolverCreate(entity);
    updateSolverPreview(entity, solvers);
    m_solverEmpty->setVisible(solvers.isEmpty());
}

void PropertiesPanel::updateSolverPreview(
    const model::Entity& entity, const QVector<model::SolverSegment>& solvers)
{
    if (!m_solverPreview)
        return;

    // Follow the card selection; without one fall back to the segment under
    // the playhead, then to the first segment, so the window is never empty
    // while the entity has solvers.
    Uuid solverId;
    for (const model::SolverSegment& solver : solvers)
        if (solver.id == m_selectedSolverId)
            solverId = solver.id;
    const int relative = sessionRelativeFrame(entity);
    if (solverId.isEmpty())
    {
        for (const model::SolverSegment& solver : solvers)
        {
            const int end = model::solverStructuralEnd(entity, solver);
            if (relative >= solver.startFrame && relative <= end)
            {
                solverId = solver.id;
                break;
            }
        }
    }
    if (solverId.isEmpty() && !solvers.isEmpty())
        solverId = solvers.first().id;

    if (solverId.isEmpty())
    {
        m_solverPreview->clearSolver();
        m_solverPreview->hide();
        return;
    }

    // setSolver() already evaluates on a rebind; refreshPreview() covers the
    // parameter-change case with the same ids.
    const bool sameBinding = m_solverPreview->entityUuid() == entity.uuid
                             && m_solverPreview->solverId() == solverId;
    m_solverPreview->setSolver(entity.uuid, solverId);
    if (sameBinding)
        m_solverPreview->refreshPreview();
    m_solverPreview->setEnabled(sessionEditable());
    m_solverPreview->show();
}

void PropertiesPanel::syncSolverFocus(
    const QVector<model::SolverSegment>& solvers)
{
    bool found = false;
    for (const model::SolverSegment& solver : solvers)
        if (solver.id == m_selectedSolverId)
        {
            found = true;
            break;
        }
    if (!found && !m_selectedSolverId.isEmpty())
    {
        m_selectedSolverId.clear();
        emit solverSelected(m_sessionUuid, Uuid());
    }
}

void PropertiesPanel::selectSolver(const Uuid& solverId)
{
    if (m_selectedSolverId == solverId)
        return;
    m_selectedSolverId = solverId;
    emit solverSelected(m_sessionUuid, solverId);
    for (SolverCard& card : m_solverCards)
    {
        if (!card.box)
            continue;
        const QString title = card.box->title();
        const QString bare = title.startsWith(QStringLiteral("▶ "))
                                 ? title.mid(2)
                                 : title;
        card.box->setTitle(card.id == solverId
                               ? QStringLiteral("▶ ") + bare
                               : bare);
    }
    if (const model::Entity* entity = sessionEntity())
        updateSolverPreview(*entity, entity->solvers);
}

void PropertiesPanel::rebuildSolverCards(
    const Uuid& entityUuid, const QVector<model::SolverSegment>& solvers)
{
    for (SolverCard& card : m_solverCards)
    {
        if (!card.box)
            continue;
        m_solverCardsLayout->removeWidget(card.box);
        card.box->setObjectName(QString());
        for (QWidget* child : card.box->findChildren<QWidget*>())
            child->setObjectName(QString());
        card.box->hide();
        card.box->deleteLater();
    }
    m_solverCards.clear();

    for (int index = 0; index < solvers.size(); ++index)
    {
        const model::SolverSegment& solver = solvers.at(index);
        SolverCard card;
        card.id = solver.id;
        card.effect = solver.effect;
        card.box = new QGroupBox(m_solverHost);
        card.box->setObjectName(QStringLiteral("solverCard%1").arg(index));
        card.box->installEventFilter(this);
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
        form->addRow(QStringLiteral("开始帧"), card.start);

        card.endInfo = new QLabel(card.box);
        card.endInfo->setObjectName(
            QStringLiteral("solverEndInfo%1").arg(index));
        card.endInfo->setWordWrap(true);
        card.endInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(card.endInfo);

        card.manualEnd =
            new QCheckBox(QStringLiteral("手动指定结束帧"), card.box);
        card.manualEnd->setObjectName(
            QStringLiteral("solverManualEnd%1").arg(index));
        card.manualEnd->setToolTip(
            QStringLiteral("高级覆盖：只解到指定帧，之后冻结。默认由物理停止决定。"));
        form->addRow(card.manualEnd);

        card.endRow = new QWidget(card.box);
        auto* endLayout = new QHBoxLayout(card.endRow);
        endLayout->setContentsMargins(0, 0, 0, 0);
        endLayout->addWidget(new QLabel(QStringLiteral("结束帧"), card.endRow));
        card.end = new QSpinBox(card.endRow);
        card.end->setObjectName(QStringLiteral("solverEnd%1").arg(index));
        card.end->setRange(-1, 1000000);
        card.end->setSpecialValueText(QStringLiteral("自动"));
        card.end->setKeyboardTracking(false);
        endLayout->addWidget(card.end, 1);
        card.endRow->hide();
        form->addRow(card.endRow);

        auto* velocityHint =
            new QLabel(QStringLiteral("初速度（主入口：上方预览窗口的红色箭头）"),
                       card.box);
        velocityHint->setObjectName(
            QStringLiteral("solverVelocityHint%1").arg(index));
        velocityHint->setWordWrap(true);
        form->addRow(velocityHint);

        // X/Y stay as an advanced numeric fallback, collapsed by default so
        // they do not compete with the arrow handle.
        card.advanced = new QToolButton(card.box);
        card.advanced->setObjectName(
            QStringLiteral("solverAdvanced%1").arg(index));
        card.advanced->setText(QStringLiteral("高级数值（X/Y）"));
        card.advanced->setCheckable(true);
        card.advanced->setAutoRaise(true);
        form->addRow(card.advanced);

        card.velocityRow = new QWidget(card.box);
        card.velocityRow->setObjectName(
            QStringLiteral("solverVelocityRow%1").arg(index));
        auto* velocityForm = new QFormLayout(card.velocityRow);
        velocityForm->setContentsMargins(0, 0, 0, 0);
        velocityForm->setVerticalSpacing(4);
        velocityForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        card.velocityX = new QDoubleSpinBox(card.velocityRow);
        card.velocityX->setObjectName(QStringLiteral("solverVelocityX%1").arg(index));
        card.velocityX->setRange(-1000000.0, 1000000.0);
        card.velocityX->setDecimals(1);
        card.velocityX->setSingleStep(10.0);
        card.velocityX->setSuffix(QStringLiteral(" px/s"));
        card.velocityX->setKeyboardTracking(false);
        velocityForm->addRow(QStringLiteral("X"), card.velocityX);

        card.velocityY = new QDoubleSpinBox(card.velocityRow);
        card.velocityY->setObjectName(QStringLiteral("solverVelocityY%1").arg(index));
        card.velocityY->setRange(-1000000.0, 1000000.0);
        card.velocityY->setDecimals(1);
        card.velocityY->setSingleStep(10.0);
        card.velocityY->setSuffix(QStringLiteral(" px/s"));
        card.velocityY->setKeyboardTracking(false);
        velocityForm->addRow(QStringLiteral("Y"), card.velocityY);
        card.velocityRow->hide();
        form->addRow(card.velocityRow);
        connect(card.advanced, &QToolButton::toggled, card.velocityRow,
                &QWidget::setVisible);

        const Uuid solverId = solver.id;
        card.remove = new QPushButton(QStringLiteral("删除求解段"), card.box);
        card.remove->setObjectName(QStringLiteral("solverDelete%1").arg(index));
        form->addRow(card.remove);
        connect(card.remove, &QPushButton::clicked, this,
                [this, solverId] { deleteSolver(solverId); });

        connect(card.start, &QSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const int value = card.start->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver start"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.startFrame = value;
                                     return true;
                                 });
                });
        connect(card.end, &QSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const int value = card.end->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver end"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.endFrame = value;
                                     return true;
                                 });
                });
        connect(card.manualEnd, &QCheckBox::toggled, this,
                [this, solverId, card](bool on) {
                    if (m_editing)
                        return;
                    const int value = std::max(0, card.end->value());
                    updateSolver(
                        solverId,
                        QStringLiteral("pvf: edit solver end mode"),
                        [solverId, on, value](model::SolverSegment& s) {
                            if (s.id != solverId)
                                return false;
                            s.endFrame = on ? std::max(s.startFrame, value) : -1;
                            return true;
                        });
                });
        connect(card.velocityX, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityX->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver velocity"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.initialVelocity.setX(value);
                                     return true;
                                 });
                });
        connect(card.velocityY, &QDoubleSpinBox::editingFinished, this,
                [this, solverId, card] {
                    const double value = card.velocityY->value();
                    updateSolver(solverId, QStringLiteral("pvf: edit solver velocity"),
                                 [solverId, value](model::SolverSegment& s) {
                                     if (s.id != solverId)
                                         return false;
                                     s.initialVelocity.setY(value);
                                     return true;
                                 });
                });

        const QVector<ParamSpec> specs = solverParams(solver.effect);
        for (const ParamSpec& spec : specs)
        {
            QWidget* editor = nullptr;
            if (spec.kind == ParamKind::Uuid)
            {
                auto* combo = new QComboBox(card.box);
                combo->setObjectName(
                    QStringLiteral("solverParam%1_%2").arg(index).arg(spec.key));
                combo->addItem(QStringLiteral("（选择目标实体）"), QString());
                for (auto it = m_session->project().entities.constBegin();
                     it != m_session->project().entities.constEnd(); ++it)
                {
                    if (it.key() == entityUuid)
                        continue;
                    const QString label =
                        it.value().name.isEmpty()
                            ? it.key().left(8)
                            : QStringLiteral("%1 (%2)")
                                  .arg(it.value().name, it.key().left(8));
                    combo->addItem(label, it.key());
                }
                editor = combo;
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, solverId, spec, combo](int) {
                            if (m_editing)
                                return;
                            const QString value = combo->currentData().toString();
                            if (value.isEmpty())
                                return;
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
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
                            if (m_editing)
                                return;
                            const int value = combo->currentData().toInt();
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
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
                            updateSolver(
                                solverId,
                                QStringLiteral("pvf: edit solver %1").arg(spec.key),
                                [solverId, spec, value](model::SolverSegment& s) {
                                    if (s.id != solverId)
                                        return false;
                                    s.params.insert(spec.key, value);
                                    return true;
                                });
                        });
            }
            form->addRow(spec.label, editor);
            card.params.insert(spec.key, editor);
        }

        for (QWidget* child : card.box->findChildren<QWidget*>())
            child->installEventFilter(this);
        m_solverCardsLayout->addWidget(card.box);
        fillSolverCard(card, entityUuid, solver);
        m_solverCards.append(card);
    }
}

void PropertiesPanel::fillSolverCard(SolverCard& card, const Uuid& entityUuid,
                                     const model::SolverSegment& solver)
{
    const QString title = QStringLiteral("%1 · %2")
                              .arg(solverEffectName(solver.effect),
                                   solverRangeText(solver));
    card.box->setTitle(card.id == m_selectedSolverId
                           ? QStringLiteral("▶ ") + title
                           : title);
    setSpinIfIdle(card.start, solver.startFrame);

    SolverPreviewEval preview;
    const model::Entity* entity = nullptr;
    if (m_session)
    {
        const auto it = m_session->project().entities.constFind(entityUuid);
        if (it != m_session->project().entities.constEnd())
        {
            entity = &it.value();
            // The card's end frame reads the same session cache as the
            // timeline and the mini preview.  A cold cache is solved once
            // here while paused; playback never reaches this path.
            if (const solver::Result* cached =
                    m_session->solverResultFor(entityUuid, solver.id))
                preview.result = *cached;
            else
                preview.error = QStringLiteral("求解缓存未就绪");
        }
    }
    if (card.endInfo)
    {
        card.endInfo->setText(
            entity ? solverStopInfoText(*entity, solver, preview)
                   : QStringLiteral("结束：无法解算"));
        if (!preview.error.isEmpty())
            card.endInfo->setToolTip(preview.error);
        else
            card.endInfo->setToolTip(
                QStringLiteral("自动结束由后续第一个关键帧、物理停止或片段"
                               "末尾决定；勾选下方高级选项可手动覆盖。"));
    }

    const int duration = entity ? entity->duration() : 1;
    int effectiveEnd = solver.endFrame >= 0
                           ? solver.endFrame
                           : std::max(0, duration - 1);
    if (entity && solver.endFrame < 0)
    {
        effectiveEnd = model::solverStructuralEnd(*entity, solver);
        if (preview.result.stopFrame >= 0)
            effectiveEnd = std::min(effectiveEnd, preview.result.stopFrame);
    }
    const int suggestedEnd = std::max(solver.startFrame, effectiveEnd);
    if (card.manualEnd)
    {
        const QSignalBlocker blocker(card.manualEnd);
        card.manualEnd->setChecked(solver.endFrame >= 0);
    }
    if (card.endRow)
        card.endRow->setVisible(solver.endFrame >= 0);
    if (card.end)
    {
        card.end->setRange(std::max(0, solver.startFrame),
                           std::max(0, duration - 1));
        setSpinIfIdle(card.end, suggestedEnd);
    }
    setSpinIfIdle(card.velocityX, solver.initialVelocity.x());
    setSpinIfIdle(card.velocityY, solver.initialVelocity.y());

    const bool usesVelocity = solver.effect == SolverEffect::Gravity
        || solver.effect == SolverEffect::BlackHole;
    card.velocityX->setEnabled(usesVelocity);
    card.velocityY->setEnabled(usesVelocity);

    const QVector<ParamSpec> specs = solverParams(solver.effect);
    for (const ParamSpec& spec : specs)
    {
        QWidget* editor = card.params.value(spec.key);
        if (!editor)
            continue;
        if (auto* combo = qobject_cast<QComboBox*>(editor))
        {
            const QSignalBlocker blocker(combo);
            if (spec.kind == ParamKind::Uuid)
            {
                const QString value = solver.params.value(spec.key).toString();
                int index = combo->findData(value);
                combo->setCurrentIndex(index >= 0 ? index : 0);
            }
            else
            {
                const int value =
                    solver.params.value(spec.key, int(spec.defaultValue)).toInt();
                int index = combo->findData(value);
                combo->setCurrentIndex(index >= 0 ? index : 0);
            }
        }
        else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor))
        {
            setSpinIfIdle(spin,
                          solver.params.value(spec.key, spec.defaultValue).toDouble());
        }
    }
}

void PropertiesPanel::showSolverConvertMenu()
{
    if (!m_convertSolverButton || !m_convertSolverButton->isEnabled())
        return;

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("solverConvertMenu"));
    const QVector<QPair<SolverEffect, QString>> effects = {
        {SolverEffect::Gravity, QStringLiteral("重力")},
        {SolverEffect::Orbit, QStringLiteral("公转")},
        {SolverEffect::Spin, QStringLiteral("自转")},
        {SolverEffect::BlackHole, QStringLiteral("黑洞")},
        {SolverEffect::Spiral, QStringLiteral("螺旋")},
    };
    for (const auto& item : effects)
    {
        QAction* action = menu.addAction(item.second);
        action->setData(int(item.first));
    }
    QAction* chosen = menu.exec(m_convertSolverButton->mapToGlobal(
        QPoint(0, m_convertSolverButton->height())));
    if (chosen)
        convertSolverFromKey(SolverEffect(chosen->data().toInt()));
}

void PropertiesPanel::createSolverFromPlayhead()
{
    if (!m_solverEffectCombo)
        return;
    convertSolverFromKey(
        SolverEffect(m_solverEffectCombo->currentData().toInt()));
}

void PropertiesPanel::convertSolverFromKey(SolverEffect effect)
{
    const model::Entity* entity = sessionEntity();
    if (!entity || !sessionEditable())
        return;

    QString reason;
    if (!canConvertCurrentKey(*entity, &reason))
    {
        reportSessionError(reason);
        return;
    }

    const int relative = sessionRelativeFrame(*entity);
    const int duration = entity->duration();
    const Uuid entityUuid = entity->uuid;

    Uuid target;
    if (solverNeedsTarget(effect))
    {
        target = m_solverTargetCombo->currentData().toString();
        if (target.isEmpty() || target == entityUuid
            || !m_session->project().entities.contains(target))
        {
            reportSessionError(
                QStringLiteral("请先在下方“求解段”区选择有效的目标实体（不能是自己）"));
            return;
        }
    }

    int endFrame = m_solverManualEnd && m_solverManualEnd->isChecked()
                       ? m_solverEndSpin->value()
                       : -1;
    if (endFrame < 0)
        endFrame = -1; // automatic: next key / physical stop / clip end
    if (endFrame >= 0 && (endFrame < relative || endFrame >= duration))
    {
        reportSessionError(QStringLiteral("结束帧必须位于 %1..%2（或使用自动结束）")
                               .arg(relative)
                               .arg(duration - 1));
        return;
    }

    model::SolverSegment segment;
    segment.id = model::newUuid();
    segment.effect = effect;
    segment.startFrame = relative;
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
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: convert keyframe to solver start"),
        [segment](model::Entity& e) {
            e.solvers.append(segment);
            return true;
        });
    if (ok)
    {
        selectSolver(solverId);
        if (m_solverManualEnd)
        {
            const QSignalBlocker blocker(m_solverManualEnd);
            m_solverManualEnd->setChecked(false);
        }
        m_solverEndSpin->setValue(-1);
        m_solverEndRow->hide();
    }
    refresh();
}

void PropertiesPanel::updateSolver(
    const Uuid& solverId, const QString& message,
    const std::function<bool(model::SolverSegment&)>& mutate)
{
    if (!m_session || m_sessionUuid.isEmpty())
        return;
    m_editing = true;
    QString error;
    const bool ok = m_session->editActions()->updateEntity(
        m_sessionUuid, message,
        [solverId, &mutate](model::Entity& entity) {
            for (model::SolverSegment& segment : entity.solvers)
                if (segment.id == solverId)
                    return mutate(segment);
            return false;
        },
        &error);
    m_editing = false;
    if (!ok)
        reportSessionError(error);
    refresh();
}

void PropertiesPanel::deleteSolver(const Uuid& solverId)
{
    const bool ok = runSessionEdit(
        QStringLiteral("pvf: delete solver"),
        [solverId](model::Entity& entity) {
            for (int i = 0; i < entity.solvers.size(); ++i)
            {
                if (entity.solvers.at(i).id == solverId)
                {
                    entity.solvers.removeAt(i);
                    return true;
                }
            }
            return false;
        });
    if (ok && m_selectedSolverId == solverId)
        selectSolver(Uuid());
}

} // namespace pvf
