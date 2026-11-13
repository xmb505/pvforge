#include "next/timeline/TimelineWidget.h"

#include "next/core/Document.h"

#include <QFont>
#include <QFontMetrics>
#include <QScrollBar>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

namespace pvf::next {

TimelineWidget::TimelineWidget(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumSize(240, 140);

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setObjectName(QStringLiteral("timelineScrollBar"));
    m_hScroll->setGeometry(0, height() - kScrollBarHeight, width(),
                           kScrollBarHeight);
    m_hScroll->setRange(0, 0);
    connect(m_hScroll, &QScrollBar::valueChanged, this,
            &TimelineWidget::onScrollBarValueChanged);

    m_vScroll = new QScrollBar(Qt::Vertical, this);
    m_vScroll->setObjectName(QStringLiteral("timelineRowScrollBar"));
    m_vScroll->setRange(0, 0);
    m_vScroll->hide();
    connect(m_vScroll, &QScrollBar::valueChanged, this, [this](int value) {
        if (m_rowScroll == value)
            return;
        m_rowScroll = value;
        update();
    });

    if (m_document)
    {
        m_playhead = std::max(0, m_document->playhead());
        connect(m_document, &Document::changed, this, &TimelineWidget::refresh);
        connect(m_document, &Document::selectionChanged, this, [this]() {
            if (m_document->selection() != m_keyframeSelectionUuid)
                clearKeyframeSelection();
            update();
        });
        connect(m_document, &Document::playheadChanged, this,
                &TimelineWidget::onPlayheadChanged);
    }

    updateScrollBar();
}

TimelineWidget::~TimelineWidget() = default;

void TimelineWidget::setParentUuid(const Uuid& parentUuid)
{
    if (m_parentUuid == parentUuid)
        return;

    m_parentUuid = parentUuid;
    if (m_document && !m_parentUuid.isEmpty()
        && !m_document->project().entities.contains(m_parentUuid))
        m_parentUuid.clear();

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    clearKeyframeSelection();
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    // Start the private timeline at its owner instead of frame 0, so children
    // of a parent that starts late are visible immediately.
    double startFrame = 0.0;
    if (m_document && !m_parentUuid.isEmpty())
    {
        const auto owner =
            m_document->project().entities.constFind(m_parentUuid);
        if (owner != m_document->project().entities.constEnd())
            startFrame = std::max(0, owner.value().start);
    }
    m_scrollX = std::max(0, int(std::lround(startFrame * m_pixelsPerFrame)) - 8);
    m_rowScroll = 0;

    updateScrollBar();
    update();
}

void TimelineWidget::setPixelsPerFrame(double pixelsPerFrame)
{
    const double clamped = std::clamp(pixelsPerFrame, 0.25, 64.0);
    if (qFuzzyCompare(clamped, m_pixelsPerFrame))
        return;

    m_pixelsPerFrame = clamped;
    updateScrollBar();
    update();
}

void TimelineWidget::setPlayhead(int frame)
{
    const int clamped = std::max(0, frame);
    if (m_document)
        m_document->setPlayhead(clamped);
    m_playhead = clamped;
    updateScrollBar();
    ensureFrameVisible(clamped);
    update();
}

int TimelineWidget::playhead() const
{
    if (m_document)
        return std::max(0, m_document->playhead());
    return std::max(0, m_playhead);
}

void TimelineWidget::refresh()
{
    // A private timeline whose owner was deleted falls back to the root.
    if (m_document && !m_parentUuid.isEmpty()
        && !m_document->project().entities.contains(m_parentUuid))
    {
        m_parentUuid.clear();
        clearKeyframeSelection();
    }

    updateScrollBar();
    update();
}

int TimelineWidget::xFromFrame(int frame) const
{
    return int(std::lround(frame * m_pixelsPerFrame)) - m_scrollX;
}

int TimelineWidget::frameFromX(int x) const
{
    if (m_pixelsPerFrame <= 0.0)
        return 0;
    return int(std::lround((x + m_scrollX) / m_pixelsPerFrame));
}

QRect TimelineWidget::rulerRect() const
{
    const int top = m_parentUuid.isEmpty() ? 0 : kToolbarHeight;
    return QRect(0, top, width(), kRulerHeight);
}

QRect TimelineWidget::rulerLabelRect(int frame) const
{
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    const QFontMetrics metrics(small);
    const QString text = QString::number(frame);
    const int textWidth = metrics.horizontalAdvance(text) + 4;
    const QRect ruler = rulerRect();
    const int x = xFromFrame(frame) - textWidth / 2;
    return QRect(x, ruler.top() + 14, textWidth, ruler.height() - 14);
}

void TimelineWidget::setRowScroll(int value)
{
    updateScrollBar();
    if (!m_vScroll)
    {
        m_rowScroll = std::max(0, value);
        update();
        return;
    }
    m_vScroll->setValue(std::max(0, value));
}

QRect TimelineWidget::markerLaneRect() const
{
    const QRect ruler = rulerRect();
    return QRect(0, ruler.bottom() + 1, width(), kMarkerLaneHeight);
}

QRect TimelineWidget::breadcrumbRect() const
{
    if (m_parentUuid.isEmpty())
        return QRect();
    return QRect(0, 0, std::min(width(), 440), kToolbarHeight);
}

QRect TimelineWidget::entityBarRect(const Uuid& uuid) const
{
    if (!m_document)
        return QRect();

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
        return QRect();
    const model::Entity& entity = it.value();
    if (entity.parentUuid != m_parentUuid)
        return QRect();

    const int top = rowTop(std::max(0, entity.row));
    const int left = xFromFrame(entity.start);
    const int right = xFromFrame(entity.end);
    const int widthPixels = std::max(3, right - left);
    return QRect(left, top + 3, widthPixels, kRowHeight - 6);
}

QRect TimelineWidget::addButtonRect(const Uuid& uuid) const
{
    const QRect bar = entityBarRect(uuid);
    if (bar.isEmpty() || bar.width() < 24 || bar.height() < 14)
        return QRect();
    return QRect(bar.left() + kEdgeGrabPixels + 2, bar.top() + 1, 11, 10);
}

QVector<Uuid> TimelineWidget::visibleEntities() const
{
    if (!m_document)
        return {};
    return model::childrenOf(m_document->project(), m_parentUuid);
}

int TimelineWidget::rowTop(int row) const
{
    const QRect body = bodyRect();
    return body.bottom() - (row + 1) * kRowHeight + 1 + m_rowScroll;
}

QRect TimelineWidget::bodyRect() const
{
    const int top = markerLaneRect().bottom() + 1;
    const int bottom = height() - kScrollBarHeight;
    return QRect(0, top, width(), std::max(0, bottom - top));
}

QRect TimelineWidget::toolbarRect() const
{
    if (m_parentUuid.isEmpty())
        return QRect();
    return QRect(0, 0, width(), kToolbarHeight);
}

int TimelineWidget::labelFrameStep() const
{
    static constexpr int steps[] = {1,    2,     5,     10,    20,    25,
                                    50,   100,   200,   250,   500,   1000,
                                    2000, 2500,  5000,  10000, 20000, 50000};
    for (int step : steps)
    {
        if (step * m_pixelsPerFrame >= 60.0)
            return step;
    }
    return 50000;
}

void TimelineWidget::updateScrollBar()
{
    if (!m_hScroll)
        return;

    double contentEnd = 0.0;
    if (m_document)
    {
        const model::Project& project = m_document->project();
        for (auto it = project.entities.constBegin();
             it != project.entities.constEnd(); ++it)
            contentEnd = std::max(contentEnd, double(it.value().end));
        for (const auto& marker : project.markers)
            contentEnd = std::max(contentEnd, double(marker.frame));
    }
    contentEnd = std::max(contentEnd, double(playhead() + 1));
    contentEnd += std::max(1.0, width() / m_pixelsPerFrame);

    const int maximum = std::max(
        0, int(std::ceil(contentEnd * m_pixelsPerFrame)) - width());

    QSignalBlocker blocker(m_hScroll);
    m_hScroll->setRange(0, maximum);
    m_hScroll->setPageStep(std::max(1, width()));
    m_hScroll->setSingleStep(
        std::max(1, int(std::lround(m_pixelsPerFrame * 5.0))));
    m_scrollX = std::clamp(m_scrollX, 0, maximum);
    m_hScroll->setValue(m_scrollX);

    int highestRow = -1;
    if (m_document)
    {
        for (const Uuid& uuid : visibleEntities())
        {
            const auto it = m_document->project().entities.constFind(uuid);
            if (it != m_document->project().entities.constEnd())
                highestRow = std::max(highestRow, std::max(0, it.value().row));
        }
    }
    const QRect body = bodyRect();
    const int maximumY =
        std::max(0, (highestRow + 1) * kRowHeight - body.height());
    m_rowScroll = std::clamp(m_rowScroll, 0, maximumY);
    if (m_vScroll)
    {
        QSignalBlocker rowBlocker(m_vScroll);
        m_vScroll->setRange(0, maximumY);
        m_vScroll->setPageStep(std::max(1, body.height()));
        m_vScroll->setSingleStep(kRowHeight);
        m_vScroll->setValue(m_rowScroll);
        m_vScroll->setVisible(maximumY > 0);
        m_vScroll->setGeometry(width() - kScrollBarWidth, body.top(),
                               kScrollBarWidth, body.height());
    }
}

void TimelineWidget::setScrollX(int value)
{
    if (!m_hScroll)
    {
        m_scrollX = std::max(0, value);
        update();
        return;
    }
    m_hScroll->setValue(std::max(0, value));
}

void TimelineWidget::ensureFrameVisible(int frame)
{
    int right = width();
    if (m_vScroll && !m_vScroll->isHidden())
        right -= kScrollBarWidth;
    right = std::max(1, right);

    const int x = xFromFrame(frame);
    if (x >= 0 && x < right)
        return;

    const int target = x < 0
                           ? int(std::lround(frame * m_pixelsPerFrame)) - 2
                           : int(std::lround(frame * m_pixelsPerFrame))
                                 - right + 2;
    setScrollX(target);
}

void TimelineWidget::onPlayheadChanged()
{
    const int frame = playhead();
    updateScrollBar();
    ensureFrameVisible(frame);
    update();
}

void TimelineWidget::onScrollBarValueChanged(int value)
{
    if (m_scrollX == value)
        return;
    m_scrollX = value;
    update();
}

} // namespace pvf::next
