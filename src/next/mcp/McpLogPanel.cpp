#include "McpLogPanel.h"

#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTime>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace pvf::next {

namespace {

QString shorten(const QString& text, int limit)
{
    if (text.size() <= limit)
        return text;
    return text.left(limit) + QStringLiteral("…");
}

// One-line summary used in the "参数" column.  "{}" carries no information, so
// it is shown as a dash just like an empty payload.
QString summarizeJson(const QString& json, int limit)
{
    QString text = json.simplified();
    if (text.isEmpty() || text == QStringLiteral("{}"))
        return QStringLiteral("—");
    return shorten(text, limit);
}

QString truncateDetail(const QString& text, int limit)
{
    if (text.size() <= limit)
        return text;
    const QString note =
        QStringLiteral("\n…（已截断，原长 %1 字符）").arg(text.size());
    return text.left(std::max<qsizetype>(0, limit - note.size())) + note;
}

// Result::toJson() emits {"commit":{"hash":...,"shortHash":...}}; accepting a
// plain string as well keeps the panel tolerant of older/hand-written payloads.
QString commitHash(const QJsonObject& result)
{
    const QJsonValue value = result.value(QStringLiteral("commit"));
    if (value.isObject())
    {
        const QJsonObject commit = value.toObject();
        const QString hash = commit.value(QStringLiteral("hash")).toString();
        return hash.isEmpty()
                   ? commit.value(QStringLiteral("shortHash")).toString()
                   : hash;
    }
    if (value.isString())
        return value.toString();
    return {};
}

} // namespace

