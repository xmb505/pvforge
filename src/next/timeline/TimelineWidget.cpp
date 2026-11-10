#include "next/timeline/TimelineWidget.h"

#include "audio2/Peaks.h"
#include "next/core/Document.h"
#include "next/timeline/EntityRange.h"
#include "next/timeline/MarkerDialog.h"

#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <climits>

namespace pvf::next {

namespace {

constexpr char kAssetMimeType[] = "application/x-pvforge-asset";

bool hasAssetMime(const QMimeData* mime)
{
    return mime && mime->hasFormat(QString::fromLatin1(kAssetMimeType));
}

bool rangesOverlap(int aStart, int aEnd, int bStart, int bEnd)
{
    return aStart < bEnd && bStart < aEnd;
}

QStringList drivenTracks(SolverEffect effect)
{
    if (effect == SolverEffect::Spin)
        return {QStringLiteral("rotation")};
    return {QStringLiteral("x"), QStringLiteral("y")};
}

bool solverStartKey(const model::Entity& entity, const QString& track, int frame)
{
    for (const auto& solver : entity.solvers)
    {
        if (solver.startFrame == frame
            && drivenTracks(solver.effect).contains(track))
            return true;
    }
    return false;
}

bool entityIsLocked(const Document* document, const Uuid& uuid)
{
    return document && isEntityLocked(document->project(), uuid);
}

void collectSubtree(const model::Project& project, const Uuid& uuid,
                    QVector<Uuid>& out, QSet<Uuid>& visited);

QColor contentColor(const model::Entity& entity)
{
    switch (entity.content.kind)
    {
    case ContentKind::Text:
        return QColor(242, 153, 74);
    case ContentKind::Vector:
        return QColor(176, 132, 255);
    case ContentKind::Camera:
        return QColor(88, 196, 221);
    case ContentKind::Asset:
        switch (entity.content.asset.type)
        {
        case AssetType::Image:
            return QColor(74, 144, 226);
        case AssetType::Video:
            return QColor(235, 87, 87);
        case AssetType::Audio:
            return QColor(92, 204, 142);
        case AssetType::Html:
            return QColor(242, 201, 76);
        }
        break;
    case ContentKind::Empty:
        break;
    }
    return QColor(132, 138, 148);
}

QColor solverColor(SolverEffect effect)
{
    switch (effect)
    {
    case SolverEffect::Gravity:
        return QColor(63, 167, 214);
    case SolverEffect::Orbit:
        return QColor(166, 94, 234);
    case SolverEffect::Spin:
        return QColor(245, 166, 35);
    case SolverEffect::BlackHole:
        return QColor(91, 72, 140);
    }
    return QColor(63, 167, 214);
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QColor contrastText(const QColor& background)
{
    return background.lightness() > 150 ? QColor(20, 22, 26)
                                        : QColor(255, 255, 255);
}

// Solver segments drive x/y for gravity/orbit/blackhole and rotation for
// spin. Inside a segment (startFrame, effectiveEnd] normal keys are forbidden;
// the start frame itself holds the segment's initial key and stays editable.
bool trackBlockedAt(const model::Entity& entity, const QString& track, int frame)
{
    const int duration = entity.duration();
    if (duration <= 0)
        return false;

    for (const auto& solver : entity.solvers)
    {
        const int last = solver.endFrame < 0
                             ? duration - 1
                             : std::min(duration - 1, solver.endFrame);
        if (frame <= solver.startFrame || frame > last)
            continue;

        const bool positionDriven = solver.effect == SolverEffect::Gravity
                                    || solver.effect == SolverEffect::Orbit
                                    || solver.effect == SolverEffect::BlackHole;
        if (positionDriven
            && (track == QStringLiteral("x") || track == QStringLiteral("y")))
            return true;
        if (solver.effect == SolverEffect::Spin
            && track == QStringLiteral("rotation"))
            return true;
    }
    return false;
}

QStringList genericTransformTracks()
{
    return {QStringLiteral("x"),        QStringLiteral("y"),
            QStringLiteral("rotation"), QStringLiteral("scaleX"),
            QStringLiteral("scaleY"),   QStringLiteral("opacity")};
}

bool isStepTrack(const QString& track)
{
    return track == QStringLiteral("visible")
           || track == QStringLiteral("physicsInteract");
}

double staticTrackValue(const model::Entity& entity, const QString& track)
{
    const model::Transform& transform = entity.transform;
    if (track == QStringLiteral("x"))
        return transform.x;
    if (track == QStringLiteral("y"))
        return transform.y;
    if (track == QStringLiteral("rotation"))
        return transform.rotation;
    if (track == QStringLiteral("scaleX"))
        return transform.scaleX;
    if (track == QStringLiteral("scaleY"))
        return transform.scaleY;
    if (track == QStringLiteral("opacity"))
        return transform.opacity;
    if (track == QStringLiteral("pivotX"))
        return transform.pivotX;
    if (track == QStringLiteral("pivotY"))
        return transform.pivotY;
    if (track == QStringLiteral("visible"))
        return entity.visible ? 1.0 : 0.0;
    if (track == QStringLiteral("physicsInteract"))
        return entity.physicsInteract ? 1.0 : 0.0;
    return 0.0;
}

void setKeyframeValue(model::Track& track, int frame, double value)
{
    for (model::Keyframe& key : track.keys)
    {
        if (key.frame == frame)
        {
            key.value = value;
            return;
        }
    }

    model::Keyframe key;
    key.frame = frame;
    key.value = value;
    track.keys.append(key);
}

void removeKeyframeAt(model::Track& track, int frame)
{
    for (int i = track.keys.size() - 1; i >= 0; --i)
    {
        if (track.keys.at(i).frame == frame)
            track.keys.removeAt(i);
    }
}

void sortTrack(model::Track& track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const model::Keyframe& a, const model::Keyframe& b) {
                  return a.frame < b.frame;
              });
}

void collectSubtree(const model::Project& project, const Uuid& uuid,
                    QVector<Uuid>& out, QSet<Uuid>& visited)
{
    if (visited.contains(uuid))
        return;
    visited.insert(uuid);
    out.append(uuid);
    for (const Uuid& child : model::childrenOf(project, uuid))
        collectSubtree(project, child, out, visited);
}

void drawBadge(QPainter& painter, const QRect& bar, const QString& text, int slot)
{
    const int size = 12;
    const QRect badge(bar.right() - 4 - size * (slot + 1), bar.top() + 4, size,
                      size);
    if (badge.left() <= bar.left() + 3)
        return;

    painter.save();
    painter.fillRect(badge, QColor(16, 18, 22, 190));
    QFont font = painter.font();
    font.setPixelSize(9);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(240, 242, 248));
    painter.drawText(badge, Qt::AlignCenter, text);
    painter.restore();
}

} // namespace

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
    return body.bottom() - (row + 1) * kRowHeight + 1;
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
    const int x = xFromFrame(frame);
    if (x >= 0 && x < width())
        return;

    const int target = int(std::lround(frame * m_pixelsPerFrame)) - width() / 2;
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

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    paintToolbar(painter);
    paintRuler(painter);
    paintRows(painter);
    paintGrid(painter);
    paintMarkers(painter);
    paintEntities(painter);
    paintPlayhead(painter);
    paintEmptyHint(painter);
}

