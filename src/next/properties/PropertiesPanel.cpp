#include "PropertiesPanel.h"

#include "PropertiesPanelInternal.h"
#include "next/core/EntityLock.h"
#include "next/timeline/EntityRange.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace pvf::next {

namespace {

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
        connect(m_document, &Document::playheadChanged, this, &PropertiesPanel::refresh);
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
    buildKeyframeSection();
    buildContentSection();
    buildHtmlVariablesSection();
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
        if (m_loading)
            return;
        const model::Entity* entity = selectedEntity();
        if (!entity)
            return;
        applyRangeEdit(m_startSpin->value(), entity->end,
                       QStringLiteral("start"));
    });

    m_endSpin = new QSpinBox(m_content);
    m_endSpin->setObjectName(QStringLiteral("endSpin"));
    m_endSpin->setRange(1, 1000000);
    m_endSpin->setKeyboardTracking(false);
    form->addRow(QStringLiteral("结束帧"), m_endSpin);
    addEditable(m_endSpin);
    connect(m_endSpin, &QSpinBox::editingFinished, this, [this] {
        if (m_loading)
            return;
        const model::Entity* entity = selectedEntity();
        if (!entity)
            return;
        applyRangeEdit(entity->start, m_endSpin->value(),
                       QStringLiteral("end"));
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

void PropertiesPanel::setAutoKey(bool on)
{
    m_autoKey = on;
    if (m_autoKeyCheck && m_autoKeyCheck->isChecked() != on)
    {
        QSignalBlocker blocker(m_autoKeyCheck);
        m_autoKeyCheck->setChecked(on);
    }
}

bool PropertiesPanel::autoKey() const
{
    return m_autoKey;
}

void PropertiesPanel::applyEditingState()
{
    const bool canEdit = editable();
    for (QWidget* widget : m_editable)
        widget->setEnabled(canEdit);

    // The replacement dialog needs the current asset to exist; with a missing
    // asset the entity shows a placeholder and the button stays disabled.
    m_assetReplaceButton->setEnabled(canEdit && !m_assetMissing);

    const model::Entity* selected = selectedEntity();
    m_lockedCheck->setEnabled(
        selected != nullptr
        && !hasLockedAncestor(m_document->project(), selected->uuid));
    applyKeyframeEnabledState();
    applySolverEnabledState();
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
    return entity && !isEntityLocked(m_document->project(), entity->uuid);
}

bool PropertiesPanel::commitEdit(const QString& what,
                                 const std::function<bool(model::Entity&)>& mutate,
                                 bool ignoreLock)
{
    const model::Entity* entity = selectedEntity();
    if (!entity)
        return false;
    const QString message =
        QStringLiteral("pvf: update entity \"%1\" %2").arg(entity->name, what);
    return commitEditWithMessage(what, message, mutate, ignoreLock);
}

bool PropertiesPanel::commitEditWithMessage(
    const QString& what, const QString& message,
    const std::function<bool(model::Entity&)>& mutate, bool ignoreLock)
{
    if (m_loading || !m_document)
        return false;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return false;
    if (isEntityLocked(m_document->project(), entity->uuid) && !ignoreLock)
        return false;

    const QJsonObject before = model::toJson(*entity);
    const model::Entity original = *entity;
    model::Entity updated = *entity;
    if (!mutate(updated))
    {
        refresh();
        return false;
    }

    QString error;
    if (!model::validateEntity(updated, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        refresh();
        return false;
    }
    if (model::toJson(updated) == before)
        return false;

    const Uuid uuid = updated.uuid;
    *entity = updated;

    m_committing = true;
    const bool ok = m_document->commitEntity(uuid, message, &error);
    m_committing = false;

    if (!ok)
    {
        // The store failed to save; put the in-memory entity back so the panel
        // never shows a half-applied edit.
        model::Entity* current = selectedEntity();
        if (current && current->uuid == original.uuid)
            *current = original;
        showSaveError(error);
        refresh();
        return false;
    }

    setStatus(QStringLiteral("已保存：%1").arg(what));
    refresh();
    return true;
}

void PropertiesPanel::applyRangeEdit(int newStart, int newEnd,
                                     const QString& what)
{
    if (m_loading || !m_document)
        return;

    model::Entity* entity = selectedEntity();
    if (!entity)
        return;
    if (isEntityLocked(m_document->project(), entity->uuid))
    {
        setStatus(QStringLiteral("实体或其祖先已锁定，不能修改时间范围"));
        refresh();
        return;
    }

    const Uuid uuid = entity->uuid;
    const QString name = entity->name;
    if (newStart == entity->start && newEnd == entity->end)
        return;

    const model::Project snapshot = m_document->project();
    model::Project updated;
    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    QString error;
    if (!applyEntityRange(snapshot, m_document->assets(), uuid, newStart,
                          newEnd, &updated, &upserts, &deletes, &error))
    {
        setStatus(QStringLiteral("未修改：%1").arg(error));
        if (isVisible())
            QMessageBox::warning(this, QStringLiteral("不能修改时间范围"), error);
        refresh();
        return;
    }
    if (upserts.isEmpty() && deletes.isEmpty())
        return;

    m_document->mutableProject() = std::move(updated);
    m_committing = true;
    const bool ok = m_document->commitChanges(
        upserts, deletes,
        QStringLiteral("pvf: change entity range \"%1\" %2").arg(name, what),
        &error);
    m_committing = false;

    if (!ok)
    {
        m_document->mutableProject() = snapshot;
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
    const QString track = field.section(QLatin1Char('.'), -1);
    connect(spin, &QDoubleSpinBox::editingFinished, this,
            [this, spin, field, track, member] {
                const double value = spin->value();
                commitEdit(field, [this, member, track, value](model::Entity& entity) {
                    entity.transform.*member = value;
                    applyAutoKey(entity, track, value);
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
    // Keep automated/headless runs from blocking on a modal dialog; the status
    // label still carries the error and the entity was rolled back above.
    if (isVisible())
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
                .arg(entity->name, detail::contentKindLabel(entity->content.kind));
        if (isEntityLocked(m_document->project(), entity->uuid))
            title += QStringLiteral(" · 已锁定");
        m_title->setText(title);
        m_title->setToolTip(entity->uuid);
        m_placeholder->setVisible(false);
        m_scroll->setVisible(true);

        reloadBasic(*entity);
        reloadTransform(*entity);
        reloadKeyframes(*entity);
        reloadContent(*entity);
        reloadHtmlVariables(*entity);
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
