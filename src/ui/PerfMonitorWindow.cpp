#include "ui/PerfMonitorWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShowEvent>
#include <QSysInfo>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace pvf {
namespace {

QColor categoryColor(perf::Category category)
{
    switch (category)
    {
    case perf::Category::Render2:
        return QColor(61, 174, 233);
    case perf::Category::Meta:
        return QColor(150, 140, 220);
    case perf::Category::Lookup:
        return QColor(90, 180, 160);
    case perf::Category::Decode:
        return QColor(230, 160, 60);
    case perf::Category::Encode:
        return QColor(225, 100, 70);
    case perf::Category::Disk:
        return QColor(180, 140, 100);
    case perf::Category::Sqlite:
        return QColor(140, 120, 180);
    case perf::Category::Solver:
        return QColor(240, 200, 80);
    case perf::Category::Html:
        return QColor(230, 120, 180);
    case perf::Category::Video:
        return QColor(100, 200, 240);
    case perf::Category::Paint:
        return QColor(120, 200, 120);
    case perf::Category::Ui:
        return QColor(160, 160, 165);
    case perf::Category::Frame:
        return QColor(70, 76, 84);
    default:
        return QColor(110, 110, 115);
    }
}

bool isDarkColor(const QColor& color)
{
    return 0.299 * color.redF() + 0.587 * color.greenF()
               + 0.114 * color.blueF()
           < 0.55;
}

QString treeNodeLabel(const perf::CallNode& node)
{
    if (!node.name.isEmpty())
        return node.name;
    return perf::categoryLabel(node.category);
}

QString formatBytes(qint64 bytes)
{
    const double value = double(std::max<qint64>(0, bytes));
    if (value >= 1024.0 * 1024.0 * 1024.0)
        return QString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2)
               + QStringLiteral(" GB");
    if (value >= 1024.0 * 1024.0)
        return QString::number(value / (1024.0 * 1024.0), 'f', 1)
               + QStringLiteral(" MB");
    if (value >= 1024.0)
        return QString::number(value / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(value, 'f', 0) + QStringLiteral(" B");
}

QString msText(double ms)
{
    return ms > 0.0 ? QString::number(ms, 'f', 1) + QStringLiteral(" ms")
                    : QStringLiteral("—");
}

QJsonObject systemStatsToJson(const perf::SystemStats& stats)
{
    QJsonObject object;
    object.insert(QStringLiteral("supported"), stats.supported);
    object.insert(QStringLiteral("note"), stats.note);
    object.insert(QStringLiteral("processCpuPercent"), stats.processCpuPercent);
    object.insert(QStringLiteral("rssBytes"), QJsonValue(stats.rssBytes));
    object.insert(QStringLiteral("ioReadBytes"), QJsonValue(stats.ioReadBytes));
    object.insert(QStringLiteral("ioWriteBytes"), QJsonValue(stats.ioWriteBytes));
    object.insert(QStringLiteral("threadCount"), stats.threadCount);
    object.insert(QStringLiteral("load1"), stats.load1);
    object.insert(QStringLiteral("load5"), stats.load5);
    object.insert(QStringLiteral("load15"), stats.load15);
    object.insert(QStringLiteral("cacheDiskBytes"),
                  QJsonValue(stats.cacheDiskBytes));
    object.insert(QStringLiteral("cacheFileCount"), stats.cacheFileCount);
    return object;
}

// Table cell that sorts by the stored number instead of the formatted text.
class NumberTableItem : public QTableWidgetItem
{
public:
    NumberTableItem(double value, const QString& text)
        : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, value);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant value = other.data(Qt::UserRole);
        if (value.isValid())
            return data(Qt::UserRole).toDouble() < value.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};

} // namespace

