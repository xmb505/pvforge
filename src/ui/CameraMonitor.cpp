#include "CameraMonitor.h"

#include "core/Camera.h"
#include "core/Settings.h"
#include "render/CameraRenderer.h"
#include "ui/Viewport.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>

namespace pvf {

CameraMonitor::CameraMonitor(Project* project, Viewport* viewport, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_viewport(viewport)
{
    setObjectName(QStringLiteral("cameraMonitor"));
    setMinimumSize(220, 150);
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

    rebuildCameraCombo();
    updateButtons();
    updateFpsLabel();
    resize(Settings::monitorDockGeometry().size());
}

void CameraMonitor::setFrame(int frame)
{
    m_frame = std::max(0, frame);
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
        geometry.setSize(QSize(320, 208));
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
