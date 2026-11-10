#include "next/history/HistoryPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtTest>

using namespace pvf::next;
namespace store = pvf::store;

namespace {

QLabel* label(HistoryPanel& panel, const char* name)
{
    return panel.findChild<QLabel*>(QString::fromLatin1(name));
}

QPushButton* button(HistoryPanel& panel, const char* name)
{
    return panel.findChild<QPushButton*>(QString::fromLatin1(name));
}

QTreeWidget* tree(HistoryPanel& panel)
{
    return panel.findChild<QTreeWidget*>(QStringLiteral("historyTree"));
}

store::GitCommit makeCommit(int index, const QDateTime& time)
{
    store::GitCommit commit;
    commit.hash = QStringLiteral("%1").arg(index, 8, 16, QLatin1Char('0'))
                  + QString(32, QLatin1Char('0'));
    commit.message = QStringLiteral("pvf: commit %1").arg(index);
    commit.time = time;
    return commit;
}

QDateTime timeAt(int index)
{
    const QDateTime base(QDate(2024, 1, 1), QTime(12, 0));
    return base.addSecs(60 * index);
}

QVector<store::GitCommit> threeCommits()
{
    return {makeCommit(0, timeAt(0)), makeCommit(1, timeAt(1)),
            makeCommit(2, timeAt(2))};
}

} // namespace

class HistoryPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyState();
    void setCommitsOrdersNewestFirst();
    void selectionUpdatesHashAndButtons();
    void restoreButtonEmitsSelection();
    void doubleClickEmitsRestore();
    void busyDisablesButtonsAndShowsStatus();
    void commitLimitKeepsNewest();
    void selectionSurvivesRefresh();
};

void HistoryPanelTest::emptyState()
{
    HistoryPanel panel;

    QCOMPARE(panel.rowCountForTest(), 0);
    QVERIFY(panel.selectedHash().isEmpty());
    QVERIFY(panel.selectedMessage().isEmpty());

    QVERIFY(!button(panel, "historyRestoreButton")->isEnabled());
    QVERIFY(button(panel, "historyRefreshButton")->isEnabled());

    QLabel* empty = label(panel, "historyEmptyLabel");
    QCOMPARE(empty->text(), QStringLiteral("还没有提交"));
    QVERIFY(empty->isVisibleTo(&panel));

    QVERIFY(!label(panel, "historyStatusLabel")->isVisibleTo(&panel));
}

void HistoryPanelTest::setCommitsOrdersNewestFirst()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);

    QCOMPARE(panel.rowCountForTest(), 3);
    QTreeWidget* table = tree(panel);
    QCOMPARE(table->topLevelItem(0)->text(2), QStringLiteral("pvf: commit 2"));
    QCOMPARE(table->topLevelItem(1)->text(2), QStringLiteral("pvf: commit 1"));
    QCOMPARE(table->topLevelItem(2)->text(2), QStringLiteral("pvf: commit 0"));
    QCOMPARE(table->topLevelItem(0)->text(1), commits[2].hash.left(7));
    QVERIFY(!label(panel, "historyEmptyLabel")->isVisibleTo(&panel));
}

void HistoryPanelTest::selectionUpdatesHashAndButtons()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);

    QVERIFY(panel.selectedHash().isEmpty());
    QVERIFY(!button(panel, "historyRestoreButton")->isEnabled());

    panel.selectRowForTest(1);
    QCOMPARE(panel.selectedHash(), commits[1].hash);
    QCOMPARE(panel.selectedMessage(), commits[1].message);
    QVERIFY(button(panel, "historyRestoreButton")->isEnabled());

    panel.selectRowForTest(99);
    QCOMPARE(panel.selectedHash(), commits[1].hash);
}

void HistoryPanelTest::restoreButtonEmitsSelection()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);

    QSignalSpy spy(&panel, &HistoryPanel::restoreRequested);
    auto* restore = button(panel, "historyRestoreButton");

    restore->click();
    QCOMPARE(spy.count(), 0);

    panel.selectRowForTest(0);
    restore->click();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), commits[2].hash);
    QCOMPARE(spy.at(0).at(1).toString(), commits[2].message);
}

void HistoryPanelTest::doubleClickEmitsRestore()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);
    panel.resize(600, 300);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QSignalSpy spy(&panel, &HistoryPanel::restoreRequested);
    QTreeWidget* table = tree(panel);
    const QRect rect = table->visualItemRect(table->topLevelItem(0));
    QVERIFY(rect.isValid());
    const QPoint pos = rect.center();

    // QTest delivers MouseButtonDblClick directly (no synthetic first click);
    // QAbstractItemView only emits doubleClicked when the pressed index still
    // matches, so send the press it expects first.
    QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), commits[2].hash);
    QCOMPARE(spy.at(0).at(1).toString(), commits[2].message);

    panel.setBusy(true);
    QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(spy.count(), 1);
}

void HistoryPanelTest::busyDisablesButtonsAndShowsStatus()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);
    panel.selectRowForTest(0);

    auto* restore = button(panel, "historyRestoreButton");
    auto* refresh = button(panel, "historyRefreshButton");
    QLabel* status = label(panel, "historyStatusLabel");

    QSignalSpy restoreSpy(&panel, &HistoryPanel::restoreRequested);
    QSignalSpy refreshSpy(&panel, &HistoryPanel::refreshRequested);

    panel.setBusy(true);
    QVERIFY(!restore->isEnabled());
    QVERIFY(!refresh->isEnabled());
    QVERIFY(status->isVisibleTo(&panel));
    QVERIFY(status->text().contains(QStringLiteral("处理中")));

    restore->click();
    refresh->click();
    QCOMPARE(restoreSpy.count(), 0);
    QCOMPARE(refreshSpy.count(), 0);

    panel.setStatus(QStringLiteral("正在恢复…"));
    QCOMPARE(status->text(), QStringLiteral("正在恢复…"));

    panel.setBusy(false);
    QVERIFY(restore->isEnabled());
    QVERIFY(refresh->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("正在恢复…"));

    panel.setStatus(QString());
    QVERIFY(!status->isVisibleTo(&panel));

    refresh->click();
    QCOMPARE(refreshSpy.count(), 1);
}

void HistoryPanelTest::commitLimitKeepsNewest()
{
    HistoryPanel panel;
    QVector<store::GitCommit> commits;
    for (int i = 0; i < 205; ++i)
        commits.push_back(makeCommit(i, timeAt(i)));

    panel.setCommits(commits);
    QCOMPARE(panel.rowCountForTest(), 200);
    QTreeWidget* table = tree(panel);
    QCOMPARE(table->topLevelItem(0)->text(2), QStringLiteral("pvf: commit 204"));
    QCOMPARE(table->topLevelItem(199)->text(2),
             QStringLiteral("pvf: commit 5"));
}

void HistoryPanelTest::selectionSurvivesRefresh()
{
    HistoryPanel panel;
    const QVector<store::GitCommit> commits = threeCommits();
    panel.setCommits(commits);
    panel.selectRowForTest(1);
    const QString hash = panel.selectedHash();
    QVERIFY(!hash.isEmpty());

    panel.setCommits(commits);
    QCOMPARE(panel.selectedHash(), hash);
    QVERIFY(button(panel, "historyRestoreButton")->isEnabled());

    panel.setCommits({commits[0]});
    QVERIFY(panel.selectedHash().isEmpty());
    QVERIFY(!button(panel, "historyRestoreButton")->isEnabled());
}

QTEST_MAIN(HistoryPanelTest)
#include "HistoryPanelTest.moc"
