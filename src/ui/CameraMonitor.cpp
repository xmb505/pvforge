#include "CameraMonitor.h"

#include "core/Camera.h"
#include "core/Settings.h"
#include "media/VideoDecoder.h"
#include "media/VideoPlayer.h"
#include "media/VideoProxy.h"
#include "render/CameraRenderer.h"
#include "ui/Viewport.h"

#include <QAbstractSocket>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {
constexpr const char* kRtpHost = "239.255.42.99";
constexpr int kRtpPort = 5004;
constexpr int kDefaultStreamLongEdge = 960;
constexpr int kDefaultStreamBitrate = 5000000;

QString rtpUrl()
{
    return QStringLiteral("rtp://@%1:%2").arg(QLatin1String(kRtpHost)).arg(kRtpPort);
}

QSize streamSize(const Project& project, int longEdge)
{
    int width = longEdge;
    int height = int(std::lround(width * project.height
                                 / double(std::max(1, project.width))));
    if (project.height > project.width)
    {
        height = longEdge;
        width = int(std::lround(height * project.width
                                / double(std::max(1, project.height))));
    }
    return QSize(std::max(2, width & ~1), std::max(2, height & ~1));
}
} // namespace

CameraMonitor::CameraMonitor(Project* project, Viewport* viewport, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_viewport(viewport)
{
    setObjectName(QStringLiteral("cameraMonitor"));
    setMinimumSize(380, 150);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("摄像机监视器"));

    m_highQuality = Settings::monitorHighQuality();

    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setObjectName(QStringLiteral("monitorCameraCombo"));
    m_cameraCombo->setFocusPolicy(Qt::NoFocus);
    connect(m_cameraCombo, &QComboBox::currentIndexChanged, this, [this](int) {        if (!m_cameraCombo || m_cameraCombo->currentIndex() < 0)
            return;
        const int view = m_cameraCombo->currentData().toInt();
        m_project->monitorView = view;
        if (view >= 0)
        {
            const Entity* camera = m_project->entity(view);
            if (camera && camera->isCamera()
                && !m_project->isEffectivelyHidden(view))
            {
                m_project->activeCamera = view;
                emit cameraActivated(view);
            }
        }
        update();
    });

    m_qualityButton = new QToolButton(this);
    m_qualityButton->setObjectName(QStringLiteral("monitorQualityButton"));
    m_qualityButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_qualityButton->setFocusPolicy(Qt::NoFocus);
    connect(m_qualityButton, &QToolButton::clicked, this, &CameraMonitor::toggleQuality);

    m_fpsLabel = new QLabel(this);
    m_fpsLabel->setObjectName(QStringLiteral("monitorFpsLabel"));
    m_fpsLabel->setAlignment(Qt::AlignCenter);
    m_fpsLabel->setToolTip(
        QStringLiteral("项目帧率；播放时显示监视器实际刷新帧率"));
    m_fpsLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont fpsFont = m_fpsLabel->font();
    fpsFont.setPointSizeF(std::max(7.0, fpsFont.pointSizeF() - 1.0));
    m_fpsLabel->setFont(fpsFont);

    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setInterval(1000);
    connect(m_fpsTimer, &QTimer::timeout, this, [this] {
        if (!m_playing)
            return;
        const qint64 elapsed = m_fpsClock.restart();
        m_displayFps = elapsed > 0 ? int(std::lround(m_fpsFrames * 1000.0 / elapsed))
                                   : 0;
        m_fpsFrames = 0;
        updateFpsLabel();
    });

    m_pinButton = new QToolButton(this);
    m_pinButton->setObjectName(QStringLiteral("monitorPinButton"));
    m_pinButton->setText(QStringLiteral("置顶"));
    m_pinButton->setCheckable(true);
    m_pinButton->setFocusPolicy(Qt::NoFocus);
    m_pinButton->setChecked(Settings::monitorAlwaysOnTop());
    connect(m_pinButton, &QToolButton::toggled, this, [this](bool on) {
        Settings::setMonitorAlwaysOnTop(on);
        applyAlwaysOnTop();
    });

    m_detachButton = new QToolButton(this);
    m_detachButton->setObjectName(QStringLiteral("monitorDetachButton"));
    m_detachButton->setFocusPolicy(Qt::NoFocus);
    connect(m_detachButton, &QToolButton::clicked, this, [this] {
        if (m_detached)
            dockWindow();
        else
            detachWindow();
    });

    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName(QStringLiteral("monitorCloseButton"));
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    connect(m_closeButton, &QToolButton::clicked, this, [this] {
        if (m_detached)
        {
            dockWindow();
            return;
        }
        hide();
        Settings::setMonitorVisible(false);
        emit visibilityChanged(false);
    });

    m_traceButton = new QToolButton(this);
    m_traceButton->setObjectName(QStringLiteral("monitorTraceButton"));
    m_traceButton->setText(QStringLiteral("临摹"));
    m_traceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_traceButton->setFocusPolicy(Qt::NoFocus);
    m_traceButton->setToolTip(QStringLiteral("临摹参考视频"));
    buildTraceMenu();
    connect(m_traceButton, &QToolButton::clicked, this, [this] {
        m_traceMenu->exec(
            m_traceButton->mapToGlobal(QPoint(0, m_traceButton->height())));
    });

    m_streamButton = new QToolButton(this);
    m_streamButton->setObjectName(QStringLiteral("monitorStreamButton"));
    m_streamButton->setText(QStringLiteral("串流"));
    m_streamButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_streamButton->setFocusPolicy(Qt::NoFocus);
    m_streamButton->setToolTip(QStringLiteral("RTP 串流到手机 VLC"));
    buildStreamMenu();
    connect(m_streamButton, &QToolButton::clicked, this, [this] {
        m_streamMenu->exec(
            m_streamButton->mapToGlobal(QPoint(0, m_streamButton->height())));
    });

    m_streamTimer = new QTimer(this);
    connect(m_streamTimer, &QTimer::timeout, this, &CameraMonitor::onStreamTick);

    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection, this,
            &CameraMonitor::onHttpNewConnection);

    rebuildCameraCombo();
    updateButtons();
    updateFpsLabel();
    resize(Settings::monitorDockGeometry().size());
}

