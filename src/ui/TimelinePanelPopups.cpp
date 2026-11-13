#include "ui/TimelinePanelPopups.h"

#include "ui/TimelinePanel.h"

namespace pvf {

TimelinePanel* TimelinePanel::openPopup(EntityId ownerId)
{
    if (!sessionMode() || ownerId == kInvalidEntity
        || ownerId == m_owner)
        return nullptr;
    if (m_popups.contains(ownerId))
    {
        ClipPopup* popup = m_popups.value(ownerId);
        popup->syncGeometry();
        popup->raise();
        return popup->panel();
    }
    const Entity* entity = entityById(ownerId);
    if (!entity)
        return nullptr;
    if (depthOf(ownerId) >= 8)
        return nullptr;
    if (clipRectFor(ownerId).isEmpty())
    {
        ensureFrameVisible(clipStartOf(*entity));
    }
    auto* popup = sessionMode() && m_adapter
                      ? new ClipPopup(ownerId, this, m_adapter)
                      : nullptr;
    if (!popup)
        return nullptr;
    m_popups.insert(ownerId, popup);
    popup->panel()->setCurrentFrame(m_frame);
    popup->syncGeometry();
    return popup->panel();
}

void TimelinePanel::closeAllPopups()
{
    const auto popups = m_popups;
    m_popups.clear();
    for (ClipPopup* popup : popups)
    {
        if (!popup)
            continue;
        if (popup->panel())
            popup->panel()->closeAllPopups();
        popup->hide();
        popup->deleteLater();
    }
}

void TimelinePanel::syncPopups()
{
    for (auto it = m_popups.begin(); it != m_popups.end(); ++it)
    {
        ClipPopup* popup = it.value();
        if (!popup)
            continue;
        if (!entityById(it.key()) || clipRectFor(it.key()).isEmpty())
        {
            popup->hide();
            continue;
        }
        popup->syncGeometry();
        if (popup->panel())
            popup->panel()->syncPopups();
    }
}

void TimelinePanel::removePopup(EntityId ownerId)
{
    m_popups.remove(ownerId);
}

void TimelinePanel::syncPopupGeometry()
{
    syncPopups();
}

} // namespace pvf
