#include "next/app/CameraMonitorWidget.h"

#include "next/app/MediaServices.h"
#include "next/app/ProjectDocument.h"
#include "render2/Render.h"
#include "video2/VideoFrames.h"

#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace pvf::next {

CameraMonitorWidget::CameraMonitorWidget(ProjectDocument* document,
                                         MediaServices* media, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("cameraMonitorWidget"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(minimumSizeHint());

    m_timer = new QTimer(this);
    m_timer->setInterval(kRefreshIntervalMs);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            update();
    });

    setContext(document, media);
}

CameraMonitorWidget::~CameraMonitorWidget() = default;

void CameraMonitorWidget::setContext(ProjectDocument* document,
                                     MediaServices* media)
{
    if (m_document == document && m_media == media)
    {
        invalidateScene();
        return;
    }

    m_document = document;
    m_media = media;

    if (m_document)
    {
        connect(m_document, &Document::changed, this,
                &CameraMonitorWidget::invalidateScene, Qt::UniqueConnection);
        connect(m_document, &Document::playheadChanged, this,
                &CameraMonitorWidget::invalidateScene, Qt::UniqueConnection);
    }
    if (m_media)
    {
        connect(m_media, &MediaServices::htmlFrameReady, this,
                &CameraMonitorWidget::invalidateScene, Qt::UniqueConnection);
    }

    // Reference decoding is project-fps dependent; drop the cached frame.
    m_referenceImage = QImage();
    m_referenceDecodedPath.clear();
    m_referenceDecodedFrame = -1;
    m_referenceDecodedSize = QSize();
    m_referenceHint.clear();
    invalidateScene();
}

void CameraMonitorWidget::setCameraUuid(const Uuid& uuid)
{
    if (m_cameraUuid == uuid)
        return;
    m_cameraUuid = uuid;
    invalidateScene();
}

void CameraMonitorWidget::refresh()
{
    invalidateScene();
}

void CameraMonitorWidget::invalidateScene()
{
    ++m_revision;
    m_frameValid = false;
    update();
}

int CameraMonitorWidget::projectFps() const
{
    if (!m_document)
        return 30;
    return std::max(1, m_document->project().settings.fps);
}

QSize CameraMonitorWidget::renderSize() const
{
    double aspect = 16.0 / 9.0;
    if (m_document)
    {
        aspect = double(std::max(1, m_document->project().settings.width))
                 / double(std::max(1, m_document->project().settings.height));
    }
    if (!(aspect > 0.0) || !std::isfinite(aspect))
        aspect = 16.0 / 9.0;

    int maxWidth = width() >= 8 ? width() : sizeHint().width();
    int maxHeight = height() >= 8 ? height() : sizeHint().height();
    maxWidth = std::clamp(maxWidth, 1, kMaxWidth);
    maxHeight = std::clamp(maxHeight, 1, kMaxHeight);

    int w = maxWidth;
    int h = int(std::lround(double(w) / aspect));
    if (h > maxHeight)
    {
        h = maxHeight;
        w = int(std::lround(double(h) * aspect));
    }
    return QSize(std::max(1, w), std::max(1, h));
}

QRect CameraMonitorWidget::imageRect() const
{
    const QSize size = renderSize();
    return QRect(QPoint((width() - size.width()) / 2,
                        (height() - size.height()) / 2),
                 size);
}

QImage CameraMonitorWidget::renderScene(const QSize& size) const
{
    if (!m_document || size.isEmpty())
        return QImage();

    const model::Project& project = m_document->project();
    render2::Options options;
    options.projectFrame = m_document->playhead();
    options.cameraUuid = m_cameraUuid;
    options.outputSize = size;
    options.videoFrame = [this](const Uuid& entityUuid, double projectFrame) {
        return m_media ? m_media->videoFrame(entityUuid, projectFrame)
                       : QImage();
    };
    options.htmlFrame = [this](const Uuid& entityUuid, double projectFrame) {
        return m_media ? m_media->htmlFrame(entityUuid, projectFrame)
                       : QImage();
    };

    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    return render2::renderFrame(project, m_document->assets(), options, world,
                                cache);
}

QImage CameraMonitorWidget::decodeReferenceFrame(const QSize& size,
                                                 QString* error) const
{
    if (error)
        error->clear();
    if (m_referencePath.isEmpty())
        return QImage();

    if (!m_referenceDecoder)
        m_referenceDecoder = std::make_unique<video2::VideoFrames>();

    video2::Request request;
    request.path = m_referencePath;
    request.sourceFrame = m_document ? m_document->playhead() : 0;
    request.fps = projectFps();
    request.size = size;
    request.proxy = true;

    QString decodeError;
    const QImage image = m_referenceDecoder->frame(request, &decodeError);
    if (image.isNull() && error)
    {
        *error = decodeError.isEmpty() ? tr("无法解码这一帧") : decodeError;
    }
    return image;
}