class FrameSparkline : public QWidget
{
public:
    explicit FrameSparkline(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("perfSparkline"));
        setMinimumHeight(128);
    }

    void setFrames(const QVector<perf::FrameSample>& frames)
    {
        m_frames = frames;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        // Theme follows the host palette so the charts do not look like a
        // foreign dark box inside a light (or dark) main window.
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::Text);
        const QColor mid = palette().color(QPalette::Mid);
        QColor muted = text;
        muted.setAlpha(160);
        QColor grid = mid;
        grid.setAlpha(150);
        painter.fillRect(rect(), base);

        const QRectF plot = QRectF(rect()).adjusted(10.0, 22.0, -10.0, -16.0);
        if (plot.width() <= 0.0 || plot.height() <= 0.0)
            return;

        double maxMs = 33.4;
        for (const perf::FrameSample& sample : m_frames)
            maxMs = std::max(maxMs, sample.totalMs);
        maxMs = std::max(1.0, maxMs * 1.1);

        painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        painter.setPen(muted);
        painter.drawText(QRectF(10.0, 2.0, plot.width(), 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("帧耗时 · 最近 %1 帧")
                             .arg(m_frames.size()));

        const std::array<double, 3> gridValues = {16.7, 33.4, maxMs};
        for (double value : gridValues)
        {
            if (value > maxMs)
                continue;
            const double y = plot.bottom() - value / maxMs * plot.height();
            painter.setPen(QPen(grid, 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(muted);
            painter.drawText(QRectF(plot.right() - 54.0, y - 14.0, 54.0, 12.0),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(value, 'f', 0)
                                 + QStringLiteral(" ms"));
        }

        const int count = m_frames.size();
        if (count == 0)
        {
            painter.setPen(muted);
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待预览帧…"));
            return;
        }

        const double step =
            std::max(1.0, plot.width() / double(perf::kRingCapacity));
        const double barWidth = std::max(1.0, step - 1.0);
        for (int i = 0; i < count; ++i)
        {
            const perf::FrameSample& sample =
                m_frames.at(m_frames.size() - count + i);
            const double height =
                std::clamp(sample.totalMs / maxMs, 0.0, 1.0) * plot.height();
            const double x = plot.right() - (count - i) * step;
            QColor color(90, 220, 140);
            if (sample.totalMs > 33.4)
                color = QColor(235, 90, 80);
            else if (sample.totalMs > 16.7)
                color = QColor(230, 200, 80);
            painter.fillRect(QRectF(x, plot.bottom() - height, barWidth, height),
                             color);
        }

        painter.setPen(QPen(mid, 1.0));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    }

private:
    QVector<perf::FrameSample> m_frames;
};

class FrameWaterfall : public QWidget
{
public:
    explicit FrameWaterfall(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("perfWaterfall"));
        setMinimumHeight(58);
    }

    void setSample(const perf::FrameSample& sample, bool valid)
    {
        m_sample = sample;
        m_valid = valid;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::Text);
        QColor muted = text;
        muted.setAlpha(165);
        painter.fillRect(rect(), base);
        painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        if (!m_valid)
        {
            painter.setPen(muted);
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("最近一帧：暂无样本"));
            return;
        }

        // Self time keeps the stacked bar from double-counting a child scope
        // inside its parent; the frame total is the width denominator.
        const double total = std::max(0.001, m_sample.totalMs);

        const QRectF bar = QRectF(rect()).adjusted(10.0, 8.0, -10.0, -22.0);
        double x = bar.left();
        for (int i = 1; i < perf::kCategoryCount; ++i)
        {
            const double value = m_sample.categorySelfMs[size_t(i)];
            if (value <= 0.0)
                continue;
            const double width = std::max(1.0, value / total * bar.width());
            const QRectF part(x, bar.top(), width, bar.height());
            const QColor color = categoryColor(perf::Category(i));
            painter.fillRect(part, color);
            painter.setPen(QPen(base, 1.0));
            painter.drawRect(part);
            if (width >= 46.0)
            {
                painter.setPen(QColor(20, 20, 22));
                painter.drawText(part.adjusted(3.0, 0.0, -3.0, 0.0),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 perf::categoryLabel(perf::Category(i)));
            }
            x += width;
        }

        painter.setPen(muted);
        painter.drawText(QRectF(bar.left(), bar.bottom() + 2.0, bar.width(), 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("第 %1 帧 · %2")
                             .arg(m_sample.index)
                             .arg(msText(m_sample.totalMs)));
    }

private:
    perf::FrameSample m_sample;
    bool m_valid = false;
};

// Flame graph drawn directly with QPainter: one row per call depth, child width
// proportional to its inclusive share of the parent.  Clicking a block drills
// into that subtree; clicking the background or pressing Esc restores the root.
class FlameGraph : public QWidget
{
public:
    explicit FlameGraph(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("perfFlameGraph"));
        setMinimumHeight(190);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setTree(const perf::CallNode& tree)
    {
        m_tree = tree;
        // m_items holds pointers into the old tree; drop them before the next
        // paint so a mouse event cannot dereference freed nodes.
        m_items.clear();
        if (m_tree.name.isEmpty())
            m_focus.clear();
        else if (!pathExists(m_tree, m_focus))
            m_focus.clear();
        m_hovered = -1;
        update();
    }

    bool popFocus()
    {
        if (m_focus.isEmpty())
            return false;
        m_focus.removeLast();
        m_hovered = -1;
        update();
        return true;
    }

