#include "core/ComicBox.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "Viewport.h"

#include "anim/Anim.h"
#include "app/AppIcon.h"
#include "app/EditorSession.h"
#include "core/PerfMonitor.h"
#include "core/Settings.h"
#include "model/Model.h"
#include "next/app/MediaServices.h"
#include "next/app/ProjectDocument.h"
#include "next/app/WorkbenchServices.h"
#include "next/core/Document.h"
#include "render/SceneRenderer.h"
#include "render2/Render.h"
#include "workbench/MetaBuilder.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace pvf {

namespace {

// Gizmo geometry in screen pixels, so it keeps a constant size while zooming.
constexpr double kGizmoAxisLength = 78.0;
constexpr double kGizmoRingRadius = 48.0;
constexpr double kGizmoHitWidth = 7.0;
constexpr double kGizmoCenterDeadzone = 14.0;

// Snaps value to the nearest candidate within threshold.
double snapTo(double value, std::initializer_list<double> candidates, double threshold)
{
    for (double candidate : candidates)
        if (std::abs(value - candidate) <= threshold)
            return candidate;
    return value;
}

double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b)
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

// ---- session gizmo helpers ----------------------------------------------

constexpr double kSessionSnapAngleTolerance = 3.0;

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

double clampedCameraScale(double value)
{
    const double magnitude = std::clamp(std::abs(value), 0.05, 1.0);
    return value < 0.0 ? -magnitude : magnitude;
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

gizmo::Handle scaleHandleForCorner(int corner)
{
    switch (corner)
    {
    case 0:
        return gizmo::Handle::ScaleTopLeft;
    case 1:
        return gizmo::Handle::ScaleTopRight;
    case 2:
        return gizmo::Handle::ScaleBottomRight;
    default:
        return gizmo::Handle::ScaleBottomLeft;
    }
}

gizmo::Handle rotateHandleForCorner(int corner)
{
    switch (corner)
    {
    case 0:
        return gizmo::Handle::RotateTopLeft;
    case 1:
        return gizmo::Handle::RotateTopRight;
    case 2:
        return gizmo::Handle::RotateBottomRight;
    default:
        return gizmo::Handle::RotateBottomLeft;
    }
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

void drawRotateGlyph(QPainter& painter, QPointF center)
{
    painter.save();
    painter.translate(center);
    const QColor color(246, 249, 255);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    const QRectF arcRect(-7.0, -7.0, 14.0, 14.0);
    painter.drawArc(arcRect, 30 * 16, 120 * 16);
    painter.drawArc(arcRect, 210 * 16, 120 * 16);
    painter.restore();
}

} // namespace


Viewport::Viewport(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_showCameraFrame = Settings::showCameraFrame();

    m_workbenchButton = new QToolButton(this);
    m_workbenchButton->setObjectName(QStringLiteral("workbenchButton"));
    m_workbenchButton->setIcon(makeCubeIcon());
    m_workbenchButton->setIconSize(QSize(34, 34));
    m_workbenchButton->setFixedSize(46, 46);
    m_workbenchButton->move(12, 12);
    m_workbenchButton->setToolTip(QStringLiteral("工作台：创建图片 / 视频 / 文字 / 矢量"));
    m_workbenchButton->setStyleSheet(QStringLiteral(
        "QToolButton#workbenchButton { background: rgba(40, 40, 48, 210);"
        " border: 1px solid #5a5a66; }"
        "QToolButton#workbenchButton:hover { background: rgba(70, 70, 82, 230);"
        " border-color: #8a8a96; }"
        "QToolButton#workbenchButton:pressed { background: rgba(30, 30, 36, 230); }"));
    connect(m_workbenchButton, &QToolButton::clicked, this, &Viewport::workbenchRequested);
}

Viewport::Viewport(EditorSession* session, QWidget* parent)
    : Viewport(static_cast<Project*>(nullptr), parent)
{
    m_session = session;
    if (!m_session)
        return;

    if (auto* document = m_session->document())
    {
        connect(document, &next::Document::changed, this, [this] {
            invalidateSessionImage();
            update();
        });
        connect(document, &next::Document::playheadChanged, this, [this] {
            invalidateSessionImage();
            update();
        });
        connect(document, &next::Document::selectionChanged, this, [this] {
            invalidateSessionImage();
            update();
        });
    }

    if (auto* media = m_session->media())
    {
        connect(media, &next::MediaServices::htmlFrameReady, this, [this] {
            invalidateSessionImage();
            update();
        }, Qt::QueuedConnection);
        connect(media, &next::MediaServices::htmlServiceFailed, this,
                [this](const QString& message) {
                    setToolTip(QStringLiteral("HTML 服务未启动：%1").arg(message));
                });
    }
}

void Viewport::refreshSession()
{
    invalidateSessionImage();
    update();
}

QSize Viewport::canvasSize() const
{
    if (m_session)
    {
        const model::ProjectSettings& settings = m_session->settings();
        return QSize(std::max(1, settings.width), std::max(1, settings.height));
    }
    if (m_project)
        return QSize(std::max(1, m_project->width), std::max(1, m_project->height));
    return QSize(16, 9);
}

void Viewport::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    if (sessionMode())
        invalidateSessionImage();
    update();
}

void Viewport::setSelectedEntity(EntityId id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    update();
}

void Viewport::setCameraFrameVisible(bool on)
{
    if (m_showCameraFrame == on)
        return;
    m_showCameraFrame = on;
    update();
}

void Viewport::resetView()
{
    m_zoom = 1.0;
    m_pan = QPointF();
    emit viewChanged();
    update();
}

double Viewport::viewScale() const
{
    return canvasRect().width() / std::max(1, canvasSize().width());
}

QRectF Viewport::canvasRect() const
{
    const QSize canvas = canvasSize();
    const double margin = 40.0;
    const double fitW = (width() - margin) / std::max(1, canvas.width());
    const double fitH = (height() - margin) / std::max(1, canvas.height());
    const double scale = std::max(0.0001, std::min(fitW, fitH) * m_zoom);
    const QSizeF size(canvas.width() * scale, canvas.height() * scale);
    const QPointF center = QRectF(rect()).center() + m_pan;
    return QRectF(center.x() - size.width() / 2.0, center.y() - size.height() / 2.0,
                  size.width(), size.height());
}

QPointF Viewport::screenToCanvas(const QPointF& pos) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const QPointF center = canvas.center();
    return QPointF((pos.x() - center.x()) / s, (pos.y() - center.y()) / s);
}

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
    if (cameraUuid.isEmpty() && expandPercent > 0)
    {
        // Canvas-centred expanded world rect, identical to the workbench
        // cache meta and to sessionSceneRect().
        options.worldRect =
            workbench::renderRect(m_session->project(), expandPercent);
        options.transparentOutsideCanvas = true;
    }
    options.videoFrame = [this, &missing](const Uuid& uuid, double frame) {
        const QImage image =
            m_session->media() ? m_session->media()->videoFrame(uuid, frame)
                               : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };
    options.htmlFrame = [this, &missing](const Uuid& uuid, double frame) {
        const QImage image =
            m_session->media() ? m_session->media()->htmlFrame(uuid, frame)
                               : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };

    render2::SceneWorld world(m_session->project(), m_session->assets());
    anim::Cache cache;
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

    const QSize targetSize = size();
    const int frame = m_session->playhead();
    const Uuid camera;
    const int expand = sessionExpandPercent();

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

QPointF Viewport::projectToWidgetForTest(const QPointF& pos) const
{
    return sessionWorldToScreen().map(pos);
}

const model::Entity* Viewport::hitTestSession(const QPointF& pos) const
{
    if (!m_session)
        return nullptr;

    const model::Project& project = m_session->project();
    const int frame = m_session->playhead();
    const QPointF projectPos = sessionScreenToProject(pos);

    QVector<const model::Entity*> candidates;
    candidates.reserve(project.entities.size());
    for (auto it = project.entities.constBegin(); it != project.entities.constEnd();
         ++it)
    {
        const model::Entity& entity = it.value();
        if (entity.hidden || entity.locked || !entity.visible)
            continue;
        if (entity.content.kind == ContentKind::Camera
            || entity.content.kind == ContentKind::Empty)
            continue;
        if (entity.content.kind == ContentKind::Asset
            && entity.content.asset.type == AssetType::Audio)
            continue;
        if (frame < entity.start || frame >= entity.end)
            continue;
        candidates.append(&entity);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const model::Entity* a, const model::Entity* b) {
                  return a->row > b->row;
              });

    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
    for (const model::Entity* entity : candidates)
    {
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
    if (it->locked || it->content.kind == ContentKind::Camera)
        return;

    drawSessionGizmo(painter);
}

// ---- session gizmo (gizmo:: math) ---------------------------------------

gizmo::Transform Viewport::sessionGizmoTransform(const model::Entity& entity) const
{
    gizmo::Transform transform;
    transform.x = entity.transform.x;
    transform.y = entity.transform.y;
    transform.rotation = entity.transform.rotation;
    transform.scaleX = entity.transform.scaleX;
    transform.scaleY = entity.transform.scaleY;
    transform.pivotX = entity.transform.pivotX;
    transform.pivotY = entity.transform.pivotY;

    if (!m_session)
        return transform;

    const int playhead = m_session->playhead();
    const double relativeFrame = double(playhead - entity.start);
    transform.x = trackValueOr(entity, "x", relativeFrame, transform.x);
    transform.y = trackValueOr(entity, "y", relativeFrame, transform.y);
    transform.rotation =
        trackValueOr(entity, "rotation", relativeFrame, transform.rotation);
    transform.scaleX =
        trackValueOr(entity, "scaleX", relativeFrame, transform.scaleX);
    transform.scaleY =
        trackValueOr(entity, "scaleY", relativeFrame, transform.scaleY);
    transform.pivotX =
        trackValueOr(entity, "pivotX", relativeFrame, transform.pivotX);
    transform.pivotY =
        trackValueOr(entity, "pivotY", relativeFrame, transform.pivotY);

    if (entity.solvers.isEmpty())
        return transform;

    // Solver-driven x/y/rotation only exist inside anim::evaluateEntity().
    const model::Project& project = m_session->project();
    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
    const anim::Pose pose =
        anim::evaluateEntity(project, entity.uuid, playhead, world, cache);

    QTransform local = pose.world;
    if (!entity.parentUuid.isEmpty()
        && project.entities.contains(entity.parentUuid))
    {
        const anim::Pose parent = anim::evaluateEntity(
            project, entity.parentUuid, playhead, world, cache);
        bool invertible = false;
        const QTransform inverse = parent.world.inverted(&invertible);
        if (invertible)
            local = local * inverse;
    }

    const double scaleX = transform.scaleX;
    const double scaleY = transform.scaleY;
    if (std::abs(scaleX) > 1e-9)
    {
        transform.rotation = qRadiansToDegrees(
            std::atan2(local.m12() / scaleX, local.m11() / scaleX));
    }
    else if (std::abs(scaleY) > 1e-9)
    {
        transform.rotation = qRadiansToDegrees(
            std::atan2(-local.m21() / scaleY, local.m22() / scaleY));
    }

    const double pivotLinearX =
        local.m11() * transform.pivotX + local.m21() * transform.pivotY;
    const double pivotLinearY =
        local.m12() * transform.pivotX + local.m22() * transform.pivotY;
    transform.x = local.dx() - transform.pivotX + pivotLinearX;
    transform.y = local.dy() - transform.pivotY + pivotLinearY;
    return transform;
}

gizmo::Bounds Viewport::sessionGizmoBounds(const model::Entity& entity) const
{
    QSizeF size(100.0, 100.0);
    const model::Content content =
        m_session ? render2::animatedContent(entity, m_session->playhead())
                  : entity.content;

    switch (content.kind)
    {
    case ContentKind::Empty:
        break;
    case ContentKind::Vector:
        size = QSizeF(std::max(1.0, content.vector.width),
                      std::max(1.0, content.vector.height));
        break;
    case ContentKind::Text:
    {
        const TextBlock block = TextBlock::measure(
            TextStyle::fromData(textStyleData(content.text)));
        const QSizeF measured = block.size();
        if (measured.width() >= 1.0 && measured.height() >= 1.0)
            size = measured;
        break;
    }
    case ContentKind::Asset:
    {
        size = QSizeF(320.0, 180.0);
        if (content.asset.type == AssetType::Html)
            size = QSizeF(800.0, 120.0);
        if (m_session)
        {
            if (const auto stored = m_session->assets().get(content.asset.hash))
            {
                const int width =
                    stored->metadata.value(QStringLiteral("width")).toInt();
                const int height =
                    stored->metadata.value(QStringLiteral("height")).toInt();
                if (width > 0 && height > 0)
                    size = QSizeF(width, height);
            }
        }
        break;
    }
    case ContentKind::Camera:
        if (m_session)
        {
            size = QSizeF(std::max(1, m_session->settings().width),
                          std::max(1, m_session->settings().height));
        }
        break;
    case ContentKind::ComicText:
        size = QSizeF(std::max(1.0, content.comic.boxWidth),
                      std::max(1.0, content.comic.boxHeight));
        break;
    case ContentKind::RadialLines:
        size = QSizeF(std::max(1.0, content.radial.radius * 2.0),
                      std::max(1.0, content.radial.radius * 2.0));
        break;
    case ContentKind::FlowLines:
        size = QSizeF(std::max(1.0, content.flow.length),
                      std::max(1.0, content.flow.area));
        break;
    case ContentKind::HalftoneDots:
        size = QSizeF(std::max(1.0, content.halftone.area),
                      std::max(1.0, content.halftone.area));
        break;
    case ContentKind::BrandWall:
        size = QSizeF(
            std::max(1.0, content.brandWall.columns * content.brandWall.logoSize
                              + (content.brandWall.columns - 1)
                                    * content.brandWall.spacing),
            std::max(1.0, content.brandWall.rows * content.brandWall.logoSize
                              + (content.brandWall.rows - 1)
                                    * content.brandWall.spacing));
        break;
    }

    gizmo::Bounds bounds;
    bounds.left = -size.width() / 2.0;
    bounds.right = size.width() / 2.0;
    // Model space is Y-up: `top` is the larger Y (the visual top edge) and
    // `bottom` the smaller one.  Converting an existing Y-down QRectF starts
    // with top = -(rect.top()) and bottom = -rect.bottom(); the swap above is
    // the same rule for a symmetric local box.
    bounds.top = size.height() / 2.0;
    bounds.bottom = -size.height() / 2.0;
    return bounds;
}