McpLogPanel::McpLogPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mcpLogPanel"));
    setWindowTitle(QStringLiteral("MCP 日志"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(420, 140);
    setFocusPolicy(Qt::NoFocus);

    setStyleSheet(QStringLiteral(R"(
        QWidget#mcpLogPanel { background: #151920; }
        QWidget#mcpLogTitleBar {
            background: #1b212b;
            border-bottom: 1px solid #2b323e;
        }
        QWidget#mcpLogStatusBar {
            background: #151920;
            border-bottom: 1px solid #262e3a;
        }
        QLabel#mcpLogTitle {
            color: #e4e9f1;
            font-weight: 600;
        }
        QLabel#mcpLogCount { color: #7d8798; }
        QLabel#mcpLogServerStatus { color: #8b96a8; }
        QLabel#mcpLogAiStatus { color: #8b96a8; }
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
        QPushButton#mcpLogCloseButton {
            background: transparent;
            border: 0;
            color: #9aa5b5;
            font-size: 16px;
            padding: 0;
        }
        QPushButton#mcpLogCloseButton:hover {
            background: #7a2a24;
            color: #ffffff;
        }
        QPushButton#mcpLogStopButton {
            background: #b8342b;
            border: 1px solid #7f221c;
            color: #ffffff;
        }
        QPushButton#mcpLogStopButton:hover { background: #d14035; }
        QPushButton#mcpLogStopButton:pressed { background: #92281f; }
        QPushButton#mcpLogStopButton:disabled {
            background: #4c2a27;
            border-color: #3b2421;
            color: #8d7a78;
        }
        QPushButton#mcpLogResumeButton {
            background: #2f6b45;
            border: 1px solid #245437;
            color: #ffffff;
        }
        QPushButton#mcpLogResumeButton:hover { background: #3a8053; }
        QPushButton#mcpLogResumeButton:pressed { background: #245437; }
        QPushButton#mcpLogResumeButton:disabled {
            background: #23332a;
            border-color: #2a3a31;
            color: #6d7a72;
        }
        QTreeWidget#mcpLogTree {
            background: #10141a;
            alternate-background-color: #141922;
            border: 0;
            color: #d8dfe9;
            outline: 0;
        }
        QTreeWidget#mcpLogTree::item {
            padding: 2px 3px;
            border-bottom: 1px solid #1b212b;
        }
        QTreeWidget#mcpLogTree::item:selected {
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
    titleBar->setObjectName(QStringLiteral("mcpLogTitleBar"));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 4, 6, 4);
    titleLayout->setSpacing(6);

    auto* titleLabel = new QLabel(QStringLiteral("MCP 日志"), titleBar);
    titleLabel->setObjectName(QStringLiteral("mcpLogTitle"));

    m_countLabel = new QLabel(QStringLiteral("共 0 条"), titleBar);
    m_countLabel->setObjectName(QStringLiteral("mcpLogCount"));

    m_clearButton = new QPushButton(QStringLiteral("清空"), titleBar);
    m_clearButton->setObjectName(QStringLiteral("mcpLogClearButton"));
    m_clearButton->setToolTip(QStringLiteral("清空内存中的 MCP 日志"));
    m_clearButton->setFocusPolicy(Qt::NoFocus);

    m_closeButton = new QPushButton(QStringLiteral("×"), titleBar);
    m_closeButton->setObjectName(QStringLiteral("mcpLogCloseButton"));
    m_closeButton->setToolTip(QStringLiteral("关闭日志面板（可从菜单重新打开）"));
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    m_closeButton->setFixedSize(24, 22);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);
    titleLayout->addWidget(m_countLabel);
    titleLayout->addWidget(m_clearButton);
    titleLayout->addWidget(m_closeButton);
    root->addWidget(titleBar);

    auto* statusBar = new QWidget(this);
    statusBar->setObjectName(QStringLiteral("mcpLogStatusBar"));
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(8, 4, 6, 4);
    statusLayout->setSpacing(6);

    m_serverLabel = new QLabel(QStringLiteral("MCP 未启动"), statusBar);
    m_serverLabel->setObjectName(QStringLiteral("mcpLogServerStatus"));
    m_serverLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_aiLabel = new QLabel(QStringLiteral("AI：空闲"), statusBar);
    m_aiLabel->setObjectName(QStringLiteral("mcpLogAiStatus"));

    m_stopButton = new QPushButton(QStringLiteral("停止 AI"), statusBar);
    m_stopButton->setObjectName(QStringLiteral("mcpLogStopButton"));
    m_stopButton->setToolTip(
        QStringLiteral("急停：取消当前工具调用，并暂停 AI 写入，直到恢复"));
    m_stopButton->setFocusPolicy(Qt::NoFocus);

    m_resumeButton = new QPushButton(QStringLiteral("恢复 AI"), statusBar);
    m_resumeButton->setObjectName(QStringLiteral("mcpLogResumeButton"));
    m_resumeButton->setToolTip(QStringLiteral("恢复 AI 写入能力"));
    m_resumeButton->setFocusPolicy(Qt::NoFocus);

    statusLayout->addWidget(m_serverLabel, 1);
    statusLayout->addWidget(m_aiLabel);
    statusLayout->addWidget(m_stopButton);
    statusLayout->addWidget(m_resumeButton);
    root->addWidget(statusBar);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("mcpLogTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({QStringLiteral("时间"), QStringLiteral("工具"),
                             QStringLiteral("参数"), QStringLiteral("耗时"),
                             QStringLiteral("结果")});
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(12);
    m_tree->setWordWrap(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [](QTreeWidgetItem* item, int) {
                if (item && item->childCount() > 0)
                    item->setExpanded(!item->isExpanded());
            });
    root->addWidget(m_tree, 1);

    connect(m_clearButton, &QPushButton::clicked, this, &McpLogPanel::clearLog);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_stopButton, &QPushButton::clicked, this,
            &McpLogPanel::stopAiRequested);
    connect(m_resumeButton, &QPushButton::clicked, this,
            &McpLogPanel::resumeAiRequested);

    updateHeader();
}

QSize McpLogPanel::sizeHint() const
{
    return QSize(900, 180);
}

void McpLogPanel::setServerState(bool running, quint16 port,
                                 const QString& error)
{
    m_serverRunning = running;
    m_serverPort = port;
    m_serverError = error;
    updateStateLabels();
}

void McpLogPanel::setAiState(bool stopped, bool busy)
{
    m_aiStopped = stopped;
    m_aiBusy = busy;
    updateHeader();
}

