#include "WelcomeWindow.h"

#include "MainWindow.h"
#include "NewProjectDialog.h"
#include "core/Project.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace pvf {

namespace {
constexpr auto kRecentKey = "recentProjects";
}

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

    const QStringList files = QSettings().value(kRecentKey).toStringList();
    for (const QString& path : files)
    {
        if (!QFileInfo::exists(path))
            continue;
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

void WelcomeWindow::rememberRecent(const QString& path)
{
    QSettings settings;
    QStringList files = settings.value(kRecentKey).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 10)
        files.removeLast();
    settings.setValue(kRecentKey, files);
    refreshRecent();
}

void WelcomeWindow::openProject(const QString& path)
{
    auto* project = new Project;
    QString error;
    if (!project->load(path, &error))
    {
        QMessageBox::warning(this, QStringLiteral("无法打开项目"),
                             QStringLiteral("%1\n\n%2").arg(path, error));
        delete project;
        refreshRecent();
        return;
    }

    rememberRecent(path);

    auto* window = new MainWindow(project);
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
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (QFileInfo::exists(path))
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("项目已存在"),
            QStringLiteral("%1 已存在，要覆盖吗？").arg(path));
        if (answer != QMessageBox::Yes)
            return;
    }

    Project project;
    project.name = dialog.projectName();
    project.author = dialog.author();
    project.description = dialog.description();
    project.width = dialog.videoWidth();
    project.height = dialog.videoHeight();
    project.fps = dialog.frameRate();

    QString error;
    if (!project.save(path, &error))
    {
        QMessageBox::warning(this, QStringLiteral("无法创建项目"), error);
        return;
    }

    openProject(path);
}

void WelcomeWindow::browseProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开项目"), QString(),
        QStringLiteral("PVForge 工程 (*.pvproj);;所有文件 (*)"));
    if (!path.isEmpty())
        openProject(path);
}

} // namespace pvf
