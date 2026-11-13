#include "ui/ViewportInternal.h"

#include "anim/Anim.h"
#include "model/Model.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace pvf::ui::detail {

double distanceToSegment(const QPointF& point, const QPointF& a,
                         const QPointF& b)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared < 1e-9)
        return std::hypot(point.x() - a.x(), point.y() - a.y());
    const double t = std::clamp(((point.x() - a.x()) * ab.x()
                                 + (point.y() - a.y()) * ab.y())
                                    / lengthSquared,
                                0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(point.x() - closest.x(), point.y() - closest.y());
}

QVariantMap textStyleData(const model::TextContent& text)
{
    QVariantMap data;
    data.insert(QStringLiteral("text"), text.text);
    data.insert(QStringLiteral("font"), text.fontFamily);
    data.insert(QStringLiteral("size"), text.pixelSize);
    data.insert(QStringLiteral("color"), QColor(text.color));
    data.insert(QStringLiteral("textStrokeColor"), QColor(text.strokeColor));
    data.insert(QStringLiteral("textStrokeWidth"), text.strokeWidth);
    data.insert(QStringLiteral("align"), text.align);
    data.insert(QStringLiteral("lineHeight"), text.lineHeight);
    data.insert(QStringLiteral("letterSpacing"), text.letterSpacing);
    data.insert(QStringLiteral("padding"), text.padding);
    data.insert(QStringLiteral("boxWidth"), text.boxWidth);
    data.insert(QStringLiteral("boxHeight"), text.boxHeight);
    data.insert(QStringLiteral("bold"), text.bold);
    data.insert(QStringLiteral("italic"), text.italic);
    data.insert(QStringLiteral("underline"), text.underline);
    data.insert(QStringLiteral("vertical"), text.vertical);
    data.insert(QStringLiteral("verticalRtl"), text.verticalRtl);
    return data;
}

QPointF localCorner(const gizmo::Bounds& bounds, gizmo::Handle handle)
{
    switch (handle)
    {
    case gizmo::Handle::ScaleTopLeft:
    case gizmo::Handle::RotateTopLeft:
        return QPointF(bounds.left, bounds.top);
    case gizmo::Handle::ScaleTopRight:
    case gizmo::Handle::RotateTopRight:
        return QPointF(bounds.right, bounds.top);
    case gizmo::Handle::ScaleBottomRight:
    case gizmo::Handle::RotateBottomRight:
        return QPointF(bounds.right, bounds.bottom);
    case gizmo::Handle::ScaleBottomLeft:
    case gizmo::Handle::RotateBottomLeft:
        return QPointF(bounds.left, bounds.bottom);
    default:
        return gizmo::boundsCenter(bounds);
    }
}

QPointF unitVector(QPointF vector)
{
    const double length = std::hypot(vector.x(), vector.y());
    return length > 1e-9 ? vector / length : QPointF();
}

QPointF localCornerPoint(const gizmo::Bounds& bounds, int corner)
{
    switch (corner)
    {
    case 0:
        return QPointF(bounds.left, bounds.top);
    case 1:
        return QPointF(bounds.right, bounds.top);
    case 2:
        return QPointF(bounds.right, bounds.bottom);
    default:
        return QPointF(bounds.left, bounds.bottom);
    }
}

QPolygonF gizmoBoxWidget(const gizmo::Transform& transform,
                         const gizmo::Bounds& bounds,
                         const QTransform& parentToWidget)
{
    QPolygonF box;
    box << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.left, bounds.top)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.right, bounds.top)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.right, bounds.bottom)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.left, bounds.bottom)));
    return box;
}

QPointF cornerInwardDirection(const QPolygonF& box, int corner)
{
    if (box.size() < 4)
        return QPointF();

    const QPointF c = box.at(corner);
    const QPointF next = box.at((corner + 1) % 4);
    const QPointF prev = box.at((corner + 3) % 4);
    QPointF inward = unitVector(next - c) + unitVector(prev - c);
    if (std::hypot(inward.x(), inward.y()) > 1e-9)
        return unitVector(inward);

    const QPointF center = (box.at(0) + box.at(1) + box.at(2) + box.at(3)) / 4.0;
    return unitVector(center - c);
}

double rotateGlyphAngle(const QPolygonF& box, int corner)
{
    const QPointF inward = cornerInwardDirection(box, corner);
    return qRadiansToDegrees(std::atan2(inward.y(), inward.x())) + 225.0;
}

bool transformsEqual(const gizmo::Transform& a, const gizmo::Transform& b)
{
    constexpr double epsilon = 1e-9;
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon
           && std::abs(a.rotation - b.rotation) <= epsilon
           && std::abs(a.scaleX - b.scaleX) <= epsilon
           && std::abs(a.scaleY - b.scaleY) <= epsilon
           && std::abs(a.pivotX - b.pivotX) <= epsilon
           && std::abs(a.pivotY - b.pivotY) <= epsilon;
}

