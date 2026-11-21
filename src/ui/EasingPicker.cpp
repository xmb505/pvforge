#include "EasingPicker.h"

#include "EasingChooser.h"

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
constexpr int kCustomHeight = 32;

} // namespace

EasingPicker::EasingPicker(const model::EasingSpec& current, QWidget* parent)
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

    QVector<QVector<model::EasingSpec>> groups(3);
    for (const QString& name : model::easingPresetNames())
    {
        model::EasingSpec spec;
        spec.type = name;
        int group = 0;
        if (name.startsWith(QLatin1String("easeinout")))
            group = 2;
        else if (name.startsWith(QLatin1String("easeout")))
            group = 1;
        groups[group].append(spec);
    }

    const int gridWidth = kColumns * (kTile + kGap) - kGap;
    int y = kMargin;

    auto beginSection = [&](const QString& title) {
        Section section;
        section.title = title;
        section.titleRect = QRect(kMargin, y, gridWidth, kHeader);
        m_sections.append(section);
        y += kHeader;
    };

    auto addGrid = [&](const QVector<model::EasingSpec>& specs) {
        for (int i = 0; i < specs.size(); ++i)
        {
            const int column = i % kColumns;
            const int row = i / kColumns;
            Item item;
            item.rect = QRect(kMargin + column * (kTile + kGap),
                              y + row * (kTile + kGap), kTile, kTile);
            item.type = model::canonicalEasingName(specs[i].type);
            item.spec = specs[i];
            item.tooltip = QStringLiteral("%1\n%2")
                               .arg(easingDisplayName(specs[i]), item.type);
            m_items.append(item);
        }
        const int rows = (specs.size() + kColumns - 1) / kColumns;
        y += rows * (kTile + kGap) + kSectionGap;
    };

    beginSection(QStringLiteral("其他"));
    QVector<model::EasingSpec> basics;
    for (const QString& type :
         {QStringLiteral("linear"), QStringLiteral("instant")})
    {
        model::EasingSpec spec;
        spec.type = type;
        basics.append(spec);
    }
    addGrid(basics);

    const struct
    {
        const char* title;
        int group;
    } sections[] = {
        {"缓入", 0},
        {"缓出", 1},
        {"缓入缓出", 2},
    };
    for (const auto& section : sections)
    {
        beginSection(QString::fromUtf8(section.title));
        addGrid(groups[section.group]);
    }

    // One full-width custom bezier row closes the grid.
    Item custom;
    custom.rect = QRect(kMargin, y, gridWidth, kCustomHeight);
    custom.type = QStringLiteral("bezier");
    custom.spec.type = custom.type;
    custom.tooltip = QStringLiteral("自定义贝塞尔曲线…\n调整两个控制点后应用到当前关键帧");
    custom.custom = true;
    m_items.append(custom);
    y += kCustomHeight + kMargin;

    m_size = QSize(kMargin * 2 + gridWidth, y);
}

void EasingPicker::drawCurve(QPainter& painter, const QRectF& rect,
                             const model::EasingSpec& spec,
                             const QPalette& palette, qreal margin)
{
    const QRectF inner = rect.adjusted(margin, margin, -margin, -margin);
    if (inner.width() <= 0.0 || inner.height() <= 0.0)
        return;

    constexpr int steps = 48;
    double minimum = 0.0;
    double maximum = 1.0;
    double values[steps + 1] = {};
    for (int i = 0; i <= steps; ++i)
    {
        values[i] = model::easedProgress(spec, i / double(steps));
        minimum = std::min(minimum, values[i]);
        maximum = std::max(maximum, values[i]);
    }
    if (maximum - minimum < 1e-6)
        maximum = minimum + 1.0;

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

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(palette.color(QPalette::Highlight), 1.6));
    painter.drawPath(path);
    painter.restore();
}

QPixmap EasingPicker::curvePreview(const model::EasingSpec& spec,
                                   const QPalette& palette, const QSize& size,
                                   int margin, qreal devicePixelRatio)
{
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const QSize pixelSize(qCeil(size.width() * dpr),
                          qCeil(size.height() * dpr));
    QPixmap pixmap(pixelSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    if (size.width() <= 0 || size.height() <= 0)
        return pixmap;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawCurve(painter, QRectF(0, 0, size.width(), size.height()), spec,
              palette, margin);
    return pixmap;
}

int EasingPicker::itemForType(const QString& type) const
{
    const QString canonical = model::canonicalEasingName(type);
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].type == canonical)
            return i;
    return -1;
}

QRect EasingPicker::tileRect(const QString& type) const
{
    const int index = itemForType(type);
    return index < 0 ? QRect() : m_items[index].rect;
}

bool EasingPicker::activate(const QString& type)
{
    const int index = itemForType(type);
    if (index < 0)
        return false;
    if (m_items[index].custom)
        emit customRequested();
    else
        emit picked(m_items[index].spec);
    return true;
}

bool EasingPicker::isCurrent(const Item& item) const
{
    const QString current = model::canonicalEasingName(m_current.type);
    if (item.custom)
        return current == QLatin1String("bezier");
    return item.type == current;
}

void EasingPicker::paintCurve(QPainter& painter, const QRect& rect,
                              const model::EasingSpec& spec) const
{
    // Keep the grid fully vector: rasterising a 36x36 thumbnail here would be
    // resampled on HiDPI displays and turn every curve into a blurry smear.
    drawCurve(painter, QRectF(rect), spec, palette(), 5);
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

    for (int i = 0; i < m_items.size(); ++i)
    {
        const Item& item = m_items[i];
        const bool selected = isCurrent(item);
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
            const QRect curveRect(item.rect.left() + 2, item.rect.top() + 2,
                                  kCustomHeight - 4, kCustomHeight - 4);
            paintCurve(painter, curveRect,
                       model::canonicalEasingName(m_current.type)
                               == QLatin1String("bezier")
                           ? m_current
                           : item.spec);
            painter.setPen(pal.color(QPalette::Text));
            painter.drawText(item.rect.adjusted(kCustomHeight, 0, -8, 0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("自定义贝塞尔曲线…"));
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
    if (m_items[index].custom)
        emit customRequested();
    else
        emit picked(m_items[index].spec);
    event->accept();
}

void EasingPicker::mouseMoveEvent(QMouseEvent* event)
{
    const int index = itemAt(event->position().toPoint());
    if (index == m_hover)
        return;
    m_hover = index;
    if (index >= 0)
        QToolTip::showText(event->globalPosition().toPoint(),
                           m_items[index].tooltip, this,
                           m_items[index].rect.adjusted(-2, -2, 2, 2));
    else
        QToolTip::hideText();
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
