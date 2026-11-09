#include "assets/AssetTypeDetect.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace pvf::assets {

namespace {

const QStringList kImageExtensions = {
    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
    QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
    QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("svg"),
    QStringLiteral("ico"), QStringLiteral("avif"), QStringLiteral("heic"),
};

const QStringList kVideoExtensions = {
    QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
    QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("m4v"),
    QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("flv"),
    QStringLiteral("wmv"), QStringLiteral("ogv"), QStringLiteral("ts"),
    QStringLiteral("m2ts"), QStringLiteral("3gp"),
};

const QStringList kAudioExtensions = {
    QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"),
    QStringLiteral("oga"), QStringLiteral("flac"), QStringLiteral("aac"),
    QStringLiteral("m4a"), QStringLiteral("opus"), QStringLiteral("wma"),
    QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("alac"),
    QStringLiteral("ape"), QStringLiteral("mka"),
};

const QStringList kHtmlExtensions = {
    QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("pvhtml"),
};

std::optional<AssetType> fromExtension(const QString& suffix)
{
    if (kImageExtensions.contains(suffix))
        return AssetType::Image;
    if (kVideoExtensions.contains(suffix))
        return AssetType::Video;
    if (kAudioExtensions.contains(suffix))
        return AssetType::Audio;
    if (kHtmlExtensions.contains(suffix))
        return AssetType::Html;
    return std::nullopt;
}

std::optional<AssetType> fromMagic(const QByteArray& head, const QString& suffix)
{
    if (head.startsWith("\x89PNG\r\n\x1a\n") || head.startsWith("\xff\xd8\xff")
        || head.startsWith("GIF87a") || head.startsWith("GIF89a")
        || head.startsWith("BM"))
    {
        return AssetType::Image;
    }

    if (head.size() >= 12 && head.startsWith("RIFF"))
    {
        const QByteArray kind = head.mid(8, 4);
        if (kind == "WAVE")
            return AssetType::Audio;
        if (kind == "AVI ")
            return AssetType::Video;
        if (kind == "WEBP")
            return AssetType::Image;
    }

    if (head.startsWith("FORM") && head.size() >= 12 && head.mid(8, 4) == "AIFF")
        return AssetType::Audio;

    if (head.startsWith("OggS"))
        return suffix == QStringLiteral("ogv") ? AssetType::Video : AssetType::Audio;

    if (head.startsWith("fLaC"))
        return AssetType::Audio;

    if (head.startsWith("\x1a\x45\xdf\xa3"))
    {
        return suffix == QStringLiteral("mka") || suffix == QStringLiteral("mks")
                   ? AssetType::Audio
                   : AssetType::Video;
    }

    if (head.size() >= 12 && head.mid(4, 4) == "ftyp")
    {
        return (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("aac")
                || suffix == QStringLiteral("alac"))
                   ? AssetType::Audio
                   : AssetType::Video;
    }

    if (head.startsWith("ID3"))
        return AssetType::Audio;

    if (head.size() >= 2 && static_cast<unsigned char>(head[0]) == 0xff
        && (static_cast<unsigned char>(head[1]) & 0xe0) == 0xe0)
    {
        return AssetType::Audio;
    }

    return std::nullopt;
}

} // namespace

std::optional<AssetType> detectType(const QString& path)
{
    const QFileInfo info(path);
    if (info.isDir())
    {
        if (QFileInfo::exists(path + QStringLiteral("/index.html"))
            || QFileInfo::exists(path + QStringLiteral("/pvhtml.json")))
        {
            return AssetType::Html;
        }
        return std::nullopt;
    }
    if (!info.exists() || !info.isFile())
        return std::nullopt;

    const QString suffix = info.suffix().toLower();
    if (const auto byExtension = fromExtension(suffix))
        return byExtension;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray head = file.read(64);
    if (head.isEmpty())
        return std::nullopt;
    return fromMagic(head, suffix);
}

} // namespace pvf::assets