QTransform Viewport::sessionParentToWidget(const model::Entity& entity) const
{
    if (!m_session || entity.parentUuid.isEmpty())
        return sessionWorldToScreen();

    const model::Project& project = m_session->project();
    const auto parentIt = project.entities.constFind(entity.parentUuid);
    if (parentIt == project.entities.constEnd())
        return sessionWorldToScreen();

    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
    const anim::Pose pose = anim::evaluateEntity(
        project, entity.parentUuid, m_session->playhead(), world, cache);
    return pose.world * sessionWorldToScreen();
}

double Viewport::sessionViewScaleFor(const QTransform& parentToWidget) const
{
    return std::max(1e-6, std::sqrt(std::abs(parentToWidget.determinant())));
}

std::optional<gizmo::Handle> Viewport::hitSessionGizmo(const QPointF& pos) const
{
    if (!m_session || !m_session->document())
        return std::nullopt;
    const Uuid selected = m_session->document()->selection();
    const auto it = m_session->project().entities.constFind(selected);
    if (it == m_session->project().entities.constEnd() || it->locked
        || it->content.kind == ContentKind::Camera)
        return std::nullopt;

    const QTransform parentToWidget = sessionParentToWidget(it.value());
    bool invertible = false;
    const QTransform inverse = parentToWidget.inverted(&invertible);
    if (!invertible)
        return std::nullopt;

    const QPointF parentPos = inverse.map(pos);
    return gizmo::hitTest(parentPos, sessionGizmoTransform(it.value()),
                          sessionGizmoBounds(it.value()),
                          sessionViewScaleFor(parentToWidget));
}

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

    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
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
            if (const auto snap =
                    nearestFeatureSnap(features, {canvasCenter}, threshold))
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
        if (const auto snap = nearestFeatureSnap(
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
    if (force || nearestDistance <= kSessionSnapAngleTolerance)
        return nearest;
    return degrees;
}

void Viewport::writeSessionGizmoValue(model::Entity& entity,
                                      const QString& property, double value)
{
    if (!m_session || !m_session->document())
        return;
    const int relativeFrame = m_session->playhead() - entity.start;
    if (solverDrivesProperty(entity, property, relativeFrame))
        return;

    setStaticGizmoValue(entity, property, value);

    const bool hasTrack = entity.tracks.contains(property);
    if (!hasTrack && !m_autoKeyEnabled)
        return;
    if (relativeFrame < 0 || relativeFrame >= entity.duration())
        return;
    setTrackKeyValue(entity, property, relativeFrame, value);
}

void Viewport::startSessionGizmoDrag(const Uuid& uuid, SessionDragKind kind,
                                     gizmo::Handle handle, const QPointF& pos)
{
    if (!m_session)
        return;
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return;

    m_sessionDragKind = kind;
    m_sessionDragUuid = uuid;
    m_sessionDragStart = it.value();
    m_sessionDragHandle = handle;
    m_sessionDragTransform = sessionGizmoTransform(it.value());
    m_sessionDragBounds = sessionGizmoBounds(it.value());
    m_sessionDragParentToWidget = sessionParentToWidget(it.value());
    m_sessionDragViewScale = sessionViewScaleFor(m_sessionDragParentToWidget);
    collectSessionSnapTargets(uuid);

    bool invertible = false;
    const QTransform inverse = m_sessionDragParentToWidget.inverted(&invertible);
    m_sessionDragStartParent = invertible ? inverse.map(pos) : pos;
    m_sessionDragPivotStart = gizmo::worldPivot(m_sessionDragTransform);
    m_sessionDragCenterStart = gizmo::localToWorld(
        m_sessionDragTransform, gizmo::boundsCenter(m_sessionDragBounds));

    const auto angleAround = [this](QPointF center) {
        return std::atan2(m_sessionDragStartParent.y() - center.y(),
                          m_sessionDragStartParent.x() - center.x());
    };
    m_sessionDragAnglePivot = angleAround(m_sessionDragPivotStart);
    m_sessionDragAngleCenter = angleAround(m_sessionDragCenterStart);

    if (kind == SessionDragKind::Scale)
    {
        m_sessionDragGrabOffset =
            localCorner(m_sessionDragBounds, handle)
            - gizmo::worldToLocal(m_sessionDragTransform, m_sessionDragStartParent);
    }

    m_sessionDragChanged = false;
    m_showGuideX = false;
    m_showGuideY = false;
    setCursor(kind == SessionDragKind::Rotate ? Qt::CrossCursor
               : kind == SessionDragKind::Scale ? Qt::SizeFDiagCursor
                                                : Qt::SizeAllCursor);
}

void Viewport::applySessionGizmoDrag(const QPointF& pos,
                                     Qt::KeyboardModifiers modifiers)
{
    if (!m_session || m_sessionDragUuid.isEmpty()
        || m_sessionDragKind == SessionDragKind::None)
        return;

    auto it = m_session->mutableProject().entities.find(m_sessionDragUuid);
    if (it == m_session->mutableProject().entities.end())
    {
        cancelSessionDrag();
        return;
    }
    model::Entity& entity = it.value();
    if (entity.locked)
        return;

    bool invertible = false;
    const QTransform inverse = m_sessionDragParentToWidget.inverted(&invertible);
    if (!invertible)
        return;
    const QPointF mouseParent = inverse.map(pos);
    const QPointF parentDelta = mouseParent - m_sessionDragStartParent;
    const next::AppSettings& settings = m_session->appSettings();
    const bool snapActive = !modifiers.testFlag(Qt::ControlModifier);
    m_showGuideX = false;
    m_showGuideY = false;

    switch (m_sessionDragKind)
    {
    case SessionDragKind::Move:
    {
        double x = m_sessionDragTransform.x + parentDelta.x();
        double y = m_sessionDragTransform.y + parentDelta.y();
        if (snapActive)
        {
            x = snapSessionAxis(x, true);
            y = snapSessionAxis(y, false);
        }
        writeSessionGizmoValue(entity, QStringLiteral("x"), x);
        writeSessionGizmoValue(entity, QStringLiteral("y"), y);
        break;
    }

    case SessionDragKind::MoveX:
    case SessionDragKind::MoveY:
    {
        const bool moveX = m_sessionDragKind == SessionDragKind::MoveX;
        const QPointF axis = rotateVector(
            moveX ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0),
            m_sessionDragTransform.rotation);
        const double amount = parentDelta.x() * axis.x() + parentDelta.y() * axis.y();
        double value = moveX ? m_sessionDragTransform.x + amount
                             : m_sessionDragTransform.y + amount;
        if (snapActive)
            value = snapSessionAxis(value, moveX);
        writeSessionGizmoValue(entity, moveX ? QStringLiteral("x")
                                             : QStringLiteral("y"),
                               value);
        break;
    }

    case SessionDragKind::Pivot:
    {
        double px = m_sessionDragTransform.pivotX + parentDelta.x();
        double py = m_sessionDragTransform.pivotY + parentDelta.y();
        if (snapActive)
        {
            px = snapSessionPixel(px);
            py = snapSessionPixel(py);
        }
        writeSessionGizmoValue(entity, QStringLiteral("pivotX"), px);
        writeSessionGizmoValue(entity, QStringLiteral("pivotY"), py);
        break;
    }

    case SessionDragKind::Rotate:
    {
        const bool aroundCenter = modifiers.testFlag(Qt::AltModifier);
        const QPointF center = aroundCenter ? m_sessionDragCenterStart
                                            : m_sessionDragPivotStart;
        const double startAngle =
            aroundCenter ? m_sessionDragAngleCenter : m_sessionDragAnglePivot;
        double delta = std::atan2(mouseParent.y() - center.y(),
                                  mouseParent.x() - center.x())
                       - startAngle;
        delta = std::remainder(delta, 2.0 * M_PI);
        double degrees = qRadiansToDegrees(delta);

        if (snapActive)
        {
            const double targetRotation = snapSessionAngle(
                m_sessionDragTransform.rotation + degrees,
                modifiers.testFlag(Qt::ShiftModifier));
            degrees = targetRotation - m_sessionDragTransform.rotation;
        }

        const gizmo::DragResult result = gizmo::rotate(
            m_sessionDragTransform, m_sessionDragBounds, degrees, aroundCenter);
        writeSessionGizmoValue(entity, QStringLiteral("x"), result.transform.x);
        writeSessionGizmoValue(entity, QStringLiteral("y"), result.transform.y);
        writeSessionGizmoValue(entity, QStringLiteral("rotation"),
                               result.transform.rotation);
        break;
    }

    case SessionDragKind::Scale:
    {
        const bool uniform = modifiers.testFlag(Qt::ShiftModifier);
        QPointF mouseLocal =
            gizmo::worldToLocal(m_sessionDragTransform, mouseParent)
            + m_sessionDragGrabOffset;
        if (snapActive && m_sessionDragSnapRoot)
        {
            const QPointF cornerWorld =
                gizmo::localToWorld(m_sessionDragTransform, mouseLocal);
            mouseLocal = gizmo::worldToLocal(
                m_sessionDragTransform,
                QPointF(snapSessionPixel(cornerWorld.x()),
                        snapSessionPixel(cornerWorld.y())));
        }
        const gizmo::DragResult result = gizmo::cornerScale(
            m_sessionDragTransform, m_sessionDragBounds, m_sessionDragHandle,
            mouseLocal, uniform, true);
        writeSessionGizmoValue(entity, QStringLiteral("x"), result.transform.x);
        writeSessionGizmoValue(entity, QStringLiteral("y"), result.transform.y);
        writeSessionGizmoValue(entity, QStringLiteral("scaleX"),
                               result.transform.scaleX);
        writeSessionGizmoValue(entity, QStringLiteral("scaleY"),
                               result.transform.scaleY);
        break;
    }

    case SessionDragKind::CenterScale:
    {
        const bool uniform = modifiers.testFlag(Qt::ShiftModifier);
        QPointF mouseLocal = gizmo::worldToLocalAxesPixels(
            m_sessionDragTransform, mouseParent, m_sessionDragViewScale);
        if (snapActive)
            mouseLocal = QPointF(snapSessionPixel(mouseLocal.x()),
                                 snapSessionPixel(mouseLocal.y()));
        const gizmo::DragResult result =
            gizmo::centerScale(m_sessionDragTransform, mouseLocal, uniform);
        writeSessionGizmoValue(entity, QStringLiteral("scaleX"),
                               result.transform.scaleX);
        writeSessionGizmoValue(entity, QStringLiteral("scaleY"),
                               result.transform.scaleY);
        break;
    }

    case SessionDragKind::CameraMove:
    case SessionDragKind::CameraRotate:
    case SessionDragKind::CameraScale:
    case SessionDragKind::None:
        return;
    }

    // Compare the frame-evaluated result: animated tracks can leave the static
    // fallback untouched outside the edited frame.
    m_sessionDragChanged =
        !transformsEqual(sessionGizmoTransform(entity), m_sessionDragTransform);
    invalidateSessionImage();
    update();
}

