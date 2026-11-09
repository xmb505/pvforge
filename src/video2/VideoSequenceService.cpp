#include "video2/VideoSequenceService.h"

#include "assets/MediaProbe.h"

#include <QBuffer>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace pvf::video2 {
namespace {

constexpr int kWebpQuality = 80;
constexpr int kSaveEveryFrames = 30;
constexpr int kSaveEveryTouches = 64;
constexpr qint64 kSaveEveryMs = 2000;

bool safeHash(const AssetHash& hash)
{
    if (hash.isEmpty() || hash == QLatin1String(".")
        || hash == QLatin1String(".."))
        return false;
    return !hash.contains(QLatin1Char('/')) && !hash.contains(QLatin1Char('\\'));
}

AssetHash contentHash(const QImage& source)
{
    QImage rgba = source;
    if (rgba.format() != QImage::Format_RGBA8888)
        rgba = rgba.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull() || rgba.width() <= 0 || rgba.height() <= 0)
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QByteArray header = QByteArray::number(rgba.width()) + 'x'
                              + QByteArray::number(rgba.height()) + ':';
    hash.addData(QByteArrayView(header));

    // Hash only the real pixels; QImage rows may contain alignment padding.
    const int rowBytes = rgba.width() * 4;
    for (int y = 0; y < rgba.height(); ++y)
        hash.addData(QByteArrayView(
            reinterpret_cast<const char*>(rgba.constScanLine(y)), rowBytes));
    return QString::fromLatin1(hash.result().toHex());
}

QByteArray encodeWebp(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};

    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(kWebpQuality);
    if (!writer.write(image))
        return {};
    return bytes;
}

QImage decodeWebp(const QString& path)
{
    QImageReader reader(path, QByteArrayLiteral("webp"));
    reader.setAutoTransform(false);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);
    return image;
}

bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size() || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

VideoSequenceService::VideoSequenceService(SequenceOptions options)
    : m_options(std::move(options))
    , m_decoder(m_options.maxWidth > 0 ? m_options.maxWidth : 960,
                std::max(0, m_options.memoryEntries))
{
    m_memory.setMaxCost(std::max(0, m_options.memoryEntries));
    m_lastSaveMs = QDateTime::currentMSecsSinceEpoch();

    if (m_options.projectFps <= 0)
    {
        m_setupError = QStringLiteral("项目帧率必须大于 0");
        return;
    }
    if (m_options.maxWidth <= 0)
    {
        m_setupError = QStringLiteral("最大宽度必须大于 0");
        return;
    }
    if (m_options.cacheRoot.isEmpty())
    {
        m_setupError = QStringLiteral("缓存目录为空");
        return;
    }
    if (!safeHash(m_options.assetHash))
    {
        m_setupError = QStringLiteral("素材哈希无效");
        return;
    }

    m_cacheDir = QDir(m_options.cacheRoot)
                     .filePath(m_options.assetHash + QLatin1Char('/')
                               + QString::number(m_options.projectFps)
                               + QLatin1Char('/')
                               + QString::number(m_options.maxWidth));
    m_indexPath = QDir(m_cacheDir).filePath(QStringLiteral("index.json"));

    if (!QDir().mkpath(m_cacheDir))
    {
        m_setupError =
            QStringLiteral("无法创建缓存目录: %1").arg(m_cacheDir);
        return;
    }

    m_ready = true;
    QString ignored;
    loadIndex(&ignored);
    if (!ignored.isEmpty())
        qWarning().noquote() << QStringLiteral("video2 sequence:") << ignored;
}

VideoSequenceService::~VideoSequenceService()
{
    if (m_indexDirty)
        saveIndex(nullptr);
}

QString VideoSequenceService::webpPath(const AssetHash& hash) const
{
    return QDir(m_cacheDir).filePath(hash + QStringLiteral(".webp"));
}

bool VideoSequenceService::imageAvailable(const AssetHash& hash) const
{
    if (!m_images.contains(hash))
        return false;
    const QFileInfo info(webpPath(hash));
    return info.isFile() && info.size() > 0;
}