CameraMonitor::~CameraMonitor()
{
    if (m_streamTimer)
        m_streamTimer->stop();
    if (m_streamProcess)
    {
        m_streamProcess->kill();
        m_streamProcess->waitForFinished(500);
    }
    if (m_httpServer)
        m_httpServer->close();
    for (QTcpSocket* socket : m_httpClients)
    {
        socket->abort();
        socket->deleteLater();
    }
    m_httpClients.clear();
}

void CameraMonitor::setFrame(int frame)
{
    m_frame = std::max(0, frame);
    updateTraceImage();
    if (isVisible())
        update();
}

void CameraMonitor::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (m_playing)
    {
        m_fpsFrames = 0;
        m_displayFps = 0;
        m_fpsClock.restart();
        m_fpsTimer->start();
    }
    else
    {
        m_fpsTimer->stop();
        m_displayFps = 0;
    }
    updateFpsLabel();
    update();
}

void CameraMonitor::setProjectChanged()
{
    rebuildCameraCombo();
    if (!m_playing)
        updateFpsLabel();
    update();
}

void CameraMonitor::syncCameraView()
{
    rebuildCameraCombo();
    update();
}

void CameraMonitor::setVisibleFromMenu(bool visible)
{
    Settings::setMonitorVisible(visible);
    if (visible)
    {
        show();
        raise();
    }
    else
    {
        if (m_detached)
            dockWindow();
        hide();
    }
    emit visibilityChanged(visible);
}

void CameraMonitor::updateFromViewport()
{
    if (!m_detached)
        clampDockGeometry();
    update();
}

void CameraMonitor::rebuildCameraCombo()
{
    if (!m_cameraCombo)
        return;
    const QSignalBlocker blocker(m_cameraCombo);
    m_cameraCombo->clear();
    m_cameraCombo->addItem(QStringLiteral("工作摄像机"), kWorkCameraView);
    m_cameraCombo->addItem(QStringLiteral("整画布"), kWholeCanvasView);
    for (const Entity& entity : m_project->entities())
        if (entity.isCamera() && !m_project->isEffectivelyHidden(entity.id))
            m_cameraCombo->addItem(entity.name, entity.id);

    int index = m_cameraCombo->findData(m_project->monitorView);
    if (index < 0)
    {
        index = 0;
        m_project->monitorView = kWorkCameraView;
    }
    m_cameraCombo->setCurrentIndex(index);
}

void CameraMonitor::updateButtons()
{
    m_qualityButton->setText(m_highQuality ? QStringLiteral("高质量")
                                          : QStringLiteral("草稿"));
    m_qualityButton->setToolTip(m_highQuality
                                    ? QStringLiteral("高质量：原图、项目分辨率；播放时降为草稿")
                                    : QStringLiteral("草稿：缩略图、监视器分辨率"));
    m_detachButton->setText(m_detached ? QStringLiteral("停靠")
                                       : QStringLiteral("弹出"));
    m_detachButton->setToolTip(m_detached ? QStringLiteral("停靠回视口右下角")
                                          : QStringLiteral("弹出为独立窗口"));
    m_pinButton->setVisible(m_detached);
    m_pinButton->setChecked(Settings::monitorAlwaysOnTop());
    m_pinButton->setToolTip(QStringLiteral("独立窗口总在最前"));
}

void CameraMonitor::updateFpsLabel()
{
    if (!m_fpsLabel)
        return;
    const int projectFps = std::max(1, m_project->fps);
    const int shown = m_playing && m_displayFps > 0 ? m_displayFps : projectFps;
    m_fpsLabel->setText(QStringLiteral("%1 FPS").arg(shown));
    QPalette pal = m_fpsLabel->palette();
    QColor color = pal.color(QPalette::PlaceholderText);
    if (m_playing && m_displayFps > 0 && m_displayFps + 1 < projectFps)
        color = QColor(235, 110, 80);
    pal.setColor(QPalette::WindowText, color);
    m_fpsLabel->setPalette(pal);
    m_fpsLabel->setToolTip(
        m_playing && m_displayFps > 0
            ? QStringLiteral("播放实际：%1 FPS\n项目帧率：%2 fps")
                  .arg(m_displayFps)
                  .arg(projectFps)
            : QStringLiteral("项目帧率：%1 fps").arg(projectFps));
}

QRect CameraMonitor::titleRect() const
{
    return QRect(0, 0, width(), kTitleHeight);
}

QRect CameraMonitor::imageRect() const
{
    return QRect(0, kTitleHeight, width(), std::max(0, height() - kTitleHeight));
}

