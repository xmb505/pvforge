#pragma once

#include "core/Project.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>

class QProcess;
class QTemporaryDir;
class QTimer;

namespace pvf {

struct ExportOptions
{
    QString outputPath;
    QSize size;
    int fps = 30;
    int frameCount = 0;
    EntityId cameraId = kInvalidEntity;   // whole canvas when invalid
    int crf = 20;
    bool includeAudio = true;
};

// Renders the project frame by frame on the UI thread and streams raw frames
// to an ffmpeg process. Frames are produced by the caller so the exporter does
// not need to know about the monitor view or the video decoder.
class VideoExporter : public QObject
{
    Q_OBJECT

public:
    using FrameRenderer = std::function<QImage(int frame)>;

    explicit VideoExporter(QObject* parent = nullptr);
    ~VideoExporter() override;

    static QString ffmpegExecutable();

    // Mixes the project audio, starts ffmpeg and asks renderFrame for each
    // frame. Returns false with an error before any frame is rendered.
    bool start(const Project& project, const ExportOptions& options,
               FrameRenderer renderFrame, QString* error = nullptr);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void progress(int done, int total);
    // An empty error with success == false means the user cancelled.
    void finished(bool success, const QString& error);

private:
    void tick();
    void fail(const QString& error);
    void finish(bool success);
    void stopProcess();

    QTemporaryDir* m_temp = nullptr;
    QProcess* m_process = nullptr;
    QTimer* m_timer = nullptr;
    FrameRenderer m_render;
    ExportOptions m_options;
    QString m_stderr;
    QString m_partialPath;
    int m_nextFrame = 0;
    bool m_running = false;
};

} // namespace pvf
