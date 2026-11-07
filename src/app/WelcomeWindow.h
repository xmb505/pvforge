#pragma once

#include <QMainWindow>

class QListWidget;

namespace pvf {

class WelcomeWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit WelcomeWindow(QWidget* parent = nullptr);

private:
    void refreshRecent();
    void openProject(const QString& path);
    void createProject();
    void browseProject();
    void rememberRecent(const QString& path);

    QListWidget* m_recent = nullptr;
};

} // namespace pvf