QRect CameraMonitor::gripRect() const
{
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

QRect CameraMonitor::fitRect() const
{
    const QRect area = imageRect();
    if (area.isEmpty())
        return QRect();

    double aspect = m_project->width / double(std::max(1, m_project->height));
    if (m_project->monitorView == kWorkCameraView && m_viewport)
        aspect = m_viewport->width() / double(std::max(1, m_viewport->height()));

    int w = area.width();
    int h = int(std::lround(w / std::max(0.0001, aspect)));
    if (h > area.height())
    {
        h = area.height();
        w = int(std::lround(h * aspect));
    }
    w = std::max(1, w);
    h = std::max(1, h);
    return QRect(area.x() + (area.width() - w) / 2,
                 area.y() + (area.height() - h) / 2, w, h);
}

void CameraMonitor::layoutChildren()
{
    const int comboLeft = 22;
    int right = width() - 4;

    auto place = [&](QToolButton* button, int buttonWidth) {
        button->setGeometry(right - buttonWidth, 3, buttonWidth, 22);
        right -= buttonWidth + 2;
    };

    place(m_closeButton, kButtonWidth);
    place(m_detachButton, 40);
    if (m_detached)
        place(m_pinButton, 40);
    place(m_qualityButton, 54);
    place(m_streamButton, 38);
    place(m_traceButton, 38);
    m_fpsLabel->setGeometry(right - 42, 5, 42, 18);
    right -= 46;

    const int comboWidth = std::max(60, right - comboLeft);
    m_cameraCombo->setGeometry(comboLeft, 3, comboWidth, 22);
}

void CameraMonitor::paintEvent(QPaintEvent*)
{
    if (m_playing)
        ++m_fpsFrames;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = QApplication::palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));
    painter.fillRect(titleRect(), pal.color(QPalette::AlternateBase));
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.drawLine(0, kTitleHeight - 1, width(), kTitleHeight - 1);

    // Eight-dot drag grip on the left of the title bar.
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.color(QPalette::PlaceholderText));
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 2; ++column)
        {
            painter.drawRect(QRect(6 + column * 5, 8 + row * 5, 2, 2));
        }
    }

    const QRect image = imageRect();
    painter.fillRect(image, Qt::black);
    painter.fillRect(fitRect(), Qt::black);

    renderPreview(painter);
    renderTraceOverlay(painter);

    // The grip is drawn above the image so it stays visible.
    painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    for (int i = 0; i < 3; ++i)
    {
        const int offset = 3 + i * 4;
        painter.drawLine(width() - offset, height() - 2, width() - 2, height() - offset);
    }
}

void CameraMonitor::renderPreview(QPainter& painter)
{
    const QRect target = fitRect();
    if (target.isEmpty())
        return;

    const bool high = m_highQuality && !m_playing;
    const int view = m_project->monitorView;

    if (view == kWorkCameraView && m_viewport)
    {
        const QColor outside = m_viewport->palette().color(QPalette::Window);
        CameraRenderer::renderWorkCamera(painter, *m_project, m_frame, target,
                                         m_viewport->worldToScreenForTest(),
                                         m_viewport->size(), outside, !high);
        return;
    }

    Camera::State state;
    if (view >= 0)
    {
        const Entity* camera = m_project->entity(view);
        if (camera && camera->isCamera()
            && !m_project->isEffectivelyHidden(camera->id))
        {
            state = Camera::state(*m_project, camera->id, m_frame);
        }
    }
    if (state.size.isEmpty())
    {
        state.scale = 1.0;
        state.rotation = 0.0;
        state.center = QPointF();
        state.size = QSizeF(m_project->width, m_project->height);
    }

    if (high)
    {
        const QImage image = CameraRenderer::renderCameraImage(
            *m_project, m_frame, state, QSize(m_project->width, m_project->height), false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image);
    }
    else
    {
        CameraRenderer::renderCamera(painter, *m_project, m_frame, target, state, true);
    }
}


void CameraMonitor::buildTraceMenu()
{
    m_traceMenu = new QMenu(this);
    m_traceNameAction = m_traceMenu->addAction(QStringLiteral("未导入临摹视频"));
    m_traceNameAction->setEnabled(false);
    QAction* import = m_traceMenu->addAction(QStringLiteral("导入临摹视频…"));
    connect(import, &QAction::triggered, this, &CameraMonitor::importTraceVideo);
    m_clearTraceAction = m_traceMenu->addAction(QStringLiteral("清除临摹"));
    connect(m_clearTraceAction, &QAction::triggered, this,
            &CameraMonitor::clearTraceVideo);
    m_traceMenu->addSeparator();

    auto* opacityWidget = new QWidget(m_traceMenu);
    opacityWidget->setMinimumWidth(190);
    auto* opacityLayout = new QHBoxLayout(opacityWidget);
    opacityLayout->setContentsMargins(8, 4, 8, 4);
    opacityLayout->setSpacing(6);
    opacityLayout->addWidget(new QLabel(QStringLiteral("透明度"), opacityWidget));
    m_traceOpacitySlider = new QSlider(Qt::Horizontal, opacityWidget);
    m_traceOpacitySlider->setRange(0, 100);
    m_traceOpacitySlider->setValue(int(std::lround(m_traceOpacity * 100.0)));
    opacityLayout->addWidget(m_traceOpacitySlider, 1);
    m_traceOpacityLabel = new QLabel(QStringLiteral("45%"), opacityWidget);
    m_traceOpacityLabel->setMinimumWidth(32);
    opacityLayout->addWidget(m_traceOpacityLabel);
    auto* sliderAction = new QWidgetAction(m_traceMenu);
    sliderAction->setDefaultWidget(opacityWidget);
    m_traceMenu->addAction(sliderAction);

    connect(m_traceOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_traceOpacity = value / 100.0;
        m_traceOpacityLabel->setText(QStringLiteral("%1%").arg(value));
        update();
    });
    connect(m_traceMenu, &QMenu::aboutToShow, this, &CameraMonitor::updateTraceMenu);
    updateTraceMenu();
}

void CameraMonitor::updateTraceMenu()
{
    if (!m_traceNameAction || !m_clearTraceAction)
        return;
    m_traceNameAction->setText(
        m_tracePath.isEmpty() ? QStringLiteral("未导入临摹视频")
                              : QFileInfo(m_tracePath).fileName());
    m_clearTraceAction->setEnabled(!m_tracePath.isEmpty());
    m_traceButton->setToolTip(
        m_tracePath.isEmpty()
            ? QStringLiteral("临摹参考视频")
            : QStringLiteral("临摹：%1").arg(QFileInfo(m_tracePath).fileName()));
    if (m_traceOpacitySlider)
    {
        const QSignalBlocker blocker(m_traceOpacitySlider);
        m_traceOpacitySlider->setValue(int(std::lround(m_traceOpacity * 100.0)));
        m_traceOpacityLabel->setText(
            QStringLiteral("%1%").arg(m_traceOpacitySlider->value()));
    }
}

