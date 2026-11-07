#pragma once

#include "core/Project.h"

#include <QColor>
#include <QWidget>

class QPushButton;

namespace pvf {

class EnvironmentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EnvironmentPanel(Project* project, QWidget* parent = nullptr);

    void refresh();

signals:
    void changed();

private:
    void setColorButton(const QColor& color);
    void pickBackground();

    Project* m_project = nullptr;
    QPushButton* m_background = nullptr;
    QColor m_backgroundColor{0x10, 0x10, 0x14};
};

} // namespace pvf
