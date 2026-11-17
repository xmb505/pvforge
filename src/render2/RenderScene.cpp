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
    // parent itself, then non-negative-row children on top.
    const QVector<Uuid> children = model::childrenOf(project, uuid);
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
                          world, cache, visited, options, editorHidden);
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
                          world, cache, visited, options, editorHidden);
    }
}

void drawChildren(QPainter& painter, const model::Project& project,
                  const assets::AssetStore& store, const Uuid& parentUuid,
                  double projectFrame, anim::World& world, anim::Cache& cache,
                  QSet<Uuid>& visited, const Options& options,
                  bool parentEditorHidden = false)
{
    const QVector<Uuid> children = model::childrenOf(project, parentUuid);
    for (const Uuid& uuid : children)
    {
        drawEntitySubtree(painter, project, store, uuid, projectFrame, world,
                          cache, visited, options, parentEditorHidden);
    }
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

    QColor background = options.background;
    if (!background.isValid())
        background = QColor(project.settings.background);
    if (!background.isValid())
        background = Qt::black;

    const bool canvasMode = options.cameraUuid.isEmpty();
    const double canvasWidth = double(std::max(1, project.settings.width));
    const double canvasHeight = double(std::max(1, project.settings.height));
    // World origin is the canvas centre; +Y is up.
    const QRectF projectCanvas(-canvasWidth / 2.0, -canvasHeight / 2.0,
                               canvasWidth, canvasHeight);
    const bool customWorld =
        canvasMode && options.worldRect.isValid()
        && options.worldRect.width() > 0.0 && options.worldRect.height() > 0.0;
    const QRectF worldRect = customWorld ? options.worldRect : projectCanvas;
    // Only the expanded editor preview asks for a transparent surround.  The
    // canvas itself keeps the project background so the cached image still
    // carries the real picture inside the frame.
    const bool transparent =
        canvasMode && customWorld && options.transparentOutsideCanvas;

    QImage image(output, QImage::Format_ARGB32_Premultiplied);
    image.fill(transparent ? QColor(Qt::transparent) : background);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QTransform view =
        canvasMode ? detail::canvasView(output, worldRect)
                   : detail::cameraView(project, options, world, cache, output);
    painter.setTransform(view);
    if (transparent)
        painter.fillRect(projectCanvas, background);

    QSet<Uuid> visited;
    drawChildren(painter, project, assets, Uuid(), options.projectFrame, world,
                 cache, visited, options, false);
    painter.end();
    return image;
}

void renderToPainter(QPainter& painter, const QTransform& worldToScreen,
                     const model::Project& project,
                     const assets::AssetStore& assets, const Options& options,
                     anim::World& world, anim::Cache& cache)
{
    perf::Scope renderScope("场景直绘", perf::Category::Render2);
    QColor background = options.background;
    if (!background.isValid())
        background = QColor(project.settings.background);
    if (!background.isValid())
        background = Qt::white;

    const double canvasWidth = double(std::max(1, project.settings.width));
    const double canvasHeight = double(std::max(1, project.settings.height));
    const QRectF projectCanvas(-canvasWidth / 2.0, -canvasHeight / 2.0,
                               canvasWidth, canvasHeight);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setTransform(worldToScreen, true);
    painter.fillRect(projectCanvas, background);

    QSet<Uuid> visited;
    drawChildren(painter, project, assets, Uuid(), options.projectFrame, world,
                 cache, visited, options, false);
    painter.restore();
}

} // namespace pvf::render2
