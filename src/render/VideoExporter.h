#pragma once

#include "core/Project.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>

class QProcess;
class QTemporaryDir;
class QTimer;

namespace pvf {

enum class ExportEncoder
{
    H264Software,   // libx264, CPU
    H264Vaapi       // h264_vaapi, Intel/AMD GPU
};

struct ExportOptions
{
    QString outputPath;
    QSize size;
    int fps = 30;
    int frameCount = 0;
    EntityId cameraId = kInvalidEntity;   // whole canvas when invalid
    int crf = 20;
    bool includeAudio = true;
    ExportEncoder encoder = ExportEncoder::H264Software;
    // Abort the export when a single frame takes longer than this. The
    // provider may still answer later; that answer is ignored.
    int frameTimeoutMs = 5000;
};

// Streams raw frames to an ffmpeg process. Frames are produced by the caller
// through an asynchronous provider: the exporter requests one frame at a
// time, waits for its callback, writes it to ffmpeg and only then asks for the
// next frame. This lets a future HTML/CDP capture finish before the encoder
// moves on, and makes cancellation and per-frame timeouts deterministic.
class VideoExporter : public QObject
{
    Q_OBJECT

public:
    using FrameDone = std::function<void(const QImage& image)>;
    using FrameProvider = std::function<void(int frame, FrameDone done)>;

    explicit VideoExporter(QObject* parent = nullptr);
    ~VideoExporter() override;

    static QString ffmpegExecutable();
    // True when a VAAPI render node and the h264_vaapi encoder are available.
    static bool vaapiAvailable();

    // Mixes the project audio, starts ffmpeg and asks the provider for each
    // frame. The provider is called on the exporter's thread and must call
    // done exactly once; it may do so synchronously or later. Returns false
    // with an error before any frame is requested.
    bool start(const Project& project, const ExportOptions& options,
               FrameProvider provider, QString* error = nullptr);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void progress(int done, int total);
    // Throttled copy of the last rendered frame for the export preview window.
    void previewFrame(const QImage& image);
    // An empty error with success == false means the user cancelled.
    void finished(bool success, const QString& error);

private:
    void tick();
    void requestFrame();
    void onFrame(quint64 token, int frame, const QImage& image);
    void onFrameTimeout();
    void abandonFrame();
    void fail(const QString& error);
    void finish(bool success);
    void stopProcess();

    QTemporaryDir* m_temp = nullptr;
    QProcess* m_process = nullptr;
    QTimer* m_timer = nullptr;
    QTimer* m_frameTimer = nullptr;
    FrameProvider m_provider;
    ExportOptions m_options;
    QString m_stderr;
    QString m_partialPath;
    QElapsedTimer m_previewClock;
    // Identifies the current export so late callbacks from a previous or
    // cancelled export cannot write frames.
    quint64 m_generation = 0;
    int m_nextFrame = 0;
    int m_requestedFrame = -1;
    bool m_waiting = false;
    bool m_running = false;
};

} // namespace pvf