void Viewport::finishSessionGizmoDrag()
{
    const SessionDragKind kind = m_sessionDragKind;
    const Uuid uuid = m_sessionDragUuid;
    const bool changed = m_sessionDragChanged;
    const bool cameraDrag = kind == SessionDragKind::CameraMove
                            || kind == SessionDragKind::CameraRotate
                            || kind == SessionDragKind::CameraScale;

    m_sessionDragKind = SessionDragKind::None;
    m_sessionDragUuid.clear();
    m_sessionDragStart = model::Entity();
    m_sessionDragChanged = false;
    m_sessionDragGrabOffset = QPointF();
    m_showGuideX = false;
    m_showGuideY = false;
    unsetCursor();

    if (!m_session || !m_session->document() || !changed || uuid.isEmpty())
    {
        update();
        return;
    }

    QString action;
    if (cameraDrag)
    {
        action = kind == SessionDragKind::CameraMove
                     ? QStringLiteral("move")
                     : kind == SessionDragKind::CameraRotate
                           ? QStringLiteral("rotate")
                           : QStringLiteral("scale");
    }
    else
    {
        switch (kind)
        {
        case SessionDragKind::Move:
        case SessionDragKind::MoveX:
        case SessionDragKind::MoveY:
            action = QStringLiteral("move");
            break;
        case SessionDragKind::Rotate:
            action = QStringLiteral("rotate");
            break;
        case SessionDragKind::Scale:
        case SessionDragKind::CenterScale:
            action = QStringLiteral("scale");
            break;
        case SessionDragKind::Pivot:
            action = QStringLiteral("pivot");
            break;
        default:
            break;
        }
    }

    if (!action.isEmpty())
    {
        const auto found = m_session->project().entities.constFind(uuid);
        const QString name =
            found != m_session->project().entities.constEnd()
                ? (found->name.isEmpty() ? uuid : found->name)
                : uuid;
        QString error;
        if (!m_session->document()->commitEntity(
                uuid, QStringLiteral("pvf: %1 entity \"%2\"").arg(action, name),
                &error))
            emit sessionEditFailed(error);
    }
    invalidateSessionImage();
    update();
}

void Viewport::cancelSessionDrag()
{
    if (m_session && !m_sessionDragUuid.isEmpty()
        && !m_sessionDragStart.uuid.isEmpty())
    {
        auto it = m_session->mutableProject().entities.find(m_sessionDragUuid);
        if (it != m_session->mutableProject().entities.end())
            it.value() = m_sessionDragStart;
    }
    m_sessionDragKind = SessionDragKind::None;
    m_sessionDragUuid.clear();
    m_sessionDragStart = model::Entity();
    m_sessionDragChanged = false;
    m_sessionDragGrabOffset = QPointF();
    m_showGuideX = false;
    m_showGuideY = false;
    unsetCursor();
    invalidateSessionImage();
    update();
}

void Viewport::drawSessionGizmo(QPainter& painter)
{
    if (!m_session || !m_session->document())
        return;
    const Uuid selected = m_session->document()->selection();
    const auto it = m_session->project().entities.constFind(selected);
    if (it == m_session->project().entities.constEnd() || it->locked
        || it->content.kind == ContentKind::Camera)
        return;

    const model::Entity& entity = it.value();
    const QTransform parentToWidget = sessionParentToWidget(entity);
    const double viewScale = sessionViewScaleFor(parentToWidget);
    const gizmo::Transform transform = sessionGizmoTransform(entity);
    const gizmo::Bounds bounds = sessionGizmoBounds(entity);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (entity.hidden)
        painter.setOpacity(0.45);

    const auto screenPosition = [&](gizmo::Handle handle) {
        return parentToWidget.map(
            gizmo::handlePosition(handle, transform, bounds, viewScale));
    };

    QPolygonF box;
    box << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.left, bounds.top)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.right, bounds.top)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.right, bounds.bottom)))
        << parentToWidget.map(
               gizmo::localToWorld(transform, QPointF(bounds.left, bounds.bottom)));
    painter.setPen(QPen(QColor(116, 206, 255, 175), 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(box);

    const QPointF pivotWidget =
        parentToWidget.map(gizmo::worldPivot(transform));
    drawArrowLine(painter, pivotWidget, screenPosition(gizmo::Handle::MoveX),
                  QColor(255, 122, 122));
    drawArrowLine(painter, pivotWidget, screenPosition(gizmo::Handle::MoveY),
                  QColor(125, 255, 158));

    painter.setPen(QPen(QColor(18, 21, 26), 1.0));
    painter.setBrush(QColor(246, 249, 255));
    for (int corner = 0; corner < 4; ++corner)
    {
        const QPointF position = screenPosition(scaleHandleForCorner(corner));
        painter.drawRect(QRectF(position.x() - 3.5, position.y() - 3.5, 7.0, 7.0));
    }

    const QPointF centerScale = screenPosition(gizmo::Handle::CenterScale);
    painter.setPen(QPen(QColor(116, 206, 255), 1.2));
    painter.setBrush(QColor(34, 46, 58));
    painter.drawRect(QRectF(centerScale.x() - 4.5, centerScale.y() - 4.5, 9.0,
                            9.0));
    painter.setPen(QPen(QColor(116, 206, 255, 90), 1.0, Qt::DotLine));
    painter.drawLine(pivotWidget, centerScale);

    for (int corner = 0; corner < 4; ++corner)
        drawRotateGlyph(painter, screenPosition(rotateHandleForCorner(corner)));

    painter.setPen(QPen(QColor(18, 21, 26), 1.5));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(pivotWidget, 4.0, 4.0);

    painter.restore();
}

// ---- session camera viewfinder ------------------------------------------

QPolygonF Viewport::sessionCameraPolygon() const
{
    if (!m_session || !m_showCameraFrame)
        return QPolygonF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QPolygonF();
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd()
        || it->content.kind != ContentKind::Camera || it->hidden)
        return QPolygonF();

    const double width = double(std::max(1, project.settings.width));
    const double height = double(std::max(1, project.settings.height));
    // Camera-local corners are Y-up too, matching render2::cameraView():
    // top-left is (-w/2, +h/2).  The widget transform performs the flip.
    QPolygonF source;
    source << QPointF(-width / 2.0, height / 2.0)
           << QPointF(width / 2.0, height / 2.0)
           << QPointF(width / 2.0, -height / 2.0)
           << QPointF(-width / 2.0, -height / 2.0);

    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
    const anim::Pose pose =
        anim::evaluateEntity(project, uuid, m_session->playhead(), world, cache);
    return sessionWorldToScreen().map(pose.world.map(source));
}

QPointF Viewport::sessionCameraScreenCenter() const
{
    if (!m_session)
        return QPointF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QPointF();
    const model::Project& project = m_session->project();
    render2::SceneWorld world(project, m_session->assets());
    anim::Cache cache;
    const anim::Pose pose = anim::evaluateEntity(
        project, uuid, m_session->playhead(), world, cache);
    return sessionWorldToScreen().map(pose.world.map(QPointF(0.0, 0.0)));
}

QPointF Viewport::sessionCameraRotationHandle() const
{
    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.size() < 4)
        return QPointF();
    const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
    const QPointF center = sessionCameraScreenCenter();
    QPointF up = topCenter - center;
    const double length = std::hypot(up.x(), up.y());
    if (length < 1e-6)
        return topCenter;
    return topCenter + up / length * 28.0;
}

QRectF Viewport::sessionCameraTitleRect() const
{
    if (!m_session)
        return QRectF();
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return QRectF();
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return QRectF();
    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.isEmpty())
        return QRectF();
    const QRectF box = polygon.boundingRect();
    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(it->name);
    const QRectF title(box.left(), box.top() - 24.0, textWidth + 16.0, 20.0);
    if (title.top() < 2.0)
        return QRectF(box.left(), box.top() + 4.0, textWidth + 16.0, 20.0);
    return title;
}

Viewport::CameraPart Viewport::sessionCameraHitTest(const QPointF& pos) const
{
    if (!m_showCameraFrame || !m_session)
        return CameraPart::None;
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return CameraPart::None;
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd()
        || it->content.kind != ContentKind::Camera)
        return CameraPart::None;

    const bool selected =
        m_session->document() && m_session->document()->selection() == uuid;
    if (selected && !it->locked && it->parentUuid.isEmpty())
    {
        if (QLineF(pos, sessionCameraRotationHandle()).length() <= 10.0)
            return CameraPart::Rotate;

        const QPolygonF polygon = sessionCameraPolygon();
        for (int i = 0; i < polygon.size(); ++i)
        {
            if (QLineF(pos, polygon[i]).length() <= 8.0)
                return CameraPart(i == 0 ? CameraPart::Corner0
                                  : i == 1 ? CameraPart::Corner1
                                  : i == 2 ? CameraPart::Corner2
                                           : CameraPart::Corner3);
        }
    }

    if (sessionCameraTitleRect().contains(pos))
        return CameraPart::Move;

    const QPolygonF polygon = sessionCameraPolygon();
    for (int i = 0; i < polygon.size(); ++i)
    {
        if (distanceToSegment(pos, polygon[i], polygon[(i + 1) % polygon.size()])
            <= 6.0)
            return CameraPart::Move;
    }
    return CameraPart::None;
}

bool Viewport::handleSessionCameraPress(QMouseEvent* event)
{
    if (!m_session || event->button() != Qt::LeftButton)
        return false;

    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return false;
    const CameraPart part = sessionCameraHitTest(event->position());
    if (part == CameraPart::None)
        return false;

    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return false;
    if (m_session->document())
        m_session->document()->setSelection(uuid);
    emit uuidSelected(uuid);

    if (it->locked || !it->parentUuid.isEmpty())
        return true;

    m_sessionDragKind = part == CameraPart::Move
                            ? SessionDragKind::CameraMove
                            : part == CameraPart::Rotate
                                  ? SessionDragKind::CameraRotate
                                  : SessionDragKind::CameraScale;
    m_sessionDragUuid = uuid;
    m_sessionDragStart = it.value();
    m_sessionDragTransform = sessionGizmoTransform(it.value());
    m_sessionDragBounds = sessionGizmoBounds(it.value());
    m_sessionDragParentToWidget = sessionWorldToScreen();
    m_sessionDragViewScale = viewScale();
    m_sessionDragStartParent = sessionScreenToProject(event->position());
    // Keep the drag maths in model space (Y-up, CCW-positive): the camera
    // pivot is the pose origin, and the start angle is measured there.
    m_sessionDragPivotStart = gizmo::worldPivot(m_sessionDragTransform);
    m_sessionCameraStartAngle =
        std::atan2(m_sessionDragStartParent.y() - m_sessionDragPivotStart.y(),
                   m_sessionDragStartParent.x() - m_sessionDragPivotStart.x())
        * 180.0 / M_PI;
    collectSessionSnapTargets(uuid);
    m_sessionDragChanged = false;
    m_showGuideX = false;
    m_showGuideY = false;
    setCursor(part == CameraPart::Move ? Qt::SizeAllCursor
               : part == CameraPart::Rotate ? Qt::CrossCursor
                                            : Qt::SizeFDiagCursor);
    return true;
}

void Viewport::applySessionCameraDrag(const QPointF& pos,
                                      Qt::KeyboardModifiers modifiers)
{
    if (!m_session || m_sessionDragUuid.isEmpty())
        return;
    auto it = m_session->mutableProject().entities.find(m_sessionDragUuid);
    if (it == m_session->mutableProject().entities.end() || it->locked)
        return;
    model::Entity& entity = it.value();
    const gizmo::Transform start = m_sessionDragTransform;
    const QPointF mouse = sessionScreenToProject(pos);
    const QPointF delta = mouse - m_sessionDragStartParent;
    const bool snapActive = !modifiers.testFlag(Qt::ControlModifier);
    m_showGuideX = false;
    m_showGuideY = false;

    if (m_sessionDragKind == SessionDragKind::CameraMove)
    {
        double x = start.x + delta.x();
        double y = start.y + delta.y();
        if (snapActive)
        {
            const next::AppSettings& settings = m_session->appSettings();
            const double threshold =
                double(std::max(1, settings.snapPixels)) / viewScale();
            const QPointF center = QPointF(start.x + start.pivotX,
                                           start.y + start.pivotY);
            // Model origin is the canvas centre.
            const double canvasCenterX = 0.0;
            const double canvasCenterY = 0.0;
            if (settings.snapCanvasCenter)
            {
                if (std::abs(center.x() - canvasCenterX) <= threshold)
                {
                    x += canvasCenterX - center.x();
                    m_showGuideX = true;
                    m_guideX = canvasCenterX;
                }
                if (std::abs(center.y() - canvasCenterY) <= threshold)
                {
                    y += canvasCenterY - center.y();
                    m_showGuideY = true;
                    m_guideY = canvasCenterY;
                }
            }
            if (!m_sessionSnapTargetsX.isEmpty())
            {
                const auto snap = nearestFeatureSnap(
                    {center.x(), center.x(), center.x()}, m_sessionSnapTargetsX,
                    threshold);
                if (snap)
                {
                    x += *snap;
                    m_showGuideX = true;
                    m_guideX = center.x() + *snap;
                }
            }
            if (!m_sessionSnapTargetsY.isEmpty())
            {
                const auto snap = nearestFeatureSnap(
                    {center.y(), center.y(), center.y()}, m_sessionSnapTargetsY,
                    threshold);
                if (snap)
                {
                    y += *snap;
                    m_showGuideY = true;
                    m_guideY = center.y() + *snap;
                }
            }
        }
        writeSessionGizmoValue(entity, QStringLiteral("x"), x);
        writeSessionGizmoValue(entity, QStringLiteral("y"), y);
    }
    else if (m_sessionDragKind == SessionDragKind::CameraRotate)
    {
        // Model space: +Y up, positive angles counter-clockwise.  The drag
        // delta is measured about the model pivot, so a clockwise drag lowers
        // the angle exactly like the entity gizmo.
        const QPointF center = m_sessionDragPivotStart;
        const double angle =
            std::atan2(mouse.y() - center.y(), mouse.x() - center.x()) * 180.0
            / M_PI;
        double rotationDelta = angle - m_sessionCameraStartAngle;
        while (rotationDelta > 180.0)
            rotationDelta -= 360.0;
        while (rotationDelta < -180.0)
            rotationDelta += 360.0;
        double rotation = start.rotation + rotationDelta;
        if (snapActive)
            rotation = snapSessionAngle(rotation,
                                        modifiers.testFlag(Qt::ShiftModifier));
        writeSessionGizmoValue(entity, QStringLiteral("rotation"), rotation);
    }
    else if (m_sessionDragKind == SessionDragKind::CameraScale)
    {
        const QPointF center = QPointF(start.x + start.pivotX,
                                       start.y + start.pivotY);
        const QPointF local = rotateVector(mouse - center, -start.rotation);
        const double width = std::max(1, m_session->settings().width);
        const double height = std::max(1, m_session->settings().height);
        double scale = std::max(std::abs(local.x()) * 2.0 / width,
                                std::abs(local.y()) * 2.0 / height);
        scale = std::clamp(scale, 0.05, 1.0);
        writeSessionGizmoValue(entity, QStringLiteral("scaleX"), scale);
        writeSessionGizmoValue(entity, QStringLiteral("scaleY"), scale);
    }
    else
    {
        return;
    }

    m_sessionDragChanged =
        !transformsEqual(sessionGizmoTransform(entity), start);
    invalidateSessionImage();
    update();
}

