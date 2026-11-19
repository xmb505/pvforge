#include "Viewport.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "core/PerfMonitor.h"
#include "model/Query.h"
#include "next/app/MediaServices.h"
#include "next/app/PlaybackController.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/core/Document.h"
#include "next/core/EntityLock.h"
#include "perf/FrameProfiler.h"
#include "render2/Render.h"
#include "workbench/MetaBuilder.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>

#include <algorithm>

namespace pvf {

// ---- session shell: render2 + MediaServices + WorkbenchServices ----------

Uuid Viewport::sessionCameraUuid() const
{
    // The old viewport always edits the canvas; the camera is an overlay.
    return m_session ? m_session->activeCameraUuid() : Uuid();
}

int Viewport::sessionExpandPercent() const
{
    if (!m_session)
        return 0;
    return std::clamp(m_session->appSettings().workbenchCacheExpandPercent, 0, 200);
}

QTransform Viewport::sessionWorldToScreen() const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    // Model space is Y-up with the origin at the canvas centre; widget space
    // is Y-down.  This is the single model -> screen Y flip for the legacy
    // shell: model +Y maps upwards on screen.  Everything that maps to or
    // from widget coordinates must go through this transform (or its
    // inverse); do not flip again in the gizmo or camera code.
    QTransform transform;
    transform.translate(canvas.center().x(), canvas.center().y());
    transform.scale(s, -s);
    return transform;
}

QPointF Viewport::sessionScreenToProject(const QPointF& pos) const
{
    bool ok = false;
    const QTransform inverse = sessionWorldToScreen().inverted(&ok);
    return ok ? inverse.map(pos) : pos;
}

QRectF Viewport::sessionSceneRect() const
{
    if (!m_session)
        return QRectF();
    // Same expanded world rectangle the workbench cache renders and stores:
    // the centred canvas plus expandPercent of its width/height per side.
    const QRectF world =
        workbench::renderRect(m_session->project(), sessionExpandPercent());
    return sessionWorldToScreen().mapRect(world);
}

QImage Viewport::renderSessionFrame(const QSize& size, const Uuid& cameraUuid,
                                    int expandPercent,
                                    bool* mediaMissing) const
{
    if (!m_session)
        return {};

    bool missing = false;
    render2::Options options;
    options.projectFrame = m_session->playhead();
    options.cameraUuid = cameraUuid;
    options.outputSize = size;
    options.skipHidden = true; // workbench preview honours editor-only hidden
    if (cameraUuid.isEmpty() && expandPercent > 0)
    {
        // Canvas-centred expanded world rect, identical to the workbench
        // cache meta and to sessionSceneRect().
        options.worldRect =
            workbench::renderRect(m_session->project(), expandPercent);
        options.transparentOutsideCanvas = true;
    }
    options.videoFrame = [this, &missing](const Uuid& uuid, double frame) {
        perf::Scope videoScope("视频帧", perf::Category::Video);
        const QImage image =
            m_session->media() ? m_session->media()->videoFrame(uuid, frame)
                               : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };
    options.htmlFrame = [this, &missing](const Uuid& uuid, double frame) {
        perf::Scope htmlScope("HTML 帧", perf::Category::Html);
        const QImage image =
            m_session->media() ? m_session->media()->htmlFrame(uuid, frame)
                               : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };

    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(m_session->project(), m_session->assets(), &cache);
    const QImage image = render2::renderFrame(
        m_session->project(), m_session->assets(), options, world, cache);
    if (mediaMissing)
        *mediaMissing = missing;
    return image;
}

