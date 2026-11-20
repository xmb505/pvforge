#include "AudioStream.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pvf::audio2 {
namespace {

QString audioContentFile(const assets::StoredAsset& asset)
{
    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    return asset.directory + QLatin1Char('/') + files.first();
}

bool overlaps(const model::Entity& entity, int startFrame, int endFrame)
{
    return entity.content.kind == ContentKind::Asset
           && entity.content.asset.type == AssetType::Audio
           && entity.end > startFrame && entity.start < endFrame;
}

} // namespace

AudioSnapshot captureAudioSnapshot(const model::Project& project,
                                   const assets::AssetStore& assets,
                                   int startFrame, int endFrame, int fps,
                                   QStringList* warnings)
{
    AudioSnapshot snapshot;
    snapshot.options.startFrame = startFrame;
    snapshot.options.endFrame = endFrame;
    snapshot.options.fps = fps;
    snapshot.options.sampleRate = 48000;
    snapshot.options.channels = 2;
    snapshot.project = project;

    QSet<AssetHash> attempted;
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (!overlaps(entity, startFrame, endFrame))
            continue;
        const AssetHash hash = entity.content.asset.hash;
        if (attempted.contains(hash))
            continue;
        attempted.insert(hash);

        const auto stored = assets.get(hash);
        const QString path = stored ? audioContentFile(*stored) : QString();
        if (path.isEmpty())
        {
            if (warnings)
            {
                const QString name =
                    entity.name.isEmpty() ? entity.uuid : entity.name;
                warnings->append(
                    QStringLiteral("音频实体 \"%1\" 已跳过：素材缺失")
                        .arg(name));
            }
            continue;
        }
        snapshot.contentPaths.insert(hash, path);
    }

    return snapshot;
}

QByteArray mixSnapshotRange(const AudioSnapshot& snapshot, int startFrame,
                            int endFrame, QStringList* warnings)
{
    MixOptions options = snapshot.options;
    options.startFrame = startFrame;
    options.endFrame = endFrame;
    return mixRangeWithPaths(snapshot.project, snapshot.contentPaths, options,
                             warnings);
}

AudioStream::AudioStream(AudioSnapshot snapshot,
                         QAudioFormat::SampleFormat sampleFormat,
                         QObject* parent)
    : QIODevice(parent)
    , m_snapshot(std::move(snapshot))
    , m_sampleFormat(sampleFormat)
{
    const int sampleBytes = sampleFormat == QAudioFormat::Int16
                                ? int(sizeof(qint16))
                                : int(sizeof(float));
    // One second of ring buffer bounds memory regardless of clip length.  The
    // producer blocks once the buffer is full, so it cannot run ahead.
    m_capacity = qint64(m_snapshot.options.sampleRate)
                 * m_snapshot.options.channels * sampleBytes;
    m_ring.resize(int(m_capacity));
}

AudioStream::~AudioStream()
{
    cancel();
}

bool AudioStream::open(OpenMode mode)
{
    if (!QIODevice::open(mode | QIODevice::ReadOnly))
        return false;
    if (m_started)
        return true;
    m_started = true;
    m_thread = std::thread([this] { produce(); });
    return true;
}

void AudioStream::close()
{
    cancel();
    QIODevice::close();
}

void AudioStream::cancel()
{
    {
        QMutexLocker locker(&m_mutex);
        m_cancelled = true;
    }
    m_notEmpty.wakeAll();
    m_notFull.wakeAll();
    if (m_thread.joinable())
        m_thread.join();
}

int AudioStream::chunkFrames() const
{
    // Half a second per chunk: small enough to start quickly after a seek,
    // large enough that decoding overhead stays negligible.
    return std::max(1, m_snapshot.options.fps / 2);
}

QStringList AudioStream::warnings() const
{
    QMutexLocker locker(&m_mutex);
    return m_warnings;
}

bool AudioStream::finished() const
{
    QMutexLocker locker(&m_mutex);
    return m_finished;
}

qint64 AudioStream::bufferCapacity() const
{
    return m_capacity;
}

qint64 AudioStream::framesMixed() const
{
    return m_framesMixed.load(std::memory_order_relaxed);
}

