#include "EnvironmentPanel.h"

#include "app/EditorSession.h"
#include "model/Project.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf {


EnvironmentPanel::EnvironmentPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    buildUi();
    refresh();
}

void EnvironmentPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(8);
    layout->addLayout(form);

    m_background = new QPushButton(this);
    m_background->setObjectName(QStringLiteral("backgroundButton"));
    form->addRow(QStringLiteral("背景色"), m_background);

    auto* hint = new QLabel(QStringLiteral("环境设置会随项目保存。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_background, &QPushButton::clicked, this,
            &EnvironmentPanel::pickBackground);
}

void EnvironmentPanel::refresh()
{
    if (!m_session
    )
        return;

    const QString value = m_session->settings().background;
    const QColor color(value);
    m_backgroundColor = color.isValid() ? color : QColor(0x10, 0x10, 0x14);
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
    if (!m_session
    )
        return;

    const QColor color =
        QColorDialog::getColor(m_backgroundColor, this, QStringLiteral("背景颜色"));
    if (!color.isValid())
        return;

    m_backgroundColor = color;
    setColorButton(color);

    if (m_session)
    {
        m_session->mutableProject().settings.background = color.name();
        m_session->commitProject(QStringLiteral("pvf: 背景色"));
    }
    else
    {
    }
    emit changed();
}

} // namespace pvf
