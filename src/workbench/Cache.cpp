#include "workbench/Cache.h"

#include "framecache/ImageLru.h"
#include "framecache/LegacyImport.h"
#include "framecache/Store.h"
#include "perf/FrameProfiler.h"
#include "workbench/Qoi.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>

#include <utility>

namespace pvf::workbench {
namespace {

constexpr int kWebpQuality = 80;

const QString kQoiCodec = QStringLiteral("qoi");
const QString kWebpCodec = QStringLiteral("webp");

QString normalizedCodec(const QString& codec)
{
    return codec == kQoiCodec ? kQoiCodec : kWebpCodec;
}

QString sha1Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

// Options-only directory key.  Project fps/size/background are part of
// FrameMeta, so a project change still invalidates every frame even when the
// directories are shared.  renderSignature() remains the project-wide key
// exposed to callers that want to nest the cache root per project.
QString directorySignature(const Options& options)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(1) << options.rendererVersion << qint32(options.scalePercent)
        << qint32(options.expandPercent)
        << quint8(options.transparentOutsideCanvas ? 1 : 0)
        << options.targetSize << options.chromeVersion;
    return QStringLiteral("wb1-") + sha1Hex(payload);
}

QString safeComponent(QString value)
{
    if (value.isEmpty())
        return QStringLiteral("nocamera");
    for (QChar& character : value)
    {
        if (!character.isLetterOrNumber() && character != QLatin1Char('.')
            && character != QLatin1Char('_') && character != QLatin1Char('-'))
            character = QLatin1Char('_');
    }
    if (value == QLatin1String(".") || value == QLatin1String(".."))
        value.prepend(QLatin1Char('_'));
    return value;
}

QByteArray encodeWebp(const QImage& image)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};

    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(kWebpQuality);
    if (!writer.write(image))
        return {};
    buffer.close();
    return data;
}

QByteArray encodeFrame(const QImage& image, const QString& codec)
{
    perf::Scope encodeScope(codec == kQoiCodec ? "缓存编码/QOI"
                                               : "缓存编码/WebP",
                            perf::Category::Encode);
    return codec == kQoiCodec ? encodeQoi(image) : encodeWebp(image);
}

QImage decodeFrame(const QString& path, const QString& codec)
{
    perf::Scope decodeScope(codec == kQoiCodec ? "缓存解码/QOI"
                                               : "缓存解码/WebP",
                            perf::Category::Decode);
    if (codec == kQoiCodec)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return decodeQoi(file.readAll());
    }

    QImageReader reader(path, QByteArrayLiteral("webp"));
    reader.setAutoTransform(false);
    return reader.read();
}

QString frameKey(int frame)
{
    return QString::number(frame);
}

} // namespace

struct Cache::Impl
{
    QString directory;
    Options options;
    QString error;
    bool valid = false;

    std::unique_ptr<framecache::Store> store;
    framecache::ImageLru memory{0};
};

Cache::Cache(QString root, QString cameraKey, Options options, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->options = options;

    if (root.isEmpty())
    {
        d->error = QStringLiteral("cache root is empty");
        return;
    }

    d->directory =
        QDir(QDir(root).absolutePath())
            .filePath(directorySignature(options) + QLatin1Char('/')
                      + safeComponent(cameraKey));
    if (!QDir().mkpath(d->directory))
    {
        d->error = QStringLiteral("cannot create cache directory: %1")
                       .arg(d->directory);
        return;
    }

    // Pre-framecache caches kept their rows in a "frames" table.  Import them
    // explicitly; a failed import must abort the open so the orphan sweep
    // cannot delete frames whose metadata never made it into frame_entries.
    const framecache::LegacyImportResult imported =
        framecache::importLegacyIndex(d->directory, {QStringLiteral("frames")});
    if (!imported.ok())
    {
        d->error = imported.error;
        return;
    }

    framecache::Options storeOptions;
    storeOptions.directory = d->directory;
    storeOptions.budgetBytes = options.diskBudgetBytes;
    storeOptions.extensions = {kWebpCodec, kQoiCodec};

    d->store = std::make_unique<framecache::Store>(std::move(storeOptions));
    if (!d->store->isValid())
    {
        d->error = d->store->error();
        return;
    }

    // The old in-process LRU was created with the configured budget and then
    // evicted on every insert; keeping the same call order preserves the
    // "too small for one frame" behaviour (stats.memoryFrames == 0).
    d->memory.setBudgetBytes(options.memoryBudgetBytes);
    d->valid = true;
}

Cache::~Cache() = default;

bool Cache::isValid() const
{
    return d->valid;
}

QString Cache::error() const
{
    return d->error;
}

QString Cache::directory() const
{
    return d->directory;
}

QSize Cache::imageSize() const
{
    return d->options.targetSize;
}

