#pragma once

#include "common/Types.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <memory>

class QHideEvent;
class QPaintEvent;
class QPainter;
class QShowEvent;
class QTimer;

namespace pvf::video2 {
class VideoFrames;
}

namespace pvf::next {

class MediaServices;
class ProjectDocument;

// OBS-style live view of the current camera.
//
// The widget renders the scene through render2 with the same video/HTML
// providers as the viewport, but never draws the editor gizmo, grid or
// selection.  It repaints at 30 fps while visible; frames are only re-rendered
// when the playhead, the camera, the widget size or the document changed.
//
// A local reference video ("临摹视频") can be decoded by video2::VideoFrames
// and blended over the camera picture at a 0..1 opacity.  The reference time
// is the playhead divided by the project fps, so it follows playback and
// scrubbing.  A missing or undecodable reference frame shows a hint and never
// aborts the camera picture.
class CameraMonitorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraMonitorWidget(ProjectDocument* document,
                                 MediaServices* media,
                                 QWidget* parent = nullptr);
    ~CameraMonitorWidget() override;

    // Rebinds the raw document/media pointers.  NextWindow calls this with
    // null pointers before the document or the media services are destroyed.
    void setContext(ProjectDocument* document, MediaServices* media);

    void setCameraUuid(const Uuid& uuid);
    Uuid cameraUuid() const { return m_cameraUuid; }

    // Decodes frame 0 as a probe and keeps the path on success.  Returns false
    // with *error set when the file is missing or cannot be decoded.
    bool setReferenceVideo(const QString& path, QString* error = nullptr);
    void setReferenceOpacity(double opacity);
    void clearReference();

    QString referencePath() const { return m_referencePath; }
    double referenceOpacity() const { return m_referenceOpacity; }
    // Non-empty while the current reference frame is missing/undecodable.
    QString referenceHint() const { return m_referenceHint; }

    // Last composed picture (scene + reference overlay).  Renders on demand so
    // tests do not depend on a paintEvent.
    QImage grabForTest() const;

    QSize sizeHint() const override { return QSize(320, 120); }
    QSize minimumSizeHint() const override { return QSize(240, 90); }

public slots:
    // Forgets the cached picture; the next paint re-renders.
    void refresh();

signals:
    void referenceChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void invalidateScene();
    int projectFps() const;
    QSize renderSize() const;
    QRect imageRect() const;
    QImage renderScene(const QSize& size) const;
    QImage decodeReferenceFrame(const QSize& size, QString* error) const;
    void ensureFrame() const;
    void paintHint(QPainter& painter, const QString& text) const;

    ProjectDocument* m_document = nullptr;
    MediaServices* m_media = nullptr;
    Uuid m_cameraUuid;

    QTimer* m_timer = nullptr;

    QString m_referencePath;
    double m_referenceOpacity = 0.45;
    mutable std::unique_ptr<video2::VideoFrames> m_referenceDecoder;
    mutable QImage m_referenceImage;
    mutable QString m_referenceHint;
    mutable QString m_referenceDecodedPath;
    mutable int m_referenceDecodedFrame = -1;
    mutable QSize m_referenceDecodedSize;

    mutable QImage m_frame;
    mutable QSize m_frameSize;
    mutable Uuid m_frameCamera;
    mutable int m_frameNumber = -1;
    mutable quint64 m_revision = 0;
    mutable quint64 m_frameRevision = 0;
    mutable bool m_frameValid = false;

    // Capped preview size.  The dock is small, so rendering at 640x360 is
    // plenty and keeps the extra paint pass cheap during playback.
    static constexpr int kMaxWidth = 640;
    static constexpr int kMaxHeight = 360;
    static constexpr int kRefreshIntervalMs = 33;
};

} // namespace pvf::next
