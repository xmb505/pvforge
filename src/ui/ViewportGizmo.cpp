#include "Viewport.h"

#include "ui/ViewportInternal.h"

#include "anim/Anim.h"
#include "app/EditorSession.h"
#include "assets/AssetStore.h"
#include "core/TextBlock.h"
#include "core/TextStyle.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"
#include "render2/Render.h"

#include <algorithm>
#include <cmath>

namespace pvf {

using ui::detail::localCorner;
using ui::detail::localCornerPoint;
using ui::detail::nearestFeatureSnap;
using ui::detail::rotateVector;
using ui::detail::solverDrivesProperty;
using ui::detail::textStyleData;
using ui::detail::trackValueOr;
using ui::detail::transformsEqual;

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
    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    const anim::Pose pose =
        anim::evaluateEntity(project, entity.uuid, playhead, world, cache);

    QTransform local = pose.world;
    if (entity.space != model::EntitySpace::Canvas
        && !entity.parentUuid.isEmpty()
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
    if (!m_session || entity.space == model::EntitySpace::Canvas
        || entity.parentUuid.isEmpty())
        return sessionWorldToScreen();

    const model::Project& project = m_session->project();
    const auto parentIt = project.entities.constFind(entity.parentUuid);
    if (parentIt == project.entities.constEnd())
        return sessionWorldToScreen();

    anim::Cache& cache = m_session->animCache();
    render2::SceneWorld world(project, m_session->assets(), &cache);
    const anim::Pose pose = anim::evaluateEntity(
        project, entity.parentUuid, m_session->playhead(), world, cache);
    return pose.world * sessionWorldToScreen();
}

double Viewport::sessionViewScaleFor(const QTransform& parentToWidget) const
{
    return std::max(1e-6, std::sqrt(std::abs(parentToWidget.determinant())));
}

QPointF Viewport::SessionGizmoGeometry::handleCenter(gizmo::Handle handle) const
{
    switch (handle)
    {
    case gizmo::Handle::MoveX:
        return moveX;
    case gizmo::Handle::MoveY:
        return moveY;
    case gizmo::Handle::CenterScale:
        return centerScale;
    case gizmo::Handle::ScaleTopLeft:
        return scaleCenter[0];
    case gizmo::Handle::ScaleTopRight:
        return scaleCenter[1];
    case gizmo::Handle::ScaleBottomRight:
        return scaleCenter[2];
    case gizmo::Handle::ScaleBottomLeft:
        return scaleCenter[3];
    default:
        return rotateCenter[gizmo::visualCornerForRotateHandle(handle)];
    }
}

Viewport::SessionGizmoGeometry Viewport::sessionGizmoGeometry(
    const model::Entity& entity) const
{
    SessionGizmoGeometry geometry;
    geometry.parentToWidget = sessionParentToWidget(entity);
    geometry.transform = sessionGizmoTransform(entity);
    geometry.bounds = sessionGizmoBounds(entity);
    geometry.flippedX = geometry.transform.scaleX < 0.0;
    geometry.flippedY = geometry.transform.scaleY < 0.0;
    geometry.box = ui::detail::gizmoBoxWidget(geometry.transform, geometry.bounds,
                                             geometry.parentToWidget);
    geometry.pivot = geometry.parentToWidget.map(gizmo::worldPivot(geometry.transform));

    const double viewScale = sessionViewScaleFor(geometry.parentToWidget);
    const auto screenPosition = [&](gizmo::Handle handle) {
        return geometry.parentToWidget.map(
            gizmo::handlePosition(handle, geometry.transform, geometry.bounds, viewScale));
    };
    geometry.moveX = screenPosition(gizmo::Handle::MoveX);
    geometry.moveY = screenPosition(gizmo::Handle::MoveY);
    geometry.centerScale = screenPosition(gizmo::Handle::CenterScale);

    // Screen-pixel inset: the scale squares stay 13 px inside the corner and
    // the rotate glyphs ride the corner at 3 px inside, whatever the zoom or
    // the parent's scale is.
    for (int corner = 0; corner < 4; ++corner)
    {
        geometry.scaleCenter[corner] =
            geometry.box.at(corner)
            + ui::detail::cornerInwardDirection(geometry.box, corner) * gizmo::kScaleHandleInset;
    }
    for (int visual = 0; visual < 4; ++visual)
    {
        const int local = gizmo::localCornerForVisualCorner(
            visual, geometry.flippedX, geometry.flippedY);
        geometry.rotateCenter[visual] =
            geometry.box.at(local)
            + ui::detail::cornerInwardDirection(geometry.box, local) * gizmo::kRotateHandleInset;
        geometry.rotateAngle[visual] = ui::detail::rotateGlyphAngle(geometry.box, local);
    }
    return geometry;
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

    // Hit testing happens in widget space: the rotate handles use 18 px
    // axis-aligned squares and the offsets never pass through the parent
    // matrix, so a rotated/non-uniform parent cannot deform the targets.
    const SessionGizmoGeometry geometry = sessionGizmoGeometry(it.value());

    std::optional<gizmo::Handle> best;
    double bestDistance = 0.0;
    const auto consider = [&](gizmo::Handle handle, bool square, double extent) {
        const QPointF center = geometry.handleCenter(handle);
        const double dx = pos.x() - center.x();
        const double dy = pos.y() - center.y();
        if (square ? (std::abs(dx) > extent || std::abs(dy) > extent)
                   : std::hypot(dx, dy) > extent)
            return;
        const double distance = std::hypot(dx, dy);
        if (!best || distance < bestDistance)
        {
            best = handle;
            bestDistance = distance;
        }
    };

    for (int visual = 0; visual < 4; ++visual)
        consider(gizmo::rotateHandleForVisualCorner(visual), true,
                 gizmo::kRotateHitSize / 2.0);

    static constexpr gizmo::Handle kScaleHandles[] = {
        gizmo::Handle::ScaleTopLeft, gizmo::Handle::ScaleTopRight,
        gizmo::Handle::ScaleBottomRight, gizmo::Handle::ScaleBottomLeft};
    for (gizmo::Handle handle : kScaleHandles)
        consider(handle, false, gizmo::kHitRadius);

    consider(gizmo::Handle::CenterScale, false, gizmo::kHitRadius);

    if (best)
        return best;

    const auto hitsAxis = [&](gizmo::Handle tipHandle) {
        return ui::detail::distanceToSegment(
                   pos, geometry.pivot, geometry.handleCenter(tipHandle))
               <= gizmo::kHitRadius;
    };
    if (hitsAxis(gizmo::Handle::MoveX))
        return gizmo::Handle::MoveX;
    if (hitsAxis(gizmo::Handle::MoveY))
        return gizmo::Handle::MoveY;

    return std::nullopt;
}

void Viewport::writeSessionGizmoValue(model::Entity& entity,
                                      const QString& property, double value)
{
    if (!m_session || !m_session->document())
        return;
    const int relativeFrame = m_session->playhead() - entity.start;
    if (solverDrivesProperty(entity, property, relativeFrame))
        return;

    ui::detail::setStaticGizmoValue(entity, property, value);

    const bool hasTrack = entity.tracks.contains(property);
    if (!hasTrack && !m_autoKeyEnabled)
        return;
    if (relativeFrame < 0 || relativeFrame >= entity.duration())
        return;
    ui::detail::setTrackKeyValue(entity, property, relativeFrame, value);
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

    // Inkscape rotation gesture: Shift rotates around the opposite visual
    // corner.  Visual corners swap their local identity under negative scale,
    // so compute the local corner through the same mapping the handles use.
    m_sessionDragOppositeCorner = m_sessionDragPivotStart;
    if (kind == SessionDragKind::Rotate)
    {
        const int visual = gizmo::visualCornerForRotateHandle(handle);
        const int local = gizmo::localCornerForVisualCorner(
            (visual + 2) % 4, m_sessionDragTransform.scaleX < 0.0,
            m_sessionDragTransform.scaleY < 0.0);
        m_sessionDragOppositeCorner = gizmo::localToWorld(
            m_sessionDragTransform, localCornerPoint(m_sessionDragBounds, local));
    }
    m_sessionDragRotateOrigin = m_sessionDragPivotStart;
    m_sessionDragAroundCorner = false;
    m_sessionDragLastWidget = pos;
    m_hoveredRotateHandle.reset();

    if (kind == SessionDragKind::Scale)
    {
        m_sessionDragGrabOffset =
            localCorner(m_sessionDragBounds, handle)
            - gizmo::worldToLocal(m_sessionDragTransform, m_sessionDragStartParent);
    }

    m_sessionDragChanged = false;
    m_showGuideX = false;
    m_showGuideY = false;
    setCursor(kind == SessionDragKind::Rotate ? ui::detail::rotateCursor()
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
        // Inkscape semantics: pivot by default, opposite corner while Shift
        // is held.  Alt keeps the legacy "around the box centre" shortcut.
        const bool aroundOpposite = modifiers.testFlag(Qt::ShiftModifier);
        const bool aroundCenter =
            !aroundOpposite && modifiers.testFlag(Qt::AltModifier);
        const QPointF origin = aroundOpposite ? m_sessionDragOppositeCorner
                              : aroundCenter  ? m_sessionDragCenterStart
                                              : m_sessionDragPivotStart;
        const double startAngle =
            std::atan2(m_sessionDragStartParent.y() - origin.y(),
                       m_sessionDragStartParent.x() - origin.x());
        double delta = std::atan2(mouseParent.y() - origin.y(),
                                  mouseParent.x() - origin.x())
                       - startAngle;
        delta = std::remainder(delta, 2.0 * M_PI);
        double degrees = qRadiansToDegrees(delta);

        // PVForge snapAngles (default 0/90/180/270) with a 3 degree catch;
        // Ctrl temporarily disables snapping.  Shift selects the origin, it
        // does not change the snap strength.
        if (snapActive)
        {
            const double targetRotation =
                snapSessionAngle(m_sessionDragTransform.rotation + degrees, false);
            degrees = targetRotation - m_sessionDragTransform.rotation;
        }

        const gizmo::DragResult result =
            (aroundOpposite || aroundCenter)
                ? gizmo::rotateAround(m_sessionDragTransform, origin, degrees)
                : gizmo::rotate(m_sessionDragTransform, degrees, false);
        if (aroundOpposite || aroundCenter)
        {
            writeSessionGizmoValue(entity, QStringLiteral("x"), result.transform.x);
            writeSessionGizmoValue(entity, QStringLiteral("y"), result.transform.y);
        }
        writeSessionGizmoValue(entity, QStringLiteral("rotation"),
                               result.transform.rotation);

        m_sessionDragRotateOrigin = origin;
        m_sessionDragAroundCorner = aroundOpposite;
        m_sessionDragLastWidget = pos;
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
    m_sessionDragOppositeCorner = QPointF();
    m_sessionDragRotateOrigin = QPointF();
    m_sessionDragLastWidget = QPointF();
    m_sessionDragAroundCorner = false;
    m_hoveredRotateHandle.reset();
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
    m_sessionDragOppositeCorner = QPointF();
    m_sessionDragRotateOrigin = QPointF();
    m_sessionDragLastWidget = QPointF();
    m_sessionDragAroundCorner = false;
    m_hoveredRotateHandle.reset();
    m_showGuideX = false;
    m_showGuideY = false;
    unsetCursor();
    invalidateSessionImage();
    update();
}

} // namespace pvf
