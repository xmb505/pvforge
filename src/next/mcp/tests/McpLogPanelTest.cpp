#include "next/mcp/McpLogPanel.h"

#include <QColor>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

using pvf::next::McpLogPanel;

namespace {

const QString kOkResult = QStringLiteral(
    R"({"ok":true,"data":{"count":1},"commit":{"hash":"abcdef1234567890","shortHash":"abcdef1"}})");
const QString kErrorResult = QStringLiteral(
    R"({"ok":false,"error":{"code":"permission_denied","message":"实体已锁定"}})");

QPushButton* button(McpLogPanel& panel, const char* name)
{
    return panel.findChild<QPushButton*>(QString::fromLatin1(name));
}

QLabel* label(McpLogPanel& panel, const char* name)
{
    return panel.findChild<QLabel*>(QString::fromLatin1(name));
}

QTreeWidget* tree(McpLogPanel& panel)
{
    return panel.findChild<QTreeWidget*>(QStringLiteral("mcpLogTree"));
}

} // namespace

class McpLogPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void defaults();
    void serverStateText();
    void aiStateControlsButtons();
    void stopAndResumeSignals();
    void toolInvocationSuccess();
    void toolInvocationFailureIsRed();
    void systemEventRow();
    void rowLimitKeepsNewest();
    void detailIsTruncated();
    void clearButtonEmptiesLog();
    void activateEntrySelectsRow();
    void visibilitySignals();
    void loggingDisabledHides();
};

void McpLogPanelTest::defaults()
{
    McpLogPanel panel;
    QCOMPARE(panel.entryCountForTest(), 0);
    QVERIFY(panel.loggingEnabled());
    QCOMPARE(panel.sizeHint().height(), 180);

    QVERIFY(label(panel, "mcpLogServerStatus")->text().contains(QStringLiteral("未启动")));
    QCOMPARE(label(panel, "mcpLogAiStatus")->text(), QStringLiteral("AI：空闲"));

    QVERIFY(button(panel, "mcpLogStopButton")->isEnabled());
    QVERIFY(!button(panel, "mcpLogResumeButton")->isEnabled());
    QVERIFY(!button(panel, "mcpLogClearButton")->isEnabled());
    QVERIFY(tree(panel));
}

void McpLogPanelTest::serverStateText()
{
    McpLogPanel panel;
    panel.setServerState(true, 47821);
    const QString running = label(panel, "mcpLogServerStatus")->text();
    QVERIFY(running.contains(QStringLiteral("运行中")));
    QVERIFY(running.contains(QStringLiteral("47821")));

    panel.setServerState(false, 0, QStringLiteral("端口 47821 被占用"));
    const QString failed = label(panel, "mcpLogServerStatus")->text();
    QVERIFY(failed.contains(QStringLiteral("未启动")));
    QVERIFY(failed.contains(QStringLiteral("被占用")));

    panel.setServerState(false, 0);
    QCOMPARE(label(panel, "mcpLogServerStatus")->text(), QStringLiteral("MCP 未启动"));
}

void McpLogPanelTest::aiStateControlsButtons()
{
    McpLogPanel panel;

    panel.setAiState(false, true);
    QCOMPARE(label(panel, "mcpLogAiStatus")->text(), QStringLiteral("AI：修改中…"));
    QVERIFY(button(panel, "mcpLogStopButton")->isEnabled());
    QVERIFY(!button(panel, "mcpLogResumeButton")->isEnabled());

    panel.setAiState(true, false);
    QCOMPARE(label(panel, "mcpLogAiStatus")->text(), QStringLiteral("AI：已急停"));
    QVERIFY(!button(panel, "mcpLogStopButton")->isEnabled());
    QVERIFY(button(panel, "mcpLogResumeButton")->isEnabled());

    panel.setAiState(false, false);
    QCOMPARE(label(panel, "mcpLogAiStatus")->text(), QStringLiteral("AI：空闲"));
    QVERIFY(button(panel, "mcpLogStopButton")->isEnabled());
    QVERIFY(!button(panel, "mcpLogResumeButton")->isEnabled());
}

void McpLogPanelTest::stopAndResumeSignals()
{
    McpLogPanel panel;
    QSignalSpy stopSpy(&panel, &McpLogPanel::stopAiRequested);
    QSignalSpy resumeSpy(&panel, &McpLogPanel::resumeAiRequested);

    button(panel, "mcpLogStopButton")->click();
    QCOMPARE(stopSpy.count(), 1);

    panel.setAiState(true, false);
    button(panel, "mcpLogStopButton")->click();
    QCOMPARE(stopSpy.count(), 1);

    button(panel, "mcpLogResumeButton")->click();
    QCOMPARE(resumeSpy.count(), 1);
}

void McpLogPanelTest::toolInvocationSuccess()
{
    McpLogPanel panel;
    panel.addToolInvocation(QStringLiteral("entity.create"),
                            QStringLiteral(R"({"name":"角色","frame":0})"),
                            kOkResult, 42, true);

    QCOMPARE(panel.entryCountForTest(), 1);
    QVERIFY(panel.entryTargetForTest(0).contains(QStringLiteral("角色")));
    QVERIFY(panel.entryDetailForTest(0).contains(QStringLiteral("参数：")));
    QVERIFY(panel.entryDetailForTest(0).contains(
        QStringLiteral("commit：abcdef1234567890")));

    QTreeWidgetItem* item = tree(panel)->topLevelItem(0);
    QCOMPARE(item->text(1), QStringLiteral("entity.create"));
    QCOMPARE(item->text(3), QStringLiteral("42 ms"));
    QVERIFY(item->text(4).contains(QStringLiteral("成功")));
    QVERIFY(item->text(4).contains(QStringLiteral("abcdef1")));
    QCOMPARE(button(panel, "mcpLogClearButton")->isEnabled(), true);
}

