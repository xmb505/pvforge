#include "next/timeline/TimelineWidget.h"

#include "TimelineWidgetInternal.h"
#include "audio2/Peaks.h"
#include "next/core/Document.h"

#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace pvf::next {

namespace {

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
    case SolverEffect::Spiral:
        return QColor(80, 190, 160);
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
        painter.drawText(rulerLabelRect(frame),
                         Qt::AlignHCenter | Qt::AlignVCenter,
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

    const int maxRow = (body.height() + m_rowScroll) / kRowHeight + 2;
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
            if (detail::trackBlockedAt(entity, it.key(), key.frame))
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

} // namespace pvf::next
