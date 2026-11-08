#include "ExportDialog.h"

#include "core/Project.h"
#include "core/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

QString sanitizeName(QString name)
{
    name = name.trimmed();
    for (const QChar& c : QStringLiteral("/\\:*?\"<>|"))
        name.replace(c, QChar('_'));
    return name.isEmpty() ? QStringLiteral("未命名项目") : name;
}

} // namespace

ExportDialog::ExportDialog(const Project& project, QWidget* parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(QStringLiteral("导出视频"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("导出 MP4"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    layout->addLayout(form);

    auto* pathRow = new QHBoxLayout();
    m_path = new QLineEdit(defaultOutputPath(), this);
    pathRow->addWidget(m_path, 1);
    auto* browseButton = new QPushButton(QStringLiteral("浏览…"), this);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    pathRow->addWidget(browseButton);
    form->addRow(QStringLiteral("输出文件"), pathRow);

    auto* sizeRow = new QHBoxLayout();
    m_width = new QSpinBox(this);
    m_width->setObjectName(QStringLiteral("exportWidth"));
    m_width->setRange(16, 7680);
    m_width->setSingleStep(2);
    m_width->setValue(project.width);
    m_height = new QSpinBox(this);
    m_height->setObjectName(QStringLiteral("exportHeight"));
    m_height->setRange(16, 4320);
    m_height->setSingleStep(2);
    m_height->setValue(project.height);
    sizeRow->addWidget(m_width);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), this));
    sizeRow->addWidget(m_height);
    auto* projectSize = new QPushButton(QStringLiteral("工程尺寸"), this);
    sizeRow->addWidget(projectSize);
    sizeRow->addStretch(1);
    form->addRow(QStringLiteral("分辨率"), sizeRow);
    // Keep the project aspect so exported art is never stretched; changing
    // either edge updates the other one.
    connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int width) {
                const int height = std::max(
                    2, int(std::lround(width * m_project.height
                                       / double(std::max(1, m_project.width))))
                           & ~1);
                const QSignalBlocker blocker(m_height);
                m_height->setValue(std::min(height, m_height->maximum()));
                updateSummary();
            });
    connect(m_height, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int height) {
                const int width = std::max(
                    2, int(std::lround(height * m_project.width
                                       / double(std::max(1, m_project.height))))
                           & ~1);
                const QSignalBlocker blocker(m_width);
                m_width->setValue(std::min(width, m_width->maximum()));
                updateSummary();
            });
    connect(projectSize, &QPushButton::clicked, this, [this] {
        const QSignalBlocker widthBlocker(m_width);
        const QSignalBlocker heightBlocker(m_height);
        m_width->setValue(m_project.width);
        m_height->setValue(m_project.height);
        updateSummary();
    });

    m_view = new QComboBox(this);
    m_view->addItem(QStringLiteral("整画布"), kInvalidEntity);
    for (const Entity& entity : project.entities())
    {
        if (entity.type != EntityType::Camera
            || project.isEffectivelyHidden(entity.id))
            continue;
        m_view->addItem(entity.name, entity.id);
    }
    const int activeIndex = m_view->findData(project.activeCamera);
    if (activeIndex > 0)
        m_view->setCurrentIndex(activeIndex);
    form->addRow(QStringLiteral("视角"), m_view);

    m_quality = new QComboBox(this);
    m_quality->addItem(QStringLiteral("高画质（CRF 18，文件较大）"), 18);
    m_quality->addItem(QStringLiteral("标准（CRF 21）"), 21);
    m_quality->addItem(QStringLiteral("小体积（CRF 25）"), 25);
    m_quality->addItem(QStringLiteral("高压缩（CRF 28）"), 28);
    const int crfIndex = m_quality->findData(Settings::exportCrf());
    m_quality->setCurrentIndex(crfIndex >= 0 ? crfIndex : 1);
    form->addRow(QStringLiteral("画质"), m_quality);

    m_encoder = new QComboBox(this);
    m_encoder->setObjectName(QStringLiteral("exportEncoder"));
    m_encoder->addItem(QStringLiteral("H.264 软件（libx264，兼容）"), 0);
    if (VideoExporter::vaapiAvailable())
    {
        m_encoder->addItem(QStringLiteral("H.264 硬件（VAAPI，占用 GPU）"), 1);
        m_encoder->setToolTip(
            QStringLiteral("硬件编码更快，同档位用 QP 近似 CRF，画质略有差异"));
    }
    const int encoderIndex = m_encoder->findData(Settings::exportEncoder());
    if (encoderIndex > 0)
        m_encoder->setCurrentIndex(encoderIndex);
    form->addRow(QStringLiteral("编码器"), m_encoder);

    m_audio = new QCheckBox(QStringLiteral("包含工程音频"), this);
    m_audio->setChecked(Settings::exportAudio());
    form->addRow(QString(), m_audio);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    updateSummary();

    layout->addStretch(1);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("导出"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString ExportDialog::defaultOutputPath() const
{
    QString directory = Settings::exportDirectory();
    if (directory.isEmpty() && !m_project.filePath.isEmpty())
        directory = QFileInfo(m_project.filePath).absolutePath();
    if (directory.isEmpty())
        directory =
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();
    return QDir(directory).filePath(sanitizeName(m_project.name)
                                    + QStringLiteral(".mp4"));
}

ExportOptions ExportDialog::options() const
{
    ExportOptions options;
    options.outputPath = m_path->text().trimmed();
    if (!options.outputPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        options.outputPath += QStringLiteral(".mp4");
    options.size = QSize(m_width->value(), m_height->value());
    options.fps = std::max(1, m_project.fps);
    options.frameCount = exportFrames();
    options.cameraId = m_view->currentData().toInt();
    options.crf = m_quality->currentData().toInt();
    options.includeAudio = m_audio->isChecked();
    options.encoder = m_encoder->currentData().toInt() == 1
                          ? ExportEncoder::H264Vaapi
                          : ExportEncoder::H264Software;
    return options;
}

int ExportDialog::exportFrames() const
{
    const int length = m_project.timelineLength();
    // A project with no keyframes or timed media still has a canvas worth
    // showing; give the static scene two seconds instead of a single frame.
    if (length <= 0)
        return std::max(1, m_project.fps) * 2;
    return length;
}

void ExportDialog::accept()
{
    const QString path = m_path->text().trimmed();
    if (path.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("请选择输出文件。"));
        return;
    }
    QString target = path;
    if (!target.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        target += QStringLiteral(".mp4");
    if (QFileInfo::exists(target)
        && QMessageBox::question(
               this, QStringLiteral("导出视频"),
               QStringLiteral("文件已存在，要覆盖吗？\n%1").arg(target),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               != QMessageBox::Yes)
        return;

    Settings::setExportDirectory(QFileInfo(target).absolutePath());
    Settings::setExportCrf(m_quality->currentData().toInt());
    Settings::setExportAudio(m_audio->isChecked());
    Settings::setExportEncoder(m_encoder->currentData().toInt());
    m_path->setText(target);
    QDialog::accept();
}

void ExportDialog::browse()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 MP4"), m_path->text(),
        QStringLiteral("MP4 视频 (*.mp4)"));
    if (!path.isEmpty())
        m_path->setText(path);
}

void ExportDialog::updateSummary()
{
    const int fps = std::max(1, m_project.fps);
    const int frames = exportFrames();
    m_summary->setText(
        QStringLiteral("时长 %1 秒 · %2 帧 · %3 fps · 输出 %4×%5")
            .arg(frames / double(fps), 0, 'f', 2)
            .arg(frames)
            .arg(fps)
            .arg(m_width->value())
            .arg(m_height->value()));
}

} // namespace pvf
