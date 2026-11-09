#include "assets/AssetStore.h"

#include "assets/AssetHash.h"
#include "assets/AssetTypeDetect.h"
#include "assets/MediaProbe.h"
#include "assets/Pvhtml.h"
#include "assets/WavExtract.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

namespace pvf::assets {

namespace {

constexpr int kMaxTags = 32;
constexpr int kMaxTagLength = 64;

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QStringList tagsFromMetadata(const QJsonObject& metadata)
{
    QStringList tags;
    const QJsonArray array = metadata.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& value : array)
    {
        const QString tag = value.toString().trimmed();
        if (!tag.isEmpty() && !tags.contains(tag))
            tags.append(tag);
    }
    return tags;
}

bool isValidHash(const QString& hash)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(hash).hasMatch();
}

std::optional<AssetType> typeFromName(const QString& name)
{
    if (name == QStringLiteral("image"))
        return AssetType::Image;
    if (name == QStringLiteral("video"))
        return AssetType::Video;
    if (name == QStringLiteral("audio"))
        return AssetType::Audio;
    if (name == QStringLiteral("html"))
        return AssetType::Html;
    return std::nullopt;
}

bool writeJson(const QString& path, const QJsonObject& object, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(error, QStringLiteral("无法写入 %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        setError(error, QStringLiteral("无法写入 %1").arg(path));
        return false;
    }
    return true;
}

std::optional<QJsonObject> readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

int projectFps(const QString& root)
{
    const auto project = readJson(root + QStringLiteral("/project.json"));
    if (project)
    {
        const int fps = project->value(QStringLiteral("fps")).toInt(30);
        if (fps > 0)
            return fps;
    }
    return 30;
}

QMimeType mimeForFile(const QString& path)
{
    QMimeDatabase database;
    QMimeType type = database.mimeTypeForFile(path, QMimeDatabase::MatchContent);
    if (type.name() == QStringLiteral("application/octet-stream")
        || type.name().isEmpty())
    {
        type = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    }
    return type;
}

QString safeSuffix(const QString& suffix)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]+$"));
    return pattern.match(suffix).hasMatch() ? suffix.toLower() : QString();
}

QStringList contentFiles(const QString& directory)
{
    return QDir(directory).entryList({QStringLiteral("content.*")}, QDir::Files, QDir::Name);
}

QString contentPath(const StoredAsset& asset)
{
    const QStringList files = contentFiles(asset.directory);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

} // namespace

AssetStore::AssetStore(QString projectRoot)
    : m_root(QDir(projectRoot).absolutePath())
    , m_assetsDir(m_root + QStringLiteral("/assets"))
{
}

std::optional<StoredAsset> AssetStore::get(const AssetHash& hash) const
{
    if (!isValidHash(hash))
        return std::nullopt;
    return getByHash(hash);
}

std::optional<StoredAsset> AssetStore::getByHash(const AssetHash& hash) const
{
    const QString directory = m_assetsDir + QLatin1Char('/') + hash;
    if (!QFileInfo(directory).isDir())
        return std::nullopt;

    const auto metadata = readJson(directory + QStringLiteral("/asset.json"));
    if (!metadata)
        return std::nullopt;

    const auto type = typeFromName(metadata->value(QStringLiteral("type")).toString());
    if (!type)
        return std::nullopt;

    StoredAsset asset;
    asset.hash = hash;
    asset.type = *type;
    asset.name = metadata->value(QStringLiteral("name")).toString(hash);
    asset.directory = directory;
    asset.tags = tagsFromMetadata(*metadata);
    asset.metadata = *metadata;

    if (asset.type == AssetType::Html)
    {
        QString entry = metadata->value(QStringLiteral("entry")).toString();
        if (entry.isEmpty())
            entry = QStringLiteral("index.html");
        if (!QFileInfo(directory + QStringLiteral("/bundle/") + entry).isFile())
            return std::nullopt;
    }
    else if (contentFiles(directory).isEmpty())
    {
        return std::nullopt;
    }

    return asset;
}

