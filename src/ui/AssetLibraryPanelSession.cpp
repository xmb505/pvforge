#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "model/Query.h"

#include <QComboBox>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>

namespace pvf {

void AssetLibraryPanel::rebuildSessionEntries()
{
    m_entries.clear();
    if (!m_session)
    {
        applySessionFilter();
        return;
    }

    QSet<AssetHash> seen;
    auto addStored = [this, &seen](const assets::StoredAsset& asset) {
        if (asset.hash.isEmpty() || seen.contains(asset.hash))
            return;
        seen.insert(asset.hash);

        SessionEntry entry;
        entry.hash = asset.hash;
        entry.name = asset.name.isEmpty()
                         ? assetlibdetail::shortHash(asset.hash)
                         : asset.name;
        entry.directory = asset.directory;
        entry.type = asset.type;
        entry.tags = asset.tags;
        entry.hasAudio =
            !asset.metadata.value(QStringLiteral("audioHash")).toString().isEmpty();
        entry.width = asset.metadata.value(QStringLiteral("width")).toInt(0);
        entry.height = asset.metadata.value(QStringLiteral("height")).toInt(0);
        entry.seconds =
            asset.metadata.value(QStringLiteral("durationSeconds")).toDouble(0.0);
        entry.frames =
            asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
        entry.missing = assets::missingContent(asset);
        if (entry.missing)
            entry.missingReason = QStringLiteral("内容缺失");

        const QStringList contentFiles = QDir(asset.directory).entryList(
            {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
        if (!contentFiles.isEmpty())
            entry.content = asset.directory + QLatin1Char('/') + contentFiles.first();
        m_entries.append(entry);
    };

    for (const assets::StoredAsset& asset : m_session->assets().listIncludingMissing())
        addStored(asset);

    // Entities keep their hash after the store entry is gone. Keep one
    // offline row per referenced hash so re-importing the same content
    // restores it in place.
    QSet<AssetHash> missingSeen;
    for (const model::Entity& entity : m_session->project().entities)
    {
        if (entity.content.kind != ContentKind::Asset)
            continue;
        const AssetHash& hash = entity.content.asset.hash;
        if (hash.isEmpty() || seen.contains(hash) || missingSeen.contains(hash))
            continue;
        missingSeen.insert(hash);
        SessionEntry entry;
        entry.hash = hash;
        entry.name = assetlibdetail::shortHash(hash);
        entry.type = entity.content.asset.type;
        entry.missing = true;
        entry.missingReason = QStringLiteral("目录缺失");
        m_entries.append(entry);
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const SessionEntry& a, const SessionEntry& b) {
                  if (a.missing != b.missing)
                      return !a.missing;
                  const int byName =
                      QString::compare(a.name, b.name, Qt::CaseInsensitive);
                  if (byName != 0)
                      return byName < 0;
                  return a.hash < b.hash;
              });

    QStringList signature;
    for (const SessionEntry& entry : m_entries)
        signature << QStringLiteral("%1|%2|%3|%4|%5")
                         .arg(entry.hash)
                         .arg(entry.name)
                         .arg(entry.tags.join(QLatin1Char(',')))
                         .arg(entry.missingReason)
                         .arg(entry.missing ? 1 : 0);
    const bool changed = signature != m_sessionSignature;
    m_sessionSignature = signature;

    if (changed)
        rebuildSessionTagFilter();
    applySessionFilter();
}

void AssetLibraryPanel::rebuildSessionTagFilter()
{
    if (!m_tagFilter)
        return;

    const QString current = m_tagFilter->currentData().toString();
    QStringList tags;
    for (const SessionEntry& entry : m_entries)
        for (const QString& tag : entry.tags)
        {
            const bool exists =
                std::any_of(tags.cbegin(), tags.cend(), [&tag](const QString& seen) {
                    return QString::compare(seen, tag, Qt::CaseInsensitive) == 0;
                });
            if (!exists)
                tags.append(tag);
        }
    std::sort(tags.begin(), tags.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    const QSignalBlocker blocker(m_tagFilter);
    m_tagFilter->clear();
    m_tagFilter->addItem(QStringLiteral("全部标签"), QString());
    for (const QString& tag : tags)
        m_tagFilter->addItem(tag, tag);
    const int index = current.isEmpty() ? 0 : m_tagFilter->findData(current);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
}

void AssetLibraryPanel::applySessionFilter()
{
    if (!m_list)
        return;

    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const QString tagFilter =
        m_tagFilter ? m_tagFilter->currentData().toString() : QString();
    const QString selected = selectedHash();

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const SessionEntry& entry : m_entries)
    {
        if (!filter.isEmpty())
        {
            const bool matchesName =
                entry.name.contains(filter, Qt::CaseInsensitive);
            const bool matchesHash =
                entry.hash.contains(filter, Qt::CaseInsensitive);
            const bool matchesTag = std::any_of(
                entry.tags.cbegin(), entry.tags.cend(), [&filter](const QString& tag) {
                    return tag.contains(filter, Qt::CaseInsensitive);
                });
            if (!matchesName && !matchesHash && !matchesTag)
                continue;
        }
        if (!tagFilter.isEmpty()
            && std::none_of(entry.tags.cbegin(), entry.tags.cend(),
                            [&tagFilter](const QString& tag) {
                                return QString::compare(tag, tagFilter,
                                                        Qt::CaseInsensitive)
                                       == 0;
                            }))
            continue;
        m_list->addItem(makeSessionItem(entry));
    }

    int restore = m_list->count() > 0 ? 0 : -1;
    if (!selected.isEmpty())
    {
        for (int row = 0; row < m_list->count(); ++row)
            if (m_list->item(row)->data(Qt::UserRole).toString() == selected)
            {
                restore = row;
                break;
            }
    }
    m_list->setCurrentRow(restore);
    updateSessionSelection();
}

QListWidgetItem* AssetLibraryPanel::makeSessionItem(const SessionEntry& entry)
{
    auto* item = new QListWidgetItem;
    item->setIcon(QIcon(iconFor(entry)));
    item->setSizeHint(QSize(0, 52));

    const QString firstLine =
        entry.missing ? QStringLiteral("%1（素材缺失）").arg(entry.name) : entry.name;
    QString second = assetlibdetail::assetTypeLabel(entry.type);
    if (entry.type == AssetType::Image || entry.type == AssetType::Html)
    {
        if (entry.width > 0 && entry.height > 0)
            second += QStringLiteral(" · %1×%2").arg(entry.width).arg(entry.height);
    }
    else if (entry.type == AssetType::Video)
    {
        if (entry.width > 0 && entry.height > 0)
            second += QStringLiteral(" · %1×%2").arg(entry.width).arg(entry.height);
        if (entry.seconds > 0.0)
            second += QStringLiteral(" · %1 秒").arg(entry.seconds, 0, 'f', 2);
    }
    else if (entry.type == AssetType::Audio && entry.seconds > 0.0)
    {
        second += QStringLiteral(" · %1 秒").arg(entry.seconds, 0, 'f', 2);
    }
    if (entry.hasAudio)
        second += QStringLiteral(" · 含音频");
    if (!entry.tags.isEmpty())
    {
        QStringList decorated;
        for (const QString& tag : entry.tags)
            decorated.append(QLatin1Char('#') + tag);
        second += QStringLiteral(" · ") + decorated.join(QStringLiteral(", "));
    }
    item->setText(firstLine + QLatin1Char('\n') + second);

    item->setData(Qt::UserRole, entry.hash);
    item->setData(Qt::UserRole + 1, 2);
    item->setData(Qt::UserRole + 2, entry.missingReason);
    if (entry.missing)
        item->setForeground(QColor(QStringLiteral("#ff9a9a")));

    QString tooltip = QStringLiteral("名称：%1\n类型：%2\n哈希：%3")
                          .arg(entry.name,
                               assetlibdetail::assetTypeLabel(entry.type),
                               entry.hash);
    if (entry.frames > 0)
        tooltip += QStringLiteral("\n帧数：%1").arg(entry.frames);
    tooltip += QStringLiteral("\n标签：%1").arg(
        entry.tags.isEmpty() ? QStringLiteral("（无）")
                             : entry.tags.join(QStringLiteral(", ")));
    if (!entry.directory.isEmpty())
        tooltip += QStringLiteral("\n路径：%1").arg(entry.directory);
    if (entry.missing)
        tooltip += QStringLiteral("\n\n%1：实体仍引用该哈希。重新导入相同内容即可恢复。")
                       .arg(entry.missingReason);
    else if (entry.hasAudio)
        tooltip += QStringLiteral("\n\n已拆出 WAV 音频素材。");
    item->setToolTip(tooltip);
    return item;
}

void AssetLibraryPanel::updateSessionSelection()
{
    const QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    const QString hash = item ? item->data(Qt::UserRole).toString() : QString();
    const bool missing =
        item && !item->data(Qt::UserRole + 2).toString().isEmpty();
    if (m_create)
        m_create->setEnabled(item && !missing);
    if (m_remove)
        m_remove->setEnabled(
            item && m_session
            && m_session->assets().getIncludingMissing(hash).has_value());
    if (!item)
    {
        if (m_info)
        {
            m_info->setText(m_entries.isEmpty() ? QStringLiteral("素材库为空")
                                                : QStringLiteral("没有匹配的素材"));
            m_info->setToolTip(QString());
        }
        return;
    }

    const auto found = std::find_if(
        m_entries.cbegin(), m_entries.cend(),
        [&hash](const SessionEntry& entry) { return entry.hash == hash; });
    if (found == m_entries.cend())
        return;

    QString info = assetlibdetail::assetTypeLabel(found->type);
    if (found->width > 0 && found->height > 0)
        info += QStringLiteral(" · %1×%2").arg(found->width).arg(found->height);
    if (found->seconds > 0.0)
        info += QStringLiteral(" · %1 秒").arg(found->seconds, 0, 'f', 2);
    if (found->missing)
        info += QStringLiteral(" · %1").arg(found->missingReason);
    if (!found->tags.isEmpty())
        info += QStringLiteral("\n标签：%1").arg(found->tags.join(QStringLiteral(", ")));
    if (!found->directory.isEmpty())
        info += QStringLiteral("\n") + assetlibdetail::elidedPath(found->directory);
    info += QStringLiteral("\n") + found->hash;
    if (m_info)
    {
        m_info->setText(info);
        m_info->setToolTip(found->directory.isEmpty() ? found->hash
                                                      : found->directory);
    }
}

QString AssetLibraryPanel::selectedHash() const
{
    if (!m_list)
        return {};
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

int AssetLibraryPanel::assetCount() const
{
    return m_list ? m_list->count() : 0;
}

QString AssetLibraryPanel::assetNameAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    const QString text = m_list->item(index)->text();
    return text.section(QLatin1Char('\n'), 0, 0);
}

QString AssetLibraryPanel::assetHashAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(Qt::UserRole).toString();
}

QStringList AssetLibraryPanel::assetTagsAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    const QString hash = m_list->item(index)->data(Qt::UserRole).toString();
    for (const SessionEntry& entry : m_entries)
        if (entry.hash == hash)
            return entry.tags;
    return {};
}

