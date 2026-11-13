#pragma once

#include "core/EntityId.h"


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
    explicit ProjectPropertiesPanel(EditorSession* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void settingsChanged();

private:
    void buildUi();
    void applyName(const QString& text);

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
