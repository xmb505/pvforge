#include "next/viewport/ViewportWidget.h"

#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "model/Model.h"
#include "next/core/Document.h"
#include "render2/Render.h"

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>

namespace pvf::next {

namespace {

// Snap radius in screen pixels for the canvas centre lines and other
// entities' edges/midpoints.
constexpr double kSnapThresholdPixels = 6.0;
// A rotation result snaps when it is this close to a snapAngleDegrees()
// multiple.  Shift forces the snap regardless of distance.
constexpr double kSnapAngleToleranceDegrees = 3.0;

const QColor kWidgetBackground(20, 22, 26);
const QColor kCanvasBorder(78, 82, 92);
const QColor kSelectionColor(116, 206, 255);
const QColor kAxisXColor(255, 122, 122);
const QColor kAxisYColor(125, 255, 158);

QPointF rotateVector(QPointF vector, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return QPointF(vector.x() * cosine - vector.y() * sine,
                   vector.x() * sine + vector.y() * cosine);
}

double pointDistance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Closest target-feature pair within the threshold, as the offset that has to
// be added to the moved value.  A feature is the dragged entity's AABB
// left/centre/right (or top/middle/bottom); targets are canvas centre lines or
// other entities' matching features.
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

bool transformsEqual(const model::Transform& a, const gizmo::Transform& b)
{
    constexpr double epsilon = 1e-9;
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon
           && std::abs(a.rotation - b.rotation) <= epsilon
           && std::abs(a.scaleX - b.scaleX) <= epsilon
           && std::abs(a.scaleY - b.scaleY) <= epsilon
           && std::abs(a.pivotX - b.pivotX) <= epsilon
           && std::abs(a.pivotY - b.pivotY) <= epsilon;
}

void clampCameraScale(model::Entity& entity)
{
    if (entity.content.kind != ContentKind::Camera)
        return;

    const auto clampMagnitude = [](double value) {
        const double magnitude = std::clamp(std::abs(value), 0.05, 1.0);
        return value < 0.0 ? -magnitude : magnitude;
    };
    entity.transform.scaleX = clampMagnitude(entity.transform.scaleX);
    entity.transform.scaleY = clampMagnitude(entity.transform.scaleY);
}

void drawArrowLine(QPainter& painter, QPointF from, QPointF to, const QColor& color)
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

void drawArrowHead(QPainter& painter, QPointF tip, QPointF direction, double size,
                   const QColor& color)
{
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-9)
        return;
    const QPointF unit = direction / length;
    const QPointF perpendicular(-unit.y(), unit.x());
    QPolygonF head;
    head << tip << tip - unit * size + perpendicular * (size * 0.55)
         << tip - unit * size - perpendicular * (size * 0.55);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

void drawRotateGlyph(QPainter& painter, QPointF center, double rotation)
{
    painter.save();
    painter.translate(center);
    painter.rotate(rotation);

    const QColor color(246, 249, 255);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    const QRectF arcRect(-7.0, -7.0, 14.0, 14.0);
    painter.drawArc(arcRect, 30 * 16, 120 * 16);
    painter.drawArc(arcRect, 210 * 16, 120 * 16);

    const auto pointAt = [](double degrees) {
        const double radians = qDegreesToRadians(degrees);
        return QPointF(std::cos(radians) * 7.0, -std::sin(radians) * 7.0);
    };
    const auto tangentAt = [](double degrees) {
        const double radians = qDegreesToRadians(degrees);
        return QPointF(-std::sin(radians), -std::cos(radians));
    };
    drawArrowHead(painter, pointAt(150.0), tangentAt(150.0), 5.0, color);
    drawArrowHead(painter, pointAt(330.0), tangentAt(330.0), 5.0, color);
    painter.restore();
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

} // namespace

ViewportWidget::ViewportWidget(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(160, 90);

    if (m_document)
    {
        connect(m_document, &Document::changed, this, [this]() {
            invalidateScene();
            update();
        });
        connect(m_document, &Document::selectionChanged, this, [this]() {
            invalidateScene();
            update();
        });
        connect(m_document, &Document::playheadChanged, this, [this]() {
            invalidateScene();
            update();
        });
    }
}

ViewportWidget::~ViewportWidget() = default;

void ViewportWidget::setCameraUuid(const Uuid& uuid)
{
    if (m_cameraUuid == uuid)
        return;
    m_cameraUuid = uuid;
    invalidateScene();
    update();
}

void ViewportWidget::setVideoFrameProvider(render2::FrameProvider provider)
{
    m_videoFrameProvider = std::move(provider);
    invalidateScene();
    update();
}

void ViewportWidget::setHtmlFrameProvider(render2::FrameProvider provider)
{
    m_htmlFrameProvider = std::move(provider);
    invalidateScene();
    update();
}

void ViewportWidget::setSceneCacheHooks(SceneLookup lookup, SceneStore store)
{
    m_sceneLookup = std::move(lookup);
    m_sceneStore = std::move(store);
    invalidateScene();
    update();
}

void ViewportWidget::setShowGizmo(bool show)
{
    if (m_showGizmo == show)
        return;
    m_showGizmo = show;
    update();
}

void ViewportWidget::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
}

void ViewportWidget::setSnapPixels(int pixels)
{
    m_snapPixels = std::max(0, pixels);
}

void ViewportWidget::setSnapAngleDegrees(double degrees)
{
    m_snapAngleDegrees = std::abs(degrees);
}

void ViewportWidget::refresh()
{
    invalidateScene();
    update();
}

QImage ViewportWidget::grabFrame() const
{
    if (!m_document)
        return {};

    const model::Project& project = m_document->project();
    const QSize output(std::max(1, project.settings.width),
                       std::max(1, project.settings.height));
    const int frame = m_document->playhead();
    // A gizmo drag edits mutableProject() directly and does not emit
    // Document::changed until release, so the cache meta for this frame is
    // still the pre-drag one.  Draw and keep the live preview uncached; a
    // click that only shows the gizmo still uses the cache.
    const bool liveEdit = m_dragKind != DragKind::None
                          && m_dragKind != DragKind::Pan && m_dragChanged;

    if (m_sceneLookup && !liveEdit) {
        bool hit = false;
        const QImage cached = m_sceneLookup(frame, m_cameraUuid, &hit);
        if (hit && !cached.isNull())
            return cached;
    }

    bool mediaMissing = false;
    const QImage image = renderScene(output, &mediaMissing);
    // A frame containing a pending HTML/video placeholder must never become
    // the cached "final" picture.
    if (!liveEdit && !mediaMissing && m_sceneStore && !image.isNull())
        m_sceneStore(frame, m_cameraUuid, image);
    return image;
}

QImage ViewportWidget::renderScene(const QSize& size, bool* mediaMissing) const
{
    bool missing = false;
    render2::Options options;
    options.projectFrame = m_document->playhead();
    options.cameraUuid = m_cameraUuid;
    options.outputSize = size;
    options.videoFrame = [this, &missing](const Uuid& uuid, double frame) {
        const QImage image =
            m_videoFrameProvider ? m_videoFrameProvider(uuid, frame) : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };
    options.htmlFrame = [this, &missing](const Uuid& uuid, double frame) {
        const QImage image =
            m_htmlFrameProvider ? m_htmlFrameProvider(uuid, frame) : QImage();
        if (image.isNull())
            missing = true;
        return image;
    };

    const model::Project& project = m_document->project();
    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    const QImage image = render2::renderFrame(project, m_document->assets(),
                                              options, world, cache);
    if (mediaMissing)
        *mediaMissing = missing;
    return image;
}

QPointF ViewportWidget::projectToWidget(QPointF projectPos) const
{
    return projectToWidgetTransform().map(projectPos);
}

QPointF ViewportWidget::widgetToProject(QPointF widgetPos) const
{
    if (!m_document)
        return widgetPos;

    bool invertible = false;
    const QTransform inverse = projectToWidgetTransform().inverted(&invertible);
    return invertible ? inverse.map(widgetPos) : widgetPos;
}

QRectF ViewportWidget::viewRect() const
{
    QSizeF canvas(16.0, 9.0);
    if (m_document)
    {
        canvas = QSizeF(std::max(1, m_document->project().settings.width),
                        std::max(1, m_document->project().settings.height));
    }

    const double availableWidth = std::max(1.0, width() - 2.0 * kCanvasMargin);
    const double availableHeight = std::max(1.0, height() - 2.0 * kCanvasMargin);
    const double fit =
        std::min(availableWidth / canvas.width(), availableHeight / canvas.height());
    const double scale = fit * m_zoom;
    const QSizeF size(canvas.width() * scale, canvas.height() * scale);
    const QPointF center(width() / 2.0 + m_pan.x(), height() / 2.0 + m_pan.y());
    return QRectF(center.x() - size.width() / 2.0, center.y() - size.height() / 2.0,
                  size.width(), size.height());
}

QTransform ViewportWidget::projectToWidgetTransform() const
{
    if (!m_document)
        return {};

    const model::Project& project = m_document->project();
    const double projectWidth =
        double(std::max(1, project.settings.width));
    const double projectHeight =
        double(std::max(1, project.settings.height));
    const QRectF rect = viewRect();

    QTransform canvas;
    canvas.translate(rect.left(), rect.top());
    canvas.scale(rect.width() / projectWidth, rect.height() / projectHeight);

    if (m_cameraUuid.isEmpty())
        return canvas;

    const auto cameraIt = project.entities.constFind(m_cameraUuid);
    if (cameraIt == project.entities.constEnd())
        return canvas;

    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    const anim::Pose pose = anim::evaluateEntity(project, m_cameraUuid,
                                                 m_document->playhead(), world,
                                                 cache);

    QPolygonF source;
    source << QPointF(-projectWidth / 2.0, -projectHeight / 2.0)
           << QPointF(projectWidth / 2.0, -projectHeight / 2.0)
           << QPointF(projectWidth / 2.0, projectHeight / 2.0)
           << QPointF(-projectWidth / 2.0, projectHeight / 2.0);
    const QPolygonF quad = pose.world.map(source);

    QPolygonF target;
    target << rect.topLeft() << rect.topRight() << rect.bottomRight()
           << rect.bottomLeft();

    QTransform cameraTransform;
    if (QTransform::quadToQuad(quad, target, cameraTransform))
        return cameraTransform;
    return canvas;
}

QTransform ViewportWidget::parentWorldTransform(const Uuid& uuid) const
{
    if (!m_document)
        return {};

    const model::Project& project = m_document->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd() || it.value().parentUuid.isEmpty())
        return {};
    if (!project.entities.contains(it.value().parentUuid))
        return {};

    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    const anim::Pose pose =
        anim::evaluateEntity(project, it.value().parentUuid, m_document->playhead(),
                             world, cache);
    return pose.world;
}

