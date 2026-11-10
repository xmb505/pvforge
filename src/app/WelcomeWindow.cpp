#include "WelcomeWindow.h"

#include "EditorSession.h"
#include "MainWindow.h"
#include "NewProjectDialog.h"
#include "model/Project.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf {

WelcomeWindow::WelcomeWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PVForge"));
    resize(960, 640);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(64, 48, 64, 32);
    layout->setSpacing(18);

    auto* logo = new QLabel(QStringLiteral("PVForge"), central);
    QFont logoFont = logo->font();
    logoFont.setPointSize(44);
    logoFont.setBold(true);
    logo->setFont(logoFont);
    layout->addWidget(logo);

    auto* subtitle = new QLabel(QStringLiteral("2D Music PV Studio · 用关键帧做音乐 PV"), central);
    QFont subtitleFont = subtitle->font();
    subtitleFont.setPointSize(subtitleFont.pointSize() + 2);
    subtitle->setFont(subtitleFont);
    layout->addWidget(subtitle);
    layout->addSpacing(12);

    auto* header = new QHBoxLayout();
    auto* recentLabel = new QLabel(QStringLiteral("最近项目"), central);
    QFont recentFont = recentLabel->font();
    recentFont.setBold(true);
    recentLabel->setFont(recentFont);
    header->addWidget(recentLabel);
    header->addStretch(1);

    auto* openButton = new QPushButton(QStringLiteral("打开项目…"), central);
    connect(openButton, &QPushButton::clicked, this, &WelcomeWindow::browseProject);
    header->addWidget(openButton);

    auto* newButton = new QPushButton(QStringLiteral("新建项目"), central);
    newButton->setDefault(true);
    connect(newButton, &QPushButton::clicked, this, &WelcomeWindow::createProject);
    header->addWidget(newButton);
    layout->addLayout(header);

    m_recent = new QListWidget(central);
    m_recent->setAlternatingRowColors(true);
    m_recent->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_recent, 1);

    connect(m_recent, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                const QString path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                    openProject(path);
            });

    auto* footer = new QHBoxLayout();
    auto* version = new QLabel(QStringLiteral("PVForge 0.1"), central);
    footer->addWidget(version);
    footer->addStretch(1);
    auto* quit = new QPushButton(QStringLiteral("退出"), central);
    connect(quit, &QPushButton::clicked, this, &QWidget::close);
    footer->addWidget(quit);
    layout->addLayout(footer);

    setCentralWidget(central);
    refreshRecent();
}

void WelcomeWindow::refreshRecent()
{
    m_recent->clear();

    // recentProjects() already validates that each entry is still a .pvproj
    // directory with a project.json and drops stale legacy file paths.
    const QStringList directories = recentProjects();
    for (const QString& path : directories)
    {
        auto* item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(QSize(0, 36));
    }

    if (m_recent->count() == 0)
    {
        auto* item = new QListWidgetItem(
            QStringLiteral("还没有项目，点「新建项目」开始"), m_recent);
        item->setFlags(Qt::NoItemFlags);
    }
}

void WelcomeWindow::openProject(const QString& path)
{
    QString error;
    auto session = EditorSession::openProject(path, &error);
    if (!session)
    {
        QMessageBox::warning(this, QStringLiteral("无法打开项目"), error);
        refreshRecent();
        return;
    }

    rememberRecentProject(session->root());

    auto* window = new MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    hide();
}

void WelcomeWindow::createProject()
{
    NewProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = dialog.projectPath();
    const QFileInfo info(path);
    if (info.exists())
    {
        QMessageBox::warning(
            this, QStringLiteral("无法创建项目"),
            info.isDir()
                ? QStringLiteral("目录已存在：%1\n\n请换一个项目名称或位置。").arg(path)
                : QStringLiteral("同名文件已存在：%1").arg(path));
        return;
    }
    QDir().mkpath(info.absolutePath());

    model::ProjectSettings settings;
    settings.name = dialog.projectName();
    settings.width = dialog.videoWidth();
    settings.height = dialog.videoHeight();
    settings.fps = dialog.frameRate();

    QString error;
    auto session = EditorSession::createProject(path, settings, &error);
    if (!session)
    {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"), error);
        return;
    }

    rememberRecentProject(session->root());
    auto* window = new MainWindow(session.release());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    hide();
}

void WelcomeWindow::browseProject()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("打开项目（选择 .pvproj 工程目录）"), QString(),
        QFileDialog::ShowDirsOnly);
    if (!path.isEmpty())
        openProject(path);
}

} // namespace pvf
