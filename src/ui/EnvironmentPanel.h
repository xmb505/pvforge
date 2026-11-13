#pragma once

#include "core/EntityId.h"


#include <QColor>
#include <QWidget>

class QPushButton;

namespace pvf {

class EditorSession;

class EnvironmentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EnvironmentPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void changed();

private:
    void buildUi();
    void setColorButton(const QColor& color);
    void pickBackground();

    EditorSession* m_session = nullptr;
    QPushButton* m_background = nullptr;
    QColor m_backgroundColor{0x10, 0x10, 0x14};
};

} // namespace pvf
