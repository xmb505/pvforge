#include "WorkbenchPanel.h"

#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "assets/AssetTypeDetect.h"
#include "next/app/ProjectDocument.h"
#include "video2/VideoFrames.h"

#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>

#include <algorithm>
#include <optional>

namespace pvf {

namespace {

QString assetTypeLabelForWorkbench(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("图片");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Audio:
        return QStringLiteral("音频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    }
    return QString();
}

} // namespace

void WorkbenchPanel::refreshMediaLibrary()
{
    if (!m_mediaLibrary)
        return;

    const QSignalBlocker blocker(m_mediaLibrary);
    m_mediaLibrary->clear();

    if (m_session)
    {
        for (const assets::StoredAsset& asset : m_session->assets().list())
        {
            if (asset.type != AssetType::Image && asset.type != AssetType::Video
                && asset.type != AssetType::Audio)
                continue;
            auto* item = new QListWidgetItem(asset.name, m_mediaLibrary);
            item->setData(Qt::UserRole, asset.hash);
            item->setToolTip(asset.directory);

            const QStringList files = QDir(asset.directory).entryList(
                {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
            if (!files.isEmpty() && asset.type == AssetType::Image)
            {
                const QImage image(asset.directory + QLatin1Char('/')
                                   + files.first());
                if (!image.isNull())
                    item->setIcon(QIcon(QPixmap::fromImage(image).scaled(
                        40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            else if (!files.isEmpty() && asset.type == AssetType::Video)
            {
                video2::VideoFrames frames(
                    std::max(1, m_session->appSettings().videoProxyWidth), 4);
                video2::Request request;
                request.path = asset.directory + QLatin1Char('/') + files.first();
                request.sourceFrame = 0;
                request.fps = std::max(1, m_session->settings().fps);
                request.size = QSize(80, 80);
                const QImage frame = frames.frame(request);
                if (!frame.isNull())
                    item->setIcon(QIcon(QPixmap::fromImage(frame).scaled(
                        40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            if (asset.hash == m_mediaHash)
                m_mediaLibrary->setCurrentItem(item);
        }
        return;
    }
}

void WorkbenchPanel::selectMedia(int templateIndex)
{
    Q_UNUSED(templateIndex);
}

void WorkbenchPanel::selectSessionMedia(const QString& hash)
{
    if (!m_session || !m_mediaPreview || !m_mediaInfo)
        return;
    const auto asset = m_session->assets().get(hash);
    if (!asset)
        return;

    m_mediaHash = hash;
    m_mediaTemplateIndex = -1;
    const QStringList files = QDir(asset->directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    m_mediaPath = files.isEmpty() ? QString()
                                  : asset->directory + QLatin1Char('/') + files.first();

    const QSize previewSize(300, 140);
    QPixmap preview;
    if (asset->type == AssetType::Image && !m_mediaPath.isEmpty())
    {
        const QImage image(m_mediaPath);
        if (!image.isNull())
            preview = QPixmap::fromImage(image.scaled(
                previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else if (asset->type == AssetType::Video && !m_mediaPath.isEmpty())
    {
        video2::VideoFrames frames(
            std::max(1, m_session->appSettings().videoProxyWidth), 4);
        video2::Request request;
        request.path = m_mediaPath;
        request.sourceFrame = 0;
        request.fps = std::max(1, m_session->settings().fps);
        request.size = previewSize;
        const QImage frame = frames.frame(request);
        if (!frame.isNull())
            preview = QPixmap::fromImage(frame);
    }
    if (preview.isNull())
        m_mediaPreview->setText(assetTypeLabelForWorkbench(asset->type));
    else
        m_mediaPreview->setPixmap(preview);

    QString info = assetTypeLabelForWorkbench(asset->type)
                   + QStringLiteral(" · ") + asset->name;
    const double seconds =
        asset->metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
    if (seconds > 0.0)
        info += QStringLiteral(" · %1 秒").arg(seconds, 0, 'f', 2);
    info += QStringLiteral("\n") + m_mediaPath;
    m_mediaInfo->setText(info);
    updateCreateState();
}

void WorkbenchPanel::pickMedia()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择媒体文件"), QString(),
        QStringLiteral("媒体文件 (*.png *.jpg *.jpeg *.webp *.bmp *.mp4 *.webm *.mov "
                       "*.mkv *.avi *.m4v *.wav *.mp3 *.flac *.ogg *.opus *.m4a "
                       "*.aac *.wma *.aiff *.aif *.mka);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    if (m_session)
    {
        const auto type = assets::detectType(path);
        QString error;
        std::optional<assets::StoredAsset> stored;
        if (type && *type == AssetType::Video)
        {
            const auto video = m_session->assets().importVideoWithAudio(path, &error);
            if (video)
                stored = video->video;
        }
        else if (type)
            stored = m_session->assets().importFile(path, &error);

        if (!stored)
        {
            QMessageBox::warning(this, QStringLiteral("无法导入"),
                                 error.isEmpty() ? QStringLiteral("导入失败") : error);
            return;
        }
        m_session->document()->commitProject(
            QStringLiteral("pvf: import asset \"%1\"").arg(stored->name), nullptr);
        refreshMediaLibrary();
        for (int row = 0; row < m_mediaLibrary->count(); ++row)
            if (m_mediaLibrary->item(row)->data(Qt::UserRole).toString()
                == stored->hash)
            {
                m_mediaLibrary->setCurrentRow(row);
                break;
            }
        selectSessionMedia(stored->hash);
        return;
    }
}

} // namespace pvf
