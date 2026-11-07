#pragma once

#include <QIcon>

namespace pvf {

// The application/workbench cube icon, used for the window and the
// viewport workbench button.
QIcon makeCubeIcon();

// Row buttons shown on hover in the timeline object list.
QIcon makeEyeIcon(bool hidden);
QIcon makeLockIcon(bool locked);
QIcon makeKeyframeIcon();

} // namespace pvf
