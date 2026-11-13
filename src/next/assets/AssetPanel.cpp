#include "next/assets/AssetPanel.h"

#include "AssetPanelInternal.h"
#include "video2/VideoExtractionQueue.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace pvf::next {

namespace {

QString typeLabel(AssetType type)
{
    switch (type)
    {
    case AssetType::Image:
        return QStringLiteral("图片");
    case AssetType::Video:
        return QStringLiteral("视频");
    case AssetType::Audio:
        return QStringLiteral("音频");
    case AssetType::Html:
        return QStringLiteral("HTML");
    }
    return QStringLiteral("素材");
}

void drawCheckerboard(QPainter& painter, const QRect& rect)
{
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, QColor(QStringLiteral("#171b22")));
    constexpr int cell = 7;
    for (int y = rect.top(); y <= rect.bottom(); y += cell)
    {
        for (int x = rect.left(); x <= rect.right(); x += cell)
        {
            if (((x - rect.left()) / cell + (y - rect.top()) / cell) % 2 == 0)
                painter.fillRect(QRect(x, y, cell, cell), QColor(QStringLiteral("#252b35")));
        }
    }
    painter.restore();
}

void drawIconFrame(QPainter& painter, const QPixmap& pixmap)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(QStringLiteral("#2d3542")), 1));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
}

// QListWidget with single-asset drag-out. The item under the cursor supplies
// the hash; the panel supplies the MIME data.
class AssetListWidget : public QListWidget
{
public:
    explicit AssetListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
    }

    std::function<QMimeData*(const QString&)> mimeFactory;

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem* item = currentItem();
        if (!item || item->data(detail::kMissingRole).toBool())
            return;
        const QString hash = item->data(detail::kHashRole).toString();
        if (hash.isEmpty() || !mimeFactory)
            return;

        std::unique_ptr<QMimeData> data(mimeFactory(hash));
        if (!data)
            return;

        QDrag drag(this);
        drag.setMimeData(data.release());
        const QIcon icon = item->icon();
        if (!icon.isNull())
            drag.setPixmap(icon.pixmap(detail::thumbSize()));
        drag.exec(Qt::CopyAction);
    }
};

} // namespace