    void resetFocus()
    {
        if (m_focus.isEmpty())
            return;
        m_focus.clear();
        m_hovered = -1;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::Text);
        QColor muted = text;
        muted.setAlpha(165);
        painter.fillRect(rect(), base);
        painter.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        const perf::CallNode* root = nodeAtPath(m_focus);
        if (root == nullptr || root->name.isEmpty())
        {
            painter.setPen(muted);
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("暂无调用树样本；打开预览或录制后再看。"));
            return;
        }

        const QString breadcrumb =
            m_focus.isEmpty()
                ? treeNodeLabel(*root)
                : QStringLiteral("根 / ") + treeNodeLabel(*root);
        painter.setPen(muted);
        painter.drawText(QRectF(8.0, 2.0, width() - 16.0, 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("火焰图 · %1（点击下钻，空白处返回）")
                             .arg(breadcrumb));

        const QRectF plot =
            QRectF(rect()).adjusted(8.0, 20.0, -8.0, -6.0);
        if (plot.width() <= 0.0 || plot.height() <= 0.0)
            return;

        m_items.clear();
        int levels = 0;
        depthOf(*root, 0, &levels);
        const double rowHeight =
            std::clamp(plot.height() / std::max(1, levels), 9.0, 22.0);
        layoutNode(*root, 0, plot.left(), plot.width(), plot.top(), rowHeight,
                   QVector<int>());

        for (int i = 0; i < m_items.size(); ++i)
        {
            const Item& item = m_items.at(i);
            const QColor color = categoryColor(item.node->category);
            painter.fillRect(item.rect, color);
            painter.setPen(QPen(base, 1.0));
            painter.drawRect(item.rect);

            if (i == m_hovered)
            {
                QColor hover = text;
                hover.setAlpha(230);
                painter.setPen(QPen(hover, 2.0));
                painter.drawRect(item.rect.adjusted(1.0, 1.0, -1.0, -1.0));
            }

            if (item.rect.width() < 30.0 || item.rect.height() < 10.0)
                continue;
            painter.setPen(isDarkColor(color) ? QColor(235, 238, 240)
                                              : QColor(20, 20, 22));
            const QString text =
                painter.fontMetrics().elidedText(
                    treeNodeLabel(*item.node), Qt::ElideRight,
                    int(item.rect.width()) - 6);
            painter.drawText(item.rect.adjusted(3.0, 0.0, -3.0, 0.0),
                             Qt::AlignVCenter | Qt::AlignLeft, text);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int index = itemAt(event->position());
        if (index != m_hovered)
        {
            m_hovered = index;
            update();
            if (index >= 0)
            {
                const Item& item = m_items.at(index);
                const perf::CallNode* root = nodeAtPath(m_focus);
                const double total =
                    root != nullptr ? std::max(1e-6, root->inclusiveMs) : 1.0;
                const QString text =
                    QStringLiteral("%1\n阶段：%2\n含子：%3 ms（%4%）\n独占：%5 ms\n调用：%6 次")
                        .arg(treeNodeLabel(*item.node),
                             perf::categoryLabel(item.node->category),
                             QString::number(item.node->inclusiveMs, 'f', 2),
                             QString::number(item.node->inclusiveMs / total
                                                 * 100.0,
                                             'f', 1),
                             QString::number(item.node->exclusiveMs, 'f', 2))
                        .arg(item.node->calls);
                QToolTip::showText(event->globalPosition().toPoint(), text,
                                   this);
            }
            else
            {
                QToolTip::hideText();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = -1;
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }
        const int index = itemAt(event->position());
        if (index < 0)
        {
            resetFocus();
            return;
        }
        m_focus = m_items.at(index).path;
        m_hovered = -1;
        QToolTip::hideText();
        update();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape)
        {
            resetFocus();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    struct Item
    {
        QRectF rect;
        QVector<int> path;
        const perf::CallNode* node = nullptr;
    };

    const perf::CallNode* nodeAtPath(const QVector<int>& path) const
    {
        const perf::CallNode* node = &m_tree;
        for (int index : path)
        {
            if (index < 0 || index >= node->children.size())
                return nullptr;
            node = &node->children.at(index);
        }
        return node;
    }

    static bool pathExists(const perf::CallNode& root, const QVector<int>& path)
    {
        const perf::CallNode* node = &root;
        for (int index : path)
        {
            if (index < 0 || index >= node->children.size())
                return false;
            node = &node->children.at(index);
        }
        return true;
    }

    static void depthOf(const perf::CallNode& node, int depth, int* levels)
    {
        *levels = std::max(*levels, depth + 1);
        for (const perf::CallNode& child : node.children)
            depthOf(child, depth + 1, levels);
    }

    void layoutNode(const perf::CallNode& node, int depth, double x,
                    double width, double top, double rowHeight,
                    const QVector<int>& path)
    {
        Item item;
        item.rect = QRectF(x, top + depth * rowHeight, width, rowHeight);
        item.path = path;
        item.node = &node;
        m_items.append(item);

        if (node.children.isEmpty())
            return;
        double childSum = 0.0;
        for (const perf::CallNode& child : node.children)
            childSum += child.inclusiveMs;
        const double denominator = std::max(node.inclusiveMs, childSum);
        if (denominator <= 0.0)
            return;
        double childX = x;
        for (int i = 0; i < node.children.size(); ++i)
        {
            const perf::CallNode& child = node.children.at(i);
            const double share =
                std::clamp(child.inclusiveMs / denominator, 0.0, 1.0);
            const double childWidth = width * share;
            if (childWidth <= 0.0)
                continue;
            QVector<int> childPath = path;
            childPath.append(i);
            layoutNode(child, depth + 1, childX, childWidth, top, rowHeight,
                       childPath);
            childX += childWidth;
        }
    }

    int itemAt(const QPointF& position) const
    {
        for (int i = 0; i < m_items.size(); ++i)
            if (m_items.at(i).rect.contains(position))
                return i;
        return -1;
    }

    perf::CallNode m_tree;
    QVector<Item> m_items;
    QVector<int> m_focus;
    int m_hovered = -1;
};

PerfMonitorWindow::PerfMonitorWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_profiler(perf::FrameProfiler::instance())
    , m_systemProvider(perf::SystemStatsProvider::create())
{
    setObjectName(QStringLiteral("perfMonitorWindow"));
    setWindowTitle(QStringLiteral("性能监视"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(960, 820);
    setMinimumSize(760, 600);

    buildUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &PerfMonitorWindow::refreshNow);

    refreshNow();
}

PerfMonitorWindow::~PerfMonitorWindow()
{
    if (m_captureHeld)
        m_profiler.setCaptureEnabled(false);
    if (m_profiler.isRecording())
        m_profiler.stopRecording();
}

void PerfMonitorWindow::buildUi()
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // The window itself only hosts a scroll area: the content column below is
    // taller than most screens once the recording report and the advanced area
    // are open, so it scrolls instead of squeezing every section.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("perfScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(scroll);

    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("perfScrollContent"));
    // Grow with the layout's natural height so the sections keep their
    // minimum sizes; the window scrolls instead of squeezing them.
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    scroll->setWidget(body);

    // Children are still created with `this` as parent and reparented by the
    // layout below.
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("perfSummaryLabel"));
    m_summaryLabel->setFont(mono);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setToolTip(
        QStringLiteral("掉帧 = 帧耗时超过 33.4 ms（低于 30 fps）；"
                       "样本为最近 300 帧。"));
    root->addWidget(m_summaryLabel);

    m_sparkline = new FrameSparkline(this);
    root->addWidget(m_sparkline);

    auto* middle = new QHBoxLayout();
    middle->setSpacing(6);

    auto* stageBox = new QGroupBox(QStringLiteral("阶段耗时（含子 / 自身）"),
                                   this);
    stageBox->setToolTip(
        QStringLiteral("占比以整帧总时长为分母，不是以重叠阶段合计为分母。\n"
                       "「含子」包含子阶段（如缓存查找包含缓存解码），"
                       "「自身」= 含子 - 子阶段，默认按自身耗时排序。"));
    auto* stageLayout = new QVBoxLayout(stageBox);
    stageLayout->setContentsMargins(6, 6, 6, 6);
    m_stageTable = new QTableWidget(0, 5, stageBox);
    m_stageTable->setObjectName(QStringLiteral("perfStageTable"));
    m_stageTable->setHorizontalHeaderLabels(
        {QStringLiteral("阶段"), QStringLiteral("含子"),
         QStringLiteral("自身"), QStringLiteral("含子占比"),
         QStringLiteral("自身占比")});
    m_stageTable->verticalHeader()->hide();
    m_stageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stageTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_stageTable->setShowGrid(false);
    m_stageTable->setFont(mono);
    m_stageTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column)
        m_stageTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    m_stageTable->setMinimumHeight(150);
    stageLayout->addWidget(m_stageTable);
    auto* stageHint = new QLabel(
        QStringLiteral("提示：父阶段包含子阶段（如缓存查找含缓存解码），"
                       "请按「自身」列比较；占比以整帧总时长为分母。"),
        stageBox);
    stageHint->setObjectName(QStringLiteral("perfStageHint"));
    stageHint->setFont(mono);
    stageHint->setWordWrap(true);
    stageLayout->addWidget(stageHint);
    middle->addWidget(stageBox, 3);

    auto* side = new QVBoxLayout();
    side->setSpacing(6);

    auto* cacheBox = new QGroupBox(QStringLiteral("缓存"), this);
    auto* cacheLayout = new QVBoxLayout(cacheBox);
    cacheLayout->setContentsMargins(6, 6, 6, 6);
    m_cacheLabel = new QLabel(cacheBox);
    m_cacheLabel->setObjectName(QStringLiteral("perfCacheLabel"));
    m_cacheLabel->setFont(mono);
    m_cacheLabel->setWordWrap(true);
    m_cacheLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cacheLayout->addWidget(m_cacheLabel);
    side->addWidget(cacheBox);

    auto* systemBox = new QGroupBox(QStringLiteral("系统"), this);
    auto* systemLayout = new QVBoxLayout(systemBox);
    systemLayout->setContentsMargins(6, 6, 6, 6);
    m_systemLabel = new QLabel(systemBox);
    m_systemLabel->setObjectName(QStringLiteral("perfSystemLabel"));
    m_systemLabel->setFont(mono);
    m_systemLabel->setWordWrap(true);
    m_systemLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    systemLayout->addWidget(m_systemLabel);
    side->addWidget(systemBox);
    side->addStretch(1);
    middle->addLayout(side, 2);

    root->addLayout(middle);

    auto* waterfallBox =
        new QGroupBox(QStringLiteral("最近一帧阶段瀑布（自身时间）"), this);
    auto* waterfallLayout = new QVBoxLayout(waterfallBox);
    waterfallLayout->setContentsMargins(6, 6, 6, 6);
    m_waterfall = new FrameWaterfall(waterfallBox);
    waterfallLayout->addWidget(m_waterfall);
    root->addWidget(waterfallBox);

    auto* recordBox = new QGroupBox(QStringLiteral("录制"), this);
    auto* recordLayout = new QVBoxLayout(recordBox);
    recordLayout->setContentsMargins(6, 6, 6, 6);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    auto* durationLabel = new QLabel(QStringLiteral("时长"), recordBox);
    controls->addWidget(durationLabel);
    m_durationCombo = new QComboBox(recordBox);
    m_durationCombo->setObjectName(QStringLiteral("perfRecordDuration"));
    m_durationCombo->addItem(QStringLiteral("5 秒"), 5);
    m_durationCombo->addItem(QStringLiteral("10 秒"), 10);
    m_durationCombo->addItem(QStringLiteral("30 秒"), 30);
    m_durationCombo->setCurrentIndex(1);
    controls->addWidget(m_durationCombo);

    m_recordButton = new QPushButton(QStringLiteral("开始录制"), recordBox);
    m_recordButton->setObjectName(QStringLiteral("perfRecordButton"));
    connect(m_recordButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::toggleRecording);
    controls->addWidget(m_recordButton);

    m_recordStatus = new QLabel(QStringLiteral("选择时长后开始录制"), recordBox);
    m_recordStatus->setObjectName(QStringLiteral("perfRecordStatus"));
    m_recordStatus->setFont(mono);
    controls->addWidget(m_recordStatus, 1);
    recordLayout->addLayout(controls);

    m_report = new QPlainTextEdit(recordBox);
    m_report->setObjectName(QStringLiteral("perfReport"));
    m_report->setReadOnly(true);
    m_report->setFont(mono);
    m_report->setPlaceholderText(
        QStringLiteral("录制结束后这里会显示人话报告；也可以导出 JSON 诊断包。"));
    m_report->setMinimumHeight(180);
    recordLayout->addWidget(m_report);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    m_copyButton = new QPushButton(QStringLiteral("复制报告"), recordBox);
    m_copyButton->setObjectName(QStringLiteral("perfCopyButton"));
    connect(m_copyButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::copyReport);
    actions->addWidget(m_copyButton);
    m_exportButton = new QPushButton(QStringLiteral("导出诊断包"), recordBox);
    m_exportButton->setObjectName(QStringLiteral("perfExportButton"));
    connect(m_exportButton, &QPushButton::clicked, this,
            &PerfMonitorWindow::exportReport);
    actions->addWidget(m_exportButton);
    recordLayout->addLayout(actions);

    root->addWidget(recordBox, 1);

    // The advanced section stays collapsed for normal users: no function names
    // or call trees appear until this button is pressed.
    auto* advancedBar = new QHBoxLayout();
    advancedBar->addStretch(1);
    m_advancedButton = new QPushButton(QStringLiteral("高级 ▸"), this);
    m_advancedButton->setObjectName(QStringLiteral("perfAdvancedButton"));
    m_advancedButton->setToolTip(
        QStringLiteral("阶段火焰图与调用树；排查具体函数与嵌套阶段时再打开。"));
    connect(m_advancedButton, &QPushButton::clicked, this,
            [this]() { setAdvancedVisible(!m_advancedOpen); });
    advancedBar->addWidget(m_advancedButton);
    root->addLayout(advancedBar);

    buildAdvancedArea(root);

    // Palette-derived chrome only: borders come from QPalette::Mid, grid lines
    // use a slightly darker Mid and backgrounds are left to the native style,
    // so the window matches a light or a dark main UI instead of forcing its
    // own dark theme.
    const QColor border = palette().color(QPalette::Mid);
    const QColor grid = border.darker(120);
    setStyleSheet(
        QStringLiteral("QWidget { border-radius: 0px; }"
                       "QGroupBox { border: 1px solid %1; margin-top: 9px; }"
                       "QGroupBox::title { subcontrol-origin: margin;"
                       " left: 8px; padding: 0px 3px; }"
                       "QTableWidget { border: 1px solid %1;"
                       " gridline-color: %2; }"
                       "QHeaderView::section { border: 0px;"
                       " border-right: 1px solid %2;"
                       " border-bottom: 1px solid %2; padding: 3px; }"
                       "QPushButton, QComboBox { border: 1px solid %1;"
                       " border-radius: 0px; padding: 3px 10px; }"
                       "QLineEdit { border: 1px solid %1;"
                       " border-radius: 0px; padding: 2px 5px; }"
                       "QPlainTextEdit { border: 1px solid %1;"
                       " border-radius: 0px; }")
            .arg(border.name(), grid.name()));
}