QTransform ViewportWidget::parentToWidgetTransform(const Uuid& uuid) const
{
    return projectToWidgetTransform() * parentWorldTransform(uuid);
}

double ViewportWidget::viewScaleFor(const QTransform& parentToWidget) const
{
    const double determinant = std::abs(parentToWidget.determinant());
    return std::max(1e-6, std::sqrt(determinant));
}

gizmo::Transform ViewportWidget::gizmoTransform(const model::Entity& entity) const
{
    gizmo::Transform transform;
    transform.x = entity.transform.x;
    transform.y = entity.transform.y;
    transform.rotation = entity.transform.rotation;
    transform.scaleX = entity.transform.scaleX;
    transform.scaleY = entity.transform.scaleY;
    transform.pivotX = entity.transform.pivotX;
    transform.pivotY = entity.transform.pivotY;
    return transform;
}

QSizeF ViewportWidget::assetContentSize(const model::AssetContent& asset) const
{
    if (m_document)
    {
        if (const auto stored = m_document->assets().get(asset.hash))
        {
            const int width =
                stored->metadata.value(QStringLiteral("width")).toInt();
            const int height =
                stored->metadata.value(QStringLiteral("height")).toInt();
            if (width > 0 && height > 0)
                return QSizeF(width, height);
        }
    }

    switch (asset.type)
    {
    case AssetType::Image:
    case AssetType::Video:
        return QSizeF(320.0, 180.0);
    case AssetType::Html:
        return QSizeF(800.0, 120.0);
    case AssetType::Audio:
        break;
    }
    return QSizeF(100.0, 100.0);
}

