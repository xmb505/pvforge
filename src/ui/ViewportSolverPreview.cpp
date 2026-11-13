#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "model/Model.h"
#include "render2/Render.h"

#include <QHash>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace pvf {

namespace {

using ui::detail::plainSolverLocalTransform;
using ui::detail::solverTrackValue;
using ui::detail::transformRotationOf;

// Maps relative solver frames to project frames and project-space poses /
// shapes back into the driven entity's parent space.  This mirrors the adapter
// in anim and in the next-shell SolverPreviewWidget.
class SolverPreviewWorld final : public solver::World
{
public:
    SolverPreviewWorld(const model::Project& project,
                       const model::Entity& driven, render2::SceneWorld& scene)
        : m_project(project)
        , m_driven(driven)
        , m_scene(scene)
    {
    }

    solver::Pose poseOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
        if (uuid == m_driven.uuid)
        {
            const QTransform local =
                plainSolverLocalTransform(m_driven, frame);
            return solver::Pose{local.map(QPointF(0.0, 0.0)),
                                transformRotationOf(local)};
        }

        const solver::Pose raw = m_scene.poseOf(uuid, projectFrame);
        const QTransform parent = parentWorldOf(projectFrame);
        if (parent.isIdentity())
            return raw;

        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (!invertible)
            return raw;
        return solver::Pose{inverse.map(raw.position),
                            raw.rotation - transformRotationOf(parent)};
    }

    QPainterPath shapeOf(const Uuid& uuid, int frame) const override
    {
        const int projectFrame = frame + m_driven.start;
        if (uuid == m_driven.uuid)
        {
            if (m_driven.content.kind != ContentKind::Vector)
                return {};
            const model::VectorContent& vector = m_driven.content.vector;
            const QRectF rect(-vector.width * 0.5, -vector.height * 0.5,
                              std::max(1.0, vector.width),
                              std::max(1.0, vector.height));
            QPainterPath path;
            path.addRect(rect);
            return plainSolverLocalTransform(m_driven, frame).map(path);
        }

        const QPainterPath path = m_scene.shapeOf(uuid, projectFrame);
        if (path.isEmpty())
            return {};
        const QTransform parent = parentWorldOf(projectFrame);
        if (parent.isIdentity())
            return path;

        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        return invertible ? inverse.map(path) : path;
    }

    bool physicsInteract(const Uuid& uuid, int frame) const override
    {
        return m_scene.physicsInteract(uuid, frame + m_driven.start);
    }

    QVector<Uuid> physicsEntities(int frame) const override
    {
        return m_scene.physicsEntities(frame + m_driven.start);
    }

private:
    QTransform parentWorldOf(int projectFrame) const
    {
        if (m_driven.parentUuid.isEmpty()
            || !m_project.entities.contains(m_driven.parentUuid))
            return QTransform();

        const auto cached = m_parentWorld.constFind(projectFrame);
        if (cached != m_parentWorld.constEnd())
            return cached.value();
        const QTransform world =
            anim::evaluateEntity(m_project, m_driven.parentUuid, projectFrame,
                                 m_scene)
                .world;
        m_parentWorld.insert(projectFrame, world);
        return world;
    }

    const model::Project& m_project;
    const model::Entity& m_driven;
    render2::SceneWorld& m_scene;
    mutable QHash<int, QTransform> m_parentWorld;
};

} // namespace

SolverPreviewEval evaluateSolverPreview(const model::Project& project,
                                        const assets::AssetStore& assets,
                                        const model::Entity& entity,
                                        const model::SolverSegment& solver,
                                        int maxFrames)
{
    SolverPreviewEval preview;
    const int duration = entity.duration();
    const int structuralEnd = model::solverStructuralEnd(entity, solver);
    int effectiveEnd = structuralEnd;
    if (duration <= 0 || solver.startFrame < 0
        || solver.startFrame >= duration || effectiveEnd < solver.startFrame)
    {
        preview.error = QStringLiteral("求解段范围无效");
        return preview;
    }

    const int startFrame = solver.startFrame;
    const model::Transform& fallback = entity.transform;
    const QPointF startPosition(
        solverTrackValue(entity, QStringLiteral("x"), startFrame, fallback.x),
        solverTrackValue(entity, QStringLiteral("y"), startFrame, fallback.y));
    const double startRotation = solverTrackValue(
        entity, QStringLiteral("rotation"), startFrame, fallback.rotation);
    preview.startPose =
        solver::Pose{startPosition, startRotation};

    Uuid targetUuid;
    if (solver.effect != SolverEffect::Spin)
    {
        targetUuid =
            solver.params.value(QStringLiteral("targetUuid")).toString();
        if (targetUuid.isEmpty() || !project.entities.contains(targetUuid))
        {
            preview.error = QStringLiteral("目标缺失");
            return preview;
        }
    }

    int evaluationEnd = effectiveEnd;
    if (maxFrames > 0 && effectiveEnd - startFrame + 1 > maxFrames)
    {
        evaluationEnd = startFrame + maxFrames - 1;
        preview.truncated = true;
    }

    solver::Segment request;
    request.effect = solver.effect;
    request.entityUuid = entity.uuid;
    request.targetUuid = targetUuid;
    request.startFrame = startFrame;
    // Automatic mode must stay -1 so evaluateResult() can report the real
    // physical stop; only truncation turns it into a preview-only explicit end.
    // A later keyframe ends an automatic solver (N-1). Keep -1 only when the
    // solver is truly open to the clip end so evaluateResult() can report the
    // physical stop reason.
    request.endFrame = (preview.truncated || structuralEnd < duration - 1)
                           ? evaluationEnd
                           : -1;
    request.startPose = preview.startPose;
    request.initialVelocity = solver.initialVelocity;
    request.params = solver.params;

    render2::SceneWorld scene(project, assets);
    SolverPreviewWorld world(project, entity, scene);
    preview.result = solver::evaluateResult(
        request, world, duration, std::max(1, project.settings.fps));
    if (!preview.isValid())
    {
        if (preview.error.isEmpty())
            preview.error = QStringLiteral("无法解算");
        return preview;
    }

    preview.startPose = preview.result.samples.first().pose;
    if (!preview.truncated && solver.effect != SolverEffect::Spin)
    {
        preview.targetPosition =
            world.poseOf(targetUuid, startFrame).position;
        preview.hasTarget = true;
    }
    return preview;
}

} // namespace pvf