bool AssetLibraryPanel::assetMissingAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return false;
    return !m_list->item(index)->data(Qt::UserRole + 2).toString().isEmpty();
}

QString AssetLibraryPanel::assetMissingReasonAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(Qt::UserRole + 2).toString();
}

QPixmap AssetLibraryPanel::assetThumbnailAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->icon().pixmap(48, 48);
}

void AssetLibraryPanel::setSearchText(const QString& text)
{
    if (!m_search)
        return;
    m_search->setText(text);
}

QString AssetLibraryPanel::searchText() const
{
    return m_search ? m_search->text() : QString();
}

QStringList AssetLibraryPanel::tagFilters() const
{
    QStringList tags;
    if (!m_tagFilter)
        return tags;
    for (int index = 1; index < m_tagFilter->count(); ++index)
        tags.append(m_tagFilter->itemData(index).toString());
    return tags;
}

void AssetLibraryPanel::setTagFilter(const QString& tag)
{
    if (!m_tagFilter)
        return;
    const int index = m_tagFilter->findData(tag);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
}

QString AssetLibraryPanel::tagFilter() const
{
    return m_tagFilter ? m_tagFilter->currentData().toString() : QString();
}

AssetType AssetLibraryPanel::entryTypeForHash(const AssetHash& hash) const
{
    for (const SessionEntry& entry : m_entries)
        if (entry.hash == hash)
            return entry.type;
    return AssetType::Image;
}

int AssetLibraryPanel::referenceCount(const AssetHash& hash) const
{
    if (!m_session)
        return 0;
    int count = 0;
    for (const model::Entity& entity : m_session->project().entities)
        if (entity.content.kind == ContentKind::Asset
            && entity.content.asset.hash == hash)
            ++count;
    return count;
}

QString AssetLibraryPanel::displayNameFor(const AssetHash& hash) const
{
    if (m_session)
        if (const auto asset = m_session->assets().getIncludingMissing(hash))
            return asset->name.isEmpty() ? assetlibdetail::shortHash(hash)
                                         : asset->name;
    return assetlibdetail::shortHash(hash);
}

} // namespace pvf
