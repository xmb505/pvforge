#include "SettingsDialog.h"

#include "SettingsDialogSupport.h"
#include "core/Settings.h"
#include "mcp/StdioBridge.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pvf {

using namespace settings_detail;

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

    auto* saving = new QGroupBox(QStringLiteral("保存"), page);
    auto* savingForm = new QFormLayout(saving);
    m_autosave = new QCheckBox(QStringLiteral("定期自动保存工程"), saving);
    m_autosave->setObjectName(QStringLiteral("autosaveEnabled"));
    m_autosave->setChecked(Settings::autosaveEnabled());
    savingForm->addRow(m_autosave);
    m_autosaveMinutes = new QSpinBox(saving);
    m_autosaveMinutes->setObjectName(QStringLiteral("autosaveMinutes"));
    m_autosaveMinutes->setRange(1, 120);
    m_autosaveMinutes->setSuffix(QStringLiteral(" 分钟"));
    m_autosaveMinutes->setValue(Settings::autosaveMinutes());
    m_autosaveMinutes->setEnabled(m_autosave->isChecked());
    connect(m_autosave, &QCheckBox::toggled, m_autosaveMinutes,
            &QWidget::setEnabled);
    savingForm->addRow(QStringLiteral("间隔"), m_autosaveMinutes);
    savingForm->addRow(infoLabel(
        QStringLiteral("只自动保存已经保存过路径的工程；未保存的新工程请先手动保存一次。"),
        saving));
    layout->addWidget(saving);

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

