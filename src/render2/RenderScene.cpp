#include "Render.h"

#include "perf/FrameProfiler.h"

#include <QPainter>
#include <QSet>

#include <algorithm>

namespace pvf::render2 {
namespace detail {

bool activeAt(const model::Entity& entity, double projectFrame);
void paintContent(QPainter& painter, const model::Entity& entity,
                  const assets::AssetStore& store, const Options& options,
                  double projectFrame);
QTransform canvasView(const QSize& output, const QRectF& worldRect);
QTransform cameraView(const model::Project& project, const Options& options,
                      anim::World& world, anim::Cache& cache,
                      const QSize& output);

} // namespace detail

namespace {

void drawEntitySubtree(QPainter& painter, const model::Project& project,
                       const assets::AssetStore& store, const Uuid& uuid,
                       double projectFrame, anim::World& world,
                       anim::Cache& cache, QSet<Uuid>& visited,
                       const model::ChildrenIndex& index,
                       const Options& options, bool parentEditorHidden)
{
    if (visited.contains(uuid))
        return;
    visited.insert(uuid);

    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd())
        return;

    const model::Entity& entity = it.value();
    // Editor-only visibility: a hidden parent also hides descendants that
    // inherit visibility, mirroring the `visible` inheritance rule.
    const bool editorHidden =
        options.skipHidden
        && (entity.hidden
            || (parentEditorHidden && entity.inheritVisibility));

    // Layer Sorter: negative-row children render behind the parent, then the
    // parent itself, then non-negative-row children on top.  The parent ->
    // children index is built once per frame, so this walk is O(E log E)
    // instead of re-scanning (and deep-copying) the project per node (#132).
    const QVector<Uuid>& children = index.children(uuid);
    QVector<Uuid> negChildren;
    QVector<Uuid> posChildren;
    for (const Uuid& childUuid : children)
    {
        const auto childIt = project.entities.constFind(childUuid);
        if (childIt != project.entities.constEnd() && childIt->row < 0)
            negChildren.append(childUuid);
        else
            posChildren.append(childUuid);
    }

    for (const Uuid& childUuid : negChildren)
    {
        drawEntitySubtree(painter, project, store, childUuid, projectFrame,
                          world, cache, visited, index, options, editorHidden);
    }

    if (detail::activeAt(entity, projectFrame))
    {
        const anim::Pose pose = anim::evaluateEntity(
            project, uuid, projectFrame, world, cache);
        if (pose.visible && pose.opacity > 0.0 && !editorHidden)
        {
            painter.save();
            painter.setOpacity(std::clamp(pose.opacity, 0.0, 1.0));
            painter.setTransform(pose.world, true);
            // Content (Qt text, images, vector paths) is authored in
            // local Y-down space; model space is Y-up.  Flip once here,
            // around the content origin, so content is upright in model
            // space.  The view transform already did world -> screen.
            painter.scale(1.0, -1.0);
            detail::paintContent(painter, entity, store, options, projectFrame);
            painter.restore();
        }
    }

    for (const Uuid& childUuid : posChildren)
    {
        drawEntitySubtree(painter, project, store, childUuid, projectFrame,
                          world, cache, visited, index, options, editorHidden);
    }
}

void drawChildren(QPainter& painter, const model::Project& project,
                  const assets::AssetStore& store, const Uuid& parentUuid,
                  double projectFrame, anim::World& world, anim::Cache& cache,
                  QSet<Uuid>& visited, const model::ChildrenIndex& index,
                  const Options& options,
                  bool parentEditorHidden = false)
{
    for (const Uuid& uuid : index.children(parentUuid))
    {
        drawEntitySubtree(painter, project, store, uuid, projectFrame, world,
                          cache, visited, index, options, parentEditorHidden);
    }
}

QColor resolveBackground(const model::Project& project, const Options& options)
{
    QColor background = options.background;
    if (!background.isValid())
        background = QColor(project.settings.background);
    if (!background.isValid())
        background = QColor(0xFF, 0xFF, 0xFF);
    return background;
}

QRectF projectCanvasRect(const model::Project& project)
{
    const double canvasWidth = double(std::max(1, project.settings.width));
    const double canvasHeight = double(std::max(1, project.settings.height));
    // World origin is the canvas centre; +Y is up.
    return QRectF(-canvasWidth / 2.0, -canvasHeight / 2.0,
                  canvasWidth, canvasHeight);
}

void paintScene(QPainter& painter, const QTransform& transform, bool combine,
                const model::Project& project, const assets::AssetStore& assets,
                const Options& options, anim::World& world, anim::Cache& cache,
                const QColor& canvasBg)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setTransform(transform, combine);
    if (canvasBg.isValid())
        painter.fillRect(projectCanvasRect(project), canvasBg);

    QSet<Uuid> visited;
    const model::ChildrenIndex index(project);
    drawChildren(painter, project, assets, Uuid(), options.projectFrame, world,
                 cache, visited, index, options, false);
    painter.restore();
}

} // namespace

QImage renderFrame(const model::Project& project, const assets::AssetStore& assets,
                   const Options& options, anim::World& world, anim::Cache& cache)
{
    perf::Scope renderScope("场景渲染", perf::Category::Render2);
    QSize output = options.outputSize;
    if (output.isEmpty())
        output = QSize(project.settings.width, project.settings.height);
    output = QSize(std::max(1, output.width()), std::max(1, output.height()));

    const QColor background = resolveBackground(project, options);
    const bool canvasMode = options.cameraUuid.isEmpty();
    const QRectF projectCanvas = projectCanvasRect(project);
    const bool customWorld =
        canvasMode && options.worldRect.isValid()
        && options.worldRect.width() > 0.0 && options.worldRect.height() > 0.0;
    const QRectF worldRect = customWorld ? options.worldRect : projectCanvas;
    const bool transparent =
        canvasMode && customWorld && options.transparentOutsideCanvas;

    QImage image(output, QImage::Format_ARGB32_Premultiplied);
    image.fill(transparent ? QColor(Qt::transparent) : background);

    QPainter painter(&image);
    const QTransform view =
        canvasMode ? detail::canvasView(output, worldRect)
                   : detail::cameraView(project, options, world, cache, output);
    paintScene(painter, view, false, project, assets, options, world, cache,
               transparent ? background : QColor());
    painter.end();
    return image;
}

void renderToPainter(QPainter& painter, const QTransform& worldToScreen,
                     const model::Project& project,
                     const assets::AssetStore& assets, const Options& options,
                     anim::World& world, anim::Cache& cache)
{
    perf::Scope renderScope("场景直绘", perf::Category::Render2);
    paintScene(painter, worldToScreen, true, project, assets, options, world,
               cache, resolveBackground(project, options));
}

} // namespace pvf::render2
