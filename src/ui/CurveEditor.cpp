#include "CurveEditor.h"

#include "core/Commands.h"
#include "core/History.h"
#include "ui/EasingChooser.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pvf {

namespace {

struct CurveInfo
{
    const char* property;
    const char* label;
    int hue;
};

// Fixed order keeps the legend stable while switching between entities.
const CurveInfo kCurves[] = {
    {"x", "X", 210},
    {"y", "Y", 140},
    {"rotation", "旋转", 45},
    {"scaleX", "缩放 X", 300},
    {"scaleY", "缩放 Y", 325},
    {"opacity", "透明度", 0},
};

constexpr int kCurveCount = int(sizeof(kCurves) / sizeof(kCurves[0]));

bool frameInList(double frame, const QVector<double>& frames)
{
    for (double candidate : frames)
        if (std::abs(candidate - frame) < 1e-6)
            return true;
    return false;
}

} // namespace

CurveEditor::CurveEditor(Project* project, History* history, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_history(history)
{
    setObjectName(QStringLiteral("curveEditor"));
    setMinimumHeight(140);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);

    m_visible.fill(false, kCurveCount);
    for (int i = 0; i < kCurveCount; ++i)
    {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("curveToggle_")
                               + QString::fromLatin1(kCurves[i].property));
        button->setText(QString::fromUtf8(kCurves[i].label));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::toggled, this, [this, i](bool on) {
            if (i >= 0 && i < m_visible.size())
                m_visible[i] = on;
            update();
        });
        m_buttons.append(button);
    }

    m_hScroll = new QScrollBar(Qt::Horizontal, this);
    m_hScroll->setObjectName(QStringLiteral("curveScroll"));
    connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollX = value;
        update();
    });

    // Window-level Ctrl+C / Ctrl+V belong to entity copy/paste. The graph page
    // keeps the keys for keyframes by accepting ShortcutOverride while focused.
    updateButtons();
    updateScrollBar();
}

int CurveEditor::curveCount() const
{
    return kCurveCount;
}

QString CurveEditor::curveProperty(int index) const
{
    return index >= 0 && index < kCurveCount
               ? QString::fromLatin1(kCurves[index].property)
               : QString();
}

QString CurveEditor::curveLabel(int index) const
{
    return index >= 0 && index < kCurveCount
               ? QString::fromUtf8(kCurves[index].label)
               : QString();
}

bool CurveEditor::curveVisible(int index) const
{
    return index >= 0 && index < m_visible.size() && m_visible[index];
}

void CurveEditor::setCurveVisible(int index, bool visible)
{
    if (index < 0 || index >= m_visible.size() || m_visible[index] == visible)
        return;
    m_visible[index] = visible;
    updateButtons();
    update();
}

QColor CurveEditor::curveColor(int index) const
{
    if (index < 0 || index >= kCurveCount)
        return palette().color(QPalette::Highlight);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    return QColor::fromHsv(kCurves[index].hue, dark ? 200 : 175, dark ? 235 : 145);
}

void CurveEditor::updateButtons()
{
    for (int i = 0; i < m_buttons.size(); ++i)
    {
        QToolButton* button = m_buttons[i];
        const QSignalBlocker blocker(button);
        button->setChecked(m_visible.value(i, false));
        const QColor color = curveColor(i);
        QColor fill = color;
        fill.setAlpha(45);
        button->setStyleSheet(
            QStringLiteral("QToolButton{background:transparent;border:none;"
                           "border-bottom:2px solid %1;padding:0 6px;color:%2;}"
                           "QToolButton:hover{background:transparent;}"
                           "QToolButton:checked{background:rgba(%3,%4,%5,%6);}")
                .arg(color.name(), palette().color(QPalette::Text).name())
                .arg(fill.red())
                .arg(fill.green())
                .arg(fill.blue())
                .arg(fill.alpha()));
        button->setToolTip(QStringLiteral("%1 曲线（点击显示或隐藏）")
                               .arg(QString::fromUtf8(kCurves[i].label)));
    }
}

void CurveEditor::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange)
        updateButtons();
    QWidget::changeEvent(event);
}

void CurveEditor::setSelectedEntity(EntityId id)
{
    if (m_entity == id)
    {
        update();
        return;
    }

    m_entity = id;
    m_selection.clear();
    m_dragMode = DragMode::None;
    m_dragDelta = 0;
    m_dragRecords.clear();
    m_dragFrames.clear();

    // Default to the curves that actually carry keys for this entity.
    const Entity* entity = m_project->entity(id);
    for (int i = 0; i < kCurveCount; ++i)
    {
        bool hasKeys = false;
        if (entity)
        {
            const auto track = entity->tracks.constFind(curveProperty(i));
            hasKeys = track != entity->tracks.constEnd() && !track->keys.isEmpty();
        }
        m_visible[i] = hasKeys;
    }
    updateButtons();
    updateScrollBar();
    update();
}