void TimelineWidget::paintToolbar(QPainter& painter)
{
    const QRect bar = toolbarRect();
    if (bar.isEmpty())
        return;

    const QPalette pal = palette();
    painter.fillRect(bar, pal.color(QPalette::AlternateBase).darker(105));
    painter.setPen(QPen(pal.color(QPalette::Mid), 1));
    painter.drawLine(0, bar.bottom(), width(), bar.bottom());

    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    int x = 8;
    if (!m_parentUuid.isEmpty())
    {
        const int centerY = bar.center().y();
        const QColor accent = pal.color(QPalette::Highlight);
        painter.setPen(QPen(accent, 1.6));
        painter.drawLine(x, centerY, x + 9, centerY);
        painter.drawLine(x, centerY, x + 4, centerY - 3);
        painter.drawLine(x, centerY, x + 4, centerY + 3);
        x += 15;
    }

    const QString rootLabel = QStringLiteral("根时间线");
    painter.setPen(m_parentUuid.isEmpty() ? pal.color(QPalette::Text)
                                          : pal.color(QPalette::Highlight));
    painter.drawText(QRect(x, bar.top(), metrics.horizontalAdvance(rootLabel) + 2,
                           bar.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, rootLabel);
    x += metrics.horizontalAdvance(rootLabel);

    if (m_parentUuid.isEmpty())
        return;

    painter.setPen(pal.color(QPalette::Mid));
    const QString separator = QStringLiteral(" / ");
    painter.drawText(QRect(x, bar.top(), metrics.horizontalAdvance(separator) + 2,
                           bar.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, separator);
    x += metrics.horizontalAdvance(separator);

    QString parentName = m_parentUuid;
    if (m_document)
    {
        const auto it = m_document->project().entities.constFind(m_parentUuid);
        if (it != m_document->project().entities.constEnd()
            && !it.value().name.isEmpty())
            parentName = it.value().name;
    }

    const int available = breadcrumbRect().right() - x - 8;
    painter.setPen(pal.color(QPalette::Text));
    painter.drawText(QRect(x, bar.top(), std::max(0, available), bar.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(parentName, Qt::ElideRight,
                                        std::max(0, available)));
}

void TimelineWidget::paintRuler(QPainter& painter)
{
    const QRect ruler = rulerRect();
    const QPalette pal = palette();
    painter.fillRect(ruler, pal.color(QPalette::AlternateBase));
    painter.setPen(QPen(pal.color(QPalette::Mid), 1));
    painter.drawLine(0, ruler.bottom(), width(), ruler.bottom());

    const int step = labelFrameStep();
    const int lastFrame = std::max(0, frameFromX(width())) + step;
    const int firstFrame = (std::max(0, frameFromX(0)) / step) * step;

    QFont small = painter.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    painter.setFont(small);

    // Minor ticks make the ruler readable when the label step is coarse.
    const int minorStep = std::max(1, step / 5);
    if (minorStep != step && minorStep * m_pixelsPerFrame >= 5.0)
    {
        const int firstMinor = (std::max(0, frameFromX(0)) / minorStep)
                               * minorStep;
        for (int frame = firstMinor; frame <= lastFrame; frame += minorStep)
        {
            if (frame % step == 0)
                continue;
            const int x = xFromFrame(frame);
            if (x < 0 || x > width())
                continue;
            painter.setPen(QPen(pal.color(QPalette::Mid), 1));
            painter.drawLine(x, ruler.bottom() - 3, x, ruler.bottom());
        }
    }

    for (int frame = firstFrame; frame <= lastFrame; frame += step)
    {
        const int x = xFromFrame(frame);
        if (x < -1 || x > width() + 1)
            continue;

        painter.setPen(QPen(pal.color(QPalette::Mid), 1));
        painter.drawLine(x, ruler.bottom() - 8, x, ruler.bottom());
        painter.setPen(pal.color(QPalette::Text));
        painter.drawText(QRect(x + 3, ruler.top() + 14, 90, ruler.height() - 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(frame));
    }
}

void TimelineWidget::paintRows(QPainter& painter)
{
    const QRect body = bodyRect();
    if (body.isEmpty())
        return;

    const QPalette pal = palette();
    painter.save();
    painter.setClipRect(body);

    const int maxRow = body.height() / kRowHeight + 2;
    for (int row = 0; row <= maxRow; ++row)
    {
        const int top = rowTop(row);
        if (top > body.bottom() || top + kRowHeight <= body.top())
            continue;

        const QRect rowRect(0, top, width(), kRowHeight);
        if (row % 2 == 1)
        {
            QColor stripe = pal.color(QPalette::AlternateBase);
            stripe.setAlpha(90);
            painter.fillRect(rowRect, stripe);
        }

        QColor line = pal.color(QPalette::Mid);
        line.setAlpha(60);
        painter.setPen(QPen(line, 1));
        painter.drawLine(0, top, width(), top);
    }
    painter.restore();
}

void TimelineWidget::paintGrid(QPainter& painter)
{
    const QRect body = bodyRect();
    if (body.isEmpty())
        return;

    const int step = labelFrameStep();
    const int lastFrame = std::max(0, frameFromX(width())) + step;
    const int firstFrame = (std::max(0, frameFromX(0)) / step) * step;

    QColor line = palette().color(QPalette::Mid);
    line.setAlpha(45);

    painter.save();
    painter.setClipRect(body);
    painter.setPen(QPen(line, 1));
    for (int frame = firstFrame; frame <= lastFrame; frame += step)
    {
        const int x = xFromFrame(frame);
        if (x < 0 || x > width())
            continue;
        painter.drawLine(x, body.top(), x, body.bottom());
    }
    painter.restore();
}

void TimelineWidget::paintMarkers(QPainter& painter)
{
    if (!m_document)
        return;

    const QRect lane = markerLaneRect();
    const QRect body = bodyRect();
    const QPalette pal = palette();

    painter.save();
    QColor laneColor = pal.color(QPalette::AlternateBase).darker(112);
    painter.fillRect(lane, laneColor);
    painter.setPen(QPen(pal.color(QPalette::Mid), 1));
    painter.drawLine(0, lane.bottom(), width(), lane.bottom());
    painter.restore();

    const model::Project& project = m_document->project();
    if (project.markers.isEmpty())
        return;

    QFont small = painter.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 2.0));
    const QFontMetrics metrics(small);

    // Guide lines sit below the clips.
    painter.save();
    painter.setClipRect(body);
    for (const auto& marker : project.markers)
    {
        const int x = xFromFrame(marker.frame);
        if (x < -1 || x > width() + 1)
            continue;
        QColor color(marker.color);
        if (!color.isValid())
            color = QColor(255, 190, 60);
        painter.setPen(QPen(withAlpha(color, 120), 1, Qt::DashLine));
        painter.drawLine(x, body.top(), x, body.bottom());
    }
    painter.restore();

    painter.save();
    painter.setFont(small);
    for (const auto& marker : project.markers)
    {
        const int x = xFromFrame(marker.frame);
        if (x < -1 || x > width() + 1)
            continue;

        QColor color(marker.color);
        if (!color.isValid())
            color = QColor(255, 190, 60);

        painter.setPen(QPen(withAlpha(color, 220), 1));
        painter.drawLine(x, lane.top(), x, lane.bottom());

        QPolygonF flag;
        flag << QPointF(x, lane.top() + 1) << QPointF(x + 7, lane.top() + 5)
             << QPointF(x, lane.top() + 9);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(flag);

        const QString title = marker.title.isEmpty() ? tr("标记")
                                                     : marker.title;
        const int labelWidth =
            std::min(160, metrics.horizontalAdvance(title) + 12);
        QRect label(x + 9, lane.top() + 1, labelWidth, lane.height() - 2);
        if (label.right() > width() - 2)
            label.translate(width() - 2 - label.right(), 0);
        if (label.left() < 2)
            label.translate(2 - label.left(), 0);

        painter.fillRect(label, withAlpha(color, 225));
        painter.setPen(contrastText(color));
        painter.drawText(label.adjusted(5, 0, -3, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(title, Qt::ElideRight,
                                            std::max(0, label.width() - 8)));
    }
    painter.restore();
}

QRect TimelineWidget::markerHitRect(const model::Marker& marker) const
{
    const int x = xFromFrame(marker.frame);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 2.0));
    const QFontMetrics metrics(small);
    const QString title = marker.title.isEmpty() ? tr("标记") : marker.title;
    const int labelWidth = std::min(160, metrics.horizontalAdvance(title) + 12);
    const QRect lane = markerLaneRect();

    QRect rect(x + 9, lane.top() + 1, labelWidth, lane.height() - 2);
    if (rect.right() > width() - 2)
        rect.translate(width() - 2 - rect.right(), 0);
    if (rect.left() < 2)
        rect.translate(2 - rect.left(), 0);
    return rect.united(QRect(x - 2, lane.top(), 7, lane.height()));
}

QString TimelineWidget::markerIdAt(const QPoint& pos) const
{
    if (!m_document)
        return {};

    const QRect lane = markerLaneRect();
    const QRect body = bodyRect();
    if (!lane.contains(pos) && !body.contains(pos))
        return {};

    // Later markers are painted on top, so hit-test them first.
    const QVector<model::Marker>& markers = m_document->project().markers;
    for (int i = markers.size() - 1; i >= 0; --i)
    {
        const model::Marker& marker = markers.at(i);
        if (marker.id.isEmpty())
            continue;

        const int x = xFromFrame(marker.frame);
        if (lane.contains(pos))
        {
            if (markerHitRect(marker).contains(pos))
                return marker.id;
            if (std::abs(pos.x() - x) <= 4)
                return marker.id;
        }
        else if (std::abs(pos.x() - x) <= 3)
        {
            return marker.id;
        }
    }
    return {};
}

int TimelineWidget::markerIndex(const QString& markerId) const
{
    if (!m_document || markerId.isEmpty())
        return -1;
    const QVector<model::Marker>& markers = m_document->project().markers;
    for (int i = 0; i < markers.size(); ++i)
    {
        if (markers.at(i).id == markerId)
            return i;
    }
    return -1;
}

bool TimelineWidget::commitMarkers(const QVector<model::Marker>& before,
                                   const QString& message, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return false;
    }

    QString commitError;
    if (!m_document->commitProject(message, &commitError))
    {
        m_document->mutableProject().markers = before;
        const QString failure =
            commitError.isEmpty() ? tr("标记保存失败") : commitError;
        if (error)
            *error = failure;
        emit statusMessage(tr("标记保存失败：%1").arg(failure));
        return false;
    }

    if (error)
        error->clear();
    updateScrollBar();
    update();
    return true;
}