void Viewport::drawSessionCameraOverlay(QPainter& painter)
{
    if (!m_session || !m_showCameraFrame)
        return;
    const Uuid uuid = sessionCameraUuid();
    if (uuid.isEmpty())
        return;
    const model::Project& project = m_session->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd()
        || it->content.kind != ContentKind::Camera)
        return;

    const QPolygonF polygon = sessionCameraPolygon();
    if (polygon.isEmpty())
        return;

    const bool selected =
        m_session->document() && m_session->document()->selection() == uuid;
    const bool locked = it->locked;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath veil;
    veil.setFillRule(Qt::OddEvenFill);
    veil.addRect(QRectF(rect()));
    veil.addPolygon(polygon);
    veil.closeSubpath();
    painter.fillPath(veil, QColor(10, 10, 14, 140));

    QPen framePen(QColor(90, 210, 255, selected ? 255 : 200),
                  selected ? 2.0 : 1.2, Qt::DashLine);
    if (locked)
        framePen.setColor(QColor(150, 150, 160, 200));
    painter.setPen(framePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);

    const QRectF title = sessionCameraTitleRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 20, 26, 225));
    painter.drawRect(title);
    painter.setPen(QColor(90, 210, 255, 220));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(title);
    painter.setPen(locked ? QColor(170, 170, 180) : QColor(235, 245, 255));
    painter.drawText(title.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     it->name);

    if (selected && !locked && it->parentUuid.isEmpty())
    {
        painter.setPen(QPen(QColor(20, 20, 26), 1.0));
        painter.setBrush(QColor(90, 210, 255));
        for (const QPointF& corner : polygon)
            painter.drawRect(QRectF(corner.x() - 4.0, corner.y() - 4.0, 8.0, 8.0));

        const QPointF handle = sessionCameraRotationHandle();
        const QPointF topCenter = (polygon[0] + polygon[1]) / 2.0;
        painter.setPen(QPen(QColor(90, 210, 255), 1.5));
        painter.drawLine(topCenter, handle);
        painter.setBrush(QColor(20, 24, 30));
        painter.drawEllipse(handle, 5.0, 5.0);
    }

    painter.restore();
}

void Viewport::paintSession(QPainter& p)
{
    QElapsedTimer profile;
    profile.start();

    p.fillRect(rect(), QApplication::palette().color(QPalette::Window));

    const QRectF canvas = canvasRect();
    const int expand = sessionExpandPercent();

    p.fillRect(canvas.translated(6, 6), QColor(0, 0, 0, 90));

    const QImage image = ensureSessionImage();
    QRectF target = rect();
    if (expand > 0)
        target = sessionSceneRect();
    if (!image.isNull())
        p.drawImage(target, image);

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

    if (!image.isNull() && !m_sessionMediaMissing)
        emit framePresented(m_session->playhead());
}

EntityId Viewport::activeCameraEntity() const
{
    const Entity* entity = m_project->entity(m_project->activeCamera);
    if (!entity || !entity->isCamera()
        || m_project->isEffectivelyHidden(entity->id))
        return kInvalidEntity;
    return entity->id;
}

Camera::State Viewport::cameraState(const Entity& e) const
{
    return Camera::state(*m_project, e.id, m_frame);
}

QPolygonF Viewport::cameraScreenPolygon() const
{
    if (sessionMode())
        return sessionCameraPolygon();
    if (!m_showCameraFrame)
        return QPolygonF();
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPolygonF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPolygonF();
    return worldToScreenTransform().map(Camera::polygon(cameraState(*entity)));
}

QPointF Viewport::cameraScreenCenter() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPointF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPointF();
    return worldToScreenTransform().map(cameraState(*entity).center);
}

QPointF Viewport::cameraRotationHandle() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QPointF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QPointF();

    const Camera::State state = cameraState(*entity);
    const QPointF topLocal(0.0, -state.size.height() / 2.0);
    const QPointF topProject = Camera::frameToProject(state).map(topLocal);
    const QPointF topScreen = worldToScreenTransform().map(topProject);

    QTransform direction;
    direction.rotate(state.rotation);
    const QPointF up = direction.map(QPointF(0.0, -1.0));
    return topScreen + up * 28.0;
}

QRectF Viewport::cameraTitleRect() const
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return QRectF();
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return QRectF();

    const QPolygonF polygon = cameraScreenPolygon();
    if (polygon.isEmpty())
        return QRectF();
    const QRectF box = polygon.boundingRect();
    const QFontMetrics metrics(font());
    const int textWidth = metrics.horizontalAdvance(entity->name);
    const QRectF title(box.left(), box.top() - 24.0, textWidth + 16.0, 20.0);
    if (title.top() < 2.0)
        return QRectF(box.left(), box.top() + 4.0, textWidth + 16.0, 20.0);
    return title;
}

Viewport::CameraPart Viewport::cameraHitTest(const QPointF& pos) const
{
    if (!m_showCameraFrame)
        return CameraPart::None;
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return CameraPart::None;
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return CameraPart::None;

    const bool selected = (m_selected == id);
    if (selected && !entity->locked && entity->parent == kInvalidEntity)
    {
        if (QLineF(pos, cameraRotationHandle()).length() <= 10.0)
            return CameraPart::Rotate;

        const QPolygonF polygon = cameraScreenPolygon();
        for (int i = 0; i < polygon.size(); ++i)
        {
            if (QLineF(pos, polygon[i]).length() <= 8.0)
                return CameraPart(i == 0 ? CameraPart::Corner0
                                  : i == 1 ? CameraPart::Corner1
                                  : i == 2 ? CameraPart::Corner2
                                           : CameraPart::Corner3);
        }
    }

    if (cameraTitleRect().contains(pos))
        return CameraPart::Move;

    const QPolygonF polygon = cameraScreenPolygon();
    for (int i = 0; i < polygon.size(); ++i)
    {
        if (distanceToSegment(pos, polygon[i], polygon[(i + 1) % polygon.size()]) <= 6.0)
            return CameraPart::Move;
    }
    return CameraPart::None;
}

bool Viewport::handleCameraPress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return false;

    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return false;

    const CameraPart part = cameraHitTest(event->position());
    if (part == CameraPart::None)
        return false;

    Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(id))
        return true;

    emit entitySelected(id);

    // A parented camera follows its parent; direct frame dragging would write
    // project-space values into the parent's coordinate system.
    if (entity->parent != kInvalidEntity)
        return true;

    m_cameraPart = part;
    m_cameraStartState = cameraState(*entity);
    m_cameraStartMouse = event->position();
    const QPointF center = cameraScreenCenter();
    m_cameraStartAngle =
        std::atan2(event->position().y() - center.y(), event->position().x() - center.x())
            * 180.0 / M_PI
        + 90.0;
    m_dragging = false;
    m_gizmoMode = GizmoMode::None;
    m_activeHandle = Handle::None;
    m_dragEntity = kInvalidEntity;
    setCursor(part == CameraPart::Move ? Qt::SizeAllCursor
                                       : part == CameraPart::Rotate ? Qt::CrossCursor
                                                                    : Qt::SizeFDiagCursor);
    update();
    return true;
}

QPointF Viewport::snapCameraCenter(const Camera::State& start, const QPointF& center,
                                   bool* snappedX, double* guideX, bool* snappedY,
                                   double* guideY) const
{
    *snappedX = false;
    *snappedY = false;
    if (m_snapPixels <= 0)
        return center;

    const double threshold = m_snapPixels / viewScale();
    Camera::State candidate = start;
    candidate.center = center;
    const QRectF box = Camera::polygon(candidate).boundingRect();

    QVector<double> targetsX = {-m_project->width / 2.0, 0.0, m_project->width / 2.0};
    QVector<double> targetsY = {-m_project->height / 2.0, 0.0, m_project->height / 2.0};
    const EntityId cameraId = activeCameraEntity();
    for (const Entity& other : m_project->entities())
    {
        if (other.id == cameraId || other.hidden || other.isCamera()
            || other.isFolder()
            || m_project->effectiveOpacity(other.id, m_frame) <= 0.0)
            continue;
        const VisualBounds bounds =
            visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                           m_project->value(other.id, "y", m_frame));
        targetsX << bounds.left << bounds.right;
        targetsY << bounds.top << bounds.bottom;
        if (Settings::edgeMidpointSnap())
        {
            targetsX << (bounds.left + bounds.right) / 2.0;
            targetsY << (bounds.top + bounds.bottom) / 2.0;
        }
    }

    QPointF result = center;
    double bestX = threshold + 1.0;
    double bestTargetX = 0.0;
    for (double target : targetsX)
    {
        const double features[] = {box.left(), box.center().x(), box.right()};
        for (double feature : features)
        {
            const double distance = std::abs(feature - target);
            if (distance < bestX)
            {
                bestX = distance;
                bestTargetX = target;
            }
        }
    }
    if (bestX <= threshold)
    {
        result.setX(center.x() + (bestTargetX - box.center().x()));
        // Pick the frame feature that was closest to the winning target.
        double bestFeatureDistance = 1e9;
        double adjustment = 0.0;
        for (double feature : {box.left(), box.center().x(), box.right()})
        {
            const double distance = std::abs(feature - bestTargetX);
            if (distance < bestFeatureDistance)
            {
                bestFeatureDistance = distance;
                adjustment = bestTargetX - feature;
            }
        }
        result.setX(center.x() + adjustment);
        *snappedX = true;
        *guideX = bestTargetX;
    }

    double bestY = threshold + 1.0;
    double bestTargetY = 0.0;
    for (double target : targetsY)
    {
        for (double feature : {box.top(), box.center().y(), box.bottom()})
        {
            const double distance = std::abs(feature - target);
            if (distance < bestY)
            {
                bestY = distance;
                bestTargetY = target;
            }
        }
    }
    if (bestY <= threshold)
    {
        double bestFeatureDistance = 1e9;
        double adjustment = 0.0;
        for (double feature : {box.top(), box.center().y(), box.bottom()})
        {
            const double distance = std::abs(feature - bestTargetY);
            if (distance < bestFeatureDistance)
            {
                bestFeatureDistance = distance;
                adjustment = bestTargetY - feature;
            }
        }
        result.setY(center.y() + adjustment);
        *snappedY = true;
        *guideY = bestTargetY;
    }

    return result;
}

