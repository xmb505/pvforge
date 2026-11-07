#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pvf {

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    QString projectName() const;
    QString projectPath() const;
    QString author() const;
    QString description() const;
    int videoWidth() const;
    int videoHeight() const;
    int frameRate() const;

private:
    void browseLocation();
    void updateState();

    QLineEdit* m_name = nullptr;
    QLineEdit* m_location = nullptr;
    QComboBox* m_resolution = nullptr;
    QComboBox* m_fps = nullptr;
    QLineEdit* m_author = nullptr;
    QPlainTextEdit* m_description = nullptr;
    QPushButton* m_create = nullptr;
};

} // namespace pvf
