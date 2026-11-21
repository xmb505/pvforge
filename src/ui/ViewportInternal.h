#pragma once

// Shared free helpers for the Viewport split.  They are implementation detail
// and must not be used outside src/ui/Viewport*.cpp.

#include "gizmo/Gizmo.h"
#include "model/Entity.h"

#include <QColor>
#include <QCursor>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QVariantMap>
#include <QVector>

#include <array>
#include <optional>

class QPainter;

namespace pvf::ui::detail {

constexpr double kSessionSnapAngleTolerance = 3.0;

// Audio assets are timeline-only: they have no visual shape on the stage, so
// no selection gizmo is drawn and no stage hit test may react to them.
inline bool isAudioEntity(const model::Entity& entity)
{
    return entity.content.kind == ContentKind::Asset
           && entity.content.asset.type == AssetType::Audio;
}

// True when the entity owns the on-stage entity gizmo and its alignment-snap
// footprint.  Cameras are edited through their own viewfinder overlay and
// audio has no visual shape, so both are excluded from the shared gizmo,
// picking and stage-snap paths.
inline bool hasStageGizmo(const model::Entity& entity)
{
    return entity.content.kind != ContentKind::Camera && !isAudioEntity(entity);
}

double distanceToSegment(const QPointF& point, const QPointF& a,
                         const QPointF& b);

QVariantMap textStyleData(const model::TextContent& text);

QPointF localCorner(const gizmo::Bounds& bounds, gizmo::Handle handle);
QPointF unitVector(QPointF vector);

// Index order matches the gizmo box: 0 = local top-left, 1 = top-right,
// 2 = bottom-right, 3 = bottom-left (model space is Y-up).
QPointF localCornerPoint(const gizmo::Bounds& bounds, int corner);

// The four transformed local corners in widget space, in the same order as
// localCornerPoint().  Drawing, hit testing and tests all use this quad.
QPolygonF gizmoBoxWidget(const gizmo::Transform& transform,
                         const gizmo::Bounds& bounds,
                         const QTransform& parentToWidget);

// Unit direction from a quad corner towards the inside of the box.
QPointF cornerInwardDirection(const QPolygonF& box, int corner);

// Inkscape's carrow glyph angle: anchor * 45 + 225.
double rotateGlyphAngle(const QPolygonF& box, int corner);

QPainterPath carrowGlyphPath();
void drawRotateGlyph(QPainter& painter, QPointF center, double angleDegrees,
                     bool highlighted);
QCursor rotateCursor();
void drawRotationOriginMarker(QPainter& painter, QPointF center);
void drawRotationGripMarker(QPainter& painter, QPointF center);
void drawArrowLine(QPainter& painter, QPointF from, QPointF to,
                   const QColor& color);

bool transformsEqual(const gizmo::Transform& a, const gizmo::Transform& b);

double trackValueOr(const model::Entity& entity, const char* property,
                    double relativeFrame, double fallback);
bool setStaticGizmoValue(model::Entity& entity, const QString& property,
                         double value);
bool solverDrivesProperty(const model::Entity& entity, const QString& property,
                          int relativeFrame);

QPointF rotateVector(QPointF vector, double degrees);
std::optional<double> nearestFeatureSnap(const std::array<double, 3>& features,
                                         const QVector<double>& targets,
                                         double threshold);

double solverTrackValue(const model::Entity& entity, const QString& name,
                        int relativeFrame, double fallback);
double transformRotationOf(const QTransform& transform);
QTransform plainSolverLocalTransform(const model::Entity& entity,
                                     int relativeFrame);

} // namespace pvf::ui::detail
