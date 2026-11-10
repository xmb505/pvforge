#include "next/assets/AssetPanel.h"

#include "assets/AssetTypeDetect.h"
#include "video2/VideoExtractionQueue.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace pvf::next {

namespace {

constexpr int kHashRole = Qt::UserRole;
constexpr int kMissingRole = Qt::UserRole + 1;
constexpr int kNameRole = Qt::UserRole + 2;
constexpr int kTagsRole = Qt::UserRole + 3;
constexpr char kAssetMimeType[] = "application/x-pvforge-asset";

const QSize kThumbSize(56, 40);

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

QString shortHash(const QString& hash)
{
    return hash.left(8);
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
        if (!item || item->data(kMissingRole).toBool())
            return;
        const QString hash = item->data(kHashRole).toString();
        if (hash.isEmpty() || !mimeFactory)
            return;

        std::unique_ptr<QMimeData> data(mimeFactory(hash));
        if (!data)
            return;

        QDrag drag(this);
        drag.setMimeData(data.release());
        const QIcon icon = item->icon();
        if (!icon.isNull())
            drag.setPixmap(icon.pixmap(kThumbSize));
        drag.exec(Qt::CopyAction);
    }
};

} // namespace

// One row in the bottom "video extraction" strip. Deliberately plain widgets:
// the panel owns queue lifetime and row removal, the row only renders state.
class AssetPanel::ProgressRow final : public QFrame
{
public:
    ProgressRow(const AssetHash& hash, const QString& name, QWidget* parent)
        : QFrame(parent)
        , m_hash(hash)
    {
        setObjectName(QStringLiteral("VideoExtractionRow"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 5, 6, 6);
        layout->setSpacing(3);

        auto* top = new QHBoxLayout;
        top->setSpacing(6);

        m_name = new QLabel(name, this);
        m_name->setObjectName(QStringLiteral("VideoExtractionName"));
        m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_name->setToolTip(name);
        top->addWidget(m_name, 1);

        m_text = new QLabel(QStringLiteral("抽帧 0/0"), this);
        m_text->setObjectName(QStringLiteral("VideoExtractionText"));
        QFont small = m_text->font();
        small.setPixelSize(11);
        m_text->setFont(small);
        top->addWidget(m_text);

        m_cancel = new QPushButton(QStringLiteral("取消"), this);
        m_cancel->setObjectName(QStringLiteral("VideoExtractionCancel"));
        m_cancel->setCursor(Qt::PointingHandCursor);
        m_cancel->setFixedHeight(18);
        top->addWidget(m_cancel);

        layout->addLayout(top);

        m_bar = new QProgressBar(this);
        m_bar->setObjectName(QStringLiteral("VideoExtractionBar"));
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(5);
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        layout->addWidget(m_bar);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("VideoExtractionError"));
        m_error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_error->hide();
        layout->addWidget(m_error);
    }

    AssetHash hash() const { return m_hash; }
    QPushButton* cancelButton() const { return m_cancel; }
    bool failed() const { return m_failed; }
    bool finished() const { return m_finished; }

    QString statusText() const
    {
        return m_failed ? m_error->text() : m_text->text();
    }

    void setProgress(int done, int total)
    {
        m_total = total;
        m_finished = false;
        if (total > 0)
        {
            m_text->setText(QStringLiteral("抽帧 %1/%2").arg(done).arg(total));
            m_bar->setRange(0, total);
            m_bar->setValue(done);
        }
        else
        {
            m_text->setText(QStringLiteral("抽帧 %1").arg(done));
            m_bar->setRange(0, 0);
        }
        m_text->setToolTip(m_text->text());
    }

    void setFinished()
    {
        if (m_total > 0)
            setProgress(m_total, m_total);
        m_finished = true;
        m_text->setText(QStringLiteral("抽帧完成"));
        m_bar->setRange(0, 1);
        m_bar->setValue(1);
        m_cancel->hide();
    }

    void setFailed(const QString& error)
    {
        m_failed = true;
        m_bar->hide();
        m_text->hide();
        m_error->setText(QStringLiteral("抽帧失败：%1").arg(error));
        m_error->setToolTip(error);
        m_error->show();
        m_cancel->setText(QStringLiteral("关闭"));
        m_cancel->setEnabled(true);
    }

private:
    AssetHash m_hash;
    QLabel* m_name = nullptr;
    QLabel* m_text = nullptr;
    QLabel* m_error = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_cancel = nullptr;
    int m_total = 0;
    bool m_failed = false;
    bool m_finished = false;
};

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
    m_list->setIconSize(kThumbSize);
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
    return m_list->item(index)->data(kNameRole).toString();
}

