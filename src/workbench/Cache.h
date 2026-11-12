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
};

// One camera's frame cache: <root>/<signature>/<cameraKey|nocamera>/.
//
// Frames without raster assets are stored as <frame>.qoi (built-in codec,
// lossless alpha, ~2 ms encode at 1280x720); frames containing an image, video
// or HTML asset stay <frame>.webp so photos do not balloon the disk.  A
// SQLite row holds the full FrameMeta plus the codec name.  A lookup only hits
// when the current meta is byte-identical to the stored one; stale images are
// left in place until store() overwrites them.  Loading the index also drops
// rows whose file disappeared and deletes frame files that have no row
// (leftovers from an interrupted store).
//
// Existing caches from the WebP-only format keep working: the codec column is
// added by ALTER TABLE with a 'webp' default, and orphan cleanup understands
// both extensions.
//
// Not thread-safe: the background scan worker owns its instance and the UI
// thread owns another one.  QSqlDatabase connections are bound to the thread
// that created the Cache (and are removed in the destructor).
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

    // Meta-only comparison; never touches image files or LRU counters.
    bool matches(int frame, const FrameMeta& current) const;

    QSet<int> cachedFrames() const;
    CacheStats stats() const;

    // Removes every frame file and index row in this camera directory.
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pvf::workbench
