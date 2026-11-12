#pragma once

#include "core/EntityId.h"

#if PVFORGE_LEGACY_SHELL
#include "core/Project.h"
#endif

#include <QColor>
#include <QWidget>

class QPushButton;

namespace pvf {

class EditorSession;

class EnvironmentPanel : public QWidget
{
    Q_OBJECT

public:
#if PVFORGE_LEGACY_SHELL
    explicit EnvironmentPanel(Project* project, QWidget* parent = nullptr);
#endif
    explicit EnvironmentPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void changed();

private:
    void buildUi();
    void setColorButton(const QColor& color);
    void pickBackground();

#if PVFORGE_LEGACY_SHELL
    Project* m_project = nullptr;
#endif
    EditorSession* m_session = nullptr;
    QPushButton* m_background = nullptr;
    QColor m_backgroundColor{0x10, 0x10, 0x14};
};

} // namespace pvf
