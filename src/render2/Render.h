#pragma once

#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QSize>
#include <QVector>

namespace pvf::render2 {

// One rendered frame request.  An empty outputSize means the project canvas
// size; an invalid background means project.settings.background.  An empty
// cameraUuid renders the whole project canvas (0,0,width,height).
struct Options
{
    int projectFrame = 0;
    Uuid cameraUuid;
    QSize outputSize;
    QColor background;
};

// anim::World backed by a model project and an asset store.  Poses and shapes
// are returned in project canvas pixels.  Renderer, previews and physics all
// share this implementation so solver collisions and drawn artwork agree.
class SceneWorld final : public anim::World
{
public:
    SceneWorld(const model::Project& project, const assets::AssetStore& assets);

    solver::Pose poseOf(const Uuid& uuid, int projectFrame) const override;
    QPainterPath shapeOf(const Uuid& uuid, int projectFrame) const override;
    bool physicsInteract(const Uuid& uuid, int projectFrame) const override;
    QVector<Uuid> physicsEntities(int projectFrame) const override;

private:
    const model::Project& m_project;
    const assets::AssetStore& m_assets;
};

// Draws the requested frame into a new ARGB32 premultiplied image.  `world`
// and `cache` are supplied by the caller so repeated frames can reuse solver
// samples; pass a SceneWorld for a project-backed world.
QImage renderFrame(const model::Project& project, const assets::AssetStore& assets,
                   const Options& options, anim::World& world, anim::Cache& cache);

} // namespace pvf::render2
