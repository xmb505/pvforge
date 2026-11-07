#include "EnvironmentPanel.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf {

EnvironmentPanel::EnvironmentPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(8);
    layout->addLayout(form);

    m_background = new QPushButton(this);
    form->addRow(QStringLiteral("背景色"), m_background);

    auto* hint = new QLabel(QStringLiteral("环境设置会随项目保存。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_background, &QPushButton::clicked, this, &EnvironmentPanel::pickBackground);
    refresh();
}

void EnvironmentPanel::refresh()
{
    m_backgroundColor = m_project->background;
    setColorButton(m_backgroundColor);
}

void EnvironmentPanel::setColorButton(const QColor& color)
{
    m_background->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    m_background->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

void EnvironmentPanel::pickBackground()
{
    const QColor color =
        QColorDialog::getColor(m_backgroundColor, this, QStringLiteral("背景颜色"));
    if (!color.isValid())
        return;
    m_backgroundColor = color;
    setColorButton(color);
    m_project->background = color;
    m_project->notifyChanged();
    emit changed();
}

} // namespace pvf