QWidget* SettingsDialog::createMcpPage()
{
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("mcpSettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto* service = new QGroupBox(QStringLiteral("MCP 服务"), content);
    auto* serviceForm = new QFormLayout(service);
    m_mcpEnabled = new QCheckBox(
        QStringLiteral("本次启动启用 MCP（重启后不会自动开启）"), service);
    m_mcpEnabled->setObjectName(QStringLiteral("mcpEnableCheck"));
    m_mcpEnabled->setToolTip(
        QStringLiteral("只有在这里手动启用后服务器才会监听；关闭设置页不会自动启动。"));
    connect(m_mcpEnabled, &QCheckBox::toggled, this,
            &SettingsDialog::applyMcpEnabled);
    serviceForm->addRow(m_mcpEnabled);

    auto* portRow = new QWidget(service);
    auto* portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(8);
    m_mcpAutoPort = new QCheckBox(QStringLiteral("自动选择端口"), portRow);
    m_mcpAutoPort->setObjectName(QStringLiteral("mcpAutoPort"));
    m_mcpAutoPort->setChecked(Settings::mcpPortAuto());
    m_mcpPort = new QSpinBox(portRow);
    m_mcpPort->setObjectName(QStringLiteral("mcpPortSpin"));
    m_mcpPort->setRange(1024, 65535);
    m_mcpPort->setValue(Settings::mcpPort());
    m_mcpPort->setEnabled(!m_mcpAutoPort->isChecked());
    portLayout->addWidget(m_mcpAutoPort);
    portLayout->addWidget(m_mcpPort);
    portLayout->addStretch(1);
    serviceForm->addRow(QStringLiteral("端口"), portRow);
    connect(m_mcpAutoPort, &QCheckBox::toggled, this,
            [this] { refreshMcpState(); });

    m_mcpStatus = infoLabel(QStringLiteral("未启用"), service);
    m_mcpStatus->setObjectName(QStringLiteral("mcpStatusLabel"));
    serviceForm->addRow(QStringLiteral("状态"), m_mcpStatus);

    m_mcpError = infoLabel(QString(), service);
    m_mcpError->setObjectName(QStringLiteral("mcpErrorLabel"));
    m_mcpError->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_mcpError->hide();
    serviceForm->addRow(m_mcpError);

    auto* buttons = new QWidget(service);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    m_mcpStart = new QPushButton(QStringLiteral("启动"), buttons);
    m_mcpStart->setObjectName(QStringLiteral("mcpStartButton"));
    m_mcpStop = new QPushButton(QStringLiteral("停止"), buttons);
    m_mcpStop->setObjectName(QStringLiteral("mcpStopButton"));
    buttonLayout->addWidget(m_mcpStart);
    buttonLayout->addWidget(m_mcpStop);
    buttonLayout->addStretch(1);
    serviceForm->addRow(buttons);
    connect(m_mcpStart, &QPushButton::clicked, this, [this] {
        if (m_mcpEnabled->isChecked())
            applyMcpEnabled(true);
        else
            m_mcpEnabled->setChecked(true);
    });
    connect(m_mcpStop, &QPushButton::clicked, this, [this] {
        if (!m_mcpEnabled->isChecked())
            applyMcpEnabled(false);
        else
            m_mcpEnabled->setChecked(false);
    });
    layout->addWidget(service);

    auto* tokenGroup = new QGroupBox(QStringLiteral("访问令牌"), content);
    auto* tokenLayout = new QVBoxLayout(tokenGroup);
    auto* tokenRow = new QWidget(tokenGroup);
    auto* tokenRowLayout = new QHBoxLayout(tokenRow);
    tokenRowLayout->setContentsMargins(0, 0, 0, 0);
    tokenRowLayout->setSpacing(8);
    m_mcpToken = new QLineEdit(tokenRow);
    m_mcpToken->setObjectName(QStringLiteral("mcpTokenEdit"));
    m_mcpToken->setReadOnly(true);
    m_mcpToken->setPlaceholderText(QStringLiteral("服务器未配置"));
    m_mcpCopyToken = new QPushButton(QStringLiteral("复制"), tokenRow);
    m_mcpCopyToken->setObjectName(QStringLiteral("mcpCopyTokenButton"));
    m_mcpResetToken = new QPushButton(QStringLiteral("重置"), tokenRow);
    m_mcpResetToken->setObjectName(QStringLiteral("mcpResetTokenButton"));
    m_mcpResetToken->setToolTip(QStringLiteral("生成新的随机令牌，已连接的客户端需更新配置"));
    tokenRowLayout->addWidget(m_mcpToken, 1);
    tokenRowLayout->addWidget(m_mcpCopyToken);
    tokenRowLayout->addWidget(m_mcpResetToken);
    tokenLayout->addWidget(tokenRow);
    tokenLayout->addWidget(infoLabel(
        QStringLiteral("令牌只保存在本次进程内；日志和状态文本不会回显令牌。"), tokenGroup));
    connect(m_mcpCopyToken, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpToken);
    connect(m_mcpResetToken, &QPushButton::clicked, this,
            &SettingsDialog::resetMcpToken);
    layout->addWidget(tokenGroup);

    auto* permissionGroup = new QGroupBox(QStringLiteral("权限模式"), content);
    auto* permissionForm = new QFormLayout(permissionGroup);
    m_mcpPermission = new QComboBox(permissionGroup);
    m_mcpPermission->setObjectName(QStringLiteral("mcpPermissionCombo"));
    m_mcpPermission->addItem(QStringLiteral("只读"));
    m_mcpPermission->addItem(QStringLiteral("允许修改"));
    m_mcpPermission->setCurrentIndex(0);
    m_mcpPermission->setToolTip(
        QStringLiteral("“允许修改”需要工程已保存且播放头位于一个场景内；"
                       "解锁前会先创建检查点。"));
    connect(m_mcpPermission, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index == 1)
                    emit mcpPermissionArmRequested();
                else
                    emit mcpPermissionDisarmRequested();
            });
    permissionForm->addRow(QStringLiteral("模式"), m_mcpPermission);
    m_mcpPermissionStatus = infoLabel(
        QStringLiteral("只读：AI 只能读取工程，写工具返回 permission_denied。"),
        permissionGroup);
    m_mcpPermissionStatus->setObjectName(
        QStringLiteral("mcpPermissionStatusLabel"));
    permissionForm->addRow(m_mcpPermissionStatus);
    layout->addWidget(permissionGroup);

    auto* logGroup = new QGroupBox(QStringLiteral("日志窗"), content);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_mcpLogEnabled =
        new QCheckBox(QStringLiteral("显示 MCP 日志窗（首次 AI 调用自动展开）"),
                      logGroup);
    m_mcpLogEnabled->setObjectName(QStringLiteral("mcpLogEnabled"));
    m_mcpLogEnabled->setChecked(Settings::mcpLogVisible());
    logLayout->addWidget(m_mcpLogEnabled);
    logLayout->addWidget(infoLabel(
        QStringLiteral("关闭日志窗后仍可从状态栏的 MCP 入口重新打开；日志只保存在内存中。"),
        logGroup));
    layout->addWidget(logGroup);

    auto* configGroup = new QGroupBox(QStringLiteral("客户端配置"), content);
    auto* configLayout = new QVBoxLayout(configGroup);
    configLayout->addWidget(infoLabel(
        QStringLiteral("远程 Streamable HTTP + Bearer 认证，只监听 127.0.0.1。"
                       "只支持 stdio 的客户端需要 pvforge-mcp 桥接进程。"),
        configGroup));
    m_mcpCopyConfig = new QPushButton(
        QStringLiteral("复制 OpenCode / DeepSeek 远程配置"), configGroup);
    m_mcpCopyConfig->setObjectName(QStringLiteral("mcpCopyConfigButton"));
    connect(m_mcpCopyConfig, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpConfig);
    configLayout->addWidget(m_mcpCopyConfig);
    m_mcpCopyStdioConfig = new QPushButton(
        QStringLiteral("复制 pvforge-mcp stdio 配置"), configGroup);
    m_mcpCopyStdioConfig->setObjectName(
        QStringLiteral("mcpCopyStdioConfigButton"));
    connect(m_mcpCopyStdioConfig, &QPushButton::clicked, this,
            &SettingsDialog::copyMcpStdioConfig);
    configLayout->addWidget(m_mcpCopyStdioConfig);
    configLayout->addWidget(infoLabel(
        QStringLiteral("远程配置使用当前 URL 与令牌；stdio 配置引用本机的 "
                       "pvforge-mcp 桥接进程，由它读取端点文件。"),
        configGroup));
    layout->addWidget(configGroup);

    layout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    refreshMcpState();
    return page;
}

