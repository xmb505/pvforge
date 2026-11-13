#include "McpLogPanel.h"

#include "core/Settings.h"

#include <QApplication>
#include <QBrush>
#include <QCloseEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTime>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

enum ItemRole {
    EntityRole = Qt::UserRole + 1,
    EntityUuidRole,
    FrameRole,
};

bool readIntArgument(const QJsonObject& args, const QStringList& keys, int* out)
{
    for (const QString& key : keys)
    {
        const QJsonValue value = args.value(key);
        if (value.isDouble())
        {
            *out = int(std::lround(value.toDouble()));
            return true;
        }
        if (value.isString())
        {
            bool ok = false;
            const int number = value.toString().toInt(&ok);
            if (ok)
            {
                *out = number;
                return true;
            }
        }
    }
    return false;
}

QString readStringArgument(const QJsonObject& args, const QStringList& keys)
{
    for (const QString& key : keys)
    {
        const QJsonValue value = args.value(key);
        if (value.isString() && !value.toString().isEmpty())
            return value.toString();
        if (value.isDouble())
            return QString::number(value.toDouble(), 'g', 12);
    }
    return QString();
}

QString truncateForDisplay(const QString& text, int limit)
{
    if (text.size() <= limit)
        return text;
    return text.left(limit)
           + QStringLiteral("\n…（已截断，原长 %1 字符）").arg(text.size());
}

} // namespace

McpLogPanel::McpLogPanel(QWidget* viewport, QWidget* parent)
    : QWidget(parent ? parent : viewport)
    , m_viewport(viewport)
{
    setObjectName(QStringLiteral("mcpLogPanel"));
    setMinimumSize(340, 180);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("MCP 日志"));

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("mcpLogTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({QStringLiteral("时间"), QStringLiteral("工具"),
                             QStringLiteral("耗时"), QStringLiteral("状态"),
                             QStringLiteral("目标")});
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setWordWrap(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) { activateItem(item); });

    m_statusLabel = new QLabel(QStringLiteral("未启用"), this);
    m_statusLabel->setObjectName(QStringLiteral("mcpLogStatusLabel"));
    m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_countLabel = new QLabel(QStringLiteral("调用 0 次"), this);
    m_countLabel->setObjectName(QStringLiteral("mcpLogCountLabel"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_countLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_detachButton = new QToolButton(this);
    m_detachButton->setObjectName(QStringLiteral("mcpLogDetachButton"));
    m_detachButton->setText(QStringLiteral("弹出"));
    m_detachButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_detachButton->setFocusPolicy(Qt::NoFocus);
    m_detachButton->setToolTip(QStringLiteral("弹出为独立窗口"));
    connect(m_detachButton, &QToolButton::clicked, this, [this] {
        if (m_detached)
            dockWindow();
        else
            detachWindow();
    });

    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName(QStringLiteral("mcpLogCloseButton"));
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    m_closeButton->setToolTip(QStringLiteral("关闭日志窗（状态栏可重新打开）"));
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::close);

    m_stopButton = new QToolButton(this);
    m_stopButton->setObjectName(QStringLiteral("mcpLogStopButton"));
    m_stopButton->setText(QStringLiteral("急停"));
    m_stopButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_stopButton->setFocusPolicy(Qt::NoFocus);
    m_stopButton->setToolTip(
        QStringLiteral("急停：停止 AI 写操作，之后的写调用返回 stopped_by_human"));
    m_stopButton->setStyleSheet(
        QStringLiteral("QToolButton { background: #b8342b; color: white; "
                       "border: 1px solid #7f221c; padding: 2px 6px; }"
                       "QToolButton:disabled { background: #7a3a35; "
                       "color: #bbbbbb; }"));
    connect(m_stopButton, &QToolButton::clicked, this,
            &McpLogPanel::stopAiRequested);

    m_resumeButton = new QToolButton(this);
    m_resumeButton->setObjectName(QStringLiteral("mcpLogResumeButton"));
    m_resumeButton->setText(QStringLiteral("恢复"));
    m_resumeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_resumeButton->setFocusPolicy(Qt::NoFocus);
    m_resumeButton->setToolTip(QStringLiteral("恢复 AI 写操作"));
    connect(m_resumeButton, &QToolButton::clicked, this,
            &McpLogPanel::resumeAiRequested);
    m_resumeButton->hide();

    m_clearButton = new QToolButton(this);
    m_clearButton->setObjectName(QStringLiteral("mcpLogClearButton"));
    m_clearButton->setText(QStringLiteral("清空"));
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    m_clearButton->setToolTip(QStringLiteral("清空内存中的调用日志"));
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        m_tree->clear();
        m_totalCalls = 0;
        updateHeader();
    });

    const QRect dock = Settings::mcpLogDockGeometry();
    if (dock.width() >= minimumWidth() && dock.height() >= minimumHeight())
        resize(dock.size());
    else
        resize(420, 280);
    updateButtons();
    updateHeader();
    layoutChildren();
}

