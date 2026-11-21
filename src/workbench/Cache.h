#pragma once

#include "workbench/MetaBuilder.h"

#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

#include <memory>

namespace pvf::workbench {

struct CacheStats
{
    qint64 diskBytes = 0;
    qint64 budgetBytes = 0;
    int entries = 0;
    int memoryFrames = 0;
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 stores = 0;
    quint64 evictions = 0;
    // Filled by WorkbenchServices: queued or in-flight asynchronous stores.
    int pendingStores = 0;
};

// Result of Cache::encode(): the encoded file bytes, its codec, and the scaled
// image that belongs in the memory LRU.  All three are implicitly shared, so
// moving one between the encode worker and the UI thread does not copy pixels.
struct EncodedFrame
{
    QByteArray bytes;
    QString codec;
    QImage image;
};

// Stable content key for a frame: the 40-character lowercase hex SHA-1 from
// FrameMeta::sha1 (bytes are hashed only when a caller built a meta by hand).
// Identical visual states share one key, so different frames reuse one record.
QString frameContentKey(const FrameMeta& meta);

// One camera's frame cache: <root>/<signature>/<cameraKey|nocamera>/.
//
// Records are addressed by frameContentKey(meta), not by frame number: frames
// whose picture is unchanged share a single <sha1>.qoi / <sha1>.webp file.
// Frames without raster assets use the built-in QOI codec (lossless alpha,
// ~2 ms encode at 1280x720); frames containing an image, video or HTML asset
// stay WebP so photos do not balloon the disk.  The shared framecache Store
// keeps one row per content key in frame_entries, with the full FrameMeta in
// its payload blob and the sha1 in its tag blob.  A lookup only hits when the
// current meta is byte-identical to the stored one; stale images are left in
// place until a new content key is stored (the old one may match after undo).
// Loading the index also drops rows whose file disappeared and deletes frame
// files that have no row (leftovers from an interrupted store).
//
// Existing caches from the WebP-only format and the v2 codec column are
// imported into frame_entries on first open (the old table is kept as a
// read-only backup), and orphan cleanup understands both extensions.  Rows
// written by the pre-content-addressed workbench keep their numeric keys and
// are simply never matched again; kMetaFormat invalidates their payloads too.
//
// Encoding is a pure function of (image, targetSize, hasRaster): encode() may
// run on a worker thread, while storeEncoded() and store() write the file and
// index on the cache-owning (UI) thread.
//
// Not thread-safe: the background scan worker owns its instance and the UI
// thread owns another one.  framecache::Store connections are bound to the
// thread that created the Cache (and are removed in the destructor).
class Cache : public QObject
{
public:
    Cache(QString root, QString cameraKey, Options options,
          QObject* parent = nullptr);
    ~Cache() override;

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    bool isValid() const;
    QString error() const;
    QString directory() const;
    QSize imageSize() const;

    // Returns a null image on a miss.  `hit` distinguishes "meta matched but
    // image was unreadable" (false) from a real hit (true).
    QImage lookup(int frame, const FrameMeta& current, bool* hit = nullptr);

    // Scales to options.targetSize when needed, then encodes QOI for pure
    // vector frames or WebP quality 80 when the frame contains raster content.
    // Writes atomically, updates the index and both LRU budgets.
    bool store(int frame, const FrameMeta& meta, const QImage& image,
               QString* error = nullptr);

    // Pure image work: smooth-scale to `targetSize`, then encode QOI (vector)
    // or WebP q80 (raster).  Touches no Cache state and no SQLite connection,
    // so a background writer may call it while the UI thread uses this cache.
    static EncodedFrame encode(const QImage& image, const QSize& targetSize,
                               bool hasRaster);

    // Writes a frame produced by encode() and updates the index, memory LRU
    // and disk budget.  Must run on the thread that owns the Cache: SQLite
    // connections are thread-bound.  store() is this function plus encode().
    bool storeEncoded(int frame, const FrameMeta& meta,
                      const EncodedFrame& encoded, QString* error = nullptr);

    // Meta-only comparison; never touches image files or LRU counters.
    bool matches(int frame, const FrameMeta& current) const;

    // Frames whose content this process has stored or looked up and whose
    // record is still on disk.  The index only persists content keys, so a
    // reopened cache returns an empty set until a frame is looked up; use
    // matches() (or lookup()) as the persisted authority.
    QSet<int> cachedFrames() const;
    CacheStats stats() const;

    // Removes every frame file and index row in this camera directory.
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pvf::workbench
