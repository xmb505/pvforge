#include "HistoryPanel.h"

#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

// Status/error strings can be arbitrarily long.  A normal QLabel reports the
// full text width as its minimum size hint, which makes a QDockWidget refuse
// to shrink.  This label paints one elided line and keeps a zero-width
// minimum hint; the full text stays in the tooltip.
class ElidingLabel : public QLabel
{
public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override
    {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

    QSize sizeHint() const override
    {
        return QSize(0, QLabel::sizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        painter.setPen(palette().color(QPalette::WindowText));
        const QRect area = contentsRect();
        painter.drawText(area, alignment() | Qt::TextSingleLine,
                         fontMetrics().elidedText(text(), Qt::ElideRight,
                                                  area.width()));
    }
};

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
    setMinimumSize(180, 120);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("historyTitleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 4, 6, 4);
    titleLayout->setSpacing(6);

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), titleBar);
    m_refreshButton->setObjectName(QStringLiteral("historyRefreshButton"));
    m_refreshButton->setToolTip(QStringLiteral("重新读取提交历史"));
    m_refreshButton->setFocusPolicy(Qt::NoFocus);

    m_restoreButton = new QPushButton(QStringLiteral("恢复到此提交"), titleBar);
    m_restoreButton->setObjectName(QStringLiteral("historyRestoreButton"));
    m_restoreButton->setToolTip(QStringLiteral("把工程回退到选中的提交"));
    m_restoreButton->setFocusPolicy(Qt::NoFocus);

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
    m_tree->setMinimumSize(0, 0);
    m_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->resizeSection(0, 120);
    m_tree->header()->resizeSection(1, 68);

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
    m_stack->setMinimumSize(0, 0);
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack->addWidget(m_tree);
    m_stack->addWidget(emptyPage);
    root->addWidget(m_stack, 1);

    m_statusBar = new QWidget(this);
    m_statusBar->setObjectName(QStringLiteral("historyStatusBar"));
    auto* statusLayout = new QHBoxLayout(m_statusBar);
    statusLayout->setContentsMargins(8, 3, 6, 3);
    statusLayout->setSpacing(6);
    m_statusLabel = new ElidingLabel(m_statusBar);
    m_statusLabel->setObjectName(QStringLiteral("historyStatusLabel"));
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setWordWrap(false);
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
    return QSize(320, 260);
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
    m_statusLabel->setToolTip(text);
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
