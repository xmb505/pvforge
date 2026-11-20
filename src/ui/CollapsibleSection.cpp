#include "CollapsibleSection.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace pvf {

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* content,
                                       QWidget* parent)
    : QWidget(parent)
    , m_title(title)
    , m_content(content)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The arrow and bold title keep their natural size and sit flush left;
    // the stretch owns the rest of the row.  No centred expanding button and
    // no extra divider frame.
    auto* headerRow = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    m_header = new QToolButton(headerRow);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setAutoRaise(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_header->setText(m_title);
    QFont headerFont = m_header->font();
    headerFont.setBold(true);
    m_header->setFont(headerFont);
    headerLayout->addWidget(m_header);
    headerLayout->addStretch(1);
    layout->addWidget(headerRow);

    content->setParent(this);
    layout->addWidget(content);

    connect(m_header, &QToolButton::toggled, this, [this](bool) { applyState(); });

    applyState();
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_header->setChecked(expanded);
    applyState();
}

bool CollapsibleSection::isExpanded() const
{
    return m_header->isChecked();
}

void CollapsibleSection::applyState()
{
    const bool expanded = m_header->isChecked();
    m_content->setVisible(expanded);
    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_header->setText(m_title);
}

} // namespace pvf