void Viewport::applyCameraDrag(const QPointF& pos, Qt::KeyboardModifiers modifiers)
{
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity || m_cameraPart == CameraPart::None)
        return;
    Entity* entity = m_project->entity(id);
    if (!entity || m_project->isEffectivelyLocked(entity->id))
        return;

    const Camera::State start = m_cameraStartState;
    m_showGuideX = false;
    m_showGuideY = false;

    if (m_cameraPart == CameraPart::Move)
    {
        const double s = viewScale();
        QPointF center = start.center
                         + QPointF((pos.x() - m_cameraStartMouse.x()) / s,
                                   (pos.y() - m_cameraStartMouse.y()) / s);
        bool snappedX = false;
        bool snappedY = false;
        double guideX = 0.0;
        double guideY = 0.0;
        if (!(modifiers & Qt::ControlModifier))
            center = snapCameraCenter(start, center, &snappedX, &guideX, &snappedY,
                                      &guideY);
        center = Camera::clampCenter(*m_project, start.scale, center);
        m_showGuideX = snappedX;
        m_showGuideY = snappedY;
        m_guideX = guideX;
        m_guideY = guideY;
        emit cameraEdited(id, center.x(), center.y(), start.scale, start.rotation);
        update();
        return;
    }

    if (m_cameraPart == CameraPart::Rotate)
    {
        const QPointF center = worldToScreenTransform().map(start.center);
        double angle =
            std::atan2(pos.y() - center.y(), pos.x() - center.x()) * 180.0 / M_PI + 90.0;
        double delta = angle - m_cameraStartAngle;
        while (delta > 180.0)
            delta -= 360.0;
        while (delta < -180.0)
            delta += 360.0;

        double rotation = start.rotation + delta;
        if (!(modifiers & Qt::ControlModifier))
        {
            constexpr double tolerance = 3.0;
            for (int snapAngle : Settings::snapAngles())
            {
                const double diff = std::remainder(rotation - double(snapAngle), 360.0);
                if (std::abs(diff) <= tolerance)
                {
                    rotation = snapAngle;
                    break;
                }
            }
        }
        emit cameraEdited(id, start.center.x(), start.center.y(), start.scale, rotation);
        update();
        return;
    }

    // Corner scaling keeps the camera centre fixed; the frame aspect follows
    // the project automatically.
    const QPointF mouse = screenToCanvas(pos);
    const QPointF delta = mouse - start.center;
    const double radians = -start.rotation * M_PI / 180.0;
    const double localX = delta.x() * std::cos(radians) - delta.y() * std::sin(radians);
    const double localY = delta.x() * std::sin(radians) + delta.y() * std::cos(radians);
    double scale = std::max(std::abs(localX) * 2.0 / std::max(1, m_project->width),
                            std::abs(localY) * 2.0 / std::max(1, m_project->height));
    scale = std::clamp(scale, Camera::kMinScale, Camera::kMaxScale);
    const QPointF center = Camera::clampCenter(*m_project, scale, start.center);
    emit cameraEdited(id, center.x(), center.y(), scale, start.rotation);
    update();
}

void Viewport::drawCameraOverlay(QPainter& painter)
{
    if (!m_showCameraFrame)
        return;
    const EntityId id = activeCameraEntity();
    if (id == kInvalidEntity)
        return;
    const Entity* entity = m_project->entity(id);
    if (!entity)
        return;

    const Camera::State state = cameraState(*entity);
    const QPolygonF polygon = cameraScreenPolygon();
    if (polygon.isEmpty())
        return;

    const bool selected = (m_selected == id);
    const bool locked = entity->locked;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Even-odd fill cuts the camera polygon out of the veil without a
    // geometry subtraction, which was costly on every repaint.
    QPainterPath veil;
    veil.setFillRule(Qt::OddEvenFill);
    veil.addRect(QRectF(rect()));
    veil.addPolygon(polygon);
    veil.closeSubpath();
    painter.fillPath(veil, QColor(10, 10, 14, 140));

    QPen framePen(QColor(90, 210, 255, selected ? 255 : 200),
                  selected ? 2.0 : 1.2, Qt::DashLine);
    if (locked)
        framePen.setColor(QColor(150, 150, 160, 200));
    painter.setPen(framePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);

    const QRectF title = cameraTitleRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 20, 26, 225));
    painter.drawRect(title);
    painter.setPen(QColor(90, 210, 255, 220));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(title);
    painter.setPen(locked ? QColor(170, 170, 180) : QColor(235, 245, 255));
    painter.drawText(title.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     entity->name);

    if (selected && !locked && entity->parent == kInvalidEntity)
    {
        painter.setPen(QPen(QColor(20, 20, 26), 1.0));
        painter.setBrush(QColor(90, 210, 255));
        for (const QPointF& corner : polygon)
            painter.drawRect(QRectF(corner.x() - 4.0, corner.y() - 4.0, 8.0, 8.0));

        const QPointF handle = cameraRotationHandle();
        QPolygonF frame = polygon;
        QPointF topCenter = (frame[0] + frame[1]) / 2.0;
        if (frame.size() >= 2)
            topCenter = (frame[0] + frame[1]) / 2.0;
        painter.setPen(QPen(QColor(90, 210, 255), 1.5));
        painter.drawLine(topCenter, handle);
        painter.setBrush(QColor(20, 24, 30));
        painter.drawEllipse(handle, 5.0, 5.0);
    }

    painter.restore();
}


QRectF Viewport::entityLocalBounds(const Entity& e) const
{
    return SceneRenderer::entityLocalBounds(e);
}

const Entity* Viewport::hitTest(const QPointF& pos) const
{
    // The first row of the layer list is the topmost entity.
    const QVector<int> order = m_project->entityOrder();
    for (int oi = 0; oi < int(order.size()); ++oi)
    {
        const Entity& e = m_project->entities()[order[oi]];
        if (e.isFolder() || e.isCamera()
            || m_project->isEffectivelyHidden(e.id)
            || m_project->isEffectivelyLocked(e.id)
            || !m_project->isEntityActiveAt(e.id, m_frame)
            || m_project->effectiveOpacity(e.id, m_frame) <= 0.0)
        {
            continue;
        }
        const QRectF bounds = entityLocalBounds(e);
        if (bounds.isEmpty())
            continue;

        bool ok = false;
        const QTransform inverse = entityTransform(e).inverted(&ok);
        if (!ok)
            continue;

        if (bounds.contains(inverse.map(pos)))
            return &e;
    }
    return nullptr;
}

void Viewport::drawEntity(QPainter& p, const Entity& e, const QRectF&, double)
{
    if (e.hidden || e.isCamera() || e.isFolder()
        || m_project->isEffectivelyHidden(e.id))
        return;

    if (m_project->effectiveOpacity(e.id, m_frame) <= 0.0)
        return;

    const QRectF bounds = entityLocalBounds(e);
    if (bounds.isEmpty())
        return;

    SceneRenderer::drawEntity(p, *m_project, e, m_frame, worldToScreenTransform(), false);

    if (e.id == m_selected && !e.locked && !m_project->isEffectivelyLocked(e.id))
    {
        p.save();
        p.setTransform(entityTransform(e));
        QPen pen(QColor(77, 163, 255));
        pen.setCosmetic(true);
        pen.setWidthF(1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(bounds);
        p.restore();
    }
}

QPointF Viewport::entityScreenCenter(const Entity& e) const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    const double x = m_project->value(e.id, "x", m_frame) * s;
    const double y = m_project->value(e.id, "y", m_frame) * s;
    const double rotation = m_project->value(e.id, "rotation", m_frame);
    const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
    const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;

    const double rad = qDegreesToRadians(rotation);
    const double dx = ax * std::cos(rad) - ay * std::sin(rad);
    const double dy = ax * std::sin(rad) + ay * std::cos(rad);

    return canvas.center() + QPointF(x - dx, y - dy);
}

QTransform Viewport::entityTransformForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? entityTransform(*entity) : QTransform();
}

QPointF Viewport::entityPivotForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? entityPivotScreen(*entity) : QPointF();
}

QPointF Viewport::comicTipForTest(EntityId id) const
{
    const Entity* entity = m_project->entity(id);
    return entity ? comicTipScreenPos(*entity) : QPointF();
}

QPointF Viewport::entityPivotScreen(const Entity& e) const
{
    // X/Y are the pivot position in parent space. Applying the parent chain
    // (without the entity's own scale) keeps the gizmo on the pivot.
    const double x = m_project->value(e.id, "x", m_frame);
    const double y = m_project->value(e.id, "y", m_frame);
    const QPointF world =
        SceneRenderer::parentWorldTransform(*m_project, e, m_frame)
            .map(QPointF(x, y));
    return worldToScreenTransform().map(world);
}

Viewport::VisualBounds Viewport::visualBoundsOf(const Entity& e, double posX,
                                                double posY) const
{
    const QRectF bounds = entityLocalBounds(e);
    const double rotation = m_project->value(e.id, "rotation", m_frame);
    const double radians = rotation * M_PI / 180.0;
    const double cosA = std::abs(std::cos(radians));
    const double sinA = std::abs(std::sin(radians));
    const double ax = m_project->value(e.id, "rotPointX", m_frame);
    const double ay = m_project->value(e.id, "rotPointY", m_frame);
    const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
    const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
    const double halfW = bounds.width() / 2.0
                         * std::abs(m_project->value(e.id, "scaleX", m_frame));
    const double halfH = bounds.height() / 2.0
                         * std::abs(m_project->value(e.id, "scaleY", m_frame));
    const double aabbHalfW = halfW * cosA + halfH * sinA;
    const double aabbHalfH = halfW * sinA + halfH * cosA;
    const double centerX = posX - rotatedAx;
    const double centerY = posY - rotatedAy;

    VisualBounds result;
    result.left = centerX - aabbHalfW;
    result.right = centerX + aabbHalfW;
    result.top = centerY - aabbHalfH;
    result.bottom = centerY + aabbHalfH;
    return result;
}

QTransform Viewport::entityTransform(const Entity& e) const
{
    return SceneRenderer::worldTransform(*m_project, e, m_frame)
           * worldToScreenTransform();
}

QTransform Viewport::worldToScreenTransform() const
{
    const QRectF canvas = canvasRect();
    const double s = viewScale();
    QTransform transform;
    transform.translate(canvas.center().x(), canvas.center().y());
    transform.scale(s, s);
    return transform;
}

QTransform Viewport::entityRotationTransform(const Entity& e) const
{
    const double s = viewScale();
    const QRectF canvas = canvasRect();
    const QPointF origin =
        canvas.center() + QPointF(m_project->value(e.id, "x", m_frame) * s,
                                  m_project->value(e.id, "y", m_frame) * s);
    const double rotation = m_project->value(e.id, "rotation", m_frame);

    const double ax = m_project->value(e.id, "rotPointX", m_frame) * s;
    const double ay = m_project->value(e.id, "rotPointY", m_frame) * s;

    QTransform transform;
    transform.translate(origin.x(), origin.y());
    transform.rotate(rotation);
    transform.translate(-ax, -ay);
    return transform;
}

bool Viewport::gizmoHitTest(const QPointF& pos, GizmoMode* mode) const
{
    if (m_selected == kInvalidEntity)
        return false;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity || entity->isCamera()
        || m_project->isEffectivelyLocked(entity->id))
        return false;

    const QPointF delta = pos - entityPivotScreen(*entity);
    const double distance = std::hypot(delta.x(), delta.y());

    // Dragging the pivot dot moves the custom pivot freely.
    if (entity->rotPointCustom() && distance <= 10.0)
    {
        *mode = GizmoMode::PivotFree;
        return true;
    }

    // The rotation ring sits on top of the axes.
    if (std::abs(distance - kGizmoRingRadius) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::Rotate;
        return true;
    }

    // Close to the centre the entity itself is grabbed instead.
    if (distance <= kGizmoCenterDeadzone)
        return false;

    // X points right, Y points up: a right angle like Mine-imator.
    if (delta.x() > kGizmoCenterDeadzone && delta.x() <= kGizmoAxisLength
        && std::abs(delta.y()) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::AxisX;
        return true;
    }
    if (delta.y() < -kGizmoCenterDeadzone && delta.y() >= -kGizmoAxisLength
        && std::abs(delta.x()) <= kGizmoHitWidth)
    {
        *mode = GizmoMode::AxisY;
        return true;
    }
    return false;
}

void Viewport::drawGizmo(QPainter& p, const Entity& e)
{
    const QPointF center = entityPivotScreen(e);
    const double len = kGizmoAxisLength;
    const double ring = kGizmoRingRadius;

    const auto arrow = [&p](const QPointF& tip, const QPointF& back, const QColor& color) {
        const QPointF dir = tip - back;
        const double length = std::hypot(dir.x(), dir.y());
        if (length < 1.0)
            return;
        const QPointF unit = dir / length;
        const QPointF side(-unit.y(), unit.x());
        QPolygonF triangle;
        triangle << tip << tip - unit * 11.0 + side * 5.0 << tip - unit * 11.0 - side * 5.0;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(triangle);
    };

    // X axis: right only.
    const bool hotX = m_gizmoMode == GizmoMode::AxisX
                      || m_gizmoMode == GizmoMode::PivotX;
    const QColor xColor = hotX ? QColor(255, 150, 150) : QColor(226, 86, 86);
    p.setPen(QPen(xColor, hotX ? 4.0 : 3.0));
    p.drawLine(center, center + QPointF(len, 0));
    arrow(center + QPointF(len, 0), center, xColor);

    // Y axis: up only.
    const bool hotY = m_gizmoMode == GizmoMode::AxisY
                      || m_gizmoMode == GizmoMode::PivotY;
    const QColor yColor = hotY ? QColor(150, 235, 150) : QColor(96, 200, 96);
    p.setPen(QPen(yColor, hotY ? 4.0 : 3.0));
    p.drawLine(center, center - QPointF(0, len));
    arrow(center - QPointF(0, len), center, yColor);

    // Pivot dot: drag to move the custom pivot.
    if (e.rotPointCustom())
    {
        p.setBrush(QColor(255, 255, 255, 230));
        p.setPen(QPen(QColor(120, 170, 255), 1.5));
        p.drawEllipse(center, 5, 5);
    }

    // Rotation ring.
    const bool hotRotate = m_gizmoMode == GizmoMode::Rotate;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(hotRotate ? QColor(255, 214, 120) : QColor(210, 200, 160),
                  hotRotate ? 3.5 : 2.0));
    p.drawEllipse(center, ring, ring);
}

