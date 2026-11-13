#pragma once

// Shared free helpers for the TimelinePanel split.  They are detail
// implementation and must not be used outside src/ui/TimelinePanel*.cpp.

#include "ui/TimelineSnapshot.h"

#include <QColor>
#include <QPixmap>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace pvf::ui::detail {

QVector<EntityId> childIds(const QVector<Entity>& entities, EntityId parent);
Entity* findEntity(QVector<Entity>& entities, EntityId id);
const Entity* findEntity(const QVector<Entity>& entities, EntityId id);
void collectSubtree(const QVector<Entity>& entities, EntityId id,
                    QSet<EntityId>& out);

// Mine-imator's frame tick density table.  The tick interval is measured in
// frames and gets coarser as the timeline zooms out.
void rulerSteps(double pixelsPerFrame, int* tickStep, int* labelStep);

// Seconds-mode ruler grid.  Both intervals are measured in frames and always
// divide whole seconds evenly for the project frame rate.
struct SecondsGrid
{
    int labelFrames = 1;
    int cellFrames = 1;
};

SecondsGrid secondsGrid(int fps, double pixelsPerFrame);

QRect plusButtonRect(const QRect& clip, int visibleRight);

// Recolours a theme icon so clip buttons stay readable on dark clips.
QPixmap themedGlyph(const QStringList& names, int size, const QColor& color);

} // namespace pvf::ui::detail