void McpLogPanel::setServerState(bool running, quint16 port, const QString& error)
{
    const bool wasRunning = m_serverRunning;
    m_serverRunning = running;
    m_serverPort = port;
    m_serverError = error;
    if (running && !wasRunning)
        m_autoShowPending = true;
    if (!running)
        m_autoShowPending = false;
    updateHeader();
}

void McpLogPanel::setAiState(bool stopped, bool busy, bool writesEnabled)
{
    m_aiMode = true;
    m_resumeButton->setVisible(true);
    m_stopButton->setText(QStringLiteral("急停"));
    m_stopped = stopped;
    m_aiBusy = busy;
    m_writesEnabled = writesEnabled;
    if (!stopped)
        m_armed = false;
    updateHeader();
}

void McpLogPanel::setWritesEnabled(bool enabled)
{
    m_writesEnabled = enabled;
    updateHeader();
}

void McpLogPanel::setPermissionState(bool armed, const QString& sceneName,
                                     bool stopped)
{
    m_aiMode = false;
    m_resumeButton->setVisible(false);
    m_stopButton->setText(QStringLiteral("停止 AI"));
    m_armed = armed;
    m_stopped = stopped;
    m_sceneName = sceneName;
    updateHeader();
}

void McpLogPanel::addSystemEvent(const QString& title, const QString& detail)
{
    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    item->setText(1, title);
    item->setText(2, QStringLiteral("—"));
    item->setText(3, QStringLiteral("info"));
    item->setText(4, detail.isEmpty() ? QStringLiteral("—") : detail);
    if (!detail.isEmpty())
    {
        auto* detailItem = new QTreeWidgetItem(item);
        detailItem->setText(0, truncateForDisplay(detail, kMaxDetailChars));
        detailItem->setFirstColumnSpanned(true);
    }
    while (m_tree->topLevelItemCount() > kMaxRows)
        delete m_tree->takeTopLevelItem(0);
    if (isVisible())
        m_tree->scrollToItem(item);
    updateHeader();
}

void McpLogPanel::setLoggingEnabled(bool on)
{
    if (m_loggingEnabled == on)
        return;
    m_loggingEnabled = on;
    if (!on && isVisible())
    {
        hide();
        emit visibilityChanged(false);
    }
}

