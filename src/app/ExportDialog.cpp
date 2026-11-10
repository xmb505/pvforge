#include "ExportDialog.h"

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
#include <QSettings>
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

QString defaultPreset()
{
    return QStringLiteral("medium");
}

} // namespace

QString friendlyExportError(const QString& error)
{
    if (error == QStringLiteral("output_exists"))
        return QStringLiteral("输出文件已存在。请确认覆盖或换一个文件名。");
    if (error == QStringLiteral("encoder_unavailable"))
        return QStringLiteral(
            "所选编码器不可用。h264_vaapi 需要可用的 VAAPI 设备与 FFmpeg 支持；"
            "可改用 libx264。");
    if (error == QStringLiteral("invalid_crf"))
        return QStringLiteral("CRF 必须在 0–51 之间。");
    if (error == QStringLiteral("unknown_preset"))
        return QStringLiteral("未知的 x264 preset。");
    if (error == QStringLiteral("unknown_encoder"))
        return QStringLiteral("未知的编码器。请选择 libx264 或 h264_vaapi。");
    if (error == QStringLiteral("cancelled"))
        return QStringLiteral("导出已取消。");
    if (error == QStringLiteral("输出目录不存在"))
        return QStringLiteral("输出目录不存在，请选择有效的文件夹。");
    if (error == QStringLiteral("没有选择摄像机"))
        return QStringLiteral("没有选择摄像机。请先在工程中创建摄像机。");
    if (error == QStringLiteral("摄像机实体不存在"))
        return QStringLiteral("所选摄像机已被删除，请重新选择。");
    if (error == QStringLiteral("指定实体不是摄像机"))
        return QStringLiteral("所选实体不是摄像机。");
    if (error == QStringLiteral("摄像机时间范围无效"))
        return QStringLiteral("摄像机的时间范围无效。");
    if (error == QStringLiteral("导出范围必须位于摄像机范围内"))
        return QStringLiteral("导出范围必须位于摄像机范围内。");
    if (error == QStringLiteral("输出帧率必须是工程帧率的 1x/2x/4x"))
        return QStringLiteral("输出帧率必须是工程帧率的 1x/2x/4x。");
    if (error == QStringLiteral("分辨率倍率必须是 1x/2x/4x"))
        return QStringLiteral("分辨率倍率必须是 1x/2x/4x。");
    return error;
}