AssetPanel::AssetPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("AssetPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);
    setMinimumWidth(260);

    setStyleSheet(QStringLiteral(R"(
        AssetPanel { background: #151920; }
        QLineEdit {
            background: #0d1015;
            border: 1px solid #2b323e;
            padding: 5px 7px;
            color: #e4e9f1;
            selection-background-color: #2b4a6f;
        }
        QPushButton {
            background: #212834;
            border: 1px solid #333d4c;
            padding: 6px 10px;
            color: #e4e9f1;
        }
        QPushButton:hover { background: #2a3341; }
        QPushButton:pressed { background: #1a2029; }
        QPushButton:disabled { color: #626d7e; border-color: #262d38; }
        QComboBox {
            background: #0d1015;
            border: 1px solid #2b323e;
            padding: 4px 7px;
            color: #e4e9f1;
        }
        QComboBox QAbstractItemView {
            background: #1b2029;
            border: 1px solid #333d4c;
            color: #e4e9f1;
            selection-background-color: #24405c;
        }
        QListWidget {
            background: #10141a;
            border: 1px solid #262e3a;
            color: #d8dfe9;
            outline: 0;
        }
        QListWidget::item {
            padding: 6px 4px;
            border-bottom: 1px solid #1b212b;
        }
        QListWidget::item:selected {
            background: #24405c;
            color: #ffffff;
        }
        QMenu {
            background: #1b2029;
            border: 1px solid #333d4c;
            color: #e4e9f1;
            padding: 4px;
        }
        QMenu::item { padding: 5px 18px; }
        QMenu::item:selected { background: #24405c; }
        QLabel#AssetPanelStatus { color: #8b96a8; }
        QFrame#VideoExtractionRow {
            background: #10141a;
            border: 1px solid #262e3a;
        }
        QLabel#VideoExtractionName { color: #d8dfe9; }
        QLabel#VideoExtractionText { color: #8b96a8; }
        QLabel#VideoExtractionError { color: #ff9a9a; }
        QProgressBar#VideoExtractionBar {
            background: #0d1015;
            border: 1px solid #2b323e;
        }
        QProgressBar#VideoExtractionBar::chunk { background: #4c8ed9; }
        QPushButton#VideoExtractionCancel { padding: 1px 6px; }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* top = new QHBoxLayout;
    top->setSpacing(6);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索素材…"));
    m_search->setClearButtonEnabled(true);
    top->addWidget(m_search, 1);

    m_importButton = new QPushButton(tr("导入素材"), this);
    auto* importMenu = new QMenu(m_importButton);
    QAction* importFilesAction = importMenu->addAction(tr("导入文件…"));
    QAction* importHtmlAction = importMenu->addAction(tr("导入 .pvhtml 目录…"));
    connect(importFilesAction, &QAction::triggered, this, &AssetPanel::chooseImportFiles);
    connect(importHtmlAction, &QAction::triggered, this, &AssetPanel::chooseImportPvhtml);
    m_importButton->setMenu(importMenu);
    top->addWidget(m_importButton);
    root->addLayout(top);

    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(6);
    auto* filterLabel = new QLabel(tr("标签"), this);
    filterLabel->setObjectName(QStringLiteral("AssetPanelStatus"));
    filterRow->addWidget(filterLabel);
    m_tagFilter = new QComboBox(this);
    m_tagFilter->setObjectName(QStringLiteral("AssetTagFilter"));
    m_tagFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tagFilter->addItem(tr("全部标签"), QString());
    filterRow->addWidget(m_tagFilter, 1);
    root->addLayout(filterRow);

    auto* list = new AssetListWidget(this);
    list->mimeFactory = [this](const QString& hash) { return assetMimeData(hash); };
    m_list = list;
    m_list->setIconSize(detail::thumbSize());
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setWordWrap(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setToolTip(tr("拖入图片/视频/音频或 .pvhtml 目录来导入；拖出素材可放到时间轴"));
    root->addWidget(m_list, 1);

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(6);
    m_status = new QLabel(tr("拖入文件即可导入"), this);
    m_status->setObjectName(QStringLiteral("AssetPanelStatus"));
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottom->addWidget(m_status, 1);

    m_renameButton = new QPushButton(tr("重命名"), this);
    m_renameButton->setEnabled(false);
    connect(m_renameButton, &QPushButton::clicked, this, &AssetPanel::renameSelected);
    bottom->addWidget(m_renameButton);

    m_tagButton = new QPushButton(tr("标签…"), this);
    m_tagButton->setEnabled(false);
    connect(m_tagButton, &QPushButton::clicked, this, &AssetPanel::editSelectedTags);
    bottom->addWidget(m_tagButton);

    m_deleteButton = new QPushButton(tr("删除"), this);
    m_deleteButton->setEnabled(false);
    connect(m_deleteButton, &QPushButton::clicked, this, &AssetPanel::deleteSelected);
    bottom->addWidget(m_deleteButton);
    root->addLayout(bottom);

    m_progressArea = new QWidget(this);
    m_progressArea->setObjectName(QStringLiteral("AssetPanelProgress"));
    m_progressLayout = new QVBoxLayout(m_progressArea);
    m_progressLayout->setContentsMargins(0, 0, 0, 0);
    m_progressLayout->setSpacing(4);
    m_progressArea->hide();
    root->addWidget(m_progressArea);

    m_extractionQueue = new video2::VideoExtractionQueue(this);
    connect(m_extractionQueue, &video2::VideoExtractionQueue::started, this,
            [this](const QString& hash, int) { ensureProgressRow(hash); });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::progress, this,
            [this](const QString& hash, int done, int total) {
                updateProgressRow(hash, done, total);
            });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::finished, this,
            [this](const QString& hash) { finishProgressRow(hash); });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::failed, this,
            [this](const QString& hash, const QString& error) {
                failProgressRow(hash, error);
            });
    connect(m_extractionQueue, &video2::VideoExtractionQueue::cancelled, this,
            [this](const QString& hash) { removeProgressRow(hash); });

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(m_tagFilter, &QComboBox::currentIndexChanged, this,
            [this](int) { applyFilter(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            &AssetPanel::updateSelectionState);
    connect(m_list, &QWidget::customContextMenuRequested, this,
            &AssetPanel::showContextMenu);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    if (m_document)
        connect(m_document, &Document::changed, this, &AssetPanel::refresh);

    refresh();
}

AssetPanel::~AssetPanel()
{
    if (m_extractionQueue)
    {
        m_extractionQueue->cancelAll();
        delete m_extractionQueue;
        m_extractionQueue = nullptr;
    }
}

void AssetPanel::refresh()
{
    rebuildEntries();
    rebuildTagFilter();
    applyFilter();
}

void AssetPanel::setSearchText(const QString& text)
{
    if (m_search)
        m_search->setText(text);
    applyFilter();
}

QString AssetPanel::searchText() const
{
    return m_search ? m_search->text() : QString();
}

int AssetPanel::assetCount() const
{
    return m_list ? m_list->count() : 0;
}

QString AssetPanel::assetNameAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(detail::kNameRole).toString();
}

QString AssetPanel::assetHashAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(detail::kHashRole).toString();
}

QStringList AssetPanel::assetTagsAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(detail::kTagsRole).toStringList();
}

QStringList AssetPanel::tagFilters() const
{
    QStringList tags;
    if (!m_tagFilter)
        return tags;
    for (int i = 1; i < m_tagFilter->count(); ++i)
        tags.append(m_tagFilter->itemText(i));
    return tags;
}

void AssetPanel::setTagFilter(const QString& tag)
{
    if (!m_tagFilter)
        return;
    const int index = tag.isEmpty() ? 0 : m_tagFilter->findData(tag);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
    applyFilter();
}

QString AssetPanel::tagFilter() const
{
    return m_tagFilter ? m_tagFilter->currentData().toString() : QString();
}

bool AssetPanel::assetMissingAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return false;
    return m_list->item(index)->data(detail::kMissingRole).toBool();
}

void AssetPanel::rebuildEntries()
{
    m_entries.clear();
    if (!m_document)
        return;

    QSet<AssetHash> seen;

    auto addStored = [this, &seen](const assets::StoredAsset& asset) {
        if (seen.contains(asset.hash))
            return;
        seen.insert(asset.hash);

        Entry entry;
        entry.hash = asset.hash;
        entry.name = asset.name.isEmpty() ? detail::shortHash(asset.hash) : asset.name;
        entry.directory = asset.directory;
        entry.type = asset.type;
        entry.tags = asset.tags;
        entry.hasAudio = !asset.metadata.value(QStringLiteral("audioHash")).toString().isEmpty();

        const QStringList contentFiles = QDir(asset.directory).entryList(
            {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
        if (!contentFiles.isEmpty())
            entry.content = asset.directory + QLatin1Char('/') + contentFiles.first();

        m_entries.append(entry);
    };

    for (const assets::StoredAsset& asset : m_document->assets().list())
        addStored(asset);

    QSet<AssetHash> missingSeen;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.cbegin(); it != entities.cend(); ++it)
    {
        const model::Content& content = it.value().content;
        if (content.kind != ContentKind::Asset)
            continue;

        const AssetHash& hash = content.asset.hash;
        if (hash.isEmpty() || seen.contains(hash) || missingSeen.contains(hash))
            continue;

        if (const auto stored = m_document->assets().get(hash))
        {
            addStored(*stored);
            continue;
        }

        missingSeen.insert(hash);
        Entry entry;
        entry.hash = hash;
        entry.name = detail::shortHash(hash);
        entry.type = content.asset.type;
        entry.missing = true;
        m_entries.append(entry);
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.missing != b.missing)
            return !a.missing;
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return a.hash < b.hash;
    });
}