void CurveEditor::setCurrentFrame(int frame)
{
    frame = std::max(0, frame);
    if (m_frame == frame)
        return;
    m_frame = frame;

    // Keep the playhead in view, like the timeline does.
    const int width = plotWidth();
    const double playhead = m_frame * m_pixelsPerFrame;
    if (playhead > m_scrollX + width - 40)
        m_scrollX = int(std::lround(playhead - width + 40));
    else if (playhead < m_scrollX)
        m_scrollX = int(std::lround(playhead));
    updateScrollBar();
    update();
}

int CurveEditor::plotWidth() const
{
    return std::max(1, width() - kPlotMargin * 2);
}

QRectF CurveEditor::plotRect() const
{
    const double top = kHeaderHeight + kRulerHeight;
    const double bottom =
        std::max(top + 1.0, height() - kScrollBarHeight - 1.0);
    return QRectF(kPlotMargin, top, plotWidth(), bottom - top);
}

QRect CurveEditor::chartRectForTest() const
{
    return QRect(kPlotMargin, kHeaderHeight, plotWidth(),
                 std::max(1, height() - kHeaderHeight - kScrollBarHeight));
}

QRect CurveEditor::plotRectForTest() const
{
    return plotRect().toRect();
}

int CurveEditor::contentWidth() const
{
    const int end = std::max(m_project->timelineLength(), m_frame);
    return int(end * m_pixelsPerFrame) + plotWidth();
}

void CurveEditor::updateScrollBar()
{
    if (!m_hScroll)
        return;
    const int maximum = std::max(0, contentWidth() - plotWidth());
    m_scrollX = std::clamp(m_scrollX, 0, maximum);
    const QSignalBlocker blocker(m_hScroll);
    m_hScroll->setRange(0, maximum);
    m_hScroll->setPageStep(plotWidth());
    m_hScroll->setSingleStep(60);
    m_hScroll->setValue(m_scrollX);
    m_hScroll->setVisible(maximum > 0);
}

double CurveEditor::frameFromX(double x) const
{
    return (x - kPlotMargin + m_scrollX) / m_pixelsPerFrame;
}

double CurveEditor::xForFrame(double frame) const
{
    return kPlotMargin + frame * m_pixelsPerFrame - m_scrollX;
}

int CurveEditor::frameXForTest(double frame) const
{
    return int(std::lround(xForFrame(frame)));
}

int CurveEditor::frameFromXForTest(int x) const
{
    return int(std::lround(frameFromX(x)));
}

void CurveEditor::setPixelsPerFrameForTest(double pixels)
{
    m_pixelsPerFrame = std::clamp(pixels, 0.25, 32.0);
    updateScrollBar();
    update();
}

bool CurveEditor::trackAt(const QString& property, const Track** track) const
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return false;
    const auto it = entity->tracks.constFind(property);
    if (it == entity->tracks.constEnd() || it->keys.isEmpty())
        return false;
    if (track)
        *track = &*it;
    return true;
}

bool CurveEditor::keyIndexAt(const Track& track, double frame, int* index) const
{
    for (int i = 0; i < track.keys.size(); ++i)
        if (std::abs(track.keys[i].frame - frame) < 1e-6)
        {
            if (index)
                *index = i;
            return true;
        }
    return false;
}

CurveEditor::ValueRange CurveEditor::rangeFor(const QString& property) const
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return {};

    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    const auto track = entity->tracks.constFind(property);
    if (track != entity->tracks.constEnd())
    {
        const QVector<Keyframe>& keys = track->keys;
        for (int i = 0; i < keys.size(); ++i)
        {
            minimum = std::min(minimum, keys[i].value);
            maximum = std::max(maximum, keys[i].value);
            if (i == 0)
                continue;

            // Sample the incoming segment so elastic/bounce/bezier overshoot
            // stays inside the graph instead of being clipped at the top.
            const double span = keys[i].value - keys[i - 1].value;
            for (int step = 1; step < 32; ++step)
            {
                const double eased = applyEasing(keys[i].ease, step / 32.0);
                const double value = keys[i - 1].value + span * eased;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }

            // Keep bezier handles reachable even when they sit outside the
            // rendered curve.
            if (keys[i].ease.easing == Easing::Bezier)
            {
                const double delta = keys[i].value - keys[i - 1].value;
                minimum = std::min(minimum,
                                   keys[i - 1].value + delta * keys[i].ease.inY);
                maximum = std::max(maximum,
                                   keys[i - 1].value + delta * keys[i].ease.inY);
                minimum = std::min(minimum,
                                   keys[i - 1].value + delta * keys[i].ease.outY);
                maximum = std::max(maximum,
                                   keys[i - 1].value + delta * keys[i].ease.outY);
            }
        }
    }

    if (minimum > maximum)
    {
        const double value = entity->staticValue(property);
        minimum = value - 1.0;
        maximum = value + 1.0;
    }

    double padding = (maximum - minimum) * 0.08;
    if (padding < 1e-9)
        padding = std::max(1.0, std::abs(maximum) * 0.1);
    return {minimum - padding, maximum + padding};
}