QVector<StoredAsset> AssetStore::list() const
{
    QVector<StoredAsset> assets;
    const QStringList names = QDir(m_assetsDir).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QString& name : names)
    {
        if (!isValidHash(name))
            continue;
        if (const auto asset = getByHash(name))
            assets.append(*asset);
    }

    std::sort(assets.begin(), assets.end(), [](const StoredAsset& a, const StoredAsset& b) {
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return a.hash < b.hash;
    });
    return assets;
}

bool AssetStore::setTags(const AssetHash& hash, const QStringList& tags, QString* error)
{
    if (!isValidHash(hash))
    {
        setError(error, QStringLiteral("非法的素材哈希: %1").arg(hash));
        return false;
    }

    QStringList normalized;
    normalized.reserve(tags.size());
    for (const QString& raw : tags)
    {
        const QString tag = raw.trimmed();
        if (tag.isEmpty())
            continue;
        if (tag.size() > kMaxTagLength)
        {
            setError(error, QStringLiteral("标签过长（最多 %1 个字符）: %2")
                                .arg(kMaxTagLength)
                                .arg(tag));
            return false;
        }
        if (!normalized.contains(tag))
            normalized.append(tag);
    }
    if (normalized.size() > kMaxTags)
    {
        setError(error, QStringLiteral("标签最多 %1 个，当前 %2 个")
                            .arg(kMaxTags)
                            .arg(normalized.size()));
        return false;
    }

    const auto asset = getByHash(hash);
    if (!asset)
    {
        setError(error, QStringLiteral("素材不存在或内容缺失: %1").arg(hash));
        return false;
    }

    QJsonObject metadata = asset->metadata;
    metadata.insert(QStringLiteral("tags"), QJsonArray::fromStringList(normalized));
    return writeJson(asset->directory + QStringLiteral("/asset.json"), metadata, error);
}

bool AssetStore::remove(const AssetHash& hash, QString* error)
{
    if (!isValidHash(hash))
    {
        setError(error, QStringLiteral("非法的素材哈希: %1").arg(hash));
        return false;
    }
    const QString directory = m_assetsDir + QLatin1Char('/') + hash;
    if (!QFileInfo(directory).isDir())
    {
        setError(error, QStringLiteral("素材不存在: %1").arg(hash));
        return false;
    }
    if (!QDir(directory).removeRecursively())
    {
        setError(error, QStringLiteral("无法删除素材目录: %1").arg(directory));
        return false;
    }
    return true;
}

std::optional<StoredAsset> AssetStore::importFile(const QString& path, QString* error)
{
    return importFileImpl(path, nullptr, error);
}

std::optional<VideoImport> AssetStore::importVideoWithAudio(const QString& path,
                                                            QString* error)
{
    VideoImport result;
    const auto video = importFileImpl(path, &result.audioError, error);
    if (!video)
        return std::nullopt;

    result.video = *video;
    const auto refreshed = getByHash(video->hash);
    if (refreshed)
        result.video = *refreshed;

    const QString audioHash = result.video.metadata.value(QStringLiteral("audioHash")).toString();
    if (!audioHash.isEmpty())
        result.audio = getByHash(audioHash);

    return result;
}