void PerfMonitorWindow::buildAdvancedArea(QVBoxLayout* root)
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto* panel = new QGroupBox(QStringLiteral("高级：阶段火焰图与调用树"), this);
    panel->setObjectName(QStringLiteral("perfAdvancedPanel"));
    panel->setVisible(false);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(6);
    controls->addWidget(new QLabel(QStringLiteral("数据源"), panel));
    m_treeSource = new QComboBox(panel);
    m_treeSource->setObjectName(QStringLiteral("perfTreeSource"));
    m_treeSource->addItem(QStringLiteral("最近一帧"));
    m_treeSource->addItem(QStringLiteral("录制聚合"));
    connect(m_treeSource, &QComboBox::currentIndexChanged, this,
            [this]() {
                updateAdvanced();
                if (m_flame)
                    m_flame->resetFocus();
            });
    controls->addWidget(m_treeSource);

    controls->addWidget(new QLabel(QStringLiteral("搜索"), panel));
    m_treeSearch = new QLineEdit(panel);
    m_treeSearch->setObjectName(QStringLiteral("perfCallSearch"));
    m_treeSearch->setPlaceholderText(QStringLiteral("按名称筛选"));
    m_treeSearch->setClearButtonEnabled(true);
    m_treeSearch->setMaximumWidth(180);
    connect(m_treeSearch, &QLineEdit::textChanged, this, [this]() {
        if (!m_callTable)
            return;
        const QString needle = m_treeSearch->text().trimmed();
        for (int row = 0; row < m_callTable->rowCount(); ++row)
        {
            const QTableWidgetItem* item = m_callTable->item(row, 0);
            const bool matches =
                needle.isEmpty()
                || (item && item->text().contains(needle, Qt::CaseInsensitive));
            m_callTable->setRowHidden(row, !matches);
        }
    });
    controls->addWidget(m_treeSearch);

    m_treeStatus = new QLabel(QStringLiteral("暂无调用树"), panel);
    m_treeStatus->setObjectName(QStringLiteral("perfTreeStatus"));
    m_treeStatus->setFont(mono);
    controls->addWidget(m_treeStatus, 1);

    m_treeBackButton = new QPushButton(QStringLiteral("返回"), panel);
    m_treeBackButton->setObjectName(QStringLiteral("perfTreeBackButton"));
    connect(m_treeBackButton, &QPushButton::clicked, this, [this]() {
        if (m_flame)
            m_flame->popFocus();
    });
    controls->addWidget(m_treeBackButton);

    m_treeExportButton =
        new QPushButton(QStringLiteral("导出调用树 JSON"), panel);
    m_treeExportButton->setObjectName(QStringLiteral("perfTreeExportButton"));
    connect(m_treeExportButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出调用树"),
            QStringLiteral("pvforge-calltree.json"),
            QStringLiteral("JSON 文件 (*.json)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!exportCallTree(path, &error) && !error.isEmpty())
            m_treeStatus->setText(QStringLiteral("导出失败：%1").arg(error));
        else
            m_treeStatus->setText(QStringLiteral("调用树已导出：%1").arg(path));
    });
    controls->addWidget(m_treeExportButton);
    layout->addLayout(controls);

    m_flame = new FlameGraph(panel);
    layout->addWidget(m_flame, 1);

    auto* tableLabel = new QLabel(
        QStringLiteral("调用树（默认按含子时间降序，点表头切换排序）"), panel);
    tableLabel->setFont(mono);
    layout->addWidget(tableLabel);

    m_callTable = new QTableWidget(0, 5, panel);
    m_callTable->setObjectName(QStringLiteral("perfCallTable"));
    m_callTable->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("含子"),
         QStringLiteral("独占"), QStringLiteral("占比"),
         QStringLiteral("调用")});
    m_callTable->verticalHeader()->hide();
    m_callTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_callTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_callTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_callTable->setShowGrid(false);
    m_callTable->setFont(mono);
    m_callTable->setSortingEnabled(true);
    m_callTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column)
        m_callTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    m_callTable->setMinimumHeight(170);
    layout->addWidget(m_callTable, 1);

    root->addWidget(panel);
    m_advancedPanel = panel;
}

