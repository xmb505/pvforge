#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace pvf {

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    QComboBox* m_language = nullptr;
    QSpinBox* m_snapPixels = nullptr;
    QCheckBox* m_edgeCenterSnap = nullptr;
    QCheckBox* m_autoKey = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_fps = nullptr;
    QDoubleSpinBox* m_timelineZoom = nullptr;
    QCheckBox* m_packAssets = nullptr;
};

} // namespace pvf