void McpLogPanel::setLoggingEnabled(bool on)
{
    if (m_loggingEnabled == on)
        return;
    m_loggingEnabled = on;
    if (!on)
        hide();
}

void McpLogPanel::addSystemEvent(const QString& title, const QString& detail)
{
    const QString name = title.isEmpty() ? QStringLiteral("系统事件") : title;
    appendRow(name, summarizeJson(detail, kMaxSummaryChars), QStringLiteral("—"),
              QStringLiteral("系统"),
              detail.isEmpty() ? QString()
                               : truncateDetail(detail, kMaxDetailChars),
              true, true);
}

void McpLogPanel::addToolInvocation(const QString& name, const QString& argsJson,
                                    const QString& resultJson, qint64 ms,
                                    bool ok)
{
    ++m_toolCalls;

    QJsonObject resultObject;
    const QJsonDocument document = QJsonDocument::fromJson(resultJson.toUtf8());
    if (document.isObject())
        resultObject = document.object();

    const QString hash = commitHash(resultObject);

    QString errorCode;
    QString errorMessage;
    if (!ok)
    {
        const QJsonValue errorValue = resultObject.value(QStringLiteral("error"));
        if (errorValue.isObject())
        {
            const QJsonObject error = errorValue.toObject();
            errorCode = error.value(QStringLiteral("code")).toString();
            errorMessage = error.value(QStringLiteral("message")).toString();
        }
        else if (errorValue.isString())
        {
            errorMessage = errorValue.toString();
        }
        if (errorMessage.isEmpty())
            errorMessage = resultObject.value(QStringLiteral("message")).toString();
    }

    QString resultSummary;
    if (ok)
    {
        resultSummary = QStringLiteral("成功");
        if (!hash.isEmpty())
            resultSummary += QStringLiteral(" · commit %1").arg(hash.left(7));
    }
    else
    {
        resultSummary = QStringLiteral("失败");
        if (!errorCode.isEmpty())
            resultSummary += QStringLiteral(" · %1").arg(errorCode);
        if (!errorMessage.isEmpty())
        {
            const QString message = shorten(errorMessage.simplified(), 48);
            resultSummary += errorCode.isEmpty() ? QStringLiteral(" · ")
                                                 : QStringLiteral("：");
            resultSummary += message;
        }
    }

    QString detail;
    if (!ok)
    {
        detail += QStringLiteral("错误：");
        if (!errorCode.isEmpty())
            detail += errorCode;
        if (!errorMessage.isEmpty())
        {
            if (!errorCode.isEmpty())
                detail += QStringLiteral("：");
            detail += errorMessage;
        }
        if (detail == QStringLiteral("错误："))
            detail += QStringLiteral("未知错误");
        detail += QLatin1Char('\n');
    }
    if (!hash.isEmpty())
        detail += QStringLiteral("commit：%1\n").arg(hash);
    detail += QStringLiteral("参数：%1\n结果：%2").arg(argsJson, resultJson);

    appendRow(name, summarizeJson(argsJson, kMaxSummaryChars),
              QStringLiteral("%1 ms").arg(ms), resultSummary,
              truncateDetail(detail, kMaxDetailChars), ok, false);
}

void McpLogPanel::showFromMenu()
{
    show();
    raise();
    activateWindow();
}

void McpLogPanel::clearLog()
{
    m_tree->clear();
    m_toolCalls = 0;
    updateHeader();
}

QTreeWidgetItem* McpLogPanel::appendRow(const QString& tool,
                                        const QString& target,
                                        const QString& duration,
                                        const QString& result,
                                        const QString& detail, bool ok,
                                        bool system)
{
    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    item->setText(1, tool);
    item->setText(2, target);
    item->setText(3, duration);
    item->setText(4, result);
    item->setToolTip(2, target);
    item->setToolTip(4, result);

    if (!ok)
        item->setForeground(4, QBrush(QColor(220, 80, 70)));
    else if (!system)
        item->setForeground(4, QBrush(QColor(95, 191, 127)));

    if (!detail.isEmpty())
    {
        auto* detailItem = new QTreeWidgetItem(item);
        detailItem->setText(0, detail);
        detailItem->setFirstColumnSpanned(true);
        detailItem->setToolTip(0, detail);
    }

    trimRows();
    if (isVisible())
        m_tree->scrollToItem(item);
    updateHeader();
    return item;
}