void CameraMonitorWidget::ensureFrame() const
{
    if (!m_document)
    {
        m_frame = QImage();
        m_frameValid = false;
        return;
    }

    const QSize size = renderSize();
    const int frame = m_document->playhead();
    const bool unchanged = m_frameValid && size == m_frameSize
                           && frame == m_frameNumber
                           && m_cameraUuid == m_frameCamera
                           && m_revision == m_frameRevision;
    if (unchanged)
        return;

    QImage image = renderScene(size);
    m_referenceHint.clear();

    const bool wantsReference = !m_referencePath.isEmpty()
                                && m_referenceOpacity > 0.001
                                && !image.isNull();
    if (wantsReference)
    {
        if (m_referenceDecodedPath != m_referencePath
            || m_referenceDecodedFrame != frame
            || m_referenceDecodedSize != size)
        {
            QString error;
            m_referenceImage = decodeReferenceFrame(size, &error);
            m_referenceDecodedPath = m_referencePath;
            m_referenceDecodedFrame = frame;
            m_referenceDecodedSize = size;
            if (m_referenceImage.isNull())
            {
                m_referenceHint = error.isEmpty()
                                      ? tr("参考视频没有这一帧")
                                      : tr("参考视频：%1").arg(error);
            }
        }

        if (!m_referenceImage.isNull())
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.setOpacity(m_referenceOpacity);
            painter.drawImage(QRect(QPoint(0, 0), size), m_referenceImage);
        }
    }
    else if (m_referencePath.isEmpty())
    {
        m_referenceImage = QImage();
        m_referenceDecodedPath.clear();
        m_referenceDecodedFrame = -1;
        m_referenceDecodedSize = QSize();
    }

    m_frame = image;
    m_frameSize = size;
    m_frameCamera = m_cameraUuid;
    m_frameNumber = frame;
    m_frameRevision = m_revision;
    m_frameValid = true;
}

QImage CameraMonitorWidget::grabForTest() const
{
    ensureFrame();
    return m_frame;
}

bool CameraMonitorWidget::setReferenceVideo(const QString& path,
                                            QString* error)
{
    if (error)
        error->clear();
    if (path.isEmpty())
    {
        clearReference();
        return true;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        if (error)
            *error = tr("文件不存在：%1").arg(path);
        return false;
    }

    // Probe frame 0 at the same size the overlay would use, so an unreadable
    // or non-video file is rejected before it replaces the current reference.
    video2::VideoFrames probe;
    video2::Request request;
    request.path = info.absoluteFilePath();
    request.sourceFrame = 0;
    request.fps = projectFps();
    request.size = renderSize();
    request.proxy = true;

    QString decodeError;
    const QImage first = probe.frame(request, &decodeError);
    if (first.isNull())
    {
        if (error)
        {
            *error = decodeError.isEmpty() ? tr("无法解码视频文件")
                                           : decodeError;
        }
        return false;
    }

    m_referencePath = info.absoluteFilePath();
    m_referenceDecoder.reset();
    m_referenceImage = first;
    m_referenceDecodedPath = m_referencePath;
    m_referenceDecodedFrame = 0;
    m_referenceDecodedSize = request.size;
    m_referenceHint.clear();
    invalidateScene();
    emit referenceChanged();
    return true;
}

void CameraMonitorWidget::setReferenceOpacity(double opacity)
{
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    if (std::abs(clamped - m_referenceOpacity) < 1e-9)
        return;
    m_referenceOpacity = clamped;
    invalidateScene();
    emit referenceChanged();
}

void CameraMonitorWidget::clearReference()
{
    if (m_referencePath.isEmpty() && m_referenceImage.isNull())
        return;
    m_referencePath.clear();
    m_referenceDecoder.reset();
    m_referenceImage = QImage();
    m_referenceDecodedPath.clear();
    m_referenceDecodedFrame = -1;
    m_referenceDecodedSize = QSize();
    m_referenceHint.clear();
    invalidateScene();
    emit referenceChanged();
}

void CameraMonitorWidget::paintHint(QPainter& painter,
                                    const QString& text) const
{
    if (text.isEmpty())
        return;

    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int textWidth = metrics.horizontalAdvance(text);
    const QRect box(4, height() - metrics.height() - 8,
                    std::min(std::max(0, width() - 8), textWidth + 12),
                    metrics.height() + 4);
    painter.fillRect(box, QColor(0, 0, 0, 170));
    painter.setPen(QColor(QStringLiteral("#f0d38a")));
    painter.drawText(box.adjusted(6, 0, -4, 0),
                     Qt::AlignLeft | Qt::AlignVCenter, text);
}

void CameraMonitorWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#0b0b10")));

    if (!m_document)
    {
        paintHint(painter, tr("未打开工程"));
        return;
    }

    ensureFrame();
    if (m_frame.isNull())
    {
        paintHint(painter, tr("无法渲染相机画面"));
        return;
    }

    const QRect target = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, m_frame);
    paintHint(painter, m_referenceHint);
}

void CameraMonitorWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_timer)
        m_timer->start();
    update();
}

void CameraMonitorWidget::hideEvent(QHideEvent* event)
{
    if (m_timer)
        m_timer->stop();
    QWidget::hideEvent(event);
}

} // namespace pvf::next
