#include "Viewport.h"

#include "app/EditorSession.h"
#include "next/app/ProjectDocument.h"
#include "next/core/Document.h"

namespace pvf {

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
    return sessionGizmoGeometry(it.value()).handleCenter(handle);
}

QPointF Viewport::gizmoCornerForTest(const Uuid& uuid, int visualCorner) const
{
    if (!m_session)
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return {};
    const SessionGizmoGeometry geometry = sessionGizmoGeometry(it.value());
    const int local = gizmo::localCornerForVisualCorner(
        visualCorner, geometry.flippedX, geometry.flippedY);
    return geometry.box.at(local);
}

QRectF Viewport::gizmoRotateHitRectForTest(const Uuid& uuid,
                                           gizmo::Handle handle) const
{
    if (!m_session)
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return {};
    const QPointF center = sessionGizmoGeometry(it.value()).handleCenter(handle);
    const double half = gizmo::kRotateHitSize / 2.0;
    return QRectF(center.x() - half, center.y() - half, gizmo::kRotateHitSize,
                  gizmo::kRotateHitSize);
}

double Viewport::gizmoRotateGlyphAngleForTest(const Uuid& uuid,
                                              int visualCorner) const
{
    if (!m_session)
        return 0.0;
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return 0.0;
    return sessionGizmoGeometry(it.value()).rotateAngle[((visualCorner % 4) + 4) % 4];
}

QPointF Viewport::gizmoOppositeCornerForTest(const Uuid& uuid,
                                             gizmo::Handle handle) const
{
    if (!m_session || !gizmo::isRotateHandle(handle))
        return {};
    const auto it = m_session->project().entities.constFind(uuid);
    if (it == m_session->project().entities.constEnd())
        return {};
    const SessionGizmoGeometry geometry = sessionGizmoGeometry(it.value());
    const int visual = (gizmo::visualCornerForRotateHandle(handle) + 2) % 4;
    const int local = gizmo::localCornerForVisualCorner(
        visual, geometry.flippedX, geometry.flippedY);
    return geometry.box.at(local);
}

std::optional<gizmo::Handle> Viewport::gizmoHandleAtForTest(const QPointF& pos) const
{
    return hitSessionGizmo(pos);
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