bool AudioStream::push(const QByteArray& pcm)
{
    qint64 offset = 0;
    const qint64 total = pcm.size();
    while (offset < total)
    {
        QMutexLocker locker(&m_mutex);
        while (!m_cancelled && m_size >= m_capacity)
            m_notFull.wait(&m_mutex);
        if (m_cancelled)
            return false;

        const qint64 free = m_capacity - m_size;
        const qint64 count = std::min(free, total - offset);
        const qint64 first = std::min(count, m_capacity - m_tail);
        std::memcpy(m_ring.data() + m_tail, pcm.constData() + offset,
                    size_t(first));
        if (count > first)
        {
            std::memcpy(m_ring.data(), pcm.constData() + offset + first,
                        size_t(count - first));
        }
        m_tail = (m_tail + count) % m_capacity;
        m_size += count;
        offset += count;
        m_notEmpty.wakeAll();
    }
    return true;
}

QByteArray AudioStream::convertForDevice(const QByteArray& pcm) const
{
    if (m_sampleFormat != QAudioFormat::Int16)
        return pcm;

    const auto* source = reinterpret_cast<const float*>(pcm.constData());
    const int count = pcm.size() / int(sizeof(float));
    QByteArray converted;
    converted.resize(count * int(sizeof(qint16)));
    auto* target = reinterpret_cast<qint16*>(converted.data());
    for (int i = 0; i < count; ++i)
    {
        target[i] = qint16(std::lround(
            std::clamp(source[i], -1.0f, 1.0f) * 32767.0f));
    }
    return converted;
}

void AudioStream::produce()
{
    const MixOptions& options = m_snapshot.options;
    const int fps = std::max(1, options.fps);
    const int chunk = chunkFrames();
    int framesProduced = 0;
    const int totalFrames = std::max(0, options.endFrame - options.startFrame);
    QSet<QString> seenWarnings;

    while (framesProduced < totalFrames)
    {
        {
            QMutexLocker locker(&m_mutex);
            if (m_cancelled)
                return;
        }

        const int chunkStart = options.startFrame + framesProduced;
        const int chunkEnd = std::min(options.endFrame, chunkStart + chunk);
        const int chunkFrameCount = chunkEnd - chunkStart;
        if (chunkFrameCount <= 0)
            break;

        QStringList chunkWarnings;
        QByteArray pcm = mixSnapshotRange(m_snapshot, chunkStart, chunkEnd,
                                          &chunkWarnings);

        // The stream must stay sample-exact even when the chunk is silent.
        const int64_t samples = std::max<int64_t>(
            1, llround(double(chunkFrameCount) * double(options.sampleRate)
                       / double(fps)));
        const int bytesPerFrame = options.channels * int(sizeof(float));
        const int expectedBytes = int(samples) * bytesPerFrame;
        if (pcm.size() < expectedBytes)
        {
            const int missing = expectedBytes - pcm.size();
            pcm.append(QByteArray(missing, '\0'));
        }
        else if (pcm.size() > expectedBytes)
        {
            pcm.truncate(expectedBytes);
        }

        if (!chunkWarnings.isEmpty())
        {
            QMutexLocker locker(&m_mutex);
            for (const QString& warning : chunkWarnings)
            {
                if (seenWarnings.contains(warning))
                    continue;
                seenWarnings.insert(warning);
                m_warnings.append(warning);
            }
        }

        if (!push(convertForDevice(pcm)))
            return;

        framesProduced += chunkFrameCount;
        m_framesMixed.store(framesProduced, std::memory_order_relaxed);
    }

    QMutexLocker locker(&m_mutex);
    m_finished = true;
    m_notEmpty.wakeAll();
}

qint64 AudioStream::readData(char* data, qint64 maxSize)
{
    if (maxSize <= 0)
        return 0;

    QMutexLocker locker(&m_mutex);
    while (m_size == 0 && !m_finished && !m_cancelled)
    {
        // Wake periodically so cancel()/close() cannot deadlock a reader.
        m_notEmpty.wait(&m_mutex, 50);
    }
    if (m_cancelled)
        return -1;
    if (m_size == 0)
        return 0; // finished

    const qint64 count = std::min(maxSize, m_size);
    const qint64 first = std::min(count, m_capacity - m_head);
    std::memcpy(data, m_ring.constData() + m_head, size_t(first));
    if (count > first)
        std::memcpy(data + first, m_ring.constData(), size_t(count - first));
    m_head = (m_head + count) % m_capacity;
    m_size -= count;
    m_notFull.wakeAll();
    return count;
}

qint64 AudioStream::bytesAvailable() const
{
    QMutexLocker locker(&m_mutex);
    return m_size + QIODevice::bytesAvailable();
}

} // namespace pvf::audio2
