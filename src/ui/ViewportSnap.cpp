#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "next/app/ProjectDocument.h"
#include "render2/Render.h"

#include <algorithm>
#include <cmath>

namespace pvf {

void Viewport::collectSessionSnapTargets(const Uuid& uuid)
{
    m_sessionSnapTargetsX.clear();
    m_sessionSnapTargetsY.clear();
    m_sessionDragSnapRoot = false;
    m_sessionDragStartWorldBounds = QRectF();
    if (!m_session || uuid.isEmpty())
        return;

    const next::AppSettings& settings = m_session->appSettings();
    const model::Project& project = m_session->project();
    const auto draggedIt = project.entities.constFind(uuid);
    if (draggedIt == project.entities.constEnd())
        return;

    const model::Entity& dragged = draggedIt.value();
    m_sessionDragSnapRoot = dragged.parentUuid.isEmpty()
                            || !project.entities.contains(dragged.parentUuid);

    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    const anim::Pose draggedPose = anim::evaluateEntity(
        project, uuid, m_session->playhead(), world, cache);
    const gizmo::Bounds bounds = sessionGizmoBounds(dragged);
    QPolygonF box;
    box << draggedPose.world.map(QPointF(bounds.left, bounds.top))
        << draggedPose.world.map(QPointF(bounds.right, bounds.top))
        << draggedPose.world.map(QPointF(bounds.right, bounds.bottom))
        << draggedPose.world.map(QPointF(bounds.left, bounds.bottom));
    m_sessionDragStartWorldBounds = box.boundingRect();

    const int frame = m_session->playhead();
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& entity = it.value();
        if (it.key() == uuid || entity.content.kind == ContentKind::Camera)
            continue;
        if (frame < entity.start || frame >= entity.end)
            continue;

        bool descendant = false;
        for (Uuid parent = entity.parentUuid; !parent.isEmpty();)
        {
            if (parent == uuid)
            {
                descendant = true;
                break;
            }
            const auto parentIt = project.entities.constFind(parent);
            if (parentIt == project.entities.constEnd())
                break;
            parent = parentIt.value().parentUuid;
        }
        if (descendant)
            continue;

        const anim::Pose pose =
            anim::evaluateEntity(project, it.key(), frame, world, cache);
        if (!pose.visible)
            continue;

        const gizmo::Bounds entityBounds = sessionGizmoBounds(entity);
        QPolygonF entityBox;
        entityBox << pose.world.map(QPointF(entityBounds.left, entityBounds.top))
                  << pose.world.map(QPointF(entityBounds.right, entityBounds.top))
                  << pose.world.map(QPointF(entityBounds.right, entityBounds.bottom))
                  << pose.world.map(QPointF(entityBounds.left, entityBounds.bottom));
        const QRectF rect = entityBox.boundingRect();
        if (rect.isEmpty())
            continue;
        if (settings.snapEntityEdges)
        {
            m_sessionSnapTargetsX << rect.left() << rect.right();
            m_sessionSnapTargetsY << rect.top() << rect.bottom();
        }
        if (settings.snapEdgeMidpoints)
        {
            m_sessionSnapTargetsX << rect.center().x();
            m_sessionSnapTargetsY << rect.center().y();
        }
    }
}

double Viewport::snapSessionPixel(double value) const
{
    return std::round(value);
}

double Viewport::snapSessionAxis(double value, bool xAxis)
{
    if (!m_session)
        return snapSessionPixel(value);
    const next::AppSettings& settings = m_session->appSettings();

    if (m_sessionDragSnapRoot)
    {
        const double startValue =
            xAxis ? m_sessionDragTransform.x : m_sessionDragTransform.y;
        const double delta = value - startValue;
        const QRectF bounds = m_sessionDragStartWorldBounds;
        const std::array<double, 3> features =
            xAxis ? std::array<double, 3>{bounds.left() + delta,
                                          bounds.center().x() + delta,
                                          bounds.right() + delta}
                  : std::array<double, 3>{bounds.top() + delta,
                                          bounds.center().y() + delta,
                                          bounds.bottom() + delta};
        const double threshold =
            double(std::max(1, settings.snapPixels)) / m_sessionDragViewScale;
        // The model origin is the canvas centre, so both centre guides are 0.
        const double canvasCenter = 0.0;

        // Canvas centre lines beat other entities, which beat the pixel grid.
        if (settings.snapCanvasCenter)
        {
            if (const auto snap = ui::detail::nearestFeatureSnap(
                    features, {canvasCenter}, threshold))
            {
                m_showGuideX = m_showGuideX || xAxis;
                m_showGuideY = m_showGuideY || !xAxis;
                if (xAxis)
                    m_guideX = canvasCenter;
                else
                    m_guideY = canvasCenter;
                return value + *snap;
            }
        }
        if (const auto snap = ui::detail::nearestFeatureSnap(
                features, xAxis ? m_sessionSnapTargetsX : m_sessionSnapTargetsY,
                threshold))
        {
            if (xAxis)
            {
                m_showGuideX = true;
                m_guideX = features[1] + *snap;
            }
            else
            {
                m_showGuideY = true;
                m_guideY = features[1] + *snap;
            }
            return value + *snap;
        }
    }

    return snapSessionPixel(value);
}

double Viewport::snapSessionAngle(double degrees, bool force) const
{
    const QVector<double> angles =
        m_session ? m_session->appSettings().snapAngles : QVector<double>();
    if (angles.isEmpty())
        return degrees;

    double nearest = degrees;
    double nearestDistance = 360.0;
    for (double angle : angles)
    {
        const double distance =
            std::abs(std::remainder(degrees - angle, 360.0));
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = angle;
        }
    }
    if (force || nearestDistance <= ui::detail::kSessionSnapAngleTolerance)
        return nearest;
    return degrees;
}

} // namespace pvf