bool VideoSequenceService::loadIndex(QString* error)
{
    m_frames.clear();
    m_images.clear();
    m_totalBytes = 0;
    m_clock = 0;

    QFile file(m_indexPath);
    if (!file.exists())
    {
        // Files without an index can never be mapped back to project frames.
        removeAllFiles();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        removeAllFiles();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = QStringLiteral("索引损坏: %1").arg(parseError.errorString());
        removeAllFiles();
        return false;
    }

    const QJsonObject root = document.object();
    m_totalFrames = root.value(QStringLiteral("totalFrames")).toInt(0);
    if (m_totalFrames < 0)
        m_totalFrames = 0;

    const QJsonObject images = root.value(QStringLiteral("images")).toObject();
    for (auto it = images.constBegin(); it != images.constEnd(); ++it)
    {
        const AssetHash hash = it.key();
        if (!safeHash(hash) || !it.value().isObject())
            continue;

        const QJsonObject object = it.value().toObject();
        ImageEntry entry;
        entry.size =
            static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        entry.lastAccess = static_cast<qint64>(
            object.value(QStringLiteral("lastAccess")).toDouble());
        entry.width = object.value(QStringLiteral("width")).toInt(0);
        entry.height = object.value(QStringLiteral("height")).toInt(0);

        const QFileInfo info(webpPath(hash));
        if (!info.isFile() || info.size() <= 0)
            continue;

        entry.size = info.size();
        if (entry.lastAccess <= 0)
            entry.lastAccess = QDateTime::currentMSecsSinceEpoch();

        m_clock = std::max(m_clock, entry.lastAccess);
        m_totalBytes += entry.size;
        m_images.insert(hash, entry);
    }

    const QJsonObject frames = root.value(QStringLiteral("frames")).toObject();
    for (auto it = frames.constBegin(); it != frames.constEnd(); ++it)
    {
        bool ok = false;
        const int frame = it.key().toInt(&ok);
        if (!ok || frame < 0)
            continue;
        if (m_totalFrames > 0 && frame >= m_totalFrames)
            continue;

        const AssetHash hash = it.value().toString();
        if (m_images.contains(hash))
            m_frames.insert(frame, hash);
    }

    // Files absent from the index would otherwise consume disk forever.
    const QFileInfoList files = QDir(m_cacheDir).entryInfoList(
        QStringList{QStringLiteral("*.webp")}, QDir::Files);
    for (const QFileInfo& info : files)
    {
        if (!m_images.contains(info.completeBaseName()))
            QFile::remove(info.absoluteFilePath());
    }

    return true;
}

bool VideoSequenceService::saveIndex(QString* error)
{
    if (!m_ready)
    {
        if (error)
            *error = m_setupError;
        return false;
    }

    QJsonObject frames;
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it)
    {
        if (m_images.contains(it.value()))
            frames.insert(QString::number(it.key()), it.value());
    }

    QJsonObject images;
    for (auto it = m_images.constBegin(); it != m_images.constEnd(); ++it)
    {
        QJsonObject object;
        object.insert(QStringLiteral("size"),
                      static_cast<double>(it->size));
        object.insert(QStringLiteral("lastAccess"),
                      static_cast<double>(it->lastAccess));
        object.insert(QStringLiteral("width"), it->width);
        object.insert(QStringLiteral("height"), it->height);
        images.insert(it.key(), object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("assetHash"), m_options.assetHash);
    root.insert(QStringLiteral("projectFps"), m_options.projectFps);
    root.insert(QStringLiteral("maxWidth"), m_options.maxWidth);
    root.insert(QStringLiteral("totalFrames"), m_totalFrames);
    root.insert(QStringLiteral("frames"), frames);
    root.insert(QStringLiteral("images"), images);

    const QByteArray bytes =
        QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (!writeFile(m_indexPath, bytes, error))
        return false;

    m_indexDirty = false;
    m_touchesSinceSave = 0;
    m_lastSaveMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void VideoSequenceService::removeAllFiles()
{
    if (m_cacheDir.isEmpty())
        return;
    QDir(m_cacheDir).removeRecursively();
    QDir().mkpath(m_cacheDir);
}

void VideoSequenceService::touch(const AssetHash& hash)
{
    auto it = m_images.find(hash);
    if (it == m_images.end())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_clock = std::max(now, m_clock + 1);
    it->lastAccess = m_clock;
    m_indexDirty = true;
    ++m_touchesSinceSave;
}

void VideoSequenceService::scheduleIndexSave()
{
    if (!m_indexDirty)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_touchesSinceSave < kSaveEveryTouches
        && now - m_lastSaveMs < kSaveEveryMs)
        return;
    saveIndex(nullptr);
}

void VideoSequenceService::rememberMemory(const AssetHash& hash,
                                          const QImage& image)
{
    if (image.isNull() || m_memory.maxCost() <= 0)
        return;
    m_memory.insert(hash, new QImage(image), 1);
}

void VideoSequenceService::evict(const AssetHash& hash)
{
    auto it = m_images.find(hash);
    if (it == m_images.end())
        return;

    QFile::remove(webpPath(hash));
    m_memory.remove(hash);
    m_totalBytes = std::max<qint64>(0, m_totalBytes - it->size);
    m_images.erase(it);

    for (auto frame = m_frames.begin(); frame != m_frames.end();)
    {
        if (frame.value() == hash)
            frame = m_frames.erase(frame);
        else
            ++frame;
    }
    m_indexDirty = true;
}