std::optional<StoredAsset> AssetStore::importFileImpl(const QString& path,
                                                      QString* audioError,
                                                      QString* error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        setError(error, QStringLiteral("素材文件不存在: %1").arg(path));
        return std::nullopt;
    }

    const auto type = detectType(path);
    if (!type)
    {
        setError(error, QStringLiteral("无法识别素材类型: %1").arg(path));
        return std::nullopt;
    }
    if (*type == AssetType::Html)
    {
        setError(error, QStringLiteral("HTML 素材请使用 importPvhtml 导入: %1").arg(path));
        return std::nullopt;
    }

    const QString hash = sha256File(path, error);
    if (hash.isEmpty())
        return std::nullopt;

    if (auto existing = getByHash(hash))
    {
        if (*type == AssetType::Video)
        {
            ensureAudio(*existing, audioError);
            if (auto refreshed = getByHash(hash))
                return refreshed;
        }
        return existing;
    }

    QString probeError;
    const Probe probed = probe(path, *type, &probeError);
    if (!probeError.isEmpty())
    {
        setError(error, probeError);
        return std::nullopt;
    }

    const QMimeType mime = mimeForFile(path);
    QString suffix = safeSuffix(info.suffix());
    if (suffix.isEmpty())
        suffix = safeSuffix(mime.preferredSuffix());
    if (suffix.isEmpty())
        suffix = QStringLiteral("bin");

    const int fps = projectFps(m_root);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), assetTypeName(*type));
    metadata.insert(QStringLiteral("name"), info.fileName());
    metadata.insert(QStringLiteral("mime"), mime.name());
    metadata.insert(QStringLiteral("tags"), QJsonArray());

    switch (*type)
    {
    case AssetType::Image:
        metadata.insert(QStringLiteral("width"), probed.size.width());
        metadata.insert(QStringLiteral("height"), probed.size.height());
        break;
    case AssetType::Video:
        metadata.insert(QStringLiteral("width"), probed.size.width());
        metadata.insert(QStringLiteral("height"), probed.size.height());
        if (probed.durationSeconds > 0)
        {
            metadata.insert(QStringLiteral("durationSeconds"), probed.durationSeconds);
            metadata.insert(QStringLiteral("durationFrames"),
                            int(std::llround(probed.durationSeconds * fps)));
        }
        break;
    case AssetType::Audio:
        metadata.insert(QStringLiteral("sampleRate"), probed.sampleRate);
        metadata.insert(QStringLiteral("channels"), probed.channels);
        if (probed.durationSeconds > 0)
        {
            metadata.insert(QStringLiteral("durationSeconds"), probed.durationSeconds);
            metadata.insert(QStringLiteral("durationFrames"),
                            int(std::llround(probed.durationSeconds * fps)));
        }
        break;
    case AssetType::Html:
        break;
    }

    if (!QDir().mkpath(m_assetsDir))
    {
        setError(error, QStringLiteral("无法创建素材目录: %1").arg(m_assetsDir));
        return std::nullopt;
    }

    QTemporaryDir temporary(m_assetsDir + QStringLiteral("/.import-XXXXXX"));
    if (!temporary.isValid())
    {
        setError(error, QStringLiteral("无法创建导入临时目录"));
        return std::nullopt;
    }

    const QString contentName = QStringLiteral("content.") + suffix;
    const QString stagedContent = temporary.path() + QLatin1Char('/') + contentName;
    if (!QFile::copy(path, stagedContent))
    {
        setError(error, QStringLiteral("无法复制素材文件: %1").arg(path));
        return std::nullopt;
    }
    if (!writeJson(temporary.path() + QStringLiteral("/asset.json"), metadata, error))
        return std::nullopt;

    const QString finalDirectory = m_assetsDir + QLatin1Char('/') + hash;
    if (QFileInfo::exists(finalDirectory))
    {
        if (auto raced = getByHash(hash))
            return raced;
        setError(error, QStringLiteral("素材目录已存在但内容损坏: %1").arg(finalDirectory));
        return std::nullopt;
    }
    if (!QDir().rename(temporary.path(), finalDirectory))
    {
        if (auto raced = getByHash(hash))
            return raced;
        setError(error, QStringLiteral("无法写入素材目录: %1").arg(finalDirectory));
        return std::nullopt;
    }

    if (*type == AssetType::Video)
    {
        if (auto stored = getByHash(hash))
        {
            ensureAudio(*stored, audioError);
            if (auto refreshed = getByHash(hash))
                return refreshed;
        }
    }

    return getByHash(hash);
}