void McpLogPanel::addToolInvocation(const QString& name, const QString& argsJson,
                                    const QString& resultJson, qint64 durationMs,
                                    bool ok)
{
    QJsonObject args;
    const QJsonDocument argsDocument = QJsonDocument::fromJson(argsJson.toUtf8());
    if (argsDocument.isObject())
        args = argsDocument.object();

    QStringList entityKeys{QStringLiteral("entityId"),
                           QStringLiteral("targetEntityId")};
    // entity.get uses "id" for the entity; other tools use id for assets or
    // scenes, so only interpret it as an entity for entity.* calls.
    if (name.startsWith(QStringLiteral("entity.")))
        entityKeys.append(QStringLiteral("id"));

    int entityId = -1;
    int frame = -1;
    readIntArgument(args, entityKeys, &entityId);
    readIntArgument(args, {QStringLiteral("frame")}, &frame);
    const QString scene =
        readStringArgument(args, {QStringLiteral("scene"),
                                  QStringLiteral("sceneId")});
    QString entityUuid = readStringArgument(args, entityKeys);
    if (entityUuid.contains(QLatin1Char('-')) && entityUuid.size() >= 32)
        entityId = -1;
    else
        entityUuid.clear();

    QString target;
    QStringList targetParts;
    if (entityId >= 0)
        targetParts.append(QStringLiteral("实体 #%1").arg(entityId));
    else if (!entityUuid.isEmpty())
        targetParts.append(QStringLiteral("实体 %1").arg(entityUuid.left(8)));
    if (!scene.isEmpty())
        targetParts.append(QStringLiteral("场景 %1").arg(scene));
    if (frame >= 0)
        targetParts.append(QStringLiteral("帧 %1").arg(frame));
    target = targetParts.join(QStringLiteral(" · "));
    if (target.isEmpty())
    {
        target = argsJson.simplified();
        if (target.size() > 48)
            target = target.left(48) + QStringLiteral("…");
        if (target.isEmpty() || target == QStringLiteral("{}"))
            target = QStringLiteral("—");
    }

    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    item->setText(1, name);
    item->setText(2, QStringLiteral("%1 ms").arg(durationMs));
    item->setText(3, ok ? QStringLiteral("ok") : QStringLiteral("error"));
    item->setText(4, target);
    item->setData(0, EntityRole, entityId);
    item->setData(0, EntityUuidRole, entityUuid);
    item->setData(0, FrameRole, frame);
    if (!ok)
        item->setForeground(3, QBrush(QColor(220, 80, 70)));

    const QJsonObject resultObject =
        QJsonDocument::fromJson(resultJson.toUtf8()).object();
    const QString commitHash =
        resultObject.value(QStringLiteral("commit"))
            .toObject()
            .value(QStringLiteral("shortHash"))
            .toString();
    if (!commitHash.isEmpty())
    {
        item->setText(3, item->text(3) + QStringLiteral(" · ") + commitHash);
        item->setToolTip(3, QStringLiteral("提交 %1").arg(commitHash));
    }
    QString errorCode =
        resultObject.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("code"))
            .toString();
    if (!ok && errorCode.isEmpty())
        errorCode = QStringLiteral("unknown");

    QString detail;
    if (!errorCode.isEmpty())
        detail += QStringLiteral("错误码：%1\n").arg(errorCode);
    if (!commitHash.isEmpty())
        detail += QStringLiteral("提交：%1\n").arg(commitHash);
    detail += QStringLiteral("参数：%1\n结果：%2")
                  .arg(truncateForDisplay(argsJson, kMaxDetailChars),
                       truncateForDisplay(resultJson, kMaxDetailChars));
    auto* detailItem = new QTreeWidgetItem(item);
    detailItem->setText(0, detail);
    detailItem->setFirstColumnSpanned(true);
    detailItem->setToolTip(0, QStringLiteral("参数与结果（截断显示）"));

    ++m_totalCalls;
    while (m_tree->topLevelItemCount() > kMaxRows)
        delete m_tree->takeTopLevelItem(0);
    if (isVisible())
        m_tree->scrollToItem(item);

    updateHeader();

    if (m_autoShowPending && m_loggingEnabled)
    {
        m_autoShowPending = false;
        if (!isVisible())
        {
            show();
            raise();
            emit visibilityChanged(true);
        }
    }
}

void McpLogPanel::showFromMenu()
{
    show();
    raise();
    emit visibilityChanged(true);
}

void McpLogPanel::updateHeader()
{
    if (m_serverRunning)
    {
        QString text = QStringLiteral("监听中 · 127.0.0.1:%1").arg(m_serverPort);
        if (m_stopped)
        {
            text += QStringLiteral(" · 已急停");
        }
        else if (m_aiBusy)
        {
            text += QStringLiteral(" · AI 修改中");
        }
        else if (m_armed)
        {
            text += QStringLiteral(" · 已武装：%1")
                        .arg(m_sceneName.isEmpty() ? QStringLiteral("未分场景")
                                                   : m_sceneName);
        }
        else if (!m_writesEnabled)
        {
            text += QStringLiteral(" · 只读");
        }
        else
        {
            text += QStringLiteral(" · 可写");
        }
        m_statusLabel->setText(text);
    }
    else if (!m_serverError.isEmpty())
        m_statusLabel->setText(QStringLiteral("启动失败"));
    else
        m_statusLabel->setText(QStringLiteral("未启用"));

    QString tooltip;
    if (m_serverRunning)
    {
        tooltip = QStringLiteral("MCP 正在监听 127.0.0.1:%1").arg(m_serverPort);
        if (m_stopped)
            tooltip += QStringLiteral("\n人类已急停，写操作返回 stopped_by_human");
        else if (m_aiBusy)
            tooltip += QStringLiteral("\nAI 正在修改工程，人类编辑已临时锁定");
        else if (m_armed)
            tooltip += QStringLiteral("\n写权限已解锁：%1").arg(m_sceneName);
        else if (!m_writesEnabled)
            tooltip += QStringLiteral("\n只读：Git 未启用，写工具返回 "
                                      "permission_denied（git_disabled）");
        else
            tooltip += QStringLiteral("\n可写：Git 已启用");
    }
    else
    {
        tooltip = m_serverError.isEmpty() ? QStringLiteral("MCP 未启用")
                                          : m_serverError;
    }
    m_statusLabel->setToolTip(tooltip);

    QPalette statusPalette = QApplication::palette();
    if (m_stopped && m_serverRunning)
        statusPalette.setColor(QPalette::WindowText, QColor(220, 140, 60));
    else if (!m_serverError.isEmpty() && !m_serverRunning)
        statusPalette.setColor(QPalette::WindowText, QColor(220, 80, 70));
    else if (m_aiBusy)
        statusPalette.setColor(QPalette::WindowText, QColor(90, 169, 230));
    m_statusLabel->setPalette(statusPalette);

    m_countLabel->setText(QStringLiteral("调用 %1 次").arg(m_totalCalls));
    m_stopButton->setEnabled(m_serverRunning && !m_stopped);
    m_resumeButton->setEnabled(m_serverRunning && m_stopped);
    m_clearButton->setEnabled(m_tree->topLevelItemCount() > 0);
}

