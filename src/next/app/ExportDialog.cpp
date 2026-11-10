#include "next/app/ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

struct CameraChoice
{
    Uuid uuid;
    QString name;
};

} // namespace

ExportDialog::ExportDialog(const model::Project& project, const Uuid& preferredCamera,
                           QWidget* parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(tr("导出视频"));
    setModal(true);
    resize(560, 320);

    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setObjectName(QStringLiteral("exportCameraCombo"));
    m_cameraCombo->setMinimumWidth(260);

    QVector<CameraChoice> cameras;
    for (auto it = m_project.entities.constBegin();
         it != m_project.entities.constEnd(); ++it) {
        if (it.value().content.kind == ContentKind::Camera)
            cameras.append({it.value().uuid, it.value().name});
    }
    std::sort(cameras.begin(), cameras.end(),
              [](const CameraChoice& a, const CameraChoice& b) {
                  const int byName = QString::localeAwareCompare(a.name, b.name);
                  if (byName != 0)
                      return byName < 0;
                  return a.uuid < b.uuid;
              });

    for (const CameraChoice& camera : cameras) {
        const QString name = camera.name.trimmed().isEmpty()
                                 ? tr("摄像机")
                                 : camera.name;
        m_cameraCombo->addItem(tr("%1 (%2)").arg(name, camera.uuid.left(8)),
                               camera.uuid);
    }
    if (cameras.isEmpty())
        m_cameraCombo->addItem(tr("无摄像机"), QString());
    else {
        int index = preferredCamera.isEmpty()
                        ? 0
                        : m_cameraCombo->findData(preferredCamera);
        if (index < 0)
            index = 0;
        m_cameraCombo->setCurrentIndex(index);
    }

    m_rangeLabel = new QLabel(this);
    m_rangeLabel->setObjectName(QStringLiteral("exportRangeLabel"));

    const QSize projectSize(std::max(1, m_project.settings.width),
                            std::max(1, m_project.settings.height));
    m_resolutionCombo = new QComboBox(this);
    m_resolutionCombo->setObjectName(QStringLiteral("exportResolutionCombo"));
    for (const int scale : {1, 2, 4}) {
        const QSize size(projectSize.width() * scale, projectSize.height() * scale);
        m_resolutionCombo->addItem(
            tr("%1 × %2（%3x）").arg(size.width()).arg(size.height()).arg(scale),
            scale);
    }
    m_resolutionCombo->setCurrentIndex(0);

    const int projectFps = std::max(1, m_project.settings.fps);
    m_fpsCombo = new QComboBox(this);
    m_fpsCombo->setObjectName(QStringLiteral("exportFpsCombo"));
    for (const int scale : {1, 2, 4}) {
        m_fpsCombo->addItem(
            tr("%1（%2x）").arg(projectFps * scale).arg(scale), scale);
    }
    m_fpsCombo->setCurrentIndex(0);

    m_outputEdit = new QLineEdit(this);
    m_outputEdit->setObjectName(QStringLiteral("exportOutputPath"));
    auto* browse = new QPushButton(tr("浏览…"), this);
    auto* outputRow = new QHBoxLayout;
    outputRow->setContentsMargins(0, 0, 0, 0);
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(browse);

    m_audioCheck = new QCheckBox(tr("包含音频"), this);
    m_audioCheck->setObjectName(QStringLiteral("exportIncludeAudio"));
    m_audioCheck->setChecked(true);

    form->addRow(tr("摄像机"), m_cameraCombo);
    form->addRow(tr("范围"), m_rangeLabel);
    form->addRow(tr("分辨率"), m_resolutionCombo);
    form->addRow(tr("fps"), m_fpsCombo);
    form->addRow(tr("输出文件"), outputRow);
    form->addRow(QString(), m_audioCheck);
    root->addLayout(form);
    root->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    m_startButton = buttons->button(QDialogButtonBox::Ok);
    m_startButton->setText(tr("开始导出"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    root->addWidget(buttons);

    QString base = m_project.settings.name.trimmed();
    base = QFileInfo(base).fileName();
    if (base.isEmpty())
        base = QStringLiteral("export");
    if (base.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        base.chop(4);
    m_outputEdit->setText(QDir(QDir::homePath()).filePath(base + QStringLiteral(".mp4")));

    connect(m_cameraCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateCameraInfo();
        updateStartEnabled();
    });
    connect(browse, &QPushButton::clicked, this, [this] { chooseOutputPath(); });
    connect(m_outputEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateStartEnabled(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (cameraUuid().isEmpty() || outputPath().isEmpty())
            return;
        if (!confirmLargeExport())
            return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateCameraInfo();
    updateStartEnabled();
}

Uuid ExportDialog::cameraUuid() const
{
    return m_cameraCombo ? m_cameraCombo->currentData().toString() : Uuid();
}

QString ExportDialog::outputPath() const
{
    QString path = m_outputEdit ? m_outputEdit->text().trimmed() : QString();
    if (path.isEmpty())
        return {};
    if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        path += QStringLiteral(".mp4");
    return path;
}

bool ExportDialog::includeAudio() const
{
    return m_audioCheck && m_audioCheck->isChecked();
}

int ExportDialog::outputFps() const
{
    const int scale = m_fpsCombo ? m_fpsCombo->currentData().toInt() : 1;
    return std::max(1, m_project.settings.fps) * std::max(1, scale);
}

int ExportDialog::resolutionScale() const
{
    const int scale = m_resolutionCombo ? m_resolutionCombo->currentData().toInt() : 1;
    return scale == 2 || scale == 4 ? scale : 1;
}

void ExportDialog::updateCameraInfo()
{
    const Uuid uuid = cameraUuid();
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd()) {
        m_startFrame = 0;
        m_endFrame = 0;
        m_rangeLabel->setText(tr("未选择摄像机"));
    } else {
        m_startFrame = it.value().start;
        m_endFrame = it.value().end;
        m_rangeLabel->setText(tr("%1 .. %2（%3 帧）")
                                  .arg(m_startFrame)
                                  .arg(m_endFrame)
                                  .arg(std::max(0, m_endFrame - m_startFrame)));
    }
}

void ExportDialog::chooseOutputPath()
{
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("导出视频"), outputPath(), tr("MP4 视频 (*.mp4)"));
    if (!chosen.isEmpty())
        m_outputEdit->setText(chosen);
}

void ExportDialog::updateStartEnabled()
{
    if (m_startButton)
        m_startButton->setEnabled(!cameraUuid().isEmpty() && !outputPath().isEmpty());
}

bool ExportDialog::confirmLargeExport()
{
    const int scale = resolutionScale();
    const int width = m_project.settings.width * scale;
    const int height = m_project.settings.height * scale;
    if (scale < 4 || m_skipLargeWarning
        || (width <= 4096 && height <= 4096))
        return true;

    return QMessageBox::warning(
               this, tr("导出尺寸很大"),
               tr("4x 输出为 %1 × %2，可能非常慢并占用大量内存。\n\n"
                  "继续导出？")
                   .arg(width)
                   .arg(height),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
           == QMessageBox::Yes;
}

} // namespace pvf::next