void CameraMonitor::importTraceVideo()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入临摹视频"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.webm *.mov *.mkv *.avi *.m4v);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (!loadTraceVideo(path))
        QMessageBox::warning(this, QStringLiteral("无法导入临摹视频"),
                             QStringLiteral("无法读取这个视频文件。"));
}

bool CameraMonitor::loadTraceVideo(const QString& path)
{
    QString error;
    double duration = 0.0;
    const QImage first = decodeVideoFirstFrame(path, &error, &duration);
    if (first.isNull())
        return false;

    m_traceProject = std::make_unique<Project>();
    m_traceProject->fps = std::max(1, m_project->fps);
    m_traceProject->width = m_project->width;
    m_traceProject->height = m_project->height;
    Entity* entity =
        m_traceProject->addVideo(path, first, duration, QStringLiteral("Trace"));
    if (!entity)
    {
        m_traceProject.reset();
        return false;
    }

    m_traceEntity = entity->id;
    m_tracePath = path;
    m_tracePlayer = std::make_unique<VideoPlayer>();
    m_traceImage = first;

    if (!m_traceProxy)
    {
        m_traceProxy = new VideoProxyGenerator(this);
        connect(m_traceProxy, &VideoProxyGenerator::finished, this,
                [this](const QString& source, bool success) {
                    if (!success || source != m_tracePath || !m_tracePlayer)
                        return;
                    m_tracePlayer->clear();
                    updateTraceImage();
                    update();
                });
    }
    m_traceProxy->ensure(path);

    updateTraceImage();
    updateTraceMenu();
    update();
    return true;
}

void CameraMonitor::clearTraceVideo()
{
    m_tracePlayer.reset();
    m_traceProject.reset();
    m_tracePath.clear();
    m_traceEntity = kInvalidEntity;
    m_traceImage = QImage();
    updateTraceMenu();
    update();
}

void CameraMonitor::updateTraceImage()
{
    if (!m_traceProject || !m_tracePlayer || m_traceEntity == kInvalidEntity)
    {
        m_traceImage = QImage();
        return;
    }
    m_traceProject->fps = std::max(1, m_project->fps);
    m_tracePlayer->update(*m_traceProject, m_frame);
    const Entity* entity = m_traceProject->entity(m_traceEntity);
    m_traceImage = entity ? entity->image : QImage();
}

void CameraMonitor::setTraceOpacity(double opacity)
{
    m_traceOpacity = std::clamp(opacity, 0.0, 1.0);
    if (m_traceOpacitySlider)
    {
        const QSignalBlocker blocker(m_traceOpacitySlider);
        m_traceOpacitySlider->setValue(int(std::lround(m_traceOpacity * 100.0)));
        m_traceOpacityLabel->setText(
            QStringLiteral("%1%").arg(m_traceOpacitySlider->value()));
    }
    update();
}