void SettingsDialog::applyMcpEnabled(bool enabled)
{
    Q_UNUSED(enabled);
    refreshMcpState();
    emit mcpStateChanged(false, 0, QString());
}

void SettingsDialog::refreshMcpState()
{
    const bool hasServer = false;
    const bool running = false;

    {
        const QSignalBlocker blocker(m_mcpEnabled);
        m_mcpEnabled->setChecked(running);
    }
    m_mcpEnabled->setEnabled(hasServer);
    m_mcpAutoPort->setEnabled(hasServer && !running);
    m_mcpPort->setEnabled(hasServer && !running && !m_mcpAutoPort->isChecked());
    m_mcpStart->setEnabled(hasServer && !running);
    m_mcpStop->setEnabled(hasServer && running);
    m_mcpCopyToken->setEnabled(hasServer);
    m_mcpResetToken->setEnabled(hasServer);
    m_mcpCopyConfig->setEnabled(hasServer);
    m_mcpCopyStdioConfig->setEnabled(hasServer);
    m_mcpPermission->setEnabled(hasServer);

    if (hasServer)
    {
        m_mcpToken->setEnabled(true);
    }
    else
    {
        m_mcpToken->clear();
        m_mcpToken->setEnabled(false);
    }

    if (!hasServer)
    {
        m_mcpStatus->setText(QStringLiteral("未连接到主窗口"));
    }
    else if (running)
    {
    }
    else if (!m_mcpError->text().isEmpty())
    {
        m_mcpStatus->setText(QStringLiteral("启动失败"));
    }
    else
    {
        m_mcpStatus->setText(QStringLiteral("未启用"));
    }
}

void SettingsDialog::copyMcpToken()
{
}

void SettingsDialog::resetMcpToken()
{
    refreshMcpState();
}

void SettingsDialog::setMcpPermissionState(bool armed, const QString& sceneName,
                                           const QString& error)
{
    if (m_mcpPermission)
    {
        const QSignalBlocker blocker(m_mcpPermission);
        m_mcpPermission->setCurrentIndex(armed ? 1 : 0);
    }
    if (m_mcpPermissionStatus)
    {
        if (armed)
        {
            m_mcpPermissionStatus->setText(
                QStringLiteral("已武装：%1。写操作仅限该场景根文件夹，急停按钮可随时"
                               "停止。")
                    .arg(sceneName.isEmpty() ? QStringLiteral("当前场景") : sceneName));
        }
        else
        {
            m_mcpPermissionStatus->setText(
                QStringLiteral("只读：AI 只能读取工程，写工具返回 "
                               "permission_denied。"));
        }
    }
    if (!error.isEmpty() && m_mcpError)
    {
        m_mcpError->setText(QStringLiteral("解锁失败：%1").arg(error));
        m_mcpError->show();
    }
    else if (error.isEmpty() && m_mcpError
    )
    {
        m_mcpError->clear();
        m_mcpError->hide();
    }
}

QString SettingsDialog::mcpStdioConfigJson() const
{
    QJsonObject server;
    server[QStringLiteral("command")] = QStringLiteral(PVFORGE_MCP_BINARY);
    server[QStringLiteral("args")] =
        QJsonArray{QStringLiteral("--endpoint-file"),
                   defaultMcpEndpointPath()};

    QJsonObject root;
    root[QStringLiteral("mcpServers")] =
        QJsonObject{{QStringLiteral("pvforge"), server}};
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void SettingsDialog::copyMcpStdioConfig()
{
    QApplication::clipboard()->setText(mcpStdioConfigJson());
}

QString SettingsDialog::mcpConfigJson() const
{
    QString url;
    if (m_mcpAutoPort->isChecked())
        url = QStringLiteral("http://127.0.0.1:PORT/mcp");
    else
        url = QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_mcpPort->value());

    const QString token;
    QJsonObject server;
    server[QStringLiteral("type")] = QStringLiteral("streamable-http");
    server[QStringLiteral("url")] = url;
    server[QStringLiteral("headers")] =
        QJsonObject{{QStringLiteral("Authorization"),
                     QStringLiteral("Bearer %1").arg(token)}};

    QJsonObject root;
    root[QStringLiteral("mcpServers")] =
        QJsonObject{{QStringLiteral("pvforge"), server}};
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void SettingsDialog::copyMcpConfig()
{
    QApplication::clipboard()->setText(mcpConfigJson());
}

} // namespace pvf
