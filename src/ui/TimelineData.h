#pragma once

#include "common/Types.h"
#include "core/Project.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace pvf {

// Read-only display DTO built from the new entity model.  The timeline keeps
// using the legacy Entity struct for painting, but in read-only mode these
// copies are never written back: editing gestures are disabled.
struct TimelineMarkerView
{
    QString id;
    int frame = 0;
    QString title;
    QString note;
    QColor color = QColor(255, 190, 60);
};

struct TimelineSnapshot
{
    QVector<Entity> entities;   // parent before children, legacy Entity DTOs
    QVector<TimelineMarkerView> markers;
    int fps = 30;
    int playhead = 0;
    int contentEnd = 0;         // furthest root entity end
};

} // namespace pvf