gizmo::Bounds ViewportWidget::gizmoBounds(const model::Entity& entity) const
{
    QSizeF size(100.0, 100.0);
    switch (entity.content.kind)
    {
    case ContentKind::Empty:
        break;

    case ContentKind::Vector:
        size = QSizeF(std::max(1.0, entity.content.vector.width),
                      std::max(1.0, entity.content.vector.height));
        break;

    case ContentKind::Text:
    {
        const TextBlock block = TextBlock::measure(
            TextStyle::fromData(textStyleData(entity.content.text)));
        const QSizeF measured = block.size();
        if (measured.width() >= 1.0 && measured.height() >= 1.0)
            size = measured;
        break;
    }

    case ContentKind::Asset:
        size = assetContentSize(entity.content.asset);
        break;

    case ContentKind::Camera:
        if (m_document)
        {
            size = QSizeF(
                std::max(1, m_document->project().settings.width),
                std::max(1, m_document->project().settings.height));
        }
        break;
    }

    if (size.width() < 1.0)
        size.setWidth(100.0);
    if (size.height() < 1.0)
        size.setHeight(100.0);

    gizmo::Bounds bounds;
    bounds.left = -size.width() / 2.0;
    bounds.right = size.width() / 2.0;
    bounds.top = -size.height() / 2.0;
    bounds.bottom = size.height() / 2.0;
    return bounds;
}

QRectF ViewportWidget::entityWorldBounds(const model::Entity& entity,
                                         const QTransform& worldTransform) const
{
    // worldTransform maps the entity's local content space to project space
    // (it already includes the entity's own transform).
    const gizmo::Bounds bounds = gizmoBounds(entity);
    QPolygonF polygon;
    polygon << worldTransform.map(QPointF(bounds.left, bounds.top))
            << worldTransform.map(QPointF(bounds.right, bounds.top))
            << worldTransform.map(QPointF(bounds.right, bounds.bottom))
            << worldTransform.map(QPointF(bounds.left, bounds.bottom));
    return polygon.boundingRect();
}