bool TimelineWidget::insertMarker(const model::Marker& marker, QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return false;
    }

    model::Marker candidate = marker;
    if (candidate.id.isEmpty())
        candidate.id = model::newUuid();
    candidate.frame = std::max(0, candidate.frame);
    if (!normalizeMarker(candidate, error))
        return false;

    const QVector<model::Marker> before = m_document->project().markers;
    m_document->mutableProject().markers.append(candidate);
    return commitMarkers(
        before, tr("pvf: add marker \"%1\"").arg(candidate.title), error);
}

void TimelineWidget::addMarkerAtPlayhead()
{
    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, playhead());
    marker.title = tr("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");
    insertMarker(marker);
}

bool TimelineWidget::updateMarker(const QString& markerId,
                                  const model::Marker& marker, QString* error)
{
    const int index = markerIndex(markerId);
    if (index < 0)
    {
        if (error)
            *error = tr("标记不存在");
        return false;
    }

    model::Marker candidate = marker;
    candidate.id = markerId; // ids are stable; only fields are editable.
    if (!normalizeMarker(candidate, error))
        return false;

    const QVector<model::Marker> before = m_document->project().markers;
    m_document->mutableProject().markers[index] = candidate;
    return commitMarkers(
        before, tr("pvf: edit marker \"%1\"").arg(candidate.title), error);
}

bool TimelineWidget::deleteMarker(const QString& markerId, QString* error)
{
    const int index = markerIndex(markerId);
    if (index < 0)
    {
        if (error)
            *error = tr("标记不存在");
        return false;
    }

    const QVector<model::Marker> before = m_document->project().markers;
    const QString title = before.at(index).title;
    m_document->mutableProject().markers.removeAt(index);
    return commitMarkers(
        before,
        tr("pvf: delete marker \"%1\"").arg(title.isEmpty() ? markerId : title),
        error);
}