void CameraMonitor::renderTraceOverlay(QPainter& painter)
{
    if (m_traceImage.isNull() || m_traceOpacity <= 0.001)
        return;
    const QRect target = fitRect();
    if (target.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(m_traceOpacity);
    painter.drawImage(target, m_traceImage);
    painter.restore();
}

void CameraMonitor::buildStreamMenu()
{
    m_streamMenu = new QMenu(this);
    m_streamAction = m_streamMenu->addAction(QStringLiteral("开始串流"));
    connect(m_streamAction, &QAction::triggered, this, [this] {
        if (m_streaming)
            stopStreaming();
        else
            startStreaming();
    });
    m_streamMenu->addSeparator();

    QAction* modeHeader =
        m_streamMenu->addAction(QStringLiteral("串流方式"));
    modeHeader->setEnabled(false);
    m_httpModeAction =
        m_streamMenu->addAction(QStringLiteral("HTTP 局域网（本机 IP）"));
    m_httpModeAction->setCheckable(true);
    m_rtpModeAction =
        m_streamMenu->addAction(QStringLiteral("RTP 组播（无需本机 IP）"));
    m_rtpModeAction->setCheckable(true);
    connect(m_httpModeAction, &QAction::triggered, this, [this] {
        if (m_streaming)
            return;
        m_streamMode = StreamMode::Http;
        updateStreamMenu();
    });
    connect(m_rtpModeAction, &QAction::triggered, this, [this] {
        if (m_streaming)
            return;
        m_streamMode = StreamMode::Rtp;
        updateStreamMenu();
    });

    m_streamMenu->addSeparator();
    m_qualityMenu = m_streamMenu->addMenu(QStringLiteral("画质"));
    auto addQuality = [this](const QString& text, StreamQuality quality,
                             QAction** out) {
        QAction* action = m_qualityMenu->addAction(text);
        action->setCheckable(true);
        *out = action;
        connect(action, &QAction::triggered, this, [this, quality] {
            if (m_streaming)
                return;
            m_streamQuality = quality;
            updateStreamMenu();
        });
    };
    addQuality(QStringLiteral("流畅 640 · 2.5 Mbps"), StreamQuality::Smooth,
               &m_qualitySmoothAction);
    addQuality(QStringLiteral("标准 960 · 5 Mbps"), StreamQuality::Standard,
               &m_qualityStandardAction);
    addQuality(QStringLiteral("高清 1280 · 8 Mbps"), StreamQuality::High,
               &m_qualityHighAction);

    m_codecMenu = m_streamMenu->addMenu(QStringLiteral("编码"));
    auto addCodec = [this](const QString& text, StreamCodec codec,
                           QAction** out) {
        QAction* action = m_codecMenu->addAction(text);
        action->setCheckable(true);
        *out = action;
        connect(action, &QAction::triggered, this, [this, codec] {
            if (m_streaming)
                return;
            m_streamCodec = codec;
            updateStreamMenu();
        });
    };
    addCodec(QStringLiteral("H.264 软件（兼容）"), StreamCodec::H264Software,
             &m_h264SoftwareAction);
    addCodec(QStringLiteral("H.265 软件（更清晰）"), StreamCodec::H265Software,
             &m_h265SoftwareAction);
    m_h265SoftwareAction->setToolTip(QStringLiteral("手机 VLC 需要支持 HEVC"));
    if (vaapiAvailable())
    {
        addCodec(QStringLiteral("H.264 硬件（VAAPI）"), StreamCodec::H264Vaapi,
                 &m_h264VaapiAction);
        addCodec(QStringLiteral("H.265 硬件（VAAPI）"), StreamCodec::H265Vaapi,
                 &m_h265VaapiAction);
        m_h265VaapiAction->setToolTip(QStringLiteral("手机 VLC 需要支持 HEVC"));
    }

    m_streamMenu->addSeparator();
    m_streamAddressAction = m_streamMenu->addAction(QString());
    m_streamAddressAction->setEnabled(false);
    m_copyStreamAction = m_streamMenu->addAction(QStringLiteral("复制串流地址"));
    connect(m_copyStreamAction, &QAction::triggered, this, [this] {
        QApplication::clipboard()->setText(currentStreamUrl());
    });

    m_httpIpMenu = m_streamMenu->addMenu(QStringLiteral("本机 IP 地址"));
    connect(m_httpIpMenu, &QMenu::aboutToShow, this, [this] {
        m_httpIpMenu->clear();
        const QStringList addresses = localIpAddresses();
        const int port = m_httpPort > 0 ? m_httpPort : 8080;
        if (addresses.isEmpty())
        {
            QAction* none = m_httpIpMenu->addAction(QStringLiteral("没有局域网 IP"));
            none->setEnabled(false);
            return;
        }
        for (const QString& ip : addresses)
        {
            const QString url =
                QStringLiteral("http://%1:%2/stream.ts").arg(ip).arg(port);
            QAction* action = m_httpIpMenu->addAction(url);
            connect(action, &QAction::triggered, this, [this, url] {
                m_httpUrl = url;
                QApplication::clipboard()->setText(url);
                updateStreamMenu();
            });
        }
    });

    m_streamMenu->addSeparator();
    QAction* note =
        m_streamMenu->addAction(QStringLiteral("手机 VLC：网络串流 → 粘贴地址（同一 Wi-Fi）"));
    note->setEnabled(false);

    updateStreamMenu();
}

QStringList CameraMonitor::localIpAddresses() const
{
    QStringList addresses;
    const auto all = QNetworkInterface::allAddresses();
    for (const QHostAddress& address : all)
    {
        if (address.protocol() != QAbstractSocket::IPv4Protocol
            || address.isLoopback())
            continue;
        const QString ip = address.toString();
        if (ip.startsWith(QStringLiteral("169.254.")))
            continue;
        if (!addresses.contains(ip))
            addresses.append(ip);
    }
    return addresses;
}

QString CameraMonitor::preferredLocalIp() const
{
    const QStringList addresses = localIpAddresses();
    for (const QString& prefix : {QStringLiteral("192.168."), QStringLiteral("10."),
                                  QStringLiteral("172.")})
    {
        for (const QString& ip : addresses)
            if (ip.startsWith(prefix))
                return ip;
    }
    return addresses.isEmpty() ? QStringLiteral("127.0.0.1") : addresses.first();
}

QString CameraMonitor::currentStreamUrl() const
{
    if (m_streamMode == StreamMode::Rtp)
        return rtpUrl();
    if (!m_httpUrl.isEmpty())
        return m_httpUrl;
    return QStringLiteral("http://%1:%2/stream.ts")
        .arg(preferredLocalIp())
        .arg(8080);
}

void CameraMonitor::updateStreamMenu()
{
    if (!m_streamAction)
        return;
    m_streamAction->setText(m_streaming ? QStringLiteral("停止串流")
                                       : QStringLiteral("开始串流"));
    m_httpModeAction->setChecked(m_streamMode == StreamMode::Http);
    m_rtpModeAction->setChecked(m_streamMode == StreamMode::Rtp);
    m_httpModeAction->setEnabled(!m_streaming);
    m_rtpModeAction->setEnabled(!m_streaming);
    if (m_httpIpMenu)
        m_httpIpMenu->menuAction()->setVisible(m_streamMode == StreamMode::Http);

    const bool idle = !m_streaming;
    auto setChoice = [idle](QAction* action, bool checked) {
        if (!action)
            return;
        action->setChecked(checked);
        action->setEnabled(idle);
    };
    setChoice(m_qualitySmoothAction, m_streamQuality == StreamQuality::Smooth);
    setChoice(m_qualityStandardAction, m_streamQuality == StreamQuality::Standard);
    setChoice(m_qualityHighAction, m_streamQuality == StreamQuality::High);
    setChoice(m_h264SoftwareAction, m_streamCodec == StreamCodec::H264Software);
    setChoice(m_h265SoftwareAction, m_streamCodec == StreamCodec::H265Software);
    setChoice(m_h264VaapiAction, m_streamCodec == StreamCodec::H264Vaapi);
    setChoice(m_h265VaapiAction, m_streamCodec == StreamCodec::H265Vaapi);
    if (m_qualityMenu)
        m_qualityMenu->menuAction()->setEnabled(idle);
    if (m_codecMenu)
        m_codecMenu->menuAction()->setEnabled(idle);
    const QString url = currentStreamUrl();
    m_streamAddressAction->setText(url);
    m_copyStreamAction->setEnabled(!url.isEmpty());
    if (m_streamButton)
        m_streamButton->setToolTip(
            m_streaming ? QStringLiteral("串流中：%1").arg(url)
                        : QStringLiteral("手机 VLC 串流"));
}

int CameraMonitor::streamLongEdge() const
{
    switch (m_streamQuality)
    {
    case StreamQuality::Smooth:
        return 640;
    case StreamQuality::High:
        return 1280;
    case StreamQuality::Standard:
    default:
        return kDefaultStreamLongEdge;
    }
}

int CameraMonitor::streamBitrate() const
{
    switch (m_streamQuality)
    {
    case StreamQuality::Smooth:
        return 2500000;
    case StreamQuality::High:
        return 8000000;
    case StreamQuality::Standard:
    default:
        return kDefaultStreamBitrate;
    }
}

bool CameraMonitor::vaapiAvailable() const
{
    return QFileInfo(QStringLiteral("/dev/dri/renderD128")).isReadable();
}

QStringList CameraMonitor::codecArguments() const
{
    const QString bitrate = QString::number(streamBitrate());
    const QString maxrate = QString::number(streamBitrate() * 6 / 5);
    const QString bufsize = QString::number(streamBitrate() * 2);

    switch (m_streamCodec)
    {
    case StreamCodec::H265Software:
        return {QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                QStringLiteral("-c:v"), QStringLiteral("libx265"),
                QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                QStringLiteral("-tune"), QStringLiteral("zerolatency"),
                QStringLiteral("-b:v"), bitrate,
                QStringLiteral("-maxrate"), maxrate,
                QStringLiteral("-bufsize"), bufsize,
                QStringLiteral("-g"), QStringLiteral("15"),
                QStringLiteral("-x265-params"),
                QStringLiteral("keyint=15:min-keyint=15:scenecut=0:bframes=0")};
    case StreamCodec::H264Vaapi:
        return {QStringLiteral("-vaapi_device"),
                QStringLiteral("/dev/dri/renderD128"),
                QStringLiteral("-vf"), QStringLiteral("format=nv12,hwupload"),
                QStringLiteral("-c:v"), QStringLiteral("h264_vaapi"),
                QStringLiteral("-b:v"), bitrate,
                QStringLiteral("-maxrate"), maxrate,
                QStringLiteral("-bufsize"), bufsize,
                QStringLiteral("-bf"), QStringLiteral("0"),
                QStringLiteral("-g"), QStringLiteral("15")};
    case StreamCodec::H265Vaapi:
        return {QStringLiteral("-vaapi_device"),
                QStringLiteral("/dev/dri/renderD128"),
                QStringLiteral("-vf"), QStringLiteral("format=nv12,hwupload"),
                QStringLiteral("-c:v"), QStringLiteral("hevc_vaapi"),
                QStringLiteral("-b:v"), bitrate,
                QStringLiteral("-maxrate"), maxrate,
                QStringLiteral("-bufsize"), bufsize,
                QStringLiteral("-bf"), QStringLiteral("0"),
                QStringLiteral("-g"), QStringLiteral("15")};
    case StreamCodec::H264Software:
    default:
        return {QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"),
                QStringLiteral("-preset"), QStringLiteral("veryfast"),
                QStringLiteral("-tune"), QStringLiteral("zerolatency"),
                QStringLiteral("-b:v"), bitrate,
                QStringLiteral("-maxrate"), maxrate,
                QStringLiteral("-bufsize"), bufsize,
                QStringLiteral("-g"), QStringLiteral("15")};
    }
}

void CameraMonitor::startStreaming()
{
    if (m_streaming)
        return;
    if (m_streamMode == StreamMode::Http)
        startHttpStream();
    else
        startRtpStream();
    updateStreamMenu();
}

void CameraMonitor::startRtpStream()
{
    launchStreamProcess(
        {QStringLiteral("-f"), QStringLiteral("rtp_mpegts"), QStringLiteral("-ttl"),
         QStringLiteral("16"),
         QStringLiteral("rtp://%1:%2").arg(QLatin1String(kRtpHost)).arg(kRtpPort)},
        false);
}

void CameraMonitor::startHttpStream()
{
    if (!m_httpServer)
        return;
    int port = 8080;
    while (port < 8090 && !m_httpServer->listen(QHostAddress::Any, port))
        ++port;
    if (!m_httpServer->isListening())
    {
        QMessageBox::warning(this, QStringLiteral("无法串流"),
                             QStringLiteral("HTTP 串流端口 8080–8089 都被占用。"));
        return;
    }
    m_httpPort = port;
    m_httpUrl = QStringLiteral("http://%1:%2/stream.ts")
                    .arg(preferredLocalIp())
                    .arg(port);
    if (!launchStreamProcess(
            {QStringLiteral("-f"), QStringLiteral("mpegts"), QStringLiteral("pipe:1")},
            true))
    {
        m_httpServer->close();
        m_httpPort = 0;
        m_httpUrl.clear();
    }
}

bool CameraMonitor::launchStreamProcess(const QStringList& outputArgs, bool http)
{
    const QString ffmpeg =
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("无法串流"),
                             QStringLiteral("找不到 ffmpeg 命令。"));
        return false;
    }

    m_streamFps = std::clamp(m_project->fps, 10, 30);
    const QSize size = streamSize(*m_project, streamLongEdge());
    QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"),
        QStringLiteral("rawvideo"),
        QStringLiteral("-pixel_format"),
        QStringLiteral("rgba"),
        QStringLiteral("-video_size"),
        QStringLiteral("%1x%2").arg(size.width()).arg(size.height()),
        QStringLiteral("-framerate"),
        QString::number(m_streamFps),
        QStringLiteral("-i"),
        QStringLiteral("-")};
    arguments += codecArguments();
    arguments += outputArgs;

    auto* process = new QProcess(this);
    m_streamProcess = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::readyReadStandardError, this, [this] {
        if (m_streamProcess)
            m_streamProcess->readAllStandardError();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, http] {
        if (!m_streamProcess)
            return;
        const QByteArray data = m_streamProcess->readAllStandardOutput();
        if (http && m_streaming && !data.isEmpty())
            relayHttpData(data);
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (m_streaming)
                    stopStreaming();
            });
    process->start(ffmpeg, arguments);
    if (!process->waitForStarted(2000) || process->state() != QProcess::Running)
    {
        process->deleteLater();
        m_streamProcess = nullptr;
        QMessageBox::warning(this, QStringLiteral("无法串流"),
                             QStringLiteral("ffmpeg 未能启动。"));
        return false;
    }

    m_streaming = true;
    m_streamTimer->start(std::max(1, 1000 / m_streamFps));
    collapseForStreaming();
    return true;
}