double CurveEditor::yForValue(const ValueRange& range, double value) const
{
    const QRectF plot = plotRect();
    const double span = std::max(1e-9, range.maximum - range.minimum);
    const double normalized = (value - range.minimum) / span;
    return plot.bottom() - normalized * plot.height();
}

double CurveEditor::valueForY(const ValueRange& range, double y) const
{
    const QRectF plot = plotRect();
    const double normalized = (plot.bottom() - y) / std::max(1.0, plot.height());
    return range.minimum + normalized * (range.maximum - range.minimum);
}

double CurveEditor::valueAt(const Entity& entity, const QString& property,
                            double frame) const
{
    const auto it = entity.tracks.constFind(property);
    if (it == entity.tracks.constEnd() || it->keys.isEmpty())
        return entity.staticValue(property);

    // Handle drags are previewed locally; everything else goes through the
    // same evaluator the renderer uses.
    if (m_dragMode != DragMode::MoveHandle || property != m_handleProperty)
        return it->evaluate(frame, entity.staticValue(property));

    // Preview the one segment whose handle is being dragged. The project is
    // not modified until the mouse is released.
    const QVector<Keyframe>& keys = it->keys;
    if (frame <= keys.first().frame)
        return keys.first().value;
    if (frame >= keys.last().frame)
        return keys.last().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (frame <= keys[i].frame)
        {
            const Keyframe& a = keys[i - 1];
            const Keyframe& b = keys[i];
            const double span = b.frame - a.frame;
            if (span <= 0.0)
                return b.value;
            EasingSpec ease = b.ease;
            if (std::abs(b.frame - m_handleFrame) < 1e-6)
                ease = m_handlePreview;
            const double t = (frame - a.frame) / span;
            return a.value + (b.value - a.value) * applyEasing(ease, t);
        }
    }
    return keys.last().value;
}

double CurveEditor::displayFrame(double frame) const
{
    if (m_dragMode == DragMode::MoveKeys && isSelectedFrame(frame))
        return std::max(0.0, frame + m_dragDelta);
    return frame;
}

bool CurveEditor::isSelectedFrame(double frame) const
{
    for (const KeyframeSelection& selection : m_selection)
        if (std::abs(selection.frame - frame) < 1e-6)
            return true;
    return false;
}

EasingSpec CurveEditor::easingAt(const QString& property, double frame) const
{
    if (m_dragMode == DragMode::MoveHandle && property == m_handleProperty
        && std::abs(frame - m_handleFrame) < 1e-6)
        return m_handlePreview;

    const Track* track = nullptr;
    int index = 0;
    if (trackAt(property, &track) && keyIndexAt(*track, frame, &index))
        return track->keys[index].ease;
    return EasingSpec();
}

bool CurveEditor::hitKeyframe(const QPointF& pos, KeyframeSelection* result) const
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return false;

    bool found = false;
    double bestDistance = 9.0;
    double bestFrame = 0.0;
    for (int i = 0; i < kCurveCount; ++i)
    {
        if (!m_visible.value(i, false))
            continue;
        const QString property = curveProperty(i);
        const auto track = entity->tracks.constFind(property);
        if (track == entity->tracks.constEnd())
            continue;
        const ValueRange range = rangeFor(property);
        for (const Keyframe& key : track->keys)
        {
            const QPointF point(xForFrame(displayFrame(key.frame)),
                                yForValue(range, key.value));
            const double distance = QLineF(pos, point).length();
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                bestFrame = key.frame;
                found = true;
            }
        }
    }

    if (found && result)
    {
        result->entity = m_entity;
        result->frame = bestFrame;
    }
    return found;
}

bool CurveEditor::handlePoint(const QString& property, double frame, bool inHandle,
                              QPointF* result) const
{
    const Track* track = nullptr;
    int index = 0;
    if (!trackAt(property, &track) || !keyIndexAt(*track, frame, &index) || index <= 0)
        return false;

    const Keyframe& a = track->keys[index - 1];
    const Keyframe& b = track->keys[index];
    const EasingSpec ease = easingAt(property, frame);
    const double t = inHandle ? ease.inX : ease.outX;
    const double progress = inHandle ? ease.inY : ease.outY;
    const double value = a.value + (b.value - a.value) * progress;
    const double handleFrame = a.frame + (b.frame - a.frame) * t;
    if (result)
        *result = QPointF(xForFrame(handleFrame),
                          yForValue(rangeFor(property), value));
    return true;
}

bool CurveEditor::hitHandle(const QPointF& pos, QString* property, double* frame,
                            bool* inHandle) const
{
    bool found = false;
    double bestDistance = 10.0;
    for (const KeyframeSelection& selection : m_selection)
    {
        for (int i = 0; i < kCurveCount; ++i)
        {
            if (!m_visible.value(i, false))
                continue;
            const QString candidate = curveProperty(i);
            if (easingAt(candidate, selection.frame).easing != Easing::Bezier)
                continue;
            for (int side = 0; side < 2; ++side)
            {
                QPointF point;
                if (!handlePoint(candidate, selection.frame, side == 0, &point))
                    continue;
                const double distance = QLineF(pos, point).length();
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    found = true;
                    if (property)
                        *property = candidate;
                    if (frame)
                        *frame = selection.frame;
                    if (inHandle)
                        *inHandle = side == 0;
                }
            }
        }
    }
    return found;
}

