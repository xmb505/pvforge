#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "common/Types.h"
#include "model/Project.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

using ui::detail::findEntity;

QColor clipColor(EntityType type)
{
    switch (type)
    {
    case EntityType::Image:
        return QColor(74, 144, 226);
    case EntityType::Video:
        return QColor(123, 97, 255);
    case EntityType::Text:
        return QColor(242, 201, 76);
    case EntityType::Vector:
        return QColor(235, 87, 87);
    case EntityType::Camera:
        return QColor(186, 132, 255);
    case EntityType::Audio:
        return QColor(92, 204, 142);
    case EntityType::Folder:
        return QColor(132, 138, 148);
    case EntityType::ComicText:
        return QColor(242, 153, 74);
    case EntityType::RadialLines:
        return QColor(255, 120, 160);
    case EntityType::FlowLines:
        return QColor(88, 196, 221);
    case EntityType::BrandWall:
        return QColor(180, 160, 255);
    case EntityType::HalftoneDots:
        return QColor(140, 200, 160);
    case EntityType::HtmlLayer:
        return QColor(86, 204, 242);
    }
    return QColor(150, 150, 158);
}

const QPixmap& plusGlyphPixmap()
{
    static const QPixmap pixmap =
        ui::detail::themedGlyph({QStringLiteral("list-add"),
                                 QStringLiteral("list-add-symbolic")},
                                16, Qt::white);
    return pixmap;
}

} // namespace

void TimelinePanel::drawAudioClip(QPainter& p, const Entity& entity,
                                  const QRect& clip) const
{
    const int left = std::max(clip.left() + 2, 0);
    const int right = std::min(clip.right() - 2, width() - kScrollBarSize - 1);
    if (right <= left)
        return;
    const int center = clip.center().y();
    if (!entity.audio || entity.audio->peaks.isEmpty())
    {
        p.setPen(QPen(QColor(18, 60, 40, 120), 1.0, Qt::DashLine));
        p.drawLine(left, center, right, center);
        return;
    }

    const int fps = currentFps();
    const double trimStart =
        entity.data.value(QStringLiteral("trimStart"), 0.0).toDouble();
    const double x0 = xFromFrame(clipStartOf(entity));
    const QVector<float>& peaks = entity.audio->peaks;
    const int half = std::max(2, clip.height() / 2 - 4);

    p.save();
    p.setClipRect(clip.adjusted(2, 2, -2, -2));
    p.setPen(QPen(QColor(12, 54, 32, 190), 1.0));
    for (int x = left; x <= right; ++x)
    {
        const double local0 = (x - x0) / m_pixelsPerFrame;
        const double local1 = (x + 1 - x0) / m_pixelsPerFrame;
        const double source0 = trimStart + local0 / fps;
        const double source1 = trimStart + local1 / fps;
        int i0 = int(std::floor(source0 * 1000.0));
        int i1 = int(std::ceil(source1 * 1000.0));
        if (i1 <= 0 || i0 >= peaks.size())
            continue;
        i0 = std::clamp(i0, 0, int(peaks.size()) - 1);
        i1 = std::clamp(i1, i0 + 1, int(peaks.size()));
        float peak = 0.0f;
        for (int i = i0; i < i1; ++i)
            peak = std::max(peak, peaks[i]);
        const int amplitude = int(std::lround(peak * half));
        p.drawLine(x, center - amplitude, x, center + amplitude);
    }
    p.restore();
}

