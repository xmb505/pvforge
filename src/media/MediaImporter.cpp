#include "MediaImporter.h"

#include "media/AudioDecoder.h"
#include "media/VideoDecoder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pvf {

namespace {

const QStringList& imageExtensions()
{
    static const QStringList extensions = {QStringLiteral("png"), QStringLiteral("jpg"),
                                           QStringLiteral("jpeg"), QStringLiteral("webp"),
                                           QStringLiteral("bmp")};
    return extensions;
}

const QStringList& videoExtensions()
{
    static const QStringList extensions = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                           QStringLiteral("mov"), QStringLiteral("mkv"),
                                           QStringLiteral("avi"), QStringLiteral("m4v")};
    return extensions;
}

const QStringList& audioExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("wav"),  QStringLiteral("mp3"),  QStringLiteral("flac"),
        QStringLiteral("ogg"),  QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("aac"),  QStringLiteral("wma"),  QStringLiteral("aiff"),
        QStringLiteral("aif"),  QStringLiteral("mka"),
    };
    return extensions;
}

QString sha256File(const QString& path)
{
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    constexpr qint64 chunkSize = 1 << 20;
    while (!file.atEnd())
        hasher.addData(file.read(chunkSize));
    return QString::fromLatin1(hasher.result().toHex());
}

} // namespace

QString copyIntoAssetDirectory(const QString& sourcePath, const QString& directory)
{
    if (sourcePath.isEmpty())
        return sourcePath;
    if (directory.isEmpty())
        return sourcePath;

    const QFileInfo info(sourcePath);
    const QString absoluteSource = info.absoluteFilePath();
    const QString absoluteDirectory = QFileInfo(directory).absoluteFilePath();

    if (absoluteSource.startsWith(absoluteDirectory + QLatin1Char('/')))
        return sourcePath;

    QDir().mkpath(directory);

    const QString hash = sha256File(sourcePath);
    if (hash.isEmpty())
        return sourcePath;

    const QString suffix = info.suffix();
    const QString target = QDir(directory).filePath(
        suffix.isEmpty() ? hash : hash + QLatin1Char('.') + suffix);

    if (QFile::exists(target))
        return target;
    if (!QFile::copy(sourcePath, target))
        return sourcePath;
    return target;
}

MediaImportResult processMediaFile(const QString& path, const QString& assetDirectory)
{
    MediaImportResult result;
    result.sourcePath = path;
    result.displayName = QFileInfo(path).completeBaseName();

    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool isImage = imageExtensions().contains(suffix);
    const bool isVideo = videoExtensions().contains(suffix);
    const bool isAudio = audioExtensions().contains(suffix);
    if (!isImage && !isVideo && !isAudio)
    {
        result.error = QStringLiteral("不支持的文件类型");
        return result;
    }

    result.importedPath = copyIntoAssetDirectory(path, assetDirectory);
    if (result.importedPath.isEmpty())
    {
        result.error = QStringLiteral("复制失败");
        return result;
    }

    if (isAudio)
    {
        const AudioDecodeResult decoded = decodeAudioFile(result.importedPath);
        if (!decoded.clip)
        {
            result.error = decoded.error;
            return result;
        }
        result.type = EntityType::Audio;
        result.audio = decoded.clip;
        result.duration = decoded.clip->duration;
        result.thumbnail = renderAudioThumbnail(*decoded.clip, QSize(320, 80));
        return result;
    }

    if (isImage)
    {
        const QImage image(result.importedPath);
        if (image.isNull())
        {
            result.error = QStringLiteral("无法读取图片");
            return result;
        }
        result.type = EntityType::Image;
        result.thumbnail = image.scaled(320, 240, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
        return result;
    }

    double duration = 0.0;
    QString error;
    const QImage frame = decodeVideoFirstFrame(result.importedPath, &error, &duration);
    if (frame.isNull())
    {
        result.error = error;
        return result;
    }
    result.type = EntityType::Video;
    result.duration = duration;
    result.thumbnail =
        frame.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

QVector<MediaImportResult> processMediaFiles(const QStringList& paths,
                                             const QString& assetDirectory)
{
    QVector<MediaImportResult> results;
    results.reserve(paths.size());
    for (const QString& path : paths)
        results.append(processMediaFile(path, assetDirectory));
    return results;
}

} // namespace pvf
