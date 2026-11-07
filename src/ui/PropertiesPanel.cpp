#include "PropertiesPanel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf {

PropertiesPanel::PropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_title = new QLabel(QStringLiteral("未选择对象"), this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
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

    m_title->setText(e->name + QStringLiteral(" · 图片"));

    for (Row& r : m_rows)
    {
        const QSignalBlocker blocker(r.spin);
        r.spin->setValue(m_project->value(m_entity, r.prop, m_frame) / r.factor);
    }
}

} // namespace pvf
