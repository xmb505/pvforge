#include "HistoryPanel.h"

#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

constexpr int kHashRole = Qt::UserRole;
constexpr int kMessageRole = Qt::UserRole + 1;
constexpr int kShortHashChars = 7;

QString displayTime(const QDateTime& time)
{
    if (!time.isValid())
        return QStringLiteral("—");
    return time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString displayHash(const QString& hash)
{
    return hash.left(kShortHashChars);
}

// Commit bodies can span lines; the table keeps one line and the full text
// stays available through the tooltip and restoreRequested().
QString displayMessage(const QString& message)
{
    const QString firstLine = message.section(QLatin1Char('\n'), 0, 0).trimmed();
    return firstLine.isEmpty() ? QStringLiteral("（无提交信息）") : firstLine;
}

} // namespace

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("historyPanel"));
    setWindowTitle(QStringLiteral("Git 历史"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(300);

    setStyleSheet(QStringLiteral(R"(
        QWidget#historyPanel { background: #151920; }
        QWidget#historyTitleBar {
            background: #1b212b;
            border-bottom: 1px solid #2b323e;
        }
        QWidget#historyStatusBar {
            background: #151920;
            border-top: 1px solid #262e3a;
        }
        QLabel#historyTitle {
            color: #e4e9f1;
            font-weight: 600;
        }
        QLabel#historyStatusLabel { color: #8b96a8; }
        QLabel#historyEmptyLabel { color: #626d7e; }
        QPushButton {
            background: #212834;
            border: 1px solid #333d4c;
            color: #e4e9f1;
            padding: 3px 9px;
        }
        QPushButton:hover { background: #2a3341; }
        QPushButton:pressed { background: #1a2029; }
        QPushButton:disabled {
            color: #626d7e;
            border-color: #262d38;
            background: #1a2029;
        }
        QPushButton#historyRestoreButton {
            background: #2f5f8f;
            border-color: #274d74;
        }
        QPushButton#historyRestoreButton:hover { background: #3a709f; }
        QPushButton#historyRestoreButton:pressed { background: #274d74; }
        QPushButton#historyRestoreButton:disabled {
            background: #1a2029;
            border-color: #262d38;
            color: #626d7e;
        }
        QTreeWidget#historyTree {
            background: #10141a;
            alternate-background-color: #141922;
            border: 0;
            color: #d8dfe9;
            outline: 0;
        }
        QTreeWidget#historyTree::item {
            padding: 2px 3px;
            border-bottom: 1px solid #1b212b;
        }
        QTreeWidget#historyTree::item:selected {
            background: #24405c;
            color: #ffffff;
        }
        QHeaderView::section {
            background: #1b212b;
            color: #8b96a8;
            border: 0;
            border-right: 1px solid #262e3a;
            border-bottom: 1px solid #262e3a;
            padding: 3px 4px;
        }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("historyTitleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 4, 6, 4);
    titleLayout->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("Git 历史"), titleBar);
    title->setObjectName(QStringLiteral("historyTitle"));

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), titleBar);
    m_refreshButton->setObjectName(QStringLiteral("historyRefreshButton"));
    m_refreshButton->setToolTip(QStringLiteral("重新读取提交历史"));
    m_refreshButton->setFocusPolicy(Qt::NoFocus);

    m_restoreButton = new QPushButton(QStringLiteral("恢复到此提交"), titleBar);
    m_restoreButton->setObjectName(QStringLiteral("historyRestoreButton"));
    m_restoreButton->setToolTip(QStringLiteral("把工程回退到选中的提交"));
    m_restoreButton->setFocusPolicy(Qt::NoFocus);

    titleLayout->addWidget(title);
    titleLayout->addStretch(1);
    titleLayout->addWidget(m_refreshButton);
    titleLayout->addWidget(m_restoreButton);
    root->addWidget(titleBar);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("historyTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("时间"), QStringLiteral("Hash"),
                             QStringLiteral("消息")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto* emptyPage = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(8, 8, 8, 8);
    m_emptyLabel = new QLabel(QStringLiteral("还没有提交"), emptyPage);
    m_emptyLabel->setObjectName(QStringLiteral("historyEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyLabel);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("historyStack"));
    m_stack->addWidget(m_tree);
    m_stack->addWidget(emptyPage);
    root->addWidget(m_stack, 1);

    m_statusBar = new QWidget(this);
    m_statusBar->setObjectName(QStringLiteral("historyStatusBar"));
    auto* statusLayout = new QHBoxLayout(m_statusBar);
    statusLayout->setContentsMargins(8, 3, 6, 3);
    statusLayout->setSpacing(6);
    m_statusLabel = new QLabel(m_statusBar);
    m_statusLabel->setObjectName(QStringLiteral("historyStatusLabel"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(m_statusLabel, 1);
    root->addWidget(m_statusBar);
    m_statusBar->hide();

    connect(m_refreshButton, &QPushButton::clicked, this,
            &HistoryPanel::refreshRequested);
    connect(m_restoreButton, &QPushButton::clicked, this,
            &HistoryPanel::emitRestore);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &HistoryPanel::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (m_busy || !item)
                    return;
                const QString hash = item->data(0, kHashRole).toString();
                if (hash.isEmpty())
                    return;
                emit restoreRequested(
                    hash, item->data(0, kMessageRole).toString());
            });

    updateButtons();
    updateStatus();
    updateEmptyState();
}

QSize HistoryPanel::sizeHint() const
{
    return QSize(460, 320);
}

void HistoryPanel::setCommits(const QVector<store::GitCommit>& commits)
{
    const QString previous = currentHash();

    QVector<store::GitCommit> sorted = commits;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const store::GitCommit& a, const store::GitCommit& b) {
                         return a.time > b.time;
                     });
    if (sorted.size() > kMaxCommits)
        sorted.resize(kMaxCommits);

    const QFont hashFont =
        QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QTreeWidgetItem* selected = nullptr;

    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        for (const store::GitCommit& commit : sorted)
        {
            auto* item = new QTreeWidgetItem(m_tree);
            item->setText(0, displayTime(commit.time));
            item->setText(1, displayHash(commit.hash));
            item->setText(2, displayMessage(commit.message));
            item->setData(0, kHashRole, commit.hash);
            item->setData(0, kMessageRole, commit.message);
            item->setFont(1, hashFont);
            item->setToolTip(0, commit.time.isValid()
                                    ? commit.time.toLocalTime().toString(
                                          QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                    : QString());
            item->setToolTip(1, commit.hash);
            item->setToolTip(2, commit.message);
            if (!previous.isEmpty() && commit.hash == previous)
                selected = item;
        }

        if (selected)
        {
            m_tree->setCurrentItem(selected);
            selected->setSelected(true);
        }
        else
        {
            m_tree->setCurrentItem(nullptr);
            m_tree->clearSelection();
        }
    }

    updateButtons();
    updateEmptyState();
}

