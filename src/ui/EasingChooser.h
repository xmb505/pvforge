#pragma once

#include "core/Easing.h"

#include <QPoint>
#include <QString>

class QWidget;

namespace pvf {
namespace EasingChooser {

QString displayName(const EasingSpec& spec);

// Shows the visual template grid. Returns true when the user picked a
// template or accepted the custom bezier dialog.
bool choose(QWidget* parent, const EasingSpec& current, const QPoint& globalPos,
            EasingSpec* result, const QString& title = QString());

} // namespace EasingChooser
} // namespace pvf