void ViewportWidget::collectSnapTargets(const Uuid& draggedUuid)
{
    m_snapTargetsX.clear();
    m_snapTargetsY.clear();
    m_dragSnapRoot = false;
    m_dragStartWorldBounds = QRectF();

    if (!m_document || draggedUuid.isEmpty())
        return;

    const model::Project& project = m_document->project();
    const auto draggedIt = project.entities.constFind(draggedUuid);
    if (draggedIt == project.entities.constEnd())
        return;

    const model::Entity& dragged = draggedIt.value();
    m_dragSnapRoot = dragged.parentUuid.isEmpty()
                     || !project.entities.contains(dragged.parentUuid);
    m_dragStartWorldBounds =
        entityWorldBounds(dragged,
                          parentWorldTransform(draggedUuid)
                              * gizmo::localMatrix(gizmoTransform(dragged)));

    const int frame = m_document->playhead();
    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    for (auto it = project.entities.constBegin();
         it != project.entities.constEnd(); ++it)
    {
        const model::Entity& entity = it.value();
        if (it.key() == draggedUuid || entity.content.kind == ContentKind::Camera)
            continue;
        if (frame < entity.start || frame >= entity.end)
            continue;

        // Children travel with the dragged entity, so their edges would just
        // follow the drag.
        bool descendant = false;
        for (Uuid parent = entity.parentUuid; !parent.isEmpty();)
        {
            if (parent == draggedUuid)
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

        const QRectF bounds = entityWorldBounds(entity, pose.world);
        if (bounds.isEmpty())
            continue;
        m_snapTargetsX << bounds.left() << bounds.center().x() << bounds.right();
        m_snapTargetsY << bounds.top() << bounds.center().y() << bounds.bottom();
    }
}

double ViewportWidget::snapPixel(double value) const
{
    if (m_snapPixels <= 0)
        return value;
    const double step = double(m_snapPixels);
    return std::round(value / step) * step;
}

double ViewportWidget::snapMoveAxis(double value, bool xAxis) const
{
    if (m_dragSnapRoot && m_document)
    {
        const model::Project& project = m_document->project();
        const double startValue =
            xAxis ? m_dragStartTransform.x : m_dragStartTransform.y;
        const double delta = value - startValue;
        const QRectF bounds = m_dragStartWorldBounds;
        const std::array<double, 3> features =
            xAxis ? std::array<double, 3>{bounds.left() + delta,
                                          bounds.center().x() + delta,
                                          bounds.right() + delta}
                  : std::array<double, 3>{bounds.top() + delta,
                                          bounds.center().y() + delta,
                                          bounds.bottom() + delta};
        const double threshold = kSnapThresholdPixels / m_dragViewScale;
        const double canvasCenter =
            xAxis ? std::max(1, project.settings.width) / 2.0
                  : std::max(1, project.settings.height) / 2.0;

        // Canvas centre lines beat other entities, which beat the pixel grid.
        if (const auto snap =
                nearestFeatureSnap(features, {canvasCenter}, threshold))
            return value + *snap;
        if (const auto snap = nearestFeatureSnap(
                features, xAxis ? m_snapTargetsX : m_snapTargetsY, threshold))
            return value + *snap;
    }

    return snapPixel(value);
}

double ViewportWidget::snapAngle(double degrees, bool force) const
{
    if (m_snapAngleDegrees <= 0.0)
        return degrees;
    const double snapped =
        std::round(degrees / m_snapAngleDegrees) * m_snapAngleDegrees;
    if (force
        || std::abs(std::remainder(degrees - snapped, 360.0))
               <= kSnapAngleToleranceDegrees)
        return snapped;
    return degrees;
}

Uuid ViewportWidget::entityAt(const QPointF& widgetPos) const
{
    if (!m_document)
        return {};

    const model::Project& project = m_document->project();
    const int frame = m_document->playhead();
    const QPointF projectPos = widgetToProject(widgetPos);

    QVector<Uuid> drawOrder;
    std::function<void(const Uuid&)> appendChildren = [&](const Uuid& parent) {
        const QVector<Uuid> children = model::childrenOf(project, parent);
        for (const Uuid& child : children)
        {
            drawOrder.append(child);
            appendChildren(child);
        }
    };
    appendChildren(Uuid());

    render2::SceneWorld world(project, m_document->assets());
    const double projectWidth =
        double(std::max(1, project.settings.width));
    const double projectHeight =
        double(std::max(1, project.settings.height));
    QPolygonF cameraSource;
    cameraSource << QPointF(-projectWidth / 2.0, -projectHeight / 2.0)
                 << QPointF(projectWidth / 2.0, -projectHeight / 2.0)
                 << QPointF(projectWidth / 2.0, projectHeight / 2.0)
                 << QPointF(-projectWidth / 2.0, projectHeight / 2.0);

    // Later entities are drawn on top, so hit-test from the back of the list.
    Uuid cameraHit;
    for (int index = drawOrder.size() - 1; index >= 0; --index)
    {
        const Uuid& uuid = drawOrder.at(index);
        const auto it = project.entities.constFind(uuid);
        if (it == project.entities.constEnd())
            continue;

        const QPainterPath shape = world.shapeOf(uuid, frame);
        if (!shape.isEmpty() && shape.contains(projectPos))
            return uuid;

        // Cameras have no collision shape; remember the topmost viewfinder
        // frame as a fallback so a camera never covers real content.
        if (cameraHit.isEmpty()
            && it.value().content.kind == ContentKind::Camera)
        {
            anim::Cache cache;
            const anim::Pose pose =
                anim::evaluateEntity(project, uuid, frame, world, cache);
            const QPolygonF quad = pose.world.map(cameraSource);
            if (quad.containsPoint(projectPos, Qt::OddEvenFill))
                cameraHit = uuid;
        }
    }
    return cameraHit;
}

std::optional<gizmo::Handle> ViewportWidget::hitGizmoHandle(
    const QPointF& widgetPos, const model::Entity& entity,
    const QTransform& parentToWidget, double viewScale) const
{
    bool invertible = false;
    const QTransform inverse = parentToWidget.inverted(&invertible);
    if (!invertible)
        return std::nullopt;

    const QPointF parentPos = inverse.map(widgetPos);
    return gizmo::hitTest(parentPos, gizmoTransform(entity), gizmoBounds(entity),
                          viewScale);
}

void ViewportWidget::startGizmoDrag(const Uuid& uuid, DragKind kind,
                                    gizmo::Handle handle,
                                    const QPointF& widgetPos)
{
    if (!m_document)
        return;

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
        return;

    m_dragKind = kind;
    m_dragEntity = uuid;
    m_dragHandle = handle;
    m_dragStartTransform = gizmoTransform(it.value());
    m_dragStartBounds = gizmoBounds(it.value());
    m_dragParentToWidget = parentToWidgetTransform(uuid);
    m_dragViewScale = viewScaleFor(m_dragParentToWidget);
    collectSnapTargets(uuid);

    bool invertible = false;
    const QTransform inverse = m_dragParentToWidget.inverted(&invertible);
    m_dragStartParent = invertible ? inverse.map(widgetPos) : widgetPos;
    m_dragPivotStart = gizmo::worldPivot(m_dragStartTransform);
    m_dragCenterStart =
        gizmo::localToWorld(m_dragStartTransform,
                            gizmo::boundsCenter(m_dragStartBounds));

    const auto angleAround = [this](QPointF center) {
        return std::atan2(m_dragStartParent.y() - center.y(),
                          m_dragStartParent.x() - center.x());
    };
    m_dragAnglePivot = angleAround(m_dragPivotStart);
    m_dragAngleCenter = angleAround(m_dragCenterStart);

    if (kind == DragKind::Scale)
    {
        m_dragGrabOffset =
            localCorner(m_dragStartBounds, handle)
            - gizmo::worldToLocal(m_dragStartTransform, m_dragStartParent);
    }

    m_dragChanged = false;

    switch (kind)
    {
    case DragKind::MoveX:
    case DragKind::MoveY:
        setCursor(Qt::SizeAllCursor);
        break;
    case DragKind::Rotate:
        setCursor(Qt::CrossCursor);
        break;
    case DragKind::Scale:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case DragKind::CenterScale:
    case DragKind::Pivot:
        setCursor(Qt::SizeAllCursor);
        break;
    case DragKind::Pan:
    case DragKind::None:
        break;
    }
}

void ViewportWidget::applyGizmoDrag(const QPointF& widgetPos,
                                    Qt::KeyboardModifiers modifiers)
{
    if (!m_document || m_dragEntity.isEmpty() || m_dragKind == DragKind::None)
        return;

    auto it = m_document->mutableProject().entities.find(m_dragEntity);
    if (it == m_document->mutableProject().entities.end())
    {
        cancelGizmoDrag();
        return;
    }

    model::Entity& entity = it.value();
    if (entity.locked)
        return;

    bool invertible = false;
    const QTransform inverse = m_dragParentToWidget.inverted(&invertible);
    if (!invertible)
        return;
    const QPointF mouseParent = inverse.map(widgetPos);
    const QPointF parentDelta = mouseParent - m_dragStartParent;
    const bool snapActive =
        m_snapEnabled && !modifiers.testFlag(Qt::AltModifier);

    switch (m_dragKind)
    {
    case DragKind::MoveX:
    case DragKind::MoveY:
    {
        const bool moveX = m_dragKind == DragKind::MoveX;
        const QPointF axis = rotateVector(
            moveX ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0),
            m_dragStartTransform.rotation);
        const double amount = parentDelta.x() * axis.x() + parentDelta.y() * axis.y();
        double value = moveX ? m_dragStartTransform.x + amount
                             : m_dragStartTransform.y + amount;
        if (snapActive)
            value = snapMoveAxis(value, moveX);
        if (moveX)
            entity.transform.x = value;
        else
            entity.transform.y = value;
        break;
    }

    case DragKind::Pivot:
        entity.transform.pivotX = m_dragStartTransform.pivotX + parentDelta.x();
        entity.transform.pivotY = m_dragStartTransform.pivotY + parentDelta.y();
        break;

    case DragKind::Rotate:
    {
        const bool aroundCenter = modifiers.testFlag(Qt::AltModifier);
        const QPointF center = aroundCenter ? m_dragCenterStart : m_dragPivotStart;
        const double startAngle =
            aroundCenter ? m_dragAngleCenter : m_dragAnglePivot;
        double delta = std::atan2(mouseParent.y() - center.y(),
                                  mouseParent.x() - center.x())
                       - startAngle;
        delta = std::remainder(delta, 2.0 * std::numbers::pi);
        double degrees = qRadiansToDegrees(delta);

        if (snapActive)
        {
            const double targetRotation =
                snapAngle(m_dragStartTransform.rotation + degrees,
                          modifiers.testFlag(Qt::ShiftModifier));
            degrees = targetRotation - m_dragStartTransform.rotation;
        }

        const gizmo::DragResult result = gizmo::rotate(
            m_dragStartTransform, m_dragStartBounds, degrees, aroundCenter);
        entity.transform.x = result.transform.x;
        entity.transform.y = result.transform.y;
        entity.transform.rotation = result.transform.rotation;
        break;
    }

    case DragKind::Scale:
    {
        const bool uniform = modifiers.testFlag(Qt::ShiftModifier);
        QPointF mouseLocal =
            gizmo::worldToLocal(m_dragStartTransform, mouseParent)
            + m_dragGrabOffset;
        if (snapActive && m_dragSnapRoot && m_snapPixels > 0)
        {
            // Snap the dragged corner's world position so the scaled box stays
            // on the project pixel grid.  cornerScale() keeps the opposite
            // corner fixed and keeps the sign of the scale factors.
            const QPointF cornerWorld =
                gizmo::localToWorld(m_dragStartTransform, mouseLocal);
            mouseLocal = gizmo::worldToLocal(
                m_dragStartTransform,
                QPointF(snapPixel(cornerWorld.x()), snapPixel(cornerWorld.y())));
        }
        const gizmo::DragResult result = gizmo::cornerScale(
            m_dragStartTransform, m_dragStartBounds, m_dragHandle, mouseLocal,
            uniform, true);
        entity.transform.x = result.transform.x;
        entity.transform.y = result.transform.y;
        entity.transform.scaleX = result.transform.scaleX;
        entity.transform.scaleY = result.transform.scaleY;
        clampCameraScale(entity);
        break;
    }

    case DragKind::CenterScale:
    {
        const bool uniform = modifiers.testFlag(Qt::ShiftModifier);
        QPointF mouseLocal = gizmo::worldToLocalAxesPixels(
            m_dragStartTransform, mouseParent, m_dragViewScale);
        if (snapActive && m_snapPixels > 0)
        {
            // The center handle lives in pixel-scaled local axes, so rounding
            // here makes the handle offset a whole number of pixels while the
            // quadrant sign still drives negative scale.
            mouseLocal = QPointF(snapPixel(mouseLocal.x()),
                                 snapPixel(mouseLocal.y()));
        }
        const gizmo::DragResult result =
            gizmo::centerScale(m_dragStartTransform, mouseLocal, uniform);
        entity.transform.scaleX = result.transform.scaleX;
        entity.transform.scaleY = result.transform.scaleY;
        clampCameraScale(entity);
        break;
    }

    case DragKind::Pan:
    case DragKind::None:
        return;
    }

    m_dragChanged = !transformsEqual(entity.transform, m_dragStartTransform);
    invalidateScene();
    update();
}