QImage Viewport::ensureSessionImage()
{
    if (!m_session)
        return {};

    const int expand = sessionExpandPercent();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const QSizeF sceneSize = expand > 0 ? sessionSceneRect().size() : QSizeF(size());
    const QSize targetSize(
        std::clamp(int(std::lround(sceneSize.width() * dpr)), 1, 8192),
        std::clamp(int(std::lround(sceneSize.height() * dpr)), 1, 8192));
    const int frame = m_session->playhead();
    const Uuid camera;

    if (m_sessionImageValid && m_sessionImageSize == targetSize
        && m_sessionImageFrame == frame && m_sessionImageCamera == camera
        && m_sessionImageExpand == expand)
        return m_sessionImage;

    m_sessionMediaMissing = false;

    // A live gizmo drag is not representable by the stored frame meta until it
    // commits, so both lookup and store are bypassed for the duration of the
    // gesture.  After release the commit changes the meta and the next lookup
    // misses, rendering and updating the cache.
    const bool liveEdit = m_sessionDragKind != SessionDragKind::None
                          && m_sessionDragChanged;

    // Whole-frame workbench cache: a hit is a finished picture and skips
    // render2 entirely.  Misses render live and store the result unless a
    // media provider returned a placeholder.
    next::WorkbenchServices* workbench = m_session->workbench();
    if (workbench && !liveEdit)
    {
        perf::Scope lookupScope("帧缓存查找", perf::Category::Lookup);
        bool hit = false;
        const QImage cached = workbench->lookup(frame, camera, &hit);
        if (hit && !cached.isNull())
        {
            m_sessionImage = cached;
            m_sessionImageSize = targetSize;
            m_sessionImageFrame = frame;
            m_sessionImageCamera = camera;
            m_sessionImageExpand = expand;
            m_sessionImageValid = true;
            m_sessionMediaMissing = false;
            return m_sessionImage;
        }
    }

    bool missing = false;
    QImage image = renderSessionFrame(targetSize, camera, expand, &missing);
    // Publish the finished picture before the cache hook.  A paused store is
    // synchronous (about 2 ms QOI), while a playback raster miss only queues
    // its WebP encode, so the next paint never waits for encode or disk I/O.
    m_sessionImage = image;
    m_sessionImageSize = targetSize;
    m_sessionImageFrame = frame;
    m_sessionImageCamera = camera;
    m_sessionImageExpand = expand;
    m_sessionImageValid = true;
    m_sessionMediaMissing = missing;

    if (!liveEdit && !missing && !image.isNull() && workbench)
        workbench->store(frame, camera, image);
    return m_sessionImage;
}

void Viewport::invalidateSessionImage()
{
    m_sessionImageValid = false;
}

QImage Viewport::grabProjectFrame(const Uuid& cameraUuid) const
{
    if (!m_session)
        return {};
    const model::ProjectSettings& settings = m_session->settings();
    return renderSessionFrame(
        QSize(std::max(1, settings.width), std::max(1, settings.height)),
        cameraUuid, 0, nullptr);
}

#ifdef PVFORGE_TEST_HOOKS
QPointF Viewport::projectToWidgetForTest(const QPointF& pos) const
{
    return sessionWorldToScreen().map(pos);
}
#endif

namespace {

bool pickableContent(const model::Entity& entity)
{
    if (entity.content.kind == ContentKind::Camera
        || entity.content.kind == ContentKind::Empty)
        return false;
    if (entity.content.kind == ContentKind::Asset
        && entity.content.asset.type == AssetType::Audio)
        return false;
    return true;
}

// Mirrors render2::RenderScene's layer walk: negative-row children first, then
// the entity, then non-negative children.  Picking walks this order backwards
// so the top-most painted entity is the first hit, including hidden parents
// that take their inheriting descendants with them.
void collectPickOrder(const model::Project& project, const Uuid& parentUuid,
                      int frame, bool parentHidden,
                      QVector<const model::Entity*>& order)
{
    const auto it = project.entities.constFind(parentUuid);
    bool editorHidden = parentHidden;
    if (it != project.entities.constEnd())
        editorHidden = it->hidden
                       || (parentHidden && it->inheritVisibility);

    const QVector<Uuid> children = model::childrenOf(project, parentUuid);
    QVector<Uuid> negative;
    QVector<Uuid> positive;
    for (const Uuid& uuid : children)
    {
        const auto childIt = project.entities.constFind(uuid);
        if (childIt == project.entities.constEnd())
            continue;
        (childIt->row < 0 ? negative : positive).append(uuid);
    }

    for (const Uuid& uuid : negative)
        collectPickOrder(project, uuid, frame, editorHidden, order);

    if (it != project.entities.constEnd())
    {
        const model::Entity& entity = it.value();
        if (!editorHidden && entity.visible && frame >= entity.start
            && frame < entity.end && pickableContent(entity))
            order.append(&entity);
    }

    for (const Uuid& uuid : positive)
        collectPickOrder(project, uuid, frame, editorHidden, order);
}

} // namespace

