#pragma once

#include "anim/Anim.h"
#include "assets/AssetStore.h"
#include "model/Model.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QVector>

#include <functional>

namespace pvf::render2 {

// Returns the decoded frame for one asset-backed entity at a project frame.
// HTML and video services supply these; an empty image keeps the placeholder.
// The frame is a double so 2x/4x exports can request fractional project frames.
using FrameProvider = std::function<QImage(const Uuid& entityUuid,
                                           double projectFrame)>;

// One rendered frame request.  An empty outputSize means the project canvas
// size; an invalid background means project.settings.background.  An empty
// cameraUuid renders the whole project canvas (0,0,width,height).
// `worldRect` overrides that canvas rectangle for the canvas view; it may
// contain negative coordinates so editor-only previews can show entities
// outside the canvas.  Camera views ignore it.
// `transparentOutsideCanvas` only applies when cameraUuid is empty and
// worldRect is valid: the image starts fully transparent, the project canvas
// is filled with the background and only entities are drawn outside it.
// `projectFrame` is fractional during 2x/4x fps export sampling.
struct Options
{
    double projectFrame = 0.0;
    Uuid cameraUuid;
    QSize outputSize;
    QColor background;
    QRectF worldRect;
    bool transparentOutsideCanvas = false;
    FrameProvider videoFrame;
    FrameProvider htmlFrame;
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

// Test hook: label shown when an asset entity has no drawable picture, or
// "素材丢失" when the asset metadata/content is gone.  An empty string means
// the image asset is drawable.
QString placeholderLabelForTest(const assets::AssetStore& store,
                                const model::AssetContent& content);

} // namespace pvf::render2