void McpLogPanel::trimRows()
{
    while (m_tree->topLevelItemCount() > kMaxRows)
        delete m_tree->takeTopLevelItem(0);
}

void McpLogPanel::updateHeader()
{
    const int rows = m_tree->topLevelItemCount();
    m_countLabel->setText(QStringLiteral("共 %1 条").arg(rows));
    m_countLabel->setToolTip(QStringLiteral("工具调用 %1 次").arg(m_toolCalls));
    m_clearButton->setEnabled(rows > 0);
    updateStateLabels();
    updateButtons();
}

void McpLogPanel::updateStateLabels()
{
    QString serverText;
    QString serverColor = QStringLiteral("#8b96a8");
    QString serverToolTip;

    if (m_serverRunning)
    {
        serverText = QStringLiteral("MCP 运行中");
        if (m_serverPort != 0)
        {
            serverText +=
                QStringLiteral(" · 127.0.0.1:%1").arg(m_serverPort);
            serverToolTip =
                QStringLiteral("MCP 正在监听 127.0.0.1:%1").arg(m_serverPort);
        }
        if (!m_serverError.isEmpty())
        {
            serverText += QStringLiteral(" · %1").arg(m_serverError);
            serverToolTip += QStringLiteral("\n%1").arg(m_serverError);
            serverColor = QStringLiteral("#d98e3c");
        }
        else
        {
            serverColor = QStringLiteral("#6fbf8f");
        }
    }
    else if (!m_serverError.isEmpty())
    {
        serverText = QStringLiteral("MCP 未启动 · %1").arg(m_serverError);
        serverColor = QStringLiteral("#dc5046");
        serverToolTip = m_serverError;
    }
    else
    {
        serverText = QStringLiteral("MCP 未启动");
        serverToolTip = QStringLiteral("MCP 未启用");
    }

    m_serverLabel->setText(serverText);
    m_serverLabel->setStyleSheet(QStringLiteral("color: %1;").arg(serverColor));
    m_serverLabel->setToolTip(serverToolTip);

    QString aiText;
    QString aiColor;
    if (m_aiStopped)
    {
        aiText = QStringLiteral("AI：已急停");
        aiColor = QStringLiteral("#e08a3c");
    }
    else if (m_aiBusy)
    {
        aiText = QStringLiteral("AI：修改中…");
        aiColor = QStringLiteral("#5aa9e6");
    }
    else
    {
        aiText = QStringLiteral("AI：空闲");
        aiColor = QStringLiteral("#8b96a8");
    }
    m_aiLabel->setText(aiText);
    m_aiLabel->setStyleSheet(QStringLiteral("color: %1;").arg(aiColor));
    m_aiLabel->setToolTip(m_aiStopped
                              ? QStringLiteral("AI 写入已暂停，等待人类恢复")
                              : (m_aiBusy
                                     ? QStringLiteral("AI 正在执行写操作，人类编辑暂时锁定")
                                     : QStringLiteral("AI 当前没有写操作")));
}

void McpLogPanel::updateButtons()
{
    m_stopButton->setEnabled(!m_aiStopped);
    m_resumeButton->setEnabled(m_aiStopped);
}

int McpLogPanel::entryCountForTest() const
{
    return m_tree->topLevelItemCount();
}

QString McpLogPanel::entryTargetForTest(int row) const
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    return item ? item->text(2) : QString();
}

QString McpLogPanel::entryDetailForTest(int row) const
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    if (!item || item->childCount() == 0)
        return QString();
    return item->child(0)->text(0);
}

void McpLogPanel::activateEntryForTest(int row)
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    if (!item)
        return;
    m_tree->setCurrentItem(item);
    m_tree->scrollToItem(item);
}

void McpLogPanel::closeEvent(QCloseEvent* event)
{
    hide();
    event->accept();
}

void McpLogPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}

void McpLogPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    emit visibilityChanged(true);
}

} // namespace pvf::next
