#include "AssetLibraryPanel.h"

#include "AssetLibraryPanelInternal.h"

#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "core/Lang.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "video2/VideoExtractionQueue.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace pvf {

AssetLibraryPanel::AssetLibraryPanel(EditorSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_sessionMode(true)
{
    buildUi();
    buildSessionUi();
    if (m_session && m_session->document())
    {
        connect(m_session->document(), &next::Document::changed, this,
                &AssetLibraryPanel::refresh);
    }
    refresh();
    enqueueMissingVideoExtractions();
}

AssetLibraryPanel::~AssetLibraryPanel()
{
    if (m_extractionQueue)
        m_extractionQueue->cancelAll();
}

void AssetLibraryPanel::setCompactMode(bool compact)
{
    m_compact = compact;
    if (!m_layout)
        return;

    m_layout->setContentsMargins(compact ? 4 : 10, compact ? 4 : 10,
                                 compact ? 4 : 10, compact ? 4 : 10);
    m_layout->setSpacing(compact ? 4 : 8);
    if (m_header)
        m_header->setVisible(!compact);
    // The preview was already hidden; the info line is the long path/hash
    // text that would eat the timeline height.
    if (m_info)
        m_info->setVisible(!compact);
    if (m_list)
    {
        m_list->setMinimumHeight(compact ? 48 : 140);
        m_list->setIconSize(compact ? QSize(30, 30) : QSize(40, 40));
    }
    setMinimumWidth(compact ? 0 : minimumWidth());
}

void AssetLibraryPanel::buildUi()
{
    setObjectName(QStringLiteral("assetPanel"));
    m_layout = new QVBoxLayout(this);
    auto* layout = m_layout;
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // Folder-like header: current location plus list/icon switch. Compact
    // mode hides this whole row to keep the timeline asset bin shallow.
    m_header = new QWidget(this);
    auto* pathRow = new QHBoxLayout(m_header);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(6);
    auto* folderIcon = new QLabel(m_header);
    folderIcon->setPixmap(style()->standardIcon(QStyle::SP_DirIcon).pixmap(16, 16));
    pathRow->addWidget(folderIcon);
    auto* pathLabel = new QLabel(QStringLiteral("全部素材"), m_header);
    QFont pathFont = pathLabel->font();
    pathFont.setBold(true);
    pathLabel->setFont(pathFont);
    pathRow->addWidget(pathLabel);
    pathRow->addStretch(1);
    m_viewButton = new QToolButton(m_header);
    m_viewButton->setObjectName(QStringLiteral("assetViewToggle"));
    m_viewButton->setCheckable(true);
    m_viewButton->setText(QStringLiteral("▦"));
    m_viewButton->setToolTip(QStringLiteral("切换到图标视图"));
    m_viewButton->setFixedSize(26, 26);
    pathRow->addWidget(m_viewButton);
    layout->addWidget(m_header);
    connect(m_viewButton, &QToolButton::toggled, this,
            [this](bool iconView) { setIconView(iconView); });

    m_preview = new QLabel(QStringLiteral("素材库为空"), this);
    m_preview->setObjectName(QStringLiteral("assetPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(150);
    m_preview->setMinimumWidth(0);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_preview->setFrameShape(QFrame::StyledPanel);
    // The large preview is replaced by the hover popup; keeping it hidden
    // leaves more room for the folder list.
    m_preview->hide();

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("assetSearch"));
    m_search->setMinimumWidth(0);
    m_search->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_search->setPlaceholderText(
        Lang::t(QStringLiteral("search.assets"), QStringLiteral("搜索素材…")));
    m_search->setClearButtonEnabled(true);

    auto* searchRow = new QHBoxLayout();
    searchRow->addWidget(m_search, 1);
    auto* importButton = new QToolButton(this);
    importButton->setObjectName(QStringLiteral("assetImport"));
    importButton->setText(QStringLiteral("+"));
    importButton->setToolTip(QStringLiteral("导入素材文件（图片 / 视频 / 音频 / zip）"));
    importButton->setFixedSize(26, 26);
    searchRow->addWidget(importButton);
    auto* htmlImportButton = new QToolButton(this);
    htmlImportButton->setObjectName(QStringLiteral("assetImportHtml"));
    htmlImportButton->setText(QStringLiteral("H"));
    htmlImportButton->setToolTip(QStringLiteral("导入 .pvhtml 目录（zip 用 + 导入）"));
    htmlImportButton->setFixedSize(26, 26);
    searchRow->addWidget(htmlImportButton);
    layout->addLayout(searchRow);

    if (m_sessionMode)
    {
        auto* tagRow = new QHBoxLayout();
        tagRow->setSpacing(6);
        auto* tagLabel = new QLabel(QStringLiteral("标签"), this);
        tagLabel->setObjectName(QStringLiteral("assetTagLabel"));
        tagRow->addWidget(tagLabel);
        m_tagFilter = new QComboBox(this);
        m_tagFilter->setObjectName(QStringLiteral("assetTagFilter"));
        m_tagFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_tagFilter->addItem(QStringLiteral("全部标签"), QString());
        tagRow->addWidget(m_tagFilter, 1);
        layout->addLayout(tagRow);
        connect(m_tagFilter, &QComboBox::currentIndexChanged, this,
                [this](int) { applySessionFilter(); });
        connect(importButton, &QToolButton::clicked, this,
                &AssetLibraryPanel::chooseSessionImportFiles);
        connect(htmlImportButton, &QToolButton::clicked, this,
                &AssetLibraryPanel::chooseSessionImportHtml);
    }

    auto* list = new assetlibdetail::AssetListWidget(this);
    m_list = list;
    list->sessionMimeFactory = [this](const QString& hash) {
        return assetMimeData(hash);
    };
    m_list->setObjectName(QStringLiteral("assetList"));
    m_list->setIconSize(QSize(40, 40));
    m_list->setAlternatingRowColors(true);
    m_list->setMinimumHeight(140);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setWordWrap(false);
    m_list->setMouseTracking(true);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    m_list->viewport()->installEventFilter(this);
    connect(m_list, &QListWidget::itemEntered, this,
            &AssetLibraryPanel::showHoverPreview);
    if (m_sessionMode)
    {
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_list, &QListWidget::customContextMenuRequested, this,
                &AssetLibraryPanel::showSessionContextMenu);
    }
    layout->addWidget(m_list, 1);

    if (m_sessionMode)
    {
        m_progressArea = new QWidget(this);
        m_progressArea->setObjectName(QStringLiteral("assetExtractionArea"));
        m_progressLayout = new QVBoxLayout(m_progressArea);
        m_progressLayout->setContentsMargins(0, 0, 0, 0);
        m_progressLayout->setSpacing(3);
        m_progressArea->hide();
        layout->addWidget(m_progressArea);
    }

    // Legacy visible workflow: 创建实体 / 删除. Dragging an asset to the
    // timeline remains the second entry point.
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_create = new QPushButton(QStringLiteral("创建实体"), this);
    m_create->setObjectName(QStringLiteral("assetCreate"));
    m_create->setEnabled(false);
    buttons->addWidget(m_create, 1);
    m_remove = new QPushButton(QStringLiteral("删除"), this);
    m_remove->setObjectName(QStringLiteral("assetRemove"));
    if (m_sessionMode)
    {
        m_remove->setToolTip(
            QStringLiteral("只删除素材库条目，引用它的实体显示“素材缺失”"));
        connect(m_create, &QPushButton::clicked, this,
                &AssetLibraryPanel::createSelectedSession);
        connect(m_remove, &QPushButton::clicked, this,
                &AssetLibraryPanel::removeSelectedSession);
    }
    m_remove->setEnabled(false);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("assetInfo"));
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setMinimumWidth(0);
    m_info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_info);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
        if (m_sessionMode)
            applySessionFilter();
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
        if (m_sessionMode)
            updateSessionSelection();
    });
}

void AssetLibraryPanel::buildSessionUi()
{
    m_extractionQueue = new video2::VideoExtractionQueue(this);
    connect(m_extractionQueue, &video2::VideoExtractionQueue::started, this,
            [this](const QString& hash, int total) {
                ensureProgressRow(hash);
                updateProgressRow(hash, 0, total);
            });
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
}

void AssetLibraryPanel::refresh()
{
    if (m_sessionMode)
    {
        rebuildSessionEntries();
        return;
    }
}

void AssetLibraryPanel::setStatus(const QString& text)
{
    emit statusMessage(text);
}

} // namespace pvf