QImage Cache::lookup(int frame, const FrameMeta& current, bool* hit)
{
    if (hit)
        *hit = false;
    if (!d->store || !d->valid)
    {
        if (d->store)
            d->store->noteMiss();
        return {};
    }

    const QString key = frameKey(frame);
    const auto record = d->store->record(key);
    if (!record)
    {
        d->store->noteMiss();
        return {};
    }

    const FrameMeta stored{record->payload, record->tag};
    if (!stored.equals(current))
    {
        // The old image deliberately stays on disk: it may still match after
        // the user undoes the edit.
        d->store->noteMiss();
        return {};
    }

    if (const std::optional<QImage> memory = d->memory.get(key))
    {
        d->store->touch(key);
        d->store->noteHit();
        if (hit)
            *hit = true;
        return *memory;
    }

    const QImage image =
        decodeFrame(d->store->pathFor(*record), record->codec);
    if (image.isNull())
    {
        // The index still matched but the file is gone/corrupt; drop the row.
        d->store->remove(key, true);
        d->store->noteMiss();
        return {};
    }

    d->store->touch(key);
    d->memory.put(key, image);
    d->store->noteHit();
    if (hit)
        *hit = true;
    return image;
}

EncodedFrame Cache::encode(const QImage& image, const QSize& targetSize,
                           bool hasRaster)
{
    EncodedFrame result;
    result.codec = hasRaster ? kWebpCodec : kQoiCodec;
    if (image.isNull())
        return result;

    QImage scaled = image;
    if (targetSize.isValid() && scaled.size() != targetSize)
    {
        perf::Scope scaleScope("缓存缩放", perf::Category::Other);
        scaled = scaled.scaled(targetSize, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }
    if (scaled.isNull())
        return result;

    result.image = scaled;
    result.bytes = encodeFrame(scaled, result.codec);
    return result;
}

bool Cache::store(int frame, const FrameMeta& meta, const QImage& image,
                  QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);
    if (image.isNull())
        return fail(QStringLiteral("cannot store a null image"));

    const EncodedFrame encoded =
        encode(image, d->options.targetSize, meta.hasRaster);
    if (encoded.bytes.isEmpty())
        return fail(encoded.codec == kQoiCodec
                        ? QStringLiteral("QOI encoding failed")
                        : QStringLiteral("WebP encoding failed"));
    return storeEncoded(frame, meta, encoded, error);
}

bool Cache::storeEncoded(int frame, const FrameMeta& meta,
                         const EncodedFrame& encoded, QString* error)
{
    auto fail = [&](const QString& message) {
        d->error = message;
        if (error)
            *error = message;
        return false;
    };

    if (!d->valid)
        return fail(d->error.isEmpty() ? QStringLiteral("cache is not valid")
                                       : d->error);
    if (encoded.bytes.isEmpty())
        return fail(QStringLiteral("cannot store empty encoded bytes"));

    const QString codec = normalizedCodec(encoded.codec);
    const QString key = frameKey(frame);

    framecache::Record record;
    record.key = key;
    record.codec = codec;
    record.payload = meta.bytes;
    record.tag = meta.sha1;

    {
        perf::Scope diskScope("缓存写盘", perf::Category::Disk);
        if (!d->store->put(record, encoded.bytes, error))
        {
            d->error = d->store->error();
            if (error && error->isEmpty())
                *error = d->error;
            return false;
        }
    }

    d->memory.put(key, encoded.image);
    perf::FrameProfiler::instance().noteCacheStore(codec == kWebpCodec);
    return true;
}

bool Cache::matches(int frame, const FrameMeta& current) const
{
    if (!d->valid || !d->store)
        return false;
    const auto record = d->store->record(frameKey(frame));
    if (!record)
        return false;
    return FrameMeta{record->payload, record->tag}.equals(current);
}

QSet<int> Cache::cachedFrames() const
{
    QSet<int> frames;
    if (!d->valid || !d->store)
        return frames;
    for (auto it = d->store->records().cbegin();
         it != d->store->records().cend(); ++it)
        frames.insert(it.key().toInt());
    return frames;
}

CacheStats Cache::stats() const
{
    CacheStats result;
    result.budgetBytes = d->options.diskBudgetBytes;
    if (d->store)
    {
        const framecache::Stats stats = d->store->stats();
        result.diskBytes = stats.diskBytes;
        result.budgetBytes = stats.budgetBytes;
        result.entries = stats.entries;
        result.hits = stats.hits;
        result.misses = stats.misses;
        result.stores = stats.stores;
        result.evictions = stats.evictions;
    }
    result.memoryFrames = d->memory.entryCount();
    return result;
}

void Cache::clear()
{
    if (!d->valid || !d->store)
        return;
    d->store->clear();
    d->memory.clear();
}

} // namespace pvf::workbench
