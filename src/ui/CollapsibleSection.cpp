#include "CollapsibleSection.h"

#include <QFrame>
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

    m_header = new QToolButton(this);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setStyleSheet(QStringLiteral(
        "QToolButton { text-align: left; padding: 6px 10px; font-weight: bold;"
        " border: none; background: palette(button); }"));
    layout->addWidget(m_header);

    content->setParent(this);
    layout->addWidget(content);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    layout->addWidget(divider);

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
    m_header->setText((expanded ? QStringLiteral("▼  ") : QStringLiteral("▶  "))
                      + m_title);
}

} // namespace pvf
