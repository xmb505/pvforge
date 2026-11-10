#pragma once

#include "store/Store.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace pvf::next {

// Read-only git history browser for the new UI.
//
// The panel never talks to git: the host injects commits through setCommits(),
// toggles setBusy() while a restore/refresh runs, and performs the actual work
// when refreshRequested()/restoreRequested() arrive.
//
// Rows are always newest first.  At most kMaxCommits rows are kept; older
// commits are dropped.  The restore button needs a selected row and is
// disabled while busy; the refresh button is disabled while busy as well.
class HistoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    // Replaces the table with `commits`, sorted newest first and truncated to
    // the newest 200 entries.  A selection on a hash that still exists is
    // preserved, otherwise it is cleared.
    void setCommits(const QVector<store::GitCommit>& commits);
    void setBusy(bool busy);
    void setStatus(const QString& text);

    QString selectedHash() const;
    QString selectedMessage() const;

    // Test hooks.  selectRowForTest behaves like a mouse click on the row.
    int rowCountForTest() const;
    void selectRowForTest(int row);

    QSize sizeHint() const override;

signals:
    void restoreRequested(const QString& hash, const QString& message);
    void refreshRequested();

private:
    void updateButtons();
    void updateStatus();
    void updateEmptyState();
    void emitRestore();
    QString currentHash() const;

    QWidget* m_statusBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QTreeWidget* m_tree = nullptr;
    QStackedWidget* m_stack = nullptr;

    bool m_busy = false;
    QString m_statusText;

    static constexpr int kMaxCommits = 200;
};

} // namespace pvf::next