void McpLogPanel::updateButtons()
{
    m_detachButton->setText(m_detached ? QStringLiteral("停靠")
                                       : QStringLiteral("弹出"));
    m_detachButton->setToolTip(m_detached
                                   ? QStringLiteral("停靠回视口左侧")
                                   : QStringLiteral("弹出为独立窗口"));
}

QRect McpLogPanel::titleRect() const
{
    return QRect(0, 0, width(), kTitleHeight);
}

QRect McpLogPanel::headerRect() const
{
    return QRect(0, kTitleHeight, width(), kHeaderHeight);
}

QRect McpLogPanel::gripRect() const
{
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

void McpLogPanel::layoutChildren()
{
    int right = width() - 4;
    m_closeButton->setGeometry(right - 24, 3, 24, 22);
    right -= 26;
    m_detachButton->setGeometry(right - 44, 3, 44, 22);
    right -= 48;

    const int headerY = kTitleHeight;
    right = width() - 6;
    if (m_aiMode)
    {
        m_resumeButton->setGeometry(right - 48, headerY + 3, 48, 22);
        right -= 52;
    }
    m_stopButton->setGeometry(right - 48, headerY + 3, 48, 22);
    right -= 52;
    m_clearButton->setGeometry(right - 48, headerY + 3, 48, 22);
    right -= 52;
    m_countLabel->setGeometry(right - 84, headerY + 4, 84, 20);
    right -= 88;
    m_statusLabel->setGeometry(8, headerY + 4, std::max(40, right - 12), 20);

    m_tree->setGeometry(0, kTitleHeight + kHeaderHeight, width(),
                        std::max(0, height() - kTitleHeight - kHeaderHeight));
}

void McpLogPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QPalette pal = palette();

    painter.fillRect(rect(), pal.color(QPalette::Window));
    painter.fillRect(titleRect(), pal.color(QPalette::AlternateBase));
    painter.fillRect(headerRect(), pal.color(QPalette::Window));
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.drawLine(0, kTitleHeight - 1, width(), kTitleHeight - 1);
    painter.drawLine(0, kTitleHeight + kHeaderHeight - 1, width(),
                     kTitleHeight + kHeaderHeight - 1);

    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.color(QPalette::PlaceholderText));
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 2; ++column)
            painter.drawRect(QRect(6 + column * 5, 8 + row * 5, 2, 2));

    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(pal.color(QPalette::WindowText));
    painter.drawText(titleRect().adjusted(22, 0, -100, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("MCP 日志"));

    painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    for (int i = 0; i < 3; ++i)
    {
        const int offset = 3 + i * 4;
        painter.drawLine(width() - offset, height() - 2, width() - 2,
                         height() - offset);
    }
}

void McpLogPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void McpLogPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_initialized || m_detached)
        return;

    QRect geometry = Settings::mcpLogDockGeometry();
    if (geometry.x() < 0 || geometry.y() < 0)
        geometry = QRect(16, 16, 420, 280);
    setGeometry(clampedDockGeometry(geometry));
    m_initialized = true;
    layoutChildren();
}

QRect McpLogPanel::clampedDockGeometry(const QRect& geometry) const
{
    if (!m_viewport)
        return geometry;
    const QSize viewportSize = m_viewport->size();
    QSize size = geometry.size();
    const int maxW = std::max(minimumWidth(), viewportSize.width());
    const int maxH = std::max(minimumHeight(), viewportSize.height());
    size.setWidth(std::clamp(size.width(), minimumWidth(), maxW));
    size.setHeight(std::clamp(size.height(), minimumHeight(), maxH));
    const int maxX = std::max(0, viewportSize.width() - size.width());
    const int maxY = std::max(0, viewportSize.height() - size.height());
    return QRect(QPoint(std::clamp(geometry.x(), 0, maxX),
                        std::clamp(geometry.y(), 0, maxY)),
                 size);
}