QPointF Viewport::handleScreenPos(const Entity& e, Handle handle) const
{
    const QRectF bounds = entityLocalBounds(e);
    QPointF local;
    switch (handle)
    {
    case Handle::ComicTip:
        return comicTipScreenPos(e);
    case Handle::TopLeft:
        local = bounds.topLeft();
        break;
    case Handle::Top:
        local = QPointF(bounds.center().x(), bounds.top());
        break;
    case Handle::TopRight:
        local = bounds.topRight();
        break;
    case Handle::Right:
        local = QPointF(bounds.right(), bounds.center().y());
        break;
    case Handle::BottomRight:
        local = bounds.bottomRight();
        break;
    case Handle::Bottom:
        local = QPointF(bounds.center().x(), bounds.bottom());
        break;
    case Handle::BottomLeft:
        local = bounds.bottomLeft();
        break;
    case Handle::Left:
        local = QPointF(bounds.left(), bounds.center().y());
        break;
    case Handle::None:
        return QPointF();
    }

    return entityTransform(e).map(local);
}

QPointF Viewport::comicTipScreenPos(const Entity& e) const
{
    const ComicBox box =
        ComicBox::build(TextStyle::fromData(e.data), BubbleStyle::fromData(e.data));
    return entityTransform(e).map(box.tip());
}

QPointF Viewport::screenToEntityLocal(const Entity& e, const QPointF& screenPos) const
{
    bool ok = false;
    const QTransform inverse = entityTransform(e).inverted(&ok);
    if (!ok)
        return QPointF();
    return inverse.map(screenPos);
}

Viewport::Handle Viewport::hitHandle(const QPointF& pos) const
{
    if (m_selected == kInvalidEntity)
        return Handle::None;
    const Entity* entity = m_project->entity(m_selected);
    if (!entity || entity->isCamera()
        || m_project->isEffectivelyLocked(entity->id))
        return Handle::None;

    // The comic balloon spike has its own handle, drawn on the tip.
    if (entity->type == EntityType::ComicText
        && entity->data.value(QStringLiteral("bubbleShape"), 0).toInt() != 3
        && entity->data.value(QStringLiteral("bubbleTail"), true).toBool())
    {
        const QPointF tip = comicTipScreenPos(*entity);
        if (std::hypot(tip.x() - pos.x(), tip.y() - pos.y()) <= 8.0)
            return Handle::ComicTip;
    }

    // Only corner handles: edge midpoints sit exactly on the gizmo axes and
    // would steal axis drags.
    static const Handle handles[] = {Handle::TopLeft, Handle::TopRight,
                                     Handle::BottomRight, Handle::BottomLeft};
    for (Handle handle : handles)
    {
        const QPointF point = handleScreenPos(*entity, handle);
        if (std::hypot(point.x() - pos.x(), point.y() - pos.y()) <= 6.0)
            return handle;
    }
    return Handle::None;
}

void Viewport::drawHandles(QPainter& p, const Entity& e)
{
    // Only corner handles: edge midpoints sit exactly on the gizmo axes and
    // would steal axis drags.
    static const Handle handles[] = {Handle::TopLeft, Handle::TopRight,
                                     Handle::BottomRight, Handle::BottomLeft};
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    p.setPen(QPen(accent, 1.5));
    p.setBrush(Qt::white);
    for (Handle handle : handles)
    {
        const QPointF point = handleScreenPos(e, handle);
        p.drawRect(QRectF(point.x() - 4, point.y() - 4, 8, 8));
    }

    if (e.type == EntityType::ComicText
        && e.data.value(QStringLiteral("bubbleShape"), 0).toInt() != 3
        && e.data.value(QStringLiteral("bubbleTail"), true).toBool())
    {
        const QPointF tip = comicTipScreenPos(e);
        p.drawEllipse(QPointF(tip), 5.0, 5.0);
    }
}

void Viewport::applyHandleDrag(const QPointF& mousePos, bool uniform, bool snap)
{
    const Entity* entity = m_project->entity(m_dragEntity);
    if (!entity)
        return;

    const QRectF bounds = entityLocalBounds(*entity);
    const double s = viewScale();
    const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
    const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
    const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
    const bool parented = entity->parent != kInvalidEntity;
    m_showGuideX = false;
    m_showGuideY = false;

    // Measure against the drag-start centre: the fixed opposite corner moves
    // the entity while dragging, so the current centre would drift.
    const QTransform parentTransform =
        SceneRenderer::parentWorldTransform(*m_project, *entity, m_frame);
    QTransform childBase;
    childBase.translate(m_scaleStartPosX, m_scaleStartPosY);
    childBase.rotate(rotation);
    childBase.translate(-ax, -ay);
    QTransform base =
        childBase * parentTransform * worldToScreenTransform();
    bool ok = false;
    const QPointF local = base.inverted(&ok).map(mousePos);
    if (!ok)
        return;

    const double halfW = std::max(1.0, bounds.width() / 2.0);
    const double halfH = std::max(1.0, bounds.height() / 2.0);

    double signX = 0.0;
    double signY = 0.0;
    switch (m_activeHandle)
    {
    case Handle::ComicTip:
        return;
    case Handle::TopLeft:
        signX = -1.0;
        signY = -1.0;
        break;
    case Handle::Top:
        signY = -1.0;
        break;
    case Handle::TopRight:
        signX = 1.0;
        signY = -1.0;
        break;
    case Handle::Right:
        signX = 1.0;
        break;
    case Handle::BottomRight:
        signX = 1.0;
        signY = 1.0;
        break;
    case Handle::Bottom:
        signY = 1.0;
        break;
    case Handle::BottomLeft:
        signX = -1.0;
        signY = 1.0;
        break;
    case Handle::Left:
        signX = -1.0;
        break;
    case Handle::None:
        return;
    }

    double newScaleX = m_scaleStartX;
    double newScaleY = m_scaleStartY;
    if (signX != 0.0)
        newScaleX = signX * local.x() / halfW;
    if (signY != 0.0)
        newScaleY = signY * local.y() / halfH;

    if (uniform)
    {
        const double ratio = (std::abs(m_scaleStartX) > 1e-6)
                                 ? m_scaleStartY / m_scaleStartX
                                 : 1.0;
        if (signX != 0.0 && signY != 0.0)
        {
            if (std::abs(newScaleX) >= std::abs(newScaleY))
                newScaleY = newScaleX * ratio;
            else
                newScaleX = newScaleY / ratio;
        }
        else if (signX != 0.0)
        {
            newScaleY = newScaleX * ratio;
        }
        else if (signY != 0.0)
        {
            newScaleX = newScaleY / ratio;
        }
    }

    const double currentX = m_project->value(m_dragEntity, "x", m_frame);
    const double currentY = m_project->value(m_dragEntity, "y", m_frame);

    // Shift: uniform scaling around the entity anchor. Keeping the anchor
    // fixed makes it possible to fill the canvas from the centre, and the
    // visual AABB can snap to canvas/entity edges even when rotated or inside
    // a transformed parent.
    if (uniform)
    {
        const QRectF localBounds = entityLocalBounds(*entity);
        QTransform localTransform;
        localTransform.translate(currentX, currentY);
        localTransform.rotate(rotation);
        localTransform.translate(-ax, -ay);
        localTransform.scale(newScaleX, newScaleY);
        const QTransform visual = localTransform * parentTransform;

        const QPointF c0 = visual.map(localBounds.topLeft());
        const QPointF c1 = visual.map(localBounds.topRight());
        const QPointF c2 = visual.map(localBounds.bottomRight());
        const QPointF c3 = visual.map(localBounds.bottomLeft());
        const double minX = std::min({c0.x(), c1.x(), c2.x(), c3.x()});
        const double maxX = std::max({c0.x(), c1.x(), c2.x(), c3.x()});
        const double minY = std::min({c0.y(), c1.y(), c2.y(), c3.y()});
        const double maxY = std::max({c0.y(), c1.y(), c2.y(), c3.y()});
        const double centerX = (minX + maxX) / 2.0;
        const double centerY = (minY + maxY) / 2.0;
        double halfVisualW = (maxX - minX) / 2.0;
        double halfVisualH = (maxY - minY) / 2.0;

        if (snap && m_snapPixels > 0 && halfVisualW > 1e-9 && halfVisualH > 1e-9)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double canvasHalfH = m_project->height / 2.0;
            QVector<double> targetsX = {-canvasHalfW, 0.0, canvasHalfW};
            QVector<double> targetsY = {-canvasHalfH, 0.0, canvasHalfH};

            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden || other.isFolder()
                    || m_project->effectiveOpacity(other.id, m_frame) <= 0.0)
                    continue;
                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds otherBounds =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));
                targetsX << otherBounds.left << otherBounds.right;
                targetsY << otherBounds.top << otherBounds.bottom;
                if (Settings::edgeMidpointSnap())
                {
                    targetsX << (otherBounds.left + otherBounds.right) / 2.0;
                    targetsY << (otherBounds.top + otherBounds.bottom) / 2.0;
                }
            }

            double bestDelta = threshold;
            double bestFactor = 1.0;
            double bestTarget = 0.0;
            bool bestIsX = false;
            const auto considerTarget = [&](double target, double center,
                                            double half, bool isX) {
                const double targetHalf = std::abs(target - center);
                if (targetHalf <= 1e-9)
                    return;
                const double delta = std::abs(targetHalf - half);
                if (delta <= bestDelta)
                {
                    bestDelta = delta;
                    bestFactor = targetHalf / half;
                    bestTarget = target;
                    bestIsX = isX;
                }
            };
            for (double target : targetsX)
                considerTarget(target, centerX, halfVisualW, true);
            for (double target : targetsY)
                considerTarget(target, centerY, halfVisualH, false);

            if (bestFactor != 1.0)
            {
                newScaleX *= bestFactor;
                newScaleY *= bestFactor;
                halfVisualW *= bestFactor;
                halfVisualH *= bestFactor;
                if (bestIsX)
                {
                    m_showGuideX = true;
                    m_guideX = bestTarget;
                }
                else
                {
                    m_showGuideY = true;
                    m_guideY = bestTarget;
                }
            }
        }

        emit scaleEdited(m_dragEntity, newScaleX, newScaleY, currentX, currentY);
        return;
    }

    // PPT-style corner scaling: the opposite corner stays fixed.
    if (std::abs(rotation) < 1e-6 && signX != 0.0 && signY != 0.0)
    {
        const double startCenterX = m_scaleStartPosX - ax;
        const double startCenterY = m_scaleStartPosY - ay;
        const double fixedX = startCenterX - signX * halfW * m_scaleStartX;
        const double fixedY = startCenterY - signY * halfH * m_scaleStartY;

        double handleX = fixedX + 2.0 * signX * halfW * newScaleX;
        double handleY = fixedY + 2.0 * signY * halfH * newScaleY;

        if (snap && m_snapPixels > 0 && !parented)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double canvasHalfH = m_project->height / 2.0;

            QVector<double> targetsX = {-canvasHalfW, 0.0, canvasHalfW};
            QVector<double> targetsY = {-canvasHalfH, 0.0, canvasHalfH};

            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden || other.isFolder()
                    || m_project->effectiveOpacity(other.id, m_frame) <= 0.0)
                    continue;
                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds otherBounds =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));
                targetsX << otherBounds.left << otherBounds.right;
                targetsY << otherBounds.top << otherBounds.bottom;
                if (Settings::edgeMidpointSnap())
                {
                    targetsX << (otherBounds.left + otherBounds.right) / 2.0;
                    targetsY << (otherBounds.top + otherBounds.bottom) / 2.0;
                }
            }

            double bestX = threshold + 1.0;
            double snapX = handleX;
            for (double target : targetsX)
            {
                const double distance = std::abs(handleX - target);
                if (distance < bestX)
                {
                    bestX = distance;
                    snapX = target;
                }
            }
            if (bestX <= threshold)
            {
                newScaleX = signX * (snapX - fixedX) / (2.0 * halfW);
                handleX = snapX;
                m_showGuideX = true;
                m_guideX = snapX;
            }

            double bestY = threshold + 1.0;
            double snapY = handleY;
            for (double target : targetsY)
            {
                const double distance = std::abs(handleY - target);
                if (distance < bestY)
                {
                    bestY = distance;
                    snapY = target;
                }
            }
            if (bestY <= threshold)
            {
                newScaleY = signY * (snapY - fixedY) / (2.0 * halfH);
                m_showGuideY = true;
                m_guideY = snapY;
            }
        }

        const double newCenterX = fixedX + signX * halfW * newScaleX;
        const double newCenterY = fixedY + signY * halfH * newScaleY;
        emit scaleEdited(m_dragEntity, newScaleX, newScaleY, newCenterX + ax,
                         newCenterY + ay);
        return;
    }

    // Rotated entities still scale around their centre.
    emit scaleEdited(m_dragEntity, newScaleX, newScaleY, currentX, currentY);
}