double trackValueOr(const model::Entity& entity, const char* property,
                    double relativeFrame, double fallback)
{
    const auto it = entity.tracks.constFind(QLatin1String(property));
    if (it == entity.tracks.constEnd())
        return fallback;
    return model::evaluateTrack(it.value(), relativeFrame).value_or(fallback);
}

bool setStaticGizmoValue(model::Entity& entity, const QString& property,
                         double value)
{
    model::Transform& transform = entity.transform;
    if (property == QLatin1String("x"))
    {
        transform.x = value;
        return true;
    }
    if (property == QLatin1String("y"))
    {
        transform.y = value;
        return true;
    }
    if (property == QLatin1String("rotation"))
    {
        transform.rotation = value;
        return true;
    }
    if (property == QLatin1String("scaleX"))
    {
        transform.scaleX = value;
        return true;
    }
    if (property == QLatin1String("scaleY"))
    {
        transform.scaleY = value;
        return true;
    }
    if (property == QLatin1String("pivotX"))
    {
        transform.pivotX = value;
        return true;
    }
    if (property == QLatin1String("pivotY"))
    {
        transform.pivotY = value;
        return true;
    }
    return false;
}

void setTrackKeyValue(model::Entity& entity, const QString& property, int frame,
                      double value)
{
    model::Track& track = entity.tracks[property];
    for (model::Keyframe& keyframe : track.keys)
    {
        if (keyframe.frame == frame)
        {
            keyframe.value = value;
            return;
        }
    }
    model::Keyframe keyframe;
    keyframe.frame = frame;
    keyframe.value = value;
    track.keys.append(keyframe);
}

bool solverDrivesProperty(const model::Entity& entity, const QString& property,
                          int relativeFrame)
{
    const bool position = property == QLatin1String("x")
                          || property == QLatin1String("y");
    const bool rotation = property == QLatin1String("rotation");
    if (!position && !rotation)
        return false;

    const model::SolverSegment* segment = anim::solverAt(entity, relativeFrame);
    if (!segment)
        return false;
    if (segment->effect == SolverEffect::Spin)
        return rotation;
    return position;
}

QPointF rotateVector(QPointF vector, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    return QPointF(vector.x() * std::cos(radians) - vector.y() * std::sin(radians),
                   vector.x() * std::sin(radians) + vector.y() * std::cos(radians));
}

std::optional<double> nearestFeatureSnap(const std::array<double, 3>& features,
                                         const QVector<double>& targets,
                                         double threshold)
{
    std::optional<double> best;
    double bestDistance = threshold;
    for (double target : targets)
    {
        for (double feature : features)
        {
            const double distance = std::abs(target - feature);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = target - feature;
            }
        }
    }
    return best;
}

void drawArrowLine(QPainter& painter, QPointF from, QPointF to,
                   const QColor& color)
{
    QLineF line(from, to);
    if (line.length() < 8.0)
        return;
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(line);

    const QPointF direction = (to - from) / line.length();
    const QPointF perpendicular(-direction.y(), direction.x());
    QPolygonF head;
    head << to << to - direction * 8.0 + perpendicular * 4.0
         << to - direction * 8.0 - perpendicular * 4.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

double solverTrackValue(const model::Entity& entity, const QString& name,
                        int relativeFrame, double fallback)
{
    const auto it = entity.tracks.constFind(name);
    if (it == entity.tracks.constEnd())
        return fallback;
    return model::evaluateTrack(it.value(), relativeFrame).value_or(fallback);
}

double transformRotationOf(const QTransform& transform)
{
    return qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
}

QTransform plainSolverLocalTransform(const model::Entity& entity,
                                     int relativeFrame)
{
    const model::Transform& fallback = entity.transform;
    const double x = solverTrackValue(entity, QStringLiteral("x"),
                                      relativeFrame, fallback.x);
    const double y = solverTrackValue(entity, QStringLiteral("y"),
                                      relativeFrame, fallback.y);
    const double rotation = solverTrackValue(
        entity, QStringLiteral("rotation"), relativeFrame, fallback.rotation);
    const double scaleX = solverTrackValue(
        entity, QStringLiteral("scaleX"), relativeFrame, fallback.scaleX);
    const double scaleY = solverTrackValue(
        entity, QStringLiteral("scaleY"), relativeFrame, fallback.scaleY);
    const double pivotX = solverTrackValue(
        entity, QStringLiteral("pivotX"), relativeFrame, fallback.pivotX);
    const double pivotY = solverTrackValue(
        entity, QStringLiteral("pivotY"), relativeFrame, fallback.pivotY);

    QTransform local;
    local.translate(x, y);
    local.translate(pivotX, pivotY);
    local.rotate(rotation);
    local.scale(scaleX, scaleY);
    local.translate(-pivotX, -pivotY);
    return local;
}

} // namespace pvf::ui::detail