QPointF CurveEditor::keyframePointForTest(const QString& property, double frame) const
{
    const Track* track = nullptr;
    int index = 0;
    if (!trackAt(property, &track) || !keyIndexAt(*track, frame, &index))
        return QPointF();
    return QPointF(xForFrame(frame),
                   yForValue(rangeFor(property), track->keys[index].value));
}

QPointF CurveEditor::bezierHandlePointForTest(const QString& property, double frame,
                                              bool inHandle) const
{
    QPointF point;
    handlePoint(property, frame, inHandle, &point);
    return point;
}

QVector<KeyframeRecord> CurveEditor::selectionRecords() const
{
    QVector<KeyframeRecord> records;
    if (m_entity == kInvalidEntity)
        return records;
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return records;

    for (const KeyframeSelection& selection : m_selection)
    {
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - selection.frame) < 1e-6)
                    records.append({m_entity, it.key(), key.frame, key.value, key.ease});
            }
        }
    }
    return records;
}

int CurveEditor::clampDelta(const QVector<KeyframeRecord>& records, int desired) const
{
    const int step = desired > 0 ? 1 : -1;
    auto valid = [this, &records](int delta) {
        for (const KeyframeRecord& record : records)
        {
            const double target = record.frame + delta;
            if (target < -1e-9)
                return false;
            const Track* track = nullptr;
            if (!trackAt(record.property, &track))
                continue;
            for (const Keyframe& key : track->keys)
            {
                // Keys that travel with this drag are never obstacles.
                if (frameInList(key.frame, m_dragFrames))
                    continue;
                if (std::abs(key.frame - target) < 1e-6)
                    return false;
            }
        }
        return true;
    };

    for (int delta = desired; delta != 0; delta -= step)
        if (valid(delta))
            return delta;
    return 0;
}

void CurveEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor alt = pal.color(QPalette::AlternateBase);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::Text);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor accent = pal.color(QPalette::Highlight);
    const QRectF plot = plotRect();

    p.fillRect(rect(), base);
    p.fillRect(QRect(0, 0, width(), kHeaderHeight), alt);
    p.setPen(mid);
    p.drawLine(0, kHeaderHeight, width(), kHeaderHeight);

    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    // Header hint, only when the property buttons leave enough room.
    int hintLeft = kPlotMargin;
    if (!m_buttons.isEmpty())
        hintLeft = m_buttons.last()->geometry().right() + 14;
    if (width() - hintLeft > 160)
    {
        p.setPen(dim);
        p.drawText(QRect(hintLeft, 0, width() - hintLeft - kPlotMargin, kHeaderHeight),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("拖动关键帧改时间 · 框选多个 · Ctrl+滚轮缩放"));
    }

    // Time ruler.
    const int fps = std::max(1, m_project->fps);
    int labelSeconds = 1;
    while (labelSeconds * fps * m_pixelsPerFrame < 80.0)
        labelSeconds *= 2;
    const int labelFrames = labelSeconds * fps;

    const double leftFrame = std::max(0.0, frameFromX(plot.left()));
    const int firstLabel = int(leftFrame / labelFrames) * labelFrames;

    p.setPen(mid);
    if (fps * m_pixelsPerFrame >= 10.0)
    {
        const int firstSecond = firstLabel / fps * fps;
        for (int f = firstSecond;; f += fps)
        {
            const double x = xForFrame(f);
            if (x > plot.right())
                break;
            if (x >= plot.left())
                p.drawLine(QPointF(x, plot.top() - 4.0), QPointF(x, plot.top()));
        }
    }

    for (int f = firstLabel;; f += labelFrames)
    {
        const double x = xForFrame(f);
        if (x > plot.right())
            break;
        if (x < plot.left() - 2.0)
            continue;
        p.setPen(QPen(mid, 1.0));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(text);
        const double seconds = f / double(fps);
        p.drawText(QRectF(x + 3.0, plot.top() - kRulerHeight, 80.0, kRulerHeight - 2.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(seconds, 'g', 4) + QStringLiteral("s"));
    }

    // Horizontal grid.
    p.setPen(QPen(mid, 1.0, Qt::DotLine));
    for (int i = 1; i < 4; ++i)
    {
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    const Entity* entity = m_project->entity(m_entity);

    p.save();
    p.setClipRect(plot);
    for (int i = 0; i < kCurveCount; ++i)
    {
        if (!m_visible.value(i, false))
            continue;
        const QString property = curveProperty(i);
        const Track* track = nullptr;
        if (!entity || !trackAt(property, &track))
            continue;

        const ValueRange range = rangeFor(property);
        const QColor color = curveColor(i);

        QPainterPath path;
        bool started = false;
        for (double x = plot.left(); x <= plot.right(); x += 2.0)
        {
            const double frame = frameFromX(x);
            const double value = valueAt(*entity, property, frame);
            const QPointF point(x, yForValue(range, value));
            if (!started)
            {
                path.moveTo(point);
                started = true;
            }
            else
            {
                path.lineTo(point);
            }
        }
        p.setPen(QPen(color, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        for (const Keyframe& key : track->keys)
        {
            const bool selected = isSelectedFrame(key.frame);
            const QPointF point(xForFrame(displayFrame(key.frame)),
                                yForValue(range, key.value));
            const double radius = 4.5;
            QPolygonF diamond;
            diamond << QPointF(point.x(), point.y() - radius)
                    << QPointF(point.x() + radius, point.y())
                    << QPointF(point.x(), point.y() + radius)
                    << QPointF(point.x() - radius, point.y());
            p.setPen(QPen(selected ? accent : base, 1.0));
            p.setBrush(selected ? pal.color(QPalette::HighlightedText) : color);
            p.drawPolygon(diamond);
        }
    }

    // Bezier handles belong to the incoming segment of selected keyframes.
    for (const KeyframeSelection& selection : m_selection)
    {
        for (int i = 0; i < kCurveCount; ++i)
        {
            if (!m_visible.value(i, false))
                continue;
            const QString property = curveProperty(i);
            const Track* track = nullptr;
            int index = 0;
            if (!entity || !trackAt(property, &track)
                || !keyIndexAt(*track, selection.frame, &index) || index <= 0)
                continue;
            if (easingAt(property, selection.frame).easing != Easing::Bezier)
                continue;

            const ValueRange range = rangeFor(property);
            const QPointF start(xForFrame(displayFrame(track->keys[index - 1].frame)),
                                yForValue(range, track->keys[index - 1].value));
            const QPointF end(xForFrame(displayFrame(track->keys[index].frame)),
                              yForValue(range, track->keys[index].value));
            QPointF in;
            QPointF out;
            handlePoint(property, selection.frame, true, &in);
            handlePoint(property, selection.frame, false, &out);

            p.setPen(QPen(dim, 1.0, Qt::DashLine));
            p.drawLine(start, in);
            p.drawLine(end, out);

            const QColor color = curveColor(i);
            p.setPen(QPen(text, 1.0));
            p.setBrush(color);
            p.drawEllipse(in, 5.0, 5.0);
            p.drawEllipse(out, 5.0, 5.0);
        }
    }
    p.restore();

    // Playhead.
    const double playhead = xForFrame(m_frame);
    if (playhead >= plot.left() - 6.0 && playhead <= plot.right() + 6.0)
    {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(playhead, plot.top()), QPointF(playhead, plot.bottom()));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        QPolygonF head;
        head << QPointF(playhead - 5.0, plot.top() - 9.0)
             << QPointF(playhead + 5.0, plot.top() - 9.0)
             << QPointF(playhead, plot.top());
        p.drawPolygon(head);
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        p.setPen(QPen(accent, 1.0, Qt::DashLine));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 50));
        p.drawRect(box.intersected(plot));
    }

    if (!entity)
    {
        p.setPen(dim);
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("选择一个对象以编辑属性曲线"));
    }
    else
    {
        bool anyVisible = false;
        for (bool visible : m_visible)
            anyVisible = anyVisible || visible;
        if (!anyVisible)
        {
            p.setPen(dim);
            p.drawText(plot, Qt::AlignCenter,
                       QStringLiteral("当前对象还没有属性曲线\n"
                                      "在属性面板点 ◇ 打关键帧，或点上方曲线名称"));
        }
    }
}

void CurveEditor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    int x = kPlotMargin;
    for (QToolButton* button : m_buttons)
    {
        const int width = button->sizeHint().width();
        button->setGeometry(x, 2, width, kHeaderHeight - 4);
        x += width + 4;
    }
    if (m_hScroll)
        m_hScroll->setGeometry(kPlotMargin, height() - kScrollBarHeight, plotWidth(),
                               kScrollBarHeight);
    updateScrollBar();
}

void CurveEditor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshTracks();
}

void CurveEditor::refreshTracks()
{
    // Keys may have been added while the graph page was hidden, or after the
    // entity was selected with no tracks. Only fill in when nothing is
    // visible, so an explicit per-curve hide survives tab switches.
    const Entity* entity = m_project->entity(m_entity);
    if (entity)
    {
        bool anyVisible = false;
        for (bool visible : m_visible)
            anyVisible = anyVisible || visible;
        if (!anyVisible)
        {
            bool changed = false;
            for (int i = 0; i < kCurveCount; ++i)
            {
                const auto track = entity->tracks.constFind(curveProperty(i));
                if (track != entity->tracks.constEnd() && !track->keys.isEmpty())
                {
                    m_visible[i] = true;
                    changed = true;
                }
            }
            if (changed)
                updateButtons();
        }
    }
    update();
}

void CurveEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPointF pos = event->position();
    if (pos.y() < kHeaderHeight)
        return;
    setFocus(Qt::MouseFocusReason);

    const QRectF plot = plotRect();
    if (pos.y() < plot.top())
    {
        m_dragMode = DragMode::Scrub;
        emit frameRequested(std::max(0, int(std::lround(frameFromX(pos.x())))));
        return;
    }
    if (!plot.contains(pos))
        return;

    QString property;
    double frame = 0.0;
    bool inHandle = true;
    if (hitHandle(pos, &property, &frame, &inHandle))
    {
        const Track* track = nullptr;
        int index = 0;
        if (!trackAt(property, &track) || !keyIndexAt(*track, frame, &index))
            return;
        m_handleOldEase = track->keys[index].ease;
        m_handlePreview = m_handleOldEase;
        m_handlePreview.easing = Easing::Bezier;
        m_handleProperty = property;
        m_handleFrame = frame;
        m_handleIn = inHandle;
        m_pressPos = pos;
        m_dragMode = DragMode::MoveHandle;
        update();
        return;
    }

    KeyframeSelection hit;
    if (hitKeyframe(pos, &hit))
    {
        const bool ctrl =
            event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        auto existing =
            std::find(m_selection.begin(), m_selection.end(), hit);

        if (existing != m_selection.end() && ctrl)
        {
            m_selection.erase(existing);
            update();
            return;
        }

        if (existing == m_selection.end())
        {
            if (!shift)
                m_selection.clear();
            m_selection.append(hit);
        }

        emit frameRequested(int(std::lround(hit.frame)));
        m_pressPos = pos;
        m_pressFrame = int(std::lround(hit.frame));
        m_dragDelta = 0;
        m_dragRecords = selectionRecords();
        m_dragFrames.clear();
        for (const KeyframeSelection& selection : m_selection)
            m_dragFrames.append(selection.frame);
        m_dragMode = DragMode::MoveKeys;
        update();
        return;
    }

    if (!(event->modifiers() & Qt::ShiftModifier))
        m_selection.clear();
    m_dragMode = DragMode::PendingSelect;
    m_pressPos = pos;
    m_boxStart = pos;
    m_boxEnd = pos;
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    if (m_dragMode == DragMode::Scrub)
    {
        emit frameRequested(std::max(0, int(std::lround(frameFromX(pos.x())))));
        return;
    }

    if (m_dragMode == DragMode::PendingSelect)
    {
        if ((pos - m_pressPos).manhattanLength() <= 4.0)
            return;
        m_dragMode = DragMode::BoxSelect;
        m_boxStart = m_pressPos;
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        m_boxEnd = pos;
        update();
        return;
    }

    if (m_dragMode == DragMode::MoveKeys)
    {
        const int desired =
            int(std::lround(frameFromX(pos.x()))) - m_pressFrame;
        const int delta = clampDelta(m_dragRecords, desired);
        if (delta != m_dragDelta)
        {
            m_dragDelta = delta;
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::MoveHandle)
    {
        const Track* track = nullptr;
        int index = 0;
        if (!trackAt(m_handleProperty, &track)
            || !keyIndexAt(*track, m_handleFrame, &index) || index <= 0)
            return;

        const Keyframe& a = track->keys[index - 1];
        const Keyframe& b = track->keys[index];
        const double spanFrames = b.frame - a.frame;
        const double spanValue = b.value - a.value;
        const QRectF plot = plotRect();
        double t = spanFrames > 1e-9
                       ? (frameFromX(pos.x()) - a.frame) / spanFrames
                       : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        double progress = 0.0;
        if (std::abs(spanValue) > 1e-9)
            progress = (valueForY(rangeFor(m_handleProperty), pos.y()) - a.value)
                       / spanValue;
        else
            progress = (plot.bottom() - pos.y()) / std::max(1.0, plot.height());
        progress = std::clamp(progress, -1.0, 2.0);

        if (m_handleIn)
        {
            m_handlePreview.inX = t;
            m_handlePreview.inY = progress;
        }
        else
        {
            m_handlePreview.outX = t;
            m_handlePreview.outY = progress;
        }
        m_handlePreview.easing = Easing::Bezier;
        update();
        return;
    }

    // Hover cursor over draggable points.
    KeyframeSelection hit;
    QString property;
    double frame = 0.0;
    bool inHandle = true;
    if (hitHandle(pos, &property, &frame, &inHandle) || hitKeyframe(pos, &hit))
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragMode == DragMode::PendingSelect)
    {
        m_dragMode = DragMode::None;
        emit frameRequested(std::max(0, int(std::lround(frameFromX(event->position().x())))));
        update();
        return;
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd).normalized();
        if (!(event->modifiers() & Qt::ShiftModifier))
            m_selection.clear();

        const Entity* entity = m_project->entity(m_entity);
        if (entity)
        {
            for (int i = 0; i < kCurveCount; ++i)
            {
                if (!m_visible.value(i, false))
                    continue;
                const QString property = curveProperty(i);
                const auto track = entity->tracks.constFind(property);
                if (track == entity->tracks.constEnd())
                    continue;
                const ValueRange range = rangeFor(property);
                for (const Keyframe& key : track->keys)
                {
                    const QPointF point(xForFrame(key.frame),
                                        yForValue(range, key.value));
                    if (!box.contains(point))
                        continue;
                    const KeyframeSelection selection{m_entity, key.frame};
                    if (std::find(m_selection.begin(), m_selection.end(), selection)
                        == m_selection.end())
                        m_selection.append(selection);
                }
            }
        }
        m_dragMode = DragMode::None;
        update();
        return;
    }

    if (m_dragMode == DragMode::MoveKeys)
    {
        const int delta = m_dragDelta;
        const QVector<KeyframeRecord> records = m_dragRecords;
        m_dragMode = DragMode::None;
        m_dragDelta = 0;
        m_dragRecords.clear();
        m_dragFrames.clear();
        if (delta != 0 && !records.isEmpty())
        {
            const QVector<Entity> before = m_project->entities();
            m_project->moveKeyframes(records, double(delta));
            m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                                before, m_project->entities(),
                                QStringLiteral("移动关键帧")),
                            *m_project);
            for (KeyframeSelection& selection : m_selection)
                selection.frame = std::max(0.0, selection.frame + delta);
            emit frameRequested(std::max(0, m_pressFrame + delta));
            emit statusMessage(QStringLiteral("已移动关键帧"));
        }
        update();
        return;
    }

    if (m_dragMode == DragMode::MoveHandle)
    {
        const bool changed = !(m_handlePreview == m_handleOldEase);
        const QString property = m_handleProperty;
        const double frame = m_handleFrame;
        const EasingSpec oldEase = m_handleOldEase;
        const EasingSpec newEase = m_handlePreview;
        m_dragMode = DragMode::None;
        m_handleProperty.clear();
        if (changed)
        {
            KeyframeEasingRecord record;
            record.entity = m_entity;
            record.property = property;
            record.frame = frame;
            record.oldEase = oldEase;
            record.newEase = newEase;
            m_history->push(
                std::make_unique<SetKeyframeEasingCommand>(
                    QVector<KeyframeEasingRecord>{record}),
                *m_project);
            emit statusMessage(QStringLiteral("已调整关键帧缓动"));
        }
        update();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CurveEditor::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    if (event->modifiers() & Qt::ControlModifier)
    {
        const double oldZoom = m_pixelsPerFrame;
        double next = std::clamp(oldZoom * std::pow(2.0, steps), 0.25, 32.0);
        if (next > 1.0)
            next = std::round(next);
        if (std::abs(next - oldZoom) < 1e-6)
            return;
        const double anchor = frameFromX(event->position().x());
        m_pixelsPerFrame = next;
        m_scrollX = int(std::lround(anchor * next
                                    - (event->position().x() - kPlotMargin)));
        updateScrollBar();
        update();
        event->accept();
        return;
    }

    m_scrollX -= int(std::lround(steps * 120.0));
    updateScrollBar();
    update();
    event->accept();
}

