#include "ProjectPropertiesPanel.h"

#include "app/EditorSession.h"
#include "model/Project.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pvf {

ProjectPropertiesPanel::ProjectPropertiesPanel(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    buildUi();
    refresh();
}

ProjectPropertiesPanel::ProjectPropertiesPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    buildUi();

    // Width/height/fps are fixed when the project is created in P1.  Keep the
    // same rows and look, but show them as read-only values.
    for (QSpinBox* spin : {m_width, m_height, m_fps})
    {
        spin->setReadOnly(true);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setFocusPolicy(Qt::NoFocus);
        spin->setToolTip(QStringLiteral("创建工程后锁定"));
    }
    m_authorLabel->hide();
    m_author->hide();
    m_descriptionLabel->hide();
    m_description->hide();

    refresh();
}

void ProjectPropertiesPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(8);
    layout->addLayout(form);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("projectName"));
    form->addRow(QStringLiteral("名称"), m_name);

    m_author = new QLineEdit(this);
    m_authorLabel = new QLabel(QStringLiteral("作者"), this);
    form->addRow(m_authorLabel, m_author);

    m_description = new QPlainTextEdit(this);
    m_description->setMaximumHeight(90);
    m_descriptionLabel = new QLabel(QStringLiteral("描述"), this);
    form->addRow(m_descriptionLabel, m_description);

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

    auto* hint =
        new QLabel(QStringLiteral("时间轴长度由素材和关键帧决定，这里不设时长。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_name, &QLineEdit::textEdited, this,
            [this](const QString& text) { applyName(text); });
    connect(m_author, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!m_project)
            return;
        m_project->author = text;
        m_project->notifyChanged();
    });
    connect(m_description, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_project)
            return;
        m_project->description = m_description->toPlainText();
        m_project->notifyChanged();
    });
    connect(m_width, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_project)
            return;
        m_project->width = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_project)
            return;
        m_project->height = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
    connect(m_fps, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_project)
            return;
        m_project->fps = value;
        m_project->notifyChanged();
        emit settingsChanged();
    });
}

void ProjectPropertiesPanel::applyName(const QString& text)
{
    if (m_session)
    {
        m_session->mutableProject().settings.name = text;
        // One commit per edit gesture; the store debounces the git commit.
        m_session->commitProject(QStringLiteral("pvf: 重命名工程"));
    }
    else if (m_project)
    {
        m_project->name = text;
        m_project->notifyChanged();
    }
    emit settingsChanged();
}

void ProjectPropertiesPanel::refresh()
{
    if (!m_session && !m_project)
        return;

    const QString name = m_session ? m_session->settings().name : m_project->name;
    const int width = m_session ? m_session->settings().width : m_project->width;
    const int height = m_session ? m_session->settings().height : m_project->height;
    const int fps = m_session ? m_session->settings().fps : m_project->fps;

    if (!m_name->hasFocus())
    {
        const QSignalBlocker blocker(m_name);
        m_name->setText(name);
    }
    if (m_project && !m_author->hasFocus())
    {
        const QSignalBlocker blocker(m_author);
        m_author->setText(m_project->author);
    }
    if (m_project && !m_description->hasFocus())
    {
        const QSignalBlocker blocker(m_description);
        m_description->setPlainText(m_project->description);
    }
    {
        const QSignalBlocker blocker(m_width);
        m_width->setValue(width);
    }
    {
        const QSignalBlocker blocker(m_height);
        m_height->setValue(height);
    }
    {
        const QSignalBlocker blocker(m_fps);
        m_fps->setValue(fps);
    }
}

} // namespace pvf