void CameraMonitor::stopStreaming()
{
    if (!m_streaming && !m_streamProcess
        && (!m_httpServer || !m_httpServer->isListening()))
        return;
    m_streaming = false;
    if (m_streamTimer)
        m_streamTimer->stop();
    if (m_streamProcess)
    {
        m_streamProcess->closeWriteChannel();
        if (!m_streamProcess->waitForFinished(800))
            m_streamProcess->kill();
        m_streamProcess->deleteLater();
        m_streamProcess = nullptr;
    }
    stopHttpServer();
    restoreFromStreaming();
    updateStreamMenu();
}

void CameraMonitor::stopHttpServer()
{
    for (QTcpSocket* socket : m_httpClients)
    {
        socket->abort();
        socket->deleteLater();
    }
    m_httpClients.clear();
    m_httpReady.clear();
    if (m_httpServer)
        m_httpServer->close();
    m_httpPort = 0;
    m_httpUrl.clear();
}

void CameraMonitor::onHttpNewConnection()
{
    if (!m_httpServer)
        return;
    while (QTcpSocket* socket = m_httpServer->nextPendingConnection())
    {
        m_httpClients.append(socket);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (m_httpReady.contains(socket))
            {
                socket->readAll();
                return;
            }
            const QByteArray request = socket->readAll();
            if (!request.contains("\r\n\r\n"))
                return;
            socket->write("HTTP/1.0 200 OK\r\n"
                          "Content-Type: video/mp2t\r\n"
                          "Cache-Control: no-cache\r\n"
                          "Connection: close\r\n\r\n");
            m_httpReady.insert(socket);
            socket->flush();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_httpReady.remove(socket);
            m_httpClients.removeAll(socket);
            socket->deleteLater();
        });
    }
}