void McpLogPanel::clampDockGeometry()
{
    if (!m_detached)
        setGeometry(clampedDockGeometry(geometry()));
}

void McpLogPanel::saveDockGeometry()
{
    if (!m_detached)
        Settings::setMcpLogDockGeometry(geometry());
}

void McpLogPanel::detachWindow()
{
    if (m_detached)
        return;
    saveDockGeometry();
    m_detached = true;
    setParent(m_viewport ? m_viewport->window() : nullptr);
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("MCP 日志"));

    QRect geometry = Settings::mcpLogWindowGeometry();
    bool onScreen = false;
    for (QScreen* screen : QGuiApplication::screens())
    {
        if (screen->availableGeometry().intersects(geometry))
        {
            onScreen = true;
            break;
        }
    }
    if (!onScreen && QGuiApplication::primaryScreen())
        geometry.moveCenter(
            QGuiApplication::primaryScreen()->availableGeometry().center());
    setGeometry(geometry);
    show();
    raise();
    m_initialized = true;
    updateButtons();
    layoutChildren();
}

void McpLogPanel::dockWindow()
{
    if (!m_detached)
        return;
    Settings::setMcpLogWindowGeometry(geometry());
    m_detached = false;
    setWindowFlags(Qt::Widget);
    setParent(m_viewport);
    show();
    setGeometry(clampedDockGeometry(Settings::mcpLogDockGeometry()));
    raise();
    m_initialized = true;
    updateButtons();
    layoutChildren();
}

void McpLogPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (gripRect().contains(event->position().toPoint()))
    {
        m_resizing = true;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = geometry();
        return;
    }
    if (titleRect().contains(event->position().toPoint()))
    {
        m_dragging = true;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = geometry();
        return;
    }
    QWidget::mousePressEvent(event);
}

void McpLogPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_resizing)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        QSize size = m_pressGeometry.size() + QSize(delta.x(), delta.y());
        size.setWidth(std::max(minimumWidth(), size.width()));
        size.setHeight(std::max(minimumHeight(), size.height()));
        if (!m_detached && m_viewport)
        {
            size.setWidth(std::min(size.width(), m_viewport->width()));
            size.setHeight(std::min(size.height(), m_viewport->height()));
        }
        setGeometry(QRect(m_pressGeometry.topLeft(), size));
        return;
    }

    if (m_dragging)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        if (m_detached)
            move(m_pressGeometry.topLeft() + delta);
        else
            setGeometry(clampedDockGeometry(m_pressGeometry.translated(delta)));
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void McpLogPanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragging && !m_detached && m_viewport)
    {
        const QWidget* mainWindow = m_viewport->window();
        const QRect frame = mainWindow ? mainWindow->frameGeometry() : QRect();
        if (!frame.contains(event->globalPosition().toPoint()))
        {
            m_dragging = false;
            const QPoint globalPos = event->globalPosition().toPoint();
            detachWindow();
            move(globalPos - QPoint(width() / 2, 10));
            m_resizing = false;
            return;
        }
    }

    if (m_dragging || m_resizing)
    {
        if (m_detached)
            Settings::setMcpLogWindowGeometry(geometry());
        else
            saveDockGeometry();
    }
    m_dragging = false;
    m_resizing = false;
}

void McpLogPanel::closeEvent(QCloseEvent* event)
{
    if (m_detached)
        Settings::setMcpLogWindowGeometry(geometry());
    else
        saveDockGeometry();
    hide();
    emit visibilityChanged(false);
    event->accept();
}

void McpLogPanel::activateItem(QTreeWidgetItem* item)
{
    if (!item || item->parent())
        return;
    const int entityId = item->data(0, EntityRole).toInt();
    const QString entityUuid = item->data(0, EntityUuidRole).toString();
    const int frame = item->data(0, FrameRole).toInt();
    if (!entityUuid.isEmpty())
        emit entityUuidActivated(entityUuid);
    else if (entityId >= 0)
        emit entityActivated(EntityId(entityId));
    if (frame >= 0)
        emit frameRequested(frame);
}

#ifdef PVFORGE_TEST_HOOKS
int McpLogPanel::entryCountForTest() const
{
    return m_tree->topLevelItemCount();
}

QString McpLogPanel::entryTargetForTest(int row) const
{
    QTreeWidgetItem* item = m_tree->topLevelItem(row);
    return item ? item->text(4) : QString();
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
    activateItem(item);
}
#endif

} // namespace pvf
