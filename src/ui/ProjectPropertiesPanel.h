#pragma once

#include "core/Project.h"

#include <QColor>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace pvf {

class ProjectPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectPropertiesPanel(Project* project, QWidget* parent = nullptr);

    void refresh();

signals:
    void settingsChanged();

private:
    void setColorButton(const QColor& color);
    void pickBackground();

    Project* m_project = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_author = nullptr;
    QPlainTextEdit* m_description = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
    QPushButton* m_background = nullptr;
    QColor m_backgroundColor{0x10, 0x10, 0x14};
};

} // namespace pvf
