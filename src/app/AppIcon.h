#pragma once

#include <QIcon>

namespace pvf {

// The application/workbench cube icon, used for the window and the
// viewport workbench button.
QIcon makeCubeIcon();

// Clip status icons: eye (with a slash when hidden) and lock.
QIcon makeEyeIcon(bool hidden);
QIcon makeLockIcon(bool locked);

// Vector shape thumbnails for the workbench and properties panel.
QIcon makeShapeIcon(int shape);

} // namespace pvf
