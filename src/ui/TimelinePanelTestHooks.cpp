#include "TimelinePanel.h"

#include "ui/TimelinePanelInternal.h"

#include <QFontMetrics>
#include <QScrollBar>

#include <algorithm>

#ifdef PVFORGE_TEST_HOOKS

namespace pvf {

int TimelinePanel::rulerTickStepForTest() const
{
    int tickStep = 0;
    int labelStep = 0;
    ui::detail::rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
    return tickStep;
}

int TimelinePanel::rulerLabelStepForTest() const
{
    int tickStep = 0;
    int labelStep = 0;
    ui::detail::rulerSteps(m_pixelsPerFrame, &tickStep, &labelStep);
    return labelStep;
}

int TimelinePanel::secondsCellStepForTest() const
{
    return ui::detail::secondsGrid(currentFps(), m_pixelsPerFrame).cellFrames;
}

int TimelinePanel::secondsLabelStepForTest() const
{
    return ui::detail::secondsGrid(currentFps(), m_pixelsPerFrame).labelFrames;
}

EntityId TimelinePanel::rowEntityForTest(int row) const
{
    return entityAtRow(row);
}

int TimelinePanel::entityRowCountForTest() const
{
    int count = 0;
    for (EntityId id : rowIds())
        if (id != kInvalidEntity)
            ++count;
    return count;
}

QString TimelinePanel::rowNameForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->name : QString();
}

QString TimelinePanel::clipNameTextForTest(EntityId id) const
{
    const Entity* entity = entityById(id);
    if (!entity)
        return QString();
    const QRect clip = clipRect(*entity);
    if (clip.isEmpty())
        return QString();
    const bool showButtons = clip.width() >= kInlineButtonsMinWidth;
    const int textLeft = clip.left() + 5
                         + (showButtons ? kButtonStrip + 2 : 0);
    const QRect plus =
        ui::detail::plusButtonRect(clip, width() - kScrollBarSize);
    const int textRight = plus.isEmpty() ? clip.right() - 5 : plus.left() - 4;
    const bool showInfo = clip.width() >= 170;
    const int infoWidth =
        showInfo ? std::min(150, std::max(0, textRight - textLeft) / 2) : 0;
    const int available =
        std::max(0, textRight - textLeft - infoWidth - (showInfo ? 8 : 0));
    return QFontMetrics(font()).elidedText(entity->name, Qt::ElideRight,
                                           available);
}

bool TimelinePanel::clipNameElidedForTest(EntityId id) const
{
    const Entity* entity = entityById(id);
    if (!entity)
        return false;
    const QString shown = clipNameTextForTest(id);
    return !shown.isEmpty() && shown != entity->name;
}

int TimelinePanel::rowStartForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    return entity->data.value(QStringLiteral("modelStart"),
                              clipStartOf(*entity))
        .toInt();
}

int TimelinePanel::rowEndForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    if (!entity)
        return -1;
    return entity->data.value(QStringLiteral("modelEnd"), clipEndOf(*entity))
        .toInt();
}

bool TimelinePanel::rowIsAudioForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity && entity->isAudio();
}

QString TimelinePanel::rowAssetHashForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->data.value(QStringLiteral("assetHash")).toString()
                  : QString();
}

double TimelinePanel::rowAssetSecondsForTest(int row) const
{
    const Entity* entity = entityById(entityAtRow(row));
    return entity ? entity->data
                        .value(QStringLiteral("assetDurationSeconds"))
                        .toDouble()
                  : 0.0;
}

int TimelinePanel::markerFrameForTest(int index) const
{
    return index >= 0 && index < m_snapshot.markers.size()
               ? m_snapshot.markers.at(index).frame
               : -1;
}

int TimelinePanel::rowForEntityForTest(EntityId id) const
{
    const Entity* entity = entityById(id);
    return entity ? rowOfEntity(*entity) : -1;
}

bool TimelinePanel::verticalScrollVisibleForTest() const
{
    return m_vScroll && m_vScroll->isVisible();
}

} // namespace pvf

#endif