void CameraMonitor::relayHttpData(const QByteArray& data)
{
    for (QTcpSocket* socket : m_httpClients)
    {
        if (!m_httpReady.contains(socket)
            || socket->state() != QAbstractSocket::ConnectedState)
            continue;
        if (socket->bytesToWrite() > 4 * 1024 * 1024)
        {
            socket->abort();
            continue;
        }
        socket->write(data);
    }
}

void CameraMonitor::onStreamTick()
{
    if (!m_streaming || !m_streamProcess
        || m_streamProcess->state() != QProcess::Running)
        return;
    if (m_streamProcess->bytesToWrite() > 8 * 1024 * 1024)
        return;

    const QImage image = renderStreamImage();
    if (image.isNull())
        return;
    const int rowBytes = image.width() * 4;
    const qint64 frameBytes = qint64(rowBytes) * image.height();
    if (image.bytesPerLine() == rowBytes)
    {
        const qint64 written = m_streamProcess->write(
            reinterpret_cast<const char*>(image.constBits()), frameBytes);
        if (written != frameBytes)
        {
            // Never leave a partially written raw frame in the pipe; ffmpeg
            // would decode the remainder as corrupted macroblocks.
            stopStreaming();
            return;
        }
    }
    else
    {
        QByteArray frame;
        frame.reserve(int(frameBytes));
        for (int y = 0; y < image.height(); ++y)
            frame.append(reinterpret_cast<const char*>(image.constScanLine(y)),
                         rowBytes);
        const qint64 written = m_streamProcess->write(frame);
        if (written != frame.size())
        {
            stopStreaming();
            return;
        }
    }
}

void CameraMonitor::collapseForStreaming()
{
    if (m_detached || m_streamCollapsed)
        return;
    m_streamSavedGeometry = geometry();
    setFixedHeight(kTitleHeight);
    m_streamCollapsed = true;
    layoutChildren();
}

void CameraMonitor::restoreFromStreaming()
{
    if (!m_streamCollapsed)
        return;
    setMinimumHeight(150);
    setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_streamSavedGeometry.isValid())
        setGeometry(clampedDockGeometry(m_streamSavedGeometry));
    m_streamCollapsed = false;
    layoutChildren();
}

QImage CameraMonitor::renderStreamImage() const
{
    const QSize size = streamSize(*m_project, streamLongEdge());
    const int view = m_project->monitorView;
    if (view == kWorkCameraView && m_viewport)
    {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        QPainter painter(&image);
        CameraRenderer::renderWorkCamera(
            painter, *m_project, m_frame, QRect(QPoint(0, 0), size),
            m_viewport->worldToScreenForTest(), m_viewport->size(),
            m_viewport->palette().color(QPalette::Window), false);
        painter.end();
        return image.convertToFormat(QImage::Format_RGBA8888);
    }

    Camera::State state;
    if (view >= 0)
    {
        const Entity* camera = m_project->entity(view);
        if (camera && camera->isCamera()
            && !m_project->isEffectivelyHidden(camera->id))
            state = Camera::state(*m_project, camera->id, m_frame);
    }
    if (state.size.isEmpty())
    {
        state.scale = 1.0;
        state.rotation = 0.0;
        state.center = QPointF();
        state.size = QSizeF(m_project->width, m_project->height);
    }
    const QImage image = CameraRenderer::renderCameraImage(
        *m_project, m_frame, state, size, false);
    return image.convertToFormat(QImage::Format_RGBA8888);
}

