#include "EasingPicker.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

constexpr int kTile = 36;
constexpr int kGap = 6;
constexpr int kColumns = 5;
constexpr int kMargin = 10;
constexpr int kHeader = 18;
constexpr int kSectionGap = 8;

const Easing kEaseIn[] = {
    Easing::EaseInQuad,    Easing::EaseInCubic,   Easing::EaseInQuart,
    Easing::EaseInQuint,   Easing::EaseInSine,    Easing::EaseInExpo,
    Easing::EaseInCirc,    Easing::EaseInElastic, Easing::EaseInBack,
    Easing::EaseInBounce,
};

const Easing kEaseOut[] = {
    Easing::EaseOutQuad,    Easing::EaseOutCubic,   Easing::EaseOutQuart,
    Easing::EaseOutQuint,   Easing::EaseOutSine,    Easing::EaseOutExpo,
    Easing::EaseOutCirc,    Easing::EaseOutElastic, Easing::EaseOutBack,
    Easing::EaseOutBounce,
};

const Easing kEaseInOut[] = {
    Easing::EaseInOutQuad,    Easing::EaseInOutCubic,   Easing::EaseInOutQuart,
    Easing::EaseInOutQuint,   Easing::EaseInOutSine,    Easing::EaseInOutExpo,
    Easing::EaseInOutCirc,    Easing::EaseInOutElastic, Easing::EaseInOutBack,
    Easing::EaseInOutBounce,
};

} // namespace

EasingPicker::EasingPicker(const EasingSpec& current, QWidget* parent)
    : QWidget(parent)
    , m_current(current)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    buildLayout();
    setFixedSize(m_size);
}

void EasingPicker::buildLayout()
{
    m_items.clear();
    m_sections.clear();

    const struct
    {
        const char* title;
        const Easing* values;
        int count;
    } groups[] = {
        {"缓入", kEaseIn, 10},
        {"缓出", kEaseOut, 10},
        {"缓入缓出", kEaseInOut, 10},
    };

    int y = kMargin;

    auto beginSection = [&](const QString& title, int itemCount) {
        Section section;
        section.title = title;
        section.titleRect = QRect(kMargin, y, kColumns * (kTile + kGap) - kGap, kHeader);
        section.firstItem = m_items.size();
        section.itemCount = itemCount;
        m_sections.append(section);
        y += kHeader;
        return section;
    };

    // Other: linear, instant, custom bezier.
    beginSection(QStringLiteral("其他"), 3);
    {
        EasingSpec linear;
        linear.easing = Easing::Linear;
        m_items.append({QRect(kMargin, y, kTile, kTile), linear,
                        easingDisplayName(Easing::Linear), false});
        EasingSpec instant;
        instant.easing = Easing::Instant;
        m_items.append({QRect(kMargin + kTile + kGap, y, kTile, kTile), instant,
                        easingDisplayName(Easing::Instant), false});
        EasingSpec bezier;
        bezier.easing = Easing::Bezier;
        m_items.append({QRect(kMargin + 2 * (kTile + kGap), y, kTile, kTile), bezier,
                        QStringLiteral("自定义贝塞尔…"), true});
    }
    y += kTile + kSectionGap;
    m_sections[0].itemCount = 3;

    for (const auto& group : groups)
    {
        const QString title = QString::fromUtf8(group.title);
        beginSection(title, group.count);
        for (int i = 0; i < group.count; ++i)
        {
            const int column = i % kColumns;
            const int row = i / kColumns;
            const QRect rect(kMargin + column * (kTile + kGap),
                             y + row * (kTile + kGap), kTile, kTile);
            EasingSpec spec;
            spec.easing = group.values[i];
            m_items.append({rect, spec, easingDisplayName(group.values[i]), false});
        }
        const int rows = (group.count + kColumns - 1) / kColumns;
        y += rows * (kTile + kGap) + kSectionGap;
    }

    m_size = QSize(kMargin * 2 + kColumns * (kTile + kGap) - kGap, y + kMargin);
}

void EasingPicker::paintCurve(QPainter& painter, const QRect& rect,
                              const EasingSpec& spec)
{
    const QRectF inner = rect.adjusted(5, 5, -5, -5);
    painter.save();
    painter.setClipRect(inner);

    constexpr int steps = 48;
    double values[steps + 1] = {};
    double minimum = 0.0;
    double maximum = 1.0;
    for (int i = 0; i <= steps; ++i)
    {
        const double t = i / double(steps);
        values[i] = applyEasing(spec, t);
        minimum = std::min(minimum, values[i]);
        maximum = std::max(maximum, values[i]);
    }
    if (maximum - minimum < 1e-6)
        maximum = minimum + 1.0;
    const double padding = (maximum - minimum) * 0.06;
    minimum -= padding;
    maximum += padding;

    QPainterPath path;
    for (int i = 0; i <= steps; ++i)
    {
        const double t = i / double(steps);
        const QPointF point(
            inner.left() + t * inner.width(),
            inner.bottom() - (values[i] - minimum) / (maximum - minimum)
                                 * inner.height());
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1.6));
    painter.drawPath(path);
    painter.restore();
}

void EasingPicker::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Window));

    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    small.setBold(true);
    painter.setFont(small);
    painter.setPen(pal.color(QPalette::PlaceholderText));
    for (const Section& section : m_sections)
        painter.drawText(section.titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         section.title);

    QFont curveFont = font();
    curveFont.setPointSizeF(std::max(7.0, curveFont.pointSizeF() - 1.0));
    for (int i = 0; i < m_items.size(); ++i)
    {
        const Item& item = m_items[i];
        const bool selected =
            item.custom ? (m_current.easing == Easing::Bezier)
                        : (item.spec.easing == m_current.easing);
        if (selected || i == m_hover)
        {
            QColor fill = pal.color(QPalette::Highlight);
            fill.setAlpha(selected ? 50 : 28);
            painter.fillRect(item.rect, fill);
        }

        painter.setPen(QPen(selected ? pal.color(QPalette::Highlight)
                                     : pal.color(QPalette::Mid),
                            selected ? 1.6 : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(item.rect);

        if (item.custom)
        {
            painter.setFont(curveFont);
            painter.setPen(pal.color(QPalette::Text));
            painter.drawText(item.rect.adjusted(1, 1, -1, -1),
                             Qt::AlignCenter, QStringLiteral("自定义"));
        }
        else
        {
            paintCurve(painter, item.rect, item.spec);
        }
    }
}

int EasingPicker::itemAt(const QPoint& pos) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].rect.contains(pos))
            return i;
    return -1;
}

void EasingPicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int index = itemAt(event->position().toPoint());
    if (index < 0)
        return;
    const Item& item = m_items[index];
    if (item.custom)
        emit customRequested();
    else
        emit picked(item.spec);
    event->accept();
}

void EasingPicker::mouseMoveEvent(QMouseEvent* event)
{
    const int index = itemAt(event->position().toPoint());
    if (index == m_hover)
        return;
    m_hover = index;
    if (index >= 0)
    {
        QToolTip::showText(event->globalPosition().toPoint(),
                           m_items[index].tooltip, this,
                           m_items[index].rect.adjusted(-2, -2, 2, 2));
    }
    else
    {
        QToolTip::hideText();
    }
    update();
}

void EasingPicker::leaveEvent(QEvent* event)
{
    m_hover = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

} // namespace pvf
