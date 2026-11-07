#include "SettingsDialog.h"

#include "AppIcon.h"
#include "SystemInfo.h"
#include "core/Settings.h"
#include "media/DecoderInfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pvf {

namespace {

QIcon themeIcon(const QStringList& names)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

QLabel* infoLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
    return label;
}

QString aboutText()
{
    QString text = collectSystemInfo().toText();
    text += QStringLiteral("\n\n【解码】\n") + decoderInfo().toText();
    text += QStringLiteral(
        "\n\n【项目】\n"
        "主页：https://github.com/xmb505/pvforge\n"
        "许可证：MIT");
    return text;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(660, 460);
    resize(760, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setFixedWidth(158);
    m_categories->setFrameShape(QFrame::StyledPanel);
    m_categories->setIconSize(QSize(16, 16));
    m_categories->setUniformItemSizes(true);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    body->addWidget(m_categories);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("settingsPages"));
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(m_pages, 1);

    addPage(QStringLiteral("常规"), createGeneralPage(),
            {QStringLiteral("preferences-system"), QStringLiteral("settings")});
    addPage(QStringLiteral("编辑器"), createEditorPage(),
            {QStringLiteral("document-edit"), QStringLiteral("accessories-text-editor")});
    addPage(QStringLiteral("新项目"), createProjectPage(),
            {QStringLiteral("document-new")});
    addPage(QStringLiteral("时间轴"), createTimelinePage(),
            {QStringLiteral("view-media-playlist"), QStringLiteral("media-playback-start")});
    addPage(QStringLiteral("素材"), createAssetsPage(),
            {QStringLiteral("folder")});
    addPage(QStringLiteral("解码"), createDecodePage(),
            {QStringLiteral("video-x-generic"), QStringLiteral("media-playback-start")});
    m_aboutIndex = addPage(QStringLiteral("关于"), createAboutPage(),
                           {QStringLiteral("help-about")});

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_pages->setCurrentIndex(row);
    });
    m_categories->setCurrentRow(0);

    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QStringLiteral("确定"));
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(buttons);
}

int SettingsDialog::addPage(const QString& title, QWidget* page,
                            const QStringList& iconNames)
{
    auto* item = new QListWidgetItem(themeIcon(iconNames), title, m_categories);
    item->setSizeHint(QSize(0, 30));
    m_pages->addWidget(page);
    return m_categories->row(item);
}

void SettingsDialog::showAboutPage()
{
    m_categories->setCurrentRow(m_aboutIndex);
}

