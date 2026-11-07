#include "ProjectPropertiesPanel.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pvf {

ProjectPropertiesPanel::ProjectPropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QLabel(QStringLiteral("项目"), this);
    QFont headerFont = header->font();
    headerFont.setBold(true);
    header->setFont(headerFont);
    layout->addWidget(header);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(8);
    layout->addLayout(form);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("projectName"));
    form->addRow(QStringLiteral("名称"), m_name);

    m_author = new QLineEdit(this);
    form->addRow(QStringLiteral("作者"), m_author);

    m_description = new QPlainTextEdit(this);
    m_description->setMaximumHeight(90);
    form->addRow(QStringLiteral("描述"), m_description);

    m_width = new QSpinBox(this);
    m_width->setRange(16, 8192);
    m_width->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("宽度"), m_width);

    m_height = new QSpinBox(this);
    m_height->setRange(16, 8192);
    m_height->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("高度"), m_height);

    m_fps = new QSpinBox(this);
    m_fps->setRange(1, 240);
    m_fps->setSuffix(QStringLiteral(" fps"));
    form->addRow(QStringLiteral("帧率"), m_fps);

    m_background = new QPushButton(this);
    form->addRow(QStringLiteral("背景色"), m_background);

    layout->addStretch(1);

    auto* hint =
        new QLabel(QStringLiteral("时间轴长度由素材和关键帧决定，这里不设时长。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_project->name = text;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_author, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_project->author = text;
        m_project->notifyChanged();
    });
    connect(m_description, &QPlainTextEdit::textChanged, this, [this] {
        m_project->description = m_description->toPlainText();
        m_project->notifyChanged();
    });
    connect(m_width, &QSpinBox::valueChanged, this, [this](int value) {
        m_project->width = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this](int value) {
        m_project->height = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_fps, &QSpinBox::valueChanged, this, [this](int value) {
        m_project->fps = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_background, &QPushButton::clicked, this,
            &ProjectPropertiesPanel::pickBackground);

    refresh();
}

void ProjectPropertiesPanel::refresh()
{
    if (!m_name->hasFocus())
    {
        const QSignalBlocker blocker(m_name);
        m_name->setText(m_project->name);
    }
    if (!m_author->hasFocus())
    {
        const QSignalBlocker blocker(m_author);
        m_author->setText(m_project->author);
    }
    if (!m_description->hasFocus())
    {
        const QSignalBlocker blocker(m_description);
        m_description->setPlainText(m_project->description);
    }
    {
        const QSignalBlocker blocker(m_width);
        m_width->setValue(m_project->width);
    }
    {
        const QSignalBlocker blocker(m_height);
        m_height->setValue(m_project->height);
    }
    {
        const QSignalBlocker blocker(m_fps);
        m_fps->setValue(m_project->fps);
    }

    m_backgroundColor = m_project->background;
    setColorButton(m_backgroundColor);
}

void ProjectPropertiesPanel::setColorButton(const QColor& color)
{
    m_background->setText(color.name().toUpper());
    const QString text = color.lightness() > 140 ? QStringLiteral("#000")
                                                 : QStringLiteral("#fff");
    m_background->setStyleSheet(
        QStringLiteral("background-color:%1;color:%2;").arg(color.name(), text));
}

void ProjectPropertiesPanel::pickBackground()
{
    const QColor color =
        QColorDialog::getColor(m_backgroundColor, this, QStringLiteral("背景颜色"));
    if (!color.isValid())
        return;
    m_backgroundColor = color;
    setColorButton(color);
    m_project->background = color;
    m_project->notifyChanged();
}

} // namespace pvf