const model::Entity* Viewport::hitTestSession(const QPointF& pos) const
{
    if (!m_session)
        return nullptr;

    const model::Project& project = m_session->project();
    const int frame = m_session->playhead();
    const QPointF projectPos = sessionScreenToProject(pos);

    QVector<const model::Entity*> order;
    order.reserve(project.entities.size());
    collectPickOrder(project, Uuid(), frame, false, order);

    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    for (int i = order.size() - 1; i >= 0; --i)
    {
        const model::Entity* entity = order.at(i);
        const QPainterPath shape = world.shapeOf(entity->uuid, frame);
        if (!shape.isEmpty() && shape.contains(projectPos))
            return entity;
    }
    return nullptr;
}

void Viewport::drawSessionSelection(QPainter& painter)
{
    if (!m_session || !m_session->document())
        return;

    const Uuid selected = m_session->document()->selection();
    if (selected.isEmpty())
        return;

    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(selected);
    if (it == project.entities.constEnd())
        return;
    if (next::isEntityLocked(project, selected)
        || it->content.kind == ContentKind::Camera)
        return;

    drawSessionGizmo(painter);
}

void Viewport::paintSession(QPainter& p)
{
    perf::FrameScope frameScope;
    QElapsedTimer profile;
    profile.start();

    p.fillRect(rect(), QApplication::palette().color(QPalette::Window));

    const QRectF canvas = canvasRect();
    const int expand = sessionExpandPercent();

    p.fillRect(canvas.translated(6, 6), QColor(0, 0, 0, 90));

    bool presented = false;
    const bool playing =
        m_session && m_session->playback() && m_session->playback()->isPlaying();
    if (playing)
    {
        const QImage image = ensureSessionImage();

        perf::Scope paintScope("视口绘制", perf::Category::Paint);
        QRectF target = rect();
        if (expand > 0)
            target = sessionSceneRect();
        if (!image.isNull())
        {
            p.save();
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(target, image);
            p.restore();
            presented = !m_sessionMediaMissing;
        }
    }
    else if (m_session)
    {
        perf::Scope paintScope("视口直绘", perf::Category::Paint);
        render2::Options options;
        options.projectFrame = m_session->playhead();
        options.skipHidden = true;
        options.videoFrame = [this](const Uuid& uuid, double frame) {
            return m_session->media()
                       ? m_session->media()->videoFrame(uuid, frame)
                       : QImage();
        };
        options.htmlFrame = [this](const Uuid& uuid, double frame) {
            return m_session->media()
                       ? m_session->media()->htmlFrame(uuid, frame)
                       : QImage();
        };
        anim::Cache& cache = m_session->animCache();
        render2::SceneWorld world(m_session->project(), m_session->assets(), &cache);
        render2::renderToPainter(p, sessionWorldToScreen(), m_session->project(),
                                 m_session->assets(), options, world, cache);
        presented = true;
    }

    p.setPen(QPen(QColor(70, 70, 80), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(canvas);

    p.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    p.drawLine(QPointF(canvas.center().x(), canvas.top()),
               QPointF(canvas.center().x(), canvas.bottom()));
    p.drawLine(QPointF(canvas.left(), canvas.center().y()),
               QPointF(canvas.right(), canvas.center().y()));

    drawSessionCameraOverlay(p);
    drawSessionSelection(p);

    // CAD-style alignment guides while dragging.
    if (m_showGuideX)
    {
        const double gx = sessionWorldToScreen().map(QPointF(m_guideX, 0.0)).x();
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(QPointF(gx, canvas.top()), QPointF(gx, canvas.bottom()));
    }
    if (m_showGuideY)
    {
        const double gy = sessionWorldToScreen().map(QPointF(0.0, m_guideY)).y();
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(QPointF(canvas.left(), gy), QPointF(canvas.right(), gy));
    }

    if (m_session->project().entities.isEmpty())
    {
        p.setPen(QColor(150, 150, 160));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("点击左上角工作台按钮\n创建图片 / 视频 / 文字 / 矢量"));
    }

    PerfMonitor::instance().recordScene(profile.nsecsElapsed() / 1e6);
    PerfMonitor::instance().recordViewport(profile.nsecsElapsed() / 1e6);

    if (presented)
        emit framePresented(m_session->playhead());
}

} // namespace pvf