ExportDialog::ExportDialog(const model::Project& project,
                           const Uuid& preferredCamera,
                           const QString& projectRoot, QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_projectRoot(projectRoot)
{
    setWindowTitle(QStringLiteral("导出视频"));
    setMinimumWidth(560);

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

    m_camera = new QComboBox(this);
    m_camera->setObjectName(QStringLiteral("exportCameraCombo"));
    m_camera->setMinimumWidth(280);
    struct CameraChoice
    {
        Uuid uuid;
        QString name;
    };
    QVector<CameraChoice> cameras;
    for (auto it = m_project.entities.constBegin();
         it != m_project.entities.constEnd(); ++it)
    {
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
    for (const CameraChoice& camera : cameras)
    {
        const QString name = camera.name.trimmed().isEmpty()
                                 ? QStringLiteral("摄像机")
                                 : camera.name;
        m_camera->addItem(name, camera.uuid);
    }
    if (cameras.isEmpty())
    {
        m_camera->addItem(QStringLiteral("无摄像机"), QString());
    }
    else
    {
        int index = preferredCamera.isEmpty()
                        ? 0
                        : m_camera->findData(preferredCamera);
        if (index < 0)
            index = 0;
        m_camera->setCurrentIndex(index);
    }
    form->addRow(QStringLiteral("摄像机"), m_camera);

    m_range = new QLabel(this);
    m_range->setObjectName(QStringLiteral("exportRangeLabel"));
    form->addRow(QStringLiteral("范围"), m_range);

    auto* pathRow = new QHBoxLayout();
    m_path = new QLineEdit(defaultOutputPath(), this);
    m_path->setObjectName(QStringLiteral("exportOutputPath"));
    pathRow->addWidget(m_path, 1);
    auto* browseButton = new QPushButton(QStringLiteral("浏览…"), this);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    pathRow->addWidget(browseButton);
    form->addRow(QStringLiteral("输出文件"), pathRow);

    QSettings stored;
    const QSize projectSize(std::max(1, m_project.settings.width),
                            std::max(1, m_project.settings.height));
    m_resolution = new QComboBox(this);
    m_resolution->setObjectName(QStringLiteral("exportResolutionCombo"));
    for (const int scale : {1, 2, 4})
    {
        const QSize size(projectSize.width() * scale,
                         projectSize.height() * scale);
        m_resolution->addItem(QStringLiteral("%1 × %2（%3x）")
                                  .arg(size.width())
                                  .arg(size.height())
                                  .arg(scale),
                              scale);
    }
    const int storedResolution =
        stored.value(QStringLiteral("export/resolutionScale"), 1).toInt();
    int resolutionIndex = m_resolution->findData(storedResolution);
    m_resolution->setCurrentIndex(resolutionIndex >= 0 ? resolutionIndex : 0);
    form->addRow(QStringLiteral("分辨率"), m_resolution);

    const int projectFps = std::max(1, m_project.settings.fps);
    m_fps = new QComboBox(this);
    m_fps->setObjectName(QStringLiteral("exportFpsCombo"));
    for (const int scale : {1, 2, 4})
    {
        m_fps->addItem(QStringLiteral("%1（%2x）")
                           .arg(projectFps * scale)
                           .arg(scale),
                       scale);
    }
    const int storedFps = stored.value(QStringLiteral("export/fpsScale"), 1).toInt();
    int fpsIndex = m_fps->findData(storedFps);
    m_fps->setCurrentIndex(fpsIndex >= 0 ? fpsIndex : 0);
    form->addRow(QStringLiteral("fps"), m_fps);

    m_crf = new QSpinBox(this);
    m_crf->setObjectName(QStringLiteral("exportCrfSpin"));
    m_crf->setRange(0, 51);
    m_crf->setValue(
        std::clamp(stored.value(QStringLiteral("export/crf"), 18).toInt(), 0, 51));
    m_crf->setToolTip(
        QStringLiteral("0 画质最好、文件最大；51 画质最差、文件最小。默认 18。"));
    form->addRow(QStringLiteral("CRF"), m_crf);

    m_encoder = new QComboBox(this);
    m_encoder->setObjectName(QStringLiteral("exportEncoderCombo"));
    m_encoder->addItem(QStringLiteral("H.264 软件（libx264，兼容）"),
                       QStringLiteral("libx264"));
    m_encoder->addItem(QStringLiteral("H.264 硬件（h264_vaapi，占用 GPU）"),
                       QStringLiteral("h264_vaapi"));
    const int storedEncoder =
        stored.value(QStringLiteral("export/encoder"), 0).toInt();
    m_encoder->setCurrentIndex(storedEncoder == 1 ? 1 : 0);
    form->addRow(QStringLiteral("编码器"), m_encoder);

    m_preset = new QComboBox(this);
    m_preset->setObjectName(QStringLiteral("exportPresetCombo"));
    for (const QString& preset :
         {QStringLiteral("ultrafast"), QStringLiteral("superfast"),
          QStringLiteral("veryfast"), QStringLiteral("faster"),
          QStringLiteral("fast"), QStringLiteral("medium"),
          QStringLiteral("slow"), QStringLiteral("slower"),
          QStringLiteral("veryslow")})
    {
        m_preset->addItem(preset, preset);
    }
    const QString storedPreset =
        stored.value(QStringLiteral("export/preset"), defaultPreset()).toString();
    int presetIndex = m_preset->findData(storedPreset);
    m_preset->setCurrentIndex(presetIndex >= 0 ? presetIndex
                                               : m_preset->findData(defaultPreset()));
    form->addRow(QStringLiteral("preset"), m_preset);

    m_audio = new QCheckBox(QStringLiteral("包含工程音频"), this);
    m_audio->setObjectName(QStringLiteral("exportIncludeAudio"));
    m_audio->setChecked(
        stored.value(QStringLiteral("export/audio"), true).toBool());
    form->addRow(QString(), m_audio);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    layout->addStretch(1);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_start = buttons->button(QDialogButtonBox::Ok);
    m_start->setObjectName(QStringLiteral("exportStartButton"));
    m_start->setText(QStringLiteral("开始导出"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_camera, &QComboBox::currentIndexChanged, this, [this](int) {
        updateCameraInfo();
        updateSummary();
        updateStartEnabled();
    });
    connect(m_resolution, &QComboBox::currentIndexChanged, this,
            [this](int) { updateSummary(); });
    connect(m_fps, &QComboBox::currentIndexChanged, this,
            [this](int) { updateSummary(); });
    connect(m_crf, &QSpinBox::valueChanged, this,
            [this](int) { updateSummary(); });
    connect(m_encoder, &QComboBox::currentIndexChanged, this,
            [this](int) { updateSummary(); });
    connect(m_preset, &QComboBox::currentIndexChanged, this,
            [this](int) { updateSummary(); });
    connect(m_path, &QLineEdit::textChanged, this,
            [this](const QString&) { updateStartEnabled(); });

    updateCameraInfo();
    updateSummary();
    updateStartEnabled();
}

Uuid ExportDialog::cameraUuid() const
{
    return m_camera ? m_camera->currentData().toString() : Uuid();
}

QString ExportDialog::outputPath() const
{
    QString path = m_path ? m_path->text().trimmed() : QString();
    if (path.isEmpty())
        return {};
    if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        path += QStringLiteral(".mp4");
    return path;
}

bool ExportDialog::includeAudio() const
{
    return m_audio && m_audio->isChecked();
}

int ExportDialog::outputFps() const
{
    const int scale = m_fps ? m_fps->currentData().toInt() : 1;
    return std::max(1, m_project.settings.fps) * std::max(1, scale);
}

int ExportDialog::resolutionScale() const
{
    const int scale = m_resolution ? m_resolution->currentData().toInt() : 1;
    return scale == 2 || scale == 4 ? scale : 1;
}

int ExportDialog::crf() const
{
    return m_crf ? m_crf->value() : 18;
}

QString ExportDialog::encoder() const
{
    return m_encoder ? m_encoder->currentData().toString()
                     : QStringLiteral("libx264");
}

QString ExportDialog::preset() const
{
    return m_preset ? m_preset->currentData().toString()
                    : defaultPreset();
}

void ExportDialog::updateCameraInfo()
{
    const Uuid uuid = cameraUuid();
    const auto it = m_project.entities.constFind(uuid);
    if (it == m_project.entities.constEnd())
    {
        m_startFrame = 0;
        m_endFrame = 0;
        m_range->setText(QStringLiteral("未选择摄像机"));
        return;
    }
    m_startFrame = it.value().start;
    m_endFrame = it.value().end;
    const int frames = std::max(0, m_endFrame - m_startFrame);
    const double seconds =
        frames / double(std::max(1, m_project.settings.fps));
    m_range->setText(QStringLiteral("%1 .. %2（%3 帧 · %4 秒）")
                         .arg(m_startFrame)
                         .arg(m_endFrame)
                         .arg(frames)
                         .arg(seconds, 0, 'f', 2));
}

void ExportDialog::updateSummary()
{
    const int projectFps = std::max(1, m_project.settings.fps);
    const int outFps = outputFps();
    const int frames = std::max(0, m_endFrame - m_startFrame);
    const double seconds = frames / double(projectFps);
    const int outputFrames =
        std::max(1, int(std::llround(seconds * outFps)));
    const QSize size(std::max(2, m_project.settings.width * resolutionScale()),
                     std::max(2, m_project.settings.height * resolutionScale()));
    m_summary->setText(
        QStringLiteral("时长 %1 秒 · 输出 %2 帧 · %3 fps · %4×%5 · CRF %6 · %7")
            .arg(seconds, 0, 'f', 2)
            .arg(outputFrames)
            .arg(outFps)
            .arg(size.width())
            .arg(size.height())
            .arg(crf())
            .arg(preset()));
}

void ExportDialog::updateStartEnabled()
{
    if (m_start)
        m_start->setEnabled(!cameraUuid().isEmpty() && !outputPath().isEmpty());
}

void ExportDialog::accept()
{
    if (cameraUuid().isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("工程中没有可用的摄像机。"));
        return;
    }
    if (outputPath().isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("请选择输出文件。"));
        return;
    }
    const QFileInfo targetInfo(outputPath());
    if (!targetInfo.absoluteDir().exists())
    {
        QMessageBox::warning(this, QStringLiteral("导出视频"),
                             QStringLiteral("输出目录不存在，请选择有效的文件夹。"));
        return;
    }
    if (!confirmOverwrite())
        return;
    if (!confirmLargeExport())
        return;

    const QString target = outputPath();
    QSettings stored;
    stored.setValue(QStringLiteral("export/directory"),
                    QFileInfo(target).absolutePath());
    stored.setValue(QStringLiteral("export/crf"), crf());
    stored.setValue(QStringLiteral("export/audio"), includeAudio());
    stored.setValue(QStringLiteral("export/encoder"),
                    encoder() == QStringLiteral("h264_vaapi") ? 1 : 0);
    stored.setValue(QStringLiteral("export/preset"), preset());
    stored.setValue(QStringLiteral("export/fpsScale"),
                    std::max(1, outputFps() / std::max(1, m_project.settings.fps)));
    stored.setValue(QStringLiteral("export/resolutionScale"), resolutionScale());
    m_path->setText(target);
    QDialog::accept();
}

bool ExportDialog::confirmOverwrite()
{
    const QString path = outputPath();
    if (path.isEmpty() || !QFileInfo::exists(path))
    {
        m_overwriteExisting = false;
        return true;
    }
    m_overwriteExisting =
        m_hasOverwriteOverride
            ? m_overwriteOverride
            : QMessageBox::question(
                  this, QStringLiteral("文件已存在"),
                  QStringLiteral("%1 已存在。\n\n要覆盖它吗？").arg(path),
                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                  == QMessageBox::Yes;
    return m_overwriteExisting;
}

bool ExportDialog::confirmLargeExport()
{
    const int scale = resolutionScale();
    const int width = m_project.settings.width * scale;
    const int height = m_project.settings.height * scale;
    if (scale < 4 || m_skipLargeWarning || (width <= 4096 && height <= 4096))
        return true;
    if (m_hasLargeOverride)
        return m_largeOverride;
    return QMessageBox::warning(
               this, QStringLiteral("导出尺寸很大"),
               QStringLiteral("4x 输出为 %1 × %2，可能非常慢并占用大量内存。\n\n"
                              "继续导出？")
                   .arg(width)
                   .arg(height),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
           == QMessageBox::Yes;
}

void ExportDialog::browse()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 MP4"), outputPath(),
        QStringLiteral("MP4 视频 (*.mp4)"));
    if (!path.isEmpty())
        m_path->setText(path);
}

QString ExportDialog::defaultOutputPath() const
{
    QSettings stored;
    QString directory =
        stored.value(QStringLiteral("export/directory")).toString();
    if (directory.isEmpty() && !m_projectRoot.isEmpty())
        directory = QFileInfo(m_projectRoot).absolutePath();
    if (directory.isEmpty())
        directory =
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();
    return QDir(directory).filePath(sanitizeName(m_project.settings.name)
                                    + QStringLiteral(".mp4"));
}

} // namespace pvf
