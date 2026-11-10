#pragma once

#include "core/Project.h"

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
    explicit ProjectPropertiesPanel(Project* project, QWidget* parent = nullptr);
    explicit ProjectPropertiesPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void settingsChanged();

private:
    void buildUi();
    void applyName(const QString& text);

    Project* m_project = nullptr;
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