void PerfMonitorWindow::setCacheRoot(const QString& root)
{
    if (m_cacheRoot == root)
        return;
    m_cacheRoot = root;
    refreshNow();
}

void PerfMonitorWindow::setAdvancedVisible(bool visible)
{
    m_advancedOpen = visible;
    if (m_advancedPanel)
        m_advancedPanel->setVisible(visible);
    if (m_advancedButton)
    {
        m_advancedButton->setText(visible ? QStringLiteral("高级 ▾")
                                          : QStringLiteral("高级 ▸"));
        m_advancedButton->setToolTip(
            visible ? QStringLiteral("收起火焰图与调用树")
                    : QStringLiteral("阶段火焰图与调用树；排查具体函数与嵌套阶段时再打开。"));
    }
    if (visible)
        updateAdvanced();
    // The scroll area takes care of the extra height: the window keeps its
    // size and the user scrolls to the flame graph and call table.
}

perf::CallNode PerfMonitorWindow::selectedTree() const
{
    if (m_treeSource && m_treeSource->currentIndex() == 1)
        return m_profiler.recordedReport().callTree;
    return m_snapshot.lastFrameTree;
}

void PerfMonitorWindow::updateAdvanced()
{
    if (!m_advancedOpen || m_advancedPanel == nullptr || m_flame == nullptr)
        return;

    const perf::RecordingReport report = m_profiler.recordedReport();
    if (m_treeSource)
    {
        m_treeSource->setItemText(
            1, report.valid ? QStringLiteral("录制聚合")
                            : QStringLiteral("录制聚合（暂无）"));
        if (!report.valid && m_treeSource->currentIndex() == 1)
            m_treeSource->setCurrentIndex(0);
    }

    const perf::CallNode tree = selectedTree();
    QString key = QStringLiteral("%1|%2|%3|%4")
                      .arg(tree.name)
                      .arg(tree.calls)
                      .arg(qRound64(tree.inclusiveMs * 100.0))
                      .arg(tree.children.size());
    if (m_treeSource && m_treeSource->currentIndex() == 1)
    {
        key += QStringLiteral("|r%1|%2")
                   .arg(report.frameCount)
                   .arg(qRound64(report.elapsedSeconds * 10.0));
    }
    else if (!m_snapshot.frames.isEmpty())
    {
        key += QStringLiteral("|f%1").arg(m_snapshot.frames.last().index);
    }
    if (key == m_lastTreeKey)
        return;
    m_lastTreeKey = key;

    m_flame->setTree(tree);
    updateCallTable(tree);

    int nodes = 0;
    std::function<void(const perf::CallNode&)> count =
        [&nodes, &count](const perf::CallNode& node) {
            ++nodes;
            for (const perf::CallNode& child : node.children)
                count(child);
        };
    if (!tree.name.isEmpty())
        count(tree);
    m_treeStatus->setText(
        tree.name.isEmpty()
            ? QStringLiteral("暂无调用树")
            : QStringLiteral("%1 · %2 个节点 · %3")
                  .arg(tree.name)
                  .arg(nodes)
                  .arg(m_treeSource && m_treeSource->currentIndex() == 1
                           ? QStringLiteral("录制聚合")
                           : QStringLiteral("最近一帧")));
    if (m_treeExportButton)
        m_treeExportButton->setEnabled(!tree.name.isEmpty());
}