void Viewport::paintEvent(QPaintEvent*)
{
    if (sessionMode())
    {
        QPainter sessionPainter(this);
        paintSession(sessionPainter);
        return;
    }
    if (!m_project)
    {
        QPainter painter(this);
        painter.fillRect(rect(), QApplication::palette().color(QPalette::Window));
        return;
    }

    QElapsedTimer profile;
    profile.start();
    QPainter p(this);
    p.fillRect(rect(), QApplication::palette().color(QPalette::Window));

    const QRectF canvas = canvasRect();
    const double s = viewScale();

    // Canvas drop shadow and border.
    p.fillRect(canvas.translated(6, 6), QColor(0, 0, 0, 90));
    p.fillRect(canvas, m_project->background);
    p.setPen(QPen(QColor(70, 70, 80), 1));
    p.drawRect(canvas);

    // Center guides.
    p.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    p.drawLine(QPointF(canvas.center().x(), canvas.top()),
               QPointF(canvas.center().x(), canvas.bottom()));
    p.drawLine(QPointF(canvas.left(), canvas.center().y()),
               QPointF(canvas.right(), canvas.center().y()));

    // The layer list runs top to bottom: draw the last (bottom) row first so
    // the first (top) row ends up in front.
    const QVector<int> order = m_project->entityOrder();
    for (int index = int(order.size()) - 1; index >= 0; --index)
        drawEntity(p, m_project->entities()[order[index]], canvas, s);
    const qint64 sceneNs = profile.nsecsElapsed();
    PerfMonitor::instance().recordScene(sceneNs / 1e6);

    // Camera viewfinder and the veil outside it.
    drawCameraOverlay(p);

    // CAD-style alignment guides while dragging.
    if (m_showGuideX)
    {
        const int gx = int(std::lround(canvas.center().x() + m_guideX * s));
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(gx, int(canvas.top()), gx, int(canvas.bottom()));
    }
    if (m_showGuideY)
    {
        const int gy = int(std::lround(canvas.center().y() + m_guideY * s));
        p.setPen(QPen(QColor(90, 210, 255), 1.5, Qt::DashLine));
        p.drawLine(int(canvas.left()), gy, int(canvas.right()), gy);
    }

    // Gizmo on top of the selected entity. Cameras have their own frame
    // handles and must not get the generic entity gizmo.
    if (m_selected != kInvalidEntity)
    {
        if (const Entity* entity = m_project->entity(m_selected))
        {
            if (!entity->isCamera()
                && !m_project->isEffectivelyLocked(entity->id))
            {
                drawHandles(p, *entity);
                drawGizmo(p, *entity);
            }
        }
    }

    // Empty state hint.
    if (m_project->entities().isEmpty())
    {
        p.setPen(QColor(150, 150, 160));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("点击左上角工作台按钮\n创建图片 / 视频 / 文字 / 矢量"));
    }

    PerfMonitor::instance().recordViewport(profile.nsecsElapsed() / 1e6);
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (sessionMode())
    {
        if (event->button() == Qt::MiddleButton)
        {
            m_panning = true;
            m_panLast = event->position();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (event->button() != Qt::LeftButton)
            return;

        setFocus(Qt::MouseFocusReason);

        // The camera frame is selected through its border/title; clicks inside
        // pass through to the scene so entities remain editable.
        if (m_editingEnabled && handleSessionCameraPress(event))
        {
            update();
            return;
        }

        if (m_editingEnabled)
        {
            if (const auto handle = hitSessionGizmo(event->position()))
            {
                const SessionDragKind kind =
                    *handle == gizmo::Handle::MoveX ? SessionDragKind::MoveX
                    : *handle == gizmo::Handle::MoveY ? SessionDragKind::MoveY
                    : *handle == gizmo::Handle::CenterScale
                        ? SessionDragKind::CenterScale
                    : *handle == gizmo::Handle::ScaleTopLeft
                              || *handle == gizmo::Handle::ScaleTopRight
                              || *handle == gizmo::Handle::ScaleBottomRight
                              || *handle == gizmo::Handle::ScaleBottomLeft
                        ? SessionDragKind::Scale
                        : SessionDragKind::Rotate;
                startSessionGizmoDrag(m_session->document()->selection(), kind,
                                      *handle, event->position());
                update();
                return;
            }
        }

        const model::Entity* hit = hitTestSession(event->position());
        if (hit)
        {
            if (m_session->document())
                m_session->document()->setSelection(hit->uuid);
            emit uuidSelected(hit->uuid);
            if (m_editingEnabled && !hit->locked)
                startSessionGizmoDrag(hit->uuid, SessionDragKind::Move,
                                      gizmo::Handle::MoveX, event->position());
        }
        else
        {
            if (m_session->document())
                m_session->document()->setSelection(Uuid());
            emit uuidSelected(Uuid());
        }
        update();
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_panLast = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);

    // The camera frame is selected through its border/title; clicks inside
    // pass through to the scene so entities remain editable.
    if (handleCameraPress(event))
        return;

    const Entity* selectedEntity = m_project->entity(m_selected);
    if (!selectedEntity || !m_project->isEffectivelyLocked(selectedEntity->id))
    {
        // Scaling handles take priority over the gizmo axes.
        const Handle handle = hitHandle(event->position());
        if (handle != Handle::None)
        {
            m_gizmoMode = handle == Handle::ComicTip ? GizmoMode::ComicTip
                                                      : GizmoMode::Scale;
            m_activeHandle = handle;
            m_dragging = true;
            m_dragEntity = m_selected;
            m_scaleStartX = m_project->value(m_selected, "scaleX", m_frame);
            m_scaleStartY = m_project->value(m_selected, "scaleY", m_frame);
            m_scaleStartPosX = m_project->value(m_selected, "x", m_frame);
            m_scaleStartPosY = m_project->value(m_selected, "y", m_frame);
            m_dragLast = event->position();
            return;
        }

        // The gizmo takes priority over the entity body.
        GizmoMode gizmoMode = GizmoMode::None;
        if (gizmoHitTest(event->position(), &gizmoMode))
        {
            const Entity* entity = m_project->entity(m_selected);
            if (entity && entity->rotPointCustom())
            {
                if (gizmoMode == GizmoMode::AxisX)
                    gizmoMode = GizmoMode::PivotX;
                else if (gizmoMode == GizmoMode::AxisY)
                    gizmoMode = GizmoMode::PivotY;
            }

            m_gizmoMode = gizmoMode;
            m_dragging = true;
            m_dragEntity = m_selected;
            m_gizmoCenter = entityPivotScreen(*entity);
            m_gizmoStartMouse = event->position();
            m_gizmoStartAngle = std::atan2(event->position().y() - m_gizmoCenter.y(),
                                          event->position().x() - m_gizmoCenter.x())
                                * 180.0 / M_PI;
            m_gizmoStartRotation = m_project->value(m_selected, "rotation", m_frame);
            if (gizmoMode == GizmoMode::PivotFree || gizmoMode == GizmoMode::PivotX
                || gizmoMode == GizmoMode::PivotY)
            {
                m_gizmoStartX = m_project->value(m_selected, "rotPointX", m_frame);
                m_gizmoStartY = m_project->value(m_selected, "rotPointY", m_frame);
            }
            else
            {
                m_gizmoStartX = m_project->value(m_selected, "x", m_frame);
                m_gizmoStartY = m_project->value(m_selected, "y", m_frame);
            }
            m_dragLast = event->position();
            return;
        }
    }

    const Entity* hit = hitTest(event->position());
    if (hit)
    {
        m_selected = hit->id;
        emit entitySelected(hit->id);
        update();   // show the gizmo immediately, even without dragging
        if (!hit->locked)
        {
            m_dragging = true;
            m_dragEntity = hit->id;
            m_dragLast = event->position();
            m_gizmoMode = GizmoMode::Move;
            m_gizmoStartMouse = event->position();
            m_gizmoStartX = m_project->value(hit->id, "x", m_frame);
            m_gizmoStartY = m_project->value(hit->id, "y", m_frame);
        }
    }
    else
    {
        m_selected = kInvalidEntity;
        emit entitySelected(kInvalidEntity);
        update();
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (sessionMode())
    {
        if (m_panning)
        {
            m_pan += event->position() - m_panLast;
            m_panLast = event->position();
            emit viewChanged();
            update();
            return;
        }
        if (m_sessionDragKind == SessionDragKind::CameraMove
            || m_sessionDragKind == SessionDragKind::CameraRotate
            || m_sessionDragKind == SessionDragKind::CameraScale)
        {
            applySessionCameraDrag(event->position(), event->modifiers());
            return;
        }
        if (m_sessionDragKind != SessionDragKind::None)
            applySessionGizmoDrag(event->position(), event->modifiers());
        return;
    }

    if (m_cameraPart != CameraPart::None)
    {
        applyCameraDrag(event->position(), event->modifiers());
        return;
    }

    if (m_panning)
    {
        m_pan += event->position() - m_panLast;
        m_panLast = event->position();
        emit viewChanged();
        update();
        return;
    }

    if (!m_dragging)
        return;

    const double s = viewScale();

    if (m_gizmoMode == GizmoMode::Scale)
    {
        applyHandleDrag(event->position(), event->modifiers() & Qt::ShiftModifier,
                        !(event->modifiers() & Qt::ControlModifier));
        return;
    }

    if (m_gizmoMode == GizmoMode::ComicTip)
    {
        const Entity* entity = m_project->entity(m_dragEntity);
        if (!entity)
            return;
        const QPointF local = screenToEntityLocal(*entity, event->position());
        const ComicBox box = ComicBox::build(TextStyle::fromData(entity->data),
                                              BubbleStyle::fromData(entity->data));
        const QSizeF body = box.bodySize();
        if (body.width() < 1 || body.height() < 1)
            return;
        emit comicTipEdited(m_dragEntity, local.x() / (body.width() / 2.0),
                             local.y() / (body.height() / 2.0));
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotFree)
    {
        double px = m_gizmoStartX + (event->position().x() - m_gizmoStartMouse.x()) / s;
        double py = m_gizmoStartY + (event->position().y() - m_gizmoStartMouse.y()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleX =
                    std::abs(m_project->value(m_dragEntity, "scaleX", m_frame));
                const double scaleY =
                    std::abs(m_project->value(m_dragEntity, "scaleY", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfW = bounds.width() / 2.0 * scaleX;
                const double halfH = bounds.height() / 2.0 * scaleY;
                px = snapTo(px, {0.0, -halfW, halfW}, threshold);
                py = snapTo(py, {0.0, -halfH, halfH}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity, px, py);
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotX)
    {
        double px = m_gizmoStartX + (event->position().x() - m_gizmoStartMouse.x()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleX =
                    std::abs(m_project->value(m_dragEntity, "scaleX", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfW = bounds.width() / 2.0 * scaleX;
                px = snapTo(px, {0.0, -halfW, halfW}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity, px,
                         m_project->value(m_dragEntity, "rotPointY", m_frame));
        return;
    }

    if (m_gizmoMode == GizmoMode::PivotY)
    {
        double py = m_gizmoStartY + (event->position().y() - m_gizmoStartMouse.y()) / s;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const Entity* entity = m_project->entity(m_dragEntity);
            if (entity)
            {
                const QRectF bounds = entityLocalBounds(*entity);
                const double scaleY =
                    std::abs(m_project->value(m_dragEntity, "scaleY", m_frame));
                const double threshold = m_snapPixels / s;
                const double halfH = bounds.height() / 2.0 * scaleY;
                py = snapTo(py, {0.0, -halfH, halfH}, threshold);
            }
        }
        emit pivotEdited(m_dragEntity,
                         m_project->value(m_dragEntity, "rotPointX", m_frame), py);
        return;
    }

    if (m_gizmoMode == GizmoMode::AxisX)
    {
        const double total = (event->position().x() - m_gizmoStartMouse.x()) / s;
        double x = m_gizmoStartX + total;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfW = m_project->width / 2.0;
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
            const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
            const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
            const double radians = rotation * M_PI / 180.0;
            const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfW =
                entity ? entityLocalBounds(*entity).width() / 2.0 * std::abs(scaleX) : 0.0;
            const double halfH =
                entity ? entityLocalBounds(*entity).height() / 2.0 * std::abs(scaleY) : 0.0;
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfW = halfW * cosA + halfH * sinA;
            const double halfWForSnap = aabbHalfW;

            const double visual = x - rotatedAx;
            if (std::abs(visual) <= threshold)
                x -= visual;
            else if (std::abs(visual - (-canvasHalfW + halfWForSnap)) <= threshold)
                x += (-canvasHalfW + halfWForSnap) - visual;
            else if (std::abs(visual - (canvasHalfW - halfWForSnap)) <= threshold)
                x += (canvasHalfW - halfWForSnap) - visual;
            else if (m_edgeCenterSnap && std::abs(visual - halfWForSnap) <= threshold)
                x += halfWForSnap - visual;
            else if (m_edgeCenterSnap && std::abs(visual + halfWForSnap) <= threshold)
                x += -halfWForSnap - visual;
        }
        emit valueEdited(m_dragEntity, QStringLiteral("x"), x);
        return;
    }

    if (m_gizmoMode == GizmoMode::AxisY)
    {
        const double total = (event->position().y() - m_gizmoStartMouse.y()) / s;
        double y = m_gizmoStartY + total;
        if (!(event->modifiers() & Qt::ControlModifier) && m_snapPixels > 0)
        {
            const double threshold = m_snapPixels / s;
            const double canvasHalfH = m_project->height / 2.0;
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
            const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
            const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
            const double radians = rotation * M_PI / 180.0;
            const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const Entity* entity = m_project->entity(m_dragEntity);
            const double halfH =
                entity ? entityLocalBounds(*entity).height() / 2.0 * std::abs(scaleY) : 0.0;
            const double halfW =
                entity ? entityLocalBounds(*entity).width() / 2.0 * std::abs(scaleX) : 0.0;
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfH = halfW * sinA + halfH * cosA;
            const double halfHForSnap = aabbHalfH;

            const double visual = y - rotatedAy;
            if (std::abs(visual) <= threshold)
                y -= visual;
            else if (std::abs(visual - (-canvasHalfH + halfHForSnap)) <= threshold)
                y += (-canvasHalfH + halfHForSnap) - visual;
            else if (std::abs(visual - (canvasHalfH - halfHForSnap)) <= threshold)
                y += (canvasHalfH - halfHForSnap) - visual;
            else if (m_edgeCenterSnap && std::abs(visual - halfHForSnap) <= threshold)
                y += halfHForSnap - visual;
            else if (m_edgeCenterSnap && std::abs(visual + halfHForSnap) <= threshold)
                y += -halfHForSnap - visual;
        }
        emit valueEdited(m_dragEntity, QStringLiteral("y"), y);
        return;
    }

    if (m_gizmoMode == GizmoMode::Rotate)
    {
        const double angle = std::atan2(event->position().y() - m_gizmoCenter.y(),
                                         event->position().x() - m_gizmoCenter.x())
                             * 180.0 / M_PI;
        double delta = angle - m_gizmoStartAngle;
        while (delta > 180.0)
            delta -= 360.0;
        while (delta < -180.0)
            delta += 360.0;

        double rotation = m_gizmoStartRotation + delta;
        if (!(event->modifiers() & Qt::ControlModifier))
        {
            constexpr double tolerance = 3.0;
            for (int snapAngle : Settings::snapAngles())
            {
                // Compare on the circle so 268° still snaps to 270°.
                const double diff =
                    std::remainder(rotation - double(snapAngle), 360.0);
                if (std::abs(diff) <= tolerance)
                {
                    rotation = snapAngle;
                    break;
                }
            }
        }
        emit valueEdited(m_dragEntity, QStringLiteral("rotation"), rotation);
        return;
    }

    const double totalX = (event->position().x() - m_gizmoStartMouse.x()) / s;
    const double totalY = (event->position().y() - m_gizmoStartMouse.y()) / s;

    const Entity* moving = m_project->entity(m_dragEntity);
    const bool parented = moving && moving->parent != kInvalidEntity;
    double localDeltaX = totalX;
    double localDeltaY = totalY;
    if (parented)
    {
        const QTransform parent =
            SceneRenderer::parentWorldTransform(*m_project, *moving, m_frame);
        const QPointF origin = parent.map(QPointF(0, 0));
        const QPointF mapped = parent.map(QPointF(totalX, totalY));
        localDeltaX = mapped.x() - origin.x();
        localDeltaY = mapped.y() - origin.y();
    }

    double x = m_gizmoStartX + localDeltaX;
    double y = m_gizmoStartY + localDeltaY;

    m_showGuideX = false;
    m_showGuideY = false;

    const bool free = event->modifiers() & Qt::ControlModifier;
    bool lockX = false;
    bool lockY = false;
    if (!parented && !free && std::abs(totalX) > 1e-6
        && std::abs(totalY) > 1e-6)
    {
        if (std::abs(totalX) >= std::abs(totalY) * 4.0)
        {
            y = m_gizmoStartY;   // horizontal drag
            lockY = true;
        }
        else if (std::abs(totalY) >= std::abs(totalX) * 4.0)
        {
            x = m_gizmoStartX;   // vertical drag
            lockX = true;
        }
    }

    if (!parented && !free && m_snapPixels > 0)
    {
        const double threshold = m_snapPixels / s;
        const double canvasHalfW = m_project->width / 2.0;
        const double canvasHalfH = m_project->height / 2.0;
        const double ax = m_project->value(m_dragEntity, "rotPointX", m_frame);
        const double ay = m_project->value(m_dragEntity, "rotPointY", m_frame);
        const double rotation = m_project->value(m_dragEntity, "rotation", m_frame);
        const double radians = rotation * M_PI / 180.0;
        const double rotatedAx = ax * std::cos(radians) - ay * std::sin(radians);
        const double rotatedAy = ax * std::sin(radians) + ay * std::cos(radians);
        const Entity* entity = m_project->entity(m_dragEntity);
        if (entity)
        {
            const QRectF bounds = entityLocalBounds(*entity);
            const double scaleX = m_project->value(m_dragEntity, "scaleX", m_frame);
            const double scaleY = m_project->value(m_dragEntity, "scaleY", m_frame);
            const double halfW = bounds.width() / 2.0 * std::abs(scaleX);
            const double halfH = bounds.height() / 2.0 * std::abs(scaleY);
            const double cosA = std::abs(std::cos(radians));
            const double sinA = std::abs(std::sin(radians));
            const double aabbHalfW = halfW * cosA + halfH * sinA;
            const double aabbHalfH = halfW * sinA + halfH * cosA;

            const auto snapVisual = [&](double pos, double offset, double half,
                                        double canvasHalf) {
                const double visual = pos - offset;
                if (std::abs(visual) <= threshold)
                    return pos - visual;
                if (std::abs(visual - (-canvasHalf + half)) <= threshold)
                    return pos + (-canvasHalf + half) - visual;
                if (std::abs(visual - (canvasHalf - half)) <= threshold)
                    return pos + (canvasHalf - half) - visual;
                if (m_edgeCenterSnap && std::abs(visual - half) <= threshold)
                    return pos + half - visual;
                if (m_edgeCenterSnap && std::abs(visual + half) <= threshold)
                    return pos - half - visual;
                return pos;
            };

            if (!lockX)
                x = snapVisual(x, rotatedAx, aabbHalfW, canvasHalfW);
            if (!lockY)
                y = snapVisual(y, rotatedAy, aabbHalfH, canvasHalfH);

            // Align to the edges of other axis-aligned entities.
            VisualBounds mine = visualBoundsOf(*entity, x, y);
            bool snappedX = false;
            bool snappedY = false;
            for (const Entity& other : m_project->entities())
            {
                if (other.id == m_dragEntity || other.hidden || other.isFolder()
                    || m_project->effectiveOpacity(other.id, m_frame) <= 0.0
                    || (snappedX && snappedY))
                    continue;

                const double otherRotation =
                    m_project->value(other.id, "rotation", m_frame);
                const double modulo = std::fmod(std::abs(otherRotation) + 1e-6, 90.0);
                if (std::abs(modulo) > 1e-3 && std::abs(modulo - 90.0) > 1e-3)
                    continue;

                const VisualBounds target =
                    visualBoundsOf(other, m_project->value(other.id, "x", m_frame),
                                   m_project->value(other.id, "y", m_frame));

                if (!lockX && !snappedX)
                {
                    const double myEdges[3] = {mine.left, (mine.left + mine.right) / 2.0,
                                               mine.right};
                    const double targetEdges[3] = {target.left,
                                                   (target.left + target.right) / 2.0,
                                                   target.right};
                    for (double myEdge : myEdges)
                    {
                        bool matched = false;
                        for (double targetEdge : targetEdges)
                        {
                            if (std::abs(myEdge - targetEdge) <= threshold)
                            {
                                x += targetEdge - myEdge;
                                m_guideX = targetEdge;
                                m_showGuideX = true;
                                snappedX = true;
                                matched = true;
                                break;
                            }
                        }
                        if (matched)
                            break;
                    }
                }

                if (!lockY && !snappedY)
                {
                    const double myEdges[3] = {mine.top, (mine.top + mine.bottom) / 2.0,
                                               mine.bottom};
                    const double targetEdges[3] = {target.top,
                                                   (target.top + target.bottom) / 2.0,
                                                   target.bottom};
                    for (double myEdge : myEdges)
                    {
                        bool matched = false;
                        for (double targetEdge : targetEdges)
                        {
                            if (std::abs(myEdge - targetEdge) <= threshold)
                            {
                                y += targetEdge - myEdge;
                                m_guideY = targetEdge;
                                m_showGuideY = true;
                                snappedY = true;
                                matched = true;
                                break;
                            }
                        }
                        if (matched)
                            break;
                    }
                }

                if (snappedX || snappedY)
                    mine = visualBoundsOf(*entity, x, y);
            }

            if (lockY)
            {
                m_guideY = (mine.top + mine.bottom) / 2.0;
                m_showGuideY = true;
            }
            if (lockX)
            {
                m_guideX = (mine.left + mine.right) / 2.0;
                m_showGuideX = true;
            }
        }
    }

    emit positionEdited(m_dragEntity, x, y);
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (sessionMode())
    {
        if (event->button() == Qt::MiddleButton)
        {
            m_panning = false;
            unsetCursor();
        }
        if (event->button() == Qt::LeftButton
            && m_sessionDragKind != SessionDragKind::None)
            finishSessionGizmoDrag();
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        unsetCursor();
    }
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        m_dragEntity = kInvalidEntity;
        m_gizmoMode = GizmoMode::None;
        m_activeHandle = Handle::None;
        m_cameraPart = CameraPart::None;
        m_showGuideX = false;
        m_showGuideY = false;
        unsetCursor();
        update();
    }
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;

    const double s0 = viewScale();
    if (sessionMode())
    {
        const QPointF before = sessionScreenToProject(event->position());
        m_zoom = std::clamp(m_zoom * std::pow(1.15, steps), 0.1, 8.0);
        const double s1 = viewScale();
        // Keep the model point under the cursor fixed.  `before` is Y-up
        // model space, while m_pan is a Y-down widget offset, hence the flip.
        m_pan += (s0 - s1) * QPointF(before.x(), -before.y());
        invalidateSessionImage();
        emit viewChanged();
        update();
        return;
    }

    const QPointF before = screenToCanvas(event->position());
    m_zoom = std::clamp(m_zoom * std::pow(1.15, steps), 0.1, 8.0);
    const double s1 = viewScale();
    m_pan += before * (s0 - s1);
    emit viewChanged();
    update();
}

void Viewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (sessionMode())
        invalidateSessionImage();
    emit viewChanged();
}

void Viewport::keyPressEvent(QKeyEvent* event)
{
    if (sessionMode() && event->key() == Qt::Key_Escape
        && m_sessionDragKind != SessionDragKind::None)
    {
        cancelSessionDrag();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---- session gizmo test hooks -------------------------------------------

gizmo::Transform Viewport::gizmoTransformForTest(const Uuid& uuid) const
{
    if (!m_session)
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    return it == m_session->project().entities.constEnd()
               ? gizmo::Transform()
               : sessionGizmoTransform(it.value());
}

gizmo::Bounds Viewport::gizmoBoundsForTest(const Uuid& uuid) const
{
    if (!m_session)
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    return it == m_session->project().entities.constEnd() ? gizmo::Bounds()
                                                         : sessionGizmoBounds(it.value());
}

QPointF Viewport::gizmoHandleForTest(const Uuid& uuid, gizmo::Handle handle) const
{
    if (!m_session)
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return {};
    const QTransform parentToWidget = sessionParentToWidget(it.value());
    return parentToWidget.map(gizmo::handlePosition(
        handle, sessionGizmoTransform(it.value()), sessionGizmoBounds(it.value()),
        sessionViewScaleFor(parentToWidget)));
}

QPointF Viewport::sessionPivotForTest() const
{
    if (!m_session || !m_session->document())
        return {};
    const auto it =
        m_session->project().entities.constFind(m_session->document()->selection());
    if (it == m_session->project().entities.constEnd())
        return {};
    return sessionParentToWidget(it.value())
        .map(gizmo::worldPivot(sessionGizmoTransform(it.value())));
}

double Viewport::snapAngleForTest(double degrees, bool force) const
{
    return snapSessionAngle(degrees, force);
}

} // namespace pvf