void ViewportWidget::finishGizmoDrag()
{
    const DragKind kind = m_dragKind;
    const Uuid uuid = m_dragEntity;
    const bool changed = m_dragChanged;

    m_dragKind = DragKind::None;
    m_dragEntity.clear();
    m_dragChanged = false;
    m_dragGrabOffset = QPointF();

    if (!m_document || !changed || uuid.isEmpty())
    {
        update();
        return;
    }

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd())
    {
        update();
        return;
    }

    const QString name = it.value().name.isEmpty() ? uuid : it.value().name;
    QString action;
    switch (kind)
    {
    case DragKind::MoveX:
    case DragKind::MoveY:
        action = QStringLiteral("move");
        break;
    case DragKind::Rotate:
        action = QStringLiteral("rotate");
        break;
    case DragKind::Scale:
    case DragKind::CenterScale:
        action = QStringLiteral("scale");
        break;
    case DragKind::Pivot:
        action = QStringLiteral("pivot");
        break;
    case DragKind::Pan:
    case DragKind::None:
        break;
    }

    if (!action.isEmpty())
        m_document->commitEntity(
            uuid, QStringLiteral("pvf: %1 entity \"%2\"").arg(action, name));

    invalidateScene();
    update();
}

void ViewportWidget::cancelGizmoDrag()
{
    if (m_document && !m_dragEntity.isEmpty())
    {
        auto it = m_document->mutableProject().entities.find(m_dragEntity);
        if (it != m_document->mutableProject().entities.end())
        {
            it.value().transform.x = m_dragStartTransform.x;
            it.value().transform.y = m_dragStartTransform.y;
            it.value().transform.rotation = m_dragStartTransform.rotation;
            it.value().transform.scaleX = m_dragStartTransform.scaleX;
            it.value().transform.scaleY = m_dragStartTransform.scaleY;
            it.value().transform.pivotX = m_dragStartTransform.pivotX;
            it.value().transform.pivotY = m_dragStartTransform.pivotY;
        }
    }

    const bool wasPanning = m_dragKind == DragKind::Pan;
    m_dragKind = DragKind::None;
    m_dragEntity.clear();
    m_dragChanged = false;
    m_dragGrabOffset = QPointF();
    if (wasPanning)
        m_panning = false;
    invalidateScene();
    update();
}

