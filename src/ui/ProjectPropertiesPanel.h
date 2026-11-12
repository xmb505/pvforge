#pragma once

#include "core/EntityId.h"

#if PVFORGE_LEGACY_SHELL
#include "core/Project.h"
#endif

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace pvf {

class EditorSession;

class ProjectPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
#if PVFORGE_LEGACY_SHELL
    explicit ProjectPropertiesPanel(Project* project, QWidget* parent = nullptr);
#endif
    explicit ProjectPropertiesPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void settingsChanged();

private:
    void buildUi();
    void applyName(const QString& text);

#if PVFORGE_LEGACY_SHELL
    Project* m_project = nullptr;
#endif
    EditorSession* m_session = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_author = nullptr;
    QLabel* m_authorLabel = nullptr;
    QPlainTextEdit* m_description = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
};

} // namespace pvf