void PerfMonitorWindow::updateCallTable(const perf::CallNode& tree)
{
    if (m_callTable == nullptr)
        return;
    m_callTable->setSortingEnabled(false);
    m_callTable->clearContents();
    m_callTable->setRowCount(0);
    if (tree.name.isEmpty())
    {
        m_callTable->setSortingEnabled(true);
        return;
    }

    const double rootMs = std::max(1e-6, tree.inclusiveMs);
    int row = 0;
    std::function<void(const perf::CallNode&, int)> append =
        [&](const perf::CallNode& node, int depth) {
            m_callTable->insertRow(row);
            const QString indent =
                depth > 0 ? QString(depth * 2, QLatin1Char(' ')) : QString();
            auto* name = new QTableWidgetItem(indent + treeNodeLabel(node));
            name->setToolTip(perf::categoryLabel(node.category));
            // categoryColor is tuned for a dark chart background; on a light
            // palette the lighter variant washes out, so pick the direction
            // that contrasts with the table base.
            const QColor category = categoryColor(node.category);
            name->setForeground(
                isDarkColor(palette().color(QPalette::Base))
                    ? category.lighter(150)
                    : category.darker(140));

            const auto numberItem = [](double value) {
                auto* item = new NumberTableItem(
                    value, QString::number(value, 'f', 2));
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                return item;
            };
            auto* inclusive = numberItem(node.inclusiveMs);
            auto* exclusive = numberItem(node.exclusiveMs);
            auto* share = new NumberTableItem(
                node.inclusiveMs / rootMs,
                QString::number(node.inclusiveMs / rootMs * 100.0, 'f', 1)
                    + QStringLiteral("%"));
            share->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto* calls = new NumberTableItem(double(node.calls),
                                              QString::number(node.calls));
            calls->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

            m_callTable->setItem(row, 0, name);
            m_callTable->setItem(row, 1, inclusive);
            m_callTable->setItem(row, 2, exclusive);
            m_callTable->setItem(row, 3, share);
            m_callTable->setItem(row, 4, calls);
            ++row;
            for (const perf::CallNode& child : node.children)
                append(child, depth + 1);
        };
    append(tree, 0);

    m_callTable->setSortingEnabled(true);
    m_callTable->sortItems(1, Qt::DescendingOrder);

    if (m_treeSearch)
    {
        const QString needle = m_treeSearch->text().trimmed();
        for (int i = 0; i < m_callTable->rowCount(); ++i)
        {
            const QTableWidgetItem* item = m_callTable->item(i, 0);
            m_callTable->setRowHidden(
                i, !needle.isEmpty()
                       && (!item
                           || !item->text().contains(needle,
                                                     Qt::CaseInsensitive)));
        }
    }
}

