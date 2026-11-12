#pragma once

#include <QHash>
#include <QImage>
#include <QString>

#include <optional>

namespace pvf::framecache {

// Byte-budgeted LRU for decoded frames, shared by the workbench memory cache
// and htmlcache's MemoryCache.  QImage is implicitly shared, so get() returns
// a handle to the cached pixels rather than copying them.
//
// `tag` is an owner-defined grouping key (htmlcache stores the asset hash) so
// a whole asset can be dropped at once.  Eviction is deterministic: oldest
// lastAccess first, ties broken by key.
class ImageLru
{
public:
    explicit ImageLru(qint64 budgetBytes = 0);

    ImageLru(const ImageLru&) = delete;
    ImageLru& operator=(const ImageLru&) = delete;

    // Returns false when the image is null.  The image is evicted immediately
    // when it alone exceeds the budget, exactly like a normal LRU overflow.
    bool put(const QString& key, const QImage& image, const QString& tag = {});

    // Returns a handle to the cached image and refreshes its LRU position.
    std::optional<QImage> get(const QString& key);

    bool contains(const QString& key) const;
    void remove(const QString& key);
    int removeTag(const QString& tag);
    void clear();

    void setBudgetBytes(qint64 bytes);
    qint64 budgetBytes() const;
    qint64 bytes() const;
    int entryCount() const;
    quint64 evictions() const;

private:
    struct Slot
    {
        QImage image;
        QString tag;
        qint64 lastAccess = 0;
    };

    void enforceBudget();
    qint64 nextAccess();

    QHash<QString, Slot> m_slots;
    qint64 m_bytes = 0;
    qint64 m_budgetBytes = 0;
    qint64 m_clock = 0;
    quint64 m_evictions = 0;
};

} // namespace pvf::framecache