void CameraMonitor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void CameraMonitor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_initialized || m_detached)
        return;

    QRect geometry = Settings::monitorDockGeometry();
    if (geometry.x() < 0 || geometry.y() < 0)
    {
        geometry.setSize(QSize(400, 250));
        if (m_viewport)
            geometry.moveTopLeft(QPoint(m_viewport->width() - geometry.width() - 16,
                                        m_viewport->height() - geometry.height() - 16));
    }
    setGeometry(clampedDockGeometry(geometry));
    m_initialized = true;
    layoutChildren();
}

QRect CameraMonitor::clampedDockGeometry(const QRect& geometry) const
{
    if (!m_viewport)
        return geometry;
    const QSize viewportSize = m_viewport->size();
    QSize size = geometry.size();
    const int maxW = std::max(minimumWidth(), viewportSize.width());
    const int maxH = std::max(minimumHeight(), viewportSize.height());
    size.setWidth(std::clamp(size.width(), minimumWidth(), maxW));
    size.setHeight(std::clamp(size.height(), minimumHeight(), maxH));
    const int maxX = std::max(0, viewportSize.width() - size.width());
    const int maxY = std::max(0, viewportSize.height() - size.height());
    return QRect(QPoint(std::clamp(geometry.x(), 0, maxX),
                        std::clamp(geometry.y(), 0, maxY)),
                 size);
}

void CameraMonitor::clampDockGeometry()
{
    if (!m_detached)
        setGeometry(clampedDockGeometry(geometry()));
}

void CameraMonitor::saveDockGeometry()
{
    if (!m_detached)
        Settings::setMonitorDockGeometry(geometry());
}

void CameraMonitor::toggleQuality()
{
    m_highQuality = !m_highQuality;
    Settings::setMonitorHighQuality(m_highQuality);
    updateButtons();
    update();
}

void CameraMonitor::detachWindow()
{
    if (m_detached)
        return;
    saveDockGeometry();
    m_detached = true;
    setParent(m_viewport ? m_viewport->window() : nullptr);
    setWindowFlags(Qt::Window);
    setWindowFlag(Qt::WindowStaysOnTopHint, Settings::monitorAlwaysOnTop());
    setWindowTitle(QStringLiteral("摄像机监视器"));

    QRect geometry = Settings::monitorWindowGeometry();
    bool onScreen = false;
    for (QScreen* screen : QGuiApplication::screens())
    {
        if (screen->availableGeometry().intersects(geometry))
        {
            onScreen = true;
            break;
        }
    }
    if (!onScreen && QGuiApplication::primaryScreen())
    {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        geometry.moveCenter(available.center());
    }
    setGeometry(geometry);
    show();
    raise();
    updateButtons();
    layoutChildren();
}

void CameraMonitor::dockWindow()
{
    if (!m_detached)
        return;
    Settings::setMonitorWindowGeometry(geometry());
    m_detached = false;
    setWindowFlags(Qt::Widget);
    setParent(m_viewport);
    show();
    setGeometry(clampedDockGeometry(Settings::monitorDockGeometry()));
    raise();
    m_initialized = true;
    updateButtons();
    layoutChildren();
}

void CameraMonitor::applyAlwaysOnTop()
{
    if (!m_detached)
        return;
    const bool on = Settings::monitorAlwaysOnTop();
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) == on)
        return;
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();
}

void CameraMonitor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (gripRect().contains(event->position().toPoint()))
    {
        m_resizing = true;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = geometry();
        return;
    }
    if (titleRect().contains(event->position().toPoint()))
    {
        m_dragging = true;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = geometry();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CameraMonitor::mouseMoveEvent(QMouseEvent* event)
{
    if (m_resizing)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        QSize size = m_pressGeometry.size() + QSize(delta.x(), delta.y());
        size.setWidth(std::max(minimumWidth(), size.width()));
        size.setHeight(std::max(minimumHeight(), size.height()));
        if (!m_detached && m_viewport)
        {
            size.setWidth(std::min(size.width(), m_viewport->width()));
            size.setHeight(std::min(size.height(), m_viewport->height()));
        }
        setGeometry(QRect(m_pressGeometry.topLeft(), size));
        return;
    }

    if (m_dragging)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        if (m_detached)
            move(m_pressGeometry.topLeft() + delta);
        else
            setGeometry(clampedDockGeometry(m_pressGeometry.translated(delta)));
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void CameraMonitor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragging && !m_detached && m_viewport)
    {
        const QWidget* mainWindow = m_viewport->window();
        const QRect frame = mainWindow ? mainWindow->frameGeometry() : QRect();
        if (!frame.contains(event->globalPosition().toPoint()))
        {
            m_dragging = false;
            const QPoint globalPos = event->globalPosition().toPoint();
            detachWindow();
            move(globalPos - QPoint(width() / 2, 10));
            return;
        }
    }

    if (m_dragging || m_resizing)
    {
        if (m_detached)
            Settings::setMonitorWindowGeometry(geometry());
        else
            saveDockGeometry();
    }
    m_dragging = false;
    m_resizing = false;
}

void CameraMonitor::closeEvent(QCloseEvent* event)
{
    if (m_detached)
    {
        dockWindow();
        event->ignore();
        return;
    }
    hide();
    Settings::setMonitorVisible(false);
    emit visibilityChanged(false);
    event->accept();
}

} // namespace pvf