void TimelineWidget::deleteMarkerWithConfirm(const QString& markerId)
{
    const int index = markerIndex(markerId);
    if (index < 0)
        return;

    const QString title = m_document->project().markers.at(index).title;
    const auto answer = QMessageBox::question(
        this, tr("删除标记"),
        tr("确定删除标记 \"%1\"？").arg(title.isEmpty() ? markerId : title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!deleteMarker(markerId, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

void TimelineWidget::openNewMarkerDialog()
{
    if (!m_document)
        return;

    model::Marker marker;
    marker.id = model::newUuid();
    marker.frame = std::max(0, playhead());
    marker.title = tr("标记 %1").arg(marker.frame);
    marker.color = QStringLiteral("#ffbe3c");

    MarkerDialog dialog(this);
    dialog.setWindowTitle(tr("添加标记"));
    dialog.setMarker(marker);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!insertMarker(dialog.marker(), &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

void TimelineWidget::openMarkerEditor(const QString& markerId)
{
    const int index = markerIndex(markerId);
    if (index < 0)
        return;

    MarkerDialog dialog(this);
    dialog.setWindowTitle(tr("编辑标记"));
    dialog.setMarker(m_document->project().markers.at(index));
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!updateMarker(markerId, dialog.marker(), &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("标记"), error);
}

void TimelineWidget::paintEntities(QPainter& painter)
{
    if (!m_document)
        return;

    const QRect body = bodyRect();
    if (body.isEmpty())
        return;

    painter.save();
    painter.setClipRect(body);
    for (const Uuid& uuid : visibleEntities())
        paintEntity(painter, uuid);
    painter.restore();
}

void TimelineWidget::paintEntity(QPainter& painter, const Uuid& uuid)
{
    if (!m_document)
        return;

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    const model::Entity& entity = it.value();
    const QRect bar = entityBarRect(uuid);
    if (bar.isEmpty() || !bar.intersects(bodyRect()))
        return;

    const QColor color = contentColor(entity);
    const bool selected = m_document->selection() == uuid;
    const QPalette pal = palette();

    painter.save();
    painter.fillRect(bar, withAlpha(color, entity.hidden ? 90 : 235));
    painter.setPen(QPen(color.darker(170), 1));
    painter.drawRect(bar.adjusted(0, 0, -1, -1));

    paintWaveform(painter, entity, bar);
    paintSolverBands(painter, entity, bar);
    paintKeyframes(painter, entity, bar);

    const QRect addRect = addButtonRect(uuid);
    if (!addRect.isEmpty())
    {
        const QColor glyph = contrastText(color);
        painter.fillRect(addRect, QColor(16, 18, 22, 120));
        painter.setPen(QPen(glyph, 1));
        painter.drawRect(addRect.adjusted(0, 0, -1, -1));
        const int cx = addRect.center().x();
        const int cy = addRect.center().y();
        painter.drawLine(cx - 3, cy, cx + 3, cy);
        painter.drawLine(cx, cy - 3, cx, cy + 3);
    }

    const int badges = (entity.hidden ? 14 : 0) + (entity.locked ? 14 : 0);
    const int textLeft =
        addRect.isEmpty() ? bar.left() + 7 : addRect.right() + 5;
    const int textRight = bar.right() - 7 - badges;
    if (textRight - textLeft > 8 && bar.height() >= 14)
    {
        QFont font = painter.font();
        font.setBold(selected);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QString name = entity.name.isEmpty() ? uuid : entity.name;
        painter.setPen(contrastText(color));
        painter.drawText(
            QRect(textLeft, bar.top(), textRight - textLeft, bar.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(name, Qt::ElideRight, textRight - textLeft));
    }

    if (entity.hidden)
        drawBadge(painter, bar, QStringLiteral("H"), 0);
    if (entity.locked)
        drawBadge(painter, bar, QStringLiteral("L"), entity.hidden ? 1 : 0);

    if (selected)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(pal.color(QPalette::Highlight), 2));
        painter.drawRect(bar.adjusted(1, 1, -3, -3));
    }
    painter.restore();
}

void TimelineWidget::paintWaveform(QPainter& painter,
                                   const model::Entity& entity,
                                   const QRect& bar)
{
    if (!m_document || entity.content.kind != ContentKind::Asset
        || entity.content.asset.type != AssetType::Audio)
        return;

    const QRect inner = bar.adjusted(2, 2, -2, -2);
    if (inner.width() < 2 || inner.height() < 6)
        return;

    QString error;
    QVector<float> peaks;
    const auto stored = m_document->assets().get(entity.content.asset.hash);
    if (stored)
        peaks = peaksForAsset(*stored, &error);

    const QColor ink = contrastText(contentColor(entity));
    const int centerY = inner.center().y();
    const int half = std::max(2, inner.height() / 2 - 2);

    painter.save();
    painter.setClipRect(inner);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (peaks.isEmpty())
    {
        painter.setPen(QPen(withAlpha(ink, 110), 1));
        painter.drawLine(inner.left(), centerY, inner.right(), centerY);
        if (inner.width() >= 72 && inner.height() >= 16)
        {
            QFont font = painter.font();
            font.setPointSizeF(std::max(7.0, font.pointSizeF() - 2.0));
            painter.setFont(font);
            painter.setPen(withAlpha(ink, 150));
            painter.drawText(inner, Qt::AlignCenter, tr("波形不可用"));
        }
        painter.restore();
        return;
    }

    const int fps = std::max(1, m_document->project().settings.fps);
    double assetSeconds = 0.0;
    if (stored)
    {
        assetSeconds = stored->metadata
                           .value(QStringLiteral("durationSeconds"))
                           .toDouble(0.0);
        if (assetSeconds <= 0.0)
        {
            const int frames = stored->metadata
                                   .value(QStringLiteral("durationFrames"))
                                   .toInt(0);
            if (frames > 0)
                assetSeconds = double(frames) / double(fps);
        }
    }

    const double entitySeconds =
        double(std::max(1, entity.duration())) / double(fps);
    const double sourceStart =
        double(std::max(0, entity.content.asset.sourceIn)) / double(fps);
    if (assetSeconds <= 0.0)
        assetSeconds = sourceStart + entitySeconds;

    const int x0 = std::max(inner.left(), 0);
    const int x1 = std::min(inner.right(), width() - 1);
    const int span = std::max(1, inner.width());
    const int peakCount = peaks.size();

    painter.setPen(QPen(withAlpha(ink, 170), 1));
    for (int x = x0; x <= x1; ++x)
    {
        const double t0 =
            sourceStart + entitySeconds * double(x - inner.left()) / span;
        const double t1 =
            sourceStart + entitySeconds * double(x - inner.left() + 1) / span;

        int i0 = int(std::floor(t0 / assetSeconds * peakCount));
        int i1 = int(std::ceil(t1 / assetSeconds * peakCount));
        i0 = std::clamp(i0, 0, peakCount);
        i1 = std::clamp(i1, i0, peakCount);

        float value = 0.0f;
        for (int i = i0; i < i1; ++i)
            value = std::max(value, peaks.at(i));

        const int amplitude =
            int(std::lround(std::clamp(double(value), 0.0, 1.0) * half));
        painter.drawLine(x, centerY - amplitude, x, centerY + amplitude);
    }
    painter.restore();
}

QVector<float> TimelineWidget::peaksForAsset(const assets::StoredAsset& asset,
                                             QString* error)
{
    if (!m_document)
    {
        if (error)
            *error = tr("未打开工程");
        return {};
    }

    // Projects are content-addressed, so a cache hit stays valid for the whole
    // project. Switching project roots drops everything.
    const QString root = m_document->assets().projectRoot();
    if (m_peakCacheRoot != root)
    {
        m_peakCache.clear();
        m_peakCacheRoot = root;
    }

    const QString key =
        asset.hash + QLatin1Char(':') + QString::number(kPeakBuckets);
    const auto cached = m_peakCache.constFind(key);
    if (cached != m_peakCache.constEnd())
        return cached.value();

    const QStringList files = QDir(asset.directory).entryList(
        {QStringLiteral("content.*")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
    {
        if (error)
            *error = tr("素材内容文件缺失");
        return {};
    }

    const QString path = asset.directory + QLatin1Char('/') + files.first();
    const QVector<float> peaks = audio2::audioPeaks(path, kPeakBuckets, error);
    m_peakCache.insert(key, peaks);
    return peaks;
}

void TimelineWidget::clearWaveformCache()
{
    m_peakCache.clear();
    m_peakCacheRoot.clear();
    update();
}

void TimelineWidget::paintSolverBands(QPainter& painter,
                                      const model::Entity& entity,
                                      const QRect& bar)
{
    const int duration = entity.duration();
    if (duration <= 0 || entity.solvers.isEmpty())
        return;

    painter.save();
    painter.setClipRect(bar.adjusted(1, 1, -1, -1));

    for (const auto& solver : entity.solvers)
    {
        const int relativeStart = std::clamp(solver.startFrame, 0, duration);
        const int relativeEnd =
            solver.endFrame < 0
                ? duration
                : std::clamp(solver.endFrame + 1, 0, duration);
        if (relativeEnd <= relativeStart)
            continue;

        const int x0 =
            std::max(bar.left(), xFromFrame(entity.start + relativeStart));
        const int x1 =
            std::min(bar.right() + 1, xFromFrame(entity.start + relativeEnd));
        if (x1 <= x0)
            continue;

        const QColor color = solverColor(solver.effect);
        const int bandTop = bar.top() + bar.height() / 2;
        painter.fillRect(QRect(x0, bandTop, x1 - x0, bar.bottom() - bandTop),
                         withAlpha(color, 110));
        painter.fillRect(QRect(x0, bar.bottom() - 3, x1 - x0, 3), color);
        painter.fillRect(QRect(x0, bar.top() + 1, 2, bar.height() - 2), color);
        painter.fillRect(QRect(x1 - 2, bar.top() + 1, 2, bar.height() - 2),
                         color);

        // Start marker: right-pointing triangle. End marker: square.
        QPolygonF startMark;
        startMark << QPointF(x0 + 2, bar.top() + 2)
                  << QPointF(x0 + 10, bar.top() + 6)
                  << QPointF(x0 + 2, bar.top() + 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(startMark);
        painter.fillRect(QRect(x1 - 9, bar.top() + 2, 6, 6), color);
    }

    painter.restore();
}

void TimelineWidget::paintKeyframes(QPainter& painter,
                                    const model::Entity& entity,
                                    const QRect& bar)
{
    if (!m_document || m_document->selection() != entity.uuid)
        return;

    const QSet<int> frames = mergedKeyFrames(entity);
    if (frames.isEmpty())
        return;

    const bool framesSelected = m_keyframeSelectionUuid == entity.uuid;

    painter.save();
    painter.setClipRect(bar.adjusted(1, 1, -1, -1));

    const QColor accent = palette().color(QPalette::Highlight);
    const int centerY = bar.center().y();
    for (int frame : frames)
    {
        const int x = xFromFrame(entity.start + frame);
        if (x < bar.left() - 6 || x > bar.right() + 6)
            continue;

        const bool selected = framesSelected && m_selectedKeyframes.contains(frame);
        const double radius = selected ? 5.4 : 4.2;

        QPolygonF diamond;
        diamond << QPointF(x, centerY - radius)
                << QPointF(x + radius, centerY)
                << QPointF(x, centerY + radius)
                << QPointF(x - radius, centerY);

        painter.setPen(QPen(selected ? accent.darker(150)
                                     : QColor(15, 17, 22, 220),
                            selected ? 1.6 : 1.2));
        painter.setBrush(selected ? accent : QColor(250, 251, 255));
        painter.drawPolygon(diamond);
    }
    painter.restore();
}

QSet<int> TimelineWidget::mergedKeyFrames(const model::Entity& entity) const
{
    QSet<int> frames;
    const int duration = entity.duration();
    if (duration <= 0)
        return frames;

    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
         ++it)
    {
        for (const auto& key : it.value().keys)
        {
            if (key.frame < 0 || key.frame >= duration)
                continue;
            if (trackBlockedAt(entity, it.key(), key.frame))
                continue;
            frames.insert(key.frame);
        }
    }
    return frames;
}

void TimelineWidget::paintPlayhead(QPainter& painter)
{
    const int x = xFromFrame(playhead());
    if (x < -1 || x > width() + 1)
        return;

    const QRect ruler = rulerRect();
    const QRect body = bodyRect();
    const QColor accent = palette().color(QPalette::Highlight);

    painter.save();
    painter.setPen(QPen(accent, 1.5));
    painter.drawLine(x, ruler.top(), x, body.bottom());

    QPolygonF head;
    head << QPointF(x - 5, ruler.top()) << QPointF(x + 5, ruler.top())
         << QPointF(x, ruler.top() + 7);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawPolygon(head);
    painter.restore();
}

void TimelineWidget::paintEmptyHint(QPainter& painter)
{
    const QRect body = bodyRect();
    if (body.isEmpty())
        return;

    QString text;
    if (!m_document)
        text = QStringLiteral("未打开工程");
    else if (visibleEntities().isEmpty())
        text = m_parentUuid.isEmpty() ? QStringLiteral("没有根实体")
                                      : QStringLiteral("该父级没有子实体");
    else
        return;

    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(body, Qt::AlignCenter, text);
}

int TimelineWidget::keyframeAt(const QPoint& pos) const
{
    if (!m_document)
        return -1;

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return -1;

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
        return -1;

    const model::Entity& entity = it.value();
    const QRect bar = entityBarRect(uuid);
    if (bar.isEmpty() || !bar.contains(pos))
        return -1;

    // Diamonds sit on the vertical centre; keep the hit lane narrow enough to
    // leave the “+” button and trim edges usable.
    if (std::abs(pos.y() - bar.center().y()) > 5)
        return -1;

    int bestFrame = -1;
    int bestDistance = 6;
    for (int frame : mergedKeyFrames(entity))
    {
        const int x = xFromFrame(entity.start + frame);
        const int distance = std::abs(pos.x() - x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestFrame = frame;
        }
    }
    return bestFrame;
}

void TimelineWidget::clearKeyframeSelection()
{
    m_keyframeSelectionUuid.clear();
    m_selectedKeyframes.clear();
}

Uuid TimelineWidget::keyframeSelectionEntity() const
{
    if (!m_document)
        return Uuid();
    return m_document->selection() == m_keyframeSelectionUuid
               ? m_keyframeSelectionUuid
               : Uuid();
}

QString TimelineWidget::entityDisplayName(const model::Entity& entity) const
{
    return entity.name.isEmpty() ? entity.uuid : entity.name;
}

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint& pos) const
{
    Hit hit;
    if (!m_document || !bodyRect().contains(pos))
        return hit;

    const QVector<Uuid> entities = visibleEntities();
    for (int i = entities.size() - 1; i >= 0; --i)
    {
        const Uuid& uuid = entities.at(i);
        const QRect bar = entityBarRect(uuid);
        if (!bar.contains(pos))
            continue;

        hit.uuid = uuid;
        if (pos.x() <= bar.left() + kEdgeGrabPixels)
            hit.zone = HitZone::LeftEdge;
        else if (pos.x() >= bar.right() - kEdgeGrabPixels + 1)
            hit.zone = HitZone::RightEdge;
        else
            hit.zone = HitZone::Body;
        break;
    }
    return hit;
}

void TimelineWidget::updateHoverCursor(const QPoint& pos)
{
    if (!m_document)
    {
        unsetCursor();
        return;
    }

    if (markerLaneRect().contains(pos) && !markerIdAt(pos).isEmpty())
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }

    for (const Uuid& uuid : visibleEntities())
    {
        const QRect addRect = addButtonRect(uuid);
        if (!addRect.isEmpty() && addRect.contains(pos))
        {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    if (keyframeAt(pos) >= 0)
    {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty())
    {
        unsetCursor();
        return;
    }

    if (entityIsLocked(m_document, hit.uuid))
    {
        unsetCursor();
        return;
    }

    if (hit.zone == HitZone::LeftEdge || hit.zone == HitZone::RightEdge)
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::OpenHandCursor);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_document || event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);
    const QPoint pos = event->position().toPoint();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    const QString pressedMarker =
        markerLaneRect().contains(pos) ? markerIdAt(pos) : QString();
    if (!pressedMarker.isEmpty())
    {
        const int index = markerIndex(pressedMarker);
        m_dragMode = DragMode::Marker;
        m_dragMarkerId = pressedMarker;
        m_dragMarkerSnapshot = m_document->project().markers;
        m_dragPressPos = pos;
        m_dragChanged = false;
        if (index >= 0)
            m_document->setPlayhead(
                std::max(0, m_document->project().markers.at(index).frame));
        setCursor(Qt::SizeHorCursor);
        return;
    }

    if (!m_parentUuid.isEmpty() && breadcrumbRect().contains(pos))
    {
        setParentUuid(Uuid());
        return;
    }

    if (rulerRect().contains(pos) || markerLaneRect().contains(pos))
    {
        m_dragMode = DragMode::Scrub;
        m_dragEntity.clear();
        m_dragChanged = false;
        setPlayhead(frameFromX(pos.x()));
        return;
    }

    // The “+” button on the left of an entity bar opens its private timeline.
    for (const Uuid& uuid : visibleEntities())
    {
        const QRect addRect = addButtonRect(uuid);
        if (addRect.isEmpty() || !addRect.contains(pos))
            continue;

        m_dragMode = DragMode::None;
        m_dragEntity.clear();
        m_dragChanged = false;
        m_document->setSelection(uuid);
        emit privateTimelineRequested(uuid);
        return;
    }

    // Merged keyframe diamonds of the selected entity.
    const int keyFrame = keyframeAt(pos);
    if (keyFrame >= 0)
    {
        const auto entityIt =
            m_document->project().entities.constFind(m_document->selection());
        if (entityIt == m_document->project().entities.constEnd())
            return;

        const model::Entity& entity = entityIt.value();
        if (entityIsLocked(m_document, entity.uuid))
        {
            emit statusMessage(
                tr("实体已锁定：%1").arg(entityDisplayName(entity)));
            return;
        }

        if (m_keyframeSelectionUuid != entity.uuid)
        {
            m_keyframeSelectionUuid = entity.uuid;
            m_selectedKeyframes.clear();
        }

        const bool toggle = event->modifiers()
                            & (Qt::ControlModifier | Qt::ShiftModifier);
        if (toggle)
        {
            if (m_selectedKeyframes.contains(keyFrame))
                m_selectedKeyframes.remove(keyFrame);
            else
                m_selectedKeyframes.insert(keyFrame);
        }
        else if (!m_selectedKeyframes.contains(keyFrame))
        {
            m_selectedKeyframes = QSet<int>{keyFrame};
        }

        m_dragMode = DragMode::Keyframes;
        m_dragEntity = entity.uuid;
        m_dragPressPos = pos;
        m_dragKeyPressFrame = keyFrame;
        m_dragKeyFrames = m_selectedKeyframes;
        for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
             ++it)
            m_dragKeyTracks.insert(it.key(), it.value().keys);
        m_dragChanged = false;
        update();
        return;
    }

    // A click that is not on a diamond drops the frame selection.
    clearKeyframeSelection();

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty())
    {
        m_dragMode = DragMode::None;
        m_dragEntity.clear();
        m_dragChanged = false;
        m_document->setSelection(Uuid());
        update();
        return;
    }

    const auto it = m_document->project().entities.constFind(hit.uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    m_document->setSelection(hit.uuid);
    m_dragEntity = hit.uuid;
    m_dragPressPos = pos;
    m_dragOriginalStart = it.value().start;
    m_dragOriginalEnd = it.value().end;
    m_dragChanged = false;

    if (entityIsLocked(m_document, hit.uuid))
    {
        m_dragMode = DragMode::None;
        setCursor(Qt::ArrowCursor);
    }
    else if (hit.zone == HitZone::LeftEdge)
    {
        m_dragMode = DragMode::TrimStart;
        setCursor(Qt::SizeHorCursor);
    }
    else if (hit.zone == HitZone::RightEdge)
    {
        m_dragMode = DragMode::TrimEnd;
        setCursor(Qt::SizeHorCursor);
    }
    else
    {
        m_dragMode = DragMode::Move;
        QSet<Uuid> visited;
        collectSubtree(m_document->project(), m_dragEntity, m_dragMoveUuids,
                       visited);
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto entity =
                m_document->project().entities.constFind(uuid);
            if (entity == m_document->project().entities.constEnd())
                continue;
            if (entityIsLocked(m_document, uuid))
            {
                emit statusMessage(
                    tr("子树包含锁定实体，不能移动：%1")
                        .arg(entityDisplayName(entity.value())));
                m_dragMode = DragMode::None;
                m_dragMoveUuids.clear();
                setCursor(Qt::ArrowCursor);
                update();
                return;
            }
            m_dragOriginalRanges.insert(
                uuid, qMakePair(entity.value().start, entity.value().end));
        }
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_dragMode == DragMode::TrimStart
        || m_dragMode == DragMode::TrimEnd)
    {
        m_dragProjectSnapshot = m_document->project();
        m_dragHasSnapshot = true;
    }
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_document)
        return;

    const QPoint pos = event->position().toPoint();
    if (m_dragMode == DragMode::Scrub)
    {
        setPlayhead(frameFromX(pos.x()));
        return;
    }

    if (m_dragMode == DragMode::Marker)
    {
        const int index = markerIndex(m_dragMarkerId);
        if (index < 0)
            return;
        const int frame = std::max(0, frameFromX(pos.x()));
        model::Marker& marker =
            m_document->mutableProject().markers[index];
        if (marker.frame != frame)
        {
            marker.frame = frame;
            m_dragChanged = true;
            updateScrollBar();
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::Keyframes)
    {
        applyKeyframeDrag(pos);
        return;
    }

    if (m_dragMode == DragMode::Move || m_dragMode == DragMode::TrimStart
        || m_dragMode == DragMode::TrimEnd)
    {
        applyDrag(pos);
        return;
    }

    updateHoverCursor(pos);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    finishDrag();
    updateHoverCursor(event->position().toPoint());
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_document || event->button() != Qt::LeftButton)
        return;

    const QPoint pos = event->position().toPoint();
    if (markerLaneRect().contains(pos))
    {
        const QString markerId = markerIdAt(pos);
        if (!markerId.isEmpty())
        {
            openMarkerEditor(markerId);
            event->accept();
        }
        return;
    }

    if (rulerRect().contains(pos) || toolbarRect().contains(pos))
        return;

    const Hit hit = hitTest(pos);
    if (hit.uuid.isEmpty() || hit.zone != HitZone::Body)
        return;

    // The “+” button never writes keys.
    const QRect addRect = addButtonRect(hit.uuid);
    if (!addRect.isEmpty() && addRect.contains(pos))
        return;

    const auto it = m_document->project().entities.constFind(hit.uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    m_document->setSelection(hit.uuid);
    clearKeyframeSelection();

    const int duration = it.value().duration();
    if (duration <= 0)
        return;

    const int relativeFrame =
        std::clamp(frameFromX(pos.x()) - it.value().start, 0, duration - 1);
    insertKeyframesAt(hit.uuid, relativeFrame);
    event->accept();
}

void TimelineWidget::insertKeyframesAt(const Uuid& uuid, int relativeFrame)
{
    if (!m_document)
        return;

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    const int duration = entity.duration();
    if (duration <= 0)
        return;
    const int frame = std::clamp(relativeFrame, 0, duration - 1);

    // Prefer the tracks that already animate this entity; only fall back to
    // the generic transform tracks when no track has keys yet.
    QStringList targets;
    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
         ++it)
    {
        if (!it.value().keys.isEmpty())
            targets.append(it.key());
    }
    const bool animated = !targets.isEmpty();
    if (!animated)
        targets = genericTransformTracks();

    const QHash<QString, model::Track> before = entity.tracks;

    int written = 0;
    int blocked = 0;
    for (const QString& trackName : targets)
    {
        if (trackBlockedAt(entity, trackName, frame))
        {
            ++blocked;
            continue;
        }

        double value = staticTrackValue(entity, trackName);
        const auto trackIt = entity.tracks.constFind(trackName);
        if (animated && trackIt != entity.tracks.constEnd())
        {
            if (const auto evaluated =
                    trackIt.value().valueAt(frame, isStepTrack(trackName)))
                value = *evaluated;
        }

        setKeyframeValue(entity.tracks[trackName], frame, value);
        sortTrack(entity.tracks[trackName]);
        ++written;
    }

    if (written == 0)
    {
        entity.tracks = before;
        emit statusMessage(tr("该帧由求解段驱动，不能写入普通关键帧"));
        update();
        return;
    }

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: set keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧写入失败")
                               : tr("关键帧写入失败：%1").arg(error));
    }

    if (blocked > 0 && written > 0)
    {
        emit statusMessage(
            tr("求解段内的轨道已跳过，写入 %1 条轨道").arg(written));
    }

    update();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_document)
    {
        QWidget::contextMenuEvent(event);
        return;
    }

    const QPoint pos = event->pos();
    const bool onRuler =
        rulerRect().contains(pos) || markerLaneRect().contains(pos);
    const QString markerId = markerIdAt(pos);
    if (!onRuler && markerId.isEmpty())
    {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction* addAction = menu.addAction(tr("在播放头添加标记…"));
    QAction* editAction = nullptr;
    QAction* deleteAction = nullptr;
    if (!markerId.isEmpty())
    {
        menu.addSeparator();
        editAction = menu.addAction(tr("编辑标记…"));
        deleteAction = menu.addAction(tr("删除标记"));
    }

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == addAction)
        openNewMarkerDialog();
    else if (chosen == editAction)
        openMarkerEditor(markerId);
    else if (chosen == deleteAction)
        deleteMarkerWithConfirm(markerId);
    event->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_document)
    {
        QWidget::keyPressEvent(event);
        return;
    }

    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier
        && event->key() == Qt::Key_M)
    {
        addMarkerAtPlayhead();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (!m_selectedKeyframes.isEmpty())
        {
            deleteSelectedKeyframes();
            event->accept();
            return;
        }
    }
    else if (ctrl && event->key() == Qt::Key_C)
    {
        copySelectedKeyframes();
        event->accept();
        return;
    }
    else if (ctrl && event->key() == Qt::Key_V)
    {
        pasteKeyframes();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void TimelineWidget::deleteSelectedKeyframes()
{
    const Uuid uuid = keyframeSelectionEntity();
    if (uuid.isEmpty() || m_selectedKeyframes.isEmpty())
        return;

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    for (auto it = entity.tracks.constBegin(); it != entity.tracks.constEnd();
         ++it)
    {
        for (const model::Keyframe& key : it.value().keys)
        {
            if (m_selectedKeyframes.contains(key.frame)
                && solverStartKey(entity, it.key(), key.frame))
            {
                emit statusMessage(
                    tr("求解段开始帧的关键帧不能删除，请先删除求解段"));
                return;
            }
        }
    }

    const QHash<QString, model::Track> before = entity.tracks;
    bool changed = false;
    for (auto it = entity.tracks.begin(); it != entity.tracks.end();)
    {
        QVector<model::Keyframe> kept;
        kept.reserve(it.value().keys.size());
        for (const model::Keyframe& key : it.value().keys)
        {
            if (m_selectedKeyframes.contains(key.frame))
                changed = true;
            else
                kept.append(key);
        }

        if (kept.isEmpty())
            it = entity.tracks.erase(it);
        else
        {
            it.value().keys = kept;
            ++it;
        }
    }

    if (!changed)
        return;

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: delete keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧删除失败")
                               : tr("关键帧删除失败：%1").arg(error));
    }

    clearKeyframeSelection();
    update();
}