QWidget* SettingsDialog::createGeneralPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* interface = new QGroupBox(QStringLiteral("界面"), page);
    auto* form = new QFormLayout(interface);
    m_language = new QComboBox(interface);
    m_language->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    const int index = m_language->findData(Settings::language());
    m_language->setCurrentIndex(index >= 0 ? index : 0);
    form->addRow(QStringLiteral("界面语言"), m_language);
    form->addRow(infoLabel(QStringLiteral("语言切换后需要重新启动 PVForge。"), interface));

    layout->addWidget(interface);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createEditorPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* snapping = new QGroupBox(QStringLiteral("吸附"), page);
    auto* snapForm = new QFormLayout(snapping);
    m_snapPixels = new QSpinBox(snapping);
    m_snapPixels->setRange(0, 40);
    m_snapPixels->setSuffix(QStringLiteral(" px"));
    m_snapPixels->setValue(Settings::snapPixels());
    m_snapPixels->setToolTip(QStringLiteral("0 表示关闭吸附"));
    snapForm->addRow(QStringLiteral("吸附距离"), m_snapPixels);
    m_edgeCenterSnap = new QCheckBox(QStringLiteral("实体边缘吸附到画面中心线"), snapping);
    m_edgeCenterSnap->setChecked(Settings::edgeCenterSnap());
    snapForm->addRow(m_edgeCenterSnap);
    m_edgeMidpointSnap =
        new QCheckBox(QStringLiteral("吸附其他实体的边缘中点"), snapping);
    m_edgeMidpointSnap->setChecked(Settings::edgeMidpointSnap());
    snapForm->addRow(m_edgeMidpointSnap);
    layout->addWidget(snapping);

    auto* rotation = new QGroupBox(QStringLiteral("旋转"), page);
    auto* rotationForm = new QFormLayout(rotation);
    auto* angleRow = new QWidget(rotation);
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
    rotationForm->addRow(QStringLiteral("吸附角度"), angleRow);
    layout->addWidget(rotation);

    auto* behaviour = new QGroupBox(QStringLiteral("关键帧与音频"), page);
    auto* behaviourForm = new QFormLayout(behaviour);
    m_autoKey = new QCheckBox(QStringLiteral("新项目默认开启自动关键帧"), behaviour);
    m_autoKey->setChecked(Settings::autoKeyDefault());
    behaviourForm->addRow(m_autoKey);
    m_audioScrub = new QCheckBox(QStringLiteral("拖动/逐帧时播放音频（音频擦洗）"), behaviour);
    m_audioScrub->setChecked(Settings::audioScrub());
    behaviourForm->addRow(m_audioScrub);
    layout->addWidget(behaviour);

    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createProjectPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* project = new QGroupBox(QStringLiteral("新项目默认值"), page);
    auto* form = new QFormLayout(project);
    m_width = new QSpinBox(project);
    m_width->setRange(16, 8192);
    m_width->setSuffix(QStringLiteral(" px"));
    m_width->setValue(Settings::defaultWidth());
    form->addRow(QStringLiteral("默认宽度"), m_width);
    m_height = new QSpinBox(project);
    m_height->setRange(16, 8192);
    m_height->setSuffix(QStringLiteral(" px"));
    m_height->setValue(Settings::defaultHeight());
    form->addRow(QStringLiteral("默认高度"), m_height);
    m_fps = new QSpinBox(project);
    m_fps->setRange(1, 240);
    m_fps->setSuffix(QStringLiteral(" fps"));
    m_fps->setValue(Settings::defaultFps());
    form->addRow(QStringLiteral("默认帧率"), m_fps);

    layout->addWidget(project);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createTimelinePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* timeline = new QGroupBox(QStringLiteral("时间轴"), page);
    auto* form = new QFormLayout(timeline);
    m_timelineZoom = new QDoubleSpinBox(timeline);
    m_timelineZoom->setRange(0.5, 32.0);
    m_timelineZoom->setDecimals(1);
    m_timelineZoom->setSuffix(QStringLiteral(" px/帧"));
    m_timelineZoom->setValue(Settings::timelinePixelsPerFrame());
    form->addRow(QStringLiteral("默认缩放"), m_timelineZoom);
    form->addRow(infoLabel(
        QStringLiteral("时间轴左上角的「秒 / 帧」按钮可以随时切换刻度单位。"), timeline));

    layout->addWidget(timeline);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAssetsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* assets = new QGroupBox(QStringLiteral("素材"), page);
    auto* form = new QFormLayout(assets);
    m_packAssets = new QCheckBox(QStringLiteral("新项目默认开启「全部打包到项目」"), assets);
    m_packAssets->setChecked(Settings::packAssetsDefault());
    form->addRow(m_packAssets);

    layout->addWidget(assets);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createDecodePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* decode = new QGroupBox(QStringLiteral("当前解码路径"), page);
    auto* decodeLayout = new QVBoxLayout(decode);
    decodeLayout->setSpacing(10);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    const DecoderInfo info = decoderInfo();
    int row = 0;
    auto addDecoderRow = [decode, grid, &row](const QString& title,
                                              const QString& value,
                                              const QString& objectName) {
        auto* name = new QLabel(title, decode);
        name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        name->setMinimumWidth(64);
        QLabel* field = infoLabel(value, decode);
        field->setObjectName(objectName);
        field->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(name, row, 0);
        grid->addWidget(field, row, 1);
        ++row;
    };
    addDecoderRow(QStringLiteral("解码方式"), info.mode,
                  QStringLiteral("decoderModeValue"));
    addDecoderRow(QStringLiteral("视频解码"), info.video,
                  QStringLiteral("decoderVideoValue"));
    addDecoderRow(QStringLiteral("音频解码"), info.audioDecode,
                  QStringLiteral("decoderAudioValue"));
    addDecoderRow(QStringLiteral("音频播放"), info.audioOutput,
                  QStringLiteral("decoderOutputValue"));
    addDecoderRow(QStringLiteral("硬件加速"), info.hardware,
                  QStringLiteral("decoderHardwareValue"));
    decodeLayout->addLayout(grid);

    decodeLayout->addWidget(infoLabel(
        QStringLiteral("这里是只读信息，显示当前构建实际使用的路径。"
                       "视频播放接入后再提供可切换的硬件解码选项。"),
        decode));

    auto* copy = new QPushButton(QStringLiteral("复制解码信息"), decode);
    copy->setObjectName(QStringLiteral("copyDecoderInfo"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(decoderInfo().toText());
    });
    decodeLayout->addWidget(copy);

    layout->addWidget(decode);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::createAboutPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* header = new QWidget(page);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* logo = new QLabel(header);
    logo->setPixmap(makeCubeIcon().pixmap(48, 48));
    logo->setFixedSize(48, 48);
    headerLayout->addWidget(logo);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("PVForge"), header);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titleLayout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("2D 音乐 PV 编辑器 · MIT License"), header);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout, 1);
    layout->addWidget(header);

    m_aboutText = new QPlainTextEdit(page);
    m_aboutText->setObjectName(QStringLiteral("aboutText"));
    m_aboutText->setReadOnly(true);
    m_aboutText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_aboutText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_aboutText->setPlainText(aboutText());
    layout->addWidget(m_aboutText, 1);

    auto* footer = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("复制信息"), page);
    copy->setObjectName(QStringLiteral("aboutCopy"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_aboutText->toPlainText());
    });
    footer->addWidget(copy);
    footer->addStretch(1);
    layout->addLayout(footer);

    return page;
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
    Settings::setDefaultSize(m_width->value(), m_height->value(), m_fps->value());
    Settings::setTimelinePixelsPerFrame(m_timelineZoom->value());
    Settings::setPackAssetsDefault(m_packAssets->isChecked());
    QDialog::accept();
}

} // namespace pvf
