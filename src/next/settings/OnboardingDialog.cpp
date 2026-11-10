#include "next/settings/OnboardingDialog.h"

#include "next/settings/GitDetect.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pvf::next {

namespace {

QLabel* welcomeLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

OnboardingDialog::OnboardingDialog(const AppSettings& settings, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("欢迎使用 PVForge"));
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel(QStringLiteral("欢迎使用 PVForge"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    root->addWidget(welcomeLabel(
        QStringLiteral("PVForge 使用新的实体模型：工程由 project.json 与 entities/*.json "
                       "组成，一个实体一个文件。编辑会自动保存到磁盘，撤销使用本次运行的"
                       "内存撤销栈。"),
        this));
    root->addWidget(welcomeLabel(
        QStringLiteral("启用 Git 后，PVForge 会自动为文本工程文件建立提交历史，"
                       "用于历史查看、崩溃恢复和 AI 写入审计。"),
        this));

    auto* form = new QFormLayout;
    m_gitEnabled =
        new QCheckBox(QStringLiteral("启用 Git（历史、崩溃恢复、AI 写入）"), this);
    m_gitEnabled->setObjectName(QStringLiteral("onboardingGitEnabled"));
    m_gitEnabled->setChecked(settings.gitEnabled);
    form->addRow(m_gitEnabled);

    m_gitStatus = welcomeLabel(QString(), this);
    m_gitStatus->setObjectName(QStringLiteral("onboardingGitStatus"));
    form->addRow(QStringLiteral("检测状态"), m_gitStatus);

    m_gitDetect = new QPushButton(QStringLiteral("重新检测"), this);
    m_gitDetect->setObjectName(QStringLiteral("onboardingGitDetect"));
    connect(m_gitDetect, &QPushButton::clicked, this, &OnboardingDialog::refreshGitStatus);
    form->addRow(QStringLiteral("检测"), m_gitDetect);
    root->addLayout(form);

    root->addWidget(welcomeLabel(
        QStringLiteral("关闭时只保留本次运行的内存撤销；重启后无法撤销。"
                       "没有 Git 时 MCP 只读。"),
        this));

    auto* buttons = new QDialogButtonBox(this);
    buttons->setObjectName(QStringLiteral("onboardingButtons"));
    auto* start = buttons->addButton(QStringLiteral("开始使用"),
                                     QDialogButtonBox::AcceptRole);
    start->setObjectName(QStringLiteral("onboardingStart"));
    start->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);

    refreshGitStatus();
}

bool OnboardingDialog::gitEnabled() const
{
    return m_gitEnabled->isChecked();
}

void OnboardingDialog::refreshGitStatus()
{
    const GitInfo info = detectGit();
    m_gitStatus->setText(
        info.found
            ? QStringLiteral("已找到：%1（%2）").arg(info.executable, info.version)
            : QStringLiteral("未找到 Git：%1").arg(info.error));
}

} // namespace pvf::next