void TimelineWidget::copySelectedKeyframes()
{
    const Uuid uuid = keyframeSelectionEntity();
    if (uuid.isEmpty() || m_selectedKeyframes.isEmpty())
        return;

    const auto entityIt = m_document->project().entities.constFind(uuid);
    if (entityIt == m_document->project().entities.constEnd())
        return;

    m_keyClipboard.clear();
    m_keyClipboardUuid = uuid;
    for (auto it = entityIt.value().tracks.constBegin();
         it != entityIt.value().tracks.constEnd(); ++it)
    {
        for (const model::Keyframe& key : it.value().keys)
        {
            if (!m_selectedKeyframes.contains(key.frame))
                continue;
            CopiedKey copied;
            copied.track = it.key();
            copied.frame = key.frame;
            copied.value = key.value;
            copied.easing = key.easing;
            copied.note = key.note;
            m_keyClipboard.append(copied);
        }
    }

    if (m_keyClipboard.isEmpty())
        return;
    emit statusMessage(
        tr("已复制 %1 个关键帧").arg(m_keyClipboard.size()));
}

void TimelineWidget::pasteKeyframes()
{
    if (!m_document || m_keyClipboard.isEmpty())
        return;

    const Uuid uuid = m_document->selection();
    if (uuid.isEmpty())
        return;
    if (uuid != m_keyClipboardUuid)
    {
        emit statusMessage(tr("关键帧只能粘贴回原实体"));
        return;
    }

    auto entityIt = m_document->mutableProject().entities.find(uuid);
    if (entityIt == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = entityIt.value();
    if (entityIsLocked(m_document, entity.uuid))
    {
        emit statusMessage(
            tr("实体已锁定：%1").arg(entityDisplayName(entity)));
        return;
    }

    const int duration = entity.duration();
    if (duration <= 0)
        return;

    int firstFrame = m_keyClipboard.first().frame;
    int lastFrame = firstFrame;
    for (const CopiedKey& copied : m_keyClipboard)
    {
        firstFrame = std::min(firstFrame, copied.frame);
        lastFrame = std::max(lastFrame, copied.frame);
    }

    const int span = lastFrame - firstFrame;
    if (span >= duration)
    {
        emit statusMessage(tr("关键帧片段超出实体时长，无法粘贴"));
        return;
    }

    const int base = std::clamp(
        playhead() - entity.start, 0, std::max(0, duration - 1 - span));

    QVector<CopiedKey> placed;
    placed.reserve(m_keyClipboard.size());
    for (const CopiedKey& copied : m_keyClipboard)
    {
        CopiedKey target = copied;
        target.frame = base + (copied.frame - firstFrame);
        if (trackBlockedAt(entity, target.track, target.frame))
        {
            emit statusMessage(tr("粘贴目标落在求解段驱动的区间，已取消"));
            return;
        }
        placed.append(target);
    }

    const QHash<QString, model::Track> before = entity.tracks;
    QSet<int> pastedFrames;
    for (const CopiedKey& target : placed)
    {
        model::Track& track = entity.tracks[target.track];
        removeKeyframeAt(track, target.frame);
        model::Keyframe key;
        key.frame = target.frame;
        key.value = target.value;
        key.easing = target.easing;
        key.note = target.note;
        track.keys.append(key);
        sortTrack(track);
        pastedFrames.insert(target.frame);
    }

    const QString name = entityDisplayName(entity);
    QString error;
    const bool ok = m_document->commitEntity(
        uuid, QStringLiteral("pvf: paste keyframes \"%1\"").arg(name), &error);
    if (!ok)
    {
        auto restoreIt = m_document->mutableProject().entities.find(uuid);
        if (restoreIt != m_document->mutableProject().entities.end())
            restoreIt.value().tracks = before;
        emit statusMessage(error.isEmpty()
                               ? tr("关键帧粘贴失败")
                               : tr("关键帧粘贴失败：%1").arg(error));
        update();
        return;
    }

    m_keyframeSelectionUuid = uuid;
    m_selectedKeyframes = pastedFrames;
    update();
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int delta = event->angleDelta().y();
        if (delta == 0)
        {
            event->ignore();
            return;
        }

        const int cursorX = int(event->position().x());
        const double frame =
            (cursorX + m_scrollX) / std::max(0.0001, m_pixelsPerFrame);
        const double factor = std::pow(1.2, delta / 120.0);
        setPixelsPerFrame(m_pixelsPerFrame * factor);
        setScrollX(int(std::lround(frame * m_pixelsPerFrame - cursorX)));
        event->accept();
        return;
    }

    int delta = event->angleDelta().x();
    if (delta == 0)
        delta = event->angleDelta().y();
    if (delta == 0)
    {
        event->ignore();
        return;
    }

    setScrollX(m_scrollX - delta);
    event->accept();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hScroll)
    {
        m_hScroll->setGeometry(0, height() - kScrollBarHeight, width(),
                               kScrollBarHeight);
    }
    updateScrollBar();
}

