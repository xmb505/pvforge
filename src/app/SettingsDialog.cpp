#include "SettingsDialog.h"

#include "core/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pvf {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* general = new QGroupBox(QStringLiteral("常规"), this);
    auto* generalForm = new QFormLayout(general);
    m_language = new QComboBox(general);
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    const int index = m_language->findData(Settings::language());
    m_language->setCurrentIndex(index >= 0 ? index : 0);
    generalForm->addRow(QStringLiteral("界面语言"), m_language);
    generalForm->addRow(new QLabel(QStringLiteral("语言切换后需要重新启动软件。"), general));
    layout->addWidget(general);

    auto* editor = new QGroupBox(QStringLiteral("编辑器"), this);
    auto* editorForm = new QFormLayout(editor);
    m_snapPixels = new QSpinBox(editor);
    m_snapPixels->setRange(0, 40);
    m_snapPixels->setSuffix(QStringLiteral(" px"));
    m_snapPixels->setValue(Settings::snapPixels());
    m_snapPixels->setToolTip(QStringLiteral("0 表示关闭吸附"));
    editorForm->addRow(QStringLiteral("吸附距离"), m_snapPixels);
    m_edgeCenterSnap = new QCheckBox(QStringLiteral("实体边缘吸附到画面中心线"), editor);
    m_edgeCenterSnap->setChecked(Settings::edgeCenterSnap());
    editorForm->addRow(m_edgeCenterSnap);

    m_edgeMidpointSnap =
        new QCheckBox(QStringLiteral("吸附其他实体的边缘中点"), editor);
    m_edgeMidpointSnap->setChecked(Settings::edgeMidpointSnap());
    editorForm->addRow(m_edgeMidpointSnap);

    auto* angleRow = new QWidget(editor);
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(8);
    const QList<int> angles = Settings::snapAngles();
    m_angle0 = new QCheckBox(QStringLiteral("0°"), angleRow);
    m_angle90 = new QCheckBox(QStringLiteral("90°"), angleRow);
    m_angle180 = new QCheckBox(QStringLiteral("180°"), angleRow);
    m_angle270 = new QCheckBox(QStringLiteral("270°"), angleRow);
    m_angle0->setChecked(angles.contains(0));
    m_angle90->setChecked(angles.contains(90));
    m_angle180->setChecked(angles.contains(180));
    m_angle270->setChecked(angles.contains(270));
    angleLayout->addWidget(m_angle0);
    angleLayout->addWidget(m_angle90);
    angleLayout->addWidget(m_angle180);
    angleLayout->addWidget(m_angle270);
    angleLayout->addStretch(1);
    editorForm->addRow(QStringLiteral("旋转吸附角度"), angleRow);
    m_autoKey = new QCheckBox(QStringLiteral("新项目默认开启自动关键帧"), editor);
    m_autoKey->setChecked(Settings::autoKeyDefault());
    editorForm->addRow(m_autoKey);
    m_audioScrub = new QCheckBox(QStringLiteral("拖动/逐帧时播放音频（音频擦洗）"), editor);
    m_audioScrub->setChecked(Settings::audioScrub());
    editorForm->addRow(m_audioScrub);
    layout->addWidget(editor);

    auto* decode = new QGroupBox(QStringLiteral("解码"), this);
    auto* decodeForm = new QFormLayout(decode);
    m_hardwareDecode = new QComboBox(decode);
    m_hardwareDecode->addItem(QStringLiteral("自动（硬件优先）"), true);
    m_hardwareDecode->addItem(QStringLiteral("软件解码（兼容性优先）"), false);
    const int hwIndex =
        m_hardwareDecode->findData(Settings::hardwareVideoDecode());
    m_hardwareDecode->setCurrentIndex(hwIndex >= 0 ? hwIndex : 0);
    decodeForm->addRow(QStringLiteral("视频硬件解码"), m_hardwareDecode);
    auto* decodeHint = new QLabel(
        QStringLiteral("作用于 Qt Multimedia 视频播放后端，修改后重启生效。\n"
                       "软件解码会禁用 VAAPI/VDPAU，避免驱动兼容问题；"
                       "音频始终使用 FFmpeg 软件解码，当前视频首帧预览也是软件解码。"),
        decode);
    decodeHint->setWordWrap(true);
    decodeForm->addRow(decodeHint);
    layout->addWidget(decode);

    auto* project = new QGroupBox(QStringLiteral("新项目"), this);
    auto* projectForm = new QFormLayout(project);
    m_width = new QSpinBox(project);
    m_width->setRange(16, 8192);
    m_width->setValue(Settings::defaultWidth());
    projectForm->addRow(QStringLiteral("默认宽度"), m_width);
    m_height = new QSpinBox(project);
    m_height->setRange(16, 8192);
    m_height->setValue(Settings::defaultHeight());
    projectForm->addRow(QStringLiteral("默认高度"), m_height);
    m_fps = new QSpinBox(project);
    m_fps->setRange(1, 240);
    m_fps->setValue(Settings::defaultFps());
    projectForm->addRow(QStringLiteral("默认帧率"), m_fps);
    layout->addWidget(project);

    auto* timeline = new QGroupBox(QStringLiteral("时间轴"), this);
    auto* timelineForm = new QFormLayout(timeline);
    m_timelineZoom = new QDoubleSpinBox(timeline);
    m_timelineZoom->setRange(0.5, 32.0);
    m_timelineZoom->setDecimals(1);
    m_timelineZoom->setSuffix(QStringLiteral(" px/帧"));
    m_timelineZoom->setValue(Settings::timelinePixelsPerFrame());
    timelineForm->addRow(QStringLiteral("默认缩放"), m_timelineZoom);
    layout->addWidget(timeline);

    auto* assets = new QGroupBox(QStringLiteral("素材"), this);
    auto* assetsForm = new QFormLayout(assets);
    m_packAssets = new QCheckBox(QStringLiteral("新项目默认开启「全部打包到项目」"), assets);
    m_packAssets->setChecked(Settings::packAssetsDefault());
    assetsForm->addRow(m_packAssets);
    layout->addWidget(assets);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::accept()
{
    Settings::setLanguage(m_language->currentData().toString());
    Settings::setSnapPixels(m_snapPixels->value());
    Settings::setEdgeCenterSnap(m_edgeCenterSnap->isChecked());
    Settings::setEdgeMidpointSnap(m_edgeMidpointSnap->isChecked());
    QList<int> angles;
    if (m_angle0->isChecked())
        angles.append(0);
    if (m_angle90->isChecked())
        angles.append(90);
    if (m_angle180->isChecked())
        angles.append(180);
    if (m_angle270->isChecked())
        angles.append(270);
    Settings::setSnapAngles(angles);
    Settings::setAutoKeyDefault(m_autoKey->isChecked());
    Settings::setAudioScrub(m_audioScrub->isChecked());
    Settings::setHardwareVideoDecode(m_hardwareDecode->currentData().toBool());
    Settings::setDefaultSize(m_width->value(), m_height->value(), m_fps->value());
    Settings::setTimelinePixelsPerFrame(m_timelineZoom->value());
    Settings::setPackAssetsDefault(m_packAssets->isChecked());
    QDialog::accept();
}

} // namespace pvf
