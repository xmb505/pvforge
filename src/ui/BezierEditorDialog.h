#pragma once

#include "core/Easing.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

namespace pvf {

class BezierGraph;

// Mine-imator style custom bezier editor: a unit square with two draggable
// handles. X is clamped to 0..1, Y may overshoot.
class BezierEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BezierEditorDialog(const EasingSpec& spec, QWidget* parent = nullptr);

    EasingSpec easing() const { return m_spec; }

private:
    void updateSpins();
    void updateFromSpins();

    EasingSpec m_spec;
    BezierGraph* m_graph = nullptr;
    QDoubleSpinBox* m_inX = nullptr;
    QDoubleSpinBox* m_inY = nullptr;
    QDoubleSpinBox* m_outX = nullptr;
    QDoubleSpinBox* m_outY = nullptr;
    QCheckBox* m_sync = nullptr;
    bool m_updating = false;
};

} // namespace pvf
