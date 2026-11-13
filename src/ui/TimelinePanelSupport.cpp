#include "ui/TimelinePanelInternal.h"

#include <QIcon>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace pvf::ui::detail {

QVector<EntityId> childIds(const QVector<Entity>& entities, EntityId parent)
{
    QVector<EntityId> result;
    for (const Entity& entity : entities)
        if (entity.parent == parent)
            result.append(entity.id);
    return result;
}

Entity* findEntity(QVector<Entity>& entities, EntityId id)
{
    for (Entity& entity : entities)
        if (entity.id == id)
            return &entity;
    return nullptr;
}

const Entity* findEntity(const QVector<Entity>& entities, EntityId id)
{
    for (const Entity& entity : entities)
        if (entity.id == id)
            return &entity;
    return nullptr;
}

void collectSubtree(const QVector<Entity>& entities, EntityId id,
                    QSet<EntityId>& out)
{
    if (out.contains(id))
        return;
    out.insert(id);
    for (const Entity& entity : entities)
        if (entity.parent == id)
            collectSubtree(entities, entity.id, out);
}

void rulerSteps(double pixelsPerFrame, int* tickStep, int* labelStep)
{
    *tickStep = 1;
    *labelStep = 5;
    if (pixelsPerFrame < 5.0)
    {
        *tickStep = 5;
        *labelStep = 10;
    }
    if (pixelsPerFrame < 3.0)
    {
        *tickStep = 10;
        *labelStep = 50;
    }
    if (pixelsPerFrame < 1.0)
    {
        *tickStep = 20;
        *labelStep = 100;
    }
    if (pixelsPerFrame < 0.5)
    {
        *tickStep = 50;
        *labelStep = 200;
    }
}

SecondsGrid secondsGrid(int fps, double pixelsPerFrame)
{
    const int safeFps = std::max(1, fps);
    SecondsGrid grid;

    int labelFrames = safeFps;
    while (labelFrames * pixelsPerFrame < 48.0)
        labelFrames *= 2;
    while (labelFrames > 1 && labelFrames % 2 == 0
           && labelFrames * pixelsPerFrame > 240.0)
        labelFrames /= 2;
    grid.labelFrames = labelFrames;

    grid.cellFrames = labelFrames;
    for (int step = 1; step <= labelFrames; ++step)
    {
        if (labelFrames % step != 0)
            continue;
        if (safeFps % step != 0 && step % safeFps != 0)
            continue;
        if (step * pixelsPerFrame >= 5.0)
        {
            grid.cellFrames = step;
            break;
        }
    }
    return grid;
}

QRect plusButtonRect(const QRect& clip, int visibleRight)
{
    const int left = std::max(0, clip.left());
    const int right = std::min(clip.right(), visibleRight);
    if (right - left < 84)
        return QRect();
    return QRect(right - 22, clip.top() + (clip.height() - 16) / 2, 16, 16);
}

QPixmap themedGlyph(const QStringList& names, int size, const QColor& color)
{
    for (const QString& name : names)
    {
        const QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            continue;
        QPixmap pixmap = icon.pixmap(QSize(size, size));
        if (pixmap.isNull())
            continue;
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        return pixmap;
    }
    return QPixmap();
}

} // namespace pvf::ui::detail