int TimelineWidget::assetDurationFrames(const assets::StoredAsset& asset) const
{
    int frames =
        asset.metadata.value(QStringLiteral("durationFrames")).toInt(0);
    if (frames <= 0 && m_document)
        frames = m_document->project().settings.fps;
    return std::max(1, frames);
}

int TimelineWidget::dropRowFor(int y, int startFrame, int endFrame) const
{
    const QRect body = bodyRect();
    int candidate = -1;
    if (!body.isEmpty() && y >= body.top() && y <= body.bottom())
        candidate = std::max(0, (body.bottom() - y) / kRowHeight);

    int highestUsed = -1;
    int highestInRange = -1;
    const QVector<Uuid> siblings = visibleEntities();
    for (const Uuid& uuid : siblings)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            continue;
        const model::Entity& entity = it.value();
        highestUsed = std::max(highestUsed, std::max(0, entity.row));
        if (rangesOverlap(entity.start, entity.end, startFrame, endFrame))
            highestInRange = std::max(highestInRange, std::max(0, entity.row));
    }

    // Blank rows above the existing row space (and drops outside the body)
    // use the model default: highest occupied row in [start, end) + 1.
    const int autoRow = std::max(0, highestInRange + 1);
    if (candidate < 0 || candidate > highestUsed)
        return autoRow;

    // Keep the row under the cursor, unless a sibling already overlaps this
    // time range there; then fall back to the next free row.
    for (const Uuid& uuid : siblings)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            continue;
        const model::Entity& entity = it.value();
        if (std::max(0, entity.row) == candidate
            && rangesOverlap(entity.start, entity.end, startFrame, endFrame))
            return autoRow;
    }
    return candidate;
}