void TimelinePanel::paintEvent(QPaintEvent*)
{
    if (!sessionMode())
        return;
    clampScroll();

    const QPalette pal = QApplication::palette();
    const QColor base = pal.color(QPalette::Base);
    const QColor alt = pal.color(QPalette::AlternateBase);
    const QColor text = pal.color(QPalette::Text);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor accent = pal.color(QPalette::Highlight);
    const int right = width() - kScrollBarSize;
    const int bottom = bodyBottom();

    QPainter p(this);
    p.fillRect(rect(), base);
    p.fillRect(QRect(0, 0, width(), kRulerHeight), alt);

    p.setPen(mid);
    p.drawLine(0, kRulerHeight, width(), kRulerHeight);

    const int fps = currentFps();
    QFont small = p.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
    p.setFont(small);

    auto drawTick = [&](int frame, int tickHeight, const QColor& color) {
        const int x = xFromFrame(frame);
        if (x < nameWidth() || x > right)
            return;
        p.setPen(QPen(color, 1));
        p.drawLine(x, kRulerHeight - tickHeight, x, kRulerHeight);
    };
    auto drawLabel = [&](int frame, const QString& label, const QColor& color) {
        const int x = xFromFrame(frame);
        if (x < nameWidth() - 24 || x > right + 24)
            return;
        // Keep the header mode button from covering a ruler label.
        if (m_modeButton && m_modeButton->isVisible() && x > right - 62)
            return;
        p.setPen(color);
        // Labels sit centred on their tick, not offset to the right.
        p.drawText(QRect(x - 48, 1, 96, kRulerHeight - 5),
                   Qt::AlignHCenter | Qt::AlignVCenter, label);
    };

    const int leftFrame = frameFromX(nameWidth());
    if (m_displayFrames)
    {
        int tickStep = 0;
        int labelStep = 0;
        ui::detail::rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
        const int lastFrame = frameFromX(right) + tickStep;
        const int firstTick =
            ((leftFrame + tickStep - 1) / tickStep) * tickStep;

        for (int frame = firstTick; frame <= lastFrame; frame += tickStep)
        {
            const bool major = frame % labelStep == 0;
            drawTick(frame, major ? 9 : 4, major ? text : mid);
        }
        const int firstLabel =
            ((leftFrame + labelStep - 1) / labelStep) * labelStep;
        for (int frame = firstLabel; frame <= lastFrame; frame += labelStep)
            drawLabel(frame, QString::number(frame), text);
    }
    else
    {
        const ui::detail::SecondsGrid grid =
            ui::detail::secondsGrid(fps, m_pixelsPerFrame);
        const int lastFrame = frameFromX(right) + grid.cellFrames;
        const int firstCell =
            ((leftFrame + grid.cellFrames - 1) / grid.cellFrames)
            * grid.cellFrames;

        for (int frame = firstCell; frame <= lastFrame; frame += grid.cellFrames)
            drawTick(frame, 4, mid);

        if (fps * m_pixelsPerFrame >= 8.0)
        {
            const int firstSecond = ((leftFrame + fps - 1) / fps) * fps;
            for (int frame = firstSecond; frame <= lastFrame; frame += fps)
            {
                if (frame % grid.labelFrames != 0)
                    drawTick(frame, 7, mid);
            }
        }

        const int firstLabel =
            ((leftFrame + grid.labelFrames - 1) / grid.labelFrames)
            * grid.labelFrames;
        for (int frame = firstLabel; frame <= lastFrame;
             frame += grid.labelFrames)
        {
            drawTick(frame, 10, text);
            const double seconds = frame / double(fps);
            const bool whole = std::abs(seconds - std::round(seconds)) < 1e-9;
            const QString label =
                (whole ? QString::number(int(std::round(seconds)))
                       : QString::number(seconds, 'g', 3))
                + QStringLiteral("s");
            drawLabel(frame, label, text);
        }
    }

    for (int i = 0; i < m_snapshot.markers.size(); ++i)
    {
        const QRect marker = markerRect(i);
        if (marker.isEmpty() || marker.left() < nameWidth())
            continue;
        const TimelineMarkerView& view = m_snapshot.markers.at(i);
        QColor color = view.color.isValid() ? view.color
                                            : QColor(255, 190, 60);
        QColor lineColor = color;
        lineColor.setAlpha(90);
        p.setPen(QPen(lineColor, 1.0));
        p.drawLine(marker.left() + 3, marker.bottom(), marker.left() + 3,
                   bottom);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(QRectF(marker).adjusted(0.5, 0.5, -0.5, -0.5));
        if (!view.title.isEmpty())
        {
            p.setPen(QColor(24, 26, 30));
            p.drawText(marker.adjusted(6, 0, -3, 0),
                       Qt::AlignLeft | Qt::AlignVCenter, view.title);
        }
    }

    const QVector<EntityId> rows = rowIds();
    p.save();
    p.setClipRect(QRect(0, kRulerHeight, right, bottom - kRulerHeight));
    for (int row = 0; row < rows.size(); ++row)
    {
        const int top = rowTop(row);
        if (top + kRowHeight < kRulerHeight || top > bottom)
            continue;
        const Entity* entity = findEntity(activeEntities(), rows[row]);
        const QRect rowRect(0, top, right, kRowHeight);
        const bool rowSelected =
            entity
            && (entity->id == m_selected
                || m_entitySelection.contains(entity->id));
        if (rowSelected)
            p.fillRect(rowRect, QColor(accent.red(), accent.green(),
                                       accent.blue(), 60));
        else if (row % 2)
            p.fillRect(rowRect, QColor(alt.red(), alt.green(), alt.blue(), 90));
        p.setPen(QColor(mid.red(), mid.green(), mid.blue(), 70));
        p.drawLine(0, top + kRowHeight - 1, right, top + kRowHeight - 1);
        if (!entity)
            continue;

        // Clip-first: the entity name is the clip name. Eye / lock / select
        // keys stay available as small inline buttons when the clip is wide
        // enough; short clips fall back to the clip context menu.
        const bool hidden = entity->hidden;
        const bool locked = entity->locked;
        const QRect clip = clipRect(*entity);
        if (clip.isEmpty())
            continue;
        const bool showButtons = clip.width() >= kInlineButtonsMinWidth;
        if (showButtons)
        {
            makeEyeIcon(hidden).paint(&p, rowButtonRect(row, 0));
            makeLockIcon(locked).paint(&p, rowButtonRect(row, 1));
            makeKeyframeIcon().paint(&p, rowButtonRect(row, 2));
        }
        QColor color = clipColor(entity->type);
        const bool selected = rowSelected;
        const int alpha = hidden ? 80 : (selected ? 230 : 200);
        QColor fill = color;
        fill.setAlpha(alpha);
        p.setPen(QPen(color.darker(170), 1));
        p.setBrush(fill);
        // Flat, square corners: no gradient, rounding or white highlight.
        p.drawRect(clip.adjusted(0, 0, -1, -1));
        if (selected)
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(accent, 2));
            p.drawRect(clip.adjusted(1, 1, -2, -2));
        }
        if (sessionMode() && isEntityLocked(entity->id))
        {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 150), 1, Qt::DashLine));
            p.drawRect(clip.adjusted(2, 2, -3, -3));
        }

        // Solver segments get a dedicated colour band clipped to the real
        // Result::stopFrame, not the structural clip end.  The empty area
        // after the band means "stopped / holding".
        if (sessionMode())
        {
            const Uuid uuid = uuidForEntityId(entity->id);
            const auto modelIt =
                m_session->project().entities.constFind(uuid);
            if (modelIt != m_session->project().entities.constEnd())
            {
                for (const model::SolverSegment& solver : modelIt->solvers)
                {
                    const QRect band = solverBandRect(uuid, solver.id);
                    if (band.isEmpty())
                        continue;
                    QColor color(116, 206, 255);
                    if (solver.effect == SolverEffect::Orbit)
                        color = QColor(178, 136, 255);
                    else if (solver.effect == SolverEffect::Spin)
                        color = QColor(255, 214, 102);
                    else if (solver.effect == SolverEffect::BlackHole)
                        color = QColor(255, 120, 200);
                    else if (solver.effect == SolverEffect::Spiral)
                        color = QColor(80, 190, 160);
                    p.fillRect(band, QColor(color.red(), color.green(),
                                            color.blue(), 85));
                    p.setPen(QPen(color, 1.0));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(band.adjusted(0, 0, -1, -1));
                    p.setPen(QPen(QColor(20, 22, 26), 1.0));
                    p.setBrush(color.darker(140));
                    p.drawRect(QRect(band.left(), band.top(), 3,
                                     band.height()));
                    p.setBrush(color.lighter(125));
                    p.drawRect(QRect(band.right() - 2, band.top(), 3,
                                     band.height()));
                    if (band.width() > 52 && band.height() >= 14)
                    {
                        p.setPen(QColor(255, 255, 255, 215));
                        p.drawText(band.adjusted(6, 0, -4, 0),
                                   Qt::AlignVCenter | Qt::AlignLeft,
                                   QStringLiteral("求解"));
                    }
                }
            }
        }

        if (entity->isAudio())
            drawAudioClip(p, *entity, clip);

        const QRect plus = ui::detail::plusButtonRect(clip, right);
        if (!plus.isEmpty())
        {
            p.setPen(QPen(QColor(255, 255, 255, 125), 1));
            p.setBrush(QColor(0, 0, 0, 85));
            p.drawRect(plus);
            p.setPen(QPen(Qt::white, 1.5));
            const QPixmap& glyph = plusGlyphPixmap();
            if (!glyph.isNull())
            {
                p.drawPixmap(plus.topLeft(), glyph);
            }
            else
            {
                const QPointF plusCenter = QRectF(plus).center();
                p.drawLine(QPointF(plusCenter.x() - 4.0, plusCenter.y()),
                           QPointF(plusCenter.x() + 4.0, plusCenter.y()));
                p.drawLine(QPointF(plusCenter.x(), plusCenter.y() - 4.0),
                           QPointF(plusCenter.x(), plusCenter.y() + 4.0));
            }
        }

        // Entity name = clip name, drawn inside the clip. Time/duration and
        // solver count are secondary text on the right when the clip is wide
        // enough; the tooltip carries the full details for short clips.
        const int textLeft = clip.left() + 5
                             + (showButtons ? kButtonStrip + 2 : 0);
        const int textRight =
            plus.isEmpty() ? clip.right() - 5 : plus.left() - 4;
        const int startFrame = clipStartOf(*entity);
        const int endFrame = clipEndOf(*entity);
        QString rangeInfo =
            QStringLiteral("%1–%2 · %3f")
                .arg(startFrame)
                .arg(endFrame)
                .arg(endFrame - startFrame);
        if (entity->isAudio())
        {
            const double seconds =
                entity->data.value(QStringLiteral("assetDurationSeconds"))
                    .toDouble();
            if (seconds > 0.0)
                rangeInfo += QStringLiteral(" · %1s").arg(seconds, 0, 'f', 2);
        }
        if (sessionMode())
        {
            const Uuid uuid = uuidForEntityId(entity->id);
            const int solvers =
                m_session->project().entities.value(uuid).solvers.size();
            if (solvers > 0)
                rangeInfo += QStringLiteral(" · 求解%1").arg(solvers);
        }

        const bool showInfo = clip.width() >= 170;
        const int infoWidth =
            showInfo ? std::min(150, std::max(0, textRight - textLeft) / 2) : 0;
        const int nameWidthPx =
            std::max(0, textRight - textLeft - infoWidth - (showInfo ? 8 : 0));
        const QFontMetrics nameMetrics(font());
        const QString shownName =
            nameMetrics.elidedText(entity->name, Qt::ElideRight, nameWidthPx);

        QFont nameFont = font();
        p.setFont(nameFont);
        p.setPen((hidden || locked) ? dim : QColor(255, 255, 255, 235));
        p.drawText(QRect(textLeft, clip.top(), nameWidthPx, clip.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, shownName);
        p.setFont(small);
        if (showInfo && infoWidth > 20)
        {
            const QFontMetrics infoMetrics(p.font());
            const QString shownInfo =
                infoMetrics.elidedText(rangeInfo, Qt::ElideRight, infoWidth);
            p.setPen(QColor(255, 255, 255, 190));
            p.drawText(QRect(textRight - infoWidth, clip.top(), infoWidth,
                             clip.height()),
                       Qt::AlignVCenter | Qt::AlignRight, shownInfo);
        }

        const int centerY = clip.center().y();
        for (int keyFrame : keyframeFrames(*entity))
        {
            if (keyFrame < clipStartOf(*entity) || keyFrame >= clipEndOf(*entity))
                continue;
            const int x = xFromFrame(keyFrame);
            if (x < clip.left() - 6 || x > clip.right() + 6)
                continue;
            const bool keySelected =
                keyframeSelectedAt(entity->id, keyFrame);
            const double radius = keySelected ? 5.5 : 4.5;
            QPolygonF diamond;
            diamond << QPointF(x, centerY - radius)
                    << QPointF(x + radius, centerY)
                    << QPointF(x, centerY + radius)
                    << QPointF(x - radius, centerY);
            if (keySelected)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 95));
                p.drawEllipse(QPointF(x, centerY), 9.0, 9.0);
                p.setPen(QPen(Qt::white, 1.5));
                p.setBrush(accent);
            }
            else
            {
                p.setPen(QPen(QColor(14, 16, 20, 210), 1.2));
                p.setBrush(QColor(248, 250, 255));
            }
            p.drawPolygon(diamond);
        }
    }

    if (m_dragMode == DragMode::MoveClip && m_moved)
    {
        // activeEntities() is the live preview in both modes, so the marker
        // tracks the row under the pointer while dragging.
        const Entity* dragged = findEntity(activeEntities(), m_dragEntity);
        const QVector<EntityId> ids = rowIds();
        const int row = dragged ? ids.indexOf(dragged->id) : -1;
        if (row >= 0)
        {
            const int y = rowTop(row);
            p.setPen(QPen(accent.lighter(130), 2));
            p.drawLine(nameWidth(), y, right, y);
        }
    }

    const bool noEntities =
        std::none_of(rows.begin(), rows.end(),
                     [](EntityId id) { return id != kInvalidEntity; });
    if (noEntities)
    {
        p.setPen(dim);
        const QString emptyText =
            !searchFilter().isEmpty()
                ? QStringLiteral("没有匹配的对象")
                : (m_owner < 0
                       ? QStringLiteral("把素材拖到这里创建 clip")
                       : QStringLiteral("把素材拖到这里创建子 clip"));
        p.drawText(QRect(nameWidth(), kRulerHeight, right - nameWidth(),
                         bottom - kRulerHeight),
                   Qt::AlignCenter, emptyText);
    }

    if (m_dragMode == DragMode::BoxSelect)
    {
        const QRectF box = QRectF(m_boxStart, m_boxEnd)
                               .normalized()
                               .intersected(QRectF(nameWidth(), kRulerHeight,
                                                   right - nameWidth(),
                                                   bottom - kRulerHeight));
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 50));
        p.drawRect(box);
    }

    if (m_dropActive)
    {
        const int dropX = xFromFrame(m_dropFrame);
        if (dropX >= nameWidth() && dropX <= right)
        {
            p.setPen(QPen(accent.lighter(120), 2));
            p.drawLine(dropX, kRulerHeight, dropX, bottom);
            p.setPen(accent.lighter(140));
            p.drawText(QRect(dropX + 4, kRulerHeight + 2, 120, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("放到第 %1 帧").arg(m_dropFrame));
        }
    }

    const bool playheadInOwner =
        m_owner < 0 || (m_frame >= contentStart() && m_frame < contentEnd());
    const int playhead = xFromFrame(m_frame);
    if (playheadInOwner && playhead >= nameWidth() && playhead <= right)
    {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(playhead, 0, playhead, bottom);
        QPolygonF marker;
        marker << QPointF(playhead - 5, 0) << QPointF(playhead + 5, 0)
               << QPointF(playhead, 8);
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPolygon(marker);
    }
    p.restore();
}

} // namespace pvf
