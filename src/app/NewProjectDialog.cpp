#include "NewProjectDialog.h"

#include "core/Settings.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSize>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace pvf {

namespace {

QString sanitizeName(const QString& name)
{
    QString result = name.trimmed();
    for (const QChar& c : QStringLiteral("/\\:*?\"<>|"))
        result.replace(c, QChar('_'));
    if (result.isEmpty())
        result = QStringLiteral("未命名项目");
    return result;
}

} // namespace

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("新建项目"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("新建项目"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    layout->addLayout(form);

    m_name = new QLineEdit(QStringLiteral("未命名项目"), this);
    form->addRow(QStringLiteral("项目名称"), m_name);

    auto* locationRow = new QHBoxLayout();
    m_location = new QLineEdit(this);
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    m_location->setText(QDir(base).filePath(QStringLiteral("PVForge")));
    locationRow->addWidget(m_location, 1);
    auto* browse = new QPushButton(QStringLiteral("浏览…"), this);
    connect(browse, &QPushButton::clicked, this, &NewProjectDialog::browseLocation);
    locationRow->addWidget(browse);
    form->addRow(QStringLiteral("位置"), locationRow);

    m_resolution = new QComboBox(this);
    m_resolution->addItem(QStringLiteral("1920 × 1080 (Full HD)"), QSize(1920, 1080));
    m_resolution->addItem(QStringLiteral("1280 × 720 (HD)"), QSize(1280, 720));
    m_resolution->addItem(QStringLiteral("3840 × 2160 (4K)"), QSize(3840, 2160));
    m_resolution->addItem(QStringLiteral("1080 × 1920 (竖屏)"), QSize(1080, 1920));
    const int defaultIndex =
        m_resolution->findData(QSize(Settings::defaultWidth(), Settings::defaultHeight()));
    if (defaultIndex >= 0)
        m_resolution->setCurrentIndex(defaultIndex);
    form->addRow(QStringLiteral("分辨率"), m_resolution);

    m_fps = new QComboBox(this);
    m_fps->addItem(QStringLiteral("24 fps"), 24);
    m_fps->addItem(QStringLiteral("30 fps"), 30);
    m_fps->addItem(QStringLiteral("60 fps"), 60);
    const int fpsIndex = m_fps->findData(Settings::defaultFps());
    m_fps->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : 1);
    form->addRow(QStringLiteral("帧率"), m_fps);

    m_targetHint = new QLabel(this);
    m_targetHint->setWordWrap(true);
    m_targetHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_targetHint);

    auto* hint = new QLabel(
        QStringLiteral("工程以 .pvproj 目录保存；分辨率与帧率创建后锁定。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    m_create = new QPushButton(QStringLiteral("创建项目"), this);
    m_create->setDefault(true);
    connect(m_create, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_create);
    layout->addLayout(buttons);

    connect(m_name, &QLineEdit::textChanged, this, [this] { updateState(); });
    connect(m_location, &QLineEdit::textChanged, this, [this] { updateState(); });
    updateState();
}

void NewProjectDialog::browseLocation()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择项目位置"), m_location->text());
    if (!directory.isEmpty())
        m_location->setText(directory);
}

void NewProjectDialog::updateState()
{
    const bool valid = !m_name->text().trimmed().isEmpty()
                       && !m_location->text().trimmed().isEmpty();
    m_create->setEnabled(valid);
    m_targetHint->setText(valid
                              ? QStringLiteral("将创建目录：%1").arg(projectPath())
                              : QString());
}

QString NewProjectDialog::projectName() const
{
    return sanitizeName(m_name->text());
}

QString NewProjectDialog::projectPath() const
{
    return QDir(m_location->text().trimmed())
        .filePath(projectName() + QStringLiteral(".pvproj"));
}

int NewProjectDialog::videoWidth() const
{
    return m_resolution->currentData().toSize().width();
}

int NewProjectDialog::videoHeight() const
{
    return m_resolution->currentData().toSize().height();
}

int NewProjectDialog::frameRate() const
{
    return m_fps->currentData().toInt();
}

} // namespace pvf