std::optional<StoredAsset> AssetStore::ensureAudio(const StoredAsset& video,
                                                   QString* error)
{
    if (video.type != AssetType::Video)
        return std::nullopt;

    const QString existingHash = video.metadata.value(QStringLiteral("audioHash")).toString();
    if (!existingHash.isEmpty())
    {
        if (auto existing = getByHash(existingHash))
            return existing;
    }

    const QString content = contentPath(video);
    if (content.isEmpty())
    {
        setError(error, QStringLiteral("视频内容文件缺失: %1").arg(video.hash));
        return std::nullopt;
    }

    QString probeError;
    const Probe probed = probe(content, AssetType::Video, &probeError);
    if (!probeError.isEmpty())
    {
        setError(error, probeError);
        return std::nullopt;
    }
    if (probed.sampleRate <= 0 && probed.channels <= 0)
        return std::nullopt; // No audio stream: not an error.

    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        setError(error, QStringLiteral("无法创建拆分音频的临时目录"));
        return std::nullopt;
    }

    QString stem = QFileInfo(video.name).completeBaseName();
    if (stem.isEmpty())
        stem = QStringLiteral("audio");
    const QString wavPath = temporary.path() + QLatin1Char('/') + stem + QStringLiteral(".wav");

    QString wavError;
    if (!extractWav(content, wavPath, &wavError))
    {
        setError(error, wavError);
        return std::nullopt;
    }

    QString importError;
    const auto audio = importFile(wavPath, &importError);
    if (!audio)
    {
        setError(error, importError);
        return std::nullopt;
    }

    QJsonObject metadata = video.metadata;
    metadata.insert(QStringLiteral("audioHash"), audio->hash);
    QString writeError;
    if (!writeJson(video.directory + QStringLiteral("/asset.json"), metadata, &writeError))
    {
        setError(error, writeError);
        return std::nullopt;
    }

    return audio;
}

std::optional<StoredAsset> AssetStore::importPvhtml(const QString& path, QString* error)
{
    const QFileInfo source(path);
    if (!source.exists())
    {
        setError(error, QStringLiteral("pvhtml 源不存在: %1").arg(path));
        return std::nullopt;
    }

    if (!QDir().mkpath(m_assetsDir))
    {
        setError(error, QStringLiteral("无法创建素材目录: %1").arg(m_assetsDir));
        return std::nullopt;
    }

    QTemporaryDir temporary(m_assetsDir + QStringLiteral("/.pvhtml-XXXXXX"));
    if (!temporary.isValid())
    {
        setError(error, QStringLiteral("无法创建导入临时目录"));
        return std::nullopt;
    }

    const QString bundlePath = temporary.path() + QStringLiteral("/bundle");
    PvhtmlManifest manifest;
    QString importError;
    if (!pvf::assets::importPvhtml(path, bundlePath, &manifest, &importError))
    {
        setError(error, importError);
        return std::nullopt;
    }

    const QString hash = sha256Directory(bundlePath, error);
    if (hash.isEmpty())
        return std::nullopt;

    if (auto existing = getByHash(hash))
        return existing;

    QString name = source.isDir() ? source.fileName() : source.completeBaseName();
    if (name.isEmpty())
        name = QStringLiteral("pvhtml");

    const QString finalDirectory = m_assetsDir + QLatin1Char('/') + hash;
    if (QFileInfo::exists(finalDirectory))
    {
        setError(error, QStringLiteral("素材目录已存在但内容损坏: %1").arg(finalDirectory));
        return std::nullopt;
    }
    if (!QDir().mkpath(finalDirectory))
    {
        setError(error, QStringLiteral("无法创建素材目录: %1").arg(finalDirectory));
        return std::nullopt;
    }
    if (!QDir().rename(bundlePath, finalDirectory + QStringLiteral("/bundle")))
    {
        QDir(finalDirectory).removeRecursively();
        setError(error, QStringLiteral("无法写入素材目录: %1").arg(finalDirectory));
        return std::nullopt;
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("hash"), hash);
    metadata.insert(QStringLiteral("type"), assetTypeName(AssetType::Html));
    metadata.insert(QStringLiteral("name"), name);
    metadata.insert(QStringLiteral("mime"), QStringLiteral("text/html"));
    metadata.insert(QStringLiteral("tags"), QJsonArray());
    metadata.insert(QStringLiteral("width"), manifest.logicalSize.width());
    metadata.insert(QStringLiteral("height"), manifest.logicalSize.height());
    metadata.insert(QStringLiteral("entry"), manifest.entry);
    metadata.insert(QStringLiteral("variables"), QJsonObject::fromVariantMap(manifest.variables));
    if (!writeJson(finalDirectory + QStringLiteral("/asset.json"), metadata, error))
    {
        QDir(finalDirectory).removeRecursively();
        return std::nullopt;
    }

    return getByHash(hash);
}

} // namespace pvf::assets