QString AssetPanel::assetHashAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(kHashRole).toString();
}

QStringList AssetPanel::assetTagsAt(int index) const
{
    if (!m_list || index < 0 || index >= m_list->count())
        return {};
    return m_list->item(index)->data(kTagsRole).toStringList();
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
    return m_list->item(index)->data(kMissingRole).toBool();
}

int AssetPanel::progressRowCount() const
{
    return m_progressRows.size();
}

QString AssetPanel::progressTextAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return {};
    return m_progressRows.at(index)->statusText();
}

QPushButton* AssetPanel::progressCancelButtonAt(int index) const
{
    if (index < 0 || index >= m_progressRows.size())
        return nullptr;
    return m_progressRows.at(index)->cancelButton();
}

int AssetPanel::progressRowIndex(const AssetHash& hash) const
{
    for (int i = 0; i < m_progressRows.size(); ++i)
    {
        if (m_progressRows.at(i)->hash() == hash)
            return i;
    }
    return -1;
}

QString AssetPanel::displayNameFor(const AssetHash& hash) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.hash == hash)
            return entry.name;
    }
    if (m_document)
    {
        if (const auto asset = m_document->assets().get(hash))
            return asset->name.isEmpty() ? hash.left(8) : asset->name;
    }
    return hash.left(8);
}

void AssetPanel::ensureProgressRow(const AssetHash& hash)
{
    if (!m_progressArea || progressRowIndex(hash) >= 0)
        return;

    auto* row = new ProgressRow(hash, displayNameFor(hash), m_progressArea);
    m_progressLayout->addWidget(row);
    m_progressRows.append(row);
    connect(row->cancelButton(), &QPushButton::clicked, this, [this, hash]() {
        const int index = progressRowIndex(hash);
        if (index < 0)
            return;
        ProgressRow* current = m_progressRows.at(index);
        if (current->failed())
        {
            removeProgressRow(hash);
            return;
        }
        current->cancelButton()->setEnabled(false);
        if (m_extractionQueue)
            m_extractionQueue->cancel(hash);
    });
    m_progressArea->show();
}

void AssetPanel::updateProgressRow(const AssetHash& hash, int done, int total)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    m_progressRows.at(index)->setProgress(done, total);
}

void AssetPanel::finishProgressRow(const AssetHash& hash)
{
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;

    ProgressRow* row = m_progressRows.at(index);
    row->setFinished();

    // Keep the completed row for a moment so the user sees 100%, then drop it.
    QPointer<ProgressRow> guard(row);
    QTimer::singleShot(2000, this, [this, hash, guard]() {
        if (!guard)
            return;
        const int current = progressRowIndex(hash);
        if (current >= 0 && m_progressRows.at(current) == guard.data()
            && guard->finished())
            removeProgressRow(hash);
    });
}

void AssetPanel::failProgressRow(const AssetHash& hash, const QString& error)
{
    ensureProgressRow(hash);
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;
    m_progressRows.at(index)->setFailed(error);
}

void AssetPanel::removeProgressRow(const AssetHash& hash)
{
    const int index = progressRowIndex(hash);
    if (index < 0)
        return;

    ProgressRow* row = m_progressRows.takeAt(index);
    if (m_progressLayout)
        m_progressLayout->removeWidget(row);
    row->hide();
    row->deleteLater();

    if (m_progressRows.isEmpty() && m_progressArea)
        m_progressArea->hide();
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
        entry.name = asset.name.isEmpty() ? shortHash(asset.hash) : asset.name;
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
        entry.name = shortHash(hash);
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
            if (m_list->item(row)->data(kHashRole).toString() == selected)
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
    const bool missing = item && item->data(kMissingRole).toBool();
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
    return item ? item->data(kHashRole).toString() : QString();
}

