#include "framecache/ImageLru.h"

#include <QDateTime>

#include <algorithm>
#include <limits>

namespace pvf::framecache {

ImageLru::ImageLru(qint64 budgetBytes)
    : m_budgetBytes(std::max<qint64>(0, budgetBytes))
{
}

qint64 ImageLru::nextAccess()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_clock = std::max(now, m_clock + 1);
    return m_clock;
}

bool ImageLru::put(const QString& key, const QImage& image, const QString& tag)
{
    if (image.isNull())
        return false;

    remove(key);

    Slot slot;
    slot.image = image;
    slot.tag = tag;
    slot.lastAccess = nextAccess();
    m_slots.insert(key, slot);
    m_bytes += image.sizeInBytes();
    enforceBudget();
    return m_slots.contains(key);
}

std::optional<QImage> ImageLru::get(const QString& key)
{
    auto it = m_slots.find(key);
    if (it == m_slots.end())
        return std::nullopt;
    it->lastAccess = nextAccess();
    return it->image;
}

bool ImageLru::contains(const QString& key) const
{
    return m_slots.contains(key);
}

void ImageLru::remove(const QString& key)
{
    auto it = m_slots.find(key);
    if (it == m_slots.end())
        return;
    m_bytes = std::max<qint64>(0, m_bytes - it->image.sizeInBytes());
    m_slots.erase(it);
}

int ImageLru::removeTag(const QString& tag)
{
    QStringList keys;
    for (auto it = m_slots.cbegin(); it != m_slots.cend(); ++it)
    {
        if (it->tag == tag)
            keys.append(it.key());
    }
    for (const QString& key : keys)
        remove(key);
    return keys.size();
}

void ImageLru::clear()
{
    m_slots.clear();
    m_bytes = 0;
}

void ImageLru::setBudgetBytes(qint64 bytes)
{
    m_budgetBytes = std::max<qint64>(0, bytes);
    enforceBudget();
}

qint64 ImageLru::budgetBytes() const
{
    return m_budgetBytes;
}

qint64 ImageLru::bytes() const
{
    return m_bytes;
}

int ImageLru::entryCount() const
{
    return m_slots.size();
}

quint64 ImageLru::evictions() const
{
    return m_evictions;
}

void ImageLru::enforceBudget()
{
    while (m_bytes > m_budgetBytes && !m_slots.isEmpty())
    {
        QString victim;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (auto it = m_slots.cbegin(); it != m_slots.cend(); ++it)
        {
            if (it->lastAccess < oldest
                || (it->lastAccess == oldest
                    && (victim.isEmpty() || it.key() < victim)))
            {
                oldest = it->lastAccess;
                victim = it.key();
            }
        }
        if (victim.isEmpty())
            break;
        remove(victim);
        ++m_evictions;
    }
}

} // namespace pvf::framecache