void AssetPanel::rebuildTagFilter()
{
    if (!m_tagFilter)
        return;

    const QString current = m_tagFilter->currentData().toString();
    QStringList tags;
    for (const Entry& entry : m_entries)
    {
        for (const QString& tag : entry.tags)
        {
            const bool exists = std::any_of(tags.cbegin(), tags.cend(),
                                            [&tag](const QString& seen) {
                                                return QString::compare(seen, tag,
                                                                        Qt::CaseInsensitive)
                                                       == 0;
                                            });
            if (!exists)
                tags.append(tag);
        }
    }
    std::sort(tags.begin(), tags.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    QSignalBlocker blocker(m_tagFilter);
    m_tagFilter->clear();
    m_tagFilter->addItem(tr("全部标签"), QString());
    for (const QString& tag : tags)
        m_tagFilter->addItem(tag, tag);

    const int index = current.isEmpty() ? 0 : m_tagFilter->findData(current);
    m_tagFilter->setCurrentIndex(index > 0 ? index : 0);
}

void AssetPanel::applyFilter()
{
    if (!m_list)
        return;

    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const QString tagFilter = m_tagFilter ? m_tagFilter->currentData().toString() : QString();
    const QString selected = selectedHash();

    QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const Entry& entry : m_entries)
    {
        if (!filter.isEmpty())
        {
            const bool matchesName = entry.name.contains(filter, Qt::CaseInsensitive);
            const bool matchesHash = entry.hash.contains(filter, Qt::CaseInsensitive);
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
        {
            continue;
        }
        m_list->addItem(makeItem(entry));
    }

    if (!selected.isEmpty())
    {
        for (int row = 0; row < m_list->count(); ++row)
        {
            if (m_list->item(row)->data(detail::kHashRole).toString() == selected)
            {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }

    updateSelectionState();
}

void AssetPanel::updateSelectionState()
{
    const QString hash = selectedHash();
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    const bool missing = item && item->data(detail::kMissingRole).toBool();
    const bool actionable = !hash.isEmpty() && !missing;
    if (m_renameButton)
        m_renameButton->setEnabled(actionable);
    if (m_tagButton)
        m_tagButton->setEnabled(actionable);
    if (m_deleteButton)
        m_deleteButton->setEnabled(actionable);
}

QString AssetPanel::selectedHash() const
{
    if (!m_list)
        return {};
    QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(detail::kHashRole).toString() : QString();
}

QListWidgetItem* AssetPanel::makeItem(const Entry& entry)
{
    auto* item = new QListWidgetItem;
    item->setIcon(QIcon(iconFor(entry)));
    item->setSizeHint(QSize(0, 62));

    const QString firstLine = entry.missing
                                  ? QStringLiteral("%1（素材缺失）").arg(entry.name)
                                  : entry.name;
    QString secondLine = typeLabel(entry.type) + QStringLiteral(" · ") + detail::shortHash(entry.hash);
    if (entry.hasAudio)
        secondLine += QStringLiteral(" · 含音频");
    if (!entry.tags.isEmpty())
    {
        QStringList decorated;
        decorated.reserve(entry.tags.size());
        for (const QString& tag : entry.tags)
            decorated.append(QLatin1Char('#') + tag);
        secondLine += QStringLiteral(" · ") + decorated.join(QStringLiteral(", "));
    }
    item->setText(firstLine + QLatin1Char('\n') + secondLine);

    item->setData(detail::kHashRole, entry.hash);
    item->setData(detail::kMissingRole, entry.missing);
    item->setData(detail::kNameRole, entry.name);
    item->setData(detail::kTagsRole, entry.tags);

    if (entry.missing)
        item->setForeground(QColor(QStringLiteral("#ff9a9a")));

    QString tooltip = QStringLiteral("名称：%1\n类型：%2\n哈希：%3")
                          .arg(entry.name, typeLabel(entry.type), entry.hash);
    tooltip += QStringLiteral("\n标签：%1").arg(
        entry.tags.isEmpty() ? tr("（无）") : entry.tags.join(QStringLiteral(", ")));
    if (!entry.directory.isEmpty())
        tooltip += QStringLiteral("\n路径：%1").arg(entry.directory);
    if (entry.missing)
        tooltip += QStringLiteral("\n\n素材缺失：实体仍引用该哈希。重新导入相同内容即可恢复。");
    else if (entry.hasAudio)
        tooltip += QStringLiteral("\n\n已拆出音频素材。");
    item->setToolTip(tooltip);
    return item;
}

QPixmap AssetPanel::iconFor(const Entry& entry)
{
    const QString key = (entry.missing ? QStringLiteral("m:") : QStringLiteral("a:"))
                        + entry.hash;
    if (const auto cached = m_iconCache.constFind(key); cached != m_iconCache.cend())
        return *cached;

    QPixmap pixmap(detail::thumbSize());
    pixmap.fill(QColor(QStringLiteral("#0f1217")));
    QPainter painter(&pixmap);

    if (entry.missing)
    {
        drawCheckerboard(painter, pixmap.rect());
        painter.setPen(QPen(QColor(QStringLiteral("#ff6b6b")), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
        QFont font = painter.font();
        font.setPixelSize(9);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("缺失"));
    }
    else
    {
        switch (entry.type)
        {
        case AssetType::Image:
        {
            QImage image;
            if (!entry.content.isEmpty())
            {
                QImageReader reader(entry.content);
                reader.setAutoTransform(true);
                image = reader.read();
            }
            if (!image.isNull())
            {
                const QImage scaled = image.scaled(detail::thumbSize(), Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                painter.drawImage(QPoint((detail::thumbSize().width() - scaled.width()) / 2,
                                         (detail::thumbSize().height() - scaled.height()) / 2),
                                  scaled);
            }
            else
            {
                drawCheckerboard(painter, pixmap.rect());
                painter.setPen(QPen(QColor(QStringLiteral("#ff6b6b")), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
                QFont font = painter.font();
                font.setPixelSize(9);
                font.setBold(true);
                painter.setFont(font);
                painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("坏图"));
            }
            break;
        }
        case AssetType::Video:
        {
            painter.setPen(QPen(QColor(QStringLiteral("#7fd1ff")), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(9, 9, 38, 22));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#7fd1ff")));
            QPolygon play;
            play << QPoint(24, 15) << QPoint(24, 25) << QPoint(34, 20);
            painter.drawPolygon(play);
            break;
        }
        case AssetType::Audio:
        {
            static const int heights[] = {6, 12, 20, 10, 24, 14, 8, 18, 11};
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#7fd1ff")));
            for (int i = 0; i < 9; ++i)
            {
                const int height = heights[i];
                const int x = 9 + i * 5;
                painter.fillRect(QRect(x, 20 - height / 2, 3, height),
                                 QColor(QStringLiteral("#7fd1ff")));
            }
            break;
        }
        case AssetType::Html:
        {
            QFont font = painter.font();
            font.setPixelSize(15);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(QStringLiteral("#ffcf6b")));
            painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("</>"));
            break;
        }
        }
        drawIconFrame(painter, pixmap);
    }

    m_iconCache.insert(key, pixmap);
    return pixmap;
}

void AssetPanel::setStatus(const QString& text)
{
    if (m_status)
    {
        m_status->setText(text);
        m_status->setToolTip(text);
    }
    emit statusMessage(text);
}

} // namespace pvf::next
