#include "EasingChooser.h"

#include "BezierEditorDialog.h"
#include "EasingPicker.h"

#include <QMenu>
#include <QWidgetAction>

namespace pvf {
namespace EasingChooser {

QString displayName(const EasingSpec& spec)
{
    return easingDisplayName(spec.easing);
}

bool choose(QWidget* parent, const EasingSpec& current, const QPoint& globalPos,
            EasingSpec* result)
{
    QMenu menu(parent);
    auto* picker = new EasingPicker(current, &menu);
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(picker);
    menu.addAction(action);

    EasingSpec selected;
    bool picked = false;
    bool custom = false;
    QObject::connect(picker, &EasingPicker::picked, &menu,
                     [&menu, &selected, &picked](const EasingSpec& spec) {
                         selected = spec;
                         picked = true;
                         menu.close();
                     });
    QObject::connect(picker, &EasingPicker::customRequested, &menu,
                     [&menu, &custom] {
                         custom = true;
                         menu.close();
                     });
    menu.exec(globalPos);

    if (custom)
    {
        EasingSpec spec = current;
        if (spec.easing != Easing::Bezier)
        {
            spec.easing = Easing::Bezier;
            spec.inX = 0.42;
            spec.inY = 0.0;
            spec.outX = 0.58;
            spec.outY = 1.0;
        }
        BezierEditorDialog dialog(spec, parent);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        *result = dialog.easing();
        return true;
    }

    if (picked)
    {
        *result = selected;
        return true;
    }
    return false;
}

} // namespace EasingChooser
} // namespace pvf