void ViewportWidget::invalidateScene()
{
    m_sceneValid = false;
}

QImage ViewportWidget::ensureSceneImage(const QSize& size)
{
    if (!m_document)
        return {};

    const QSize clamped(
        std::clamp(size.width(), 1, kMaxRenderDimension),
        std::clamp(size.height(), 1, kMaxRenderDimension));
    const int frame = m_document->playhead();
    if (m_sceneValid && m_sceneSize == clamped && m_sceneCamera == m_cameraUuid
        && m_sceneFrame == frame)
        return m_sceneImage;

    // See grabFrame(): live gizmo edits are not representable by the stored
    // frame meta until the drag commits. A plain click that only shows the
    // gizmo must keep using the cached frame; only actual movement bypasses it.
    const bool liveEdit = m_dragKind != DragKind::None
                          && m_dragKind != DragKind::Pan && m_dragChanged;

    // The workbench cache stores whole frames, possibly at a lower resolution
    // than the viewport; drawImage() scales it back up.
    if (m_sceneLookup && !liveEdit) {
        bool hit = false;
        const QImage cached = m_sceneLookup(frame, m_cameraUuid, &hit);
        if (hit && !cached.isNull()) {
            m_sceneImage = cached;
            m_sceneSize = clamped;
            m_sceneCamera = m_cameraUuid;
            m_sceneFrame = frame;
            m_sceneValid = true;
            return m_sceneImage;
        }
    }

    bool mediaMissing = false;
    m_sceneImage = renderScene(clamped, &mediaMissing);
    m_sceneSize = clamped;
    m_sceneCamera = m_cameraUuid;
    m_sceneFrame = frame;
    m_sceneValid = true;

    if (!liveEdit && !mediaMissing && m_sceneStore && !m_sceneImage.isNull())
        m_sceneStore(frame, m_cameraUuid, m_sceneImage);
    return m_sceneImage;
}