void HistoryPanel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    updateButtons();
    updateStatus();
}

void HistoryPanel::setStatus(const QString& text)
{
    m_statusText = text;
    updateStatus();
}

QString HistoryPanel::selectedHash() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || !item->isSelected())
        return {};
    return item->data(0, kHashRole).toString();
}

QString HistoryPanel::selectedMessage() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || !item->isSelected())
        return {};
    return item->data(0, kMessageRole).toString();
}

#ifdef PVFORGE_TEST_HOOKS
int HistoryPanel::rowCountForTest() const
{
    return m_tree->topLevelItemCount();
}

void HistoryPanel::selectRowForTest(int row)
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    if (!item)
        return;
    m_tree->setCurrentItem(item);
    item->setSelected(true);
    m_tree->scrollToItem(item);
}
#endif

void HistoryPanel::updateButtons()
{
    const bool hasSelection = !selectedHash().isEmpty();
    m_restoreButton->setEnabled(!m_busy && hasSelection);
    m_refreshButton->setEnabled(!m_busy);
}

void HistoryPanel::updateStatus()
{
    QString text = m_statusText;
    if (text.isEmpty() && m_busy)
        text = QStringLiteral("处理中…");
    m_statusLabel->setText(text);
    m_statusBar->setVisible(!text.isEmpty());
}

void HistoryPanel::updateEmptyState()
{
    m_stack->setCurrentIndex(m_tree->topLevelItemCount() == 0 ? 1 : 0);
}

void HistoryPanel::emitRestore()
{
    if (m_busy)
        return;
    const QString hash = selectedHash();
    if (hash.isEmpty())
        return;
    emit restoreRequested(hash, selectedMessage());
}

QString HistoryPanel::currentHash() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, kHashRole).toString() : QString();
}

} // namespace pvf::next