bool CurveEditor::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool ctrl = key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        if (ctrl && (key->key() == Qt::Key_C || key->key() == Qt::Key_V
                     || key->key() == Qt::Key_A))
        {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void CurveEditor::keyPressEvent(QKeyEvent* event)
{
    const bool ctrl = event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (!m_selection.isEmpty())
            deleteSelection();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_C)
    {
        copySelection();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_V)
    {
        pasteClipboard();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_A)
    {
        selectAllKeyframes();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape)
    {
        m_selection.clear();
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CurveEditor::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->pos().y() < kHeaderHeight)
        return;

    KeyframeSelection hit;
    if (hitKeyframe(event->pos(), &hit) && !isSelectedFrame(hit.frame))
    {
        m_selection.clear();
        m_selection.append(hit);
        emit frameRequested(int(std::lround(hit.frame)));
        update();
    }

    const bool hasSelection = !m_selection.isEmpty();
    QMenu menu(this);
    QAction* easing = menu.addAction(QStringLiteral("缓动…"));
    QAction* bezier = menu.addAction(QStringLiteral("转为贝塞尔"));
    menu.addSeparator();
    QAction* copy = menu.addAction(QStringLiteral("复制关键帧"));
    QAction* paste = menu.addAction(QStringLiteral("粘贴到播放头"));
    QAction* remove = menu.addAction(QStringLiteral("删除关键帧"));
    menu.addSeparator();
    QAction* selectAll = menu.addAction(QStringLiteral("全选关键帧"));

    easing->setEnabled(hasSelection);
    bezier->setEnabled(hasSelection);
    copy->setEnabled(hasSelection);
    paste->setEnabled(!m_clipboard.isEmpty() && m_entity != kInvalidEntity
                      && !m_project->isEffectivelyLocked(m_entity));
    remove->setEnabled(hasSelection);
    selectAll->setEnabled(m_entity != kInvalidEntity);

    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == easing)
        showEasingMenu(event->globalPos());
    else if (chosen == bezier)
        convertToBezier();
    else if (chosen == copy)
        copySelection();
    else if (chosen == paste)
        pasteClipboard();
    else if (chosen == remove)
        deleteSelection();
    else if (chosen == selectAll)
        selectAllKeyframes();
}

