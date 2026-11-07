#pragma once

#include <QWidget>

class QToolButton;

namespace pvf {

// A collapsible category header + content block, matching Mine-imator's
// property panel categories.
class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    CollapsibleSection(const QString& title, QWidget* content,
                       QWidget* parent = nullptr);

    QString title() const { return m_title; }
    void setExpanded(bool expanded);
    bool isExpanded() const;

private:
    void applyState();

    QString m_title;
    QToolButton* m_header = nullptr;
    QWidget* m_content = nullptr;
};

} // namespace pvf