QVector<Uuid> TimelineWidget::createEntitiesForAsset(
    const assets::StoredAsset& asset, int startFrame, int row)
{
    QVector<Uuid> created;
    if (!m_document)
        return created;

    const int start = std::max(0, startFrame);
    const int duration = assetDurationFrames(asset);
    const QString name = asset.name.isEmpty() ? asset.hash : asset.name;

    model::Entity main;
    main.uuid = model::newUuid();
    main.name = name;
    main.parentUuid = m_parentUuid;
    main.start = start;
    main.end = start + duration;
    main.row = std::max(0, row);
    main.content.kind = ContentKind::Asset;
    main.content.asset.hash = asset.hash;
    main.content.asset.type = asset.type;
    main.content.asset.sourceIn = 0;
    main.content.asset.volume = 1.0;
    m_document->mutableProject().entities.insert(main.uuid, main);
    created.append(main.uuid);

    // Video imports split their audio into a separate asset. Recreate the
    // paired entity on the row above, exactly like the importer does.
    const QString audioHash =
        asset.metadata.value(QStringLiteral("audioHash")).toString();
    if (asset.type == AssetType::Video && !audioHash.isEmpty())
    {
        model::Entity audio;
        audio.uuid = model::newUuid();
        audio.name = name + QStringLiteral(" 音频");
        audio.parentUuid = m_parentUuid;
        audio.start = main.start;
        audio.end = main.end;
        audio.row = main.row + 1;
        audio.content.kind = ContentKind::Asset;
        audio.content.asset.hash = audioHash;
        audio.content.asset.type = AssetType::Audio;
        audio.content.asset.sourceIn = 0;
        audio.content.asset.volume = 1.0;
        m_document->mutableProject().entities.insert(audio.uuid, audio);
        created.append(audio.uuid);
    }

    return created;
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_document && hasAssetMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (m_document && hasAssetMime(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineWidget::dropEvent(QDropEvent* event)
{
    if (!m_document || !hasAssetMime(event->mimeData()))
    {
        event->ignore();
        return;
    }

    const QString hash =
        QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kAssetMimeType)))
            .trimmed();
    if (hash.isEmpty())
    {
        event->ignore();
        return;
    }

    const auto asset = m_document->assets().get(hash);
    if (!asset)
    {
        emit statusMessage(tr("素材缺失：%1").arg(hash));
        event->ignore();
        return;
    }

    const QPoint pos = event->position().toPoint();
    const int start = m_parentUuid.isEmpty()
                          ? std::max(0, frameFromX(pos.x()))
                          : frameFromX(pos.x());
    const int end = start + assetDurationFrames(*asset);

    if (!m_parentUuid.isEmpty())
    {
        const auto parent =
            m_document->project().entities.constFind(m_parentUuid);
        if (parent == m_document->project().entities.constEnd())
        {
            emit statusMessage(tr("父实体不存在，不能拖入素材"));
            event->ignore();
            return;
        }
        if (start < parent.value().start || end > parent.value().end)
        {
            emit statusMessage(
                tr("素材范围 [%1, %2) 超出父实体 [%3, %4)")
                    .arg(start)
                    .arg(end)
                    .arg(parent.value().start)
                    .arg(parent.value().end));
            event->ignore();
            return;
        }
    }

    const int row = dropRowFor(pos.y(), start, end);
    const QVector<Uuid> created = createEntitiesForAsset(*asset, start, row);
    if (created.isEmpty())
    {
        event->ignore();
        return;
    }

    m_document->setSelection(created.first());
    const QString label = asset->name.isEmpty() ? hash : asset->name;
    m_document->commitEntities(
        created, QStringLiteral("pvf: add asset \"%1\"").arg(label));
    updateScrollBar();
    update();
    event->acceptProposedAction();
}

bool TimelineWidget::trimEntityTree(const Uuid& uuid, int newStart, int newEnd,
                                    QVector<Uuid>& upserts,
                                    QVector<Uuid>& deletes, QString* error)
{
    if (!m_document)
        return false;

    model::Project trimmed;
    QVector<Uuid> localUpserts;
    QVector<Uuid> localDeletes;
    if (!applyEntityRange(m_document->project(), m_document->assets(), uuid,
                          newStart, newEnd, &trimmed, &localUpserts,
                          &localDeletes, error))
        return false;

    m_document->mutableProject() = std::move(trimmed);
    upserts = localUpserts;
    deletes = localDeletes;
    return true;
}

void TimelineWidget::applyKeyframeDrag(const QPoint& pos)
{
    if (!m_document || m_dragEntity.isEmpty() || m_dragKeyFrames.isEmpty())
        return;

    auto it = m_document->mutableProject().entities.find(m_dragEntity);
    if (it == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = it.value();
    const int duration = entity.duration();
    if (duration <= 0)
        return;
    if (entityIsLocked(m_document, entity.uuid))
        return;

    int minFrame = INT_MAX;
    int maxFrame = INT_MIN;
    for (int frame : m_dragKeyFrames)
    {
        minFrame = std::min(minFrame, frame);
        maxFrame = std::max(maxFrame, frame);
    }

    int delta = frameFromX(pos.x()) - m_dragKeyPressFrame;
    delta = std::clamp(delta, -minFrame, duration - 1 - maxFrame);
    if (delta == m_dragKeyDelta)
        return;

    // Every preview starts from the press snapshot, so dragging back and forth
    // never accumulates rounding or overwritten keys.
    entity.tracks.clear();
    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd(); ++trackIt)
    {
        model::Track track;
        track.keys = trackIt.value();
        entity.tracks.insert(trackIt.key(), track);
    }

    bool conflict = false;
    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd() && !conflict; ++trackIt)
    {
        for (const model::Keyframe& key : trackIt.value())
        {
            if (!m_dragKeyFrames.contains(key.frame))
                continue;
            const int targetFrame = key.frame + delta;
            if (solverStartKey(entity, trackIt.key(), key.frame)
                || solverStartKey(entity, trackIt.key(), targetFrame)
                || trackBlockedAt(entity, trackIt.key(), targetFrame))
            {
                conflict = true;
                break;
            }
        }
    }

    m_dragKeyDelta = delta;
    m_dragKeyConflict = conflict;
    if (conflict)
    {
        m_dragChanged = false;
        update();
        return;
    }

    for (auto trackIt = m_dragKeyTracks.constBegin();
         trackIt != m_dragKeyTracks.constEnd(); ++trackIt)
    {
        QSet<int> destinations;
        QVector<model::Keyframe> kept;
        QVector<model::Keyframe> moved;
        kept.reserve(trackIt.value().size());
        moved.reserve(trackIt.value().size());

        for (const model::Keyframe& key : trackIt.value())
        {
            if (!m_dragKeyFrames.contains(key.frame))
            {
                kept.append(key);
                continue;
            }
            model::Keyframe shifted = key;
            shifted.frame = key.frame + delta;
            moved.append(shifted);
            destinations.insert(shifted.frame);
        }
        if (moved.isEmpty())
            continue;

        // Moving onto a frame that already has a key on the same track
        // overwrites it, matching normal keyframe editing behaviour.
        QVector<model::Keyframe> finalKeys;
        finalKeys.reserve(kept.size() + moved.size());
        for (const model::Keyframe& key : kept)
        {
            if (!destinations.contains(key.frame))
                finalKeys.append(key);
        }
        finalKeys += moved;
        std::sort(finalKeys.begin(), finalKeys.end(),
                  [](const model::Keyframe& a, const model::Keyframe& b) {
                      return a.frame < b.frame;
                  });

        entity.tracks[trackIt.key()].keys = finalKeys;
    }

    QSet<int> shiftedSelection;
    for (int frame : m_dragKeyFrames)
        shiftedSelection.insert(frame + delta);
    m_selectedKeyframes = shiftedSelection;
    m_keyframeSelectionUuid = m_dragEntity;
    m_dragChanged = delta != 0;
    update();
}