bool PerfMonitorWindow::exportCallTree(const QString& path, QString* error)
{
    const perf::CallNode tree = selectedTree();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data =
        QJsonDocument(perf::FrameProfiler::callTreeToJson(tree))
            .toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

void PerfMonitorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_captureHeld)
    {
        m_profiler.setCaptureEnabled(true);
        m_captureHeld = true;
    }
    m_timer->start();
    refreshNow();
    updateReportText();
    raise();
    activateWindow();
}

void PerfMonitorWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    if (m_captureHeld)
    {
        m_profiler.setCaptureEnabled(false);
        m_captureHeld = false;
    }
    if (m_profiler.isRecording())
        m_profiler.stopRecording();
    QWidget::hideEvent(event);
}

void PerfMonitorWindow::closeEvent(QCloseEvent* event)
{
    emit closed();
    QWidget::closeEvent(event);
}

bool PerfMonitorWindow::isRecording() const
{
    return m_profiler.isRecording();
}

QString PerfMonitorWindow::currentReportText() const
{
    return m_report ? m_report->toPlainText() : QString();
}

void PerfMonitorWindow::refreshNow()
{
    m_snapshot = m_profiler.snapshot();
    if (m_systemProvider)
        m_systemStats = m_systemProvider->sample(m_cacheRoot);

    updateOverview();

    const bool recording = m_profiler.isRecording();
    if (m_wasRecording && !recording)
    {
        updateReportText();
        // After a recording the aggregate tree is the interesting one.
        if (m_treeSource && m_advancedOpen)
            m_treeSource->setCurrentIndex(1);
        m_lastTreeKey.clear();
    }
    m_wasRecording = recording;
    updateRecordingUi();
    updateAdvanced();
}

void PerfMonitorWindow::updateOverview()
{
    const perf::Summary& summary = m_snapshot.summary;
    const int sampleCount = summary.count;

    m_summaryLabel->setText(
        QStringLiteral(
            "FPS %1    p50 %2    p95 %3    p99 %4    掉帧 %5    样本 %6/%7")
            .arg(QString::number(summary.fps, 'f', 1), msText(summary.p50Ms),
                 msText(summary.p95Ms), msText(summary.p99Ms))
            .arg(summary.droppedFrames)
            .arg(sampleCount)
            .arg(perf::kRingCapacity));

    m_sparkline->setFrames(m_snapshot.frames);

    // Stage table: skip the whole-frame pseudo category, keep only measured
    // stages and sort by total time.
    struct Row
    {
        perf::Category category;
        double totalMs;
        double selfMs;
        double share;
        double selfShare;
    };
    QVector<Row> rows;
    for (int i = 1; i < perf::kCategoryCount; ++i)
    {
        const double total = summary.categoryTotalMs[size_t(i)];
        const double self = summary.categorySelfMs[size_t(i)];
        if (total <= 0.0 && self <= 0.0)
            continue;
        rows.append(Row{perf::Category(i), total, self,
                        summary.categoryShare[size_t(i)],
                        summary.categorySelfShare[size_t(i)]});
    }
    // Self time first: the lookup parent wraps decode, so ranking by inclusive
    // would keep blaming lookup for time actually spent decoding.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.selfMs > b.selfMs;
    });

    m_stageTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
    {
        const Row& stage = rows.at(row);
        auto* name = new QTableWidgetItem(perf::categoryLabel(stage.category));
        const auto averageItem = [sampleCount](double totalMs) {
            auto* item = new QTableWidgetItem(
                sampleCount > 0 ? msText(totalMs / sampleCount)
                                : QStringLiteral("—"));
            item->setData(Qt::UserRole, totalMs / std::max(1, sampleCount));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        auto* inclusive = averageItem(stage.totalMs);
        auto* self = averageItem(stage.selfMs);
        auto* share = new QTableWidgetItem(
            QString::number(stage.share * 100.0, 'f', 1)
            + QStringLiteral("%"));
        share->setData(Qt::UserRole, stage.share);
        share->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* selfShare = new QTableWidgetItem(
            QString::number(stage.selfShare * 100.0, 'f', 1)
            + QStringLiteral("%"));
        selfShare->setData(Qt::UserRole, stage.selfShare);
        selfShare->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        const QString hint =
            QStringLiteral("%1：含子 %2 ms（整帧 %3%），其中子阶段 %4 ms；"
                           "自身 %5 ms（整帧 %6%）。\n"
                           "含子包含子阶段，可与子阶段重叠；自身才是该阶段真正"
                           "占用的时间。")
                .arg(perf::categoryLabel(stage.category))
                .arg(QString::number(stage.totalMs / std::max(1, sampleCount),
                                     'f', 2))
                .arg(QString::number(stage.share * 100.0, 'f', 1))
                .arg(QString::number(
                         std::max(0.0, stage.totalMs - stage.selfMs)
                             / std::max(1, sampleCount),
                         'f', 2))
                .arg(QString::number(stage.selfMs / std::max(1, sampleCount),
                                     'f', 2))
                .arg(QString::number(stage.selfShare * 100.0, 'f', 1));
        for (QTableWidgetItem* item :
             {name, inclusive, self, share, selfShare})
            item->setToolTip(hint);

        m_stageTable->setItem(row, 0, name);
        m_stageTable->setItem(row, 1, inclusive);
        m_stageTable->setItem(row, 2, self);
        m_stageTable->setItem(row, 3, share);
        m_stageTable->setItem(row, 4, selfShare);
    }

    const perf::CacheTotals& cache = m_snapshot.cache;
    const quint64 lookups = cache.hits + cache.misses;
    const double hitRate =
        lookups > 0 ? double(cache.hits) / double(lookups) : 0.0;
    m_cacheLabel->setText(
        QStringLiteral("命中 %1 / 未命中 %2\n命中率 %3\n"
                       "QOI 编码 %4 帧 · WebP 编码 %5 帧\n"
                       "存储队列 %6 · 累计写入 %7")
            .arg(cache.hits)
            .arg(cache.misses)
            .arg(lookups > 0 ? QString::number(hitRate * 100.0, 'f', 1)
                                  + QStringLiteral("%")
                             : QStringLiteral("—"))
            .arg(cache.qoiStores)
            .arg(cache.webpStores)
            .arg(cache.pendingStores)
            .arg(cache.stores));

    if (m_systemStats.supported)
    {
        m_systemLabel->setText(
            QStringLiteral("进程 CPU %1% · 线程 %2\n"
                           "RSS %3\n"
                           "磁盘读 %4 / 写 %5\n"
                           "系统负载 %6 / %7 / %8\n"
                           "缓存占用 %9（%10 帧文件）")
                .arg(QString::number(m_systemStats.processCpuPercent, 'f', 1))
                .arg(m_systemStats.threadCount)
                .arg(formatBytes(m_systemStats.rssBytes),
                     formatBytes(m_systemStats.ioReadBytes),
                     formatBytes(m_systemStats.ioWriteBytes))
                .arg(QString::number(m_systemStats.load1, 'f', 2),
                     QString::number(m_systemStats.load5, 'f', 2),
                     QString::number(m_systemStats.load15, 'f', 2),
                     formatBytes(m_systemStats.cacheDiskBytes))
                .arg(m_systemStats.cacheFileCount));
    }
    else
    {
        m_systemLabel->setText(
            m_systemStats.note.isEmpty()
                ? QStringLiteral("当前平台暂不支持系统参数")
                : m_systemStats.note);
    }

    if (!m_snapshot.frames.isEmpty())
        m_waterfall->setSample(m_snapshot.frames.last(), true);
    else
        m_waterfall->setSample(perf::FrameSample{}, false);
}