void VideoSequenceService::enforceDiskBudget()
{
    const qint64 budget = std::max<qint64>(0, m_options.diskBudgetBytes);
    while (m_totalBytes > budget && !m_images.isEmpty())
    {
        AssetHash oldest;
        qint64 oldestAccess = std::numeric_limits<qint64>::max();
        for (auto it = m_images.cbegin(); it != m_images.cend(); ++it)
        {
            if (it->lastAccess < oldestAccess
                || (it->lastAccess == oldestAccess
                    && (oldest.isEmpty() || it.key() < oldest)))
            {
                oldestAccess = it->lastAccess;
                oldest = it.key();
            }
        }
        if (oldest.isEmpty())
            break;
        evict(oldest);
    }
}

bool VideoSequenceService::extract(
    const std::function<bool(const SequenceProgress&)>& progress)
{
    m_lastError.clear();
    m_cancelled.store(false);

    auto fail = [this](const QString& message) {
        if (m_ready)
            saveIndex(nullptr); // keep frames completed before the failure
        m_lastError = message;
        return false;
    };

    if (!m_ready)
        return fail(m_setupError.isEmpty() ? QStringLiteral("缓存未就绪")
                                           : m_setupError);
    if (m_options.videoPath.isEmpty()
        || !QFileInfo(m_options.videoPath).isFile())
        return fail(QStringLiteral("视频文件不存在: %1")
                        .arg(m_options.videoPath));

    QString probeError;
    const assets::Probe probe =
        assets::probe(m_options.videoPath, AssetType::Video, &probeError);
    if (!probeError.isEmpty())
        return fail(probeError);
    if (probe.durationSeconds <= 0)
        return fail(QStringLiteral("无法读取视频时长"));

    const int total =
        int(std::llround(probe.durationSeconds * double(m_options.projectFps)));
    if (total <= 0)
        return fail(QStringLiteral("视频时长不足以产生一帧"));

    m_totalFrames = total;
    for (auto it = m_frames.begin(); it != m_frames.end();)
    {
        if (it.key() >= total)
            it = m_frames.erase(it);
        else
            ++it;
    }

    auto stopped = [&](const SequenceProgress& current) {
        return m_cancelled.load() || current.cancelled;
    };

    SequenceProgress current{0, total, false};
    if (progress && (!progress(current) || stopped(current)))
    {
        saveIndex(nullptr);
        return false;
    }

    for (int frame = 0; frame < total; ++frame)
    {
        if (m_cancelled.load())
        {
            saveIndex(nullptr);
            return false;
        }

        const AssetHash existing = m_frames.value(frame);
        AssetHash hash;
        bool resumable = !existing.isEmpty() && imageAvailable(existing);
        if (resumable)
        {
            hash = existing;
            touch(hash);
        }
        else
        {
            Request request;
            request.path = m_options.videoPath;
            request.sourceFrame = frame;
            request.fps = m_options.projectFps;
            request.proxy = true;

            QString decodeError;
            QImage image = m_decoder.frame(request, &decodeError);
            if (image.isNull())
                return fail(decodeError.isEmpty()
                                ? QStringLiteral("无法解码第 %1 帧").arg(frame)
                                : decodeError);

            if (m_options.maxWidth > 0 && image.width() > m_options.maxWidth)
                image = image.scaledToWidth(m_options.maxWidth,
                                            Qt::SmoothTransformation);
            if (image.format() != QImage::Format_RGBA8888)
                image = image.convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull())
                return fail(QStringLiteral("无法转换第 %1 帧").arg(frame));

            hash = contentHash(image);
            if (hash.isEmpty())
                return fail(QStringLiteral("无法计算第 %1 帧的哈希").arg(frame));

            const bool stored = m_images.contains(hash) && imageAvailable(hash);
            if (!stored)
            {
                // Drop a stale entry whose file was removed outside the
                // service before rewriting the content-addressed file.
                auto stale = m_images.find(hash);
                if (stale != m_images.end())
                {
                    m_totalBytes =
                        std::max<qint64>(0, m_totalBytes - stale->size);
                    m_images.erase(stale);
                    m_memory.remove(hash);
                }

                const QByteArray webp = encodeWebp(image);
                if (webp.isEmpty())
                    return fail(QStringLiteral("无法编码第 %1 帧为 WebP")
                                    .arg(frame));

                QString writeError;
                if (!writeFile(webpPath(hash), webp, &writeError))
                    return fail(QStringLiteral("无法写入帧文件: %1")
                                    .arg(writeError));

                ImageEntry entry;
                entry.size = webp.size();
                entry.width = image.width();
                entry.height = image.height();
                m_clock = std::max(QDateTime::currentMSecsSinceEpoch(),
                                   m_clock + 1);
                entry.lastAccess = m_clock;
                m_images.insert(hash, entry);
                m_totalBytes += entry.size;
                m_indexDirty = true;
                enforceDiskBudget();
            }
            else
            {
                touch(hash);
            }

            if (m_images.contains(hash))
            {
                m_frames.insert(frame, hash);
                rememberMemory(hash, image);
            }
            else
            {
                // A tiny disk budget evicted the frame immediately.
                m_frames.remove(frame);
            }
        }

        ++current.done;
        current.total = total;
        current.cancelled = false;
        if (progress && (!progress(current) || stopped(current)))
        {
            saveIndex(nullptr);
            return false;
        }

        if ((frame + 1) % kSaveEveryFrames == 0)
        {
            QString saveError;
            if (!saveIndex(&saveError))
                return fail(saveError);
        }
    }

    enforceDiskBudget();
    QString saveError;
    if (!saveIndex(&saveError))
        return fail(saveError);
    return true;
}