void CurveEditor::showEasingMenu(const QPoint& globalPos)
{
    EasingSpec current;
    bool found = false;
    const Entity* entity = m_project->entity(m_entity);
    if (entity)
    {
        for (const KeyframeSelection& selection : m_selection)
        {
            for (auto it = entity->tracks.constBegin();
                 it != entity->tracks.constEnd() && !found; ++it)
            {
                for (const Keyframe& key : it->keys)
                {
                    if (std::abs(key.frame - selection.frame) < 1e-6)
                    {
                        current = key.ease;
                        found = true;
                        break;
                    }
                }
            }
            if (found)
                break;
        }
    }

    const QString title = QStringLiteral("批量缓动：%1 个关键帧").arg(m_selection.size());
    EasingSpec chosen;
    if (EasingChooser::choose(this, current, globalPos, &chosen, title))
        applyEasingToSelection(chosen);
}

void CurveEditor::applyEasingToSelection(const EasingSpec& spec)
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return;

    QVector<KeyframeEasingRecord> records;
    for (const KeyframeSelection& selection : m_selection)
    {
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - selection.frame) > 1e-6 || key.ease == spec)
                    continue;
                KeyframeEasingRecord record;
                record.entity = m_entity;
                record.property = it.key();
                record.frame = key.frame;
                record.oldEase = key.ease;
                record.newEase = spec;
                records.append(record);
            }
        }
    }
    if (records.isEmpty())
        return;

    m_history->push(std::make_unique<SetKeyframeEasingCommand>(records), *m_project);
    emit statusMessage(QStringLiteral("已更新关键帧缓动"));
    update();
}

void CurveEditor::convertToBezier()
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return;

    EasingSpec bezier;
    bezier.easing = Easing::Bezier;
    QVector<KeyframeEasingRecord> records;
    for (const KeyframeSelection& selection : m_selection)
    {
        for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        {
            for (const Keyframe& key : it->keys)
            {
                if (std::abs(key.frame - selection.frame) > 1e-6
                    || key.ease.easing == Easing::Bezier)
                    continue;
                KeyframeEasingRecord record;
                record.entity = m_entity;
                record.property = it.key();
                record.frame = key.frame;
                record.oldEase = key.ease;
                record.newEase = bezier;
                records.append(record);
            }
        }
    }
    if (records.isEmpty())
        return;

    m_history->push(std::make_unique<SetKeyframeEasingCommand>(records), *m_project);
    emit statusMessage(QStringLiteral("已转为贝塞尔缓动，可拖动圆点手柄"));
    update();
}

void CurveEditor::copySelection()
{
    const QVector<KeyframeRecord> records = selectionRecords();
    if (records.isEmpty())
        return;
    m_clipboard = records;
    m_clipboardBase = records.first().frame;
    for (const KeyframeRecord& record : records)
        m_clipboardBase = std::min(m_clipboardBase, record.frame);
    emit statusMessage(QStringLiteral("已复制 %1 个关键帧").arg(records.size()));
}

void CurveEditor::pasteClipboard()
{
    if (m_clipboard.isEmpty() || m_entity == kInvalidEntity
        || m_project->isEffectivelyLocked(m_entity))
        return;

    const double delta = m_frame - m_clipboardBase;
    const QVector<Entity> before = m_project->entities();
    int added = 0;
    QVector<double> frames;
    for (const KeyframeRecord& record : m_clipboard)
    {
        const double target = record.frame + delta;
        if (target < -1e-9)
            continue;
        if (m_project->keyframe(m_entity, record.property, target))
            continue;
        Keyframe key;
        key.frame = target;
        key.value = record.value;
        key.ease = record.ease;
        m_project->addKeyframe(m_entity, record.property, key);
        ++added;
        if (!frameInList(target, frames))
            frames.append(target);
    }

    if (added == 0)
    {
        emit statusMessage(QStringLiteral("目标帧已有关键帧，未粘贴"));
        return;
    }

    m_history->push(std::make_unique<EntitiesSnapshotCommand>(
                        before, m_project->entities(), QStringLiteral("粘贴关键帧")),
                    *m_project);
    m_selection.clear();
    for (double frame : frames)
        m_selection.append({m_entity, frame});
    updateScrollBar();
    update();
    emit statusMessage(QStringLiteral("已粘贴 %1 个关键帧").arg(added));
}

void CurveEditor::deleteSelection()
{
    const QVector<KeyframeRecord> records = selectionRecords();
    if (records.isEmpty())
        return;
    m_history->push(std::make_unique<RemoveKeyframesCommand>(records), *m_project);
    m_selection.clear();
    update();
    emit statusMessage(QStringLiteral("已删除关键帧"));
}

void CurveEditor::selectAllKeyframes()
{
    const Entity* entity = m_project->entity(m_entity);
    if (!entity)
        return;
    m_selection.clear();
    for (auto it = entity->tracks.constBegin(); it != entity->tracks.constEnd(); ++it)
        for (const Keyframe& key : it->keys)
            if (!isSelectedFrame(key.frame))
                m_selection.append({m_entity, key.frame});
    update();
    emit statusMessage(QStringLiteral("已选中 %1 个关键帧").arg(m_selection.size()));
}

} // namespace pvf
