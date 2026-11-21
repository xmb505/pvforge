#pragma once

#include "model/Easing.h"

#include <QMenu>

namespace pvf {

class EasingPicker;

// Human name for a model easing type/ spec ("缓入缓出 · 三次").  The technical
// name is always available as the second line of a tooltip.
QString easingDisplayName(const model::EasingSpec& spec);
QString easingDisplayName(const QString& type);

// Non-blocking popup used by the properties panel.  The picker lives inside a
// QWidgetAction, so the caller can connect to its signals directly.  The menu
// deletes itself when it closes.
class EasingPopup : public QMenu
{
    Q_OBJECT

public:
    explicit EasingPopup(const model::EasingSpec& current,
                         QWidget* parent = nullptr);

    EasingPicker* picker() const { return m_picker; }

signals:
    void picked(const model::EasingSpec& spec);
    void customRequested();

private:
    EasingPicker* m_picker = nullptr;
};

} // namespace pvf
