#include "PanelGeometry.h"

#include <algorithm>

namespace pvf::panels {

QRect defaultDockRect(const QPoint& anchor, const QSize& parentSize,
                      const QSize& preferredSize, const QSize& minimumSize)
{
    const int width = std::min(preferredSize.width(),
                               std::max(minimumSize.width(),
                                        parentSize.width() - 16));
    const int height = std::min(preferredSize.height(),
                                std::max(minimumSize.height(),
                                         parentSize.height() - 16));
    return clampToParent(QRect(anchor, QSize(width, height)), minimumSize,
                         parentSize);
}

QRect clampToParent(const QRect& desired, const QSize& minimumSize,
                    const QSize& parentSize)
{
    QSize size = desired.size().expandedTo(minimumSize);
    size.setWidth(std::min(size.width(),
                           std::max(minimumSize.width(), parentSize.width())));
    size.setHeight(std::min(size.height(),
                            std::max(minimumSize.height(), parentSize.height())));

    const int maxX = std::max(8, parentSize.width() - size.width() - 8);
    const int maxY = std::max(8, parentSize.height() - size.height() - 8);
    return QRect(QPoint(std::clamp(desired.x(), 8, maxX),
                        std::clamp(desired.y(), 8, maxY)),
                 size);
}

namespace {

QRect screenOrFallback(const QRect& desired, const QList<QRect>& availableScreens,
                       const QRect& primaryScreen, int* bestIndex)
{
    int bestArea = 0;
    *bestIndex = -1;
    for (int i = 0; i < availableScreens.size(); ++i)
    {
        const QRect intersection = desired.intersected(availableScreens.at(i));
        const int area = intersection.width() * intersection.height();
        if (area > bestArea)
        {
            bestArea = area;
            *bestIndex = i;
        }
    }
    if (*bestIndex >= 0)
        return availableScreens.at(*bestIndex);
    if (primaryScreen.isValid())
        return primaryScreen;
    if (!availableScreens.isEmpty())
        return availableScreens.first();
    return QRect();
}

} // namespace

QRect clampToScreens(const QRect& desired, const QSize& minimumSize,
                     const QList<QRect>& availableScreens,
                     const QRect& primaryScreen)
{
    int bestIndex = -1;
    const QRect target =
        screenOrFallback(desired, availableScreens, primaryScreen, &bestIndex);

    QSize size = desired.size().expandedTo(minimumSize);
    if (target.isValid())
    {
        size.setWidth(std::min(size.width(), target.width()));
        size.setHeight(std::min(size.height(), target.height()));
    }

    QRect result(desired.topLeft(), size);
    if (!target.isValid())
        return result;

    if (bestIndex < 0)
    {
        result.moveCenter(target.center());
        result.moveLeft(std::clamp(result.left(), target.left(),
                                   target.left() + target.width() - size.width()));
        result.moveTop(std::clamp(result.top(), target.top(),
                                  target.top() + target.height() - size.height()));
        return result;
    }

    result.moveLeft(std::clamp(result.left(), target.left(),
                               target.left() + target.width() - size.width()));
    result.moveTop(std::clamp(result.top(), target.top(),
                              target.top() + target.height() - size.height()));
    return result;
}

bool intersectsAnyScreen(const QRect& rect, const QList<QRect>& availableScreens)
{
    for (const QRect& screen : availableScreens)
    {
        if (screen.intersects(rect))
            return true;
    }
    return false;
}

} // namespace pvf::panels