void ViewportWidget::drawSelection(QPainter& painter, const Uuid& uuid)
{
    if (!m_document || !m_showGizmo)
        return;

    const auto it = m_document->project().entities.constFind(uuid);
    if (it == m_document->project().entities.constEnd() || it.value().locked)
        return;

    const model::Entity& entity = it.value();
    const QTransform parentToWidget = parentToWidgetTransform(uuid);
    const double viewScale = viewScaleFor(parentToWidget);
    const gizmo::Transform transform = gizmoTransform(entity);
    const gizmo::Bounds bounds = gizmoBounds(entity);

    painter.save();
    if (entity.hidden)
        painter.setOpacity(0.45);

    const auto screenPosition = [&](gizmo::Handle handle) {
        return parentToWidget.map(
            gizmo::handlePosition(handle, transform, bounds, viewScale));
    };

    // Selection box.
    QPolygonF box;
    box << parentToWidget.map(gizmo::localToWorld(transform, QPointF(bounds.left, bounds.top)))
        << parentToWidget.map(gizmo::localToWorld(transform, QPointF(bounds.right, bounds.top)))
        << parentToWidget.map(gizmo::localToWorld(transform, QPointF(bounds.right, bounds.bottom)))
        << parentToWidget.map(gizmo::localToWorld(transform, QPointF(bounds.left, bounds.bottom)));
    painter.setPen(QPen(QColor(116, 206, 255, 175), 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(box);

    const QPointF pivotWidget =
        parentToWidget.map(gizmo::worldPivot(transform));

    // Local X/Y move arrows.
    drawArrowLine(painter, pivotWidget, screenPosition(gizmo::Handle::MoveX),
                  kAxisXColor);
    drawArrowLine(painter, pivotWidget, screenPosition(gizmo::Handle::MoveY),
                  kAxisYColor);

    // Office corner scale handles: small squares inside each corner.
    painter.setPen(QPen(QColor(18, 21, 26), 1.0));
    painter.setBrush(QColor(246, 249, 255));
    for (int corner = 0; corner < 4; ++corner)
    {
        const QPointF position = screenPosition(scaleHandleForCorner(corner));
        painter.drawRect(QRectF(position.x() - 3.5, position.y() - 3.5, 7.0, 7.0));
    }

    // Center scale square in the entity's local +X/+Y quadrant.
    const QPointF centerScale = screenPosition(gizmo::Handle::CenterScale);
    painter.setPen(QPen(kSelectionColor, 1.2));
    painter.setBrush(QColor(34, 46, 58));
    painter.drawRect(QRectF(centerScale.x() - 4.5, centerScale.y() - 4.5, 9.0,
                            9.0));
    painter.setPen(QPen(QColor(116, 206, 255, 90), 1.0, Qt::DotLine));
    painter.drawLine(pivotWidget, centerScale);

    // Curved double arrows outside the corners.
    for (int corner = 0; corner < 4; ++corner)
    {
        const QPointF position = screenPosition(rotateHandleForCorner(corner));
        drawRotateGlyph(painter, position, transform.rotation);
    }

    // Pivot dot.
    painter.setPen(QPen(QColor(18, 21, 26), 1.5));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(pivotWidget, 4.0, 4.0);

    painter.restore();
}

void ViewportWidget::drawCameraFrame(QPainter& painter, const Uuid& uuid,
                                     const QTransform& projectToWidget) const
{
    if (!m_document)
        return;

    const model::Project& project = m_document->project();
    const auto it = project.entities.constFind(uuid);
    if (it == project.entities.constEnd()
        || it.value().content.kind != ContentKind::Camera)
        return;

    const double projectWidth =
        double(std::max(1, project.settings.width));
    const double projectHeight =
        double(std::max(1, project.settings.height));

    render2::SceneWorld world(project, m_document->assets());
    anim::Cache cache;
    const anim::Pose pose =
        anim::evaluateEntity(project, uuid, m_document->playhead(), world, cache);

    QPolygonF source;
    source << QPointF(-projectWidth / 2.0, -projectHeight / 2.0)
           << QPointF(projectWidth / 2.0, -projectHeight / 2.0)
           << QPointF(projectWidth / 2.0, projectHeight / 2.0)
           << QPointF(-projectWidth / 2.0, projectHeight / 2.0);
    const QPolygonF quad = projectToWidget.map(pose.world.map(source));
    if (quad.size() < 4)
        return;

    painter.save();
    if (it.value().hidden)
        painter.setOpacity(0.45);

    painter.setPen(QPen(QColor(98, 206, 230), 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(quad);

    QPointF titleAnchor = quad.at(0);
    for (const QPointF& point : quad)
    {
        if (point.y() < titleAnchor.y()
            || (qFuzzyCompare(point.y(), titleAnchor.y())
                && point.x() < titleAnchor.x()))
            titleAnchor = point;
    }

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    const QString name = it.value().name.isEmpty() ? uuid : it.value().name;
    const QString title = QStringLiteral("相机 · %1").arg(name);
    const QFontMetrics metrics(font);
    const int titleWidth = metrics.horizontalAdvance(title) + 12;
    QRectF titleRect(titleAnchor.x() + 4.0, titleAnchor.y() - 20.0, titleWidth,
                     17.0);
    titleRect.moveLeft(std::clamp(titleRect.left(), 2.0,
                                  std::max(2.0, double(width()) - titleWidth - 2.0)));
    titleRect.moveTop(std::clamp(titleRect.top(), 2.0,
                                 std::max(2.0, double(height()) - 19.0)));
    painter.fillRect(titleRect, QColor(14, 20, 24, 215));
    painter.setPen(QPen(QColor(98, 206, 230), 1.0));
    painter.drawRect(titleRect);
    painter.setPen(QColor(155, 231, 255));
    painter.drawText(titleRect.adjusted(6.0, 0.0, -6.0, 0.0),
                     Qt::AlignLeft | Qt::AlignVCenter, title);

    painter.restore();
}

void ViewportWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kWidgetBackground);

    if (!m_document)
    {
        painter.setPen(QColor(130, 136, 148));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("未打开工程"));
        return;
    }

    const QRectF rect = viewRect();
    if (rect.width() < 1.0 || rect.height() < 1.0)
        return;

    const QSize imageSize(std::max(1, int(std::lround(rect.width()))),
                          std::max(1, int(std::lround(rect.height()))));
    const QImage scene = ensureSceneImage(imageSize);
    if (!scene.isNull())
        painter.drawImage(rect, scene);

    const model::Project& project = m_document->project();
    const QRectF canvas(0.0, 0.0, std::max(1, project.settings.width),
                        std::max(1, project.settings.height));
    const QPolygonF canvasPolygon =
        projectToWidgetTransform().map(QPolygonF(canvas));
    painter.setPen(QPen(kCanvasBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(canvasPolygon);

    const Uuid selected = m_document->selection();
    if (!selected.isEmpty() && m_showGizmo)
    {
        drawCameraFrame(painter, selected, projectToWidgetTransform());
        drawSelection(painter, selected);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    const QPointF position = event->position();

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spaceHeld))
    {
        m_panning = true;
        m_panLast = position;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_document && m_showGizmo)
    {
        const Uuid selected = m_document->selection();
        if (!selected.isEmpty())
        {
            const auto it = m_document->project().entities.constFind(selected);
            if (it != m_document->project().entities.constEnd()
                && !it.value().locked)
            {
                const QTransform parentToWidget =
                    parentToWidgetTransform(selected);
                const double viewScale = viewScaleFor(parentToWidget);
                const gizmo::Transform transform = gizmoTransform(it.value());
                const QPointF pivotWidget =
                    parentToWidget.map(gizmo::worldPivot(transform));

                if (pointDistance(position, pivotWidget) <= kPivotHitRadius)
                {
                    startGizmoDrag(selected, DragKind::Pivot,
                                   gizmo::Handle::CenterScale, position);
                    event->accept();
                    return;
                }

                if (const auto handle =
                        hitGizmoHandle(position, it.value(), parentToWidget,
                                       viewScale))
                {
                    DragKind kind = DragKind::None;
                    switch (*handle)
                    {
                    case gizmo::Handle::MoveX:
                        kind = DragKind::MoveX;
                        break;
                    case gizmo::Handle::MoveY:
                        kind = DragKind::MoveY;
                        break;
                    case gizmo::Handle::CenterScale:
                        kind = DragKind::CenterScale;
                        break;
                    case gizmo::Handle::ScaleTopLeft:
                    case gizmo::Handle::ScaleTopRight:
                    case gizmo::Handle::ScaleBottomRight:
                    case gizmo::Handle::ScaleBottomLeft:
                        kind = DragKind::Scale;
                        break;
                    case gizmo::Handle::RotateTopLeft:
                    case gizmo::Handle::RotateTopRight:
                    case gizmo::Handle::RotateBottomRight:
                    case gizmo::Handle::RotateBottomLeft:
                        kind = DragKind::Rotate;
                        break;
                    }

                    if (kind != DragKind::None)
                    {
                        startGizmoDrag(selected, kind, *handle, position);
                        event->accept();
                        return;
                    }
                }
            }
        }
    }

    const Uuid hit = entityAt(position);
    if (m_document)
        m_document->setSelection(hit);
    update();
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF position = event->position();

    if (m_panning)
    {
        m_pan += position - m_panLast;
        m_panLast = position;
        update();
        return;
    }

    if (m_dragKind != DragKind::None)
    {
        applyGizmoDrag(position, event->modifiers());
        return;
    }

    updateHoverCursor(position);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasPanning = m_panning;
    if (m_panning)
    {
        m_panning = false;
        unsetCursor();
    }

    if (m_dragKind != DragKind::None && m_dragKind != DragKind::Pan)
        finishGizmoDrag();

    if (wasPanning)
        updateHoverCursor(event->position());

    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_document)
    {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        event->ignore();
        return;
    }

    const QPointF anchor = event->position();
    const QPointF projectAnchor = widgetToProject(anchor);
    const double factor = std::pow(1.2, delta / 120.0);
    const double newZoom = std::clamp(m_zoom * factor, 0.05, 64.0);
    if (qFuzzyCompare(newZoom, m_zoom))
    {
        event->ignore();
        return;
    }

    m_zoom = newZoom;
    const QPointF mapped = projectToWidget(projectAnchor);
    m_pan += anchor - mapped;
    invalidateScene();
    update();
    event->accept();
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    invalidateScene();
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        m_spaceHeld = true;
        if (!m_panning)
            setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape && m_dragKind != DragKind::None)
    {
        cancelGizmoDrag();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        m_spaceHeld = false;
        if (!m_panning)
            unsetCursor();
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void ViewportWidget::updateHoverCursor(const QPointF& widgetPos)
{
    if (!m_document || !m_showGizmo)
    {
        unsetCursor();
        return;
    }

    const Uuid selected = m_document->selection();
    if (!selected.isEmpty())
    {
        const auto it = m_document->project().entities.constFind(selected);
        if (it != m_document->project().entities.constEnd()
            && !it.value().locked)
        {
            const QTransform parentToWidget = parentToWidgetTransform(selected);
            const double viewScale = viewScaleFor(parentToWidget);
            const gizmo::Transform transform = gizmoTransform(it.value());
            const QPointF pivotWidget =
                parentToWidget.map(gizmo::worldPivot(transform));
            if (pointDistance(widgetPos, pivotWidget) <= kPivotHitRadius)
            {
                setCursor(Qt::SizeAllCursor);
                return;
            }

            if (const auto handle = hitGizmoHandle(
                    widgetPos, it.value(), parentToWidget, viewScale))
            {
                switch (*handle)
                {
                case gizmo::Handle::MoveX:
                case gizmo::Handle::MoveY:
                case gizmo::Handle::CenterScale:
                    setCursor(Qt::SizeAllCursor);
                    return;
                case gizmo::Handle::ScaleTopLeft:
                case gizmo::Handle::ScaleBottomRight:
                    setCursor(Qt::SizeFDiagCursor);
                    return;
                case gizmo::Handle::ScaleTopRight:
                case gizmo::Handle::ScaleBottomLeft:
                    setCursor(Qt::SizeBDiagCursor);
                    return;
                case gizmo::Handle::RotateTopLeft:
                case gizmo::Handle::RotateTopRight:
                case gizmo::Handle::RotateBottomRight:
                case gizmo::Handle::RotateBottomLeft:
                    setCursor(Qt::CrossCursor);
                    return;
                }
            }
        }
    }

    if (!entityAt(widgetPos).isEmpty())
    {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    unsetCursor();
}

} // namespace pvf::next
