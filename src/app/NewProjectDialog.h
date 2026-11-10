#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

namespace pvf {

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    QString projectName() const;
    // Directory project: `<location>/<name>.pvproj/`.
    QString projectPath() const;
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
    QLabel* m_targetHint = nullptr;
    QPushButton* m_create = nullptr;
};

} // namespace pvf