void TimelineWidget::applyDrag(const QPoint& pos)
{
    if (!m_document || m_dragEntity.isEmpty())
        return;

    auto it = m_document->mutableProject().entities.find(m_dragEntity);
    if (it == m_document->mutableProject().entities.end())
        return;

    model::Entity& entity = it.value();
    const int delta = frameFromX(pos.x()) - frameFromX(m_dragPressPos.x());
    const int duration = std::max(1, m_dragOriginalEnd - m_dragOriginalStart);

    int minStart = 0;
    int maxEnd = INT_MAX;
    if (!entity.parentUuid.isEmpty())
    {
        const auto parent =
            m_document->project().entities.constFind(entity.parentUuid);
        if (parent != m_document->project().entities.constEnd())
        {
            minStart = parent.value().start;
            maxEnd = parent.value().end;
        }
    }

    switch (m_dragMode)
    {
    case DragMode::Move:
    {
        const int lastStart = maxEnd == INT_MAX ? INT_MAX : maxEnd - duration;
        const int newStart =
            std::clamp(m_dragOriginalStart + delta, minStart,
                       std::max(minStart, lastStart));
        int appliedDelta = newStart - m_dragOriginalStart;

        // Never push any entity of the moved subtree before frame 0.
        int earliestStart = INT_MAX;
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto range = m_dragOriginalRanges.constFind(uuid);
            if (range != m_dragOriginalRanges.constEnd())
                earliestStart = std::min(earliestStart, range.value().first);
        }
        if (earliestStart != INT_MAX)
            appliedDelta = std::max(appliedDelta, -earliestStart);

        bool changed = false;
        for (const Uuid& uuid : m_dragMoveUuids)
        {
            const auto range = m_dragOriginalRanges.constFind(uuid);
            if (range == m_dragOriginalRanges.constEnd())
                continue;

            auto entityIt = m_document->mutableProject().entities.find(uuid);
            if (entityIt == m_document->mutableProject().entities.end())
                continue;

            const int shiftedStart = range.value().first + appliedDelta;
            const int shiftedEnd = range.value().second + appliedDelta;
            if (entityIt.value().start != shiftedStart
                || entityIt.value().end != shiftedEnd)
            {
                entityIt.value().start = shiftedStart;
                entityIt.value().end = shiftedEnd;
                changed = true;
            }
        }
        if (changed)
            m_dragChanged = true;
        break;
    }
    case DragMode::TrimStart:
    {
        const int lastStart = std::min(m_dragOriginalEnd - 1,
                                       maxEnd == INT_MAX ? INT_MAX : maxEnd - 1);
        const int newStart = std::clamp(m_dragOriginalStart + delta, minStart,
                                        std::max(minStart, lastStart));
        if (newStart != entity.start)
        {
            entity.start = newStart;
            m_dragChanged = true;
        }
        // Preview only moves the visible edge. Keyframes, sourceIn and solver
        // segments are trimmed destructively once, on release.
        break;
    }
    case DragMode::TrimEnd:
    {
        const int firstEnd = m_dragOriginalStart + 1;
        const int newEnd = std::max(
            firstEnd, std::min(m_dragOriginalEnd + delta,
                               maxEnd == INT_MAX ? INT_MAX : maxEnd));
        if (newEnd != entity.end)
        {
            entity.end = newEnd;
            m_dragChanged = true;
        }
        // Preview only; see TrimStart above.
        break;
    }
    default:
        break;
    }

    if (m_dragChanged)
    {
        updateScrollBar();
        update();
    }
}

void TimelineWidget::finishDrag()
{
    const DragMode mode = m_dragMode;
    const Uuid uuid = m_dragEntity;
    const bool changed = m_dragChanged;
    const QVector<Uuid> moveUuids = m_dragMoveUuids;
    const QHash<Uuid, QPair<int, int>> moveRanges = m_dragOriginalRanges;
    const model::Project trimSnapshot = m_dragProjectSnapshot;
    const bool hasTrimSnapshot = m_dragHasSnapshot;
    const QHash<QString, QVector<model::Keyframe>> keyTracks = m_dragKeyTracks;
    const QSet<int> keyFrames = m_dragKeyFrames;
    const int keyDelta = m_dragKeyDelta;
    const bool keyConflict = m_dragKeyConflict;
    const QString dragMarkerId = m_dragMarkerId;
    const QVector<model::Marker> markerSnapshot = m_dragMarkerSnapshot;

    int previewStart = 0;
    int previewEnd = 0;
    bool hasPreview = false;
    if (m_document && !uuid.isEmpty()
        && (mode == DragMode::TrimStart || mode == DragMode::TrimEnd))
    {
        const auto preview = m_document->project().entities.constFind(uuid);
        if (preview != m_document->project().entities.constEnd())
        {
            previewStart = preview.value().start;
            previewEnd = preview.value().end;
            hasPreview = true;
        }
    }

    m_dragMode = DragMode::None;
    m_dragEntity.clear();
    m_dragMoveUuids.clear();
    m_dragOriginalRanges.clear();
    m_dragProjectSnapshot = model::Project();
    m_dragHasSnapshot = false;
    m_dragChanged = false;
    m_dragKeyTracks.clear();
    m_dragKeyFrames.clear();
    m_dragKeyDelta = 0;
    m_dragKeyConflict = false;
    m_dragMarkerId.clear();
    m_dragMarkerSnapshot.clear();

    if (!m_document)
        return;

    if (mode == DragMode::Marker)
    {
        if (!changed || dragMarkerId.isEmpty())
            return;
        const int index = markerIndex(dragMarkerId);
        if (index < 0)
            return;

        const QString title = m_document->project().markers.at(index).title;
        QString error;
        commitMarkers(markerSnapshot,
                      tr("pvf: move marker \"%1\"")
                          .arg(title.isEmpty() ? dragMarkerId : title),
                      &error);
        return;
    }

    if (uuid.isEmpty())
        return;

    if (mode == DragMode::Keyframes)
    {
        auto it = m_document->mutableProject().entities.find(uuid);
        if (it == m_document->mutableProject().entities.end())
            return;

        auto restoreTracks = [this, &uuid, &keyTracks]() {
            auto restoreIt = m_document->mutableProject().entities.find(uuid);
            if (restoreIt == m_document->mutableProject().entities.end())
                return;
            restoreIt.value().tracks.clear();
            for (auto trackIt = keyTracks.constBegin();
                 trackIt != keyTracks.constEnd(); ++trackIt)
            {
                model::Track track;
                track.keys = trackIt.value();
                restoreIt.value().tracks.insert(trackIt.key(), track);
            }
        };

        if (keyConflict)
        {
            restoreTracks();
            m_keyframeSelectionUuid = uuid;
            m_selectedKeyframes = keyFrames;
            emit statusMessage(
                tr("关键帧不能移动求解段开始帧，也不能移入求解段驱动的区间"));
            update();
            return;
        }
        if (!changed || keyDelta == 0)
        {
            update();
            return;
        }

        const QString name = entityDisplayName(it.value());
        QString error;
        const bool ok = m_document->commitEntity(
            uuid, QStringLiteral("pvf: move keyframes \"%1\"").arg(name),
            &error);
        if (!ok)
        {
            restoreTracks();
            m_keyframeSelectionUuid = uuid;
            m_selectedKeyframes = keyFrames;
            emit statusMessage(error.isEmpty()
                                   ? tr("关键帧移动失败")
                                   : tr("关键帧移动失败：%1").arg(error));
        }
        update();
        return;
    }

    if (!changed)
        return;

    if (mode == DragMode::Move)
    {
        const auto it = m_document->project().entities.constFind(uuid);
        if (it == m_document->project().entities.constEnd())
            return;
        const QString name = it.value().name.isEmpty() ? uuid : it.value().name;

        QVector<Uuid> commitUuids;
        bool differs = false;
        for (const Uuid& moved : moveUuids)
        {
            const auto range = moveRanges.constFind(moved);
            const auto entity =
                m_document->project().entities.constFind(moved);
            if (range == moveRanges.constEnd()
                || entity == m_document->project().entities.constEnd())
                continue;

            commitUuids.append(moved);
            if (entity.value().start != range.value().first
                || entity.value().end != range.value().second)
                differs = true;
        }
        if (!differs || commitUuids.isEmpty())
            return;

        m_document->commitEntities(
            commitUuids, QStringLiteral("pvf: move entity \"%1\"").arg(name));
        updateScrollBar();
        update();
        return;
    }

    if (mode != DragMode::TrimStart && mode != DragMode::TrimEnd)
        return;
    if (!hasTrimSnapshot || !hasPreview)
        return;
    if (previewStart == m_dragOriginalStart && previewEnd == m_dragOriginalEnd)
        return;

    const auto snapshotEntity = trimSnapshot.entities.constFind(uuid);
    if (snapshotEntity == trimSnapshot.entities.constEnd())
        return;
    const QString name =
        snapshotEntity.value().name.isEmpty() ? uuid : snapshotEntity.value().name;

    // Rebuild the destructive trim from the pre-drag snapshot; the mouse-move
    // preview only changed start/end in memory.
    m_document->mutableProject() = trimSnapshot;

    QVector<Uuid> upserts;
    QVector<Uuid> deletes;
    QString error;
    if (!trimEntityTree(uuid, previewStart, previewEnd, upserts, deletes,
                        &error))
    {
        m_document->mutableProject() = trimSnapshot;
        emit statusMessage(
            error.isEmpty() ? tr("裁剪失败")
                            : tr("裁剪失败：%1").arg(error));
        updateScrollBar();
        update();
        return;
    }

    const bool committed = m_document->commitChanges(
        upserts, deletes,
        QStringLiteral("pvf: trim entity \"%1\"").arg(name), &error);
    if (!committed)
    {
        m_document->mutableProject() = trimSnapshot;
        emit statusMessage(
            error.isEmpty() ? tr("裁剪提交失败")
                            : tr("裁剪提交失败：%1").arg(error));
    }

    updateScrollBar();
    update();
}

} // namespace pvf::next
