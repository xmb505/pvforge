#pragma once

#include "core/Project.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <memory>

class QAction;
class QProcess;
class QCloseEvent;
class QComboBox;
class QLabel;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QSlider;
class QTimer;
class QToolButton;
class QWidgetAction;

namespace pvf {

class VideoPlayer;
class VideoProxyGenerator;
class Viewport;

// OBS-style viewer for the final camera image. It starts as a floating child
// in the bottom-right of the viewport and can be dragged out into its own
// top-level window.
class CameraMonitor : public QWidget
{
    Q_OBJECT

public:
    CameraMonitor(Project* project, Viewport* viewport, QWidget* parent = nullptr);
    ~CameraMonitor() override;

    void setFrame(int frame);
    void setPlaying(bool playing);
    void setProjectChanged();
    void syncCameraView();
    void setVisibleFromMenu(bool visible);
    void updateFromViewport();

    bool isDetached() const { return m_detached; }
    bool isHighQuality() const { return m_highQuality; }

    // Test hooks / programmatic trace import.
    bool loadTraceVideo(const QString& path);
    void clearTraceForTest() { clearTraceVideo(); }
    void setTraceOpacity(double opacity);
    bool startStreamingForTest() { startRtpStream(); return m_streaming; }
    void stopStreamingForTest() { stopRtpStream(); }
    bool isStreaming() const { return m_streaming; }

signals:
    void cameraActivated(EntityId id);
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QRect imageRect() const;
    QRect titleRect() const;
    QRect gripRect() const;
    QRect fitRect() const;
    void layoutChildren();
    void rebuildCameraCombo();
    void updateButtons();
    void updateFpsLabel();
    void toggleQuality();
    void detachWindow();
    void dockWindow();
    void applyAlwaysOnTop();
    void clampDockGeometry();
    QRect clampedDockGeometry(const QRect& geometry) const;
    void saveDockGeometry();
    void renderPreview(QPainter& painter);
    void renderTraceOverlay(QPainter& painter);
    void importTraceVideo();
    void clearTraceVideo();
    void updateTraceImage();
    void updateTraceMenu();
    void buildTraceMenu();
    void buildStreamMenu();
    void startRtpStream();
    void stopRtpStream();
    void onStreamTick();
    void updateStreamMenu();
    void collapseForStreaming();
    void restoreFromStreaming();
    QImage renderStreamImage() const;

    Project* m_project = nullptr;
    Viewport* m_viewport = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QToolButton* m_qualityButton = nullptr;
    QToolButton* m_pinButton = nullptr;
    QToolButton* m_detachButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QToolButton* m_traceButton = nullptr;
    QToolButton* m_streamButton = nullptr;

    QMenu* m_traceMenu = nullptr;
    QAction* m_traceNameAction = nullptr;
    QAction* m_clearTraceAction = nullptr;
    QSlider* m_traceOpacitySlider = nullptr;
    QLabel* m_traceOpacityLabel = nullptr;
    std::unique_ptr<Project> m_traceProject;
    std::unique_ptr<VideoPlayer> m_tracePlayer;
    VideoProxyGenerator* m_traceProxy = nullptr;
    QString m_tracePath;
    EntityId m_traceEntity = kInvalidEntity;
    QImage m_traceImage;
    double m_traceOpacity = 0.45;

    QMenu* m_streamMenu = nullptr;
    QAction* m_streamAction = nullptr;
    QAction* m_copyStreamAction = nullptr;
    QProcess* m_streamProcess = nullptr;
    QTimer* m_streamTimer = nullptr;
    bool m_streaming = false;
    bool m_streamCollapsed = false;
    QRect m_streamSavedGeometry;
    int m_streamFps = 24;

    int m_frame = 0;
    bool m_playing = false;
    bool m_highQuality = false;
    int m_fpsFrames = 0;
    int m_displayFps = 0;
    QElapsedTimer m_fpsClock;
    QTimer* m_fpsTimer = nullptr;
    bool m_detached = false;
    bool m_initialized = false;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;

    static constexpr int kTitleHeight = 28;
    static constexpr int kGripSize = 14;
    static constexpr int kButtonWidth = 24;
};

} // namespace pvf
