#pragma once

#include <QList>
#include <QRect>
#include <QSize>

// Pure geometry helpers for dockable panels.  Kept free of QWidget so the
// clamps can be unit-tested without a window manager.
namespace pvf::panels {

// Dock rectangle derived from the viewport anchor.  The size follows the old
// workbench rule: prefer 640x560, shrink to the parent minus a 16 px margin,
// never below the minimum.
QRect defaultDockRect(const QPoint& anchor, const QSize& parentSize,
                      const QSize& preferredSize, const QSize& minimumSize);

// Clamp a dock/floating-child rectangle into a parent widget.  The result keeps
// an 8 px margin when the parent has room; smaller parents keep the minimum
// size and top-left anchor (the old overflow behaviour).
QRect clampToParent(const QRect& desired, const QSize& minimumSize,
                    const QSize& parentSize);

// Clamp a top-level window rectangle into the screen with the largest overlap
// with `desired`.  A rectangle that touches no screen is centred on the primary
// screen.  The size never drops below `minimumSize` and never exceeds the
// chosen screen.
QRect clampToScreens(const QRect& desired, const QSize& minimumSize,
                     const QList<QRect>& availableScreens,
                     const QRect& primaryScreen);

bool intersectsAnyScreen(const QRect& rect, const QList<QRect>& availableScreens);

} // namespace pvf::panels