void McpLogPanelTest::toolInvocationFailureIsRed()
{
    McpLogPanel panel;
    panel.addToolInvocation(QStringLiteral("entity.update"), QStringLiteral("{}"),
                            kErrorResult, 7, false);

    QCOMPARE(panel.entryCountForTest(), 1);
    QTreeWidgetItem* item = tree(panel)->topLevelItem(0);
    QVERIFY(item->text(4).contains(QStringLiteral("失败")));
    QVERIFY(item->text(4).contains(QStringLiteral("permission_denied")));
    QVERIFY(item->text(4).contains(QStringLiteral("实体已锁定")));
    QCOMPARE(item->foreground(4).color(), QColor(220, 80, 70));

    const QString detail = panel.entryDetailForTest(0);
    QVERIFY(detail.contains(QStringLiteral("permission_denied")));
    QVERIFY(detail.contains(QStringLiteral("实体已锁定")));
}

void McpLogPanelTest::systemEventRow()
{
    McpLogPanel panel;
    panel.addSystemEvent(QStringLiteral("通知：server.ready"),
                         QStringLiteral("已监听 127.0.0.1:47821"));

    QCOMPARE(panel.entryCountForTest(), 1);
    QTreeWidgetItem* item = tree(panel)->topLevelItem(0);
    QCOMPARE(item->text(1), QStringLiteral("通知：server.ready"));
    QVERIFY(item->text(2).contains(QStringLiteral("127.0.0.1:47821")));
    QCOMPARE(item->text(4), QStringLiteral("系统"));
    QCOMPARE(panel.entryDetailForTest(0),
             QStringLiteral("已监听 127.0.0.1:47821"));

    panel.addSystemEvent(QStringLiteral("AI 已停止"));
    QCOMPARE(panel.entryCountForTest(), 2);
    QCOMPARE(panel.entryDetailForTest(1), QString());
}

void McpLogPanelTest::rowLimitKeepsNewest()
{
    McpLogPanel panel;
    for (int i = 0; i < 520; ++i)
    {
        panel.addToolInvocation(QStringLiteral("tool.%1").arg(i),
                                QStringLiteral("{}"), kOkResult, i, true);
    }

    QCOMPARE(panel.entryCountForTest(), 500);
    QTreeWidgetItem* first = tree(panel)->topLevelItem(0);
    QTreeWidgetItem* last = tree(panel)->topLevelItem(499);
    QCOMPARE(first->text(1), QStringLiteral("tool.20"));
    QCOMPARE(last->text(1), QStringLiteral("tool.519"));
}

void McpLogPanelTest::detailIsTruncated()
{
    McpLogPanel panel;
    panel.addToolInvocation(QStringLiteral("asset.import"),
                            QString(5000, QLatin1Char('a')),
                            QString(5000, QLatin1Char('b')), 1, true);

    const QString detail = panel.entryDetailForTest(0);
    QVERIFY(!detail.isEmpty());
    QVERIFY(detail.size() <= 1500);
    QVERIFY(detail.contains(QStringLiteral("已截断")));
}

void McpLogPanelTest::clearButtonEmptiesLog()
{
    McpLogPanel panel;
    panel.addSystemEvent(QStringLiteral("事件"));
    QCOMPARE(panel.entryCountForTest(), 1);

    button(panel, "mcpLogClearButton")->click();
    QCOMPARE(panel.entryCountForTest(), 0);
    QCOMPARE(label(panel, "mcpLogCount")->text(), QStringLiteral("共 0 条"));
    QVERIFY(!button(panel, "mcpLogClearButton")->isEnabled());
}

void McpLogPanelTest::activateEntrySelectsRow()
{
    McpLogPanel panel;
    panel.addSystemEvent(QStringLiteral("事件一"));
    panel.addSystemEvent(QStringLiteral("事件二"));

    panel.activateEntryForTest(1);
    QCOMPARE(tree(panel)->currentItem(), tree(panel)->topLevelItem(1));

    panel.activateEntryForTest(99);
    QCOMPARE(tree(panel)->currentItem(), tree(panel)->topLevelItem(1));
}

void McpLogPanelTest::visibilitySignals()
{
    McpLogPanel panel;
    QSignalSpy spy(&panel, &McpLogPanel::visibilityChanged);

    panel.showFromMenu();
    QVERIFY(panel.isVisible());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    panel.close();
    QVERIFY(!panel.isVisible());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void McpLogPanelTest::loggingDisabledHides()
{
    McpLogPanel panel;
    panel.showFromMenu();
    QVERIFY(panel.isVisible());

    QSignalSpy spy(&panel, &McpLogPanel::visibilityChanged);
    panel.setLoggingEnabled(false);
    QVERIFY(!panel.loggingEnabled());
    QVERIFY(!panel.isVisible());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
}

QTEST_MAIN(McpLogPanelTest)

#include "McpLogPanelTest.moc"