QListWidgetItem* AssetPanel::makeItem(const Entry& entry)
{
    auto* item = new QListWidgetItem;
    item->setIcon(QIcon(iconFor(entry)));
    item->setSizeHint(QSize(0, 62));

    const QString firstLine = entry.missing
                                  ? QStringLiteral("%1（素材缺失）").arg(entry.name)
                                  : entry.name;
    QString secondLine = typeLabel(entry.type) + QStringLiteral(" · ") + shortHash(entry.hash);
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

    item->setData(kHashRole, entry.hash);
    item->setData(kMissingRole, entry.missing);
    item->setData(kNameRole, entry.name);
    item->setData(kTagsRole, entry.tags);

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

    QPixmap pixmap(kThumbSize);
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
                const QImage scaled = image.scaled(kThumbSize, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                painter.drawImage(QPoint((kThumbSize.width() - scaled.width()) / 2,
                                         (kThumbSize.height() - scaled.height()) / 2),
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

void AssetPanel::setVideoExtractionOptions(int maxWidth, qint64 diskBudgetBytes)
{
    if (maxWidth > 0)
        m_videoExtractionWidth = maxWidth;
    if (diskBudgetBytes > 0)
        m_videoExtractionDiskBudget = diskBudgetBytes;
}

int AssetPanel::videoExtractionWidth() const
{
    return m_videoExtractionWidth;
}

qint64 AssetPanel::videoExtractionDiskBudget() const
{
    return m_videoExtractionDiskBudget;
}

bool AssetPanel::buildExtractionJob(const assets::StoredAsset& asset,
                                    video2::ExtractionJob* job) const
{
    if (!m_document || !job || asset.type != AssetType::Video)
        return false;

    const int fps = m_document->project().settings.fps;
    if (fps <= 0)
        return false;

    const QStringList contentFiles = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (contentFiles.isEmpty())
        return false;

    const QString projectRoot =
        QFileInfo(m_document->assets().assetsDirectory()).absolutePath();

    job->assetHash = asset.hash;
    job->videoPath = asset.directory + QLatin1Char('/') + contentFiles.first();
    job->projectFps = fps;
    job->maxWidth = m_videoExtractionWidth;
    job->diskBudget = m_videoExtractionDiskBudget;
    job->cacheRoot = QDir(projectRoot).filePath(QStringLiteral(".cache/video"));
    return true;
}

void AssetPanel::enqueueVideoExtraction(const assets::StoredAsset& asset)
{
    if (!m_extractionQueue)
        return;

    video2::ExtractionJob job;
    if (!buildExtractionJob(asset, &job))
        return;
    m_extractionQueue->enqueue(job);
}

void AssetPanel::enqueueMissingVideoExtractions()
{
    if (!m_document || !m_extractionQueue)
        return;

    int queued = 0;
    for (const assets::StoredAsset& asset : m_document->assets().list())
    {
        if (asset.type != AssetType::Video)
            continue;
        if (m_extractionQueue->isRunning(asset.hash)
            || m_extractionQueue->isQueued(asset.hash))
            continue;

        video2::ExtractionJob job;
        if (!buildExtractionJob(asset, &job))
            continue;
        m_extractionQueue->enqueue(job);
        // enqueue() silently skips jobs with a complete cache index.
        if (m_extractionQueue->isQueued(asset.hash)
            || m_extractionQueue->isRunning(asset.hash))
            ++queued;
    }

    if (queued > 0)
        setStatus(tr("已排队补抽 %1 个视频的帧").arg(queued));
}

bool AssetPanel::importPaths(const QStringList& paths, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    QStringList imported;
    QStringList failures;
    QStringList warnings;
    bool audioSplit = false;

    for (const QString& rawPath : paths)
    {
        const QFileInfo info(rawPath);
        if (!info.exists())
        {
            failures << tr("%1：文件不存在").arg(rawPath);
            continue;
        }

        const QString path = info.absoluteFilePath();
        const auto type = assets::detectType(path);
        QString importError;
        std::optional<assets::StoredAsset> stored;

        const bool isBundle = info.isDir()
                              || info.suffix().compare(QStringLiteral("zip"),
                                                       Qt::CaseInsensitive) == 0
                              || (type && *type == AssetType::Html);
        if (isBundle)
        {
            stored = m_document->assets().importPvhtml(path, &importError);
        }
        else if (!type)
        {
            failures << tr("%1：不支持的素材类型").arg(info.fileName());
            continue;
        }
        else if (*type == AssetType::Video)
        {
            const auto video = m_document->assets().importVideoWithAudio(path, &importError);
            if (video)
            {
                stored = video->video;
                if (video->audio)
                    audioSplit = true;
                if (!video->audioError.isEmpty())
                    warnings << tr("%1：音频拆分失败（%2）")
                                    .arg(info.fileName(), video->audioError);
            }
        }
        else
        {
            stored = m_document->assets().importFile(path, &importError);
        }

        if (!stored)
        {
            failures << tr("%1：%2").arg(info.fileName(),
                                        importError.isEmpty() ? tr("导入失败")
                                                              : importError);
            continue;
        }
        imported.append(stored->name);
        if (stored->type == AssetType::Video)
            enqueueVideoExtraction(*stored);
    }

    if (!imported.isEmpty())
    {
        refresh();

        QString message;
        if (imported.size() == 1)
            message = QStringLiteral("pvf: import asset \"%1\"").arg(imported.first());
        else
            message = QStringLiteral("pvf: import assets \"%1\"")
                          .arg(imported.join(QStringLiteral("\", \"")));

        QString commitError;
        if (!m_document->commitProject(message, &commitError))
        {
            failures << tr("提交失败：%1").arg(commitError);
        }
        else
        {
            QString status = imported.size() == 1
                                 ? tr("已导入素材 \"%1\"").arg(imported.first())
                                 : tr("已导入 %1 个素材").arg(imported.size());
            if (audioSplit)
                status += tr("，已拆出音频素材");
            if (!warnings.isEmpty())
                status += tr("；%1").arg(warnings.join(QStringLiteral("；")));
            setStatus(status);
        }
        emit assetsChanged();
    }

    if (!failures.isEmpty())
    {
        const QString text = failures.join(QLatin1Char('\n'));
        if (error)
            *error = text;
        setStatus(text);
        return false;
    }
    return true;
}

bool AssetPanel::renameAsset(const AssetHash& hash, const QString& newName,
                             QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    const QString name = newName.trimmed();
    if (name.isEmpty())
    {
        if (error)
            *error = tr("素材名称不能为空");
        return false;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        if (error)
            *error = tr("素材不存在或内容缺失：%1").arg(hash);
        return false;
    }
    if (asset->name == name)
        return true;

    QJsonObject metadata = asset->metadata;
    metadata.insert(QStringLiteral("name"), name);

    const QString metadataPath = asset->directory + QStringLiteral("/asset.json");
    QSaveFile file(metadataPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = tr("无法写入 %1：%2").arg(metadataPath, file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        if (error)
            *error = tr("无法写入 %1").arg(metadataPath);
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: rename asset \"%1\" -> \"%2\"").arg(asset->name, name),
        &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已重命名素材为 \"%1\"").arg(name));

    if (!committed)
    {
        if (error)
            *error = tr("重命名已写入，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

bool AssetPanel::setAssetTags(const AssetHash& hash, const QStringList& tags,
                              QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        if (error)
            *error = tr("素材不存在或内容缺失：%1").arg(hash);
        return false;
    }

    QString tagsError;
    if (!m_document->assets().setTags(hash, tags, &tagsError))
    {
        if (error)
            *error = tagsError.isEmpty() ? tr("无法写入素材标签：%1").arg(hash)
                                         : tagsError;
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: edit asset tags \"%1\"").arg(asset->name), &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已更新素材 \"%1\" 的标签").arg(asset->name));

    if (!committed)
    {
        if (error)
            *error = tr("标签已写入，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

bool AssetPanel::deleteAsset(const AssetHash& hash, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("没有打开的工程");
        return false;
    }

    QString name = shortHash(hash);
    if (const auto asset = m_document->assets().get(hash))
        name = asset->name;

    QString removeError;
    if (!m_document->assets().remove(hash, &removeError))
    {
        if (error)
            *error = removeError.isEmpty() ? tr("无法删除素材：%1").arg(hash)
                                           : removeError;
        return false;
    }

    QString commitError;
    const bool committed = m_document->commitProject(
        QStringLiteral("pvf: delete asset \"%1\"").arg(name), &commitError);

    refresh();
    emit assetsChanged();
    setStatus(tr("已删除素材 \"%1\"").arg(name));

    if (!committed)
    {
        if (error)
            *error = tr("素材已从磁盘删除，但提交失败：%1").arg(commitError);
        return false;
    }
    return true;
}

QMimeData* AssetPanel::assetMimeData(const AssetHash& hash) const
{
    if (!m_document || hash.isEmpty())
        return nullptr;
    if (!m_document->assets().get(hash))
        return nullptr;

    auto* data = new QMimeData;
    data->setData(QString::fromLatin1(kAssetMimeType), hash.toUtf8());
    data->setText(hash);
    return data;
}

int AssetPanel::referenceCount(const AssetHash& hash) const
{
    if (!m_document)
        return 0;
    int count = 0;
    const auto& entities = m_document->project().entities;
    for (auto it = entities.cbegin(); it != entities.cend(); ++it)
    {
        const model::Content& content = it.value().content;
        if (content.kind == ContentKind::Asset && content.asset.hash == hash)
            ++count;
    }
    return count;
}

void AssetPanel::chooseImportFiles()
{
    const QString filter = tr(
        "支持的素材 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg "
        "*.avif *.heic *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg *.wav "
        "*.mp3 *.ogg *.flac *.aac *.m4a *.opus *.zip *.pvhtml);;"
        "图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.avif *.heic);;"
        "视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mpeg *.mpg);;"
        "音频 (*.wav *.mp3 *.ogg *.flac *.aac *.m4a *.opus);;"
        "PVForge HTML (*.zip *.pvhtml);;"
        "所有文件 (*)");

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("导入素材"), m_lastImportDir, filter);
    if (paths.isEmpty())
        return;

    m_lastImportDir = QFileInfo(paths.first()).absolutePath();
    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetPanel::chooseImportPvhtml()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 .pvhtml 目录"), m_lastImportDir, QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;

    m_lastImportDir = QFileInfo(directory).absolutePath();
    QString error;
    if (!importPaths({directory}, &error) && !error.isEmpty())
        setStatus(error);
}

void AssetPanel::renameSelected()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(kMissingRole).toBool())
        return;

    const QString hash = item->data(kHashRole).toString();
    const QString currentName = item->data(kNameRole).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名素材"), tr("素材名称："), QLineEdit::Normal, currentName, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == currentName)
        return;

    QString error;
    if (!renameAsset(hash, name, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("重命名失败"), error);
}

void AssetPanel::editSelectedTags()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(kMissingRole).toBool())
        return;

    const QString hash = item->data(kHashRole).toString();
    QString initial;
    if (m_document)
    {
        if (const auto asset = m_document->assets().get(hash))
            initial = asset->tags.join(QStringLiteral(", "));
    }

    bool ok = false;
    QString text = QInputDialog::getText(this, tr("编辑标签"),
                                         tr("标签（用逗号分隔，最多 32 个）："),
                                         QLineEdit::Normal, initial, &ok);
    if (!ok)
        return;

    text.replace(QChar(0xFF0C), QLatin1Char(',')); // full-width comma
    QStringList tags;
    for (const QString& part : text.split(QLatin1Char(',')))
    {
        const QString tag = part.trimmed();
        if (!tag.isEmpty())
            tags.append(tag);
    }

    QString error;
    if (!setAssetTags(hash, tags, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("编辑标签失败"), error);
}

void AssetPanel::deleteSelected()
{
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    if (!item || item->data(kMissingRole).toBool())
        return;

    const QString hash = item->data(kHashRole).toString();
    const QString name = item->data(kNameRole).toString();
    const int refs = referenceCount(hash);

    QString text = tr("确定删除素材 \"%1\"？").arg(name);
    if (refs > 0)
    {
        text += tr("\n\n%1 个实体正在引用它。删除后这些实体会显示“素材缺失”，"
                   "重新导入相同内容即可恢复。")
                    .arg(refs);
    }
    else
    {
        text += tr("\n\n删除后可通过 git 历史恢复。");
    }

    const auto answer = QMessageBox::question(
        this, tr("删除素材"), text, QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!deleteAsset(hash, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("删除素材"), error);
}

void AssetPanel::showContextMenu(const QPoint& pos)
{
    if (!m_list)
        return;
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item)
        return;

    m_list->setCurrentItem(item);
    const bool missing = item->data(kMissingRole).toBool();

    QMenu menu(this);
    QAction* renameAction = menu.addAction(tr("重命名…"));
    QAction* tagsAction = menu.addAction(tr("编辑标签…"));
    QAction* deleteAction = menu.addAction(tr("删除…"));
    renameAction->setEnabled(!missing);
    tagsAction->setEnabled(!missing);
    deleteAction->setEnabled(!missing);

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAction)
        renameSelected();
    else if (chosen == tagsAction)
        editSelectedTags();
    else if (chosen == deleteAction)
        deleteSelected();
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

void AssetPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
        {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void AssetPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void AssetPanel::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (paths.isEmpty())
    {
        event->ignore();
        return;
    }

    QString error;
    if (!importPaths(paths, &error) && !error.isEmpty())
        setStatus(error);
    event->acceptProposedAction();
}

} // namespace pvf::next