void PerfMonitorWindow::updateRecordingUi()
{
    const bool recording = m_profiler.isRecording();
    m_durationCombo->setEnabled(!recording);
    m_recordButton->setText(recording ? QStringLiteral("停止录制")
                                      : QStringLiteral("开始录制"));

    if (recording)
    {
        m_recordStatus->setText(
            QStringLiteral("录制中 %1 / %2 秒")
                .arg(QString::number(m_profiler.recordingElapsedSeconds(), 'f',
                                     1))
                .arg(m_profiler.recordingRemainingSeconds()
                             + m_profiler.recordingElapsedSeconds(),
                     'f', 1));
        return;
    }

    const perf::RecordingReport report = m_profiler.recordedReport();
    if (report.valid)
    {
        m_recordStatus->setText(
            QStringLiteral("录制完成：%1 帧 · %2 秒")
                .arg(report.frameCount)
                .arg(QString::number(report.elapsedSeconds, 'f', 1)));
    }
    else
    {
        m_recordStatus->setText(QStringLiteral("选择时长后开始录制"));
    }
}

void PerfMonitorWindow::updateReportText()
{
    const perf::RecordingReport report = m_profiler.recordedReport();
    if (!report.valid)
        return;
    m_report->setPlainText(report.text);
}

void PerfMonitorWindow::toggleRecording()
{
    if (m_profiler.isRecording())
    {
        m_profiler.stopRecording();
        updateReportText();
        m_wasRecording = false;
        m_lastTreeKey.clear();
        if (m_treeSource && m_advancedOpen)
            m_treeSource->setCurrentIndex(1);
        updateRecordingUi();
        refreshNow();
        return;
    }

    const int seconds = m_durationCombo->currentData().toInt();
    if (!m_profiler.startRecording(seconds))
        return;
    m_report->clear();
    m_wasRecording = true;
    updateRecordingUi();
    refreshNow();
}

void PerfMonitorWindow::copyReport()
{
    QString text = m_report->toPlainText();
    if (text.isEmpty())
        text = m_profiler.recordedReportText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
}

bool PerfMonitorWindow::exportDiagnostics(const QString& path, QString* error)
{
    QJsonObject root = m_profiler.diagnosticsJson();
    root.insert(QStringLiteral("system"), systemStatsToJson(m_systemStats));
    root.insert(QStringLiteral("platform"), QSysInfo::productType());
    root.insert(QStringLiteral("generatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

void PerfMonitorWindow::exportReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出诊断包"),
        QStringLiteral("pvforge-perf.json"),
        QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!exportDiagnostics(path, &error) && !error.isEmpty())
        m_recordStatus->setText(QStringLiteral("导出失败：%1").arg(error));
    else
        m_recordStatus->setText(QStringLiteral("诊断包已导出：%1").arg(path));
}

} // namespace pvf