QImage VideoSequenceService::frame(int projectFrame, QString* error)
{
    if (error)
        error->clear();

    auto failed = [error](const QString& message) {
        if (error)
            *error = message;
        return QImage();
    };

    if (!m_ready)
        return failed(m_setupError.isEmpty() ? QStringLiteral("缓存未就绪")
                                             : m_setupError);
    if (projectFrame < 0)
        return failed(QStringLiteral("帧号不能为负数"));
    if (m_totalFrames > 0 && projectFrame >= m_totalFrames)
        return failed(QStringLiteral("帧号超出范围: %1").arg(projectFrame));

    AssetHash hash = m_frames.value(projectFrame);
    if (!hash.isEmpty() && m_images.contains(hash))
    {
        if (!imageAvailable(hash))
        {
            // The WebP disappeared or was emptied; hasFrame() is now false.
            evict(hash);
        }
        else
        {
            if (QImage* cached = m_memory.object(hash))
            {
                ++m_stats.memoryHits;
                touch(hash);
                scheduleIndexSave();
                return *cached;
            }

            const QImage decoded = decodeWebp(webpPath(hash));
            if (!decoded.isNull())
            {
                ++m_stats.diskHits;
                touch(hash);
                rememberMemory(hash, decoded);
                scheduleIndexSave();
                return decoded;
            }
            evict(hash);
        }
    }
    else if (!hash.isEmpty())
    {
        m_frames.remove(projectFrame);
        m_indexDirty = true;
    }

    // Cache miss or an evicted frame: decode the original video.
    ++m_stats.fallbacks;
    if (m_options.videoPath.isEmpty()
        || !QFileInfo(m_options.videoPath).isFile())
        return failed(QStringLiteral("视频文件不存在: %1")
                          .arg(m_options.videoPath));

    Request request;
    request.path = m_options.videoPath;
    request.sourceFrame = projectFrame;
    request.fps = m_options.projectFps;
    request.proxy = true;

    QString decodeError;
    const QImage image = m_decoder.frame(request, &decodeError);
    if (image.isNull())
        return failed(decodeError.isEmpty()
                          ? QStringLiteral("无法解码第 %1 帧").arg(projectFrame)
                          : decodeError);
    return image;
}

bool VideoSequenceService::hasFrame(int projectFrame) const
{
    if (projectFrame < 0)
        return false;
    if (m_totalFrames > 0 && projectFrame >= m_totalFrames)
        return false;

    const AssetHash hash = m_frames.value(projectFrame);
    if (hash.isEmpty())
        return false;
    return imageAvailable(hash);
}

void VideoSequenceService::clear()
{
    m_memory.clear();
    m_frames.clear();
    m_images.clear();
    m_totalBytes = 0;
    m_totalFrames = 0;
    m_indexDirty = false;
    m_touchesSinceSave = 0;
    m_cancelled.store(false);
    m_lastError.clear();
    m_stats = Stats();
    m_decoder.clear();
    removeAllFiles();
}

VideoSequenceService::Stats VideoSequenceService::stats() const
{
    Stats result = m_stats;
    result.uniqueImages = m_images.size();
    return result;
}

void VideoSequenceService::cancel()
{
    m_cancelled.store(true);
}

int VideoSequenceService::totalFrames() const
{
    return m_totalFrames;
}

QString VideoSequenceService::cacheDirectory() const
{
    return m_cacheDir;
}

QString VideoSequenceService::indexFilePath() const
{
    return m_indexPath;
}

QString VideoSequenceService::lastError() const
{
    return m_lastError;
}

} // namespace pvf::video2
